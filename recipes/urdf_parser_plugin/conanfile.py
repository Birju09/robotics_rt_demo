from conan import ConanFile
from conan.tools.files import copy, get
import os


class UrdfParserPluginConan(ConanFile):
    name = "urdf_parser_plugin"
    version = "2.14.0"
    description = "URDF parser plugin library (ROS2) - header only"
    url = "https://github.com/ros2/urdf"

    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    def requirements(self):
        pass  # No dependencies - just provides headers

    def source(self):
        # Download from the same ROS2 urdf repository
        get(self,
            url="https://github.com/ros2/urdf/archive/refs/tags/{}.tar.gz".format(self.version),
            destination=self.source_folder,
            strip_root=True)

    def package(self):
        # Copy headers from urdf_parser_plugin/include/urdf_parser_plugin to include/urdf_parser_plugin
        src = os.path.join(self.source_folder, "urdf_parser_plugin", "include", "urdf_parser_plugin")
        if os.path.exists(src):
            copy(self, "*",
                 src=src,
                 dst=os.path.join(self.package_folder, "include", "urdf_parser_plugin"),
                 keep_path=True)

    def package_info(self):
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.set_property("cmake_target_name", "urdf_parser_plugin::urdf_parser_plugin")
        self.cpp_info.set_property("cmake_file_name", "urdf_parser_plugin")
        self.cpp_info.set_property("cmake_find_mode", "both")
        # No libs - this is a header-only package
