#define COMPILE(X)  ((X).compile_to_assembly("/dev/stdout", args, target))
#define VECTORSIZE 64 //Vector width in bytes. (Single mode)

template<typename T>
void testZeroExtend(Target &target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  Halide::Func ZeroExtend;
  ZeroExtend (x, y) = cast(UInt(sizeof(T) * 2 * 8), inputOne(x, y));
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  ZeroExtend.split(x, x_outer, x_inner, split_by);
  ZeroExtend.vectorize(x_inner);
  std::vector<Argument> args(1);
  args[0]  = inputOne;
  COMPILE(ZeroExtend);
}

template<typename T>
void testSignExtend(Target &target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  Halide::Func SignExtend;
  SignExtend (x, y) = cast(Int(sizeof(T) * 2 * 8), inputOne(x, y));
  Var x_outer, x_inner;
  int split_by = VECTORSIZE / sizeof(T);
  SignExtend.split(x, x_outer, x_inner, split_by);
  SignExtend.vectorize(x_inner);
  std::vector<Argument> args(1);
  args[0]  = inputOne;
  COMPILE(SignExtend);
}
