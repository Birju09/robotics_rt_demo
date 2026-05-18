from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get


class ConsoleBridgeConan(ConanFile):
    name = "console_bridge"
    version = "1.0.2"
    description = "A ROS-agnostic library for logging"
    url = "https://github.com/ros-industrial/console_bridge"
    license = "BSD-3-Clause"
    package_type = "library"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": True,
        "fPIC": True,
    }

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self,
            url="https://github.com/ros/console_bridge/archive/refs/tags/1.0.2.tar.gz",
            strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "console_bridge::console_bridge")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.libs = ["console_bridge"]
