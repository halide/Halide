#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

// RUN: ./vdowncast32_to_8.out | FileCheck %s
//CHECK: __test_w_u8_sat
//CHECK: vsat(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
//CHECK: vsat(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
//CHECK: vsat(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
void test_w_u8_sat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f(type_of<uint8_t>(), 1);
  ImageParam g(type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(usat_8(cast<int32_t>(f(x)) + cast<int32_t>(g(x))));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_w_u8_sat");
}

//CHECK: __test_uw_u8_sat
//CHECK: vsat(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
//CHECK: vsat(v{{[0-9]+}}.w,v{{[0-9]+}}.w)
//CHECK: vsat(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
void test_uw_u8_sat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f(type_of<uint8_t>(), 1);
  ImageParam g(type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(usat_8(cast<uint32_t>(f(x)) + cast<uint32_t>(g(x))));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_uw_u8_sat");
}
//CHECK: __test_w_u8_nosat
//CHECK: vshuffe(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
//CHECK: vshuffe(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void test_w_u8_nosat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f(type_of<uint8_t>(), 1);
  ImageParam g(type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(cast<int32_t>(f(x)) + cast<int32_t>(g(x)));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_w_u8_nosat");
}

//CHECK: __test_uw_u8_nosat
//CHECK: vshuffe(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
//CHECK: vshuffe(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void test_uw_u8_nosat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f(type_of<uint8_t>(), 1);
  ImageParam g(type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(cast<uint32_t>(f(x)) + cast<uint32_t>(g(x)));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_uw_u8_nosat");
}

//CHECK: __test_w_i8_nosat
//CHECK: vshuffe(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
//CHECK: vshuffe(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void test_w_i8_nosat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f(type_of<uint8_t>(), 1);
  ImageParam g(type_of<uint8_t>(), 1);
  res(x) = cast<int8_t>(cast<int32_t>(f(x)) + cast<int32_t>(g(x)));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_w_i8_nosat");
}

//CHECK: __test_uw_i8_nosat
//CHECK: vshuffe(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
//CHECK: vshuffe(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void test_uw_i8_nosat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f(type_of<uint8_t>(), 1);
  ImageParam g(type_of<uint8_t>(), 1);
  res(x) = cast<int8_t>(cast<uint32_t>(f(x)) + cast<uint32_t>(g(x)));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_uw_i8_nosat");
}
void testall(Target &target) {
  test_w_u8_sat(target);
  test_uw_u8_sat(target);
  test_w_u8_nosat(target);
  test_uw_u8_nosat(target);
  test_w_i8_nosat(target);
  test_uw_i8_nosat(target);
}
// At the present moment, downcasting u32 or i32 to i8 will lead to this
// error
// "Saturate and packing not supported when downcasting words to signed chars"
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);
  testall(target);
  printf ("Done\n");
  return 0;
}
