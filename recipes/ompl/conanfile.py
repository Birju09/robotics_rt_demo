from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import get, copy, save
import os


class OmplConan(ConanFile):
    name = "ompl"
    version = "1.7.0"
    description = "The Open Motion Planning Library"
    url = "https://ompl.kavrakilab.org"
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

    def requirements(self):
        self.requires("boost/1.85.0", options={
            "without_locale": True,
            "without_stacktrace": True,
        })
        self.requires("eigen/3.4.0")

    def source(self):
        # TODO: Compute correct SHA256 for OMPL 1.7.0 tarball:
        # curl -L https://github.com/ompl/ompl/archive/refs/tags/1.7.0.tar.gz | sha256sum
        get(self,
            url="https://github.com/ompl/ompl/archive/refs/tags/1.7.0.tar.gz",
            strip_root=True)

    def export_sources(self):
        copy(self, "*.patch", os.path.join(self.recipe_folder, "patches"),
             os.path.join(self.export_sources_folder, "patches"))

    def _patch_sources(self):
        """Patch OMPL CMakeLists.txt to skip ldd/get_prerequisites calls that fail in containers"""
        cmake_file = os.path.join(self.source_folder, "CMakeLists.txt")
        content = open(cmake_file).read()
        # Comment out get_prerequisites calls that fail in Docker
        content = content.replace("get_prerequisites(", "# get_prerequisites(")
        save(self, cmake_file, content)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        # Disable OMPL's optional build targets
        tc.variables["OMPL_BUILD_DEMOS"] = False
        tc.variables["OMPL_BUILD_TESTS"] = False
        tc.variables["OMPL_BUILD_PYBINDINGS"] = False
        tc.variables["OMPL_BUILD_PYTESTS"] = False
        # Skip RPATH checks that fail in Docker/cross-compilation
        tc.variables["CMAKE_SKIP_RPATH"] = True
        tc.generate()

    def build(self):
        self._patch_sources()
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["ompl"]
