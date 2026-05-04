from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get
import os
import shutil


class KdlParserConan(ConanFile):
    name = "kdl_parser"
    version = "2.8.0"
    description = "URDF to KDL Parser"
    url = "https://github.com/ros/kdl_parser"
    license = "BSD-3-Clause"
    package_type = "library"

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
        self.requires("orocos-kdl/1.5.1")
        self.requires("urdfdom/4.0.0")
        self.requires("tinyxml2/10.0.0")
        self.requires("console_bridge/1.0.2")

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self,
            url="https://github.com/ros/kdl_parser/archive/refs/tags/2.8.0.tar.gz",
            strip_root=True)

        # Move kdl_parser subdirectory contents to source root
        kdl_src = os.path.join(self.source_folder, "kdl_parser")
        if os.path.isdir(kdl_src):
            for item in os.listdir(kdl_src):
                src_path = os.path.join(kdl_src, item)
                dst_path = os.path.join(self.source_folder, item)
                if os.path.isdir(src_path):
                    shutil.copytree(src_path, dst_path)
                else:
                    shutil.copy2(src_path, dst_path)
            shutil.rmtree(kdl_src)

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
        self.cpp_info.set_property("cmake_target_name", "kdl_parser::kdl_parser")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.libs = ["kdl_parser"]
