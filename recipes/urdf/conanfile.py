from conan import ConanFile
from conan.tools.files import copy
import os


class UrdfConan(ConanFile):
    name = "urdf"
    version = "2.0.0"
    description = "URDF parser - system package wrapper for ROS2"

    def package_info(self):
        # Point to system ROS2 installation
        ros2_include = "/opt/ros/jazzy/include/urdf"
        ros2_lib = "/opt/ros/jazzy/lib"

        self.cpp_info.includedirs = [ros2_include]
        self.cpp_info.libdirs = [ros2_lib]
        self.cpp_info.libs = ["urdf", "urdf_xml_parser"]
