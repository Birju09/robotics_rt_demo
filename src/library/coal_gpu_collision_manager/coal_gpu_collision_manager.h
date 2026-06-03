#include <CL/opencl.hpp>
#include <coal/broadphase/broadphase_collision_manager.h>
#include <coal/collision.h>
#include <coal/collision_object.h>

// GPU based BroadphaseCollisionManager using OpenCL to accelerate broadphase
// collision queries.
// A naive GPU implementation for evaluation on MALI 310 GPU on iMX95.
class CoalGPUCollisionManager : public coal::BroadPhaseCollisionManager {
public:
  CoalGPUCollisionManager();
  ~CoalGPUCollisionManager() override = default;

  /// \copydoc coal::BroadPhaseCollisionManager::registerObjects
  void registerObjects(
      const std::vector<coal::CollisionObject *> &other_objs) override;

  /// \copydoc coal::BroadPhaseCollisionManager::registerObject
  void registerObject(coal::CollisionObject *obj) override;

  /// \copydoc coal::BroadPhaseCollisionManager::unregisterObject
  void unregisterObject(coal::CollisionObject *obj) override;

  /// \copydoc coal::BroadPhaseCollisionManager::setup
  // Allocates persistent GPU buffers and binds static kernel args.
  // Must be called once after all objects are registered.
  void setup() override;

  /// \copydoc coal::BroadPhaseCollisionManager::update
  void update() override {}

  /// \copydoc coal::BroadPhaseCollisionManager::update
  void update(coal::CollisionObject *updated_obj) override {}

  /// \copydoc coal::BroadPhaseCollisionManager::update
  void
  update(const std::vector<coal::CollisionObject *> &updated_objs) override {}

  /// \copydoc coal::BroadPhaseCollisionManager::clear
  void clear() override;

  /// \copydoc coal::BroadPhaseCollisionManager::getObjects
  void getObjects(std::vector<coal::CollisionObject *> &objs) const override;

  /// \copydoc coal::BroadPhaseCollisionManager::collide
  void collide(coal::CollisionObject *obj,
               coal::CollisionCallBackBase *callback) const override;

  /// \copydoc coal::BroadPhaseCollisionManager::distance
  void distance(coal::CollisionObject *obj,
                coal::DistanceCallBackBase *callback) const override;

  /// \copydoc coal::BroadPhaseCollisionManager::collide
  void collide(coal::CollisionCallBackBase *callback) const override;

  /// \copydoc coal::BroadPhaseCollisionManager::distance
  void distance(coal::DistanceCallBackBase *callback) const override;

  /// \copydoc coal::BroadPhaseCollisionManager::collide
  void collide(coal::BroadPhaseCollisionManager *other_manager,
               coal::CollisionCallBackBase *callback) const override;

  /// \copydoc coal::BroadPhaseCollisionManager::distance
  void distance(coal::BroadPhaseCollisionManager *other_manager,
                coal::DistanceCallBackBase *callback) const override;

  /// \copydoc coal::BroadPhaseCollisionManager::empty
  bool empty() const override;

  /// \copydoc coal::BroadPhaseCollisionManager::size
  size_t size() const override;

private:
  cl::Context context_;
  cl::Device device_;
  cl::Program distance_program_;
  cl::Program collide_program_;
  std::vector<coal::CollisionObject *> objects_;

  bool has_unified_memory_ =
      false; // cached at construction, avoids per-call driver query

  // All of the following are mutable because collide/distance overrides are
  // const (required by BroadPhaseCollisionManager), but OpenCL C++ enqueue
  // and setArg calls are non-const on their respective wrapper types.

  mutable cl::CommandQueue queue_;

  // Pre-created kernel objects: avoids per-call string lookup into the
  // compiled program. Args for the self-check kernels are bound once in
  // setup() and never change; one-vs-all/cross args are set per call.
  mutable cl::Kernel k_aabb_distance_;
  mutable cl::Kernel k_overlap_self_;
  mutable cl::Kernel k_overlap_one_vs_all_;
  mutable cl::Kernel k_distance_one_vs_all_;
  mutable cl::Kernel k_overlap_cross_;
  mutable cl::Kernel k_distance_cross_;

  // Persistent buffers for the self-check hot path (OMPL calls this on every
  // validity check). Allocated once in setup(), reused on every collide().
  mutable cl::Buffer buf_aabbs_;
  mutable cl::Buffer buf_overlaps_self_;
  mutable cl::Buffer buf_distances_self_;
  mutable std::vector<float> aabbs_staging_;
  mutable std::vector<cl_uchar> overlaps_staging_;
  mutable std::vector<float> distances_staging_;

  void logKernelTime(const cl::Event &ev, const char *label) const;

  // Extract AABBs from an arbitrary list of collision objects into a flat
  // float array (6 floats per object: min_xyz, max_xyz). The caller must
  // ensure out has capacity for objs.size() * 6 floats.
  static void extractAABBs(const std::vector<coal::CollisionObject *> &objs,
                           float *out);

  // Allocate a plain read-write device buffer of the given byte size.
  cl::Buffer allocBuffer(size_t size) const;

  // Helper method to invoke distance callbacks
  void invokeDistanceCallbacks(coal::DistanceCallBackBase *callback,
                               const std::vector<float> &distances,
                               size_t n) const;
};
