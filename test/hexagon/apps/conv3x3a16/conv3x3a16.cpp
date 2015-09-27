#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
#define COMPILE_OBJ(X)  ((X).compile_to_file("conv3x3a16", args, target))

using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
#if LOG2VLEN == 7
  target.set_feature(Target::HVX_DOUBLE);
#endif

  Halide::Var x("x"), y("y");
  Var xo,xi;

  ImageParam In (type_of<uint8_t>(), 2);

  Halide::Func mask;
  mask(x,y) = 0;
  mask(-1,-1) =  1; mask( 0,-1) = -4; mask( 1,-1) =  7;
  mask(-1, 0) =  2; mask( 0, 0) = -5; mask( 1, 0) =  8;
  mask(-1, 1) =  3; mask( 0, 1) = -6; mask( 1, 1) =  9;
  Halide::RDom r(-1,3,-1,3);

  Halide::Func conv3x3;
  conv3x3(x, y) = cast<uint8_t>
    (clamp(sum(cast<int16_t>(In(x+r.x, y+r.y)) * cast<int16_t>( mask(r.x, r.y))) >> 4, 0, 255));

#ifndef NOVECTOR
  conv3x3.vectorize(x, 1 << LOG2VLEN);
#endif
  std::vector<Argument> args(1);
  args[0]  = In;
#ifdef BITCODE
  conv3x3.compile_to_bitcode("conv3x3a16.bc", args, target);
#endif
#ifdef ASSEMBLY
  conv3x3.compile_to_assembly("conv3x3a16.s", args, target);
#endif
#ifdef STMT
  conv3x3.compile_to_lowered_stmt("conv3x3a16.html", args, HTML);
#endif
#ifdef RUN
 COMPILE_OBJ(conv3x3);
#endif
  printf ("Done\n");
  return 0;
}
