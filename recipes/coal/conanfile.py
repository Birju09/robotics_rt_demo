from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get
import os


class CoalConan(ConanFile):
    name = "coal"
    version = "3.0.0"
    description = "Collision Object And Liquid library"
    url = "https://github.com/coal-library/coal"
    license = "BSD-2-Clause"
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
        self.requires("assimp/5.3.1")
        self.requires("boost/1.85.0", options={
            "without_locale": True,
            "without_stacktrace": True,
        })

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self,
            url="https://github.com/coal-library/coal/archive/refs/tags/v3.0.0.tar.gz",
            strip_root=True)

        # Patch root CMakeLists.txt for CMake version and Python detection
        cmakelists_path = os.path.join(self.source_folder, "CMakeLists.txt")
        with open(cmakelists_path, "r") as f:
            content = f.read()

        lines = content.split("\n")
        for i, line in enumerate(lines):
            if "cmake_minimum_required" in line:
                lines[i] = "cmake_minimum_required(VERSION 3.22)"
            if "FINDPYTHON" in line and not line.strip().startswith("#"):
                lines[i] = "# " + line

        content = "\n".join(lines)
        with open(cmakelists_path, "w") as f:
            f.write(content)

        # Patch src/CMakeLists.txt to disable assimp mesh loader source files
        src_cmakelists = os.path.join(self.source_folder, "src", "CMakeLists.txt")
        with open(src_cmakelists, "r") as f:
            content = f.read()

        lines = content.split("\n")
        for i, line in enumerate(lines):
            # Comment out mesh_loader source files
            if "mesh_loader/" in line and ".cpp" in line and not line.strip().startswith("#"):
                lines[i] = "  # " + line.lstrip()

        content = "\n".join(lines)
        with open(src_cmakelists, "w") as f:
            f.write(content)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["COAL_PYTHON_BINDINGS"] = False
        tc.variables["BUILD_TESTING"] = False
        tc.variables["COAL_BUILD_BENCHMARKS"] = False
        tc.variables["COAL_HAS_OCTOMAP"] = False
        tc.variables["COAL_HAS_ASSIMP"] = False
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        # Disable all optional dependencies to avoid searching for eigenpy, etc.
        tc.variables["ENABLE_OPTIONAL_DEPENDENCIES"] = False
        tc.variables["BUILD_PYTHON_INTERFACE"] = False
        tc.generate()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "coal::coal")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.libs = ["coal"]
