#include "halide-hexagon-setup.h"
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

template<typename T>
void testBzero(Target& target) {
  Halide::Var x("x"), y("y");
  Halide::Func Bzero;
  Bzero (x, y) = 0;
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  Bzero.split(x, x_outer, x_inner, split_by);
  Bzero.vectorize(x_inner);
  std::vector<Argument> args(0);
  Bzero.compile_to_bitcode("vzero.bc", args, target);
}

template<typename T>
void testDBzero(Target& target) {
  Halide::Var x("x"), y("y");
  Halide::Func DBzero;
  DBzero (x, y) = 0;
  Var x_outer, x_inner;
  int split_by = DOUBLEVECTORSIZE / sizeof(T);
  DBzero.split(x, x_outer, x_inner, split_by);
  DBzero.vectorize(x_inner);
  std::vector<Argument> args(0);
  DBzero.compile_to_bitcode("vzero.bc", args, target);
}
