#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Func ref("ref");
    {
        Func num, den;
        num(x, y) = cast<uint16_t>(x);
        den(x, y) = max(1, cast<uint16_t>(y));

        num.compute_root();
        den.compute_root();

        ref(x, y) = cast<uint16_t>(num(x, y)) / cast<uint16_t>(den(x, y));
    }

    Func f("f");
    {
        Func num, den;
        num(x, y) = cast<uint16_t>(x);
        den(x, y) = max(1, cast<uint16_t>(y));

        Expr q = cast<uint16_t>(strict_float(cast<float>(num(x, y)) / cast<float>(den(x, y))));

        Expr quantized = q * den(x, y);
        Expr m = num(x, y) - quantized;
        Expr m_negative = num(x, y) < quantized;

        // m >= 2^15 means that either it underflowed and is actually
        // negative, or the numerator is >= 2^15 and the denominator is
        // strictly larger.

        Expr m_sign_mask = cast<uint16_t>(cast<int16_t>(m) >> 15);
        m_sign_mask = m_sign_mask;

        // We may need to subtract one from the result if the modulus comes out negative.
        /*
        q -= select(m_negative, cast<uint16_t>(1), cast<uint16_t>(0));
        m += select(m_negative, den(x, y), 0);
        */

        q += m_negative;

        //q -= ((den(x, y) - 1) & num(x, y)) >> 15;

        // If the denominator was one and the numerator is large, we
        // just made a mistake, and should add one to undo it
        //q += (num(x, y) >> 15) & (select(den(x, y) == 1, cast<uint16_t>(1), cast<uint16_t>(0)));

        m += select(m_negative, den(x, y), 0);

        Expr worked = (num(x, y) == den(x, y) * q + m) && (m < den(x, y));

        f(x, y) = q; //print_when(!worked, q, "num =", num(x, y), "den =", den(x, y), "q =", q, "m =", m);

        num.compute_root();
        den.compute_root();

    }


    ref.vectorize(x, 16);
    f.vectorize(x, 16);

    ref.compile_to_assembly("/dev/stdout", {}, Target("x86-64-avx2-no_asserts-no_bounds_query-disable_llvm_loop_opt-no_runtime"));
    f.compile_to_assembly("/dev/stdout", {}, Target("x86-64-avx2-no_asserts-no_bounds_query-disable_llvm_loop_opt-no_runtime"));
    //f.compile_to_assembly("/dev/stdout", {}, Target("arm-64-linux-no_asserts-no_bounds_query-disable_llvm_loop_opt-no_runtime"));

    const int tile_bits = 14;

    Buffer<uint16_t> ref_buf(1 << tile_bits, 1 << tile_bits);
    Buffer<uint16_t> f_buf(1 << tile_bits, 1 << tile_bits);

    for (int ty = 0; ty < (1 << (16 - tile_bits)); ty++) {
        for (int tx = 0; tx < (1 << (16 - tile_bits)); tx++) {
            printf("%d %d\n", tx, ty);
            ref_buf.set_min(tx << tile_bits, (ty << tile_bits) + 1);
            f_buf.set_min(tx << tile_bits, (ty << tile_bits) + 1);
            ref.realize(ref_buf);
            f.realize(f_buf);
            if (std::memcmp(f_buf.data(), ref_buf.data(), f_buf.size_in_bytes()) == 0) {
                continue;
            }
            for (int y = ref_buf.dim(1).min(); y < ref_buf.dim(1).max(); y++) {
                for (int x = ref_buf.dim(0).min(); x < ref_buf.dim(0).max(); x++) {
                    if (f_buf(x, y) != ref_buf(x, y)) {
                        printf("(Method 1) %d / %d = %d instead of %d\n", x, y,
                               f_buf(x, y), ref_buf(x, y));
                        return -1;
                    }
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
