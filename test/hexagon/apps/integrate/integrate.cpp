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

int main(int argc, char **argv) {
    Target target;
    setupHexagonTarget(target);
#if LOG2VLEN == 7
    target.set_feature(Target::HVX_DOUBLE);
#endif

    Halide::Var x("x"), y("y");
    Var xo, xi;

    ImageParam In(type_of<uint8_t>(), 2);

    Halide::Func integrate;
    // Compute summed-area table
    integrate(x, y) = cast<uint32_t>(In(x, y));
    RDom r1(1, In.width()-1, 0, 1 );
    integrate(r1.x, r1.y) += integrate(r1.x - 1, r1.y);
    RDom r0(0, 1, 1, In.height()-1);
    integrate(x, r0.y) += integrate(x, r0.y - 1);
    integrate.vectorize(x, 1 << LOG2VLEN);

    std::vector<Argument> args(1);
    args[0] = In;
#ifdef BITCODE
    integrate.compile_to_bitcode("integrate.bc", args, target);
#endif
#ifdef ASSEMBLY
    integrate.compile_to_assembly("integrate.s", args, target);
#endif
#ifdef STMT
    integrate.compile_to_lowered_stmt("integrate.html", args, HTML);
#endif
#ifdef DOC
    integrate.compile_to_c("integrate.c", args, "integrate_halide", target);
#endif
#ifdef RUN
    integrate.compile_to_file("integrate", args, target);
#endif
    printf("Done\n");
    return 0;
}
