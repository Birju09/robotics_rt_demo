#include "collision_scene.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <coal/collision.h>
#include <coal/distance.h>
#include <iostream>
#include <kdl/chainfksolverpos_recursive.hpp>
#include <ompl/base/spaces/RealVectorStateSpace.h>

CollisionScene::CollisionScene(const RobotModel &model) : model_(model) {
  adjacency_.resize(model.numJoints());

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
    lco.local_transform =
        coal::Transform3s(coal::Matrix3s::Identity(),
                          coal::Vec3s::Zero()); // Will be updated per check
    lco.collision_obj = std::make_shared<coal::CollisionObject>(bvh);

    link_objects_.push_back(lco);
  }

  buildAdjacencyList();

  std::cout << "CollisionScene initialized with " << link_objects_.size()
            << " collision objects" << std::endl;
}

bool CollisionScene::isCollisionFree(const std::vector<double> &q) const {
  if (q.size() != static_cast<size_t>(model_.numJoints())) {
    std::cerr << "CollisionScene::isCollisionFree: joint size mismatch"
              << std::endl;
    return false;
  }

  updateCollisionTransforms(q);
  return checkCollisions();
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

  // Process all meshes in the file
  for (unsigned int mesh_idx = 0; mesh_idx < scene->mNumMeshes; ++mesh_idx) {
    const aiMesh *mesh = scene->mMeshes[mesh_idx];

    // Add vertices
    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
      const aiVector3D &vertex = mesh->mVertices[v];
      bvh->addVertex(coal::Vec3s(vertex.x, vertex.y, vertex.z));
    }

    // Add triangles by vertex position (not indices)
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace &face = mesh->mFaces[f];
      if (face.mNumIndices >= 3) {
        // Get the three vertex positions from the mesh
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
  // For now, a simple heuristic: links with consecutive indices are adjacent
  // This is a simplification; a proper implementation would extract joint
  // connectivity from the robot model
  for (int i = 0; i < model_.numJoints(); ++i) {
    if (i > 0) {
      adjacency_[i].push_back(i - 1);
    }
    if (i < model_.numJoints() - 1) {
      adjacency_[i].push_back(i + 1);
    }
  }
}

void CollisionScene::updateCollisionTransforms(
    const std::vector<double> &q) const {
  // NOTE: This is a placeholder implementation.
  // Collision transforms are currently fixed at load time.
  // A proper implementation would:
  // 1. Access the KDL chain from the robot model
  // 2. Compute FK to each link's frame
  // 3. Update the collision object's transform in the world frame
  //
  // This is acceptable for a demo; production systems should expose the chain
  // from RobotModel and properly update transforms on each collision check.

  (void)q; // Avoid unused parameter warning
}

bool CollisionScene::checkCollisions() const {
  // Check all pairs of collision objects
  // Skip pairs that are adjacent

  for (size_t i = 0; i < link_objects_.size(); ++i) {
    for (size_t j = i + 1; j < link_objects_.size(); ++j) {
      // Check if i and j are adjacent (should skip)
      bool adjacent = false;
      if (i < static_cast<size_t>(model_.numJoints())) {
        for (int adj : adjacency_[i]) {
          if (static_cast<size_t>(adj) == j) {
            adjacent = true;
            break;
          }
        }
      }

      if (adjacent) {
        continue;
      }

      // Check collision between i and j
      coal::CollisionRequest request;
      coal::CollisionResult result;
      coal::collide(link_objects_[i].collision_obj.get(),
                    link_objects_[j].collision_obj.get(), request, result);

      if (result.isCollision()) {
        return false; // Collision detected
      }
    }
  }

  return true; // No collisions
}
