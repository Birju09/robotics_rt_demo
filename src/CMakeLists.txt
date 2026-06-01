add_library(robot_core STATIC
    robot_model.cpp
    collision_scene.cpp
    trajectory.cpp
    planner.cpp
    rt_executor.cpp
)

target_include_directories(robot_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(robot_core PUBLIC
    ompl::ompl
    coal::coal
    orocos-kdl::orocos-kdl
    kdl_parser::kdl_parser
    urdf::urdf
    urdfdom_headers::urdfdom_headers
    urdfdom::urdfdom
    Eigen3::Eigen
    assimp::assimp
    tinyxml2::tinyxml2
    class_loader::class_loader
    rcutils::rcutils
    rcpputils::rcpputils
    console_bridge::console_bridge
)

target_compile_options(robot_core PRIVATE "-fsanitize=undefined")
target_link_libraries(robot_core PRIVATE "-fsanitize=undefined")

target_compile_options(robot_core PRIVATE
    -fpass-plugin=$<TARGET_FILE:UBSanCountPlugin>)

add_dependencies(robot_core UBSanCountPlugin)

add_executable(panda_demo main.cpp)
target_link_libraries(panda_demo PRIVATE robot_core)
target_compile_features(panda_demo PRIVATE cxx_std_23)

target_compile_options(panda_demo PRIVATE
    -fsanitize=undefined
    -fpass-plugin=$<TARGET_FILE:UBSanCountPlugin>)
target_link_libraries(panda_demo PRIVATE "-fsanitize=undefined")
add_dependencies(panda_demo UBSanCountPlugin)

# Install targets
install(TARGETS panda_demo
    RUNTIME DESTINATION bin
)

install(TARGETS robot_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
