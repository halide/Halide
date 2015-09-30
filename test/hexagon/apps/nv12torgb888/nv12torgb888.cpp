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

    Expr scaleY = inputY(x,y) & 0xff;
    scaleY = cast<int>(scaleY) - 16;
    scaleY = 1192 * max(scaleY,0);

    Expr V = select((x & 1) == 0, (inputUV(x,y) & 0xff) -128, (inputUV(x-1,y) & 0xff) -128);
    Expr U = select((x & 1) == 0, (inputUV(x+1,y) & 0xff) -128, (inputUV(x,y)));
    Expr R = clamp(scaleY + 1634 * cast<int>(V), 0, 262143);
    Expr G = clamp(scaleY - 833 * cast<int>(V) -400 * cast<int>(U), 0, 262143);
    Expr B = clamp(scaleY + 2066 * cast<int>(U), 0, 262143);

    Expr rgb1 = (((B << 6) & 0xff0000)
                | ((G >> 2) & 0xff00) | ((R >> 10) & 0xff)) | (0xff<<24);

    Func nv12torgb888("nv12torgb888");
    nv12torgb888(x,y) = cast<uint32_t>(rgb1);

#ifndef NOVECTOR
    nv12torgb888.vectorize(x, 1<<LOG2VLEN);
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
	setupHexagonTarget(target);
	test_nv12torgb888(target);
	printf ("Done\n");
	return 0;
}

