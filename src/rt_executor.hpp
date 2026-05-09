#ifndef ROBOTICS_RT_DEMO_RT_EXECUTOR_HPP_
#define ROBOTICS_RT_DEMO_RT_EXECUTOR_HPP_

#include "trajectory.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

//! RTExecutor runs a 1kHz real-time thread for trajectory playback.
//!
//! Responsibilities:
//!  - Run on POSIX SCHED_FIFO with high priority
//!  - Accept trajectory updates via lock-free handoff
//!  - Evaluate cubic spline trajectory at each 1ms tick
//!  - Invoke output callback with current joint positions
class RTExecutor {
public:
  using OutputCallback =
      std::function<void(double time_s, const std::vector<double> &q)>;

  //! Construct RT executor.
  //!
  //! @param on_tick Callback invoked at each 1kHz tick with current time and joint positions
  explicit RTExecutor(OutputCallback on_tick);

  ~RTExecutor();

  //! Update the active trajectory.
  //!
  //! Can be called safely from any thread. Uses a mutex for thread-safe
  //! handoff. The RT thread will pick up the new trajectory at the next tick.
  //!
  //! @param traj New trajectory to execute (or nullptr to hold last position)
  void setTrajectory(std::shared_ptr<const Trajectory> traj);

  //! Start the RT thread.
  //!
  //! Sets up POSIX SCHED_FIFO priority and CPU affinity.
  //! Requires CAP_SYS_NICE capability (or run as root in Docker).
  void start();

  //! Stop the RT thread and join.
  void stop();

  //! Check if RT executor is running.
  bool isRunning() const { return running_.load(); }

private:
  //! Main loop of the RT thread.
  //!
  //! Runs at 1kHz using clock_nanosleep with TIMER_ABSTIME for precise timing.
  void rtLoop();

  std::shared_ptr<const Trajectory> active_traj_{nullptr};
  mutable std::mutex traj_mutex_;

  std::atomic<bool> running_{false};

  OutputCallback on_tick_;
  std::thread thread_;

  // Local copies used within the RT loop (avoid atomic operations in hot path)
  std::shared_ptr<const Trajectory> local_traj_;
  double loop_time_s_{0.0};
  std::vector<double> last_q_; // Last joint positions
};

#endif // ROBOTICS_RT_DEMO_RT_EXECUTOR_HPP_
