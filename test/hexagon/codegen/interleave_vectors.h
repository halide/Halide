#include "halide-hexagon-setup.h"
#define VECTOR_SIZE_IN_BYTES 64
template <typename T>
void check_interleave(Target target, const char *name) {
  Halide::Var x("x"), y("y");
  ImageParam i1 (type_of<T>(), 1);
  ImageParam i2 (type_of<T>(), 1);
  Halide::Func f;
  f(x) = select((x%2) == 0, i1(x/2), i2(x/2));
  std::vector<Argument> args(2);
  args[0]  = i1;
  args[1] = i2;
  f.vectorize(x, (VECTOR_SIZE_IN_BYTES / sizeof(T)) * 2);
  COMPILE(f, name);
}
