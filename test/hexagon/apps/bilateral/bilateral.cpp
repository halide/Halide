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

#define TAP(_x, _y) \
    pixel = ClampedIn(x + _x, y + _y); \
    absdiff = cast<uint8_t>(abs(cast<short>(pixel) - cast<short>(center))); \
    weight = (cast<uint16_t>(range_LUT(absdiff)) * cast<uint16_t>(gauss_LUT((4 + _y) , (4 + _x)))) >> 8; \
    filtered += cast<uint32_t>(pixel)*cast<uint32_t>(weight); \
    weights  += cast<uint32_t>(weight);

#define HORTAPS(_y) \
    TAP(-4, _y); \
    TAP(-3, _y); \
    TAP(-2, _y); \
    TAP(-1, _y); \
    TAP(-0, _y); \
    TAP( 1, _y); \
    TAP( 2, _y); \
    TAP( 3, _y); \
    TAP( 4, _y);

int main(int argc, char **argv) {
    Target target;
    setupHexagonTarget(target, LOG2VLEN == 7 ? Target::HVX_128 : Target::HVX_64);
    commonPerfSetup(target);
    Halide::Var x("x"), y("y"), k;
    Var xo, xi;

    ImageParam In(type_of<uint8_t>(), 2);
    ImageParam gauss_LUT(type_of<uint8_t>(), 2);
    ImageParam range_LUT(type_of<uint8_t>(), 1);

    Halide::Func bilateral9x9;
    Func ClampedIn = BoundaryConditions::constant_exterior(In, 0);
    ClampedIn.compute_root();
    Expr filtered = cast<int>(0), weights = cast<int>(0), weight, absdiff, pixel;

    Expr center = ClampedIn(x, y);
    HORTAPS(-4);
    HORTAPS(-3);
    HORTAPS(-2);
    HORTAPS(-1);
    HORTAPS( 0);
    HORTAPS( 1);
    HORTAPS( 2);
    HORTAPS( 3);
    HORTAPS( 4);

    Expr fZero = weights == 0;
    weights = select(fZero, 1, weights); // avoid div by 0?
    bilateral9x9(x, y) = cast<uint8_t>(select(!fZero, filtered/weights, 0));

#ifdef DOVECTOR
    bilateral9x9.vectorize(x, 1<<LOG2VLEN);
#endif

    std::vector<Argument> args(3);
    args[0] = In;
    args[1] = gauss_LUT;
    args[2] = range_LUT;

#ifdef BITCODE
    bilateral9x9.compile_to_bitcode("bilateral.bc", args, target);
#endif
#ifdef ASSEMBLY
    bilateral9x9.compile_to_assembly("bilateral.s", args, target);
#endif
#ifdef STMT
    bilateral9x9.compile_to_lowered_stmt("bilateral.html", args, HTML);
#endif
#ifdef DOC
    bilateral9x9.compile_to_c("bilateral.c", args, "bilateral_halide", target);
#endif
#ifdef RUN
    bilateral9x9.compile_to_file("bilateral", args, target);
#endif

    printf("Done\n");
    return 0;
}
