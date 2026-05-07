from conan import ConanFile
from conan.tools.files import copy, get
import os


class FrankaDescriptionConan(ConanFile):
    name = "franka_description"
    version = "2.7.0"
    description = "Franka Panda robot URDF and mesh files (FR3 only)"
    url = "https://github.com/frankarobotics/franka_description"

    def source(self):
        # Download the repository from the correct branch/tag
        get(self,
            url="https://github.com/frankarobotics/franka_description/archive/refs/tags/{}.tar.gz".format(self.version),
            destination=self.source_folder)
        # The archive extracts to franka_description-VERSION, move contents up
        import shutil
        extracted_dir = os.path.join(self.source_folder, "franka_description-{}".format(self.version))
        if os.path.exists(extracted_dir):
            for item in os.listdir(extracted_dir):
                src = os.path.join(extracted_dir, item)
                dst = os.path.join(self.source_folder, item)
                if os.path.isdir(src):
                    shutil.copytree(src, dst, dirs_exist_ok=True)
                else:
                    shutil.copy2(src, dst)
            shutil.rmtree(extracted_dir)

    def build(self):
        # Process FR3 xacro file to URDF
        import subprocess
        import glob
        fr3_xacro = os.path.join(self.source_folder, "robots", "fr3", "fr3.urdf.xacro")
        fr3_urdf = os.path.join(self.source_folder, "robots", "fr3", "fr3.urdf")
        robots_dir = os.path.join(self.source_folder, "robots")

        if os.path.exists(fr3_xacro):
            # Patch all .xacro files in robots directory (they include each other)
            xacro_files = glob.glob(os.path.join(robots_dir, "**", "*.xacro"), recursive=True)

            def patch_xacro_content(content):
                """Replace all franka_description package references with relative paths."""
                # Running xacro from robots/fr3/ directory. Convert package paths to relative:
                # $(find franka_description)/robots/common/ -> ../common/
                # $(find franka_description)/meshes/ -> ../../meshes/
                # $(find franka_description)/ -> ../../ (fallback)
                patched = content.replace(
                    "$(find franka_description)/robots/",
                    "../"
                )
                patched = patched.replace(
                    "$(find franka_description)/meshes/",
                    "../../meshes/"
                )
                patched = patched.replace(
                    "$(find franka_description)/",
                    "../../"
                )
                # Also replace package://franka_description for mesh paths
                patched = patched.replace(
                    "package://franka_description/meshes/",
                    "../../meshes/"
                )
                patched = patched.replace(
                    "package://franka_description/",
                    "../../"
                )
                return patched

            # Patch all xacro files in place
            self.output.info(f"Patching {len(xacro_files)} xacro files...")
            for xacro_file in xacro_files:
                with open(xacro_file, 'r') as f:
                    original_content = f.read()
                patched_content = patch_xacro_content(original_content)
                with open(xacro_file, 'w') as f:
                    f.write(patched_content)
                self.output.info(f"  Patched: {os.path.relpath(xacro_file, self.source_folder)}")

            try:
                # Use xacro to convert fr3.urdf.xacro to .urdf
                # Run from fr3 directory so includes work with relative paths
                self.output.info(f"Running xacro from: {os.path.dirname(fr3_xacro)}")
                with open(fr3_urdf, "w") as out:
                    result = subprocess.run(
                        ["xacro", "fr3.urdf.xacro"],
                        stdout=out, stderr=subprocess.PIPE, text=True,
                        cwd=os.path.dirname(fr3_xacro)
                    )
                if result.returncode != 0:
                    self.output.error(f"xacro stderr:\n{result.stderr}")
                    raise subprocess.CalledProcessError(result.returncode, result.args)

                # Post-process URDF to fix mesh paths
                # The xacro ran from robots/fr3/, so paths are ../../meshes/robots/fr3/
                # But in the package, we flatten robots/fr3/* to meshes/*, so fix paths
                with open(fr3_urdf, 'r') as f:
                    urdf_content = f.read()

                # Fix relative mesh paths: ../../meshes/robots/fr3/ -> ../meshes/
                urdf_content = urdf_content.replace(
                    "../../meshes/robots/fr3/",
                    "../meshes/"
                )

                # Remove package:// URIs completely, use relative paths instead
                # package://franka_description/meshes/robots/fr3/ -> ../meshes/
                urdf_content = urdf_content.replace(
                    "package://franka_description/meshes/robots/fr3/",
                    "../meshes/"
                )
                urdf_content = urdf_content.replace(
                    "package://franka_description/meshes/",
                    "../meshes/"
                )
                urdf_content = urdf_content.replace(
                    "package://franka_description/",
                    "../"
                )

                with open(fr3_urdf, 'w') as f:
                    f.write(urdf_content)

                self.output.info(f"Processed FR3 xacro to URDF: {fr3_urdf}")
                self.output.info(f"Fixed mesh paths in URDF")
            except (subprocess.CalledProcessError, FileNotFoundError) as e:
                self.output.warning(f"xacro processing failed: {e}")
                self.output.info("Packaging xacro file as-is")

    def package(self):
        # Copy only FR3 URDF and relevant meshes
        fr3_robots_src = os.path.join(self.source_folder, "robots", "fr3")
        fr3_meshes_src = os.path.join(self.source_folder, "meshes", "robots", "fr3")

        self.output.info(f"Source folder: {self.source_folder}")
        self.output.info(f"FR3 robots src: {fr3_robots_src} (exists: {os.path.exists(fr3_robots_src)})")
        self.output.info(f"FR3 meshes src: {fr3_meshes_src} (exists: {os.path.exists(fr3_meshes_src)})")

        # List directory contents for debugging
        if os.path.exists(self.source_folder):
            self.output.info(f"Source folder contents: {os.listdir(self.source_folder)}")
        if os.path.exists(fr3_robots_src):
            self.output.info(f"FR3 robots contents: {os.listdir(fr3_robots_src)}")
        if os.path.exists(fr3_meshes_src):
            self.output.info(f"FR3 meshes contents: {os.listdir(fr3_meshes_src)}")

        # Copy FR3 URDF files (prefer .urdf if processed, fallback to .xacro)
        if os.path.exists(fr3_robots_src):
            copy(self, "fr3.urdf",
                 src=fr3_robots_src,
                 dst=os.path.join(self.package_folder, "urdf"),
                 keep_path=False)
            copy(self, "fr3.urdf.xacro",
                 src=fr3_robots_src,
                 dst=os.path.join(self.package_folder, "urdf"),
                 keep_path=False)
            copy(self, "*.yaml",
                 src=fr3_robots_src,
                 dst=os.path.join(self.package_folder, "urdf"),
                 keep_path=False)
            self.output.info(f"Packaged FR3 URDFs")
        else:
            self.output.warning(f"FR3 robots source not found at {fr3_robots_src}")

        # Copy FR3 mesh files
        if os.path.exists(fr3_meshes_src):
            copy(self, "*",
                 src=fr3_meshes_src,
                 dst=os.path.join(self.package_folder, "meshes"))
            self.output.info(f"Packaged FR3 meshes")
        else:
            self.output.warning(f"FR3 meshes source not found at {fr3_meshes_src}")

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []

        urdf_path = os.path.join(self.package_folder, "urdf")
        mesh_path = os.path.join(self.package_folder, "meshes")

        self.buildenv_info.define("FRANKA_DESCRIPTION_URDF_PATH", urdf_path)
        self.buildenv_info.define("FRANKA_DESCRIPTION_MESH_PATH", mesh_path)
