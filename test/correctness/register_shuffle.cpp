#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();

    int cap = t.get_cuda_capability_lower_bound();
    if (cap < 50) {
        printf("[SKIP] CUDA with capability greater than or equal to 5.0 required, cap:%d\n", cap);
        return 0;
    }

    {
        // Shuffle test to do a small convolution
        Func f, g;
        Var x, y;

        f(x, y) = cast<uint8_t>(x + y);
        g(x, y) = f(x - 1, y) + f(x + 1, y);

        Var xo, xi, yi, yo;
        g
            .gpu_tile(x, y, xi, yi, 32, 2, TailStrategy::RoundUp)
            .gpu_lanes(xi);

        f.compute_root();

        f
            .in(g)
            .compute_at(g, yi)
            .split(x, xo, xi, 32, TailStrategy::RoundUp)
            .gpu_lanes(xi)
            .unroll(xo);

        Buffer<uint8_t> out = g.realize({32, 4});
        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                uint8_t correct = 2 * (x + y);
                uint8_t actual = out(x, y);
                if (correct != actual) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // Broadcast test - an outer product access pattern
        Func a, b, c;
        Var x, y;
        a(x) = cast<float>(x);
        b(y) = cast<float>(y);
        c(x, y) = a(x) + 100 * b(y);

        a.compute_root();
        b.compute_root();

        Var xi, yi, yii;

        c
            .tile(x, y, xi, yi, 32, 32, TailStrategy::RoundUp)
            .gpu_blocks(x, y)
            .gpu_lanes(xi);
        // We're going to be computing 'a' and 'b' at block level, but
        // we want them in register, not shared, so we explicitly call
        // store_in.
        a
            .in(c)
            .compute_at(c, x)
            .gpu_lanes(x)
            .store_in(MemoryType::Register);
        b
            .in(c)
            .compute_at(c, x)
            .gpu_lanes(y)
            .store_in(MemoryType::Register);

        Buffer<float> out = c.realize({32, 32});
        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                float correct = x + 100 * y;
                float actual = out(x, y);
                // The floats are small integers, so they should be exact.
                if (correct != actual) {
                    printf("out(%d, %d) = %f instead of %f\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // Vectorized broadcast test. Each lane is responsible for a
        // 2-vector from 'a' and a 2-vector from 'b' instead of a single
        // value.
        Func a, b, c;
        Var x, y;
        a(x) = cast<float>(x);
        b(y) = cast<float>(y);
        c(x, y) = a(x) + 100 * b(y);

        a.compute_root();
        b.compute_root();

        Var xi, yi, yii;

        c
            .tile(x, y, xi, yi, 64, 64, TailStrategy::RoundUp)
            .gpu_blocks(x, y)
            .split(yi, yi, yii, 64)
            .unroll(yii, 2)
            .gpu_threads(yi)
            .vectorize(xi, 2)
            .gpu_lanes(xi);
        a
            .in(c)
            .compute_at(c, yi)
            .vectorize(x, 2)
            .gpu_lanes(x);
        b
            .in(c)
            .compute_at(c, yi)
            .vectorize(y, 2)
            .gpu_lanes(y);

        Buffer<float> out = c.realize({64, 64});
        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                float correct = x + 100 * y;
                float actual = out(x, y);
                // The floats are small integers, so they should be exact.
                if (correct != actual) {
                    printf("out(%d, %d) = %f instead of %f\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // A stencil chain where many of the lanes will be masked
        Func a, b, c, d;
        Var x, y;

        a(x, y) = x + y;
        a.compute_root();

        b(x, y) = a(x - 1, y) + a(x, y) + a(x + 1, y);
        c(x, y) = b(x - 1, y) + b(x, y) + b(x + 1, y);
        d(x, y) = c(x - 1, y) + c(x, y) + c(x + 1, y);

        Var xi, yi;
        // Compute 24-wide pieces of output per block. Should use 32
        // warp lanes to do so. The footprint on the input is 30, so
        // the last two lanes are always inactive. 26-wide blocks
        // would be a more efficient use of the gpu, but a less
        // interesting test.
        d
            .gpu_tile(x, y, xi, yi, 24, 2)
            .gpu_lanes(xi);
        for (Func stage : {a.in(), b, c}) {
            stage
                .compute_at(d, yi)
                .gpu_lanes(x);
        }

        Buffer<int> out = d.realize({24, 2});
        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                int correct = 27 * (x + y);
                int actual = out(x, y);
                if (correct != actual) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // Same as above, but in half-warps
        Func a, b, c, d;
        Var x, y;

        a(x, y) = x + y;
        a.compute_root();

        b(x, y) = a(x - 1, y) + a(x, y) + a(x + 1, y);
        c(x, y) = b(x - 1, y) + b(x, y) + b(x + 1, y);
        d(x, y) = c(x - 1, y) + c(x, y) + c(x + 1, y);

        Var xi, yi;
        // Compute 10-wide pieces of output per block. Should use 16
        // warp lanes to do so.
        d
            .gpu_tile(x, y, xi, yi, 10, 2)
            .gpu_lanes(xi);
        for (Func stage : {a.in(), b, c}) {
            stage
                .compute_at(d, yi)
                .gpu_lanes(x);
        }

        Buffer<int> out = d.realize({24, 2});
        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                int correct = 27 * (x + y);
                int actual = out(x, y);
                if (correct != actual) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // A shuffle with a shift amount that depends on the y coord
        Func a, b;
        Var x, y;

        a(x, y) = x + y;
        b(x, y) = a(x + y, y);

        Var xi, yi;
        b
            .gpu_tile(x, y, xi, yi, 16, 8, TailStrategy::RoundUp)
            .gpu_lanes(xi);
        a
            .compute_at(b, yi)
            .gpu_lanes(x);

        Buffer<int> out = b.realize({32, 32});
        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                int correct = x + 2 * y;
                int actual = out(x, y);
                if (correct != actual) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // Bilinear upsample
        Func f, upx, upy;
        Var x, y;

        f(x, y) = cast<float>(x + y);
        f.compute_root();

        upx(x, y) = 0.25f * f((x / 2) - 1 + 2 * (x % 2), y) + 0.75f * f(x / 2, y);
        upy(x, y) = 0.25f * upx(x, (y / 2) - 1 + 2 * (y % 2)) + 0.75f * upx(x, y / 2);

        // Compute 128x64 tiles of output, which require 66x34 tiles
        // of input. All intermediate data stored in lanes and
        // accessed using register shuffles.

        Var xi, yi, xii, yii;
        upy
            .tile(x, y, xi, yi, 128, 64, TailStrategy::RoundUp)
            .tile(xi, yi, xii, yii, 4, 8)
            .vectorize(xii)
            .gpu_blocks(x, y)
            .gpu_threads(yi)
            .gpu_lanes(xi);

        upx
            .compute_at(upy, yi)
            .unroll(x, 4)
            .gpu_lanes(x)
            .unroll(y);

        // Stage the input into lanes, doing two dense vector loads
        // per lane, and use register shuffles to do the upsample in x.
        f
            .in()
            .compute_at(upy, yi)
            .align_storage(x, 64)
            .vectorize(x, 2, TailStrategy::RoundUp)
            .split(x, x, xi, 32, TailStrategy::GuardWithIf)
            .reorder(xi, y, x)
            .gpu_lanes(xi)
            .unroll(x)
            .unroll(y);

        upy
            .output_buffer()
            .dim(0)
            .set_min(0)
            .dim(1)
            .set_min(0);
        Buffer<float> out = upy.realize({128, 128});

        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                float actual = out(x, y);
                float correct = (x + y - 1) / 2.0f;
                if (correct != actual) {
                    printf("out(%d, %d) = %f instead of %f\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // Box-downsample by a factor of 8 using summation within each
        // warp.
        Func f;
        Var x, y;
        f(x, y) = cast<float>(x + y);
        f.compute_root();

        Func s1, s2, s3, s4;

        s1(x, y) = f(2 * x, y) + f(2 * x + 1, y);
        s2(x, y) = s1(2 * x, y) + s1(2 * x + 1, y);
        s3(x, y) = s2(2 * x, y) + s2(2 * x + 1, y);
        s4(x, y) = s3(x, y);

        Var xi, yi;
        s4
            .gpu_tile(x, y, xi, yi, 64, 1, TailStrategy::RoundUp)
            .vectorize(xi, 2)
            .gpu_lanes(xi);
        s3
            .compute_at(s4, yi)
            .split(x, x, xi, 32, TailStrategy::RoundUp)
            .gpu_lanes(xi)
            .unroll(x);
        s2
            .compute_at(s4, yi)
            .split(x, x, xi, 32, TailStrategy::RoundUp)
            .gpu_lanes(xi)
            .unroll(x);
        s1
            .compute_at(s4, yi)
            .split(x, x, xi, 32, TailStrategy::RoundUp)
            .gpu_lanes(xi)
            .unroll(x);
        f
            .in()
            .compute_at(s4, yi)
            .split(x, x, xi, 64, TailStrategy::RoundUp)
            .vectorize(xi, 2)
            .gpu_lanes(xi)
            .unroll(x);

        Buffer<float> out = s4.realize({64, 64});

        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                float actual = out(x, y);
                // One factor of 8 from adding instead of averaging,
                // and another factor of 8 from the compression of the
                // coordinate system across x.
                float correct = (x * 8 + y) * 8 + 28;
                if (correct != actual) {
                    printf("out(%d, %d) = %f instead of %f\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // The same, with a narrower tile in x so that one warp is divided up across many scanlines.
        Func f;
        Var x, y;
        f(x, y) = cast<float>(x + y);
        f.compute_root();

        Func s1, s2, s3, s4;

        s1(x, y) = f(2 * x, y) + f(2 * x + 1, y);
        s2(x, y) = s1(2 * x, y) + s1(2 * x + 1, y);
        s3(x, y) = s2(2 * x, y) + s2(2 * x + 1, y);
        s4(x, y) = s3(x, y);

        Var xi, yi;
        s4
            .gpu_tile(x, y, xi, yi, 8, 16, TailStrategy::RoundUp)
            .vectorize(xi, 2)
            .gpu_lanes(xi);
        s3
            .compute_at(s4, yi)
            .split(x, x, xi, 4, TailStrategy::RoundUp)
            .gpu_lanes(xi)
            .unroll(x);
        s2
            .compute_at(s4, yi)
            .split(x, x, xi, 4, TailStrategy::RoundUp)
            .gpu_lanes(xi)
            .unroll(x);
        s1
            .compute_at(s4, yi)
            .split(x, x, xi, 4, TailStrategy::RoundUp)
            .gpu_lanes(xi)
            .unroll(x);
        f
            .in()
            .compute_at(s4, yi)
            .split(x, x, xi, 8, TailStrategy::RoundUp)
            .vectorize(xi, 2)
            .gpu_lanes(xi)
            .unroll(x);

        Buffer<float> out = s4.realize({32, 32});

        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                float actual = out(x, y);
                float correct = (x * 8 + y) * 8 + 28;
                if (correct != actual) {
                    printf("out(%d, %d) = %f instead of %f\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        Buffer<uint8_t> buf(256, 256);
        buf.for_each_value([](uint8_t &x) {
            x = rand();
        });
        buf.set_host_dirty();

        // Store a small LUT in-register, populated at the warp
        // level.
        Func lut;
        Var x, y;
        lut(x) = cast<uint16_t>(x) + 1;

        Func curved;
        curved(x, y) = lut(buf(x, y));

        Var xi, yi, xo;
        curved
            .compute_root()
            .tile(x, y, xi, yi, 32, 32)
            .gpu_blocks(x, y)
            .gpu_threads(yi)
            .gpu_lanes(xi);

        lut.compute_root();

        // Load the LUT into shared at the start of each block using warp 0.
        lut
            .in()
            .compute_at(curved, x)
            .split(x, xo, xi, 32 * 4)
            .vectorize(xi, 4)
            .gpu_lanes(xi)
            .unroll(xo);

        // Load it from shared into registers for each warp.
        lut
            .in()
            .in()
            .compute_at(curved, yi)
            .split(x, xo, xi, 32 * 4)
            .vectorize(xi, 4)
            .gpu_lanes(xi)
            .unroll(xo);

        Buffer<uint16_t> out = curved.realize({buf.width(), buf.height()});

        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                uint16_t actual = out(x, y);
                uint16_t correct = ((uint16_t)buf(x, y)) + 1;
                if (correct != actual) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    {
        // Use warp shuffle to do the reduction.
        Func a, b, c;
        Var x, y, yo, yi, ylane, u;
        RVar ro, ri;

        a(x, y) = x + y;
        a.compute_root();

        RDom r(0, 1024);
        b(y) = 0;
        b(y) += a(r, y);
        c(y) = b(y);

        int warp = 8;
        c
            .split(y, yo, yi, 1 * warp)
            .split(yi, yi, ylane, 1)
            .gpu_blocks(yo)
            .gpu_threads(yi, ylane);
        Func intm = b.update()
                        .split(r, ri, ro, warp)
                        .reorder(ri, ro)
                        .rfactor(ro, u);
        intm
            .compute_at(c, yi)
            .update()
            .gpu_lanes(u);
        intm
            .gpu_lanes(u);

        Buffer<int> out = c.realize({256});
        for (int y = 0; y < out.width(); y++) {
            int correct = 0;
            for (int x = 0; x < 1024; x++) {
                correct += x + y;
            }
            int actual = out(y);
            if (correct != actual) {
                printf("out(%d) = %d instead of %d\n",
                       y, actual, correct);
                return -1;
            }
        }
    }

    {
        // Test a case that caused combinatorial explosion
        Var x;
        Expr e = x;
        for (int i = 0; i < 10; i++) {
            e = fast_pow(e, e + 1);
        }

        Func f;
        f(x) = e;

        Var xo, xi;
        f.gpu_tile(x, xo, xi, 32);
        f.realize({1024});
    }

    printf("Success!\n");
    return 0;
}
