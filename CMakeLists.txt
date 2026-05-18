cmake_minimum_required(VERSION 3.25)
project(robotics_rt_demo CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default install prefix to /app/install
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/app/install" CACHE PATH "Installation prefix" FORCE)
endif()

# Use link paths for RPATH to help executables find their libraries
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


find_package(ompl REQUIRED)
find_package(Boost REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(coal REQUIRED)
find_package(orocos-kdl REQUIRED)
find_package(kdl_parser REQUIRED)
find_package(rcpputils REQUIRED)
find_package(class_loader REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(pluginlib REQUIRED)
find_package(urdf REQUIRED)
find_package(urdfdom_headers REQUIRED)
find_package(urdfdom REQUIRED)
find_package(assimp REQUIRED)
find_package(TinyXML2 REQUIRED)

add_subdirectory(tests/examples)
add_subdirectory(src)

# Install configuration
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/install.cmake)
