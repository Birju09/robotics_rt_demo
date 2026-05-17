#!/bin/bash
################################################################################
# Build script for robotics_rt_demo inside Docker (ros:jazzy)
#
# This script:
# 1. Sets up Conan (if needed, though it's preinstalled in Docker image)
# 2. Exports local Conan recipes
# 3. Installs all dependencies (Conan packages + CCI)
# 4. Configures the project with CMake
# 5. Builds the project and installs artifacts to /app/robotics_rt_demo/install
#
# Usage:
#   ./scripts/docker-build.sh [build_type] [--verbose]
#   ./scripts/docker-build.sh                  # default: Release
#   ./scripts/docker-build.sh Debug
#   ./scripts/docker-build.sh Release --verbose
################################################################################

set -e  # Exit on error

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_TYPE="${1:-Release}"
VERBOSE="${2:-}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE_LOWER=$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')
BUILD_DIR="${PROJECT_ROOT}/build/${BUILD_TYPE_LOWER}"

# Add Python user bin to PATH (for pip-installed tools like conan)
export PATH="${HOME}/Library/Python/3.9/bin:${HOME}/.local/bin:/usr/local/bin:$PATH"

# Verbose mode
if [ "$VERBOSE" = "--verbose" ] || [ "$VERBOSE" = "-v" ]; then
  set -x  # Print commands as they execute
  QUIET=""
else
  QUIET="-qq"
fi

# Ensure we're in the project root
cd "$PROJECT_ROOT"

echo -e "${YELLOW}=== Robotics RT Demo Build Script ===${NC}"
echo "Build type: $BUILD_TYPE"
echo "Project root: $PROJECT_ROOT"
echo "Build directory: $BUILD_DIR"
echo

################################################################################
# Step 1: Setup Conan (if not already installed)
################################################################################
echo -e "${YELLOW}[1/5] Setting up Conan...${NC}"

if ! command -v conan &> /dev/null; then
    echo "  Installing Conan..."
    if [ -z "$VERBOSE" ]; then
        pip3 install $QUIET conan 2>&1 | grep -v "WARNING: The script" || true
    else
        pip3 install conan
    fi
else
    echo "  Conan already installed: $(conan --version)"
fi

# Always detect/create the profile (even if conan was already installed)
echo "  Detecting Conan profile..."
if ! conan profile detect --force 2>&1 | grep -q "Detected"; then
    echo "  ${YELLOW}Warning: Profile detection had issues, trying again...${NC}"
    conan profile detect --force
fi

echo -e "${GREEN}✓ Conan ready${NC}"
echo

################################################################################
# Step 2: Export local Conan recipes
################################################################################
echo -e "${YELLOW}[2/5] Exporting local Conan recipes...${NC}"

# List of local recipes to export
LOCAL_RECIPES=(
    "kdl_parser:kdl_parser:2.10.0"
    "coal:coal:3.0.0"
    "orocos-kdl:orocos-kdl:1.5.1"
    "urdfdom_headers:urdfdom_headers:1.0.6"
    "urdfdom:urdfdom:4.0.0"
    "ompl:ompl:1.7.0"
    "assimp:assimp:5.3.1"
    "franka_description:franka_description:2.7.0"
    "urdf:urdf:2.14.0"
    "rcutils:rcutils:6.1.0"
    "performance_test_fixture:performance_test_fixture:0.0.0"
    "urdf_parser_plugin:urdf_parser_plugin:2.14.0"
    "pluginlib:pluginlib:5.4.1"
    "class_loader:class_loader:2.1.2"
    "ament_index_cpp:ament_index_cpp:1.8.0"
    "rcpputils:rcpputils:2.10.0"
)

for recipe_entry in "${LOCAL_RECIPES[@]}"; do
    IFS=':' read -r recipe_dir recipe_name recipe_version <<< "$recipe_entry"
    recipe_path="${PROJECT_ROOT}/recipes/${recipe_dir}"

    if [ -d "$recipe_path" ]; then
        echo "  Exporting ${recipe_name}/${recipe_version}..."
        conan export "$recipe_path" --name="$recipe_name" --version="$recipe_version" > /dev/null
    else
        echo "  ${YELLOW}Warning: Recipe not found at $recipe_path${NC}"
    fi
done

echo -e "${GREEN}✓ Local recipes exported${NC}"
echo

################################################################################
# Step 3: Install dependencies with Conan
################################################################################
echo -e "${YELLOW}[3/5] Installing Conan dependencies (this may take a while)...${NC}"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Use repo profiles (Clang 22, AArch64, libc++)
PROFILE_BUILD="${PROJECT_ROOT}/conan/profiles/build"
PROFILE_HOST="${PROJECT_ROOT}/conan/profiles/host"

echo "  Using profiles:"
echo "    Build: $PROFILE_BUILD"
echo "    Host: $PROFILE_HOST"

if [ -f "$PROFILE_BUILD" ] && [ -f "$PROFILE_HOST" ]; then
  if [ -z "$VERBOSE" ]; then
    conan install "$PROJECT_ROOT" \
        -pr:b "$PROFILE_BUILD" \
        -pr:h "$PROFILE_HOST" \
        --build=missing \
        -of . > /dev/null 2>&1
  else
    conan install "$PROJECT_ROOT" \
        -pr:b "$PROFILE_BUILD" \
        -pr:h "$PROFILE_HOST" \
        --build=missing \
        -of .
  fi
else
  echo -e "${RED}ERROR: Profiles not found!${NC}"
  echo "Expected:"
  echo "  Build profile: $PROFILE_BUILD"
  echo "  Host profile: $PROFILE_HOST"
  exit 1
fi

echo -e "${GREEN}✓ Dependencies installed${NC}"
echo

################################################################################
# Step 4: Configure and build with CMake
################################################################################
echo -e "${YELLOW}[4/5] Configuring and building with CMake...${NC}"

cd "$BUILD_DIR"

echo "  Configuring CMake..."
cmake "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="conan_toolchain.cmake"

echo "  Building project (this may take a while)..."
cmake --build . --config "$BUILD_TYPE" --parallel "$(nproc)"

echo -e "${GREEN}✓ Build complete${NC}"
echo

################################################################################
# Step 5: Install artifacts and libraries
################################################################################
echo -e "${YELLOW}[5/5] Installing artifacts to /app/install...${NC}"

cd "$BUILD_DIR"

cmake --install . --config "$BUILD_TYPE"

INSTALL_DIR="/app/install"
echo -e "${GREEN}✓ Installation complete${NC}"
echo

################################################################################
# Summary
################################################################################
echo -e "${GREEN}=== Build and Install Successful ===${NC}"
echo "Build directory: $BUILD_DIR"
echo "Install directory: $INSTALL_DIR"
echo ""
echo "Installed artifacts:"
echo "  Executable: $INSTALL_DIR/bin/panda_demo"
echo "  Libraries: $INSTALL_DIR/lib/"
echo ""
echo "To run the application:"
echo "  $INSTALL_DIR/bin/panda_demo"
echo ""
echo "To use installed libraries in another project:"
echo "  export LD_LIBRARY_PATH=$INSTALL_DIR/lib:\$LD_LIBRARY_PATH"
echo
