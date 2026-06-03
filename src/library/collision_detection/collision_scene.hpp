#ifndef ROBOTICS_RT_DEMO_COLLISION_SCENE_HPP_
#define ROBOTICS_RT_DEMO_COLLISION_SCENE_HPP_

#include "robot_model.hpp"

#include <coal/BVH/BVH_model.h>
#include <coal/broadphase/broadphase_collision_manager.h>
#include <coal/collision.h>
#include <kdl/frames.hpp>
#include <memory>
#include <ompl/base/SpaceInformation.h>
#include <unordered_set>
#include <vector>

//! CollisionScene manages collision detection for the robot using COAL.
//!
//! Responsibilities:
//!  - Load robot link meshes and build collision models
//!  - Check collisions between robot and environment
//!  - Provide an OMPL-compatible state validity checker
class CollisionScene {
public:
  enum class Mode { CPU, GPU };

  //! Construct collision scene from robot model.
  //!
  //! @param model RobotModel with link meshes
  //! @param mode  CPU uses NaiveCollisionManager; GPU uses
  //! CoalGPUCollisionManager
  explicit CollisionScene(const RobotModel &model, Mode mode);

  //! Check if a robot configuration is collision-free.
  //!
  //! @param q Joint configuration (7D for Franka)
  //! @return true if no collisions, false otherwise
  bool isCollisionFree(const std::vector<double> &q) const;

  //! Create an OMPL state validity checker function.
  //!
  //! @param si SpaceInformation for the OMPL problem
  //! @return Function compatible with ompl::base::setStateValidityChecker
  ompl::base::StateValidityCheckerFn
  makeOMPLChecker(std::shared_ptr<ompl::base::SpaceInformation> si) const;

private:
  struct LinkCollisionObject {
    std::string link_name;
    std::shared_ptr<coal::BVHModel<coal::OBBRSS>> bvh_model;
    coal::Transform3s local_transform; // mesh offset in link frame
    std::shared_ptr<coal::CollisionObject> collision_obj;
  };

  std::vector<LinkCollisionObject> link_objects_;
  const RobotModel &model_;
  std::unique_ptr<coal::BroadPhaseCollisionManager> broadphase_;

  // Set of "nameA:::nameB" (both orderings) for kinematically adjacent links
  std::unordered_set<std::string> adjacent_pairs_;

  //! Load a mesh file and create a COAL BVH model.
  //!
  //! @param mesh_path Path to .stl file
  //! @return BVH model, or nullptr if load fails
  std::shared_ptr<coal::BVHModel<coal::OBBRSS>>
  loadMesh(const std::string &mesh_path) const;

  //! Build adjacency exclusion list to skip checks between connected links.
  void buildAdjacencyList();

  //! Update all collision object transforms for a given joint config.
  void updateCollisionTransforms(const std::vector<double> &q) const;

  //! Check collisions between all relevant link pairs.
  bool checkCollisions() const;
};

#endif // ROBOTICS_RT_DEMO_COLLISION_SCENE_HPP_
