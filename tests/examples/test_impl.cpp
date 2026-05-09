#include <iostream>
#include <ompl/base/SpaceInformation.h>
#include <ompl/base/spaces/SE3StateSpace.h>
#include <ompl/geometric/SimpleSetup.h>
#include <ompl/geometric/planners/rrt/RRTstar.h>

namespace ob = ompl::base;
namespace og = ompl::geometric;

// Validity checker: simple box constraint
bool isStateValid(const ob::State *state) {
  auto *se3state = state->as<ob::SE3StateSpace::StateType>();
  auto *pos = se3state->as<ob::RealVectorStateSpace::StateType>(0);

  // Allow motion within [-2, 2] in x, y, z
  double x = pos->values[0];
  double y = pos->values[1];
  double z = pos->values[2];

  // Obstacle: sphere at origin with radius 0.5
  double dist = std::sqrt(x * x + y * y + z * z);
  if (dist < 0.5) {
    return false;
  }

  // Bounds check
  return x >= -2.0 && x <= 2.0 && y >= -2.0 && y <= 2.0 && z >= 0.0 && z <= 4.0;
}

int main() {
  std::cout << "=== OMPL Rigid Body Planning Example ===" << std::endl;

  // Create state space: SE(3) - 3D position + orientation
  auto space = std::make_shared<ob::SE3StateSpace>();

  // Define bounds
  ob::RealVectorBounds bounds(3);
  bounds.setLow(-2.0);
  bounds.setHigh(2.0);
  space->setBounds(bounds);

  // Create space information
  auto si = std::make_shared<ob::SpaceInformation>(space);
  si->setStateValidityChecker(
      [](const ob::State *state) { return isStateValid(state); });
  si->setup();

  std::cout << "State space dimension: " << si->getStateSpace()->getDimension()
            << std::endl;

  og::SimpleSetup ss(si);

  ob::ScopedState<ob::SE3StateSpace> start(space);
  start->setXYZ(-1.5, -1.5, 1.0);
  start->as<ob::SO3StateSpace::StateType>(1)->setIdentity();
  ss.setStartState(start);

  ob::ScopedState<ob::SE3StateSpace> goal(space);
  goal->setXYZ(1.5, 1.5, 1.0);
  goal->as<ob::SO3StateSpace::StateType>(1)->setIdentity();
  ss.setGoalState(goal, 0.1); // tolerance: 0.1 units

  auto planner = std::make_shared<og::RRTstar>(si);
  ss.setPlanner(planner);

  std::cout << "\nSolving..." << std::endl;
  ob::PlannerStatus solved = ss.solve(2.0);

  if (solved) {
    std::cout << "Solution found!" << std::endl;

    // Get path
    auto path = ss.getSolutionPath();
    std::cout << "Path length: " << path.length() << std::endl;
    std::cout << "Path states: " << path.getStateCount() << std::endl;

    // Print path waypoints
    std::cout << "\nPath waypoints:" << std::endl;
    for (size_t i = 0; i < path.getStateCount(); ++i) {
      auto *state = path.getState(i)->as<ob::SE3StateSpace::StateType>();
      auto *pos = state->as<ob::RealVectorStateSpace::StateType>(0);
      printf("  [%zu] (%.3f, %.3f, %.3f)\n", i, pos->values[0], pos->values[1],
             pos->values[2]);
    }

    return 0;
  } else {
    std::cout << "No solution found." << std::endl;
    return 1;
  }
}
