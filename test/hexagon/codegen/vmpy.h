#include "halide-hexagon-setup.h"
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

template<typename T1, typename T2,  typename T3>
void testVMPY(Target& target) {
  Halide::Var x("x");
  ImageParam i1 (type_of<T1>(), 1);
  ImageParam i2 (type_of<T2>(), 1);
  Halide::Func F;
  F(x) = cast<T3>(i1(x) * i2(x));
  int vector_factor = VECTORSIZE / sizeof(T1);
  F.vectorize(x, vector_factor);
  std::vector<Argument> args(2);
  args[0]  = i1;
  args[1] = i2;
  COMPILE(F, "testVMPY");
}
