#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define COMPILE_OBJ(X)  ((X).compile_to_file("gaussian5x5", args, target))

/*
 * Given a 5x5 patch, find the middle element
 * We do this by first finding the minimum, maximum, and middle for each column
 * Then across columns, we find the maximum minimum, the minimum maximum, and the middle middle.
 * Then we take the middle of those three results.
 */


void test_gaussian5x5(Target& target) {
  Halide::Var x("x"),y("y");
  ImageParam input(type_of<uint8_t>(), 2);
#ifndef DEBUG_SYNTHETIC
  set_min(input, 0, 0);
  set_min(input, 1, 0);
  set_stride_multiple(input, 1, 1 << LOG2VLEN);
#endif

  Func input_16("input_16");
  input_16(x, y) = cast<int16_t>(input(x, y));
  // Algorithm.
  Halide::Func rows("rows");
  rows(x, y) = input_16(x-2, y) + 4*input_16(x-1, y) + 6*input_16(x,y)+ 4*input_16(x+1,y) + input_16(x+2,y);
  Halide::Func cols("cols");
  cols(x,y) =  rows(x, y-2) + 4*rows(x, y-1) + 6*rows(x, y) + 4*rows(x, y+1) + rows(x, y+2);

  Func gaussian5x5("gaussian5x5");
  gaussian5x5(x, y) = cast<uint8_t> (cols(x, y) >> 8);

#ifndef NOVECTOR
  // Schedule.
  gaussian5x5.vectorize(x, 1 << LOG2VLEN);
#endif

#ifndef DEBUG_SYNTHETIC
  set_output_buffer_min(gaussian5x5, 0, 0);
  set_output_buffer_min(gaussian5x5, 1, 0);
  set_stride_multiple(gaussian5x5, 1, 1 << LOG2VLEN);
#endif
  std::vector<Argument> args(1);
  args[0]  = input;
#ifdef BITCODE
  gaussian5x5.compile_to_bitcode("gaussian5x5.bc", args, target);
#endif
#ifdef ASSEMBLY
  gaussian5x5.compile_to_assembly("gaussian5x5.s", args, target);
#endif
#ifdef STMT
  gaussian5x5.compile_to_lowered_stmt("gaussian5x5.html", args, HTML);
#endif
#ifdef RUN
  COMPILE_OBJ(gaussian5x5);
#endif
}

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
#if LOG2VLEN == 7
  target.set_feature(Target::HVX_128);
#endif
  target.set_cgoption(Target::BuffersAligned);
  test_gaussian5x5(target);
  printf ("Done\n");
  return 0;
}

