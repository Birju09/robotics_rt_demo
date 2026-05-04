from conan import ConanFile
from conan.tools.files import get, copy
import os


class UrdfdomHeadersConan(ConanFile):
    name = "urdfdom_headers"
    version = "1.0.6"
    description = "Headers for URDFDOM"
    url = "https://github.com/ros/urdfdom_headers"
    license = "BSD"
    package_type = "header-library"

    settings = "os", "arch", "compiler", "build_type"

    def source(self):
        get(self,
            url="https://github.com/ros/urdfdom_headers/archive/refs/tags/1.0.6.tar.gz",
            strip_root=True)

    def package(self):
        copy(self, "*.h",
             src=os.path.join(self.source_folder, "include"),
             dst=os.path.join(self.package_folder, "include"),
             keep_path=True)

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "urdfdom_headers::urdfdom_headers")
        # Header-only, no libs
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
