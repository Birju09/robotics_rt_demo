cmake_minimum_required(VERSION 3.25)
project(robotics_rt_demo CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add ROS2 installation to prefix path for finding system packages
list(APPEND CMAKE_PREFIX_PATH /opt/ros/jazzy)

find_package(ompl REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG)
find_package(Eigen3 REQUIRED CONFIG)
find_package(coal REQUIRED CONFIG)
find_package(orocos-kdl REQUIRED CONFIG)
find_library(URDF_LIBRARY urdf REQUIRED)
find_library(URDF_XML_PARSER_LIBRARY urdf_xml_parser REQUIRED)
find_package(urdfdom_headers REQUIRED CONFIG)
find_package(urdfdom REQUIRED CONFIG)
find_package(assimp REQUIRED CONFIG)
find_package(TinyXML2 REQUIRED CONFIG)

add_subdirectory(tests/examples)
add_subdirectory(src)

