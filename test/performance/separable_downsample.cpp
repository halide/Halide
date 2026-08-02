#include "Halide.h"
#include "halide_benchmark.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    constexpr int stride = 2;
    constexpr int radius = 7;
    constexpr int diameter = 2 * radius + 1;
    constexpr int output_width = 1024;
    constexpr int output_height = 768;

    Buffer<float> input(stride * output_width + 2 * radius,
                        stride * output_height + 2 * radius);
    input.set_min(-radius, -radius);
    for (int y = input.dim(1).min(); y <= input.dim(1).max(); y++) {
        for (int x = input.dim(0).min(); x <= input.dim(0).max(); x++) {
            input(x, y) = static_cast<float>((7 * x + 13 * y + 1001) % 61 - 30);
        }
    }

    Buffer<float> kernel(diameter);
    kernel.set_min(-radius);
    constexpr std::array<int, diameter> binomial_weights = {
        1, 14, 91, 364, 1001, 2002, 3003, 3432,
        3003, 2002, 1001, 364, 91, 14, 1};
    for (int i = 0; i < diameter; i++) {
        kernel(i - radius) = static_cast<float>(binomial_weights[i]) / 16384.0f;
    }

    Var x{"x"}, y{"y"};
    const int vec = target.natural_vector_size<float>();

    // The one-step implementation filters and downsamples both dimensions in a
    // single 2D reduction.
    RDom direct_r(-radius, diameter, -radius, diameter, "direct_r");
    Func direct{"direct_downsample"};
    direct(x, y) = 0.0f;
    direct(x, y) +=
        kernel(direct_r.x) * kernel(direct_r.y) *
        input(stride * x + direct_r.x, stride * y + direct_r.y);

    direct.compute_root()
        .parallel(y)
        .vectorize(x, vec);
    direct.update()
        .reorder(x, direct_r.x, direct_r.y, y)
        .vectorize(x, vec)
        .unroll(direct_r.x)
        .unroll(direct_r.y);

    // Start with the same one-step definition. Preserving r.x leaves r.y as the
    // reduction dimension of vertical_weighted. kernel(r.x) is then invariant
    // over that reduction, so hoist_invariants() produces:
    //
    //   vertical(x, y, dx) =
    //       sum_ry kernel(ry) * input(2*x + dx, 2*y + ry)
    //   separated(x, y) =
    //       sum_dx kernel(dx) * vertical(x, y, dx)
    //
    // Thus the first stage filters and downsamples in y, and the second stage
    // filters and downsamples in x.
    RDom r(-radius, diameter, -radius, diameter, "r");
    Func separated{"separable_downsample"};
    separated(x, y) = 0.0f;
    separated(x, y) +=
        kernel(r.x) * kernel(r.y) *
        input(stride * x + r.x, stride * y + r.y);

    Var dx{"dx"};
    Func vertical_weighted = separated.update().rfactor(r.x, dx);
    Func vertical = vertical_weighted.update().hoist_invariants()[0];

    separated.compute_root()
        .parallel(y)
        .vectorize(x, vec);
    separated.update()
        .reorder(x, r.x, y)
        .vectorize(x, vec)
        .unroll(r.x);

    // Compute one vertical pass for the current x-kernel offset, consume it
    // across the output row, then move to the next offset.
    vertical_weighted.compute_at(separated, r.x)
        .vectorize(x, vec);
    vertical_weighted.update()
        .reorder(x, dx, y)
        .vectorize(x, vec);

    vertical.compute_at(vertical_weighted, x)
        .vectorize(x, vec);
    vertical.update()
        .reorder(x, r.y, dx, y)
        .vectorize(x, vec)
        .unroll(r.y);

    Buffer<float> direct_output(output_width, output_height);
    Buffer<float> separated_output(output_width, output_height);

    // Warm up and JIT both variants before timing.
    direct.realize(direct_output, target);
    separated.realize(separated_output, target);

    for (int yy = 0; yy < output_height; yy++) {
        for (int xx = 0; xx < output_width; xx++) {
            const float ref = direct_output(xx, yy);
            const float actual = separated_output(xx, yy);
            const float tolerance = 1e-5f * std::max(1.0f, std::abs(ref));
            if (std::abs(actual - ref) > tolerance) {
                printf("Separable downsample mismatch at (%d, %d): %f vs direct %f\n",
                       xx, yy, actual, ref);
                return 1;
            }
        }
    }

    const double direct_time = benchmark([&] {
        direct.realize(direct_output, target);
    });
    const double separated_time = benchmark([&] {
        separated.realize(separated_output, target);
    });

    printf("2x downsample with %dx%d separable kernel\n"
           "Direct 2D reduction:          %0.4f ms\n"
           "rfactor + hoist_invariants:   %0.4f ms (%0.2fx vs direct)\n",
           diameter, diameter,
           direct_time * 1000,
           separated_time * 1000,
           direct_time / separated_time);

    printf("Success!\n");
    return 0;
}
