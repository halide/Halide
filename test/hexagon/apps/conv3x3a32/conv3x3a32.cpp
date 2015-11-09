#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
#define COMPILE_OBJ(X)  ((X).compile_to_file("conv3x3a32", args, target))

using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
#if LOG2VLEN == 7
  target.set_feature(Target::HVX_DOUBLE);
#endif
  target.set_cgoption(Target::BuffersAligned);

  Halide::Var x("x"), y("y");
  Var xo,xi;

  ImageParam In (type_of<uint8_t>(), 2);
  ImageParam Mask (type_of<int8_t>(), 2);
  set_min(In, 0, 0);
  set_min(In, 1, 0);
  set_stride_multiple(In, 1, 1 << LOG2VLEN);
  set_min(Mask, 0, 0);
  set_min(Mask, 1, 0);

  Halide::RDom r(-1,3,-1,3);

  Halide::Func conv3x3;
  conv3x3(x, y) = cast<uint8_t>
    (clamp(sum(cast<int32_t>(cast<int16_t>(In(x+r.x, y+r.y)) * cast<int16_t>(Mask(1+r.x, 1+r.y)))) >> 4, 0, 255));

#if VECTOR
  conv3x3.vectorize(x, 1 << LOG2VLEN);
#endif
  set_output_buffer_min(conv3x3, 0, 0);
  set_output_buffer_min(conv3x3, 1, 0);
  set_stride_multiple(conv3x3, 1, 1 << LOG2VLEN);
  std::vector<Argument> args(2);
  args[0]  = In;
  args[1]  = Mask;
#ifdef BITCODE
  conv3x3.compile_to_bitcode("conv3x3a32.bc", args, target);
#endif
#ifdef ASSEMBLY
  conv3x3.compile_to_assembly("conv3x3a32.s", args, target);
#endif
#ifdef STMT
  conv3x3.compile_to_lowered_stmt("conv3x3a32.html", args, HTML);
#endif
#ifdef RUN
 COMPILE_OBJ(conv3x3);
#endif
  printf ("Done\n");
  return 0;
}
