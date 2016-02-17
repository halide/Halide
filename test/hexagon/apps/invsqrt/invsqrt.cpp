#include <Halide.h>
#include "halide-hexagon-setup.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

#define VECTORSIZE 64 //Vector width in bytes. (Single mode)
#define DOUBLEVECTORSIZE 128
#define COMPILE_OBJ(X)  ((X).compile_to_file("invsqrt", args, target))

void test_invsqrt(Target& target) {
    Var x("x");
    ImageParam input(type_of<unsigned short>(), 1);

    Image<unsigned short> val_table(24);
    val_table(0) = 4096;
    val_table(1) = 3862;
    val_table(2) = 3664;
    val_table(3) = 3493;
    val_table(4) = 3344;
    val_table(5) = 3213;
    val_table(6) = 3096;
    val_table(7) = 2991;
    val_table(8) = 2896;
    val_table(9) = 2810;
    val_table(10) = 2731;
    val_table(11) = 2658;
    val_table(12) = 2591;
    val_table(13) = 2528;
    val_table(14) = 2470;
    val_table(15) = 2416;
    val_table(16) = 2365;
    val_table(17) = 2317;
    val_table(18) = 2272;
    val_table(19) = 2230;
    val_table(20) = 2189;
    val_table(21) = 2151;
    val_table(22) = 2115;
    val_table(23) = 2081;

    Image<unsigned short> slope_table(24);
    slope_table(0) = 234;
    slope_table(1) = 198;
    slope_table(2) = 171;
    slope_table(3) = 149;
    slope_table(4) = 131;
    slope_table(5) = 117;
    slope_table(6) = 105;
    slope_table(7) = 95;
    slope_table(8) = 86;
    slope_table(9) = 79;
    slope_table(10) = 73;
    slope_table(11) = 67;
    slope_table(12) = 63;
    slope_table(13) = 58;
    slope_table(14) = 54;
    slope_table(15) = 51;
    slope_table(16) = 48;
    slope_table(17) = 45;
    slope_table(18) = 42;
    slope_table(19) = 41;
    slope_table(20) = 38;
    slope_table(21) = 36;
    slope_table(22) = 34;
    slope_table(23) = 33;

    Expr x1 = cast<unsigned int>(input(x));
    x1 = select(x1 == 0, 1, x1);

    Expr shft = (31-count_leading_zeros(x1))>>1;

    Expr shift_nbits = 13 - 2 * cast<short int>(shft);
    Expr t1 = select(shift_nbits >= 0, x1 << shift_nbits, x1 >> -shift_nbits);
    Expr idx = cast<unsigned short>(clamp((t1 >> 10) - 8,0,23));
    t1 = t1 & 0x3ff;
    Expr t3 = cast<int>( (slope_table(idx) * t1 + 512) >> 10);

    Func invsqrt("invsqrt");
    invsqrt(x) = {cast<unsigned short>(shft),cast<unsigned short>(val_table(idx) - t3)};

#ifndef NOVECTOR
    invsqrt.vectorize(x, VECTORSIZE);
#endif
    std::vector<Argument> args(1);
    args[0]  = input;

#ifdef BITCODE
  invsqrt.compile_to_bitcode("invsqrt.bc", args, target);
#endif
#ifdef ASSEMBLY
  invsqrt.compile_to_assembly("invsqrt.s", args, target);
#endif
#ifdef STMT
  invsqrt.compile_to_lowered_stmt("invsqrt.html", HTML);
#endif
#ifdef RUN
  COMPILE_OBJ(invsqrt);
#endif
}

int main(int argc, char **argv) {
	Target target;
	setupHexagonTarget(target);
	test_invsqrt(target);
	printf ("Done\n");
	return 0;
}

