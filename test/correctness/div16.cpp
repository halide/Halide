#include "Halide.h"

using namespace Halide;

Expr next_float(Expr e) {
    return reinterpret<float>(reinterpret<int32_t>(e) + 1);
}

Expr prev_float(Expr e) {
    return reinterpret<float>(reinterpret<int32_t>(e) - 1);
}

int main(int argc, char **argv) {
    Var x, y;

    Func num_u, den_u;
    num_u(x, y) = cast<uint16_t>(x);
    den_u(x, y) = max(1, cast<uint16_t>(y));

    Func num_s, den_s;
    Expr n = cast<int16_t>(x);
    Expr d = cast<int16_t>(y);
    num_s(x, y) = select(n == -32768 && d == -1, 0, n);
    den_s(x, y) = select(d == 0, 1, d);

    num_u.compute_root();
    den_u.compute_root();
    num_s.compute_root();
    den_s.compute_root();

    Func ref_u("ref_u");
    Func ref_s("ref_s");
    {
        ref_u(x, y) = num_u(x, y) / den_u(x, y);
        ref_s(x, y) = num_s(x, y) / den_s(x, y);
    }

    Func f_u("f_u"), f_s("f_s");
    {
        // Do the division as a float, then take the floor. If you
        // move down to the previous floating point number before
        // taking the floor you sometimes get the wrong answer
        // (e.g. consider what happens with 1.0f / 1.0f). However if
        // you move on to the next floating point number, or the one
        // after that, it still works. We move onto the next floating
        // point number to make sure that the division operation only
        // has to be exact to within +/-1 in the last place.
        n = num_u(x, y);
        d = den_u(x, y);

        Expr r = cast<float>(n) / cast<float>(d);
        r = next_float(r);
        r = floor(r);
        f_u(x, y) = cast<uint16_t>(strict_float(r));

        n = num_s(x, y);
        d = den_s(x, y);

        r = cast<float>(n) / cast<float>(d);

        Expr d_sign_mask = cast<uint32_t>((cast<uint16_t>(d) & cast<uint16_t>(0x8000))) << 16;
        Expr n_sign_adjust = 1 - cast<int32_t>((n >> 14) & cast<int16_t>(2));

        r = reinterpret<uint32_t>(r);
        // Paranoid adjustment for stability. Slightly more
        // complicated than above. Can safely be done zero times, one
        // time, or two times, so we do it once.
        //r += n_sign_adjust;
        //r += n_sign_adjust;

        r = r ^ d_sign_mask;
        r = reinterpret<float>(r);
        r = floor(r);
        r = reinterpret<uint32_t>(r);
        r = r ^ d_sign_mask;

        r = reinterpret<float>(r);
        r = strict_float(r);
        r = cast<int16_t>(r);
        f_s(x, y) = r;
    }

    // Check the stability of the above algorithm, by moving to the
    // next or previous float before rounding.
    {

    }

    ref_u.vectorize(x, 16);
    ref_s.vectorize(x, 16);
    f_u.vectorize(x, 16);
    f_s.vectorize(x, 16);

    ref_u.compile_to_assembly("/dev/stdout", {}, Target("x86-64-avx2-no_asserts-no_bounds_query-disable_llvm_loop_opt-no_runtime"));
    ref_s.compile_to_assembly("/dev/stdout", {}, Target("x86-64-avx2-no_asserts-no_bounds_query-disable_llvm_loop_opt-no_runtime"));
    f_u.compile_to_assembly("/dev/stdout", {}, Target("x86-64-avx2-no_asserts-no_bounds_query-disable_llvm_loop_opt-no_runtime"));
    f_s.compile_to_assembly("/dev/stdout", {}, Target("x86-64-avx2-no_asserts-no_bounds_query-disable_llvm_loop_opt-no_runtime"));

    const int tile_bits = 14;

    Buffer<uint16_t> ref_u_buf(1 << tile_bits, 1 << tile_bits);
    Buffer<int16_t> ref_s_buf(1 << tile_bits, 1 << tile_bits);
    Buffer<uint16_t> f_u_buf(1 << tile_bits, 1 << tile_bits);
    Buffer<int16_t> f_s_buf(1 << tile_bits, 1 << tile_bits);

    for (int ty = 0; ty < (1 << (16 - tile_bits)); ty++) {
        for (int tx = 0; tx < (1 << (16 - tile_bits)); tx++) {
            printf("%d %d\n", tx, ty);
            ref_u_buf.set_min(tx << tile_bits, (ty << tile_bits) + 1);
            f_u_buf.set_min(tx << tile_bits, (ty << tile_bits) + 1);
            ref_s_buf.set_min(tx << tile_bits, (ty << tile_bits) + 1);
            f_s_buf.set_min(tx << tile_bits, (ty << tile_bits) + 1);
            ref_u.realize(ref_u_buf);
            ref_s.realize(ref_s_buf);
            f_u.realize(f_u_buf);
            f_s.realize(f_s_buf);
            if (std::memcmp(f_u_buf.data(), ref_u_buf.data(), f_u_buf.size_in_bytes()) != 0) {
                for (int y = ref_u_buf.dim(1).min(); y < ref_u_buf.dim(1).max(); y++) {
                    for (int x = ref_u_buf.dim(0).min(); x < ref_u_buf.dim(0).max(); x++) {
                        if (f_u_buf(x, y) != ref_u_buf(x, y)) {
                            printf("(unsigned) %d / %d = %d instead of %d\n", x, y,
                                   f_u_buf(x, y), ref_u_buf(x, y));
                            return -1;
                        }
                    }
                }
            }
            if (std::memcmp(f_s_buf.data(), ref_s_buf.data(), f_s_buf.size_in_bytes()) != 0) {
                for (int y = ref_s_buf.dim(1).min(); y < ref_s_buf.dim(1).max(); y++) {
                    for (int x = ref_s_buf.dim(0).min(); x < ref_s_buf.dim(0).max(); x++) {
                        if (f_s_buf(x, y) != ref_s_buf(x, y)) {
                            printf("(signed) %d / %d = %d instead of %d\n", (int16_t)x, (int16_t)y,
                                   f_s_buf(x, y), ref_s_buf(x, y));
                            return -1;
                        }
                    }
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
