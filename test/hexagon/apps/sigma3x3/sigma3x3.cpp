/**=============================================================================
Copyright (c) 2015 QUALCOMM Incorporated.
All Rights Reserved Qualcomm Proprietary
=============================================================================**/
#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define COMPILE_OBJ(X)  ((X).compile_to_file("sigma3x3", args, target))

#define TAP(_x, _y) \
    absdiff = cast<uint8_t>(abs(cast<short>(ClampedIn(x - _x, y - _y)) - cast<short>(ClampedIn(x, y)))); \
    sum = select(absdiff <= threshold, sum + cast<unsigned short>(ClampedIn(x - _x, y - _y)), sum); \
    cnt = select(absdiff <= threshold, cnt + 1, cnt)

void test_sigma3x3(Target& target) {
    Halide::Var x("x"), y("y"), k;
    Var xo, xi;

    ImageParam In(type_of<uint8_t>(), 2);
    // ImageParam mask(type_of<int8_t>(), 2);
    Param<int> threshold;

    Image<uint16_t> invTable(10);
    invTable(0) = 0;
    invTable(1) = 32768;
    invTable(2) = 16384;
    invTable(3) = 10922;
    invTable(4) = 8192;
    invTable(5) = 6553;
    invTable(6) = 5461;
    invTable(7) = 4681;
    invTable(8) = 4096;
    invTable(9) = 3640;

    Halide::Func sigma3x3;
    Func ClampedIn = BoundaryConditions::constant_exterior(In, 0);
    ClampedIn.compute_root();
    Expr sum = cast<int>(0), cnt = cast<int>(0), absdiff;

    TAP(-1, -1);
    TAP( 0, -1);
    TAP( 1, -1);

    TAP(-1,  0);
    TAP( 0,  0);
    TAP( 1,  0);

    TAP(-1,  1);
    TAP( 0,  1);
    TAP( 1,  1);

    sigma3x3(x, y) = cast<uint8_t>((cast<int>(sum)*invTable(cnt)+(1<<14))>>15);

    // vectorization disabled for now - until select supported
#ifdef DOVECTOR
    sigma3x3.vectorize(x, 1<<LOG2VLEN);
#endif

    std::vector<Argument> args(2);
    args[0] = In;
    args[1] = threshold;

#ifdef BITCODE
    sigma3x3.compile_to_bitcode("sigma3x3.bc", args, target);
#endif
#ifdef ASSEMBLY
    sigma3x3.compile_to_assembly("sigma3x3.s", args, target);
#endif
#ifdef STMT
    sigma3x3.compile_to_lowered_stmt("sigma3x3.html", args, HTML);
#endif
#ifdef RUN
    COMPILE_OBJ(sigma3x3);
#endif
}

int main(int argc, char **argv) {
    Target target;
    setupHexagonTarget(target);
    test_sigma3x3(target);
    printf("Done\n");
    return 0;
}

