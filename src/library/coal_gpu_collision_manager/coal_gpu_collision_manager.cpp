#include "coal_gpu_collision_manager.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

// Debug logging macro - can be disabled by defining COAL_GPU_NO_DEBUG
#ifdef COAL_GPU_DEBUG_LOG
#define COAL_GPU_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl;

// Step-level timing for the GPU hot path.
// COAL_GPU_TIMER_START declares _gpu_t_prev and _gpu_t0 in the local scope.
// COAL_GPU_STEP prints microseconds since the previous step and advances the
// cursor. COAL_GPU_TIMER_TOTAL prints total since COAL_GPU_TIMER_START.
#define COAL_GPU_TIMER_START                                                   \
  auto _gpu_t_prev = std::chrono::steady_clock::now();                         \
  auto _gpu_t0 = _gpu_t_prev;                                                  \
  (void)_gpu_t0
#define COAL_GPU_STEP(label)                                                   \
  do {                                                                         \
    auto _t = std::chrono::steady_clock::now();                                \
    std::cout                                                                  \
        << "[TIMING]   " << (label) << ": "                                    \
        << std::chrono::duration<double, std::micro>(_t - _gpu_t_prev).count() \
        << " us\n";                                                            \
    _gpu_t_prev = _t;                                                          \
  } while (0)
#define COAL_GPU_TIMER_TOTAL(label)                                            \
  do {                                                                         \
    std::cout << "[TIMING] " << (label) << " total: "                          \
              << std::chrono::duration<double, std::micro>(                    \
                     std::chrono::steady_clock::now() - _gpu_t0)               \
                     .count()                                                  \
              << " us\n";                                                      \
  } while (0)

#else

#define COAL_GPU_DEBUG(msg)                                                    \
  do {                                                                         \
  } while (0)
#define COAL_GPU_TIMER_START
#define COAL_GPU_STEP(label)
#define COAL_GPU_TIMER_TOTAL(label)
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

  // Cache unified memory support once — avoids a driver round-trip per buffer.
  cl_bool unified = CL_FALSE;
  device_.getInfo(CL_DEVICE_HOST_UNIFIED_MEMORY, &unified);
  has_unified_memory_ = (unified == CL_TRUE);
  COAL_GPU_DEBUG(
      "Host unified memory: " << (has_unified_memory_ ? "yes" : "no"));

  // Compile programs and pre-create all kernel objects.
  // Creating cl::Kernel by name is a program lookup — doing it once here
  // avoids that cost on every collide/distance call.
  try {
    cl_int err;
    distance_program_ =
        cl::Program(context_, kAABBDistanceKernelSrc, false, &err);
    if (err != CL_SUCCESS)
      throw std::runtime_error("Failed to create distance program: " +
                               std::to_string(err));
    err = distance_program_.build({device_});
    if (err != CL_SUCCESS) {
      std::string msg =
          "Failed to build distance program: " + std::to_string(err);
      if (err == CL_BUILD_PROGRAM_FAILURE)
        msg += "\n" +
               distance_program_.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device_);
      throw std::runtime_error(msg);
    }
    COAL_GPU_DEBUG("Distance kernel compiled successfully");

    collide_program_ =
        cl::Program(context_, kAABBCollideKernelSrc, false, &err);
    if (err != CL_SUCCESS)
      throw std::runtime_error("Failed to create collide program: " +
                               std::to_string(err));
    err = collide_program_.build({device_});
    if (err != CL_SUCCESS) {
      std::string msg =
          "Failed to build collide program: " + std::to_string(err);
      if (err == CL_BUILD_PROGRAM_FAILURE)
        msg +=
            "\n" + collide_program_.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device_);
      throw std::runtime_error(msg);
    }
    COAL_GPU_DEBUG("Collide kernels compiled successfully");

    k_aabb_distance_ = cl::Kernel(distance_program_, "aabbDistance");
    k_overlap_self_ = cl::Kernel(collide_program_, "aabbOverlapSelf");
    k_overlap_one_vs_all_ = cl::Kernel(collide_program_, "aabbOverlapOneVsAll");
    k_distance_one_vs_all_ =
        cl::Kernel(collide_program_, "aabbDistanceOneVsAll");
    k_overlap_cross_ = cl::Kernel(collide_program_, "aabbOverlapCross");
    k_distance_cross_ = cl::Kernel(collide_program_, "aabbDistanceCross");
    COAL_GPU_DEBUG("All kernel objects pre-created");
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
  if (it != objects_.end())
    objects_.erase(it);
}

void CoalGPUCollisionManager::clear() { objects_.clear(); }

void CoalGPUCollisionManager::getObjects(
    std::vector<coal::CollisionObject *> &objs) const {
  objs = objects_;
}

bool CoalGPUCollisionManager::empty() const { return objects_.empty(); }
size_t CoalGPUCollisionManager::size() const { return objects_.size(); }

void CoalGPUCollisionManager::setup() {
  const size_t n = objects_.size();
  if (n == 0)
    return;
  const size_t num_pairs = n * (n - 1) / 2;

  // Allocate persistent device buffers once. CL_MEM_ALLOC_HOST_PTR hints the
  // driver to use pinned/shared memory, improving transfer throughput without
  // requiring a fixed host pointer (unlike CL_MEM_USE_HOST_PTR).
  buf_aabbs_ = allocBuffer(n * 6 * sizeof(float));
  buf_overlaps_self_ = allocBuffer(num_pairs * sizeof(cl_uchar));
  buf_distances_self_ = allocBuffer(num_pairs * sizeof(float));

  // Pre-size staging vectors — avoids allocation on the hot path.
  aabbs_staging_.resize(n * 6);
  overlaps_staging_.resize(num_pairs, 0);
  distances_staging_.resize(num_pairs, 0.0f);

  // Bind static args for self-check kernels once. These buffer bindings never
  // change between calls; only the AABB contents change (uploaded each call).
  k_overlap_self_.setArg(0, buf_aabbs_);
  k_overlap_self_.setArg(1, buf_overlaps_self_);
  k_overlap_self_.setArg(2, static_cast<cl_int>(n));

  k_aabb_distance_.setArg(0, buf_aabbs_);
  k_aabb_distance_.setArg(1, buf_distances_self_);
  k_aabb_distance_.setArg(2, static_cast<cl_int>(n));

  COAL_GPU_DEBUG("setup(): persistent buffers allocated for n="
                 << n << " objects, " << num_pairs << " pairs");
}

// Hot path — called by OMPL on every state validity check.
// On unified memory (MALI): map/unmap replaces write/readBuffer — no data
// copy, just cache-coherence operations on shared DRAM.
// On discrete GPU: falls back to enqueueWriteBuffer / enqueueReadBuffer.
void CoalGPUCollisionManager::collide(
    coal::CollisionCallBackBase *callback) const {
  if (objects_.empty() || !callback)
    return;
  callback->init();

  const size_t n = objects_.size();
  const size_t num_pairs = n * (n - 1) / 2;

  cl::Event ev;
  COAL_GPU_TIMER_START;
  if (has_unified_memory_) {
    auto *aabb_ptr = static_cast<float *>(queue_.enqueueMapBuffer(
        buf_aabbs_, CL_TRUE, CL_MAP_WRITE_INVALIDATE_REGION, 0,
        n * 6 * sizeof(float)));
    COAL_GPU_STEP("mapBuffer(aabbs, WRITE_INVALIDATE)");

    extractAABBs(objects_, aabb_ptr);
    COAL_GPU_STEP("extractAABBs");

    queue_.enqueueUnmapMemObject(buf_aabbs_, aabb_ptr);
    COAL_GPU_STEP("unmapMemObject(aabbs)");

    queue_.enqueueNDRangeKernel(k_overlap_self_, cl::NullRange,
                                cl::NDRange(num_pairs), cl::NullRange, nullptr,
                                &ev);
    COAL_GPU_STEP("enqueueNDRangeKernel");

    const auto *overlap_ptr = static_cast<cl_uchar *>(
        queue_.enqueueMapBuffer(buf_overlaps_self_, CL_TRUE, CL_MAP_READ, 0,
                                num_pairs * sizeof(cl_uchar)));
    COAL_GPU_STEP("mapBuffer(overlaps, READ) [blocks until kernel done]");
    logKernelTime(ev, "aabbOverlapSelf");

    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = i + 1; j < n; ++j, ++k) {
        if (overlap_ptr[k]) {
          COAL_GPU_DEBUG("Overlap: object[" << i << "] vs object[" << j << "]");
          if ((*callback)(objects_[i], objects_[j])) {
            queue_.enqueueUnmapMemObject(buf_overlaps_self_,
                                         const_cast<cl_uchar *>(overlap_ptr));
            COAL_GPU_STEP("callbacks+unmapMemObject(overlaps) [early exit]");
            COAL_GPU_TIMER_TOTAL("collide(unified)");
            return;
          }
        }
      }
    }
    queue_.enqueueUnmapMemObject(buf_overlaps_self_,
                                 const_cast<cl_uchar *>(overlap_ptr));
    COAL_GPU_STEP("callbacks+unmapMemObject(overlaps)");
  } else {
    extractAABBs(objects_, aabbs_staging_.data());
    COAL_GPU_STEP("extractAABBs");

    queue_.enqueueWriteBuffer(buf_aabbs_, CL_FALSE, 0, n * 6 * sizeof(float),
                              aabbs_staging_.data());
    COAL_GPU_STEP("enqueueWriteBuffer(aabbs, non-blocking)");

    queue_.enqueueNDRangeKernel(k_overlap_self_, cl::NullRange,
                                cl::NDRange(num_pairs), cl::NullRange, nullptr,
                                &ev);
    COAL_GPU_STEP("enqueueNDRangeKernel");

    queue_.enqueueReadBuffer(buf_overlaps_self_, CL_TRUE, 0,
                             num_pairs * sizeof(cl_uchar),
                             overlaps_staging_.data());
    COAL_GPU_STEP("enqueueReadBuffer(overlaps, blocking)");
    logKernelTime(ev, "aabbOverlapSelf");

    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = i + 1; j < n; ++j, ++k) {
        if (overlaps_staging_[k]) {
          COAL_GPU_DEBUG("Overlap: object[" << i << "] vs object[" << j << "]");
          if ((*callback)(objects_[i], objects_[j])) {
            COAL_GPU_STEP("callbacks [early exit]");
            COAL_GPU_TIMER_TOTAL("collide(discrete)");
            return;
          }
        }
      }
    }
    COAL_GPU_STEP("callbacks");
  }
  COAL_GPU_TIMER_TOTAL("collide");
}

void CoalGPUCollisionManager::distance(
    coal::DistanceCallBackBase *callback) const {
  if (objects_.empty() || !callback)
    return;
  callback->init();

  const size_t n = objects_.size();
  const size_t num_pairs = n * (n - 1) / 2;
  COAL_GPU_DEBUG("Computing distances (" << n << " objects, " << num_pairs
                                         << " pairs)");

  cl::Event ev;
  if (has_unified_memory_) {
    auto *aabb_ptr = static_cast<float *>(queue_.enqueueMapBuffer(
        buf_aabbs_, CL_TRUE, CL_MAP_WRITE_INVALIDATE_REGION, 0,
        n * 6 * sizeof(float)));
    extractAABBs(objects_, aabb_ptr);
    queue_.enqueueUnmapMemObject(buf_aabbs_, aabb_ptr);

    queue_.enqueueNDRangeKernel(k_aabb_distance_, cl::NullRange,
                                cl::NDRange(num_pairs), cl::NullRange, nullptr,
                                &ev);

    const auto *dist_ptr = static_cast<float *>(
        queue_.enqueueMapBuffer(buf_distances_self_, CL_TRUE, CL_MAP_READ, 0,
                                num_pairs * sizeof(float)));
    logKernelTime(ev, "aabbDistance");
    // Copy into staging so invokeDistanceCallbacks can use the vector API.
    std::copy(dist_ptr, dist_ptr + num_pairs, distances_staging_.begin());
    queue_.enqueueUnmapMemObject(buf_distances_self_,
                                 const_cast<float *>(dist_ptr));
  } else {
    extractAABBs(objects_, aabbs_staging_.data());
    queue_.enqueueWriteBuffer(buf_aabbs_, CL_FALSE, 0, n * 6 * sizeof(float),
                              aabbs_staging_.data());
    queue_.enqueueNDRangeKernel(k_aabb_distance_, cl::NullRange,
                                cl::NDRange(num_pairs), cl::NullRange, nullptr,
                                &ev);
    queue_.enqueueReadBuffer(buf_distances_self_, CL_TRUE, 0,
                             num_pairs * sizeof(float),
                             distances_staging_.data());
    logKernelTime(ev, "aabbDistance");
  }

  invokeDistanceCallbacks(callback, distances_staging_, n);
}

// One-vs-all: not on the OMPL hot path — per-call temp buffers are acceptable.
void CoalGPUCollisionManager::collide(
    coal::CollisionObject *obj, coal::CollisionCallBackBase *callback) const {
  if (objects_.empty() || !callback)
    return;
  callback->init();

  const size_t n = objects_.size();
  std::vector<float> aabbs(n * 6);
  std::vector<cl_uchar> overlaps(n, 0);
  extractAABBs(objects_, aabbs.data());

  cl::Buffer buf_aabbs = allocBuffer(n * 6 * sizeof(float));
  cl::Buffer buf_overlaps = allocBuffer(n * sizeof(cl_uchar));
  queue_.enqueueWriteBuffer(buf_aabbs, CL_FALSE, 0, n * 6 * sizeof(float),
                            aabbs.data());

  const coal::AABB &q = obj->getAABB();
  k_overlap_one_vs_all_.setArg(0, buf_aabbs);
  k_overlap_one_vs_all_.setArg(1, buf_overlaps);
  k_overlap_one_vs_all_.setArg(2, static_cast<cl_float>(q.min_[0]));
  k_overlap_one_vs_all_.setArg(3, static_cast<cl_float>(q.min_[1]));
  k_overlap_one_vs_all_.setArg(4, static_cast<cl_float>(q.min_[2]));
  k_overlap_one_vs_all_.setArg(5, static_cast<cl_float>(q.max_[0]));
  k_overlap_one_vs_all_.setArg(6, static_cast<cl_float>(q.max_[1]));
  k_overlap_one_vs_all_.setArg(7, static_cast<cl_float>(q.max_[2]));

  cl::Event ev;
  queue_.enqueueNDRangeKernel(k_overlap_one_vs_all_, cl::NullRange,
                              cl::NDRange(n), cl::NullRange, nullptr, &ev);
  queue_.enqueueReadBuffer(buf_overlaps, CL_TRUE, 0, n * sizeof(cl_uchar),
                           overlaps.data());
  logKernelTime(ev, "aabbOverlapOneVsAll");

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
  if (objects_.empty() || !callback)
    return;
  callback->init();

  const size_t n = objects_.size();
  std::vector<float> aabbs(n * 6);
  std::vector<float> distances(n, 0.0f);
  extractAABBs(objects_, aabbs.data());

  cl::Buffer buf_aabbs = allocBuffer(n * 6 * sizeof(float));
  cl::Buffer buf_distances = allocBuffer(n * sizeof(float));
  queue_.enqueueWriteBuffer(buf_aabbs, CL_FALSE, 0, n * 6 * sizeof(float),
                            aabbs.data());

  const coal::AABB &q = obj->getAABB();
  k_distance_one_vs_all_.setArg(0, buf_aabbs);
  k_distance_one_vs_all_.setArg(1, buf_distances);
  k_distance_one_vs_all_.setArg(2, static_cast<cl_float>(q.min_[0]));
  k_distance_one_vs_all_.setArg(3, static_cast<cl_float>(q.min_[1]));
  k_distance_one_vs_all_.setArg(4, static_cast<cl_float>(q.min_[2]));
  k_distance_one_vs_all_.setArg(5, static_cast<cl_float>(q.max_[0]));
  k_distance_one_vs_all_.setArg(6, static_cast<cl_float>(q.max_[1]));
  k_distance_one_vs_all_.setArg(7, static_cast<cl_float>(q.max_[2]));

  cl::Event ev;
  queue_.enqueueNDRangeKernel(k_distance_one_vs_all_, cl::NullRange,
                              cl::NDRange(n), cl::NullRange, nullptr, &ev);
  queue_.enqueueReadBuffer(buf_distances, CL_TRUE, 0, n * sizeof(float),
                           distances.data());
  logKernelTime(ev, "aabbDistanceOneVsAll");

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
  std::vector<float> aabbs1(n * 6), aabbs2(m * 6);
  std::vector<cl_uchar> overlaps(n * m, 0);
  extractAABBs(objects_, aabbs1.data());
  extractAABBs(other_objs, aabbs2.data());

  cl::Buffer buf1 = allocBuffer(n * 6 * sizeof(float));
  cl::Buffer buf2 = allocBuffer(m * 6 * sizeof(float));
  cl::Buffer buf_overlaps = allocBuffer(n * m * sizeof(cl_uchar));
  queue_.enqueueWriteBuffer(buf1, CL_FALSE, 0, n * 6 * sizeof(float),
                            aabbs1.data());
  queue_.enqueueWriteBuffer(buf2, CL_FALSE, 0, m * 6 * sizeof(float),
                            aabbs2.data());

  k_overlap_cross_.setArg(0, buf1);
  k_overlap_cross_.setArg(1, buf2);
  k_overlap_cross_.setArg(2, buf_overlaps);
  k_overlap_cross_.setArg(3, static_cast<cl_int>(m));

  cl::Event ev;
  queue_.enqueueNDRangeKernel(k_overlap_cross_, cl::NullRange,
                              cl::NDRange(n * m), cl::NullRange, nullptr, &ev);
  queue_.enqueueReadBuffer(buf_overlaps, CL_TRUE, 0, n * m * sizeof(cl_uchar),
                           overlaps.data());
  logKernelTime(ev, "aabbOverlapCross");

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
  std::vector<float> aabbs1(n * 6), aabbs2(m * 6);
  std::vector<float> distances(n * m, 0.0f);
  extractAABBs(objects_, aabbs1.data());
  extractAABBs(other_objs, aabbs2.data());

  cl::Buffer buf1 = allocBuffer(n * 6 * sizeof(float));
  cl::Buffer buf2 = allocBuffer(m * 6 * sizeof(float));
  cl::Buffer buf_distances = allocBuffer(n * m * sizeof(float));
  queue_.enqueueWriteBuffer(buf1, CL_FALSE, 0, n * 6 * sizeof(float),
                            aabbs1.data());
  queue_.enqueueWriteBuffer(buf2, CL_FALSE, 0, m * 6 * sizeof(float),
                            aabbs2.data());

  k_distance_cross_.setArg(0, buf1);
  k_distance_cross_.setArg(1, buf2);
  k_distance_cross_.setArg(2, buf_distances);
  k_distance_cross_.setArg(3, static_cast<cl_int>(m));

  cl::Event ev;
  queue_.enqueueNDRangeKernel(k_distance_cross_, cl::NullRange,
                              cl::NDRange(n * m), cl::NullRange, nullptr, &ev);
  queue_.enqueueReadBuffer(buf_distances, CL_TRUE, 0, n * m * sizeof(float),
                           distances.data());
  logKernelTime(ev, "aabbDistanceCross");

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

void CoalGPUCollisionManager::invokeDistanceCallbacks(
    coal::DistanceCallBackBase *callback, const std::vector<float> &distances,
    size_t n) const {
  coal::CoalScalar min_dist = std::numeric_limits<coal::CoalScalar>::max();
  size_t k = 0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j, ++k) {
      const coal::CoalScalar aabb_dist =
          static_cast<coal::CoalScalar>(distances[k]);
      if (aabb_dist < min_dist) {
        COAL_GPU_DEBUG("Distance[" << i << "," << j << "]: " << aabb_dist);
        if ((*callback)(objects_[i], objects_[j], min_dist))
          return;
      }
    }
  }
}

cl::Buffer CoalGPUCollisionManager::allocBuffer(size_t size) const {
  // CL_MEM_ALLOC_HOST_PTR hints the driver to use pinned/shared memory,
  // which improves enqueueWriteBuffer/enqueueReadBuffer throughput without
  // requiring a fixed host pointer (unlike CL_MEM_USE_HOST_PTR).
  cl_int err;
  cl::Buffer buf(context_, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, size,
                 nullptr, &err);
  if (err != CL_SUCCESS)
    throw std::runtime_error("allocBuffer failed: " + std::to_string(err));
  return buf;
}

void CoalGPUCollisionManager::logKernelTime(const cl::Event &ev,
                                            const char *label) const {
  cl_ulong t_start = ev.getProfilingInfo<CL_PROFILING_COMMAND_START>();
  cl_ulong t_end = ev.getProfilingInfo<CL_PROFILING_COMMAND_END>();
  double ms = static_cast<double>(t_end - t_start) * 1e-6;
  COAL_GPU_DEBUG("Kernel [" << label << "] took " << ms << " ms");
}

void CoalGPUCollisionManager::extractAABBs(
    const std::vector<coal::CollisionObject *> &objs, float *out) {
  for (size_t i = 0; i < objs.size(); ++i) {
    const coal::AABB &aabb = objs[i]->getAABB();
    out[i * 6 + 0] = static_cast<float>(aabb.min_[0]);
    out[i * 6 + 1] = static_cast<float>(aabb.min_[1]);
    out[i * 6 + 2] = static_cast<float>(aabb.min_[2]);
    out[i * 6 + 3] = static_cast<float>(aabb.max_[0]);
    out[i * 6 + 4] = static_cast<float>(aabb.max_[1]);
    out[i * 6 + 5] = static_cast<float>(aabb.max_[2]);
  }
  COAL_GPU_DEBUG("Extracted AABBs for " << objs.size() << " objects");
}
