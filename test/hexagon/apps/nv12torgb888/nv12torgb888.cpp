#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;

#define COMPILE_OBJ(X)  ((X).compile_to_file("nv12torgb888", args, target))

/*
 * Conversion from nv12 (4:2:0) to rgb888.
 */


void test_nv12torgb888(Target& target) {
    Var x("x"),y("y");
    ImageParam inputY(type_of<uint8_t>(), 2);
    ImageParam inputUV(type_of<uint8_t>(), 2);

    // Y channel
    Expr scaleY = inputY(x,y);
    scaleY = cast<int32_t>(scaleY);
    scaleY = max(scaleY-16,0);
    scaleY = 1192*scaleY;

    // V channel
    Expr scaleV = inputUV(x - (x & 1),y >> 1);
    scaleV = cast<int32_t>(scaleV) - 128;

    // U channel
    Expr scaleU = inputUV((x - (x & 1)) + 1,y >> 1);
    scaleU = cast<int32_t>(scaleU) - 128;

    // calculate r, g, and b values
    Expr r = clamp(scaleY + 1634*scaleV, 0, 262143);
    Expr g = clamp(scaleY - 833 * scaleV - 400 * scaleU, 0, 262143);
    Expr b = clamp(scaleY + 2066 * scaleU, 0, 262143);

    Expr rgb1 = cast<uint32_t>((0xff<<24) | ((b << 6) & 0xff0000) | ((g >> 2) & 0xff00) | ((r >> 10) & 0xff));


    Func nv12torgb888("nv12torgb888");
    nv12torgb888(x,y) = rgb1;

#ifndef NOVECTOR
    nv12torgb888.vectorize(x,1<<LOG2VLEN);
#endif
    std::vector<Argument> args(2);
    args[0]  = inputY;
    args[1]  = inputUV;

#ifdef BITCODE
  nv12torgb888.compile_to_bitcode("nv12torgb888.bc", args, target);
#endif
#ifdef ASSEMBLY
  nv12torgb888.compile_to_assembly("nv12torgb888.s", args, target);
#endif
#ifdef STMT
  nv12torgb888.compile_to_lowered_stmt("nv12torgb888.html", HTML);
#endif
#ifdef RUN
  COMPILE_OBJ(nv12torgb888);
#endif
}

int main(int argc, char **argv) {
	Target target;
        setupHexagonTarget(target, LOG2VLEN == 7 ? Target::HVX_128 : Target::HVX_64);

        commonPerfSetup(target);
	test_nv12torgb888(target);
	printf ("Done\n");
	return 0;
}

