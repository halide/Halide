#include "halide-hexagon-setup.h"
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

template<typename T>
void SatSub(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 1);
  ImageParam inputTwo (type_of<T>(), 1);
  Halide::Func SatSubt;
  SatSubt(x) = saturating_subtract(inputOne(x), inputTwo(x));
  SatSubt.vectorize(x, VECTORSIZE/sizeof(T));
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(SatSubt, "SatSubt");
}

template<typename T>
void SatAdd(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 1);
  ImageParam inputTwo (type_of<T>(), 1);
  Halide::Func SatAddt;
  SatAddt (x) = saturating_add(inputOne(x), inputTwo(x));
  SatAddt.vectorize(x, VECTORSIZE/sizeof(T));
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(SatAddt, "SatAddt");
}
