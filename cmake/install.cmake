# Install runtime dependencies discovered by CMake
# Use install(CODE) to run dependency analysis during install phase, not configuration phase

install(CODE [[
  file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${CMAKE_INSTALL_PREFIX}/bin/panda_demo"
    RESOLVED_DEPENDENCIES_VAR RESOLVED_DEPS
    UNRESOLVED_DEPENDENCIES_VAR UNRESOLVED_DEPS
    DIRECTORIES "/root/.conan2/p" "/usr/lib/llvm-22/lib"
    CONFLICTING_DEPENDENCIES_PREFIX CONFLICTING_DEPS
    PRE_EXCLUDE_REGEXES
      "ld-linux"        # dynamic linker — must not be bundled
      "libc\\.so"       # glibc — ABI-tied to the kernel
      "libpthread\\.so" # glibc threading
      "libm\\.so"       # glibc math
      "libdl\\.so"      # glibc dynamic loading
      "librt\\.so"      # glibc realtime
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

  foreach(lib_name ${CONFLICTING_DEPS_FILENAMES})
    set(paths ${CONFLICTING_DEPS_${lib_name}})
    message(WARNING "Conflicting paths for ${lib_name}: ${paths}")
    # Prefer Conan-managed libraries; fall back to the first candidate
    set(chosen "")
    foreach(path ${paths})
      if(path MATCHES "/\\.conan2/" AND chosen STREQUAL "")
        set(chosen "${path}")
      endif()
    endforeach()
    if(chosen STREQUAL "")
      list(GET paths 0 chosen)
    endif()
    message(STATUS "Installing conflicting dep (chosen): ${chosen}")
    file(INSTALL "${chosen}" DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" FOLLOW_SYMLINK_CHAIN)
  endforeach()
]])
