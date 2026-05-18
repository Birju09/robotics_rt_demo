# Multi-stage build for robotics_rt_demo with ros:jazzy base
# Supports both Release and Debug builds

ARG BUILD_TYPE=Release

FROM ros:jazzy AS builder

ARG BUILD_TYPE

# Install build essentials and LLVM 22 toolchain
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3 \
    python3-pip \
    python3-venv \
    lsb-release \
    wget \
    gnupg \
    software-properties-common \
    pkg-config \
    ros-jazzy-xacro \
    && wget https://apt.llvm.org/llvm.sh -O /tmp/llvm.sh \
    && chmod +x /tmp/llvm.sh \
    && /tmp/llvm.sh 22 all \
    && rm -f /tmp/llvm.sh \
    && rm -rf /var/lib/apt/lists/*

# Create symlinks for convenience (clang -> clang-22, etc.)
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-22 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-22 100 && \
    update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-22 100

# Create Python virtual environment for Conan and build tools
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

# Install Conan and build tools in the venv
RUN /opt/venv/bin/pip install --upgrade pip && \
    /opt/venv/bin/pip install conan catkin_pkg empy==3.3.4

# Set CC and CXX to use clang 22
ENV CC=clang-22 \
    CXX=clang++-22

# Set up working directory
WORKDIR /app

# Note: Source code is mounted via volume mount at runtime
# Build script is run via docker run or docker-compose

