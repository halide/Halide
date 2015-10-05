#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define COMPILE_OBJ(X)  ((X).compile_to_file("histogram", args, target))

/*
 * Histogram of the input pixels.
 */


void test_histogram(Target& target) {
  Halide::Var x("x"),y("y");
  ImageParam input(type_of<uint8_t>(), 2);
#ifndef DEBUG_SYNTHETIC
  set_min(input, 0, 0);
  set_min(input, 1, 0);
  set_stride_multiple(input, 1, 1 << LOG2VLEN);
#endif

  Halide::Func histogram;
  histogram(x) = 0;
  RDom r(input);
  histogram(clamp(cast<int>(input(r.x, r.y)),0,255)) += 1;

  histogram.vectorize(x, 1 << LOG2VLEN);



  // Schedule.
#ifndef DEBUG_SYNTHETIC
  set_output_buffer_min(histogram, 0, 0);
  set_stride_multiple(histogram, 0, 1 << LOG2VLEN);
#endif
  std::vector<Argument> args(1);
  args[0]  = input;
#ifdef BITCODE
  histogram.compile_to_bitcode("histogram.bc", args, target);
#endif
#ifdef ASSEMBLY
  histogram.compile_to_assembly("histogram.s", args, target);
#endif
#ifdef STMT
  histogram.compile_to_lowered_stmt("histogram.html", args, HTML);
#endif
#ifdef RUN
  COMPILE_OBJ(histogram);
#endif
}

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
#if LOG2VLEN == 7
  target.set_feature(Target::HVX_DOUBLE);
#endif
  target.set_cgoption(Target::BuffersAligned);
  test_histogram(target);
  printf ("Done\n");
  return 0;
}

