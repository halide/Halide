#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {

    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    // 8-bit mat-mul into 32-bit accumulator
    {

        double times[2];

        for (int use_nested_vectorization = 0; use_nested_vectorization < 2; use_nested_vectorization++) {
            Var x, y;

            ImageParam f(UInt(8), 2), g(UInt(8), 2);

            RDom r(0, 128);

            Func prod;
            prod(x, y) += cast<int32_t>(f(x, r)) * g(r, y);

            Var xi, yi, xo, yo;
            Var bx, tx, by, ty;
            RVar ro, ri, rio, rii;

            if (use_nested_vectorization) {
                if (target.arch == Target::X86) {
                    // x86 schedule. Exploits the ability of pmaddwd
                    // to pull one arg from memory. Because we'll be
                    // intentionally spilling, the tile will be
                    // absurdly large for a gemm.
                    const int vec = target.natural_vector_size<uint8_t>();

                    prod.in()
                        .tile(x, y, xi, yi, vec, vec / 2)
                        .vectorize(xi)
                        .unroll(yi);

                    f.in().compute_at(prod, ro).vectorize(_0).unroll(_1);
                    g.in().compute_at(prod, y).vectorize(_0).unroll(_1);

                    prod.compute_at(prod.in(), x)
                        .vectorize(x)
                        .unroll(y)
                        .update()
                        .split(r, ro, ri, vec / 2)
                        .reorder(ri, x, y, ro)
                        .vectorize(x)
                        .unroll(y)
                        .atomic()
                        .vectorize(ri, 2)
                        .unroll(ri);
                } else {
                    // ARM schedule. Exploits SDOT when available.
                    const int reduce = target.has_feature(Target::ARMDotProd) ? 4 : 2;

                    prod.in()
                        .tile(x, y, xi, yi, 8, 8)
                        .vectorize(xi)
                        .unroll(yi);

                    f.in().compute_at(prod, ro).vectorize(_0).unroll(_1);
                    g.in().compute_at(prod, y).vectorize(_0).unroll(_1);

                    prod.compute_at(prod.in(), x)
                        .vectorize(x)
                        .unroll(y)
                        .update()
                        .split(r, ro, ri, reduce)
                        .reorder(ri, x, y, ro)
                        .vectorize(x)
                        .unroll(y)
                        .atomic()
                        .vectorize(ri, reduce)
                        .unroll(ri);
                }
            } else {
                g.in().compute_at(prod, ro).vectorize(_0).unroll(_1);

                const int vec = target.natural_vector_size<uint8_t>();

                prod.in()
                    .tile(x, y, xi, yi, vec, 8, TailStrategy::RoundUp)
                    .vectorize(xi)
                    .unroll(yi);

                prod.compute_at(prod.in(), x)
                    .vectorize(x)
                    .unroll(y)
                    .update()
                    .split(r, ro, ri, 8)
                    .reorder(ri, x, y, ro)
                    .vectorize(x)
                    .unroll(y)
                    .unroll(ri);
            }

            Buffer<uint8_t> f_buf(1024, 1024);
            f_buf.fill(100);
            Buffer<uint8_t> g_buf(1024, 1024);
            f_buf.fill(100);
            f.set(f_buf);
            g.set(g_buf);
            Buffer<int32_t> out(1024, 1024);

            Func result = prod.in();

            // Uncomment to check the asm
            // result.compile_to_assembly("/dev/stdout", {f, g}, target);

            times[use_nested_vectorization] =
                Tools::benchmark(20, 20, [&]() {
                    result.realize(out, target);
                    out.device_sync();
                });
        }

        double speed_up = times[0] / times[1];
        printf("8-bit gemm\n"
               "Time with nested vectorization: %0.2f ms \n"
               "Time without: %0.2f ms \n"
               "Speed-up: %0.2fx\n",
               times[1] * 1000,
               times[0] * 1000,
               speed_up);
        if (speed_up < 0.5) {
            printf("The nested vectorization schedule was supposed to be faster!\n");
            return 1;
        }
    }

    // 8-bit blur into 32-bit accumulator
    {

        double times[2];

        for (int use_nested_vectorization = 0; use_nested_vectorization < 2; use_nested_vectorization++) {
            Var x, y;

            ImageParam f(UInt(8), 1), g(UInt(8), 1);

            RDom r(0, 128);
            Func prod;
            prod(x) += cast<int32_t>(f(x + r)) * g(r);

            Func result;
            result(x) = cast<uint8_t>(prod(x) >> 24);

            RVar ro, ri;

            f.in().compute_at(prod, ro).vectorize(_0).bound_extent(_0, 16);
            g.in().compute_at(prod, ro).vectorize(_0);

            result
                .vectorize(x, 8, TailStrategy::RoundUp);

            if (use_nested_vectorization) {

                int reduce;
                if (target.arch == Target::X86) {
                    reduce = 8;
                } else if (target.has_feature(Target::ARMDotProd)) {
                    reduce = 4;
                } else {
                    reduce = 2;
                }

                prod.compute_at(result, x)
                    .vectorize(x)
                    .update()
                    .split(r, ro, ri, 8)
                    .reorder(ri, x, ro)
                    .vectorize(x)
                    .atomic()
                    .vectorize(ri, reduce)
                    .unroll(ri);
            } else {
                prod.compute_at(result, x)
                    .vectorize(x)
                    .update()
                    .split(r, ro, ri, 8)
                    .reorder(ri, x, ro)
                    .vectorize(x)
                    .unroll(ri);
            }

            Buffer<uint8_t> f_buf(1024 * 1024);
            f_buf.fill(100);
            Buffer<uint8_t> g_buf(128);
            f_buf.fill(100);
            f.set(f_buf);
            g.set(g_buf);
            Buffer<uint8_t> out(f_buf.width() - g_buf.width() - 128);

            // Uncomment to check the asm
            // result.compile_to_assembly("/dev/stdout", {f, g}, target);

            times[use_nested_vectorization] =
                Tools::benchmark(10, 10, [&]() {
                    result.realize(out, target);
                    out.device_sync();
                });
        }

        double speed_up = times[0] / times[1];
        printf("8-bit blur\n"
               "Time with nested vectorization: %0.2f ms \n"
               "Time without: %0.2f ms \n"
               "Speed-up: %0.2fx\n",
               times[1] * 1000,
               times[0] * 1000,
               speed_up);
        if (speed_up < 0.5) {
            printf("The nested vectorization schedule was supposed to be faster!\n");
            return 1;
        }
    }

    // 16-bit blur into 32-bit accumulator, with reduction over
    // adjacent vector lanes at the same time as reduction over slices
    // of the vector. This is only a win on platforms with a pmaddwd-like instruction.
    if (target.arch == Target::X86) {

        double times[2];

        for (int use_nested_vectorization = 0; use_nested_vectorization < 2; use_nested_vectorization++) {
            Var x, y;

            ImageParam f(Int(16), 1), g(Int(16), 1);

            RDom r(0, 128);
            Func prod;
            prod(x) += cast<int32_t>(f(x + r)) * g(r);

            Func result;
            result(x) = cast<int16_t>(prod(x) >> 16);

            RVar ro, ri, rio, rii;

            result
                .vectorize(x, 16, TailStrategy::RoundUp);

            if (use_nested_vectorization) {
                f.in().compute_at(prod, ro).vectorize(_0).bound_extent(_0, 32);

                // It's faster to compute this at rio and unroll rio,
                // but that's not what we're testing.
                g.in().compute_at(prod, ro).vectorize(_0);

                prod.compute_at(result, x)
                    .vectorize(x)
                    .update()
                    .split(r, ro, ri, 4)
                    .split(ri, rio, rii, 2)
                    .reorder(rii, x, rio, ro)
                    .vectorize(x)
                    .atomic()
                    .vectorize(rio)
                    .vectorize(rii);
            } else {
                prod.compute_at(result, x)
                    .vectorize(x)
                    .update()
                    .split(r, ro, ri, 4)
                    .reorder(ri, x, ro)
                    .vectorize(x)
                    .unroll(ri);
            }

            Buffer<int16_t> f_buf(1024 * 1024);
            f_buf.fill(100);
            Buffer<int16_t> g_buf(128);
            f_buf.fill(100);
            f.set(f_buf);
            g.set(g_buf);
            Buffer<int16_t> out(f_buf.width() - g_buf.width() - 128);

            // Uncomment to check the asm
            // result.compile_to_assembly("/dev/stdout", {f, g}, target);

            times[use_nested_vectorization] =
                Tools::benchmark(10, 10, [&]() {
                    result.realize(out, target);
                    out.device_sync();
                });
        }

        double speed_up = times[0] / times[1];
        printf("16-bit blur with reduction dimension outermost vector dim\n"
               "Time with nested vectorization: %0.2f ms \n"
               "Time without: %0.2f ms \n"
               "Speed-up: %0.2fx\n",
               times[1] * 1000,
               times[0] * 1000,
               speed_up);
        if (speed_up < 0.5) {
            printf("The nested vectorization schedule was supposed to be faster!\n");
            return 1;
        }
    }
    printf("Success!\n");

    // 8-bit sparse blur into 32-bit accumulator
    {

        double times[2];

        for (int use_nested_vectorization = 0; use_nested_vectorization < 2; use_nested_vectorization++) {
            Var x, y;

            ImageParam f(UInt(8), 1), g(UInt(8), 1);

            // 128 filter taps at unknown locations, which we will
            // promise are bounded.
            ImageParam taps(Int(32), 1);

            RDom r(0, 128);
            Func prod;
            prod(x) += cast<uint32_t>(f(x + unsafe_promise_clamped(taps(r), 0, 127))) * g(r);

            Func result;
            result(x) = prod(x);

            RVar ro, ri;

            g.in().compute_at(prod, ro).vectorize(_0);

            result
                .vectorize(x, 8, TailStrategy::RoundUp);

            if (use_nested_vectorization) {

                int reduce;
                if (target.has_feature(Target::ARMDotProd)) {
                    reduce = 4;
                } else {
                    reduce = 2;
                }

                prod.compute_at(result, x)
                    .vectorize(x)
                    .update()
                    .split(r, ro, ri, 16)
                    .reorder(ri, x, ro)
                    .vectorize(x)
                    .atomic()
                    .vectorize(ri, reduce)
                    .unroll(ri);
            } else {
                prod.compute_at(result, x)
                    .vectorize(x)
                    .update()
                    .split(r, ro, ri, 16)
                    .reorder(ri, x, ro)
                    .vectorize(x);
            }

            Buffer<uint8_t> f_buf(1024 * 1024);
            f_buf.fill(100);
            Buffer<uint8_t> g_buf(128);
            f_buf.fill(100);
            f.set(f_buf);
            g.set(g_buf);
            Buffer<int> taps_buf(128);
            for (int i = 0; i < 128; i++) {
                taps_buf(i) = (i * i) & 127;
            }
            taps.set(taps_buf);
            Buffer<uint32_t> out(f_buf.width() - g_buf.width() - 128);

            // Uncomment to check the asm
            // result.compile_to_assembly("/dev/stdout", {f, g, taps}, target);

            times[use_nested_vectorization] =
                Tools::benchmark(10, 10, [&]() {
                    result.realize(out, target);
                    out.device_sync();
                });
        }

        // We don't actually get any win from this on X86, as the
        // basic version also manages to use pmaddwd well.
        double speed_up = times[0] / times[1];
        printf("8-bit sparse blur\n"
               "Time with nested vectorization: %0.2f ms \n"
               "Time without: %0.2f ms \n"
               "Speed-up: %0.2fx\n",
               times[1] * 1000,
               times[0] * 1000,
               speed_up);
        if (speed_up < 0.5) {
            printf("The nested vectorization schedule was supposed to be faster!\n");
            return 1;
        }
    }

    return 0;
}
