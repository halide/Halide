#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;

using namespace Halide::Internal;
IRPrinter irp(std::cerr);


// RUN: rm -f stdout; ./nv12-max.out; llvm-dis -o stdout nv12torgb888.bc ;FileCheck %s < stdout
//CHECK: vmax

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
    Expr r = clamp(scaleY, 0, 262143);
    Func nv12torgb888("nv12torgb888");
    nv12torgb888(x,y) = r;

    nv12torgb888.vectorize(x,128);
    std::vector<Argument> args(2);
    args[0]  = inputY;
    args[1]  = inputUV;

  nv12torgb888.compile_to_bitcode("nv12torgb888.bc", args, target);
}

int main(int argc, char **argv) {
	Target target;
	setupHexagonTarget(target);
        target.set_feature(Target::HVX_DOUBLE);
	test_nv12torgb888(target);
	printf ("Done\n");
	return 0;
}

