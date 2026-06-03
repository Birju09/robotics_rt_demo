#include "coal_gpu_collision_manager.h"
#include <algorithm>
#include <iostream>
#include <limits>

// Debug logging macro - can be disabled by defining COAL_GPU_NO_DEBUG
#ifdef COAL_GPU_DEBUG_LOG
#define COAL_GPU_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl
#else
#define COAL_GPU_DEBUG(msg)                                                    \
  do {                                                                         \
  } while (0)
#endif

namespace {

// Each 1D work item k maps to exactly one unique pair (i, j) with i < j,
// covering all N*(N-1)/2 pairs with no wasted or divergent work items.
// Mapping: row i is the largest integer where i*(i+1)/2 <= k, then j = k -
// i*(i+1)/2 + i + 1. Output stored at distances[k] in pair-index order.
static const char *kAABBDistanceKernelSrc =
    "__kernel void aabbDistance(__global const float *aabbs,\n"
    "                            __global float *distances,\n"
    "                            const int n) {\n"
    "  const int k = get_global_id(0);\n"
    "\n"
    "  // Map linear pair index k to upper-triangle (i, j) with i < j.\n"
    "  // Row i is the largest i such that i*(i+1)/2 <= k.\n"
    "  int i = (int)((sqrt((float)(8 * k + 1)) - 1.0f) * 0.5f);\n"
    "  int j = k - i * (i + 1) / 2 + i + 1;\n"
    "\n"
    "  // AABB format: [min_x, min_y, min_z, max_x, max_y, max_z]\n"
    "  const float min1_x = aabbs[i * 6 + 0];\n"
    "  const float min1_y = aabbs[i * 6 + 1];\n"
    "  const float min1_z = aabbs[i * 6 + 2];\n"
    "  const float max1_x = aabbs[i * 6 + 3];\n"
    "  const float max1_y = aabbs[i * 6 + 4];\n"
    "  const float max1_z = aabbs[i * 6 + 5];\n"
    "\n"
    "  const float min2_x = aabbs[j * 6 + 0];\n"
    "  const float min2_y = aabbs[j * 6 + 1];\n"
    "  const float min2_z = aabbs[j * 6 + 2];\n"
    "  const float max2_x = aabbs[j * 6 + 3];\n"
    "  const float max2_y = aabbs[j * 6 + 4];\n"
    "  const float max2_z = aabbs[j * 6 + 5];\n"
    "\n"
    "  float dx = max(0.0f, max(min1_x, min2_x) - min(max1_x, max2_x));\n"
    "  float dy = max(0.0f, max(min1_y, min2_y) - min(max1_y, max2_y));\n"
    "  float dz = max(0.0f, max(min1_z, min2_z) - min(max1_z, max2_z));\n"
    "  distances[k] = sqrt(dx * dx + dy * dy + dz * dz);\n"
    "}\n";

// aabbOverlapSelf: same linear pair-index mapping as aabbDistance.
// aabbOverlapOneVsAll: query AABB passed as 6 scalar args vs N managed objects.
// aabbOverlapCross / aabbDistanceCross: N*M rectangular grid; work item k maps
//   to (i = k/m, j = k%m) so no guards or wasted threads are needed.
// aabbDistanceOneVsAll: query AABB as 6 scalar args, outputs N distances.
static const char *kAABBCollideKernelSrc = R"(
__kernel void aabbOverlapSelf(__global const float *aabbs,
                               __global uchar *overlaps,
                               const int n) {
  const int k = get_global_id(0);
  int i = (int)((sqrt((float)(8 * k + 1)) - 1.0f) * 0.5f);
  int j = k - i * (i + 1) / 2 + i + 1;
  const float min1_x=aabbs[i*6+0], min1_y=aabbs[i*6+1], min1_z=aabbs[i*6+2];
  const float max1_x=aabbs[i*6+3], max1_y=aabbs[i*6+4], max1_z=aabbs[i*6+5];
  const float min2_x=aabbs[j*6+0], min2_y=aabbs[j*6+1], min2_z=aabbs[j*6+2];
  const float max2_x=aabbs[j*6+3], max2_y=aabbs[j*6+4], max2_z=aabbs[j*6+5];
  overlaps[k] = (min1_x <= max2_x && max1_x >= min2_x &&
                 min1_y <= max2_y && max1_y >= min2_y &&
                 min1_z <= max2_z && max1_z >= min2_z) ? 1 : 0;
}

__kernel void aabbOverlapOneVsAll(__global const float *aabbs,
                                   __global uchar *overlaps,
                                   float qmin_x, float qmin_y, float qmin_z,
                                   float qmax_x, float qmax_y, float qmax_z) {
  const int i = get_global_id(0);
  const float min2_x=aabbs[i*6+0], min2_y=aabbs[i*6+1], min2_z=aabbs[i*6+2];
  const float max2_x=aabbs[i*6+3], max2_y=aabbs[i*6+4], max2_z=aabbs[i*6+5];
  overlaps[i] = (qmin_x <= max2_x && qmax_x >= min2_x &&
                 qmin_y <= max2_y && qmax_y >= min2_y &&
                 qmin_z <= max2_z && qmax_z >= min2_z) ? 1 : 0;
}

__kernel void aabbOverlapCross(__global const float *aabbs1,
                                __global const float *aabbs2,
                                __global uchar *overlaps,
                                const int m) {
  const int k = get_global_id(0);
  const int i = k / m;
  const int j = k % m;
  const float min1_x=aabbs1[i*6+0], min1_y=aabbs1[i*6+1], min1_z=aabbs1[i*6+2];
  const float max1_x=aabbs1[i*6+3], max1_y=aabbs1[i*6+4], max1_z=aabbs1[i*6+5];
  const float min2_x=aabbs2[j*6+0], min2_y=aabbs2[j*6+1], min2_z=aabbs2[j*6+2];
  const float max2_x=aabbs2[j*6+3], max2_y=aabbs2[j*6+4], max2_z=aabbs2[j*6+5];
  overlaps[k] = (min1_x <= max2_x && max1_x >= min2_x &&
                 min1_y <= max2_y && max1_y >= min2_y &&
                 min1_z <= max2_z && max1_z >= min2_z) ? 1 : 0;
}

__kernel void aabbDistanceCross(__global const float *aabbs1,
                                 __global const float *aabbs2,
                                 __global float *distances,
                                 const int m) {
  const int k = get_global_id(0);
  const int i = k / m;
  const int j = k % m;
  const float min1_x=aabbs1[i*6+0], min1_y=aabbs1[i*6+1], min1_z=aabbs1[i*6+2];
  const float max1_x=aabbs1[i*6+3], max1_y=aabbs1[i*6+4], max1_z=aabbs1[i*6+5];
  const float min2_x=aabbs2[j*6+0], min2_y=aabbs2[j*6+1], min2_z=aabbs2[j*6+2];
  const float max2_x=aabbs2[j*6+3], max2_y=aabbs2[j*6+4], max2_z=aabbs2[j*6+5];
  float dx = max(0.0f, max(min1_x,min2_x) - min(max1_x,max2_x));
  float dy = max(0.0f, max(min1_y,min2_y) - min(max1_y,max2_y));
  float dz = max(0.0f, max(min1_z,min2_z) - min(max1_z,max2_z));
  distances[k] = sqrt(dx*dx + dy*dy + dz*dz);
}

__kernel void aabbDistanceOneVsAll(__global const float *aabbs,
                                    __global float *distances,
                                    float qmin_x, float qmin_y, float qmin_z,
                                    float qmax_x, float qmax_y, float qmax_z) {
  const int i = get_global_id(0);
  const float min2_x=aabbs[i*6+0], min2_y=aabbs[i*6+1], min2_z=aabbs[i*6+2];
  const float max2_x=aabbs[i*6+3], max2_y=aabbs[i*6+4], max2_z=aabbs[i*6+5];
  float dx = max(0.0f, max(qmin_x,min2_x) - min(qmax_x,max2_x));
  float dy = max(0.0f, max(qmin_y,min2_y) - min(qmax_y,max2_y));
  float dz = max(0.0f, max(qmin_z,min2_z) - min(qmax_z,max2_z));
  distances[i] = sqrt(dx*dx + dy*dy + dz*dz);
}
)";

} // namespace

CoalGPUCollisionManager::CoalGPUCollisionManager() {
  // Get available platforms
  std::vector<cl::Platform> platforms;
  cl::Platform::get(&platforms);

  if (platforms.empty()) {
    throw std::runtime_error("No OpenCL platforms found");
  }

  // Get GPU devices from the first platform
  std::vector<cl::Device> devices;
  platforms[0].getDevices(CL_DEVICE_TYPE_GPU, &devices);

  if (devices.empty()) {
    throw std::runtime_error("No GPU devices found");
  }

  device_ = devices[0];

  // Log device name
  std::string device_name = device_.getInfo<CL_DEVICE_NAME>();
  COAL_GPU_DEBUG("GPU Device: " << device_name);

  // Create OpenCL context
  context_ = cl::Context(device_);
  COAL_GPU_DEBUG("OpenCL context created successfully");

  queue_ = cl::CommandQueue(context_, device_, CL_QUEUE_PROFILING_ENABLE);

  // Precompile distance kernel
  try {
    cl_int err;
    distance_program_ =
        cl::Program(context_, kAABBDistanceKernelSrc, false, &err);
    if (err != CL_SUCCESS) {
      throw std::runtime_error("Failed to create distance program: " +
                               std::to_string(err));
    }

    err = distance_program_.build({device_});
    if (err != CL_SUCCESS) {
      std::string error_msg =
          "Failed to build distance program: " + std::to_string(err);
      if (err == CL_BUILD_PROGRAM_FAILURE) {
        std::string log =
            distance_program_.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device_);
        error_msg += "\nBuild log:\n" + log;
      }
      throw std::runtime_error(error_msg);
    }

    COAL_GPU_DEBUG("Distance kernel compiled successfully");

    collide_program_ =
        cl::Program(context_, kAABBCollideKernelSrc, false, &err);
    if (err != CL_SUCCESS) {
      throw std::runtime_error("Failed to create collide program: " +
                               std::to_string(err));
    }
    err = collide_program_.build({device_});
    if (err != CL_SUCCESS) {
      std::string error_msg =
          "Failed to build collide program: " + std::to_string(err);
      if (err == CL_BUILD_PROGRAM_FAILURE) {
        std::string log =
            collide_program_.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device_);
        error_msg += "\nBuild log:\n" + log;
      }
      throw std::runtime_error(error_msg);
    }
    COAL_GPU_DEBUG("Collide kernels compiled successfully");
  } catch (const std::exception &e) {
    std::cerr << "Kernel compilation error: " << e.what() << std::endl;
    throw;
  }
}

void CoalGPUCollisionManager::registerObjects(
    const std::vector<coal::CollisionObject *> &other_objs) {
  objects_.insert(objects_.end(), other_objs.begin(), other_objs.end());
}

void CoalGPUCollisionManager::registerObject(coal::CollisionObject *obj) {
  objects_.push_back(obj);
}

void CoalGPUCollisionManager::unregisterObject(coal::CollisionObject *obj) {
  auto it = std::find(objects_.begin(), objects_.end(), obj);
  if (it != objects_.end()) {
    objects_.erase(it);
  }
}

void CoalGPUCollisionManager::clear() { objects_.clear(); }

void CoalGPUCollisionManager::getObjects(
    std::vector<coal::CollisionObject *> &objs) const {
  objs = objects_;
}

bool CoalGPUCollisionManager::empty() const { return objects_.empty(); }

size_t CoalGPUCollisionManager::size() const { return objects_.size(); }

void CoalGPUCollisionManager::collide(
    coal::CollisionObject *obj, coal::CollisionCallBackBase *callback) const {
  if (objects_.empty() || !callback) {
    return;
  }
  callback->init();

  const size_t n = objects_.size();
  std::vector<float> aabbs;
  std::vector<cl_uchar> overlaps(n, 0);

  extractAABBs(objects_, aabbs);

  cl::Buffer buf_aabbs =
      createZeroCopyBuffer(aabbs.size() * sizeof(float), aabbs.data());
  cl::Buffer buf_overlaps =
      createZeroCopyBuffer(overlaps.size() * sizeof(cl_uchar), overlaps.data());

  const coal::AABB &q = obj->getAABB();
  cl::Kernel kernel(collide_program_, "aabbOverlapOneVsAll");
  kernel.setArg(0, buf_aabbs);
  kernel.setArg(1, buf_overlaps);
  kernel.setArg(2, static_cast<cl_float>(q.min_[0]));
  kernel.setArg(3, static_cast<cl_float>(q.min_[1]));
  kernel.setArg(4, static_cast<cl_float>(q.min_[2]));
  kernel.setArg(5, static_cast<cl_float>(q.max_[0]));
  kernel.setArg(6, static_cast<cl_float>(q.max_[1]));
  kernel.setArg(7, static_cast<cl_float>(q.max_[2]));

  cl::Event ev_collide_one;
  queue_.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(n),
                              cl::NullRange, nullptr, &ev_collide_one);
  queue_.finish();
  logKernelTime(ev_collide_one, "aabbOverlapOneVsAll");
  queue_.enqueueReadBuffer(buf_overlaps, CL_TRUE, 0,
                           overlaps.size() * sizeof(cl_uchar), overlaps.data());

  for (size_t i = 0; i < n; ++i) {
    if (overlaps[i]) {
      COAL_GPU_DEBUG("Overlap: query obj vs object[" << i << "]");
      if ((*callback)(obj, objects_[i]))
        return;
    }
  }
}

void CoalGPUCollisionManager::distance(
    coal::CollisionObject *obj, coal::DistanceCallBackBase *callback) const {
  if (objects_.empty() || !callback) {
    return;
  }
  callback->init();

  const size_t n = objects_.size();
  std::vector<float> aabbs;
  std::vector<float> distances(n, 0.0f);

  extractAABBs(objects_, aabbs);

  cl::Buffer buf_aabbs =
      createZeroCopyBuffer(aabbs.size() * sizeof(float), aabbs.data());
  cl::Buffer buf_distances =
      createZeroCopyBuffer(distances.size() * sizeof(float), distances.data());

  const coal::AABB &q = obj->getAABB();
  cl::Kernel kernel(collide_program_, "aabbDistanceOneVsAll");
  kernel.setArg(0, buf_aabbs);
  kernel.setArg(1, buf_distances);
  kernel.setArg(2, static_cast<cl_float>(q.min_[0]));
  kernel.setArg(3, static_cast<cl_float>(q.min_[1]));
  kernel.setArg(4, static_cast<cl_float>(q.min_[2]));
  kernel.setArg(5, static_cast<cl_float>(q.max_[0]));
  kernel.setArg(6, static_cast<cl_float>(q.max_[1]));
  kernel.setArg(7, static_cast<cl_float>(q.max_[2]));

  cl::Event ev_dist_one;
  queue_.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(n),
                              cl::NullRange, nullptr, &ev_dist_one);
  queue_.finish();
  logKernelTime(ev_dist_one, "aabbDistanceOneVsAll");
  queue_.enqueueReadBuffer(buf_distances, CL_TRUE, 0,
                           distances.size() * sizeof(float), distances.data());

  coal::CoalScalar min_dist = std::numeric_limits<coal::CoalScalar>::max();
  for (size_t i = 0; i < n; ++i) {
    const coal::CoalScalar d = static_cast<coal::CoalScalar>(distances[i]);
    if (d < min_dist) {
      COAL_GPU_DEBUG("Distance query obj vs object[" << i << "]: " << d);
      if ((*callback)(obj, objects_[i], min_dist))
        return;
    }
  }
}

void CoalGPUCollisionManager::collide(
    coal::CollisionCallBackBase *callback) const {
  if (objects_.empty() || !callback) {
    return;
  }
  callback->init();

  const size_t n = objects_.size();
  const size_t num_pairs = n * (n - 1) / 2;
  std::vector<float> aabbs;
  std::vector<cl_uchar> overlaps(num_pairs, 0);

  extractAABBs(objects_, aabbs);

  cl::Buffer buf_aabbs =
      createZeroCopyBuffer(aabbs.size() * sizeof(float), aabbs.data());
  cl::Buffer buf_overlaps =
      createZeroCopyBuffer(overlaps.size() * sizeof(cl_uchar), overlaps.data());

  cl::Kernel kernel(collide_program_, "aabbOverlapSelf");
  kernel.setArg(0, buf_aabbs);
  kernel.setArg(1, buf_overlaps);
  kernel.setArg(2, static_cast<cl_int>(n));

  cl::Event ev_overlap_self;
  queue_.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(num_pairs),
                              cl::NullRange, nullptr, &ev_overlap_self);
  queue_.finish();
  logKernelTime(ev_overlap_self, "aabbOverlapSelf");
  queue_.enqueueReadBuffer(buf_overlaps, CL_TRUE, 0,
                           overlaps.size() * sizeof(cl_uchar), overlaps.data());

  size_t k = 0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j, ++k) {
      if (overlaps[k]) {
        COAL_GPU_DEBUG("Overlap: object[" << i << "] vs object[" << j << "]");
        if ((*callback)(objects_[i], objects_[j]))
          return;
      }
    }
  }
}

void CoalGPUCollisionManager::distance(
    coal::DistanceCallBackBase *callback) const {
  if (objects_.empty() || !callback) {
    return;
  }

  callback->init();

  const size_t n = objects_.size();
  COAL_GPU_DEBUG("Computing distances between all object pairs ("
                 << n << " objects)");

  const size_t num_pairs = n * (n - 1) / 2;
  std::vector<float> aabbs;
  std::vector<float> distances(num_pairs, 0.0f);

  extractAABBs(objects_, aabbs);

  COAL_GPU_DEBUG("Host unified memory support: "
                 << (supportsHostUnifiedMemory() ? "Yes" : "No"));

  cl::Buffer buf_aabbs =
      createZeroCopyBuffer(aabbs.size() * sizeof(float), aabbs.data());
  cl::Buffer buf_distances =
      createZeroCopyBuffer(distances.size() * sizeof(float), distances.data());

  cl::Kernel kernel(distance_program_, "aabbDistance");
  kernel.setArg(0, buf_aabbs);
  kernel.setArg(1, buf_distances);
  kernel.setArg(2, static_cast<cl_int>(n));

  COAL_GPU_DEBUG("Executing kernel for " << num_pairs << " work items (" << n
                                         << " objects)");
  cl::Event ev_dist_self;
  queue_.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(num_pairs),
                              cl::NullRange, nullptr, &ev_dist_self);
  queue_.finish();
  logKernelTime(ev_dist_self, "aabbDistance");

  queue_.enqueueReadBuffer(buf_distances, CL_TRUE, 0,
                           distances.size() * sizeof(float), distances.data());
  COAL_GPU_DEBUG("Kernel execution complete");

  invokeDistanceCallbacks(callback, distances, n);
}

void CoalGPUCollisionManager::invokeDistanceCallbacks(
    coal::DistanceCallBackBase *callback, const std::vector<float> &distances,
    size_t n) const {
  COAL_GPU_DEBUG("Invoking distance callbacks for " << n * (n - 1) / 2
                                                    << " object pairs");

  // Tracks the current minimum distance — passed to each callback call as
  // in/out so the callback can both prune work and record the global minimum.
  coal::CoalScalar min_dist = std::numeric_limits<coal::CoalScalar>::max();

  size_t k = 0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j, ++k) {
      // GPU-computed AABB distance used as a lower-bound pruning filter.
      const coal::CoalScalar aabb_dist =
          static_cast<coal::CoalScalar>(distances[k]);
      if (aabb_dist < min_dist) {
        COAL_GPU_DEBUG("Distance[" << i << "," << j << "]: " << aabb_dist);
        if ((*callback)(objects_[i], objects_[j], min_dist)) {
          COAL_GPU_DEBUG("Distance callbacks stopped early by callback");
          return;
        }
      }
    }
  }

  COAL_GPU_DEBUG("Distance callbacks completed");
}

void CoalGPUCollisionManager::collide(
    coal::BroadPhaseCollisionManager *other_manager,
    coal::CollisionCallBackBase *callback) const {
  if (!callback)
    return;
  callback->init();
  if (objects_.empty())
    return;

  if (this == other_manager) {
    collide(callback);
    return;
  }

  std::vector<coal::CollisionObject *> other_objs;
  other_manager->getObjects(other_objs);
  if (other_objs.empty())
    return;

  const size_t n = objects_.size();
  const size_t m = other_objs.size();
  std::vector<float> aabbs1, aabbs2;
  std::vector<cl_uchar> overlaps(n * m, 0);

  extractAABBs(objects_, aabbs1);
  extractAABBs(other_objs, aabbs2);

  cl::Buffer buf_aabbs1 =
      createZeroCopyBuffer(aabbs1.size() * sizeof(float), aabbs1.data());
  cl::Buffer buf_aabbs2 =
      createZeroCopyBuffer(aabbs2.size() * sizeof(float), aabbs2.data());
  cl::Buffer buf_overlaps =
      createZeroCopyBuffer(overlaps.size() * sizeof(cl_uchar), overlaps.data());

  cl::Kernel kernel(collide_program_, "aabbOverlapCross");
  kernel.setArg(0, buf_aabbs1);
  kernel.setArg(1, buf_aabbs2);
  kernel.setArg(2, buf_overlaps);
  kernel.setArg(3, static_cast<cl_int>(m));

  cl::Event ev_overlap_cross;
  queue_.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(n * m),
                              cl::NullRange, nullptr, &ev_overlap_cross);
  queue_.finish();
  logKernelTime(ev_overlap_cross, "aabbOverlapCross");
  queue_.enqueueReadBuffer(buf_overlaps, CL_TRUE, 0,
                           overlaps.size() * sizeof(cl_uchar), overlaps.data());

  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < m; ++j) {
      if (overlaps[i * m + j]) {
        COAL_GPU_DEBUG("Cross overlap: object[" << i << "] vs other[" << j
                                                << "]");
        if ((*callback)(objects_[i], other_objs[j]))
          return;
      }
    }
  }
}

void CoalGPUCollisionManager::distance(
    coal::BroadPhaseCollisionManager *other_manager,
    coal::DistanceCallBackBase *callback) const {
  if (!callback)
    return;
  callback->init();
  if (objects_.empty())
    return;

  if (this == other_manager) {
    distance(callback);
    return;
  }

  std::vector<coal::CollisionObject *> other_objs;
  other_manager->getObjects(other_objs);
  if (other_objs.empty())
    return;

  const size_t n = objects_.size();
  const size_t m = other_objs.size();
  std::vector<float> aabbs1, aabbs2;
  std::vector<float> distances(n * m, 0.0f);

  extractAABBs(objects_, aabbs1);
  extractAABBs(other_objs, aabbs2);

  cl::Buffer buf_aabbs1 =
      createZeroCopyBuffer(aabbs1.size() * sizeof(float), aabbs1.data());
  cl::Buffer buf_aabbs2 =
      createZeroCopyBuffer(aabbs2.size() * sizeof(float), aabbs2.data());
  cl::Buffer buf_distances =
      createZeroCopyBuffer(distances.size() * sizeof(float), distances.data());

  cl::Kernel kernel(collide_program_, "aabbDistanceCross");
  kernel.setArg(0, buf_aabbs1);
  kernel.setArg(1, buf_aabbs2);
  kernel.setArg(2, buf_distances);
  kernel.setArg(3, static_cast<cl_int>(m));

  cl::Event ev_dist_cross;
  queue_.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(n * m),
                              cl::NullRange, nullptr, &ev_dist_cross);
  queue_.finish();
  logKernelTime(ev_dist_cross, "aabbDistanceCross");
  queue_.enqueueReadBuffer(buf_distances, CL_TRUE, 0,
                           distances.size() * sizeof(float), distances.data());

  coal::CoalScalar min_dist = std::numeric_limits<coal::CoalScalar>::max();
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < m; ++j) {
      const coal::CoalScalar d =
          static_cast<coal::CoalScalar>(distances[i * m + j]);
      if (d < min_dist) {
        COAL_GPU_DEBUG("Cross distance object[" << i << "] vs other[" << j
                                                << "]: " << d);
        if ((*callback)(objects_[i], other_objs[j], min_dist))
          return;
      }
    }
  }
}

bool CoalGPUCollisionManager::supportsHostUnifiedMemory() const {
  try {
    cl_bool unified = CL_FALSE;
    cl_int err = device_.getInfo(CL_DEVICE_HOST_UNIFIED_MEMORY, &unified);
    if (err != CL_SUCCESS) {
      std::cerr << "Error checking host unified memory: " << err << std::endl;
      return false;
    }
    return unified == CL_TRUE;
  } catch (const std::exception &e) {
    std::cerr << "Exception checking host unified memory: " << e.what()
              << std::endl;
    return false;
  }
}

cl::Buffer CoalGPUCollisionManager::createZeroCopyBuffer(size_t size,
                                                         void *host_ptr) const {
  cl_int err;
  cl_mem_flags flags = CL_MEM_READ_WRITE;

  // Use host pointer flag for zero-copy access if device supports it
  if (supportsHostUnifiedMemory() && host_ptr) {
    flags |= CL_MEM_USE_HOST_PTR;
    COAL_GPU_DEBUG("Creating zero-copy buffer (host unified memory)");
  } else if (host_ptr) {
    // Fall back to copy host pointer if unified memory not supported
    flags |= CL_MEM_COPY_HOST_PTR;
    COAL_GPU_DEBUG(
        "Creating buffer with host data copy (unified memory not supported)");
  } else {
    COAL_GPU_DEBUG("Creating device-only buffer");
  }

  cl::Buffer buffer(context_, flags, size, host_ptr, &err);
  if (err != CL_SUCCESS) {
    throw std::runtime_error("Failed to create buffer: " + std::to_string(err));
  }

  return buffer;
}

void CoalGPUCollisionManager::logKernelTime(const cl::Event &ev,
                                            const char *label) const {
  cl_ulong t_start = ev.getProfilingInfo<CL_PROFILING_COMMAND_START>();
  cl_ulong t_end = ev.getProfilingInfo<CL_PROFILING_COMMAND_END>();
  double ms = static_cast<double>(t_end - t_start) * 1e-6;
  COAL_GPU_DEBUG("Kernel [" << label << "] took " << ms << " ms");
}

void CoalGPUCollisionManager::extractAABBs(
    const std::vector<coal::CollisionObject *> &objs,
    std::vector<float> &aabbs) {
  aabbs.resize(objs.size() * 6);
  for (size_t i = 0; i < objs.size(); ++i) {
    const coal::AABB &aabb = objs[i]->getAABB();
    aabbs[i * 6 + 0] = static_cast<float>(aabb.min_[0]);
    aabbs[i * 6 + 1] = static_cast<float>(aabb.min_[1]);
    aabbs[i * 6 + 2] = static_cast<float>(aabb.min_[2]);
    aabbs[i * 6 + 3] = static_cast<float>(aabb.max_[0]);
    aabbs[i * 6 + 4] = static_cast<float>(aabb.max_[1]);
    aabbs[i * 6 + 5] = static_cast<float>(aabb.max_[2]);
  }
  COAL_GPU_DEBUG("Extracted AABBs for " << objs.size() << " objects");
}
