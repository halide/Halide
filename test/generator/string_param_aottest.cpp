#include <iostream>
#include "string_param.h"
#include "HalideBuffer.h"
#include "HalideRuntime.h"

int main(int argc, char** argv) {
  Halide::Runtime::Buffer<float> x(3);
  x(0) = 1.0f;
  x(1) = 2.0f;
  x(2) = 3.0f;
  Halide::Runtime::Buffer<float> y(3);
  string_param(x, y);
  std::cout << y(0) << " " << y(1) << " " << y(2);
  return 0;
}
