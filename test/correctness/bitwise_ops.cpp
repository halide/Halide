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
    Buffer<float> im1 = f1.realize({256});

    for (int x = 0; x < 256; x++) {
        float halide = im1(x);
        float c = Halide::Internal::reinterpret_bits<float>(input(x));
        if (halide != c && std::isnan(halide) ^ std::isnan(c)) {
            printf("reinterpret<float>(%x) -> %f instead of %f\n", input(x), halide, c);
            return 1;
        }
    }

    // bitwise xor
    Func f2;
    f2(x) = input(x) ^ input(x + 1);
    Buffer<uint32_t> im2 = f2.realize({128});
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) ^ input(x + 1);
        if (im2(x) != correct) {
            printf("%x ^ %x -> %x instead of %x\n",
                   input(x), input(x + 1), im2(x), correct);
            return 1;
        }
    }

    // bitwise and
    Func f3;
    f3(x) = input(x) & input(x + 1);
    Buffer<uint32_t> im3 = f3.realize({128});
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) & input(x + 1);
        if (im3(x) != correct) {
            printf("%x & %x -> %x instead of %x\n",
                   input(x), input(x + 1), im3(x), correct);
            return 1;
        }
    }

    // bitwise or
    Func f4;
    f4(x) = input(x) | input(x + 1);
    Buffer<uint32_t> im4 = f4.realize({128});
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) | input(x + 1);
        if (im4(x) != correct) {
            printf("%x | %x -> %x instead of %x\n",
                   input(x), input(x + 1), im4(x), correct);
            return 1;
        }
    }

    // bitwise not
    Func f5;
    f5(x) = ~input(x);
    Buffer<uint32_t> im5 = f5.realize({128});
    for (int x = 0; x < 128; x++) {
        uint32_t correct = ~input(x);
        if (im5(x) != correct) {
            printf("~%x = %x instead of %x\n",
                   input(x), im5(x), correct);
            return 1;
        }
    }

    // shift left combined with masking
    Func f6;
    f6(x) = input(x) << (input(x + 1) & 0xf);
    Buffer<uint32_t> im6 = f6.realize({128});
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) << (input(x + 1) & 0xf);
        if (im6(x) != correct) {
            printf("%x << (%x & 0xf) -> %x instead of %x\n",
                   input(x), input(x + 1), im6(x), correct);
            return 1;
        }
    }

    // logical shift right
    Func f7;
    f7(x) = input(x) >> (input(x + 1) & 0xf);
    Buffer<uint32_t> im7 = f7.realize({128});
    for (int x = 0; x < 128; x++) {
        uint32_t correct = input(x) >> (input(x + 1) & 0xf);
        if (im7(x) != correct) {
            printf("%x >> (%x & 0xf) -> %x instead of %x\n",
                   input(x), input(x + 1), im7(x), correct);
            return 1;
        }
    }

    // arithmetic shift right
    Func f8;
    Expr a = reinterpret<int>(input(x));
    Expr b = reinterpret<unsigned>(input(x + 1));
    f8(x) = a >> (b & 0x1f);
    Buffer<int> im8 = f8.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) >> (((int)(input(x + 1))) & 0x1f);
        if (im8(x) != correct) {
            printf("%x >> uint32(%x & 0x1f) -> %x instead of %x\n",
                   input(x), input(x + 1), im8(x), correct);
            return 1;
        }
    }

    // bit shift on mixed types
    Func f9;
    Expr a32 = cast<int32_t>(input(x));
    Expr b8 = cast<int32_t>(min(31, cast<uint8_t>(input(x + 1))));
    f9(x) = a32 >> b8;
    Buffer<int> im9 = f9.realize({128});
    for (int x = 0; x < 128; x++) {
        int lhs = (int)input(x);
        int shift_amount = (uint8_t)(input(x + 1));
        shift_amount = std::min(31, shift_amount);
        int correct = lhs >> shift_amount;
        if (im9(x) != correct) {
            printf("%d >> %d -> %d instead of %d\n",
                   input(x), shift_amount, im9(x), correct);
            return 1;
        }
    }

    // arithmetic shift left with signed type (positive value)
    Func f10;
    Expr a10 = cast<int>(input(x));
    Expr b10 = cast<int>(input(x + 1));
    f10(x) = a10 << (b10 & 0x1f);
    Buffer<int> im10 = f10.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) << (((int)(input(x + 1))) & 0x1f);
        if (im10(x) != correct) {
            printf("%x << (%x & 0x1f) -> %x instead of %x\n",
                   input(x), input(x + 1), im10(x), correct);
            return 1;
        }
    }

    // arithmetic shift right with signed type (positive value) and mixed types
    Func f11;
    Expr a11 = cast<int>(input(x));
    Expr b11 = cast<int>(input(x + 1));
    f11(x) = a11 >> cast<int16_t>(b11 & 0x0f);
    Buffer<int> im11 = f11.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) >> (((int)(input(x + 1))) & 0x0f);
        if (im11(x) != correct) {
            printf("%x >> (%x & 0x1f) -> %x instead of %x\n",
                   input(x), input(x + 1), im11(x), correct);
            return 1;
        }
    }

    // arithmetic shift left with signed type (negative value)
    Func f12;
    Expr a12 = cast<int>(input(x));
    Expr b12 = cast<int>(input(x + 1));
    f12(x) = a12 << (-1 * (b12 & 0x1f));
    Buffer<int> im12 = f12.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) >> (((int)(input(x + 1))) & 0x1f);
        if (im12(x) != correct) {
            printf("%x << (-1 * (%x & 0x1f)) -> %x instead of %x\n",
                   input(x), input(x + 1), im12(x), correct);
            return 1;
        }
    }

    // arithmetic shift right with signed type (negative value)
    Func f13;
    Expr a13 = cast<int>(input(x));
    Expr b13 = cast<int>(input(x + 1));
    f13(x) = a13 >> (-1 * (b13 & 0x1f));
    Buffer<int> im13 = f13.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) << (((int)(input(x + 1))) & 0x1f);
        if (im13(x) != correct) {
            printf("%x >> (-1 * (%x & 0x1f)) -> %x instead of %x\n",
                   input(x), input(x + 1), im13(x), correct);
            return 1;
        }
    }

    // arithmetic shift left with signed type (constant positive value)
    Func f14;
    Expr a14 = cast<int>(input(x));
    int b14 = 4;
    f14(x) = a14 << b14;
    Buffer<int> im14 = f14.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) << 4;
        if (im14(x) != correct) {
            printf("%x << %x -> %x instead of %x\n",
                   input(x), b14, im14(x), correct);
            return 1;
        }
    }

    // arithmetic shift right with signed type (constant positive value)
    Func f15;
    Expr a15 = cast<int>(input(x));
    int b15 = 4;
    f15(x) = a15 >> b15;
    Buffer<int> im15 = f15.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) >> 4;
        if (im15(x) != correct) {
            printf("%x >> %x -> %x instead of %x\n",
                   input(x), b15, im15(x), correct);
            return 1;
        }
    }

    // arithmetic shift left with signed type (constant negative value)
    Func f16;
    Expr a16 = cast<int>(input(x));
    int b16 = -4;
    f16(x) = a16 << b16;
    Buffer<int> im16 = f16.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) >> 4;
        if (im16(x) != correct) {
            printf("%x << %x -> %x instead of %x\n",
                   input(x), b16, im16(x), correct);
            return 1;
        }
    }

    // arithmetic shift right with signed type (constant negative value)
    Func f17;
    Expr a17 = cast<int>(input(x));
    int b17 = -4;
    f17(x) = a17 >> b17;
    Buffer<int> im17 = f17.realize({128});
    for (int x = 0; x < 128; x++) {
        int correct = ((int)(input(x))) << 4;
        if (im17(x) != correct) {
            printf("%x >> %x -> %x instead of %x\n",
                   input(x), b17, im17(x), correct);
            return 1;
        }
    }

    // bitwise and on mixed types
    Func f18;
    Expr a8 = cast<int8_t>(input(x));
    f18(x) = a8 & cast<int8_t>(0xf0);
    Buffer<int8_t> im18 = f18.realize({128});
    for (int x = 0; x < 128; x++) {
        int8_t correct = (int8_t)(input(x)) & 0xf0;
        if (im18(x) != correct) {
            printf("(int8_t)%x & 0xf0 -> %x instead of %x\n",
                   input(x), im18(x), correct);
            return 1;
        }
    }

    // bitwise xor scalar/vector
    Expr vec = cast(UInt(8).with_lanes(4), 42) ^ 3;
    assert(vec.type().lanes() == 4);

    // Ensure signedness is preserved.
    Expr vec2 = cast(UInt(8).with_lanes(4), 42) & 3;
    assert(vec.type().is_uint());

    // Ensure that bitwise op is commutative re: type.  (This was not
    // true at least for some time, which is problematic given that
    // simplification and other things assume expressions can be
    // reordered.)
    {
        Expr a = cast(UInt(8), 42);
        Expr b = cast(UInt(16), 199);

        Expr a_then_b = a ^ b;
        Expr b_then_a = b ^ a;

        assert(a_then_b.type() == b_then_a.type());
    }

    printf("Success!\n");
    return 0;
}
