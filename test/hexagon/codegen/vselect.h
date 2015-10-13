#include "halide-hexagon-setup.h"
/* Single mode tests only. */
#define VECTORSIZE 64 //Vector width in bytes. (Single mode).

template<typename T>
void testSelectLessThan(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 1);
  ImageParam inputTwo (type_of<T>(), 1);
  Halide::Func SelectLess;
  /* This is just the min operation really. */
  SelectLess(x) = select(inputOne(x) < inputTwo(x), inputOne(x), inputTwo(x));
  SelectLess.vectorize(x, (VECTORSIZE/sizeof(T)));
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(SelectLess, "SelectLess");
}

template<typename T>
void testSelectNotEqual(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 1);
  ImageParam inputTwo (type_of<T>(), 1);
  Halide::Func SelectNE;
  SelectNE (x) = select(inputOne(x) != inputTwo(x), inputOne(x), inputTwo(x));
  SelectNE.vectorize(x, (VECTORSIZE/sizeof(T)));
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(SelectNE, "SelectNE");

}
