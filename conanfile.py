from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps


class RoboticsRtDemoConan(ConanFile):
    name = "robotics_rt_demo"
    version = "0.1.0"
    description = "Real-time robotics application with UBSAN overhead analysis"

    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        self.requires("boost/1.85.0", options={
            "without_locale": True,
            "without_stacktrace": True,
        })
        self.requires("eigen/3.4.0")
        self.requires("ompl/1.7.0")
        self.requires("coal/3.0.0")
        self.requires("orocos-kdl/1.5.1")
        self.requires("urdf/2.0.0")
        self.requires("urdfdom_headers/1.0.6")
        self.requires("urdfdom/4.0.0")
        self.requires("assimp/5.3.1")
        self.requires("tinyxml2/10.0.0")
        self.requires("franka_description/2.7.0")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

        # Copy franka_description assets to repo
        import shutil
        import os
        from pathlib import Path

        # Access the franka_description package directly via dependencies
        if "franka_description" in self.dependencies:
            pkg_folder = Path(self.dependencies["franka_description"].package_folder)
            urdf_path = pkg_folder / "urdf"
            mesh_path = pkg_folder / "meshes"

            self.output.info(f"Franka description package folder: {pkg_folder}")
            self.output.info(f"URDF path: {urdf_path} (exists: {urdf_path.exists()})")
            self.output.info(f"Mesh path: {mesh_path} (exists: {mesh_path.exists()})")

            if mesh_path.exists():
                self.output.info(f"Mesh path contents: {list(mesh_path.iterdir())}")
                mesh_files = list(mesh_path.rglob("*"))
                self.output.info(f"Total items found by rglob: {len(mesh_files)}")
                actual_files = [f for f in mesh_files if f.is_file()]
                self.output.info(f"Actual files (not dirs): {len(actual_files)}")
                for mf in actual_files[:3]:  # Show first 3 actual files
                    self.output.info(f"  - {mf}")

            if urdf_path.exists() and mesh_path.exists():
                # Use absolute path: current working directory is the project root during conan install
                assets_dir = Path(os.getcwd()) / "assets" / "franka_description"
                assets_dir.mkdir(parents=True, exist_ok=True)

                # Copy URDFs
                urdf_dst = assets_dir / "urdf"
                urdf_dst.mkdir(exist_ok=True)
                urdf_count = 0
                for f in urdf_path.glob("*"):
                    if f.is_file():
                        shutil.copy2(f, urdf_dst / f.name)
                        urdf_count += 1
                self.output.info(f"Copied {urdf_count} URDF files to {urdf_dst}")

                # Copy meshes
                mesh_dst = assets_dir / "meshes"
                mesh_dst.mkdir(exist_ok=True)
                mesh_count = 0
                self.output.info(f"Starting mesh copy to {mesh_dst}")
                for f in mesh_path.rglob("*"):
                    if f.is_file():
                        rel_path = f.relative_to(mesh_path)
                        dst_file = mesh_dst / rel_path
                        dst_dir = dst_file.parent
                        dst_dir.mkdir(parents=True, exist_ok=True)
                        try:
                            shutil.copy2(f, dst_file)
                            mesh_count += 1
                        except Exception as e:
                            self.output.error(f"Failed to copy {f} to {dst_file}: {e}")
                self.output.info(f"Copied {mesh_count} mesh files to {mesh_dst}")
            else:
                self.output.warning(f"URDF path {urdf_path} or mesh path {mesh_path} not found")
        else:
            self.output.warning("franka_description dependency not found")
