#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

double run_test(bool auto_schedule) {
    int size = 1024;
    Buffer<float> A(size, size);
    Buffer<float> B(size, size);
    Buffer<float> C(size, size);

    for (int y = 0; y < A.height(); y++) {
        for (int x = 0; x < A.width(); x++) {
            A(x, y) = rand() & 0xfff;
        }
    }

    for (int y = 0; y < B.height(); y++) {
        for (int x = 0; x < B.width(); x++) {
            B(x, y) = rand() & 0xfff;
        }
    }

    Var x("x"), y("y");

    Func prod("prod");
    RDom r(0, size);

    prod(x, y) = 0.0f;
    prod(x, y) += A(x, r.x) * B(r.x, y);

    Func out;
    out(x, y) = prod(x, y);

    Target target = get_jit_target_from_environment();
    Pipeline p(out);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        out.set_estimate(x, 0, size).set_estimate(y, 0, size);
        // Auto-schedule the pipeline
        p.apply_autoscheduler(target, {"Mullapudi2016"});
    } else if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi"), xii("xii"), yii("yii"), xt("xt"), yt("yt");
        out.tile(x, y, xi, yi, 8, 8).unroll(xi).unroll(yi).gpu_tile(x, y, xt, yt, 8, 8);
        prod.compute_at(out, xt).update().reorder(x, y, r.x);

        // This schedule as-is is terrible - 518ms

        // Not unrolled, a mat mul does 2 loads per
        // multiply-add. We unroll an 8x8 block so that the common
        // loads will be shared. This means we do 16 loads for 64
        // multiply adds, which is a huge win.

        // prod.update().unroll(x).unroll(y);
        // 53ms

        // We then also use Z-order within each 8x8 unrolled block
        // to minimize register pressure and avoid the big hit of
        // 8 high-latency loads up-front. This is surprisingly
        // effective.
        //
        // prod.update().tile(x, y, xi, yi, 2, 2).unroll(xi).unroll(yi)
        //    .tile(x, y, xii, yii, 2, 2).unroll(xii).unroll(yii)
        //    .unroll(x).unroll(y);
        // 46ms

        // We also vectorize the innermost pair of float loads so
        // that we use 64-bit memory accesses to A instead of 32-bit.

        Var t;
        prod.update()
            .tile(x, y, xi, yi, 2, 2)
            .vectorize(xi)
            .unroll(yi)
            .tile(x, y, xii, yii, 2, 2)
            .unroll(xii)
            .unroll(yii)
            .unroll(x)
            .unroll(y);

        // 36ms

        // Still not as fast as apps/linear_algebra on the CPU on
        // the same machine (28ms). There are probably way more
        // tricks a good CUDA programmer can pull out
        // here. Counting a multiply-add as two ops, this is 477
        // GFlops on a card that's supposed to be capable of
        // 1728. In terms of memory bandwidth we're doing 16 loads
        // in the inner loop, which executes 2048*2048*2048 /
        // (8*8) times, which is 238 GB/s on a card that
        // advertises 86.4. So I guess the cache is working.

        // If we assume perfect cache hits for threads in a block,
        // then each thread block handles a 64x64 tile of output,
        // so it touches 64*2048 values from each matrix, which is
        // 64*2048*4*2 bytes. There are (2048*2048)/(64*64) total
        // blocks, so the total number of bytes loaded with
        // perfect caching per block is
        // 2048*2048*2048*64*4*2/(64*64), which implies 29.8
        // GB/s. So we're getting good but not great caching.
    } else {
        Var xi, yi, xii, yii;
        // Tile the output domain
        prod.compute_at(out, x).vectorize(x);
        prod.update().reorder(x, y, r).vectorize(x).unroll(y);
        out.tile(x, y, xi, yi, 16, 4).vectorize(xi).unroll(yi).parallel(y);
        // Inspect the schedule (only for debugging))
        // out.print_loop_nest();
    }

    // Inspect the schedule (only for debugging))
    // out.print_loop_nest();

    // Benchmark the schedule
    Buffer<float> result(size, size);
    double t = benchmark(3, 10, [&]() {
        p.realize(result);
    });

    return t * 1000;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Autoschedulers do not support WebAssembly.\n");
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }

    load_plugin(argv[1]);

    double manual_time = run_test(false);
    double auto_time = run_test(true);

    const double slowdown_factor = 8.0;
    if (!get_jit_target_from_environment().has_gpu_feature() && auto_time > manual_time * slowdown_factor) {
        std::cerr << "Autoscheduler time is slower than expected:\n"
                  << "======================\n"
                  << "Manual time: " << manual_time << "ms\n"
                  << "Auto time: " << auto_time << "ms\n"
                  << "======================\n";
        exit(1);
    }

    printf("Success!\n");
    return 0;
}
