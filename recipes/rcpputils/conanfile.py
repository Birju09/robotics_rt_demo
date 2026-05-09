from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain, CMakeDeps
from conan.tools.files import get, copy
import os


class RcpputilsConan(ConanFile):
    name = "rcpputils"
    version = "2.10.0"
    description = "C++ utilities for ROS2"
    url = "https://github.com/ros2/rcpputils"
    license = "Apache-2.0"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
    }

    def requirements(self):
        self.requires("rcutils/6.1.0")

    def source(self):
        get(self,
            url="https://github.com/ros2/rcpputils/archive/refs/tags/{}.tar.gz".format(self.version),
            destination=self.source_folder,
            strip_root=True)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_TESTING"] = "OFF"
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        # Copy headers
        src = os.path.join(self.source_folder, "include", "rcpputils")
        if os.path.exists(src):
            copy(self, "*", src=src, dst=os.path.join(self.package_folder, "include", "rcpputils"), keep_path=True)

    def package_info(self):
        self.cpp_info.libs = ["rcpputils"]
        self.cpp_info.includedirs = ["include"]
