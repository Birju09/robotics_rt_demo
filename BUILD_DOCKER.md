# Building in Docker (ros:jazzy)

This project can be built inside Docker using `ros:jazzy` as the base image. All required dependencies are installed automatically during the build process.

## Prerequisites

- Docker installed and running
- ~15GB free disk space (for dependencies)
- ~10 minutes build time on a modern machine

## Quick Start

### Docker Compose (Recommended)

Build using docker-compose with volume mounts:

```bash
# Release build (default)
docker-compose run --rm builder ./scripts/docker-build.sh Release

# Debug build
docker-compose run --rm builder ./scripts/docker-build.sh Debug

# Verbose output
docker-compose run --rm builder ./scripts/docker-build.sh Release --verbose
```

The `docker-compose.yml` includes:
- Volume mount for source code: `.:/app:rw`
- Volume mount for Conan cache: `~/.conan2:/root/.conan2:rw`
- Real-time capabilities: `cap_add: [SYS_NICE]`
- Real-time limits: `ulimits.rtprio: 99`
- Environment: `CC=clang-22`, `CXX=clang++-22`, venv Python path

### Docker Image Manual Build

If you prefer manual Docker commands:

```bash
# Build Docker image
docker build -t robotics_rt_demo:builder .

# Run build with volume mounts
docker run --rm \
    --cap-add SYS_NICE \
    --ulimit rtprio=99 \
    -v $(pwd):/app:rw \
    -v ~/.conan2:/root/.conan2:rw \
    -w /app \
    robotics_rt_demo:builder \
    ./scripts/docker-build.sh Release
```

### Manual Build (Inside Docker Shell)

```bash
# Start interactive container
docker-compose run --rm builder bash

# Inside container
./scripts/docker-build.sh Release    # Release build
./scripts/docker-build.sh Debug      # Debug build
```

## Dependency Installation Details

The Dockerfile and build script automatically install and configure the following:

### System Dependencies (Installed in Dockerfile)
- `build-essential` - GCC, Make, and development tools
- `cmake` - Build system configuration
- `python3` - Python interpreter
- `python3-pip` - Python package manager
- `python3-venv` - Python virtual environment support
- `pkg-config` - Library configuration
- `lsb-release`, `wget`, `gnupg` - Utilities
- `software-properties-common` - Package management utilities
- **LLVM 22 Toolchain**: `clang-22`, `clang++-22`, `llvm-config-22`, libc++, libc++abi, compiler-rt

### Python Packages (Preinstalled in venv at /opt/venv)
- `conan` - C++ package manager
- `catkin_pkg` - ROS2 package utilities
- `empy==3.3.4` - Template expansion tool (used by ROS2 packages like rcutils)

### Conan Packages (Local Recipes)

These are built from local recipes in `recipes/` directory:

1. **kdl_parser/2.10.0** - KDL parser for URDF
2. **coal/3.0.0** - Collision detection library
3. **orocos-kdl/1.5.1** - Kinematics and dynamics library
4. **urdfdom_headers/1.0.6** - URDF headers
5. **urdfdom/4.0.0** - URDF parser library
6. **ompl/1.7.0** - Motion planning library
7. **assimp/5.3.1** - 3D model loader
8. **franka_description/2.7.0** - Franka robot URDF and meshes
9. **urdf/2.14.0** - URDF support library
10. **rcutils/6.1.0** - ROS2 utilities
11. **performance_test_fixture/0.0.0** - Performance testing utilities
12. **urdf_parser_plugin/2.14.0** - URDF parser plugin
13. **pluginlib/5.4.1** - Plugin loading library
14. **class_loader/2.1.2** - Dynamic class loader
15. **ament_index_cpp/1.8.0** - ROS2 ament indexing
16. **rcpputils/2.10.0** - ROS2 C++ utilities

### CCI Packages (From Conan Center Index)

- `boost/1.85.0` - Boost C++ libraries
- `eigen/3.4.0` - Linear algebra library
- `tinyxml2/10.0.0` - XML parsing library

## Build Configuration

### Default Build Type

By default, **Release** build is used for optimal performance.

To change the build type:

```bash
./scripts/docker-build.sh Debug
```

Build types:
- **Release** - Optimized for performance (default)
- **Debug** - Includes debug symbols, slower execution
- **RelWithDebInfo** - Optimized with debug info

### Output Structure

After successful build with `docker-compose run --rm builder ./scripts/docker-build.sh Release`:

```
robotics_rt_demo/
├── build/release/                       # Build artifacts
│   ├── conan_toolchain.cmake
│   ├── conandeps.cmake
│   ├── src/
│   │   ├── CMakeFiles/
│   │   └── (object files, CMake intermediate)
│   └── ... (CMake build system files)
├── install/                             # CMAKE_INSTALL_PREFIX (via volume mount)
│   ├── bin/
│   │   └── panda_demo                   # Final executable
│   └── lib/
│       ├── librobots_core.a             # Static library
│       └── (Conan package libraries)
├── assets/                              # Generated at build time
│   └── franka_description/
│       ├── urdf/
│       │   ├── fr3.urdf                 # Processed from xacro
│       │   ├── fr3.urdf.xacro
│       │   └── *.yaml
│       └── meshes/
│           ├── collision/               # 8 .stl files
│           └── visual/                  # 8 .dae files (DAE format)
└── scripts/
    └── docker-build.sh
```

The install directory is synced from `/app/install` (inside Docker) to `./install/` on the project root via volume mount.

## Running the Application

### After Docker Build

The executable is installed to `./install/bin/panda_demo`:

```bash
# Run the executable
./install/bin/panda_demo
```

### With Real-Time Guarantees

To run with real-time scheduling (requires `SYS_NICE` capability):

```bash
# If running inside Docker with --cap-add SYS_NICE and --ulimit rtprio=99
./install/bin/panda_demo

# Or via docker-compose (already configured)
docker-compose run --rm builder /app/install/bin/panda_demo
```

### Using Installed Libraries

To link against the installed libraries in another project:

```bash
export LD_LIBRARY_PATH=$(pwd)/install/lib:$LD_LIBRARY_PATH
./install/bin/panda_demo
```

## Troubleshooting

### Build fails with "externally-managed-environment" error

This was fixed by:
1. Installing `python3-venv` in Dockerfile
2. Creating a Python virtual environment at `/opt/venv`
3. Pre-installing Conan and catkin_pkg in the venv
4. Setting `PATH` in docker-compose.yml to include `/opt/venv/bin`

If you still see this error, ensure docker-compose.yml has the correct PATH:

```yaml
environment:
  PATH: /opt/venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
```

And rebuild the Docker image:

```bash
docker-compose build --no-cache
```

### Build fails with missing recipe

Ensure all recipe directories exist in `recipes/`:

```bash
ls -la recipes/
```

Should show:
- `coal/`, `orocos-kdl/`, `urdfdom/`, etc.

### Assets not copied

The `franka_description` recipe must be built successfully. Check logs:

```bash
conan install . --build=missing -s build_type=Release
```

### Memory issues during build

If the build runs out of memory, reduce parallel build jobs:

```bash
# In docker-build.sh, change:
# cmake --build . --parallel $(nproc)
# to:
cmake --build . --parallel 2
```

## Development Workflow

For iterative development:

1. Start an interactive shell inside the container:

```bash
docker-compose run --rm builder bash
```

2. Inside the container, run the build script:

```bash
./scripts/docker-build.sh Release
```

3. Make source code changes on your host machine (changes sync via volume mount).

4. Rebuild incrementally:

```bash
cd /app/build/release
cmake --build . --parallel $(nproc)
```

5. Run the executable:

```bash
/app/install/bin/panda_demo
```

The volume mounts (`-v .:/app:rw` and `-v ~/.conan2:/root/.conan2:rw`) ensure:
- Source code changes sync between host and container
- Conan cache persists between builds (faster rebuilds)

## Build Script Steps

The `scripts/docker-build.sh` performs 5 steps:

1. **Setup Conan**: Detects/creates Conan profile if needed
2. **Export Local Recipes**: Exports 16 local recipes from `recipes/` directory
3. **Install Dependencies**: Runs `conan install` with build/host profiles
4. **Configure CMake**: Generates build system with Conan toolchain
5. **Build & Install**: Builds with CMake and installs to `/app/install` via CMAKE_INSTALL_PREFIX

Pass `--verbose` or `-v` to see detailed output from each step:

```bash
docker-compose run --rm builder ./scripts/docker-build.sh Release --verbose
```

## Advanced: Custom Build Options

Edit `scripts/docker-build.sh` to customize:

- **Compiler**: Set `CC` and `CXX` environment variables (already `clang-22` in docker-compose.yml)
- **Optimization flags**: Add to Conan profile or CMakeToolchain
- **Dependencies**: Add to `conanfile.py` requirements
- **Build jobs**: Modify `--parallel` argument in cmake command
- **Profiles**: Use different Conan profiles from `conan/profiles/`

Edit `conan/profiles/` to customize:

- **Compiler version**: Change `compiler.version`
- **C++ standard**: Change `compiler.cppstd`
- **Libcxx**: Change `compiler.libcxx` (currently `libc++` for Clang)
- **Toolchain**: Point to custom CMake toolchain files

## Performance Notes

**First build**: ~10-15 minutes (all packages compiled from source, LLVM 22 download)
**Subsequent builds**: ~2-3 minutes (cached dependencies via `~/.conan2` mount)
**Clean Docker image rebuild**: ~5-10 minutes (downloads LLVM 22, creates venv, installs Conan)

### Build Profiles

The project uses Conan profiles from `conan/profiles/`:

- **Build profile** (`conan/profiles/build`): Clang 22, AArch64, libc++, C++23
- **Host profile** (`conan/profiles/host`): Clang 22, AArch64, libc++, C++23

These profiles are passed to `conan install`:

```bash
conan install . \
    -pr:b conan/profiles/build \
    -pr:h conan/profiles/host \
    --build=missing
```

### Speeding Up Builds

The docker-compose.yml already mounts the Conan cache for persistence:

```yaml
volumes:
  - ~/.conan2:/root/.conan2:rw  # Conan cache persists between builds
  - .:/app:rw                    # Source code volume mount
```

To further optimize:
- Use the same build machine (cache grows over time)
- Increase `--parallel` jobs in cmake (limited by container memory)
- Pre-warm the Conan cache by running `conan install` on your host
