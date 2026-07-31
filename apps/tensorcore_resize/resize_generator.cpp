#include "Halide.h"

namespace {

using namespace Halide;

enum class InterpolationType {
    Box,
    Linear,
    Cubic,
    Lanczos,
};

enum class Schedule {
    CUDA,
    TensorCore,
};

Expr kernel_box(Expr x) {
    Expr xx = abs(x);
    return select(xx <= 0.5f, 1.0f, 0.0f);
}

Expr kernel_linear(Expr x) {
    Expr xx = abs(x);
    return select(xx < 1.0f, 1.0f - xx, 0.0f);
}

Expr kernel_cubic(Expr x) {
    Expr xx = abs(x);
    Expr xx2 = xx * xx;
    Expr xx3 = xx2 * xx;
    float a = -0.5f;

    return select(xx < 1.0f, (a + 2.0f) * xx3 - (a + 3.0f) * xx2 + 1,
                  select(xx < 2.0f, a * xx3 - 5 * a * xx2 + 8 * a * xx - 4.0f * a,
                         0.0f));
}

Expr sinc(Expr x) {
    x *= 3.14159265359f;
    return sin(x) / x;
}

constexpr int lanczos_lobes = 3;

Expr kernel_lanczos(Expr x) {
    Expr value = sinc(x) * sinc(x / lanczos_lobes);
    // Take care of the singularity at zero
    value = select(x == 0.0f, 1.0f, value);
    // Clamp to zero out of bounds
    value = select(x > lanczos_lobes || x < -lanczos_lobes, 0.0f, value);
    return value;
}

struct KernelInfo {
    const char *name;
    int taps;
    Expr (*kernel)(Expr);
};

const KernelInfo kernel_info[] = {
    {"box", 1, kernel_box},
    {"linear", 2, kernel_linear},
    {"cubic", 4, kernel_cubic},
    {"lanczos", 2 * lanczos_lobes, kernel_lanczos}};

// Resampling an image is a linear operator, so it can be written as a matrix
// multiply. The matrix is enormous and almost entirely zero, so you never want
// to materialize it, but each row of it has a small number of contiguous
// non-zeros, and if we let neighbouring rows share a starting column then a
// block of 16 rows of it is a small dense matrix. That makes the inner loop a
// matrix multiply, which is a large speed-up even without tensor cores, and
// lets us use tensor cores when we have them.
class Resize : public Halide::Generator<Resize> {
public:
    GeneratorParam<InterpolationType> interpolation_type{
        "interpolation_type", InterpolationType::Lanczos, {{"box", InterpolationType::Box}, {"linear", InterpolationType::Linear}, {"cubic", InterpolationType::Cubic}, {"lanczos", InterpolationType::Lanczos}}};

    // If we statically know whether we're upsampling or downsampling, we can
    // generate different pipelines (we want to reorder the resample in x and
    // in y).
    GeneratorParam<bool> upsample{"upsample", false};

    GeneratorParam<Schedule> gpu_schedule{
        "gpu_schedule", Schedule::TensorCore, {{"cudaonly", Schedule::CUDA}, {"tensorcore", Schedule::TensorCore}}};

    Input<Buffer<float16_t, 3>> input{"input"};
    Input<float> scale_factor{"scale_factor"};
    Output<Buffer<float16_t, 3>> output{"output"};

    // The size of the blocks of the resampling matrix that we treat as dense.
    static constexpr int tile = 16;

    void generate() {
        // Invert the scale factor in a single place, to avoid getting slightly
        // different ratios showing up in different places.
        Expr inverse_scale_factor = 1.0f / scale_factor;

        // For downscaling, widen the interpolation kernel to perform lowpass
        // filtering.
        Expr kernel_scaling = upsample ? Expr(1.0f) : scale_factor;
        Expr inverse_kernel_scaling = upsample ? Expr(1.0f) : inverse_scale_factor;

        const KernelInfo &info = kernel_info[(int)(InterpolationType)interpolation_type];

        Expr kernel_radius = 0.5f * info.taps * inverse_kernel_scaling;
        Expr kernel_taps = cast<int>(ceil(info.taps * inverse_kernel_scaling));

        // The (non-integer) coordinates in the source image.
        Expr sourcex = (x + 0.5f) * inverse_scale_factor - 0.5f;
        Expr sourcey = (y + 0.5f) * inverse_scale_factor - 0.5f;

        // For a given output coordinate, the first input coordinate it depends
        // on. We can start a row of the matrix at any column we like as long
        // as we store enough columns, so we use the same starting column for
        // each group of `tile` rows.
        auto begin_of = [&](Expr coord) {
            return cast<int>(ceil((coord + 0.5f) * inverse_scale_factor - 0.5f - kernel_radius));
        };

        Expr beginx = begin_of((x / tile) * tile);
        Expr beginy = begin_of((y / tile) * tile);

        // Moving the start of each row back like that means each row has to
        // cover a longer contiguous region.
        Expr extra_zeros = begin_of(tile) - begin_of(0);

        // Round the number of columns up to the next multiple of the tile size
        // too, so that the reduction is a whole number of tiles.
        Expr span = ((kernel_taps + extra_zeros + tile - 1) / tile) * tile;

        // Don't go off the end of the image. Those columns would be zero
        // anyway.
        beginx = clamp(beginx, 0, input.width() - span);
        beginy = clamp(beginy, 0, input.height() - span);

        r = RDom(0, span, "r");

        as_float(x, y, c) = cast<float16_t>(input(x, y, c));

        unnormalized_kernel_x(x, k) = info.kernel((k + beginx - sourcex) * kernel_scaling);
        unnormalized_kernel_y(y, k) = info.kernel((k + beginy - sourcey) * kernel_scaling);

        kernel_sum_x(x) += unnormalized_kernel_x(x, r);
        kernel_sum_y(y) += unnormalized_kernel_y(y, r);

        kernel_x(x, k) = cast<float16_t>(unnormalized_kernel_x(x, k) / kernel_sum_x(x));
        kernel_y(y, k) = cast<float16_t>(unnormalized_kernel_y(y, k) / kernel_sum_y(y));

        resized_y(x, y, c) += kernel_y(y, r) * as_float(x, r + beginy, c);
        resized_x(x, y, c) += kernel_x(x, r) * resized_y(r + beginx, y, c);

        output(x, y, c) = clamp(resized_x(x, y, c), cast<float16_t>(0.f), cast<float16_t>(1.f));
    }

    void schedule() {
        Var xi("xi"), yi("yi"), ki("ki"), xii("xii"), yii("yii"), xo("xo"), z("z");

        // Precompute the sparse matrices. These are tiny compared to the
        // image, so the schedule barely matters.
        kernel_x.compute_root().gpu_tile(x, k, xi, ki, 32, 8);
        unnormalized_kernel_x.compute_root().gpu_tile(x, k, xi, ki, 32, 8);
        kernel_sum_x.in().compute_root().gpu_tile(x, xi, 32);

        kernel_y.compute_root().gpu_tile(y, k, yi, ki, 32, 8);
        unnormalized_kernel_y.compute_root().gpu_tile(y, k, yi, ki, 32, 8);
        kernel_sum_y.in().compute_root().gpu_tile(y, yi, 32);

        output.compute_root()
            .align_bounds(x, tile)
            .align_bounds(y, tile);

        if (gpu_schedule == Schedule::CUDA) {
            // Resampling in y is the expensive stage for large downsamples.
            // The load from the kernel doesn't depend on x or c, and the load
            // from the image doesn't depend on y % tile, so we schedule it
            // like a matrix multiply.
            resized_y.in()
                .compute_root()
                .align_bounds(x, tile)
                .align_bounds(y, tile)
                .reorder(c, x, y)
                .unroll(c)
                .gpu_tile(x, y, xi, yi, 32, 16, TailStrategy::RoundUp)
                .tile(xi, yi, xii, yii, 2, 4)
                .unroll(xii)
                .unroll(yii);
            resized_y
                .compute_at(resized_y.in(), xi)
                .unroll(c)
                .unroll(x)
                .unroll(y)
                .update()
                .reorder(x, y, c, r)
                .unroll(c)
                .unroll(x)
                .unroll(y);
            as_float.compute_at(resized_y, c).vectorize(x).vectorize(y);
            kernel_y.in().compute_at(resized_y, r).vectorize(y).vectorize(k);

            // After downsampling in y it's hard to fill the machine, so use
            // smaller tiles and map color channels to gpu threads.
            output
                .gpu_threads(c)
                .gpu_tile(x, y, xi, yi, 32, 4, TailStrategy::RoundUp)
                .reorder(xi, yi, c, x, y)
                .tile(xi, yi, xii, yii, 2, 2)
                .vectorize(xii)
                .unroll(yii);

            resized_x
                .compute_at(output, xi)
                .unroll(c)
                .unroll(x)
                .unroll(y)
                .update()
                .reorder(x, y, c, r)
                .unroll(c)
                .unroll(x)
                .unroll(y);
            resized_y.in().in().compute_at(resized_x, c).vectorize(y);
            kernel_x.in().compute_at(resized_x, r).vectorize(x).vectorize(k);
        } else {
            // The tensor core instructions want the reduction dimension of
            // each operand dense in memory.
            kernel_x.reorder_storage(k, x);
            kernel_y.reorder_storage(k, y);

            Var xio("xio");
            resized_y.in()
                .compute_root()
                .align_bounds(x, tile)
                .align_bounds(y, tile)
                .tile(x, y, xi, yi, 32, 16, TailStrategy::RoundUp)
                .unroll(c)
                .split(xi, xi, xii, 32)
                .split(xi, xio, xi, 1)
                .gpu_threads(xio)
                .split(yi, yi, yii, 8)
                .reorder(xii, yii, c, yi, xi, xio, x, y)
                .vectorize(xii)
                .vectorize(yii)
                .unroll(yi)
                .unroll(xi)
                .gpu_blocks(x, y);

            // An 8x32 tile of accumulator, reducing 16 taps at a time.
            resized_y.compute_at(resized_y.in(), xio)
                .store_in(MemoryType::WMMAFragment)
                .unroll(c)
                .vectorize(x, 32)
                .unroll(x)
                .vectorize(y, 8)
                .unroll(y)
                .update()
                .atomic()
                .unroll(c)
                .vectorize(x, 32)
                .unroll(x)
                .vectorize(y, 8)
                .unroll(y)
                .vectorize(r, tile)
                .reorder(y, c, x, r);

            output
                .tile(x, y, xi, yi, tile, tile, TailStrategy::RoundUp)
                .reorder(yi, xi, x, y, c)
                .gpu_blocks(x, y, c)
                .split(yi, yi, yii, 2)
                .fuse(xi, yii, z)
                .gpu_lanes(z)
                .unroll(yi);

            resized_x.in()
                .compute_at(output, x)
                .vectorize(x)
                .vectorize(y);

            RVar ri("ri"), ro("ro");
            resized_x
                .store_in(MemoryType::WMMAFragment)
                .compute_at(resized_x.in(), c)
                .vectorize(x)
                .vectorize(y)
                .update()
                .atomic()
                .split(r, ro, ri, tile)
                .reorder(ri, x, y, ro)
                .vectorize(x)
                .vectorize(y)
                .vectorize(ri);

            // An extra layer of staging, because we're not necessarily aligned
            // in x.
            resized_y.in()
                .in()
                .compute_at(output, x)
                .store_in(MemoryType::GPUShared)
                .split(x, xo, xi, 32, TailStrategy::RoundUp)
                .gpu_lanes(xi);
        }

        output.dim(0).set_min(0);
        output.dim(1).set_min(0);
        output.dim(2).set_bounds(0, 3);
        input.dim(0).set_min(0);
        input.dim(1).set_min(0);
        input.dim(2).set_bounds(0, 3);
    }

private:
    Var x{"x"}, y{"y"}, c{"c"}, k{"k"};
    RDom r;

    Func as_float{"as_float"},
        resized_x{"resized_x"},
        resized_y{"resized_y"},
        unnormalized_kernel_x{"unnormalized_kernel_x"},
        unnormalized_kernel_y{"unnormalized_kernel_y"},
        kernel_x{"kernel_x"},
        kernel_y{"kernel_y"},
        kernel_sum_x{"kernel_sum_x"},
        kernel_sum_y{"kernel_sum_y"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Resize, resize)
