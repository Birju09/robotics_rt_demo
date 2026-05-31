#include <array>

struct Rotation {
  double data[9];
  Rotation(const Rotation &arg) {
    int count = 9;
    while (count--)
      data[count] = arg.data[count];
  }
};

Rotation make(const Rotation &a) { return Rotation(a); }

std::array<double, 9> function(int b, std::array<double, 9> data) {
  std::array<double, 9> output;
  while (b--) {
    output[b] = data[b];
  }
  return output;
}