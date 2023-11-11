#include "Halide.h"

namespace {

std::vector<Halide::Func> func_vector(const std::string &name, int size) {
    std::vector<Halide::Func> funcs;
    for (int i = 0; i < size; i++) {
        funcs.emplace_back(Halide::Func{name + "_" + std::to_string(i)});
    }
    return funcs;
}

class Interpolate : public Halide::Generator<Interpolate> {
public:
    GeneratorParam<int> levels{"levels", 10};

    Input<Buffer<float, 3>> input{"input"};
    Output<Buffer<float, 3>> output{"output"};

    void generate() {
        Var x("x"), y("y"), c("c");

        // Input must have four color channels - rgba
        input.dim(2).set_bounds(0, 4);

        auto downsampled = func_vector("downsampled", levels);
        auto downx = func_vector("downx", levels);
        auto interpolated = func_vector("interpolated", levels);
        auto upsampled = func_vector("upsampled", levels);
        auto upsampledx = func_vector("upsampledx", levels);

        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        downsampled[0](x, y, c) = select(c < 3, clamped(x, y, c) * clamped(x, y, 3), clamped(x, y, 3));

        for (int l = 1; l < levels; ++l) {
            Func prev = downsampled[l - 1];

            if (l == 4) {
                // Also add a boundary condition at a middle pyramid level
                // to prevent the footprint of the downsamplings to extend
                // too far off the base image. Otherwise we look 512
                // pixels off each edge.
                Expr w = input.width() / (1 << (l - 1));
                Expr h = input.height() / (1 << (l - 1));
                prev = lambda(x, y, c, prev(clamp(x, 0, w), clamp(y, 0, h), c));
            }

            downx[l](x, y, c) = (prev(x * 2 - 1, y, c) +
                                 2.0f * prev(x * 2, y, c) +
                                 prev(x * 2 + 1, y, c)) *
                                0.25f;
            downsampled[l](x, y, c) = (downx[l](x, y * 2 - 1, c) +
                                       2.0f * downx[l](x, y * 2, c) +
                                       downx[l](x, y * 2 + 1, c)) *
                                      0.25f;
        }
        interpolated[levels - 1](x, y, c) = downsampled[levels - 1](x, y, c);
        for (int l = levels - 2; l >= 0; --l) {
            upsampledx[l](x, y, c) = (interpolated[l + 1](x / 2, y, c) +
                                      interpolated[l + 1]((x + 1) / 2, y, c)) /
                                     2.0f;
            upsampled[l](x, y, c) = (upsampledx[l](x, y / 2, c) +
                                     upsampledx[l](x, (y + 1) / 2, c)) /
                                    2.0f;
            Expr alpha = 1.0f - downsampled[l](x, y, 3);
            interpolated[l](x, y, c) = (downsampled[l](x, y, c) +
                                        alpha * upsampled[l](x, y, c));
        }

        Func normalize("normalize");
        normalize(x, y, c) = interpolated[0](x, y, c) / interpolated[0](x, y, 3);

        // Schedule
        if (using_autoscheduler()) {
            output = normalize;
        } else {
            // 0.86ms on a 2060 RTX
            Var yo, yi, xo, xi, ci, xii, yii;
            if (get_target().has_gpu_feature()) {
                normalize
                    .never_partition_all()
                    .bound(x, 0, input.width())
                    .bound(y, 0, input.height())
                    .bound(c, 0, 3)
                    .reorder(c, x, y)
                    .tile(x, y, xi, yi, 32, 32, TailStrategy::RoundUp)
                    .tile(xi, yi, xii, yii, 2, 2)
                    .gpu_blocks(x, y)
                    .gpu_threads(xi, yi)
                    .unroll(xii)
                    .unroll(yii)
                    .unroll(c);

                for (int l = 1; l < levels; l++) {
                    downsampled[l]
                        .compute_root()
                        .never_partition_all()
                        .reorder(c, x, y)
                        .unroll(c)
                        .gpu_tile(x, y, xi, yi, 16, 16);
                }

                for (int l = 3; l < levels; l += 2) {
                    interpolated[l]
                        .compute_root()
                        .never_partition_all()
                        .reorder(c, x, y)
                        .tile(x, y, xi, yi, 32, 32, TailStrategy::RoundUp)
                        .tile(xi, yi, xii, yii, 2, 2)
                        .gpu_blocks(x, y)
                        .gpu_threads(xi, yi)
                        .unroll(xii)
                        .unroll(yii)
                        .unroll(c);
                }

                upsampledx[1]
                    .compute_at(normalize, x)
                    .never_partition_all()
                    .reorder(c, x, y)
                    .tile(x, y, xi, yi, 2, 1)
                    .unroll(xi)
                    .unroll(yi)
                    .unroll(c)
                    .gpu_threads(x, y);

                interpolated[1]
                    .compute_at(normalize, x)
                    .never_partition_all()
                    .reorder(c, x, y)
                    .tile(x, y, xi, yi, 2, 2)
                    .unroll(xi)
                    .unroll(yi)
                    .unroll(c)
                    .gpu_threads(x, y);

                interpolated[2]
                    .compute_at(normalize, x)
                    .never_partition_all()
                    .reorder(c, x, y)
                    .unroll(c)
                    .gpu_threads(x, y);

                output = normalize;
            } else {
                // 4.54ms on an Intel i9-9960X using 16 threads
                Var xo, xi, yo, yi;
                const int vec = natural_vector_size<float>();
                for (int l = 1; l < levels - 1; ++l) {
                    // We must refer to the downsampled stages in the
                    // upsampling later, so they must all be
                    // compute_root or redundantly recomputed, as in
                    // the local_laplacian app.
                    downsampled[l]
                        .compute_root()
                        .never_partition(x)
                        .reorder(x, c, y)
                        .split(y, yo, yi, 8)
                        .parallel(yo)
                        .vectorize(x, vec);
                }

                // downsampled[0] takes too long to compute_root, so
                // we'll redundantly recompute it instead.  Make a
                // separate clone of it in the first downsampled stage
                // so that we can schedule the two versions
                // separately.
                downsampled[0]
                    .clone_in(downx[1])
                    .store_at(downsampled[1], yo)
                    .compute_at(downsampled[1], yi)
                    .reorder(c, x, y)
                    .unroll(c)
                    .vectorize(x, vec)
                    .never_partition(y);

                normalize
                    .bound(x, 0, input.width())
                    .bound(y, 0, input.height())
                    .bound(c, 0, 3)
                    .never_partition(y)
                    .split(x, xo, xi, vec)
                    .split(y, yo, yi, 32)
                    .reorder(xi, c, xo, yi, yo)
                    .unroll(c)
                    .vectorize(xi)
                    .parallel(yo);

                for (int l = 1; l < levels; l++) {
                    interpolated[l]
                        .store_at(normalize, yo)
                        .compute_at(normalize, yi)
                        .never_partition_all()
                        .vectorize(x, vec);
                }

                output = normalize;
            }
        }

        // Estimates (for autoscheduler; ignored otherwise)
        {
            input.dim(0).set_estimate(0, 1536);
            input.dim(1).set_estimate(0, 2560);
            input.dim(2).set_estimate(0, 4);
            output.dim(0).set_estimate(0, 1536);
            output.dim(1).set_estimate(0, 2560);
            output.dim(2).set_estimate(0, 3);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Interpolate, interpolate)
