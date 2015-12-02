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
    absdiff = abs(cast<short>(ClampedIn(x - _x, y - _y)) - cast<short>(ClampedIn(x, y))); \
    sum = select(absdiff <= threshold, sum + cast<unsigned short>(ClampedIn(x - _x, y - _y)), sum); \
    cnt = select(absdiff <= threshold, cnt + 1, cnt)

#ifndef RDOM
void test_sigma3x3(Target& target) {
    Halide::Var x("x"), y("y"), k;
    Var xo, xi;

    ImageParam In(type_of<uint8_t>(), 2);
    // ImageParam mask(type_of<int8_t>(), 2);
    // Making threshold uint16_t because absdiff is going to
    // be unsigned.
    Param<uint16_t> threshold;

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
    int i, j;
    for (i = -1; i <= 1 ; ++i) {
      for (j = -1; j <= 1; ++j) {
        TAP(j, i);
      }
    }

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

#else

void test_sigma3x3(Target& target) {
    Halide::Var x("x"), y("y"), k;
    Var xo, xi;

    ImageParam In(type_of<uint8_t>(), 2);
    set_min(In, 0, 0);
    set_min(In, 1, 0);
    set_stride_multiple(In, 1, 1 << LOG2VLEN);

    // ImageParam mask(type_of<int8_t>(), 2);
    // Making threshold uint16_t because absdiff is going to
    // be unsigned.
    Param<uint16_t> threshold;

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
    Halide::Func sum, cnt;
    sum(x, y) = cast<uint16_t> (0);
    cnt(x, y) = cast<uint16_t> (0);

    Halide::RDom r(-1,3,-1,3);
    Expr absdiff;
    absdiff = abs(cast<short>(In(x + r.x, y + r.y)) - cast<short>(In(x, y)));
    sum(x, y) += select(absdiff <= threshold, cast<unsigned short>(In(x + r.x, y + r.y)), 0);
    cnt(x, y) += select(absdiff <= threshold, cast<unsigned short>(1), cast<unsigned short>(0));

    sigma3x3(x, y) = cast<uint8_t>(((sum(x,y))*invTable(clamp(cnt(x,y), 0, 9))+(1<<14))>>15);

    // vectorization disabled for now - until select supported
#ifdef DOVECTOR
    sigma3x3.vectorize(x, 1<<LOG2VLEN);
    // sum and cnt are reductions. We want to unroll the loops in the update step.
    sum.update(0).unroll(r.y).unroll(r.x);
    cnt.update(0).unroll(r.y).unroll(r.x);
#endif
    set_output_buffer_min(sigma3x3, 0, 0);
    set_output_buffer_min(sigma3x3, 1, 0);
    set_stride_multiple(sigma3x3, 1, 1 << LOG2VLEN);

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
#endif
int main(int argc, char **argv) {
    Target target;
    setupHexagonTarget(target, LOG2VLEN == 7 ? Target::HVX_128 : Target::HVX_64);
    commonPerfSetup(target);
    test_sigma3x3(target);
    printf("Done\n");
    return 0;
}

