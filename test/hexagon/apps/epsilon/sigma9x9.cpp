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

#define COMPILE_OBJ(X)  ((X).compile_to_file(#X, args, target))

#define TAP(_x, _y) \
    absdiff = cast<uint8_t>(abs(cast<short>(ClampedIn(x - _x, y - _y)) - cast<short>(ClampedIn(x, y)))); \
    sum = select(absdiff <= threshold, sum + cast<unsigned short>(ClampedIn(x - _x, y - _y)), sum); \
    cnt = select(absdiff <= threshold, cnt + 1, cnt)

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

void test_sigma9x9(Target& target) {
    Halide::Var x("x"), y("y"), k;
    Var xo, xi;

    ImageParam In(type_of<uint8_t>(), 2);
    // ImageParam mask(type_of<int8_t>(), 2);
    Param<int> threshold;
    Image<uint16_t> invTable(128);
    invTable(0)  = 0;
    invTable(1)  = 32768;
    invTable(2)  = 16384;
    invTable(3)  = 10923;
    invTable(4)  = 8192;
    invTable(5)  = 6554;
    invTable(6)  = 5461;
    invTable(7)  = 4681;
    invTable(8)  = 4096;
    invTable(9)  = 3641;
    invTable(10) = 3277;
    invTable(11) = 2979;
    invTable(12) = 2731;
    invTable(13) = 2521;
    invTable(14) = 2341;
    invTable(15) = 2185;
    invTable(16) = 2048;
    invTable(17) = 1928;
    invTable(18) = 1820;
    invTable(19) = 1725;
    invTable(20) = 1638;
    invTable(21) = 1560;
    invTable(22) = 1489;
    invTable(23) = 1425;
    invTable(24) = 1365;
    invTable(25) = 1311;
    invTable(26) = 1260;
    invTable(27) = 1214;
    invTable(28) = 1170;
    invTable(29) = 1130;
    invTable(30) = 1092;
    invTable(31) = 1057;
    invTable(32) = 1024;
    invTable(33) = 993;
    invTable(34) = 964;
    invTable(35) = 936;
    invTable(36) = 910;
    invTable(37) = 886;
    invTable(38) = 862;
    invTable(39) = 840;
    invTable(40) = 819;
    invTable(41) = 799;
    invTable(42) = 780;
    invTable(43) = 762;
    invTable(44) = 745;
    invTable(45) = 728;
    invTable(46) = 712;
    invTable(47) = 697;
    invTable(48) = 683;
    invTable(49) = 669;
    invTable(50) = 655;
    invTable(51) = 643;
    invTable(52) = 630;
    invTable(53) = 618;
    invTable(54) = 607;
    invTable(55) = 596;
    invTable(56) = 585;
    invTable(57) = 575;
    invTable(58) = 565;
    invTable(59) = 555;
    invTable(60) = 546;
    invTable(61) = 537;
    invTable(62) = 529;
    invTable(63) = 520;
    invTable(64) = 512;
    invTable(65) = 504;
    invTable(66) = 496;
    invTable(67) = 489;
    invTable(68) = 482;
    invTable(69) = 475;
    invTable(70) = 468;
    invTable(71) = 462;
    invTable(72) = 455;
    invTable(73) = 449;
    invTable(74) = 443;
    invTable(75) = 437;
    invTable(76) = 431;
    invTable(77) = 426;
    invTable(78) = 420;
    invTable(79) = 415;
    invTable(80) = 410;
    invTable(81) = 405;
    invTable(82) = 400;
    invTable(83) = 395;
    invTable(84) = 390;
    invTable(85) = 386;
    invTable(86) = 381;
    invTable(87) = 377;
    invTable(88) = 372;
    invTable(89) = 368;
    invTable(90) = 364;
    invTable(91) = 360;
    invTable(92) = 356;
    invTable(93) = 352;
    invTable(94) = 349;
    invTable(95) = 345;
    invTable(96) = 341;
    invTable(97) = 338;
    invTable(98) = 334;
    invTable(99) = 331;
    invTable(100)= 328;
    invTable(101)= 324;
    invTable(102)= 321;
    invTable(103)= 318;
    invTable(104)= 315;
    invTable(105)= 312;
    invTable(106)= 309;
    invTable(107)= 306;
    invTable(108)= 303;
    invTable(109)= 301;
    invTable(110)= 298;
    invTable(111)= 295,
    invTable(112)= 293;
    invTable(113)= 290;
    invTable(114)= 287;
    invTable(115)= 285;
    invTable(116)= 282;
    invTable(117)= 280;
    invTable(118)= 278;
    invTable(119)= 275;
    invTable(120)= 273;
    invTable(121)= 271;
    invTable(122)= 269;
    invTable(123)= 266;
    invTable(124)= 264;
    invTable(125)= 262;
    invTable(126)= 260;
    invTable(127)= 258;

    Halide::Func sigma9x9;
    Func ClampedIn = BoundaryConditions::constant_exterior(In, 0);
    ClampedIn.compute_root();
    Expr sum = cast<int>(0), cnt = cast<int>(0), absdiff;

    HORTAPS(-4);
    HORTAPS(-3);
    HORTAPS(-2);
    HORTAPS(-1);
    HORTAPS(-0);
    HORTAPS( 1);
    HORTAPS( 2);
    HORTAPS( 3);
    HORTAPS( 4);

    sigma9x9(x, y) = cast<uint8_t>((cast<int>(sum)*invTable(cnt)+(1<<14))>>15);

    // sigma9x9.compute_root();

    // vectorization disabled for now - until select supported
#ifdef DOVECTOR
    sigma9x9.vectorize(x, 1<<LOG2VLEN);
#endif

    std::vector<Argument> args(2);
    args[0] = In;
    args[1] = threshold;

#ifdef BITCODE
    sigma3x3.compile_to_bitcode("sigma9x9.bc", args, target);
#endif
#ifdef ASSEMBLY
    sigma3x3.compile_to_assembly("sigma9x9.s", args, target);
#endif
#ifdef STMT
    sigma3x3.compile_to_lowered_stmt("sigma9x9.html", args, HTML);
#endif
#ifdef RUN
    COMPILE_OBJ(sigma9x9);
#endif
}

int main(int argc, char **argv) {
    Target target;
    setupHexagonTarget(target);
    test_sigma9x9(target);
    printf("Done\n");
    return 0;
}

