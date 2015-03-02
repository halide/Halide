#define COMPILE(X)  ((X).compile_to_assembly("/dev/stdout", args, target))
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
  COMPILE(Subt);
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
  //  Addb.compile_to_bitcode("vaddb.bc", args, target);
  COMPILE(Addb);
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
  COMPILE(Subt);
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
  //  Addb.compile_to_bitcode("vaddb.bc", args, target);
  COMPILE(Addb);
}


template<typename T>
void testAvg(Target& target) {
  Halide::Var x("x"), y("y");
  ImageParam inputOne (type_of<T>(), 2);
  ImageParam inputTwo (type_of<T>(), 2);
  Halide::Func Avg;
  int split_by = VECTORSIZE / sizeof(T);
  Avg (x, y) = (inputOne(x, y) + inputTwo(x, y))/2;
  Var x_outer, x_inner;
  Avg.split(x, x_outer, x_inner, split_by);
  Avg.vectorize(x_inner);
  std::vector<Argument> args(2);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Avg);


  Halide::Func Navg;
  Navg (x, y) = (inputOne(x, y) - inputTwo(x, y))/2;
  Navg.split(x, x_outer, x_inner, split_by);
  Navg.vectorize(x_inner);
  args[0]  = inputOne;
  args[1] = inputTwo;
  COMPILE(Navg);
}
