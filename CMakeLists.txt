cmake_minimum_required(VERSION 3.25)
project(robotics_rt_demo CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(ompl REQUIRED CONFIG)
find_package(Boost REQUIRED CONFIG)
find_package(Eigen3 REQUIRED CONFIG)
find_package(coal REQUIRED CONFIG)
find_package(orocos-kdl REQUIRED CONFIG)
find_package(kdl_parser REQUIRED CONFIG)
find_package(rcpputils REQUIRED CONFIG)
find_package(class_loader REQUIRED CONFIG)
find_package(ament_index_cpp REQUIRED CONFIG)
find_package(pluginlib REQUIRED CONFIG)
find_package(urdf REQUIRED CONFIG)
find_package(urdfdom_headers REQUIRED CONFIG)
find_package(urdfdom REQUIRED CONFIG)
find_package(assimp REQUIRED CONFIG)
find_package(TinyXML2 REQUIRED CONFIG)

add_subdirectory(tests/examples)
add_subdirectory(src)

