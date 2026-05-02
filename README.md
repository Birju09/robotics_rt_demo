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


Platforms being targetted
--------------------------

- Ubuntu 24
- QNX8 (potentially)


Compiler
--------
- Clang 22.1
