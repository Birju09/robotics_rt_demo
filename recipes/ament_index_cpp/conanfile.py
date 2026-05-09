from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain, CMakeDeps
from conan.tools.files import get, copy
import os


class AmentIndexCppConan(ConanFile):
    name = "ament_index_cpp"
    version = "1.8.0"
    description = "ROS2 ament index C++ library"
    url = "https://github.com/ament/ament_index"
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

    def source(self):
        get(self,
            url="https://github.com/ament/ament_index/archive/refs/tags/{}.tar.gz".format(self.version),
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
        cmake.configure(build_script_folder="ament_index_cpp")
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        src = os.path.join(self.source_folder, "ament_index_cpp", "include", "ament_index_cpp")
        if os.path.exists(src):
            copy(self, "*", src=src, dst=os.path.join(self.package_folder, "include", "ament_index_cpp"), keep_path=True)

    def package_info(self):
        self.cpp_info.libs = ["ament_index_cpp"]
        self.cpp_info.includedirs = ["include"]
