add_library(robot_model STATIC
    robot_model.cpp
)

target_include_directories(robot_model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(robot_model PUBLIC
    orocos-kdl::orocos-kdl
    kdl_parser::kdl_parser
    urdf::urdf
    urdfdom_headers::urdfdom_headers
    urdfdom::urdfdom
    tinyxml2::tinyxml2
)