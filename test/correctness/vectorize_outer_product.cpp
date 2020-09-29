#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {
    {
        Func f, g;
        Var x, y;

        f(x) = cast<uint8_t>(sin(x));
        g(x) = cast<uint8_t>(sqrt(x));

        Func prod;
        prod(x, y) = cast<int32_t>(f(x)) * g(y);

        Var xi, yi;
        f.compute_root();
        g.compute_root();

        f.in().compute_at(prod.in(), x).vectorize(x);
        g.in().compute_at(prod.in(), x).vectorize(x);

        prod.in().tile(x, y, xi, yi, 8, 8, TailStrategy::RoundUp).vectorize(xi).unroll(yi);

        prod.compute_at(prod.in(), x).vectorize(x).vectorize(y);
    }

    // Now do a mat-mul
    {
        Func f, g;
        Var x, y;

        f(x, y) = sin(x + y);
        g(x, y) = sqrt(x + y);

        RDom r(0, 128);

        Func prod;
        prod(x, y) += f(x, r) * g(r, y);

        Var xi, yi;
        Var bx, tx, by, ty;
        RVar ro, ri;

        f.compute_root();
        g.compute_root();

        f.in().compute_at(prod, ro).vectorize(x).unroll(y).reorder_storage(y, x);
        g.in().compute_at(prod, ro).vectorize(x).vectorize(y);

        prod.in().tile(x, y, xi, yi, 4, 4, TailStrategy::RoundUp).vectorize(xi).unroll(yi);

        prod.compute_at(prod.in(), x)
            .vectorize(x)
            .vectorize(y)
            .update()
            .split(r, ro, ri, 4)
            .reorder(ri, x, y, ro)
            .vectorize(x)
            .vectorize(y)
            .atomic()
            .vectorize(ri);

        //prod.in().compile_to_assembly("/dev/stdout", {}, Target("host-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
        //prod.in().compile_to_assembly("/dev/stdout", {}, Target("host-cuda-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
        prod.in().compile_to_assembly("/dev/stdout", {}, Target("arm-64-linux-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
    }

    // 8-bit mat-mul into 32-bit accumulator
    {
        Func f, g;
        Var x, y;

        f(x, y) = cast<uint8_t>(sin(x + y));
        g(x, y) = cast<uint8_t>(sqrt(x + y));

        RDom r(0, 128);

        Func prod;
        prod(x, y) += cast<int32_t>(f(x, r)) * g(r, y);

        Var xi, yi, xo, yo;
        Var bx, tx, by, ty;
        RVar ro, ri, rio, rii;

        f.compute_root();
        g.compute_root();

        f.in().compute_at(prod, ro).vectorize(x).unroll(y);
        g.in().compute_at(prod, ro).vectorize(x).vectorize(y);

        prod.in()
            .tile(x, y, xi, yi, 8, 8, TailStrategy::RoundUp)
            .vectorize(xi)
            .unroll(yi);

        prod.compute_at(prod.in(), x)
            .vectorize(x)
            .vectorize(y)
            .update()
            .split(r, ro, ri, 4)
            .reorder(ri, x, y, ro)
            .vectorize(x)
            .vectorize(y)
            .atomic()
            .vectorize(ri);

        //prod.in().compile_to_assembly("/dev/stdout", {}, Target("host-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
        //prod.in().compile_to_assembly("/dev/stdout", {}, Target("host-cuda-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
        prod.in().compile_to_assembly("/dev/stdout", {}, Target("arm-64-arm_dot_prod-linux-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
    }

    // 8-bit blur into 32-bit accumulator
    {
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
                prod.compute_at(result, x)
                    .vectorize(x)
                    .update()
                    .split(r, ro, ri, 8)
                    .reorder(ri, x, ro)
                    .vectorize(x)
                    .atomic()
                    .vectorize(ri, 8)  // Use 8 for x86, 4 for arm
                    .unroll(ri);
            } else {
                prod.compute_at(result, x)
                    .vectorize(x)
                    .update()
                    .split(r, ro, ri, 4)
                    .reorder(ri, x, ro)
                    .vectorize(x);
            }

            //result.compile_to_assembly("/dev/stdout", {f, g}, Target("arm-64-linux-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
            //result.compile_to_assembly("/dev/stdout", {f, g}, Target("x86-64-linux-sse41-avx-avx2-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));

            Buffer<uint8_t> f_buf(1024 * 1024 * 10);
            f_buf.fill(100);
            Buffer<uint8_t> g_buf(128);
            f_buf.fill(100);
            f.set(f_buf);
            g.set(g_buf);
            Buffer<uint8_t> out(f_buf.width() - g_buf.width() - 128);

            double t1 = Tools::benchmark(3, 3, [&]() { result.realize(out); });
            printf("TIME %d: %f\n", use_nested_vectorization, t1);
        }
    }

    /*
    // 8-bit blur into 32-bit accumulator
    {
        Func f, g;
        Var x, y;

        f(x) = cast<uint8_t>(sin(x));
        g(x) = cast<uint8_t>(sqrt(x));

        RDom r(0, 128);

        Func prod;
        prod(x) += cast<uint32_t>(cast<uint16_t>(f(x + r)) * g(r));

        Func result;
        result(x) = cast<uint8_t>(prod(x) >> 24);

        Var xi, yi, xo, yo;
        Var bx, tx, by, ty;
        RVar ro, ri, rio, rii;

        f.compute_root();
        g.compute_root();

        f.in().compute_at(prod, ro).vectorize(x).bound_extent(x, 16);
        g.in().compute_at(prod, ro).vectorize(x);

        result
            .vectorize(x, 8, TailStrategy::RoundUp);

        prod.compute_at(result, x)
            .vectorize(x)
            .update()
            .split(r, ro, ri, 8)
            .reorder(ri, x, ro)
            .vectorize(x)
            .unroll(ri);

        result.compile_to_assembly("/dev/stdout", {}, Target("arm-64-arm_dot_prod-linux-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
        result.compile_to_assembly("/dev/stdout", {}, Target("x86-64-linux-sse41-avx-avx2-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"));
    }
    */

    return 0;
}
