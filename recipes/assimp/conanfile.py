from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get
import os


class AssimpConan(ConanFile):
    name = "assimp"
    version = "5.3.1"
    description = "Open Asset Import Library"
    url = "https://github.com/assimp/assimp"
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

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self,
            url="https://github.com/assimp/assimp/archive/refs/tags/v5.3.1.tar.gz",
            strip_root=True)

    def build(self):
        # Fix Draco's missing <algorithm> include by patching the file directly
        ply_reader_path = os.path.join(self.source_folder, "contrib/draco/src/draco/io/ply_reader.cc")
        if os.path.exists(ply_reader_path):
            with open(ply_reader_path, "r") as f:
                content = f.read()

            # Add #include <algorithm> after the copyright header
            if "#include <algorithm>" not in content:
                # Find the first #include and insert before it
                lines = content.split("\n")
                insert_pos = 0
                for i, line in enumerate(lines):
                    if line.startswith("#include"):
                        insert_pos = i
                        break

                lines.insert(insert_pos, "#include <algorithm>")
                content = "\n".join(lines)

                with open(ply_reader_path, "w") as f:
                    f.write(content)
                self.output.info("Patched ply_reader.cc: added #include <algorithm>")

        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.variables["ASSIMP_BUILD_TESTS"] = False
        tc.variables["ASSIMP_BUILD_SAMPLES"] = False

        # Suppress -Wnontrivial-memcall warning in SceneCombiner.cpp
        # assimp uses memcpy on non-trivially copyable types, which is safe for this use case
        tc.extra_cxxflags = ["-Wno-nontrivial-memcall"]

        # Ensure C files don't inherit C++ standard library flags
        # CMake by default may pass CMAKE_CXX_FLAGS to C compilation
        # We fix this by explicitly configuring CMake to not do so
        tc.variables["CMAKE_C_STANDARD"] = "11"
        tc.variables["CMAKE_C_STANDARD_REQUIRED"] = "ON"
        tc.variables["CMAKE_C_EXTENSIONS"] = "ON"  # Enable POSIX extensions for vendored zip lib

        tc.generate()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "assimp::assimp")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.libs = ["assimp"]
        self.cpp_info.includedirs = ["include"]
