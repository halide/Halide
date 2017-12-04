#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

double run_test_1(bool auto_schedule) {
    /* THE ALGORITHM */

    // TODO: Replace the parameters with the meaningful constant names
    Buffer<float> data(128, 128, 64, 4);

    int pad = 1; // Padding required to handle boundaries

    Func f_in_bound("f_in_bound");
    f_in_bound = BoundaryConditions::repeat_edge(data, 0, 128, 0, 128);
    Buffer<float> W(3, 3, 64, 64), b(64);

    Var x("x"), y("y"), z("z"), n("n");

    Func f_conv("conv");
    RDom r(0, 3, 0, 3, 0, 64);

    f_conv(x, y, z, n) = b(z);

    f_conv(x, y, z, n) += W(r.x, r.y, r.z, z) * f_in_bound(x + r.x - pad, y + r.y - pad, r.z, n);

    Func f_ReLU("ReLU");
    f_ReLU(x, y, z, n) = max(0, f_conv(x, y, z, n));

    Target target = get_target_from_environment();
    Pipeline p(f_ReLU);

    if (!auto_schedule) {
        if (target.has_gpu_feature()) {
            Var ni, no, xi, xo, yi, yo, zi, zo;
            f_ReLU.compute_root()
                .split(x, xo, xi, 8)
                .split(y, yo, yi, 8)
                .split(z, zo, zi, 16)
                .reorder(xi, yi, zi, n, xo, yo, zo)
                .gpu_threads(xi, yi, zi)
                .gpu_blocks(xo, yo, zo);

            f_conv.compute_at(f_ReLU, n)
                .gpu_threads(x, y, z)
                .update()
                .unroll(r.x)
                .unroll(r.y)
                .gpu_threads(x, y, z);

            Var v0 = f_in_bound.args()[0];
            Var v1 = f_in_bound.args()[1];
            Var v2 = f_in_bound.args()[2];
            Var v0o, v0i, v1o, v1i, v2o, v2i;
            f_in_bound.compute_at(f_ReLU, n)
                .split(v0, v0o, v0i, 2)
                .split(v1, v1o, v1i, 2)
                .split(v2, v2o, v2i, 4)
                .reorder(v0i, v1i, v2i, v0o, v1o, v2o)
                .unroll(v0i)
                .unroll(v1i)
                .gpu_threads(v0o, v1o, v2o);
        } else {
            // Blocking spatially with vectorization
            Var z_t, y_t, par;
            int vec_len = 8;
            int o_block_size = 32;
            int y_block = 32;
            f_in_bound.compute_root().parallel(f_in_bound.args()[3]);
            f_conv.compute_root();
            f_conv.fuse(z, n, par).parallel(par);
            f_conv.update().reorder(x, y, r.z);
            f_conv.update().split(y, y, y_t, y_block);
            f_conv.update().split(z, z, z_t, o_block_size);
            f_conv.update().reorder(y_t, z_t, y, r.z, z);
            f_conv.update().vectorize(x, vec_len);
            f_conv.update().unroll(r.x);
            f_conv.update().unroll(r.y);
            f_conv.update().fuse(z, n, par).parallel(par);
            f_ReLU.reorder(n, z).parallel(z).vectorize(x, 8);
        }
    } else {
        // Specifying estimates
        f_ReLU.estimate(x, 0, 128)
            .estimate(y, 0, 128)
            .estimate(z, 0, 64)
            .estimate(n, 0, 4);

        // Auto-schedule the pipeline
        p.auto_schedule(target);
    }

    // Inspect the schedule
    //f_ReLU.print_loop_nest();

    // Run the schedule
    Buffer<float> out(128, 128, 64, 4);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t*1000;
}

double run_test_2(bool auto_schedule) {
    /* THE ALGORITHM */

    // TODO: Replace the parameters with the meaningful constant names
    Buffer<float> data(131, 131, 64, 4);
    Buffer<float> W(3, 3, 64, 64), b(64);

    Var x("x"), y("y"), z("z"), n("n");

    Func f_conv("conv");
    RDom r(0, 3, 0, 3, 0, 64);

    f_conv(x, y, z, n) = b(z);

    f_conv(x, y, z, n) += W(r.x, r.y, r.z, z) * data(x + r.x, y + r.y, r.z, n);

    Func f_ReLU("ReLU");
    f_ReLU(x, y, z, n) = max(0, f_conv(x, y, z, n));

    /* THE SCHEDULE */

    // Auto-schedule the pipeline
    Target target = get_target_from_environment();
    Pipeline p(f_ReLU);

    if (!auto_schedule) {
        if (target.has_gpu_feature()) {
            Var ni, no, xi, xo, yi, yo, zi, zo;
            f_ReLU.compute_root()
                .split(x, xo, xi, 8)
                .split(y, yo, yi, 8)
                .split(z, zo, zi, 16)
                .reorder(xi, yi, zi, n, xo, yo, zo)
                .gpu_threads(xi, yi, zi)
                .gpu_blocks(xo, yo, zo);

            f_conv.compute_at(f_ReLU, n)
                .gpu_threads(x, y, z)
                .update()
                .unroll(r.x)
                .unroll(r.y)
                .gpu_threads(x, y, z);
        } else {
            // blocking spatially with vectorization
            Var z_t, y_t, par;
            int vec_len = 8;
            int o_block_size = 32;
            int y_block = 32;
            f_conv.compute_root();
            f_conv.fuse(z, n, par).parallel(par);
            f_conv.update().reorder(x, y, r.z);
            f_conv.update().split(y, y, y_t, y_block);
            f_conv.update().split(z, z, z_t, o_block_size);
            f_conv.update().reorder(y_t, z_t, y, r.z, z);
            f_conv.update().vectorize(x, vec_len);
            f_conv.update().unroll(r.x);
            f_conv.update().unroll(r.y);
            f_conv.update().fuse(z, n, par).parallel(par);
            f_ReLU.reorder(n, z).parallel(z).vectorize(x, 8);
        }
    } else {
        // Specifying estimates
        f_ReLU.estimate(x, 0, 128)
            .estimate(y, 0, 128)
            .estimate(z, 0, 64)
            .estimate(n, 0, 4);

        p.auto_schedule(target);
    }

    // Inspect the schedule
    //f_ReLU.print_loop_nest();

    // Run the schedule
    Buffer<float> out(128, 128, 64, 4);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t*1000;
}


int main(int argc, char **argv) {
    Target target = get_target_from_environment();
    {
        std::cout << "Test 1:" << std::endl;
        double manual_time = run_test_1(false);
        double auto_time = run_test_1(true);

        std::cout << "======================" << std::endl;
        std::cout << "Manual time: " << manual_time << "ms" << std::endl;
        std::cout << "Auto time: " << auto_time << "ms" << std::endl;
        std::cout << "======================" << std::endl;

        if (!target.has_gpu_feature() && (auto_time > manual_time * 2)) {
            printf("Auto-scheduler is much much slower than it should be.\n");
            return -1;
        }
    }

    {
        std::cout << "Test 2:" << std::endl;
        double manual_time = run_test_2(false);
        double auto_time = run_test_2(true);

        std::cout << "======================" << std::endl;
        std::cout << "Manual time: " << manual_time << "ms" << std::endl;
        std::cout << "Auto time: " << auto_time << "ms" << std::endl;
        std::cout << "======================" << std::endl;

        if (!target.has_gpu_feature() && (auto_time > manual_time * 2)) {
            printf("Auto-scheduler is much much slower than it should be.\n");
            return -1;
        }
    }
    printf("Success!\n");
    return 0;
}
