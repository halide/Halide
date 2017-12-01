#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<uint32_t> input(256);
    for (int i = 0; i < 256; i++) {
        input(i) = rand();
    }
    Var x;

    // reinterpret cast
    Func f1;
    f1(x) = reinterpret<float>(input(x));
    Buffer<float> im1 = f1.realize(256);

    for (int x = 0; x < 256; x++) {
        float halide = im1(x);
        float c = Halide::Internal::reinterpret_bits<float>(input(x));
        if (halide != c && std::isnan(halide) ^ std::isnan(c)) {
            printf("reinterpret<float>(%x) -> %f instead of %f\n", input(x), halide, c);
            return -1;
        }
    }

    // bitwise xor
    Func f2;
    f2(x) = input(x) ^ input(x+1);
    Buffer<uint32_t> im2 = f2.realize(128);
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) ^ input(x+1);
        if (im2(x) != correct) {
            printf("%x ^ %x -> %x instead of %x\n",
                   input(x), input(x+1), im2(x), correct);
            return -1;
        }
    }

    // bitwise and
    Func f3;
    f3(x) = input(x) & input(x+1);
    Buffer<uint32_t> im3 = f3.realize(128);
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) & input(x+1);
        if (im3(x) != correct) {
            printf("%x & %x -> %x instead of %x\n",
                   input(x), input(x+1), im3(x), correct);
            return -1;
        }
    }

    // bitwise or
    Func f4;
    f4(x) = input(x) | input(x+1);
    Buffer<uint32_t> im4 = f4.realize(128);
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) | input(x+1);
        if (im4(x) != correct) {
            printf("%x | %x -> %x instead of %x\n",
                   input(x), input(x+1), im4(x), correct);
            return -1;
        }
    }

    // bitwise not
    Func f5;
    f5(x) = ~input(x);
    Buffer<uint32_t> im5 = f5.realize(128);
    for (int x = 0; x < 128; x++) {
        uint32_t correct = ~input(x);
        if (im5(x) != correct) {
            printf("~%x = %x instead of %x\n",
                   input(x), im5(x), correct);
            return -1;
        }
    }

    // shift left combined with masking
    Func f6;
    f6(x) = input(x) << (input(x+1) & 0xf);
    Buffer<uint32_t> im6 = f6.realize(128);
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) << (input(x+1) & 0xf);
        if (im6(x) != correct) {
            printf("%x << (%x & 0xf) -> %x instead of %x\n",
                   input(x), input(x+1), im6(x), correct);
            return -1;
        }
    }

    // logical shift right
    Func f7;
    f7(x) = input(x) >> (input(x+1) & 0xf);
    Buffer<uint32_t> im7 = f7.realize(128);
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) >> (input(x+1) & 0xf);
        if (im7(x) != correct) {
            printf("%x >> (%x & 0xf) -> %x instead of %x\n",
                   input(x), input(x+1), im7(x), correct);
            return -1;
        }
    }

    // arithmetic shift right
    Func f8;
    Expr a = reinterpret<int>(input(x));
    Expr b = reinterpret<int>(input(x+1));
    f8(x) = a >> (b & 0xf);
    Buffer<int> im8 = f8.realize(128);
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) >> (((int)(input(x+1))) & 0xf);
        if (im8(x) != correct) {
            printf("%x >> (%x & 0xf) -> %x instead of %x\n",
                   input(x), input(x+1), im8(x), correct);
            return -1;
        }
    }


    // bit shift on mixed types
    Func f9;
    Expr a32 = cast<int32_t>(input(x));
    Expr b8  = min(31, cast<uint8_t>(input(x+1)));
    f9(x) = a32 >> b8;
    Buffer<int> im9 = f9.realize(128);
    for (int x = 0; x < 128; x++) {
        int lhs = (int)input(x);
        int shift_amount = (uint8_t)(input(x+1));
        shift_amount = std::min(31, shift_amount);
        int correct = lhs >> shift_amount;
        if (im9(x) != correct) {
            printf("%x >> (uint8_t)%x -> %x instead of %x\n",
                   input(x), input(x+1), im9(x), correct);
            return -1;
        }
    }

    // bitwise and on mixed types
    Func f10;
    Expr a8 = cast<int8_t>(input(x));
    f10(x) = a8 & 0xf0;
    Buffer<int8_t> im10 = f10.realize(128);
    for (int x = 0; x < 128; x++) {
        int8_t correct = (int8_t)(input(x)) & 0xf0;
        if (im10(x) != correct) {
            printf("(int8_t)%x & 0xf0 -> %x instead of %x\n",
                   input(x), im10(x), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;

}
