add_library(collision_detection STATIC
    collision_scene.cpp
)

target_include_directories(collision_detection PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(collision_detection PRIVATE
    assimp::assimp
    class_loader::class_loader
    rcutils::rcutils
    rcpputils::rcpputils
    console_bridge::console_bridge
    robot_model
    coal_gpu_collision_manager
)

target_link_libraries(collision_detection PUBLIC
    ompl::ompl
    coal::coal
    orocos-kdl::orocos-kdl
    kdl_parser::kdl_parser
)