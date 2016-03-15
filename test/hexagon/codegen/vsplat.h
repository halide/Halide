#include "halide-hexagon-setup.h"
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

template<typename T>
void testBcast(Target& target) {
  Halide::Var x("x"), y("y");
  Param<T> bcastval;
  Halide::Func Bcast;
  Bcast (x, y) = bcastval;
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  Bcast.split(x, x_outer, x_inner, split_by);
  Bcast.vectorize(x_inner);
  std::vector<Argument> args(1);
  args[0] = bcastval;
  COMPILE(Bcast, "Bcast");
}
