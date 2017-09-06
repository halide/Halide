//
// Code based on apps/interpoloate/interpolate.cpp
//
#include <iostream>
#include <limits>

#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

using std::vector;

int run_test(bool auto_schedule, int argc, char **argv) {
    int W = 1536;
    int H = 2560;

    Buffer<float> in(W, H, 4);

    // 8-bit RGBA image (per apps/images/rgba.png)
    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            for (int c = 0; c < 4; c++) {
                in(x, y, c) = rand() & 0xff;
            }
        }
    }

    const int levels = 10;

    Func downsampled[levels];
    Func downx[levels];
    Func interpolated[levels];
    Func upsampled[levels];
    Func upsampledx[levels];
    Var x("x"), y("y"), c("c");

    Func clamped = BoundaryConditions::repeat_edge(in);

    // This triggers a bug in llvm 3.3 (3.2 and trunk are fine), so we
    // rewrite it in a way that doesn't trigger the bug. The rewritten
    // form assumes the in alpha is zero or one.
    // downsampled[0](x, y, c) = select(c < 3, clamped(x, y, c) * clamped(x, y, 3), clamped(x, y, 3));
    downsampled[0](x, y, c) = clamped(x, y, c) * clamped(x, y, 3);

    for (int l = 1; l < levels; ++l) {
        Func prev = downsampled[l-1];

        if (l == 4) {
            // Also add a boundary condition at a middle pyramid level
            // to prevent the footprint of the downsamplings to extend
            // too far off the base image. Otherwise we look 512
            // pixels off each edge.
            Expr w = in.width()/(1 << l);
            Expr h = in.height()/(1 << l);
            prev = lambda(x, y, c, prev(clamp(x, 0, w), clamp(y, 0, h), c));
        }

        downx[l](x, y, c) = (prev(x*2-1, y, c) +
                             2.0f * prev(x*2, y, c) +
                             prev(x*2+1, y, c)) * 0.25f;
        downsampled[l](x, y, c) = (downx[l](x, y*2-1, c) +
                                   2.0f * downx[l](x, y*2, c) +
                                   downx[l](x, y*2+1, c)) * 0.25f;
    }
    interpolated[levels-1](x, y, c) = downsampled[levels-1](x, y, c);
    for (int l = levels-2; l >= 0; --l) {
        upsampledx[l](x, y, c) = (interpolated[l+1](x/2, y, c) +
                                  interpolated[l+1]((x+1)/2, y, c)) / 2.0f;
        upsampled[l](x, y, c) =  (upsampledx[l](x, y/2, c) +
                                  upsampledx[l](x, (y+1)/2, c)) / 2.0f;
        interpolated[l](x, y, c) = downsampled[l](x, y, c) + (1.0f - downsampled[l](x, y, 3)) * upsampled[l](x, y, c);
    }

    Func normalize("normalize");
    normalize(x, y, c) = interpolated[0](x, y, c) / interpolated[0](x, y, 3);
    normalize
        .estimate(c, 0, 4)
        .estimate(x, 0, in.width())
        .estimate(y, 0, in.height());

    std::cout << "Finished function setup." << std::endl;

    Pipeline p(normalize);

    Target target = get_target_from_environment();

    if (!auto_schedule) {
        int sched;
        if (target.has_gpu_feature()) {
            sched = 4;
        } else {
            sched = 2;
        }

        switch (sched) {
        case 0:
        {
            std::cout << "Flat schedule." << std::endl;
            for (int l = 0; l < levels; ++l) {
                downsampled[l].compute_root();
                interpolated[l].compute_root();
            }
            normalize.compute_root();
            break;
        }
        case 1:
        {
            std::cout << "Flat schedule with vectorization." << std::endl;
            for (int l = 0; l < levels; ++l) {
                downsampled[l].compute_root().vectorize(x,4);
                interpolated[l].compute_root().vectorize(x,4);
            }
            normalize.compute_root();
            break;
        }
        case 2:
        {
            Var xi, yi;
            std::cout << "Flat schedule with parallelization + vectorization." << std::endl;
            for (int l = 1; l < levels-1; ++l) {
                downsampled[l]
                    .compute_root()
                    .parallel(y, 8)
                    .vectorize(x, 4);
                interpolated[l]
                    .compute_root()
                    .parallel(y, 8)
                    .unroll(x, 2)
                    .unroll(y, 2)
                    .vectorize(x, 8);
            }
            normalize
                .reorder(c, x, y)
                .bound(c, 0, 3)
                .unroll(c)
                .tile(x, y, xi, yi, 2, 2)
                .unroll(xi)
                .unroll(yi)
                .parallel(y, 8)
                .vectorize(x, 8)
                .bound(x, 0, in.width())
                .bound(y, 0, in.height());
            break;
        }
        case 3:
        {
            std::cout << "Flat schedule with vectorization sometimes." << std::endl;
            for (int l = 0; l < levels; ++l) {
                if (l + 4 < levels) {
                    Var yo,yi;
                    downsampled[l].compute_root().vectorize(x,4);
                    interpolated[l].compute_root().vectorize(x,4);
                } else {
                    downsampled[l].compute_root();
                    interpolated[l].compute_root();
                }
            }
            normalize.compute_root();
            break;
        }
        case 4:
        {
            std::cout << "GPU schedule." << std::endl;

            // Some gpus don't have enough memory to process the entire
            // image, so we process the image in tiles.
            Var yo, yi, xo, xi, ci;

            // We can't compute the entire output stage at once on the GPU
            // - it takes too much GPU memory on some of our build bots,
            // so we wrap the final stage in a CPU stage.
            Func cpu_wrapper = normalize.in();

            cpu_wrapper
                .reorder(c, x, y)
                .bound(c, 0, 3)
                .tile(x, y, xo, yo, xi, yi, in.width()/4, in.height()/4)
                .vectorize(xi, 8);

            normalize
                .compute_at(cpu_wrapper, xo)
                .reorder(c, x, y)
                .gpu_tile(x, y, xi, yi, 16, 16)
                .unroll(c);

            // Start from level 1 to save memory - level zero will be computed on demand
            for (int l = 1; l < levels; ++l) {
                int tile_size = 32 >> l;
                if (tile_size < 1) tile_size = 1;
                if (tile_size > 8) tile_size = 8;
                downsampled[l]
                    .compute_root()
                    .gpu_tile(x, y, c, xi, yi, ci, tile_size, tile_size, 4);
                if (l == 1 || l == 4) {
                    interpolated[l]
                        .compute_at(cpu_wrapper, xo)
                        .gpu_tile(x, y, c, xi, yi, ci, 8, 8, 4);
                } else {
                    int parent = l > 4 ? 4 : 1;
                    interpolated[l]
                        .compute_at(interpolated[parent], x)
                        .gpu_threads(x, y, c);
                }
            }

            // The cpu wrapper is our new output Func
            normalize = cpu_wrapper;

            break;
        }
        default:
            assert(0 && "No schedule with this number.");
        }
    } else {
        p.auto_schedule(target);
    }

    // Inspect the schedule
    normalize.print_loop_nest();

    // JIT compile the pipeline eagerly, so we don't interfere with timing
    normalize.compile_jit(target);

    // Benchmark the schedule
    Buffer<float> out(in.width(), in.height(), 3);
    double t = benchmark(5, 50, [&]() {
        p.realize(out);
    });

    return t*1000;
}


int main(int argc, char **argv) {
    double manual_time = run_test(false, argc, argv);
    double auto_time = run_test(true, argc, argv);

    std::cout << "======================" << std::endl;
    std::cout << "Manual time: " << manual_time << "ms" << std::endl;
    std::cout << "Auto time: " << auto_time << "ms" << std::endl;
    std::cout << "======================" << std::endl;

    if (auto_time > manual_time * 2) {
        printf("Auto-scheduler is much much slower than it should be.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
