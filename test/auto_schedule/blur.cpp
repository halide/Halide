#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

enum class BlurGPUSchedule {
    Inline,         // Fully inlining schedule.
    Cache,          // Schedule caching intermedia result of blur_x.
    Slide,          // Schedule enabling sliding window opt within each
                    // work-item or cuda thread.
    SlideVectorize, // The same as above plus vectorization per work-item.
};

double run_test(bool auto_schedule) {
    int W = 6408;
    int H = 4802;
    Buffer<uint16_t> img(W, H);

    for (int y = 0; y < img.height(); y++) {
        for (int x = 0; x < img.width(); x++) {
                img(x, y) = rand() & 0xfff;
        }
    }

    BlurGPUSchedule schedule = BlurGPUSchedule::SlideVectorize;

    Param<int> tile_x; // X tile.
    Param<int> tile_y;
    ImageParam input(UInt(16), 2, "input");

    Func blur_x("blur_x"), blur_y("blur_y");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    // The algorithm
    blur_x(x, y) = (input(x, y) + input(x+1, y) + input(x+2, y))/3;
    blur_y(x, y) = (blur_x(x, y) + blur_x(x, y+1) + blur_x(x, y+2))/3;

    Target target = get_target_from_environment();
    Pipeline p(blur_y);

    if (!auto_schedule) {
        if (target.has_gpu_feature()) {
            // GPU schedule.
            switch (schedule) {
            case BlurGPUSchedule::Inline:
                // - Fully inlining.
                blur_y.gpu_tile(x, y, xi, yi, tile_x, tile_y);
                break;
            case BlurGPUSchedule::Cache:
                // - Cache blur_x calculation.
                blur_y.gpu_tile(x, y, xi, yi, tile_x, tile_y);
                blur_x.compute_at(blur_y, x).gpu_threads(x, y);
                break;
            case BlurGPUSchedule::Slide: {
                // - Instead caching blur_x calculation explicitly, the
                //   alternative is to allow each work-item in OpenCL or thread
                //   in CUDA to calculate more rows of blur_y so that temporary
                //   blur_x calculation is re-used implicitly. This achieves
                //   the similar schedule of sliding window.
                Var y_inner("y_inner");
                blur_y.split(y, y, y_inner, tile_y).reorder(y_inner, x).unroll(y_inner)
                    .gpu_tile(x, y, xi, yi, tile_x, 1);
                break;
            }
            case BlurGPUSchedule::SlideVectorize: {
                // Vectorization factor.
                int factor = sizeof(int)/sizeof(short);
                Var y_inner("y_inner");
                blur_y.vectorize(x, factor)
                    .split(y, y, y_inner, tile_y).reorder(y_inner, x).unroll(y_inner)
                    .gpu_tile(x, y, xi, yi, tile_x, 1);
                break;
            }
            default:
                break;
            }
        } else {
            // CPU schedule.
            blur_y.split(y, y, yi, 8).parallel(y).vectorize(x, 8);
            blur_x.store_at(blur_y, y).compute_at(blur_y, yi).vectorize(x, 8);
        }
    } else {
        input.dim(0).set_bounds_estimate(0, img.width());
        input.dim(1).set_bounds_estimate(0, img.height());
        blur_y.estimate(x, 0, img.width()-8).estimate(y, 0, img.height()-2);
        p.auto_schedule(target);
    }

    tile_x.set(32);
    tile_y.set(8);
    input.set(img);

    // Benchmark the schedule
    Buffer<uint16_t> out(img.width()-8, img.height()-2);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t*1000;
}

int main(int argc, char **argv) {
    double manual_time = run_test(false);
    double auto_time = run_test(true);

    std::cout << "======================" << std::endl;
    std::cout << "Manual time: " << manual_time << "ms" << std::endl;
    std::cout << "Auto time: " << auto_time << "ms" << std::endl;
    std::cout << "======================" << std::endl;

    if (!get_target_from_environment().has_gpu_feature() &&
        (auto_time > manual_time * 4)) {
        printf("Auto-scheduler is much much slower than it should be.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
