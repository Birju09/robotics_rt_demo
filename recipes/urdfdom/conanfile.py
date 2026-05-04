from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get


class UrdfdomConan(ConanFile):
    name = "urdfdom"
    version = "4.0.0"
    description = "URDF parsers for model description format"
    url = "https://github.com/ros/urdfdom"
    license = "BSD"
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
        self.requires("tinyxml2/10.0.0")
        self.requires("console_bridge/1.0.2")
        self.requires("urdfdom_headers/1.0.6")

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self,
            url="https://github.com/ros/urdfdom/archive/refs/tags/4.0.0.tar.gz",
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
        self.cpp_info.set_property("cmake_target_name", "urdfdom::urdfdom")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.libs = ["urdfdom_model", "urdfdom_world", "urdfdom_sensor"]
        self.cpp_info.includedirs = ["include/urdfdom"]
