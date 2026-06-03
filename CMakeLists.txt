cmake_minimum_required(VERSION 3.25)
project(robotics_rt_demo CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default install prefix to /app/install
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/app/install" CACHE PATH "Installation prefix" FORCE)
endif()

# Use link paths for RPATH to help executables find their libraries
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Third party
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/third_party.cmake)

add_subdirectory(llvm)
add_subdirectory(tests/examples)
add_subdirectory(src)
add_subdirectory(app)

# Install configuration
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/install.cmake)
