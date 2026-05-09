#include "trajectory.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

std::vector<double> Trajectory::eval(double t) const {
  // Clamp t to [0, duration_s]
  t = std::max(0.0, std::min(t, duration_s));

  std::vector<double> result(num_joints, 0.0);

  if (segments.empty() || segments[0].empty()) {
    return result;
  }

  // Find the segment containing time t using binary search
  int seg_idx = 0;
  for (int i = 0; i < num_segments; ++i) {
    if (segments[0][i].t_start <= t &&
        (i == num_segments - 1 || segments[0][i + 1].t_start > t)) {
      seg_idx = i;
      break;
    }
  }

  // Evaluate each joint at time t
  for (int j = 0; j < num_joints; ++j) {
    const Segment &seg = segments[j][seg_idx];
    double tau = t - seg.t_start; // local time within segment

    // Cubic evaluation: q(tau) = a + b*tau + c*tau^2 + d*tau^3
    result[j] = seg.coeff[0] + seg.coeff[1] * tau + seg.coeff[2] * tau * tau +
                seg.coeff[3] * tau * tau * tau;
  }

  return result;
}

Trajectory fitCubicSpline(const std::vector<std::vector<double>> &waypoints,
                          double duration_s) {
  Trajectory traj;

  int n_wp = waypoints.size();
  if (n_wp < 2) {
    std::cerr << "fitCubicSpline: need at least 2 waypoints" << std::endl;
    return traj;
  }

  if (waypoints[0].empty()) {
    std::cerr << "fitCubicSpline: waypoints have zero size" << std::endl;
    return traj;
  }

  int n_joints = waypoints[0].size();
  int n_segments = n_wp - 1;

  traj.duration_s = duration_s;
  traj.num_joints = n_joints;
  traj.num_segments = n_segments;

  // Uniform time parameterization
  std::vector<double> t_knots(n_wp);
  for (int i = 0; i < n_wp; ++i) {
    t_knots[i] = (duration_s / (n_wp - 1)) * i;
  }

  // Segment durations
  std::vector<double> h(n_segments);
  for (int i = 0; i < n_segments; ++i) {
    h[i] = t_knots[i + 1] - t_knots[i];
  }

  // For each joint, fit a natural cubic spline
  traj.segments.resize(n_joints);

  for (int j = 0; j < n_joints; ++j) {
    // Extract waypoint values for this joint
    std::vector<double> y(n_wp);
    for (int i = 0; i < n_wp; ++i) {
      y[i] = waypoints[i][j];
    }

    // Compute second derivatives using natural cubic spline (Thomas algorithm)
    // Natural boundary conditions: M[0] = M[n-1] = 0

    std::vector<double> alpha(n_segments, 0.0);
    for (int i = 1; i < n_segments; ++i) {
      alpha[i] = (3.0 / h[i]) * (y[i + 1] - y[i]) -
                 (3.0 / h[i - 1]) * (y[i] - y[i - 1]);
    }

    // Solve tridiagonal system: l * M = alpha
    // Using Thomas algorithm (forward sweep, back substitution)
    std::vector<double> l(n_wp, 1.0);
    std::vector<double> mu(n_segments, 0.0);
    std::vector<double> z(n_wp, 0.0);

    for (int i = 1; i < n_segments; ++i) {
      l[i] = 2.0 * (h[i - 1] + h[i]) - h[i - 1] * mu[i - 1];
      mu[i] = h[i] / l[i];
      z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }

    // Back substitution
    std::vector<double> M(n_wp, 0.0);
    M[n_wp - 1] = 0.0; // Natural boundary: M[n-1] = 0
    for (int i = n_wp - 2; i >= 0; --i) {
      M[i] = z[i] - mu[i] * M[i + 1];
    }

    // Construct segments and compute coefficients
    traj.segments[j].resize(n_segments);

    for (int i = 0; i < n_segments; ++i) {
      Trajectory::Segment seg;
      seg.t_start = t_knots[i];
      seg.h = h[i];

      // Cubic segment coefficients
      // q(tau) = a + b*tau + c*tau^2 + d*tau^3
      double h_inv = 1.0 / h[i];
      double h_sq_inv = h_inv * h_inv;

      seg.coeff[0] = y[i]; // a = y[i]
      seg.coeff[1] =
          (y[i + 1] - y[i]) * h_inv - h[i] * (2.0 * M[i] + M[i + 1]) / 6.0; // b
      seg.coeff[2] = M[i] / 2.0;                                            // c
      seg.coeff[3] = (M[i + 1] - M[i]) / (6.0 * h[i]);                      // d

      traj.segments[j][i] = seg;
    }
  }

  return traj;
}
