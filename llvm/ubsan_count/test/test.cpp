//! This is a playground test file for analzying the IR.

#include <array>

struct Rotation {
  double data[9];
  // UBSAN instrumentation is added for signed arithmetic in this function.
  // Specially for the `count` variable, the instrumentation seems redundant.
  // But the ScalarEvolution cannot prove this since instrumentation branch
  // also dominates the latch.
  // Without instrumentation, this function is optimized using SIMD instructions
  // or built-in memcpy.
  // With instrumentation, the loop persists with checks in every loop.
  Rotation(const Rotation &arg) {
    int count = 9;
    while (count--) {
      data[count] = arg.data[count];
    }
  }
};

Rotation testRotation(const Rotation &a) { return Rotation(a); }

// In this function, the UBSAN optimization pass cannot prove that
// signed overflow never occurs. So the instrumented code stays even after
// optimization passes.
std::array<double, 9> testCopy(int b, std::array<double, 9> data) {
  if (b > 9) {
    return {};
  }
  std::array<double, 9> output;
  while (b--) {
    output[b] = data[b];
  }
  return output;
}