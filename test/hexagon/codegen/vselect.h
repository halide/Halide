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
template<typename T1, typename T2>
void testSelectNarrowing(Target &target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T1>(), 1);
  ImageParam inputTwo (type_of<T1>(), 1);
  ImageParam inputThree (type_of<T1>(), 1);
  Halide::Func SelectNarrow, ResultNarrow;
  SelectNarrow(x) = (select(inputOne(x) != inputTwo(x), cast<T2>(inputOne(x)), cast<T2>(inputTwo(x))));
  ResultNarrow(x) = cast<T1>(cast<T2>(inputThree(x)) + SelectNarrow(x));
  ResultNarrow.vectorize(x, (VECTORSIZE/sizeof(T1)));
  std::vector<Argument> args(3);
  args[0]  = inputOne;
  args[1] = inputTwo;
  args[2] = inputThree;
  COMPILE(ResultNarrow, "ResultNarrow");
}
