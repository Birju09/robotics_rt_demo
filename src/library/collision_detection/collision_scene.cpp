#include "collision_scene.hpp"

#include "coal_gpu_collision_manager.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <chrono>
#include <coal/broadphase/broadphase_callbacks.h>
#include <coal/broadphase/broadphase_dynamic_AABB_tree.h>
#include <coal/collision.h>
#include <coal/distance.h>
#include <iomanip>
#include <iostream>
#include <kdl/chainfksolverpos_recursive.hpp>
#include <ompl/base/spaces/RealVectorStateSpace.h>
#include <unordered_set>

namespace {

coal::Transform3s kdlToCoal(const KDL::Frame &f) {
  coal::Matrix3s rot;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      rot(r, c) = f.M(r, c);
  return coal::Transform3s(rot, coal::Vec3s(f.p.x(), f.p.y(), f.p.z()));
}

// Broadphase callback that skips adjacent link pairs and runs narrow-phase
// collision on each AABB-overlapping pair. Sets `collided = true` and stops
// early on the first real contact.
struct NarrowPhaseCallback : public coal::CollisionCallBackBase {
  const std::vector<coal::CollisionObject *> &ptrs;
  const std::vector<std::string> &names;
  const std::unordered_set<std::string> &adjacent;
  bool collided = false;

  NarrowPhaseCallback(const std::vector<coal::CollisionObject *> &p,
                      const std::vector<std::string> &n,
                      const std::unordered_set<std::string> &adj)
      : ptrs(p), names(n), adjacent(adj) {}

  bool collide(coal::CollisionObject *o1, coal::CollisionObject *o2) override {
    size_t idx1 = findPtr(o1);
    size_t idx2 = findPtr(o2);
    if (idx1 == SIZE_MAX || idx2 == SIZE_MAX)
      return false;

    if (adjacent.count(names[idx1] + ":::" + names[idx2]) ||
        adjacent.count(names[idx2] + ":::" + names[idx1]))
      return false;

    coal::CollisionRequest req;
    coal::CollisionResult res;
    coal::collide(o1, o2, req, res);
    if (res.isCollision()) {
      collided = true;
      return true;
    }
    return false;
  }

private:
  size_t findPtr(const coal::CollisionObject *obj) const {
    for (size_t i = 0; i < ptrs.size(); ++i)
      if (ptrs[i] == obj)
        return i;
    return SIZE_MAX;
  }
};

} // namespace

CollisionScene::CollisionScene(const RobotModel &model, Mode mode)
    : model_(model) {

  // Load all link meshes and create collision objects
  for (const auto &link_mesh : model.linkMeshes()) {
    auto bvh = loadMesh(link_mesh.mesh_path);
    if (!bvh) {
      std::cerr << "Warning: Failed to load mesh for link "
                << link_mesh.link_name << " from " << link_mesh.mesh_path
                << std::endl;
      continue;
    }

    LinkCollisionObject lco;
    lco.link_name = link_mesh.link_name;
    lco.bvh_model = bvh;
    // Will be updated per check
    lco.local_transform = kdlToCoal(link_mesh.visual_offset);
    lco.collision_obj = std::make_shared<coal::CollisionObject>(bvh);

    link_objects_.push_back(lco);
  }

  buildAdjacencyList();

  if (mode == Mode::GPU) {
    broadphase_ = std::make_unique<CoalGPUCollisionManager>();
  } else {
    broadphase_ = std::make_unique<coal::DynamicAABBTreeCollisionManager>();
  }

  for (const auto &lco : link_objects_) {
    broadphase_->registerObject(lco.collision_obj.get());
  }
  broadphase_->setup();

  std::cout << "CollisionScene initialized with " << link_objects_.size()
            << " collision objects (" << (mode == Mode::GPU ? "GPU" : "CPU")
            << " broadphase)" << std::endl;
}

void CollisionScene::TimingStats::record(double ms) {
  ++count;
  total_ms += ms;
  if (ms < min_ms)
    min_ms = ms;
  if (ms > max_ms)
    max_ms = ms;
}

double CollisionScene::TimingStats::avg_ms() const {
  return count > 0 ? total_ms / static_cast<double>(count) : 0.0;
}

void CollisionScene::TimingStats::print(const char *label) const {
  std::cout << "  " << label << ":"
            << " calls=" << count << std::fixed << std::setprecision(4)
            << "  total=" << total_ms << "ms"
            << "  avg=" << avg_ms() << "ms"
            << "  min=" << (count > 0 ? min_ms : 0.0) << "ms"
            << "  max=" << max_ms << "ms" << std::endl;
}

bool CollisionScene::isCollisionFree(const std::vector<double> &q) const {
  if (q.size() != static_cast<size_t>(model_.numJoints())) {
    std::cerr << "CollisionScene::isCollisionFree: joint size mismatch"
              << std::endl;
    return false;
  }

  using Clock = std::chrono::steady_clock;

  auto t0 = Clock::now();
  updateCollisionTransforms(q);
  auto t1 = Clock::now();
  bool result = checkCollisions();
  auto t2 = Clock::now();

  auto to_ms = [](Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
  };
  update_stats_.record(to_ms(t1 - t0));
  collide_stats_.record(to_ms(t2 - t1));

  return result;
}

void CollisionScene::logTimingStats() const {
  std::cout << "\n=== CollisionScene Timing (" << update_stats_.count
            << " checks) ===" << std::endl;
  update_stats_.print("updateTransforms");
  collide_stats_.print("broadphaseCollide");

  const double total = update_stats_.total_ms + collide_stats_.total_ms;
  const double avg = update_stats_.count > 0
                         ? total / static_cast<double>(update_stats_.count)
                         : 0.0;
  std::cout << std::fixed << std::setprecision(4)
            << "  total (both phases): " << total << "ms"
            << "  avg/check: " << avg << "ms" << std::endl;
}

ompl::base::StateValidityCheckerFn CollisionScene::makeOMPLChecker(
    std::shared_ptr<ompl::base::SpaceInformation> /*si*/) const {
  // Return a lambda that captures 'this' and calls isCollisionFree
  return [this](const ompl::base::State *state) -> bool {
    // Cast state to RealVectorStateSpace::StateType
    auto *rstate = state->as<ompl::base::RealVectorStateSpace::StateType>();
    std::vector<double> q(model_.numJoints());
    for (int i = 0; i < model_.numJoints(); ++i) {
      q[i] = rstate->values[i];
    }
    return isCollisionFree(q);
  };
}

std::shared_ptr<coal::BVHModel<coal::OBBRSS>>
CollisionScene::loadMesh(const std::string &mesh_path) const {
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(
      mesh_path, aiProcess_Triangulate | aiProcess_FlipWindingOrder |
                     aiProcess_GenNormals);

  if (!scene || !scene->HasMeshes()) {
    std::cerr << "Assimp failed to load mesh: " << mesh_path << std::endl;
    return nullptr;
  }

  auto bvh = std::make_shared<coal::BVHModel<coal::OBBRSS>>();
  bvh->beginModel();

  // Process all meshes in the file
  for (unsigned int mesh_idx = 0; mesh_idx < scene->mNumMeshes; ++mesh_idx) {
    const aiMesh *mesh = scene->mMeshes[mesh_idx];

    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace &face = mesh->mFaces[f];
      if (face.mNumIndices == 3) {
        const aiVector3D &v0 = mesh->mVertices[face.mIndices[0]];
        const aiVector3D &v1 = mesh->mVertices[face.mIndices[1]];
        const aiVector3D &v2 = mesh->mVertices[face.mIndices[2]];
        bvh->addTriangle(coal::Vec3s(v0.x, v0.y, v0.z),
                         coal::Vec3s(v1.x, v1.y, v1.z),
                         coal::Vec3s(v2.x, v2.y, v2.z));
      }
    }
  }

  bvh->endModel();
  return bvh;
}

void CollisionScene::buildAdjacencyList() {
  for (const auto &[a, b] : model_.adjacentLinkPairs()) {
    adjacent_pairs_.insert(a + ":::" + b);
    adjacent_pairs_.insert(b + ":::" + a);
  }
}

void CollisionScene::updateCollisionTransforms(
    const std::vector<double> &q) const {
  for (const auto &lco : link_objects_) {
    KDL::Frame link_world;
    if (!model_.fkLink(q, lco.link_name, link_world)) {
      continue;
    }
    // world_T_mesh = world_T_link * link_T_mesh
    coal::Transform3s tf = kdlToCoal(link_world) * lco.local_transform;
    lco.collision_obj->setTransform(tf);
    lco.collision_obj->computeAABB();
  }
  broadphase_->update();
}

bool CollisionScene::checkCollisions() const {
  std::vector<coal::CollisionObject *> ptrs;
  ptrs.reserve(link_objects_.size());
  for (const auto &lco : link_objects_)
    ptrs.push_back(lco.collision_obj.get());

  std::vector<std::string> names;
  names.reserve(link_objects_.size());
  for (const auto &lco : link_objects_)
    names.push_back(lco.link_name);

  NarrowPhaseCallback cb(ptrs, names, adjacent_pairs_);
  broadphase_->collide(&cb);
  return !cb.collided;
}
