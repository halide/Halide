// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"

using namespace Halide;

Expr div_up(const Expr &a, const Expr &b) {
    return (a + b - 1) / b;
}

Expr align_up(const Expr &a, const Expr &b) {
    return div_up(a, b) * b;
}

Var x{"x"}, y{"y"}, yo{"yo"}, yi{"yi"}, xo{"xo"}, xi{"xi"}, p{"p"};

Func direct_gaussian_blur(Func input, const Expr &sigma, const Expr &trunc, const LoopLevel &tiles, const LoopLevel &rows, const Target &target) {
    Func kernel{"kernel"};
    kernel(x) = exp(-(x * x) / (2 * sigma * sigma));
    kernel.compute_root();

    Expr radius = cast<int>(ceil(trunc * sigma));
    RDom r(-radius, 2 * radius + 1);

    Func kernel_sum{"kernel_sum"};
    kernel_sum() = sum(kernel(r));
    kernel_sum.compute_root();

    Func kernel_normalized{"kernel_normalized"};
    kernel_normalized(x) = kernel(x) / kernel_sum();
    kernel_normalized.compute_root();

    Func blur_y{"blur_y"}, blur_y_sum{"blur_y_sum"}, blur_x{"blur_x"}, blur_x_sum{"blur_x_sum"};
    blur_y(x, y) = sum(kernel_normalized(r) * input(x, y + r), blur_y_sum);
    blur_x(x, y) = sum(kernel_normalized(r) * blur_y(x + r, y), blur_x_sum);

    const int vec = target.natural_vector_size<float>();

    if (target.has_gpu_feature()) {
        blur_y.in()
            .compute_root()
            .never_partition(x, y)
            .gpu_tile(x, y, xi, yi, 32, 8);
    } else {
        blur_y_sum
            .align_bounds(x, vec)
            .store_at(tiles)
            .compute_at(rows)
            .vectorize(x, vec, TailStrategy::RoundUp)
            .update()
            .unroll(r, 2)
            .reorder(x, r)
            .vectorize(x, vec, TailStrategy::RoundUp);
    }

    return blur_x;
}

using namespace Halide;
using namespace Halide::BoundaryConditions;

class GaussianBlurDirect : public Generator<GaussianBlurDirect> {
public:
    Input<Buffer<float>> input{"input", 2};
    Input<float> sigma{"sigma"};
    Input<int> trunc{"trunc"};
    Output<Buffer<float>> output{"output", 2};

    void generate() {
        Func clamped = BoundaryConditions::repeat_edge(input);

        LoopLevel tiles, rows;
        output = direct_gaussian_blur(clamped, sigma, trunc, tiles, rows, target);

        const int vec = natural_vector_size<float>();

        const int tasks = 32;

        if (get_target().has_gpu_feature()) {
            output.compute_root()
                .never_partition(x, y)
                .gpu_tile(x, y, xi, yi, 32, 8);
        } else {
            output.compute_root()
                .reorder(x, y)
                .split(y, yo, yi, div_up(output.height(), tasks), TailStrategy::GuardWithIf)
                .vectorize(x, vec)
                .parallel(yo);
        }

        tiles.set({Func{output}, yo});
        rows.set({Func{output}, yi});
    }
};

HALIDE_REGISTER_GENERATOR(GaussianBlurDirect, gaussian_blur_direct)

class GaussianBlur : public Generator<GaussianBlur> {
public:
    GeneratorParam<int> factor{"factor", 8},
        upsample_order{"upsample_order", 3},
        downsample_order{"downsample_order", 2},
        passes{"passes", 2};

    Input<Buffer<float>> input{"input", 2};
    Input<float> sigma{"sigma"};
    Input<int> trunc{"trunc"};
    Output<Buffer<float>> output{"output", 2};

    Var x{"x"}, y{"y"};

    std::pair<Func, float> make_resampling_kernel(int order) {
        // Make a downsampling/upsampling kernel for the given factor. Use some
        // number of boxes.
        RDom r_box(0, factor);
        Func box{"box"};
        box(x) = select(x >= 0 && x < factor, 1.0f / factor, 0.f);
        Func kernel = box;
        for (int i = 1; i < order; i++) {
            Func next{"kernel_" + std::to_string(i)};
            next(x) = sum(kernel(x - r_box) * box(r_box));
            kernel = next;
            kernel.compute_root();

            // Add a [1 1] filter as well, so that we add 'order' to the width
            // of the kernel, instead of order - 1. This gives a modest boost in
            // PSNR at no cost.
            next = Func{};
            next(x) = (kernel(x) + kernel(x - 1)) * 0.5f;
            kernel = next;
            kernel.compute_root();
        }
        if (order > 1) {
            // Just compute it now and embed it as constant data
            Buffer<float> k = kernel.realize({factor * order});
            kernel = Func{};
            kernel(x) = k(x);
        }
        // Compute the variance of the downsampling/upsampling kernel. It's
        // 'order' discrete box distributions.
        float variance = order * ((float)factor * factor - 1) / 12;
        // plus our 'order - 1' [1 1] filters, which each have variance 1/4
        variance += (order - 1) / 4.0f;
        return {kernel, variance};
    }

    void generate() {
        // See the interactive doc impulse_viewer.html for an explanation of this algorithm.

        const int vec = natural_vector_size<float>();

        auto [up_kernel, up_variance] = make_resampling_kernel(upsample_order);
        auto [down_kernel, down_variance] = make_resampling_kernel(downsample_order);

        Func clamped_y{"clamped_y"};
        clamped_y(x, y) = input(x, clamp(y, input.dim(1).min(), input.dim(1).max()));

        RDom rf(0, factor), rp(0, downsample_order);
        RDom rx(0, (int)factor * downsample_order);

        Expr shift = (((int)upsample_order - downsample_order) * factor) / 2;

        Func down_y_phases{"down_y_phases"};
        down_y_phases(x, y, p) += clamped_y(x, factor * y + rf + shift) * down_kernel(rf + p * factor);
        Func down_y{"down_y"};
        down_y(x, y) += down_y_phases(x, y + rp, rp);

        Func clamped_x{"clamped_x"};
        clamped_x(x, y) = down_y(clamp(likely(x), input.dim(0).min(), input.dim(0).max()), y);

        Func down_x{"down_x"};
        down_x(x, y) += clamped_x(factor * x + rx + shift, y) * down_kernel(rx);

        // We're going to blur at low res using a smaller filter. Upsampling and
        // downsampling already blur, so we'd better account for that.
        Expr sigma_lo = sqrt(max(sigma * sigma - up_variance - down_variance, 1e-4f)) / factor;

        // Clamp in y again, so that we don't compute lots of entire padding rows of down_x
        Func clamped_y_2{"clamped_y_2"};
        clamped_y_2(x, y) = down_x(x, clamp(y, -(int)upsample_order, div_up(input.height(), factor)));

        LoopLevel tiles, rows;
        Func blurred = direct_gaussian_blur(clamped_y_2, sigma_lo, trunc, tiles, rows, target);

        // Treat upsampling as applying a multi-phase filter and interleaving the results
        Expr e = 0.f;
        for (int i = 0; i < upsample_order; i++) {
            e += blurred(x - i, y) * (up_kernel(i * factor + p) * factor);
        }
        Func up_x_phases{"up_x_phases"};
        up_x_phases(x, y, p) = e;

        Func up_x{"up_x"};
        up_x(x, y) = up_x_phases(x / factor, y, x % factor);

        e = 0.f;
        for (int i = 0; i < upsample_order; i++) {
            e += up_x(x, y - i) * (up_kernel(i * factor + p) * factor);
        }
        Func up_y_phases{"up_y_phases"};
        up_y_phases(x, y, p) = e;

        output(x, y) = up_y_phases(x, y / factor, y % factor);

        const int tasks = 32;

        if (get_target().has_gpu_feature()) {
            // GPU schedule

            Var xii{"xii"}, yii{"yii"};

            output.compute_root()
                .never_partition(x, y)
                .align_bounds(x, factor)
                .align_bounds(y, factor)
                .gpu_tile(x, y, xi, yi, 32 * factor, 2 * factor, TailStrategy::GuardWithIf)
                .tile(xi, yi, xi, yi, xii, yii, factor, factor)
                .vectorize(xii)
                .unroll(yii);

            blurred.in()
                .compute_root()
                .never_partition(x, y)
                .gpu_tile(x, y, xi, yi, 32, 1);

            down_x.in()
                .compute_root()
                .never_partition(x, y)
                .gpu_tile(x, y, xi, yi, 32, 1);

            down_y.in()
                .compute_at(down_x.in(), x)
                .split(x, x, xi, 32, TailStrategy::GuardWithIf)
                .gpu_threads(xi, y);

            down_y_phases.update().unroll(rf);
            down_y.update().unroll(rp);
            down_x.update().unroll(rx);

        } else {

            // CPU schedule

            if (false) {
                // One pass - lots of redundant recompute on the
                // downsample. Generally slower.
                down_x.in()
                    .store_at(tiles)
                    .compute_at(rows)
                    .vectorize(x, vec, TailStrategy::RoundUp)
                    .split(Var::outermost(), Var::outermost(), yo, 1)
                    .rename(y, yi);

            } else {
                // Two-pass. Less redundant recompute, more thread pool overhead.
                Expr strip = div_up(div_up(output.height(), factor) + upsample_order, tasks);
                down_x
                    .in()
                    .compute_root()
                    .split(y, yo, yi, strip, TailStrategy::GuardWithIf)
                    .vectorize(x, vec * 2, TailStrategy::RoundUp)
                    .parallel(yo);
            }

            clamped_x
                .compute_at(down_x.in(), x)
                .vectorize(x, vec);

            RVar rxo, rxi;
            down_x
                .compute_at(down_x.in(), x)
                .vectorize(x)
                .update()
                .split(rx, rxo, rxi, factor)
                .unroll(rxo)
                .vectorize(x)
                .atomic()
                .vectorize(rxi);

            down_y.update().unroll(rp);

            down_y_phases.in()
                .store_at(down_x.in(), yo)
                .compute_at(down_x.in(), yi)
                .reorder(p, x, y)
                .unroll(p)
                .vectorize(x, vec, TailStrategy::RoundUp)
                .fold_storage(y, downsample_order);

            down_y_phases
                .compute_at(down_y_phases.in(), x)
                .unroll(p)
                .vectorize(x)
                .update()
                .reorder(p, x, rf, y)
                .vectorize(x)
                .unroll(rf)
                .unroll(p);

            blurred
                .store_in(MemoryType::Stack)
                .compute_at(rows)
                .vectorize(x, vec * 2, TailStrategy::RoundUp);

            tiles.set({Func{up_x}, y});
            rows.set({Func{up_x}, y});

            up_x.store_at(output, yo)
                .compute_at(output, yi)
                .align_bounds(x, vec * factor)
                .vectorize(x, vec * factor, TailStrategy::RoundUp);

            Expr rows_at_a_time = std::min((int)factor, 8);
            Expr slice = div_up(output.height(), tasks);
            slice = align_up(slice, rows_at_a_time);

            output
                .never_partition(y)
                .align_bounds(y, rows_at_a_time)
                .split(y, yo, yi, slice, TailStrategy::ShiftInwards)
                .reorder(yi, x)
                .unroll(yi, rows_at_a_time)
                .reorder(x, yi)
                .vectorize(x, vec * 2)
                .parallel(yo);
        }

        output.set_host_alignment(64);
        output.dim(1).set_min(0);
        output.dim(1).set_stride((output.dim(1).stride() / 16) * 16);
        output.dim(0).set_min(0);
    }
};

HALIDE_REGISTER_GENERATOR(GaussianBlur, gaussian_blur)
