#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

// RUN: ./vdowncast16_to_8.out | FileCheck %s
//CHECK: __test_uh_u8_sat
//CHECK: vsat(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
void test_uh_u8_sat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(1);
  Halide::Func res;
  ImageParam f (type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(usat_8(cast<uint16_t>(f(x))));
  res.vectorize(x, 64);
  args[0]  = f;
  COMPILE(res, "test_uh_u8_sat");
}

//CHECK: __test_h_u8_sat
//CHECK: vsat(v{{[0-9]+}}.h,v{{[0-9]+}}.h)
void test_h_u8_sat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(1);
  Halide::Func res, F, G;
  ImageParam f (type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(usat_8(cast<int16_t>(f(x))));
  res.vectorize(x, 64);
  args[0]  = f;
  COMPILE(res, "test_h_u8_sat");
}
//CHECK: __test_uh_i8_nosat
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void test_uh_i8_nosat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f (type_of<uint8_t>(), 1);
  ImageParam g (type_of<uint8_t>(), 1);
  res(x) = cast<int8_t>(cast<uint16_t>(f(x)) + cast<uint16_t>(g(x)));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_uh_i8_nosat");
}

//CHECK: __test_h_i8_nosat
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void test_h_i8_nosat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f (type_of<uint8_t>(), 1);
  ImageParam g (type_of<uint8_t>(), 1);
  res(x) = cast<int8_t>(cast<int16_t>(f(x)) + cast<int16_t>(g(x)));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_h_i8_nosat");
}

//CHECK: __test_uh_u8_nosat
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void test_uh_u8_nosat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f (type_of<uint8_t>(), 1);
  ImageParam g (type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(cast<uint16_t>(f(x)) + cast<uint16_t>(g(x)));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_uh_u8_nosat");
}

//CHECK: __test_h_u8_nosat
//CHECK: vshuffe(v{{[0-9]+}}.b,v{{[0-9]+}}.b)
void test_h_u8_nosat(Target& target) {
  Halide::Var x("x"), y("y");
  Var x_outer, x_inner;

  std::vector<Argument> args(2);
  Halide::Func res, F, G;
  ImageParam f (type_of<uint8_t>(), 1);
  ImageParam g (type_of<uint8_t>(), 1);
  res(x) = cast<uint8_t>(cast<int16_t>(f(x)) + cast<int16_t>(g(x)));
  res.vectorize(x, 64);
  args[0]  = f;
  args[1] = g;
  COMPILE(res, "test_h_u8_nosat");
}

void testall(Target &target) {
  test_uh_u8_sat(target);
  test_h_u8_sat(target);
  test_uh_i8_nosat(target);
  test_h_i8_nosat(target);
  test_uh_u8_nosat(target);
  test_h_u8_nosat(target);
}

// At the present moment, downcasting u16 or i16 to i8 will lead to this
// error
// "Saturate and packing not supported when downcasting shorts (signed and
// unsigned) to signed chars"
int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);
  testall(target);
  printf ("Done\n");
  return 0;
}
