# Install runtime dependencies discovered by CMake
# Use install(CODE) to run dependency analysis during install phase, not configuration phase

install(CODE [[
  file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${CMAKE_INSTALL_PREFIX}/bin/panda_demo"
    RESOLVED_DEPENDENCIES_VAR RESOLVED_DEPS
    UNRESOLVED_DEPENDENCIES_VAR UNRESOLVED_DEPS
    DIRECTORIES "/root/.conan2/p" "/usr/lib/llvm-22/lib"
    PRE_EXCLUDE_REGEXES "libconsole_bridge" "libtinyxml2"
  )

  # Install all resolved runtime dependencies
  foreach(dep ${RESOLVED_DEPS})
    get_filename_component(lib_name "${dep}" NAME)
    message(STATUS "Installing runtime dependency: ${lib_name}")
    file(INSTALL "${dep}" DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" FOLLOW_SYMLINK_CHAIN)
  endforeach()

  # Explicitly find and install libc++ and libc++abi if not found above
  foreach(lib_name libc++ libc++abi)
    find_library(LIB_PATH NAMES "${lib_name}")
    if(LIB_PATH)
      message(STATUS "Installing clang library: ${LIB_PATH}")
      file(INSTALL "${LIB_PATH}" DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
      unset(LIB_PATH CACHE)
    endif()
  endforeach()

  # Log any unresolved dependencies
  if(UNRESOLVED_DEPS)
    message(WARNING "Unresolved runtime dependencies: ${UNRESOLVED_DEPS}")
  endif()
]])
