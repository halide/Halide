#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define COMPILE_OBJ(X)  ((X).compile_to_file("gaussian3x3", args, target))





void test_gaussian3x3(Target& target) {
  Halide::Var x("x"),y("y");
  ImageParam input(type_of<uint8_t>(), 2);


  Func input_16("input_16");
  input_16(x, y) = cast<int16_t>(input(x, y));


  Halide::Func rows("rows");
  rows(x, y) = (input_16(x-1,y)) + (input_16(x,y) << 1) + (input_16(x+1,y));

  Halide::Func cols("cols");
  cols(x,y) = (input_16(x,y-1)) + (input_16(x,y) << 1) + (input_16(x,y+1));

  Halide::Func left("left");
  left(x,y) = (input_16(x,y)) + (input_16(x,y) << 1) + (input_16(x+1,y));

  Halide::Func right("right");
  right(x,y) = (input_16(x-1,y)) + (input_16(x,y) << 1) + (input_16(x,y));


  Halide::Func topLeft("topLeft");
  topLeft(x,y) = (input_16(x,y) * 9) + (input_16(x,y+1) * 3) + (input_16(x+1,y) * 3) +  (input_16(x+1, y+1));

  Halide::Func topRight("topRight");
  topRight(x,y) = (input_16(x-1,y) * 3) + (input_16(x,y) * 9) + (input_16(x-1,y+1)) + (input_16(x,y+1) * 3);

  Halide::Func bottomLeft("bottomLeft");
  bottomLeft(x,y) = (input_16(x,y) * 9) + (input_16(x,y-1) * 3) + (input_16(x+1,y) * 3) +  (input_16(x+1, y-1));

  Halide::Func bottomRight("bottomRight");
  bottomRight(x,y) = (input_16(x-1,y) * 3) + (input_16(x,y) * 9) + (input_16(x-1,y-1)) + (input_16(x,y-1) * 3);




  Halide::Func gaussian3x3("gaussian3x3");
  gaussian3x3(x,y) = cast<uint8_t> (clamp((rows(x, y-1) + (rows(x,y) << 1) + rows(x, y+1))>> 4, 0, 255));
  gaussian3x3(x, 0) = cast<uint8_t> (clamp((rows(x, 0) + (rows(x,0) << 1) + rows(x, 1))>> 4, 0, 255));
  gaussian3x3(x, input.height() - 1) = cast <uint8_t> (clamp((rows(x, input.height()-2) + (rows(x,input.height()-1) << 1) + rows(x, input.height()-1)) >> 4, 0, 255));
  gaussian3x3(0, y) = cast<uint8_t> (clamp(((left(0, y-1) + (left(0, y) << 1) + left(0, y+1)) >> 4), 0, 255));
  gaussian3x3(input.width()-1, y) = cast<uint8_t> (clamp((right(input.width()-1, y-1) + (right(input.width()-1, y) << 1) + right(input.width()-1, y+1))>>4, 0, 255));
  gaussian3x3(0, 0) = cast<uint8_t> (clamp(topLeft(0, 0) >> 4, 0, 255));
  gaussian3x3(input.width()-1, 0) = cast<uint8_t> (clamp(topRight(input.width()-1, 0) >> 4, 0, 255));
  gaussian3x3(0, input.height()-1) = cast<uint8_t> (clamp(bottomLeft(0, input.height()-1) >> 4, 0, 255));
  gaussian3x3(input.width()-1, input.height()-1) = cast<uint8_t> (clamp(bottomRight(input.width()-1, input.height()-1) >> 4, 0, 255));


  gaussian3x3.vectorize(x,1 << LOG2VLEN );
  gaussian3x3.update(0).vectorize(x, 1 << LOG2VLEN);
  gaussian3x3.update(1).vectorize(x, 1 << LOG2VLEN);

  std::vector<Argument> args(1);
  args[0]  = input;
#ifdef BITCODE
  gaussian3x3.compile_to_bitcode("gaussian3x3.bc", args, target);
#endif
#ifdef ASSEMBLY
  gaussian3x3.compile_to_assembly("gaussian3x3.s", args, target);
#endif
#ifdef STMT
  gaussian3x3.compile_to_lowered_stmt("gaussian3x3.html", args, HTML);
#endif
#ifdef RUN
 COMPILE_OBJ(gaussian3x3);
#endif
}

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target);
#if LOG2VLEN == 7
  target.set_feature(Target::HVX_DOUBLE);
#endif
  test_gaussian3x3(target);
  printf ("Done\n");
  return 0;
}
