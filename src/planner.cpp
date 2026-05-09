#include "planner.hpp"

#include <iostream>
#include <ompl/base/SpaceInformation.h>
#include <ompl/base/spaces/RealVectorStateSpace.h>
#include <ompl/geometric/SimpleSetup.h>
#include <ompl/geometric/planners/rrt/RRTstar.h>


namespace ob = ompl::base;
namespace og = ompl::geometric;


Planner::Planner(const RobotModel& model,
                 const CollisionScene& collision,
                 TrajectoryCallback on_new_trajectory)
    : model_(model),
      collision_(collision),
      callback_(on_new_trajectory) {
}

Planner::~Planner() {
    stop();
}

void Planner::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }
    thread_ = std::thread(&Planner::planningLoop, this);
}

void Planner::stop() {
    if (!running_.exchange(false)) {
        return;  // Not running
    }
    request_cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Planner::requestPlan(const KDL::Frame& target_pose,
                          double trajectory_duration_s,
                          const std::vector<double>& q_current) {
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        pending_request_ = PlanRequest{
            target_pose,
            trajectory_duration_s,
            q_current
        };
    }
    request_cv_.notify_one();
}

void Planner::planningLoop() {
    while (running_.load()) {
        std::optional<PlanRequest> request;

        {
            std::unique_lock<std::mutex> lock(request_mutex_);
            request_cv_.wait(lock, [this] {
                return !running_.load() || pending_request_.has_value();
            });

            if (!running_.load()) {
                break;
            }

            if (pending_request_.has_value()) {
                request = pending_request_;
                pending_request_.reset();
            }
        }

        if (!request.has_value()) {
            continue;
        }

        std::cout << "Planner: received request, solving IK..." << std::endl;

        // Solve IK for goal configuration
        std::vector<double> q_goal(model_.numJoints());
        if (!model_.ik(request->target, request->q_current, q_goal)) {
            std::cerr << "Planner: IK failed" << std::endl;
            continue;
        }

        std::cout << "Planner: IK succeeded, running OMPL..." << std::endl;

        // Run OMPL to find path from current to goal
        auto path = runOMPL(request->q_current, q_goal, 5.0);  // 5 second time limit
        if (path.empty()) {
            std::cerr << "Planner: OMPL planning failed" << std::endl;
            continue;
        }

        std::cout << "Planner: OMPL found path with " << path.size() << " waypoints"
                  << std::endl;

        // Fit cubic spline trajectory
        auto trajectory = std::make_shared<Trajectory>(
            fitCubicSpline(path, request->duration_s));

        std::cout << "Planner: trajectory ready, duration=" << trajectory->duration_s << "s"
                  << std::endl;

        // Invoke callback to hand off to RT executor
        callback_(trajectory);
    }
}

std::vector<std::vector<double>>
Planner::runOMPL(const std::vector<double>& q_start,
                 const std::vector<double>& q_goal,
                 double time_limit_s) {
    // Create state space (joint space, 7D for Franka)
    auto space = std::make_shared<ob::RealVectorStateSpace>(model_.numJoints());

    // Set joint limits
    std::vector<double> q_min, q_max;
    model_.jointLimits(q_min, q_max);
    ob::RealVectorBounds bounds(model_.numJoints());
    for (int i = 0; i < model_.numJoints(); ++i) {
        bounds.setLow(i, q_min[i]);
        bounds.setHigh(i, q_max[i]);
    }
    space->setBounds(bounds);

    // Create space information and set collision checker
    auto si = std::make_shared<ob::SpaceInformation>(space);
    si->setStateValidityChecker(collision_.makeOMPLChecker(si));
    si->setup();

    // Create simple setup
    og::SimpleSetup ss(si);

    // Set start and goal
    ob::ScopedState<ob::RealVectorStateSpace> start(space);
    for (int i = 0; i < model_.numJoints(); ++i) {
        start->values[i] = q_start[i];
    }
    ss.setStartState(start);

    ob::ScopedState<ob::RealVectorStateSpace> goal(space);
    for (int i = 0; i < model_.numJoints(); ++i) {
        goal->values[i] = q_goal[i];
    }
    ss.setGoalState(goal, 0.05);  // 5% tolerance

    // Set planner (RRTstar)
    auto planner = std::make_shared<og::RRTstar>(si);
    ss.setPlanner(planner);

    // Solve
    ob::PlannerStatus solved = ss.solve(time_limit_s);

    std::vector<std::vector<double>> path_waypoints;
    if (solved) {
        // Extract path from goal to start (OMPL stores it backwards)
        auto path = ss.getSolutionPath();
        for (size_t i = 0; i < path.getStateCount(); ++i) {
            auto* state = path.getState(i)->as<ob::RealVectorStateSpace::StateType>();
            std::vector<double> waypoint(model_.numJoints());
            for (int j = 0; j < model_.numJoints(); ++j) {
                waypoint[j] = state->values[j];
            }
            path_waypoints.push_back(waypoint);
        }
    }

    return path_waypoints;
}
