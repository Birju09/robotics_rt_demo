#ifndef ROBOTICS_RT_DEMO_TRAJECTORY_HPP_
#define ROBOTICS_RT_DEMO_TRAJECTORY_HPP_

#include <array>
#include <memory>
#include <vector>

//! A trajectory represented as natural cubic splines.
//!
//! Works with any number of degrees of freedom (DOFs): joint positions, Cartesian positions, etc.
//! For each DOF, the trajectory is a piecewise cubic polynomial:
//!   x(t) = a + b*t + c*t^2 + d*t^3  (within each segment)
//!
//! Knot times are uniformly distributed across the total duration.
struct Trajectory {
  double duration_s{0.0}; // Total trajectory duration
  int num_dofs{0};        // Number of DOFs (e.g., 7 for Franka joints, 6 for Cartesian pose)
  int num_segments{0};    // Number of segments (waypoints - 1)

  //! A cubic polynomial segment for one DOF.
  struct Segment {
    double t_start{0.0}; // Absolute start time of this segment
    double h{0.0};       // Duration of this segment
    std::array<double, 4> coeff{
        {0, 0, 0, 0}}; // [a, b, c, d] cubic coefficients
  };

  // segments[dof_index][segment_index]
  std::vector<std::vector<Segment>> segments;

  //! Evaluate trajectory at time t.
  //!
  //! @param t Time (clamped to [0, duration_s])
  //! @return Position values for all DOFs
  std::vector<double> eval(double t) const;
};

//! Fit a natural cubic spline through waypoints.
//!
//! Uses natural boundary conditions (second derivative = 0 at boundaries).
//! Waypoints are uniformly distributed in time.
//! Works with any DOFs: joint positions, Cartesian positions, orientations, etc.
//!
//! @param waypoints Sequence of configurations [waypoint_idx][dof_idx]
//! @param duration_s Total desired trajectory duration
//! @return Trajectory object ready for evaluation
Trajectory fitCubicSpline(const std::vector<std::vector<double>> &waypoints,
                          double duration_s);

#endif // ROBOTICS_RT_DEMO_TRAJECTORY_HPP_
