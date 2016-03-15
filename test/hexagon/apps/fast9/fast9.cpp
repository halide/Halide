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
    setupHexagonTarget(target, LOG2VLEN == 7 ? Target::HVX_128 : Target::HVX_64);
    commonPerfSetup(target);
    Halide::Var x("x");

    ImageParam In(type_of<uint8_t>(), 2);
    Param<unsigned int> barrier;
    Param<unsigned int> border;
    ImageParam corner(type_of<uint8_t>(), 1);

    Halide::Func Out;

    Halide::Expr Cn = cast<int32_t>(In(x, 0));
#if 1
    Halide::Expr cb = cast<uint8_t>(min(255, Cn + cast<int>(50)));
    Halide::Expr c_b = cast<uint8_t>(max(0, Cn - cast<int>(50)));
#else
    Halide::Expr cb = cast<uint8_t>(min(255, Cn + cast<int>(barrier)));
    Halide::Expr c_b = cast<uint8_t>(max(0, Cn - cast<int>(barrier)));
#endif

    Halide::Expr brevenbit = cast<uint8_t>(0);
    Halide::Expr broddbit = cast<uint8_t>(0);
    Halide::Expr dkevenbit = cast<uint8_t>(0);
    Halide::Expr dkoddbit = cast<uint8_t>(0);

    brevenbit = select(In(x+0, 0+3) > cb, brevenbit + (1 << 7), brevenbit);
    brevenbit = select(In(x+2, 0+2) > cb, brevenbit + (1 << 6), brevenbit);
    brevenbit = select(In(x+3, 0+0) > cb, brevenbit + (1 << 5), brevenbit);
    brevenbit = select(In(x+2, 0-2) > cb, brevenbit + (1 << 4), brevenbit);
    brevenbit = select(In(x+0, 0-3) > cb, brevenbit + (1 << 3), brevenbit);
    brevenbit = select(In(x-2, 0-2) > cb, brevenbit + (1 << 2), brevenbit);
    brevenbit = select(In(x-3, 0+0) > cb, brevenbit + (1 << 1), brevenbit);
    brevenbit = select(In(x-2, 0+2) > cb, brevenbit + (1 << 0), brevenbit);

    broddbit = select(In(x+1, 0+3) > cb, broddbit + (1 << 7), broddbit);
    broddbit = select(In(x+3, 0+1) > cb, broddbit + (1 << 6), broddbit);
    broddbit = select(In(x+3, 0-1) > cb, broddbit + (1 << 5), broddbit);
    broddbit = select(In(x+1, 0-3) > cb, broddbit + (1 << 4), broddbit);
    broddbit = select(In(x-1, 0-3) > cb, broddbit + (1 << 3), broddbit);
    broddbit = select(In(x-3, 0-1) > cb, broddbit + (1 << 2), broddbit);
    broddbit = select(In(x-3, 0+1) > cb, broddbit + (1 << 1), broddbit);
    broddbit = select(In(x-1, 0+3) > cb, broddbit + (1 << 0), broddbit);

    dkevenbit = select(In(x+0, 0+3) < c_b, dkevenbit + (1 << 7), dkevenbit);
    dkevenbit = select(In(x+2, 0+2) < c_b, dkevenbit + (1 << 6), dkevenbit);
    dkevenbit = select(In(x+3, 0+0) < c_b, dkevenbit + (1 << 5), dkevenbit);
    dkevenbit = select(In(x+2, 0-2) < c_b, dkevenbit + (1 << 4), dkevenbit);
    dkevenbit = select(In(x+0, 0-3) < c_b, dkevenbit + (1 << 3), dkevenbit);
    dkevenbit = select(In(x-2, 0-2) < c_b, dkevenbit + (1 << 2), dkevenbit);
    dkevenbit = select(In(x-3, 0+0) < c_b, dkevenbit + (1 << 1), dkevenbit);
    dkevenbit = select(In(x-2, 0+2) < c_b, dkevenbit + (1 << 0), dkevenbit);

    dkoddbit = select(In(x+1, 0+3) < c_b, dkoddbit + (1 << 7), dkoddbit);
    dkoddbit = select(In(x+3, 0+1) < c_b, dkoddbit + (1 << 6), dkoddbit);
    dkoddbit = select(In(x+3, 0-1) < c_b, dkoddbit + (1 << 5), dkoddbit);
    dkoddbit = select(In(x+1, 0-3) < c_b, dkoddbit + (1 << 4), dkoddbit);
    dkoddbit = select(In(x-1, 0-3) < c_b, dkoddbit + (1 << 3), dkoddbit);
    dkoddbit = select(In(x-3, 0-1) < c_b, dkoddbit + (1 << 2), dkoddbit);
    dkoddbit = select(In(x-3, 0+1) < c_b, dkoddbit + (1 << 1), dkoddbit);
    dkoddbit = select(In(x-1, 0+3) < c_b, dkoddbit + (1 << 0), dkoddbit);

    Halide::Expr br01 = cast<uint8_t>(brevenbit & broddbit);
    Halide::Expr br23 = cast<uint8_t>((br01 + br01) + (br01 >> 7));
    Halide::Expr br03 = cast<uint8_t>(br01 & br23);
    Halide::Expr br47 = cast<uint8_t>((br03 << 2) + (br03 >> 6));
    Halide::Expr br07 = cast<uint8_t>(br03 & br47);

    Halide::Expr br8 = cast<uint8_t>((brevenbit << 4) + (brevenbit >> 4));
    Halide::Expr br15 = cast<uint8_t>((broddbit << 7) + (broddbit >> 1));
    Halide::Expr br8o15 = cast<uint8_t>(br8 | br15);
    Halide::Expr brcorner = cast<uint8_t>(br07 & br8o15);

    Halide::Expr dk01 = cast<uint8_t>(dkevenbit & dkoddbit);
    Halide::Expr dk23 = cast<uint8_t>((dk01 + dk01) + (dk01 >> 7));
    Halide::Expr dk03 = cast<uint8_t>(dk01 & dk23);
    Halide::Expr dk47 = cast<uint8_t>((dk03 << 2) + (dk03 >> 6));
    Halide::Expr dk07 = cast<uint8_t>(dk03 & dk47);

    Halide::Expr dk8 = cast<uint8_t>((dkevenbit << 4) + (dkevenbit >> 4));
    Halide::Expr dk15 = cast<uint8_t>((dkoddbit << 7) + (dkoddbit >> 1));
    Halide::Expr dk8o15 = dk8 | dk15;
    Halide::Expr dkcorner = dk07 & dk8o15;

    Halide::Expr iscorner = brcorner | dkcorner;

    Out(x) = cast<uint8_t>(iscorner);
#ifdef DOVECTOR
    Out.vectorize(x, 1<<LOG2VLEN);
#endif
    std::vector<Argument> args(3);
    args[0] = In;
    args[1] = barrier;
    args[2] = border;
#ifdef BITCODE
  Out.compile_to_bitcode("fast9.bc", args, target);
#endif
#ifdef ASSEMBLY
  Out.compile_to_assembly("fast9.s", args, target);
#endif
#ifdef STMT
  Out.compile_to_lowered_stmt("fast9.html", args, HTML);
#endif
#ifdef RUN
  Out.compile_to_file("fast9", args, target);
#endif

    printf("Done\n");
    return 0;
}
