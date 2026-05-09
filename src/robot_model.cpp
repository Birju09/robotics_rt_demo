#include "robot_model.hpp"

#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <kdl/chainfksolverpos_recursive.hpp>
#include <kdl/chainiksolvervel_pinv.hpp>
#include <kdl/chainiksolverpos_nr_jl.hpp>
#include <kdl_parser/kdl_parser.hpp>
#include <urdf/model.hpp>


RobotModel::RobotModel(const std::string& urdf_path) {
    // Read URDF file as string
    std::ifstream urdf_file(urdf_path);
    if (!urdf_file.is_open()) {
        throw std::runtime_error("Failed to open URDF file: " + urdf_path);
    }
    std::stringstream buffer;
    buffer << urdf_file.rdbuf();
    std::string urdf_string = buffer.str();

    // Parse URDF from string
    auto urdf_model_ptr = urdf::parseURDF(urdf_string);
    if (!urdf_model_ptr) {
        throw std::runtime_error("Failed to parse URDF from " + urdf_path);
    }
    const auto& urdf_model = *urdf_model_ptr;

    // Build KDL tree from URDF
    buildKDLTreeFromURDF(urdf_model);

    // Extract kinematic chain and initialize solvers
    // For Franka FR3: root is "fr3_link0", tip is "fr3_hand"
    extractChainAndInitSolvers("fr3_link0", "fr3_hand");

    // Extract joint limits and mesh information
    // Resize limit vectors (chain is now initialized)
    if (q_min_.size() != (size_t)kdl_chain_.getNrOfJoints()) {
        q_min_.resize(kdl_chain_.getNrOfJoints());
        q_max_.resize(kdl_chain_.getNrOfJoints());
    }

    int joint_idx = 0;
    for (size_t i = 0; i < kdl_chain_.getNrOfSegments(); ++i) {
        const KDL::Segment& seg = kdl_chain_.getSegment(i);
        if (seg.getJoint().getType() != KDL::Joint::None) {
            const auto& urdf_joint = urdf_model.getJoint(seg.getName());
            if (urdf_joint && urdf_joint->limits) {
                q_min_[joint_idx] = urdf_joint->limits->lower;
                q_max_[joint_idx] = urdf_joint->limits->upper;
            } else {
                // Default limits if not specified
                q_min_[joint_idx] = -M_PI;
                q_max_[joint_idx] = M_PI;
            }
            joint_idx++;
        }
    }

    // Extract visual meshes for collision checking
    extractLinkMeshes(urdf_model);

    std::cout << "RobotModel initialized: " << kdl_chain_.getNrOfJoints() << " joints, "
              << link_meshes_.size() << " meshes" << std::endl;
}

int RobotModel::numJoints() const {
    return kdl_chain_.getNrOfJoints();
}

bool RobotModel::fk(const std::vector<double>& q, KDL::Frame& out_frame) const {
    if (q.size() != static_cast<size_t>(kdl_chain_.getNrOfJoints())) {
        std::cerr << "FK: joint size mismatch. Expected " << kdl_chain_.getNrOfJoints()
                  << ", got " << q.size() << std::endl;
        return false;
    }

    KDL::JntArray q_kdl(kdl_chain_.getNrOfJoints());
    for (size_t i = 0; i < q.size(); ++i) {
        q_kdl(i) = q[i];
    }

    int ret = fk_solver_->JntToCart(q_kdl, out_frame);
    return ret >= 0;  // KDL returns >= 0 for success
}

bool RobotModel::ik(const KDL::Frame& target,
                    const std::vector<double>& q_seed,
                    std::vector<double>& q_out) const {
    if (q_seed.size() != static_cast<size_t>(kdl_chain_.getNrOfJoints())) {
        std::cerr << "IK: seed size mismatch. Expected " << kdl_chain_.getNrOfJoints()
                  << ", got " << q_seed.size() << std::endl;
        return false;
    }

    KDL::JntArray q_seed_kdl(kdl_chain_.getNrOfJoints());
    for (size_t i = 0; i < q_seed.size(); ++i) {
        q_seed_kdl(i) = q_seed[i];
    }

    KDL::JntArray q_sol(kdl_chain_.getNrOfJoints());
    int ret = ik_solver_->CartToJnt(q_seed_kdl, target, q_sol);

    if (ret >= 0) {
        q_out.resize(kdl_chain_.getNrOfJoints());
        for (int i = 0; i < kdl_chain_.getNrOfJoints(); ++i) {
            q_out[i] = q_sol(i);
        }
        return true;
    }
    return false;
}

void RobotModel::jointLimits(std::vector<double>& lower, std::vector<double>& upper) const {
    lower = q_min_;
    upper = q_max_;
}

void RobotModel::printKDLTree() const {
    std::cout << "\n=== KDL Tree Structure ===" << std::endl;
    std::cout << "Tree has " << kdl_tree_.getNrOfJoints() << " joints and "
              << kdl_tree_.getNrOfSegments() << " segments" << std::endl;
    std::cout << "==========================\n" << std::endl;
}

void RobotModel::printURDFTree(const urdf::ModelInterface& urdf_model) const {
    std::cout << "\n=== URDF Tree Structure ===" << std::endl;
    if (!urdf_model.getRoot()) {
        std::cout << "No root link found" << std::endl;
        return;
    }

    std::cout << "Root: " << urdf_model.getRoot()->name << std::endl;

    // Helper lambda to recursively print link hierarchy
    std::function<void(const urdf::LinkConstSharedPtr&, int)> print_hierarchy =
        [&](const urdf::LinkConstSharedPtr& link, int indent) {
            std::string indent_str(indent * 2, ' ');
            for (const auto& child : link->child_links) {
                const auto& joint = urdf_model.getJoint(child->parent_joint->name);
                std::string joint_type = "UNKNOWN";
                if (joint) {
                    switch (joint->type) {
                        case urdf::Joint::REVOLUTE:
                            joint_type = "REVOLUTE";
                            break;
                        case urdf::Joint::CONTINUOUS:
                            joint_type = "CONTINUOUS";
                            break;
                        case urdf::Joint::PRISMATIC:
                            joint_type = "PRISMATIC";
                            break;
                        case urdf::Joint::FIXED:
                            joint_type = "FIXED";
                            break;
                        default:
                            joint_type = "OTHER";
                    }
                }
                std::cout << indent_str << "├─ " << child->name
                          << " (joint: " << child->parent_joint->name
                          << ", type: " << joint_type << ")" << std::endl;
                print_hierarchy(child, indent + 1);
            }
        };

    print_hierarchy(urdf_model.getRoot(), 0);
    std::cout << "============================\n" << std::endl;
}

void RobotModel::buildKDLTreeFromURDF(const urdf::ModelInterface& urdf_model) {
    // Build KDL tree from URDF model by recursively walking the link tree
    if (!urdf_model.getRoot()) {
        throw std::runtime_error("URDF model has no root link");
    }

    // Print URDF tree structure for debugging
    printURDFTree(urdf_model);

    // Use kdl_parser to build the KDL tree from the URDF model
    if (!kdl_parser::treeFromUrdfModel(urdf_model, kdl_tree_)) {
        throw std::runtime_error("Failed to build KDL tree from URDF model");
    }
}

void RobotModel::extractChainAndInitSolvers(const std::string& base_link,
                                            const std::string& tip_link) {
    if (!kdl_tree_.getChain(base_link, tip_link, kdl_chain_)) {
        throw std::runtime_error("Failed to extract chain from " + base_link +
                                 " to " + tip_link);
    }

    // Initialize FK solver
    fk_solver_ = std::make_unique<KDL::ChainFkSolverPos_recursive>(kdl_chain_);

    // Initialize IK solvers
    ik_vel_solver_ = std::make_unique<KDL::ChainIkSolverVel_pinv>(kdl_chain_);

    // Use default joint limits for IK solver initialization
    // These will be updated with URDF limits later in the constructor
    KDL::JntArray q_min_kdl(kdl_chain_.getNrOfJoints());
    KDL::JntArray q_max_kdl(kdl_chain_.getNrOfJoints());
    for (int i = 0; i < kdl_chain_.getNrOfJoints(); ++i) {
        q_min_kdl(i) = -M_PI;
        q_max_kdl(i) = M_PI;
    }
    ik_solver_ = std::make_unique<KDL::ChainIkSolverPos_NR_JL>(
        kdl_chain_, q_min_kdl, q_max_kdl, *fk_solver_, *ik_vel_solver_);
}

void RobotModel::extractLinkMeshes(const urdf::ModelInterface& urdf_model) {
    for (const auto& [link_name, link] : urdf_model.links_) {
        if (!link->visual || !link->visual->geometry) {
            continue;
        }

        // Only handle mesh geometries for now
        if (link->visual->geometry->type != urdf::Geometry::MESH) {
            continue;
        }

        const auto* mesh_geom = dynamic_cast<const urdf::Mesh*>(link->visual->geometry.get());
        if (!mesh_geom) {
            continue;
        }

        LinkMesh lm;
        lm.link_name = link_name;
        lm.mesh_path = resolveMeshPath(mesh_geom->filename);

        // Extract visual offset transform
        const urdf::Pose& pose = link->visual->origin;
        double x = pose.position.x, y = pose.position.y, z = pose.position.z;
        double rx = pose.rotation.x, ry = pose.rotation.y, rz = pose.rotation.z, rw = pose.rotation.w;
        lm.visual_offset = KDL::Frame(
            KDL::Rotation::Quaternion(rx, ry, rz, rw),
            KDL::Vector(x, y, z)
        );

        link_meshes_.push_back(lm);
    }
}

std::string RobotModel::resolveMeshPath(const std::string& uri) {
    // For the demo, assume meshes are in assets/franka_description/meshes/
    // or handle "package://" URIs by stripping the prefix
    if (uri.find("package://") == 0) {
        // e.g., "package://franka_description/meshes/visual/link.stl"
        // -> "assets/franka_description/meshes/visual/link.stl"
        std::string relative = uri.substr(10);  // skip "package://"
        size_t first_slash = relative.find('/');
        if (first_slash != std::string::npos) {
            // Skip package name, keep the rest
            relative = relative.substr(first_slash + 1);
        }
        // For now, prepend "assets/"
        return "assets/" + relative;
    }
    // Otherwise assume it's already a valid path
    return uri;
}
