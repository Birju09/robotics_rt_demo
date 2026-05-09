#include "robot_model.hpp"
#include "collision_scene.hpp"
#include "planner.hpp"
#include "rt_executor.hpp"

#include <iostream>
#include <thread>
#include <iomanip>
#include <kdl/frames.hpp>


int main() {
    try {
        std::cout << "=== Franka Panda RT Motion Planning Demo ===" << std::endl;
        std::cout << std::endl;

        // Load robot model from URDF (FR3 only)
        std::cout << "Loading robot model..." << std::endl;
        RobotModel robot("../../assets/franka_description/urdf/fr3.urdf");
        std::cout << "  Joint count: " << robot.numJoints() << std::endl;
        std::cout << "  Link meshes: " << robot.linkMeshes().size() << std::endl;
        std::cout << std::endl;

        // Create collision scene
        std::cout << "Initializing collision scene..." << std::endl;
        CollisionScene collision(robot);
        std::cout << std::endl;

        // Create and start RT executor
        std::cout << "Starting RT executor (1kHz, SCHED_FIFO)..." << std::endl;
        int output_counter = 0;  // Print every 10th tick
        auto on_tick = [&output_counter](double time_s, const std::vector<double>& q) {
            if (output_counter++ % 10 == 0) {
                std::cout << "  [" << std::fixed << std::setprecision(3) << time_s << "s] q = [";
                for (size_t i = 0; i < q.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << std::fixed << std::setprecision(2) << q[i];
                }
                std::cout << "]" << std::endl;
            }
        };

        RTExecutor rt_executor(on_tick);
        rt_executor.start();
        std::cout << std::endl;

        // Create and start planner
        std::cout << "Starting planner thread..." << std::endl;
        auto on_new_trajectory = [&rt_executor](std::shared_ptr<const Trajectory> traj) {
            std::cout << "Planner: handing trajectory to RT executor" << std::endl;
            rt_executor.setTrajectory(traj);
        };

        Planner planner(robot, collision, on_new_trajectory);
        planner.start();
        std::cout << std::endl;

        // Get current joint position (zero config for start)
        std::vector<double> q_current(robot.numJoints(), 0.0);
        std::cout << "Current config (all zeros): [";
        for (size_t i = 0; i < q_current.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << q_current[i];
        }
        std::cout << "]" << std::endl;

        // Request a plan to a nearby Cartesian target
        std::cout << std::endl;
        std::cout << "Requesting plan to target Cartesian pose..." << std::endl;

        // Target pose: 0.3m forward, 0.2m to the left, at same height
        // (This is a relative target; exact values depend on Franka forward kinematics)
        KDL::Frame target;
        target.p = KDL::Vector(0.3, 0.2, 0.5);  // Rough estimate, will be refined by visualization
        target.M = KDL::Rotation::Identity();    // Identity orientation

        planner.requestPlan(target, 5.0, q_current);  // 5 second trajectory duration
        std::cout << std::endl;

        // Let the demo run for 30 seconds
        std::cout << "Demo running for 30 seconds..." << std::endl;
        std::cout << "  (Planning in background, trajectory will play back when ready)" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(30));

        // Shutdown
        std::cout << std::endl;
        std::cout << "Shutting down..." << std::endl;
        planner.stop();
        rt_executor.stop();

        std::cout << "Demo complete." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
