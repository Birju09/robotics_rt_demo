from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain, CMakeDeps
from conan.tools.files import get, copy
import os


class PluginlibConan(ConanFile):
    name = "pluginlib"
    version = "5.4.1"
    description = "ROS2 plugin loader"
    url = "https://github.com/ros/pluginlib"
    license = "BSD"

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
        self.requires("class_loader/2.1.2")

    def source(self):
        get(self,
            url="https://github.com/ros/pluginlib/archive/refs/tags/{}.tar.gz".format(self.version),
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
        cmake.configure(build_script_folder="pluginlib")
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        src = os.path.join(self.source_folder, "pluginlib", "include", "pluginlib")
        if os.path.exists(src):
            copy(self, "*", src=src, dst=os.path.join(self.package_folder, "include", "pluginlib"), keep_path=True)

    def package_info(self):
        self.cpp_info.libs = []
        self.cpp_info.includedirs = ["include"]
