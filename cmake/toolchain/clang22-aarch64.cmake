# Toolchain file for Clang 22 on AArch64 Linux
# Configures compiler, linker, and C++ standard library

cmake_minimum_required(VERSION 3.25)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Compiler
set(CMAKE_C_COMPILER clang-22)
set(CMAKE_CXX_COMPILER clang++-22)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

# Linker and archiver tools
set(CMAKE_AR llvm-ar-22)
set(CMAKE_RANLIB llvm-ranlib-22)
set(CMAKE_LINKER_TYPE LLD)

add_compile_options(-stdlib=libc++)
add_link_options(-stdlib=libc++ --ld-path=ld.lld-22)
