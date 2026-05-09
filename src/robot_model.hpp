#ifndef ROBOTICS_RT_DEMO_ROBOT_MODEL_HPP_
#define ROBOTICS_RT_DEMO_ROBOT_MODEL_HPP_

#include <string>
#include <vector>
#include <memory>
#include <kdl/tree.hpp>
#include <kdl/chain.hpp>
#include <kdl/frames.hpp>
#include <kdl/jntarray.hpp>
#include <kdl/chainfksolverpos_recursive.hpp>
#include <kdl/chainiksolvervel_pinv.hpp>
#include <kdl/chainiksolverpos_nr_jl.hpp>
#include <urdf/model.hpp>
#include <urdfdom/urdf_parser/urdf_parser.h>
#include <functional>

/**
 * @brief RobotModel encapsulates URDF-based kinematics using KDL.
 *
 * Responsibilities:
 *  - Parse URDF and build KDL kinematic chain
 *  - Provide FK (forward kinematics) solver
 *  - Provide IK (inverse kinematics) solver for Cartesian targets
 *  - Export joint limits and link mesh information for collision checking
 */
class RobotModel {
public:
    /**
     * @brief Mesh information associated with a link for collision detection.
     */
    struct LinkMesh {
        std::string link_name;        // e.g., "fr3_link1"
        std::string mesh_path;        // absolute path to .stl file
        KDL::Frame visual_offset;     // transform from link frame to mesh frame
    };

    /**
     * @brief Construct RobotModel from a URDF file.
     * @param urdf_path Absolute path to the URDF file (e.g., "assets/franka_description/urdf/fr3.urdf")
     * @throws std::runtime_error if URDF parse fails or KDL chain extraction fails
     */
    explicit RobotModel(const std::string& urdf_path);

    /**
     * @brief Number of joints in the kinematic chain.
     */
    int numJoints() const;

    /**
     * @brief Forward kinematics: joint positions -> end-effector frame.
     * @param q Joint positions (must have size == numJoints())
     * @param out_frame Output end-effector frame
     * @return true if FK succeeds, false otherwise
     */
    bool fk(const std::vector<double>& q, KDL::Frame& out_frame) const;

    /**
     * @brief Inverse kinematics: Cartesian target -> joint configuration.
     * @param target Target end-effector frame
     * @param q_seed Initial guess for joint configuration
     * @param q_out Output joint configuration (7 DOF for FR3)
     * @return true if IK converges, false if max iterations exceeded
     */
    bool ik(const KDL::Frame& target,
            const std::vector<double>& q_seed,
            std::vector<double>& q_out) const;

    /**
     * @brief Get joint lower and upper limits.
     * @param lower Output vector of lower joint limits
     * @param upper Output vector of upper joint limits
     */
    void jointLimits(std::vector<double>& lower, std::vector<double>& upper) const;

    /**
     * @brief Access link meshes for collision scene construction.
     */
    const std::vector<LinkMesh>& linkMeshes() const { return link_meshes_; }

private:
    KDL::Tree kdl_tree_;
    KDL::Chain kdl_chain_;        // "fr3_link0" -> "fr3_hand"

    std::vector<LinkMesh> link_meshes_;
    std::vector<double> q_min_, q_max_;  // joint limits

    // KDL solvers (mutable for const correctness in FK/IK calls)
    mutable std::unique_ptr<KDL::ChainFkSolverPos_recursive> fk_solver_;
    mutable std::unique_ptr<KDL::ChainIkSolverVel_pinv> ik_vel_solver_;
    mutable std::unique_ptr<KDL::ChainIkSolverPos_NR_JL> ik_solver_;

    /**
     * @brief Build KDL tree from URDF model.
     */
    void buildKDLTreeFromURDF(const urdf::ModelInterface& urdf_model);

    /**
     * @brief Extract the main kinematic chain and initialize solvers.
     */
    void extractChainAndInitSolvers(const std::string& base_link,
                                     const std::string& tip_link);

    /**
     * @brief Walk URDF links and collect visual mesh information.
     */
    void extractLinkMeshes(const urdf::ModelInterface& urdf_model);

    /**
     * @brief Resolve URDF mesh paths (handles "package://" URIs).
     */
    std::string resolveMeshPath(const std::string& uri);

    /**
     * @brief Print KDL tree structure for debugging.
     */
    void printKDLTree() const;

    /**
     * @brief Print URDF tree structure for debugging.
     */
    void printURDFTree(const urdf::ModelInterface& urdf_model) const;
};

#endif  // ROBOTICS_RT_DEMO_ROBOT_MODEL_HPP_
