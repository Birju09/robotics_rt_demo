from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps


class RoboticsRtDemoConan(ConanFile):
    name = "robotics_rt_demo"
    version = "0.1.0"
    description = "Real-time robotics application with UBSAN overhead analysis"

    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        self.requires("boost/1.85.0", options={
            "without_locale": True,
            "without_stacktrace": True,
        })
        self.requires("eigen/3.4.0")
        self.requires("ompl/1.7.0")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()
