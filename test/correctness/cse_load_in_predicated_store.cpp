#include "Halide.h"

using namespace Halide;
int main(int argc, char** argv) {
  ImageParam input1(type_of<float>(), 1);
  ImageParam input2(type_of<float>(), 2);

  Func output{"output"};
  Var x{"x"}, y{"y"};

  Expr a = input1(0);
  Expr b = input2(x, y);

  output(x, y)  = a - a * b;

  output.vectorize(x, 8, TailStrategy::GuardWithIf);

  output.compile_to_static_library("tst", {input1, input2});

  printf("Success!\n");

  return 0;

}
