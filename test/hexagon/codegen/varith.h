#include "halide-hexagon-setup.h"
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

template<typename T>
void testSub(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func Subt;
  Subt (x, y) = inputOne(x, y) - inputTwo(x, y);
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  Subt.split(x, x_outer, x_inner, split_by);
  Subt.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Subt, "Subt");
}

template<typename T>
void testAdd(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func Addb;
  Addb (x, y) = inputOne(x, y) + inputTwo(x, y);
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  Addb.split(x, x_outer, x_inner, split_by);
  Addb.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Addb, "Addb");
}


template<typename T>
void testSubDouble(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func Subt;
  Subt (x, y) = inputOne(x, y) - inputTwo(x, y);
  Var x_outer, x_inner;
  int split_by = DOUBLEVECTORSIZE / sizeof(T);
  Subt.split(x, x_outer, x_inner, split_by);
  Subt.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Subt, "Subt");
}

template<typename T>
void testAddDouble(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func Addb;
  Addb (x, y) = inputOne(x, y) + inputTwo(x, y);
  Var x_outer, x_inner;
  int split_by = DOUBLEVECTORSIZE / sizeof(T);
  Addb.split(x, x_outer, x_inner, split_by);
  Addb.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Addb, "Addb");
}
