set(TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/test.cpp)
set(TEST_IR  ${CMAKE_CURRENT_BINARY_DIR}/test.ll)

execute_process(
  COMMAND llvm-config --bindir
  OUTPUT_VARIABLE LLVM_BINDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_program(OPT opt-22 HINTS ${LLVM_BINDIR} REQUIRED)

add_custom_command(
  OUTPUT  ${TEST_IR}
  COMMAND ${CMAKE_CXX_COMPILER}
          -std=c++17
          -fsanitize=undefined   # instrument with UBSan so handlers appear in IR
          -S -emit-llvm -g0      # emit human-readable IR instead of object code
          -O3
          -o ${TEST_IR}
          ${TEST_SRC}
  DEPENDS ${TEST_SRC}
  COMMENT "Emitting LLVM IR for test.cpp → test.ll"
  VERBATIM
)

add_custom_target(test-ir ALL
  COMMAND ${OPT}
          -load-pass-plugin $<TARGET_FILE:UBSanCountPlugin>
          "-passes=function(ubsan-signed-overflow-optimize),default<O3>"
          ${TEST_IR}
          -S
          -o ${TEST_IR}
  DEPENDS ${TEST_IR} UBSanCountPlugin
  COMMENT "Counting UBSan sites in test.ll"
  VERBATIM
)
