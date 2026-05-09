from conan import ConanFile
from conan.tools.files import get, copy
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain, CMakeDeps


class PerformanceTestFixtureConan(ConanFile):
    name = "performance_test_fixture"
    version = "0.0.0"
    description = "ROS2 performance test fixture"
    url = "https://github.com/ros2/performance_test_fixture"

    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    def source(self):
        get(self,
            url="https://github.com/ros2/performance_test_fixture/archive/refs/heads/rolling.tar.gz",
            destination=self.source_folder,
            strip_root=True)

    def package(self):
        # performance_test_fixture is primarily CMake modules and headers
        copy(self, "*.cmake",
             src=self.source_folder,
             dst=self.package_folder,
             keep_path=True)
        copy(self, "*.h",
             src=self.source_folder,
             dst=self.package_folder,
             keep_path=True)
        copy(self, "*.hpp",
             src=self.source_folder,
             dst=self.package_folder,
             keep_path=True)

    def package_info(self):
        # Header-only / CMake module package
        pass
