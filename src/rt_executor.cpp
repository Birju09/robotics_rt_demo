#include "rt_executor.hpp"

#include <cstring>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <time.h>

RTExecutor::RTExecutor(OutputCallback on_tick) : on_tick_(on_tick) {}

RTExecutor::~RTExecutor() { stop(); }

void RTExecutor::setTrajectory(std::shared_ptr<const Trajectory> traj) {
  std::lock_guard<std::mutex> lock(traj_mutex_);
  active_traj_ = traj;
}

void RTExecutor::start() {
  if (running_.exchange(true)) {
    // Already running
    return;
  }
  thread_ = std::thread(&RTExecutor::rtLoop, this);
}

void RTExecutor::stop() {
  if (!running_.exchange(false)) {
    // Not running
    return;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void RTExecutor::rtLoop() {
  // Set up SCHED_FIFO priority
  struct sched_param param;
  // High priority (0-99 for FIFO)
  param.sched_priority = 80;

  int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
  if (ret != 0) {
    std::cerr << "Warning: Failed to set SCHED_FIFO. Are you running as root "
                 "or with CAP_SYS_NICE?"
              << std::endl;
    std::cerr << "        Falling back to SCHED_OTHER (non-realtime)"
              << std::endl;
  } else {
    std::cout << "RTExecutor: SCHED_FIFO priority set to 80" << std::endl;
  }

  // Set CPU affinity to core 1 (if available)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(1, &cpuset);
  ret = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
  if (ret == 0) {
    std::cout << "RTExecutor: CPU affinity set to core 1" << std::endl;
  } else {
    std::cerr << "Warning: Failed to set CPU affinity" << std::endl;
  }

  // Calibrate initial time
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  // Convert to nanoseconds for easier arithmetic
  uint64_t now_ns =
      static_cast<uint64_t>(now.tv_sec) * 1000000000UL + now.tv_nsec;
  // 1ms = 1,000,000 ns
  uint64_t tick_period_ns = 1000000UL;
  uint64_t next_tick_ns = now_ns + tick_period_ns;

  loop_time_s_ = 0.0;
  last_q_.clear();

  std::cout << "RTExecutor: 1kHz loop started" << std::endl;

  while (running_.load(std::memory_order_acquire)) {
    // Load trajectory (mutex synchronizes with planning thread)
    std::shared_ptr<const Trajectory> traj_ptr;
    {
      std::lock_guard<std::mutex> lock(traj_mutex_);
      traj_ptr = active_traj_;
    }

    // If trajectory pointer changed, reset loop time
    if (traj_ptr != local_traj_) {
      local_traj_ = traj_ptr;
      loop_time_s_ = 0.0;
      last_q_.clear();
      if (local_traj_) {
        std::cout << "RTExecutor: new trajectory loaded" << std::endl;
      }
    }

    // Evaluate trajectory or hold last position
    std::vector<double> q;
    if (local_traj_ && loop_time_s_ <= local_traj_->duration_s) {
      q = local_traj_->eval(loop_time_s_);
      last_q_ = q;
    } else if (!last_q_.empty()) {
      q = last_q_;
    } else {
      // No trajectory, no last position
      q.clear();
    }

    // Invoke output callback
    if (!q.empty()) {
      on_tick_(loop_time_s_, q);
    }

    // Advance time for next iteration
    if (local_traj_ && loop_time_s_ <= local_traj_->duration_s) {
      // 1ms
      loop_time_s_ += 0.001;
    }

    // Sleep until next tick
    // Use clock_nanosleep with TIMER_ABSTIME for absolute wake time
    struct timespec abs_time;
    abs_time.tv_sec = static_cast<time_t>(next_tick_ns / 1000000000UL);
    abs_time.tv_nsec = static_cast<long>(next_tick_ns % 1000000000UL);

    ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &abs_time, nullptr);
    if (ret != 0 && ret != EINTR) {
      std::cerr << "RTExecutor: clock_nanosleep failed with error " << ret
                << std::endl;
    }

    next_tick_ns += tick_period_ns;
  }

  std::cout << "RTExecutor: 1kHz loop stopped" << std::endl;
}
