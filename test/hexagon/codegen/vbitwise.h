#include "halide-hexagon-setup.h"
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

template<typename T>
void testAnd(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func AndFunction;
  AndFunction (x, y) = inputOne(x, y) & inputTwo(x, y);
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  AndFunction.split(x, x_outer, x_inner, split_by);
  AndFunction.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(AndFunction, "AndF");
}

template<typename T>
void testOr(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func OrFunction;
  OrFunction (x, y) = inputOne(x, y) | inputTwo(x, y);
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  OrFunction.split(x, x_outer, x_inner, split_by);
  OrFunction.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(OrFunction, "OrF");
}

template<typename T>
void testXor(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func XorFunction;
  XorFunction (x, y) = inputOne(x, y) ^ inputTwo(x, y);
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  XorFunction.split(x, x_outer, x_inner, split_by);
  XorFunction.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(XorFunction, "XorF");
}

template<typename T>
void testNot(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  Halide::Func NotFunction;
  NotFunction (x, y) = ~inputOne(x, y);
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  NotFunction.split(x, x_outer, x_inner, split_by);
  NotFunction.vectorize(x_inner);
  std::vector<Argument> args(1);
  args[0]  = inputOne;
  COMPILE(NotFunction, "NotF");
}
