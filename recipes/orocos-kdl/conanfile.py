from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get, copy
import os
import shutil


class OrocosKDLConan(ConanFile):
    name = "orocos-kdl"
    version = "1.5.1"
    description = "Orocos Kinematics and Dynamics Library"
    url = "https://github.com/orocos/orocos_kinematics_dynamics"
    license = "LGPL"
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
        self.requires("eigen/3.4.0")

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self,
            url="https://github.com/orocos/orocos_kinematics_dynamics/archive/refs/tags/v1.5.1.tar.gz",
            strip_root=True)

        # Move orocos_kdl subdirectory contents to source root
        kdl_src = os.path.join(self.source_folder, "orocos_kdl")
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
        self.cpp_info.set_property("cmake_target_name", "orocos-kdl::orocos-kdl")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.libs = ["orocos-kdl"]
