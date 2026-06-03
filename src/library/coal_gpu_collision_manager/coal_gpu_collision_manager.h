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
  void setup() override {}

  /// \copydoc coal::BroadPhaseCollisionManager::update
  void update() override {}

  /// \copydoc coal::BroadPhaseCollisionManager::update
  void update(coal::CollisionObject *updated_obj) override {}

  /// \copydoc coal::BroadPhaseCollisionManager::update
  void
  update(const std::vector<coal::CollisionObject *> &updated_objs) override {};

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
  cl::CommandQueue queue_;
  cl::Program distance_program_;
  cl::Program collide_program_;
  std::vector<coal::CollisionObject *> objects_;

  // Helper methods for zero-copy buffer management
  bool supportsHostUnifiedMemory() const;
  cl::Buffer createZeroCopyBuffer(size_t size, void *host_ptr) const;
  void logKernelTime(const cl::Event &ev, const char *label) const;

  // Extract AABBs from an arbitrary list of collision objects.
  static void extractAABBs(const std::vector<coal::CollisionObject *> &objs,
                            std::vector<float> &aabbs);

  // Helper method to invoke distance callbacks
  void invokeDistanceCallbacks(coal::DistanceCallBackBase *callback,
                               const std::vector<float> &distances,
                               size_t n) const;
};