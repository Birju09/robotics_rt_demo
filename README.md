Understanding UBSAN overhead in Real time Robotics Application
==============================================================

This repo is attempt at exploring usage of sanitizers in real time applications. The repo contains a quintessential robot motion planning example using OMPL library on a Franka robot.

The goals of this exercise are:
- Build a robotics application where robot controller is taking a trajectory from the motion planner and executing.
- Make it soft real time compliant. i.e. the real time loop is consistently running at some frequency.
- Collect base line performance data.
- Insert UBSAN instrumentation in the code.
- Understand what sites are instrumented UBSAN.
- Measure overhead.


Operating systems
--------------------------

- Ubuntu 24
- QNX8 (potentially)

Platform beign targetted
------------------------
- Aarch64

Hardware Platform
-----------------
- FRDM iMX95

Compiler
--------
- Clang 22.1


Package Management
-------------------
- Conan



Build instructions
-------------------

1. Run Conan install
```
conan install -pr:b conan/profiles/build   -pr:h conan/profiles/host -s build_type=Release   --output-folder=build/release   --build=missing .
```

1.a
For first time, the conan recipes needed for the build have to be exposed
```
conan export recipes/kdl_parser --name=kdl_parser --version=2.8.0
conan export recipes/coal --name=coal --version=3.0.0
conan export recipes/orocos-kdl --name=orocos-kdl --version=1.5.1
conan export recipes/urdfdom_headers --name=urdfdom_headers --version=1.0.6
conan export recipes/urdfdom --name=urdfdom --version=4.0.0
conan export recipes/ompl --name=ompl --version=1.7.0
conan export recipes/assimp --name=assimp --version=5.3.1
conan export recipes/franka_description --name=franka_description --version=2.7.0
conan export recipes/urdf --name=urdf --version=2.14.0
conan export recipes/rcutils --name=rcutils --version=7.2.0
conan export recipes/performance_test_fixture --name=performance_test_fixture --version=0.0.0
conan export recipes/urdf_parser_plugin --name=urdf_parser_plugin --version=2.14.0
```

2. Run cmake
```
cmake --preset release
```


3. Build
```
cmake --build --preset release
```