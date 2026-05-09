from conan import ConanFile
from conan.tools.files import copy, get, load, save
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain, CMakeDeps
import os


class UrdfConan(ConanFile):
    name = "urdf"
    version = "2.14.0"
    description = "URDF parser library (ROS2)"
    url = "https://github.com/ros2/urdf"

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
        # Core dependencies
        self.requires("urdfdom_headers/1.0.6")
        self.requires("urdfdom/4.0.0")
        self.requires("tinyxml2/10.0.0")

        # ROS2 dependencies
        self.requires("rcutils/6.1.0")
        self.requires("urdf_parser_plugin/2.14.0")
        self.requires("pluginlib/5.4.1")
        self.requires("class_loader/2.1.2")
        self.requires("ament_index_cpp/1.8.0")
        self.requires("rcpputils/2.10.0")

    def source(self):
        # Download the specific tag from ROS2 urdf repository
        get(self,
            url="https://github.com/ros2/urdf/archive/refs/tags/{}.tar.gz".format(self.version),
            destination=self.source_folder,
            strip_root=True)

    def build_requirements(self):
        pass

    def export_sources(self):
        pass

    def _patch_cmake(self):
        # Patch urdf/CMakeLists.txt to add urdfdom include directories and disable pluginlib
        cmake_path = os.path.join(self.source_folder, "urdf", "CMakeLists.txt")
        if os.path.exists(cmake_path):
            content = load(self, cmake_path)
            # Get full include paths from dependencies
            urdfdom_dep = self.dependencies["urdfdom"]
            urdfdom_headers_dep = self.dependencies["urdfdom_headers"]
            urdfdom_pkg = urdfdom_dep.package_folder
            urdfdom_headers_pkg = urdfdom_headers_dep.package_folder
            urdfdom_include = os.path.join(urdfdom_pkg, urdfdom_dep.cpp_info.includedirs[0])
            urdfdom_headers_include = os.path.join(urdfdom_headers_pkg, urdfdom_headers_dep.cpp_info.includedirs[0])

            # Add explicit include directories for urdfdom and urdfdom_headers after find_package
            if "find_package(urdfdom" in content:
                content = content.replace(
                    "find_package(urdfdom REQUIRED)",
                    f"find_package(urdfdom REQUIRED)\ninclude_directories({urdfdom_include} {urdfdom_headers_include})"
                )

            # Add find_package for rcpputils and ament_index_cpp
            if "find_package(urdfdom" in content and "find_package(rcpputils" not in content:
                content = content.replace(
                    "find_package(urdfdom REQUIRED)",
                    "find_package(ament_index_cpp REQUIRED)\nfind_package(rcpputils REQUIRED)\nfind_package(urdfdom REQUIRED)"
                )

            # Comment out pluginlib_export_plugin_description_file call
            content = content.replace(
                "pluginlib_export_plugin_description_file",
                "# pluginlib_export_plugin_description_file"
            )

            # Add rcpputils and ament_index_cpp to target_link_libraries
            if "rcutils::rcutils" in content and "rcpputils::rcpputils" not in content:
                content = content.replace(
                    "rcutils::rcutils",
                    "rcutils::rcutils\n  rcpputils::rcpputils\n  ament_index_cpp::ament_index_cpp"
                )

            save(self, cmake_path, content)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_TESTING"] = "OFF"
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        self._patch_cmake()
        cmake = CMake(self)
        cmake.configure(build_script_folder="urdf")
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        # Also copy urdf headers from source to package
        copy(self, "*.h*",
             src=os.path.join(self.source_folder, "urdf", "include", "urdf"),
             dst=os.path.join(self.package_folder, "include", "urdf"),
             keep_path=True)

    def package_info(self):
        self.cpp_info.libs = ["urdf"]
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.bindirs = ["bin"]
