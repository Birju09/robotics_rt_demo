add_library(motion_planning STATIC
    planner.cpp
)

target_include_directories(motion_planning PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(motion_planning PRIVATE
    ompl::ompl
)

target_link_libraries(motion_planning PUBLIC
    trajectory
    robot_model
    collision_detection
)
