#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;


void testSub(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<int8_t>(), 2);
  ImageParam inputTwo (type_of<int8_t>(), 2);
  Halide::Func Subt;
  Subt (x, y) = inputOne(x, y) - inputTwo(x, y);
  Var x_outer, x_inner;
  Subt.split(x, x_outer, x_inner, 64);
  Subt.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  Subt.compile_to_bitcode("vsubb.bc", args, target);
  Subt.compile_to_assembly("vsubb.s", args, target);
}

void testAdd(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<int8_t>(), 2);
  ImageParam inputTwo (type_of<int8_t>(), 2);
  Halide::Func Addb;
  Addb (x, y) = inputOne(x, y) + inputTwo(x, y);
  Var x_outer, x_inner;
  Addb.split(x, x_outer, x_inner, 64);
  Addb.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  Addb.compile_to_bitcode("vaddb.bc", args, target);
  Addb.compile_to_assembly("vaddb.s", args, target);
}
void testAvg(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<uint8_t>(), 2);
  ImageParam inputTwo (type_of<uint8_t>(), 2);
  Halide::Func Addb;
  Addb (x, y) = (inputOne(x, y) + inputTwo(x, y))/2;
  Var x_outer, x_inner;
  Addb.split(x, x_outer, x_inner, 64);
  Addb.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  Addb.compile_to_bitcode("vavgub.bc", args, target);
  Addb.compile_to_assembly("vavgub.s", args, target);
}
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
  testSub(target);
  testAdd(target);
  testAvg(target);
  printf ("Done\n");

  return 0;
}
