#define COMPILE(X)  ((X).compile_to_assembly("/dev/stdout", args, target))
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128

template<typename T>
void testMax(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func MaxFunction;
  MaxFunction (x, y) = max(inputOne(x, y), inputTwo(x, y));
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  MaxFunction.split(x, x_outer, x_inner, split_by);
  MaxFunction.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(MaxFunction);
}

template<typename T>
void testMin(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func MinFunction;
  MinFunction (x, y) = min(inputOne(x, y), inputTwo(x, y));
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  MinFunction.split(x, x_outer, x_inner, split_by);
  MinFunction.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(MinFunction);
}
