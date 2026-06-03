find_path(OpenCL_INCLUDE_DIR
  NAMES CL/cl.h
  PATHS /usr/include /usr/local/include
)

find_library(OpenCL_LIBRARY
  NAMES OpenCL
  PATHS /usr/lib /usr/local/lib /usr/lib/aarch64-linux-gnu
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenCL DEFAULT_MSG OpenCL_INCLUDE_DIR OpenCL_LIBRARY)

if(OpenCL_FOUND)
  set(OpenCL_INCLUDE_DIRS ${OpenCL_INCLUDE_DIR})
  set(OpenCL_LIBRARIES ${OpenCL_LIBRARY})

  if(NOT TARGET OpenCL::OpenCL)
    add_library(OpenCL::OpenCL INTERFACE IMPORTED)
    set_target_properties(OpenCL::OpenCL PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OpenCL_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${OpenCL_LIBRARIES}"
    )
  endif()
endif()

mark_as_advanced(OpenCL_INCLUDE_DIR OpenCL_LIBRARY)
