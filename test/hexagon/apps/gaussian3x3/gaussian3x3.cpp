#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128
#define COMPILE_OBJ(X)  ((X).compile_to_file("gaussian3x3", args, target))

/*
 * Given a 3x3 patch, find the middle element
 * We do this by first finding the minimum, maximum, and middle for each column
 * Then across columns, we find the maximum minimum, the minimum maximum, and the middle middle.
 * Then we take the middle of those three results.
 */


void test_gaussian3x3(Target& target) {
  Halide::Var x("x"),y("y");
  ImageParam input(type_of<uint8_t>(), 2);

  Func input_16("input_16");
  Func clamped_input = BoundaryConditions::constant_exterior(input, 0);
  clamped_input.compute_root();
  input_16(x, y) = cast<int16_t>(clamped_input(x, y));

  /* EJP: note that there's some overlap between max and min and mid.
   * Does the compiler pick this up automatically, or do we need to tweak the code?
   */


  Halide::Func rows("rows");
// Halide::Func gaussian3x3("gaussian3x3");
  rows(x, y) = (input_16(x,y)) + (input_16(x+1,y) << 1) + (input_16(x+2,y));
  Halide::Func cols("cols");
  cols(x,y) =  (rows(x, y) + (rows(x, y+1) << 1) + rows(x,y+2));

  Func gaussian3x3("gaussian3x3");
  gaussian3x3(x, y) = cast<uint8_t> (clamp(cols(x, y) >> 4, 0, 255));
 
  // Current problem: cols(x,y) only returns a value the size of a byte
  

  gaussian3x3.vectorize(x, 64);

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
	test_gaussian3x3(target);
	printf ("Done\n");
	return 0;
}

