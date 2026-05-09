from conan import ConanFile
from conan.tools.files import get
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain, CMakeDeps


class RcutilsConan(ConanFile):
    name = "rcutils"
    version = "6.1.0"
    description = "ROS2 rcutils library"
    url = "https://github.com/ros2/rcutils"

    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        self.requires("performance_test_fixture/0.0.0")

    def build_requirements(self):
        pass

    def source(self):
        get(self,
            url="https://github.com/ros2/rcutils/archive/refs/tags/{}.tar.gz".format(self.version),
            destination=self.source_folder,
            strip_root=True)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        # Disable tests to avoid requiring test dependencies (mimick_vendor, etc.)
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

    def package_info(self):
        self.cpp_info.libs = ["rcutils"]
        self.cpp_info.includedirs = ["include/rcutils"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.bindirs = ["bin"]
        self.cpp_info.set_property("cmake_target_name", "rcutils::rcutils")
        self.cpp_info.set_property("cmake_file_name", "rcutils")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.set_property("cmake_target_name", "rcutils::rcutils")
        self.cpp_info.set_property("cmake_file_name", "rcutils")
        self.cpp_info.set_property("cmake_find_mode", "both")
