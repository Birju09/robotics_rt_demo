#ifndef ROBOTICS_RT_DEMO_PLANNER_HPP_
#define ROBOTICS_RT_DEMO_PLANNER_HPP_

#include "collision_scene.hpp"
#include "robot_model.hpp"
#include "trajectory.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <kdl/frames.hpp>
#include <mutex>
#include <optional>
#include <thread>

//! Planner runs motion planning in a background thread.
//!
//! Responsibilities:
//!  - Listen for planning requests (Cartesian target poses)
//!  - Solve IK to convert Cartesian goals to joint space
//!  - Run OMPL RRTstar to find collision-free paths
//!  - Fit cubic spline trajectories
//!  - Invoke callback when trajectory is ready
class Planner {
public:
  using TrajectoryCallback =
      std::function<void(std::shared_ptr<const Trajectory>)>;

  //! Construct planner.
  //!
  //! @param model Robot model for IK
  //! @param collision Collision scene for OMPL validity checking
  //! @param on_new_trajectory Callback invoked when trajectory is ready
  Planner(const RobotModel &model, const CollisionScene &collision,
          TrajectoryCallback on_new_trajectory);

  ~Planner();

  //! Start the background planning thread.
  void start();

  //! Request a plan to a Cartesian target.
  //!
  //! Thread-safe. If a plan is already in progress, this request becomes
  //! the new target (latest request wins).
  //!
  //! @param target_pose Target end-effector frame
  //! @param trajectory_duration_s Desired trajectory duration
  //! @param q_current Current joint configuration (IK seed)
  void requestPlan(const KDL::Frame &target_pose, double trajectory_duration_s,
                   const std::vector<double> &q_current);

  //! Stop the planning thread and join.
  void stop();

  //! Check if planner is currently running.
  bool isRunning() const { return running_.load(); }

private:
  struct PlanRequest {
    KDL::Frame target;
    double duration_s{0.0};
    std::vector<double> q_current;
  };

  const RobotModel &model_;
  const CollisionScene &collision_;
  TrajectoryCallback callback_;

  std::thread thread_;
  std::atomic<bool> running_{false};

  // Request queue (single-slot: latest request replaces previous)
  std::mutex request_mutex_;
  std::optional<PlanRequest> pending_request_;
  std::condition_variable request_cv_;

  //! Main loop of the planning thread.
  void planningLoop();

  //! Run OMPL RRTstar to find a path in joint space.
  //!
  //! @param q_start Starting joint configuration
  //! @param q_goal Goal joint configuration
  //! @param time_limit_s Time limit for planning
  //! @return Sequence of joint configurations if successful, empty vector otherwise
  std::vector<std::vector<double>> runOMPL(const std::vector<double> &q_start,
                                           const std::vector<double> &q_goal,
                                           double time_limit_s);
};

#endif // ROBOTICS_RT_DEMO_PLANNER_HPP_
