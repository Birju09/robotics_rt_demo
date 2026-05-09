from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain, CMakeDeps
from conan.tools.files import get, copy
import os


class ClassLoaderConan(ConanFile):
    name = "class_loader"
    version = "2.1.2"
    description = "ROS2 class loader library"
    url = "https://github.com/ros/class_loader"
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
        self.requires("rcpputils/2.10.0")
        self.requires("rcutils/6.1.0")

    def source(self):
        get(self,
            url="https://github.com/ros/class_loader/archive/refs/tags/{}.tar.gz".format(self.version),
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
        src = os.path.join(self.source_folder, "class_loader", "include", "class_loader")
        if os.path.exists(src):
            copy(self, "*", src=src, dst=os.path.join(self.package_folder, "include", "class_loader"), keep_path=True)

    def package_info(self):
        self.cpp_info.libs = ["class_loader"]
        self.cpp_info.includedirs = ["include"]
