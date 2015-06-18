#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;

#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128
#define COMPILE_OBJ(X)  ((X).compile_to_file("median", args, target))

/*
 * Given a 3x3 patch, find the middle element
 * We do this by first finding the minimum, maximum, and middle for each column
 * Then across columns, we find the maximum minimum, the minimum maximum, and the middle middle.
 * Then we take the middle of those three results.
 */


void test_median(bool isDbl, Target& target) {
  Halide::Var x("x"),y("y");
  ImageParam input(type_of<uint8_t>(), 2);

  /* EJP: note that there's some overlap between max and min and mid.
   * Does the compiler pick this up automatically, or do we need to tweak the code?
   */

  Func clamped_input = BoundaryConditions::constant_exterior(input, 0);
  clamped_input.compute_root();
  Halide::Func max_x("max_x");
  Halide::Func median("median");
  max_x(x,y) = max(max(clamped_input(x-1,y),clamped_input(x,y)),
                   clamped_input(x+1,y));
  Halide::Func min_x("min_x");
  min_x(x,y) = min(min(clamped_input(x-1,y),clamped_input(x,y)),
                   clamped_input(x+1,y));
  Halide::Func mid_x("mid_x");
  mid_x(x,y) = max(min(max(clamped_input(x-1,y),clamped_input(x,y)),
                       clamped_input(x+1,y)), min(clamped_input(x-1,y),
                                                  clamped_input(x,y)));

  Halide::Func minmax_y("minmax_y");
  minmax_y(x,y) = min(min(max_x(x,y-1),max_x(x,y)),max_x(x,y+1));
  Halide::Func maxmin_y("maxmin_y");
  maxmin_y(x,y) = max(max(min_x(x,y-1),min_x(x,y)),min_x(x,y+1));
  Halide::Func midmid_y("midmid_y");
  midmid_y(x,y) = max(min(max(mid_x(x,y-1),mid_x(x,y)),mid_x(x,y+1)),
                      min(mid_x(x,y-1),mid_x(x,y)));

  median(x,y) = max(min(max(minmax_y(x,y),maxmin_y(x,y)),midmid_y(x,y)),
                    min(minmax_y(x,y),maxmin_y(x,y)));
  // max_x.compute_root();
  // min_x.compute_root();
  // mid_x.compute_root();
  median.vectorize(x, isDbl ? DOUBLEVECTORSIZE : VECTORSIZE );
  std::vector<Argument> args(1);
  args[0]  = input;
#ifdef BITCODE
  median.compile_to_bitcode("median.bc", args, target);
#endif
#ifdef STMT
  median.compile_to_lowered_stmt("median.html", args, HTML);
#endif
#ifdef ASSEMBLY
  median.compile_to_assembly("median.s", args, target);
#endif
#ifdef RUN
  COMPILE_OBJ(median);
#endif
}

int main(int argc, char **argv) {
	Target target;
	setupHexagonTarget(target);
        bool isDbl = false;
        if (argc>1) {
          target.set_feature(Target::HVX_DOUBLE);
          isDbl = true;
        }
	test_median(isDbl, target);
	printf ("Done\n");
	return 0;
}

