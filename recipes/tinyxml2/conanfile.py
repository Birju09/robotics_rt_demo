from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get


class TinyXML2Conan(ConanFile):
    name = "tinyxml2"
    version = "10.0.0"
    description = "A small, efficient C++ XML parser library"
    url = "https://github.com/leethomason/tinyxml2"
    license = "Zlib"
    package_type = "library"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": True,
        "fPIC": True,
    }

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self,
            url="https://github.com/leethomason/tinyxml2/archive/refs/tags/10.0.0.tar.gz",
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
        self.cpp_info.set_property("cmake_target_name", "tinyxml2::tinyxml2")
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.libs = ["tinyxml2"]
