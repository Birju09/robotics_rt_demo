#ifndef ROBOTICS_RT_DEMO_TRAJECTORY_HPP_
#define ROBOTICS_RT_DEMO_TRAJECTORY_HPP_

#include <array>
#include <memory>
#include <vector>

/**
 * @brief A trajectory represented as per-joint natural cubic splines.
 *
 * For each joint, the trajectory is a piecewise cubic polynomial:
 *   q(t) = a + b*t + c*t^2 + d*t^3  (within each segment)
 *
 * Knot times are uniformly distributed across the total duration.
 */
struct Trajectory {
  double duration_s{0.0}; // Total trajectory duration
  int num_joints{0};      // Number of DOFs (e.g., 7 for Franka)
  int num_segments{0};    // Number of segments (waypoints - 1)

  /**
   * @brief A cubic polynomial segment for one joint.
   */
  struct Segment {
    double t_start{0.0}; // Absolute start time of this segment
    double h{0.0};       // Duration of this segment
    std::array<double, 4> coeff{
        {0, 0, 0, 0}}; // [a, b, c, d] cubic coefficients
  };

  // segments[joint_index][segment_index]
  std::vector<std::vector<Segment>> segments;

  /**
   * @brief Evaluate trajectory at time t.
   * @param t Time (clamped to [0, duration_s])
   * @return Joint positions for all DOFs
   */
  std::vector<double> eval(double t) const;
};

/**
 * @brief Fit a natural cubic spline through waypoints.
 *
 * Uses natural boundary conditions (second derivative = 0 at boundaries).
 * Waypoints are uniformly distributed in time.
 *
 * @param waypoints Sequence of joint configurations [waypoint_idx][joint_idx]
 * @param duration_s Total desired trajectory duration
 * @return Trajectory object ready for evaluation
 */
Trajectory fitCubicSpline(const std::vector<std::vector<double>> &waypoints,
                          double duration_s);

#endif // ROBOTICS_RT_DEMO_TRAJECTORY_HPP_
