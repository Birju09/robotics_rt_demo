add_llvm_pass_plugin(UBSanCountPlugin
  UBSanCountPass.cpp
  DEPENDS intrinsics_gen
)

target_include_directories(UBSanCountPlugin PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(UBSanCountPlugin PRIVATE
  ${LLVM_DEFINITIONS}
)

set_target_properties(UBSanCountPlugin PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

target_compile_options(UBSanCountPlugin PRIVATE -stdlib=libstdc++)
target_link_options(UBSanCountPlugin PRIVATE -stdlib=libstdc++)

add_subdirectory(test)