#include "Halide.h"

using namespace Halide;

// Generate a pipeline that reads YUV data via DMA, scales the data
// by 2, and (optionally) writes the YUV data back via DMA.
class DmaPipeline : public Generator<DmaPipeline> {
public:
    // The type must be specified when building the generator, to be either uint8 or uint16.
    Input<Buffer<void, 2>> input_y{"input_y"};
    Input<Buffer<void, 3>> input_uv{"input_uv"};
    Output<Buffer<void, 2>> output_y{"output_y"};
    Output<Buffer<void, 3>> output_uv{"output_uv"};

    enum class Schedule { Basic,
                          Fold,
                          Async,
                          Split,
                          Split_Async };
    GeneratorParam<Schedule> schedule{"schedule",
                                      /* default value */
                                      Schedule::Basic,
                                      /* map from names to values */
                                      {{"none", Schedule::Basic},
                                       {"fold", Schedule::Fold},
                                       {"async", Schedule::Async},
                                       {"split", Schedule::Split},
                                       {"split_async", Schedule::Split_Async}}};

    GeneratorParam<bool> use_dma_for_output{"use_dma_for_output", true};

    void generate() {
        // Y and UV need to be the same type (?).
        assert(input_y.type() == input_uv.type());
        assert(output_y.type() == output_uv.type());

        Var x{"x"}, y{"y"}, c{"c"};

        // We could use 'in' to generate the input copies, but we can't name the variables that way.
        Func input_y_copy("input_y_copy"), input_uv_copy("input_uv_copy");

        Func work_y("work_y");
        Func work_uv("work_uv");

        input_y_copy(x, y) = input_y(x, y);
        work_y(x, y) = input_y_copy(x, y) * 2;
        output_y(x, y) = work_y(x, y);

        input_uv_copy(x, y, c) = input_uv(x, y, c);
        work_uv(x, y, c) = input_uv_copy(x, y, c) * 2;
        output_uv(x, y, c) = work_uv(x, y, c);

        Var tx("tx"), ty("ty");

        // Do some common scheduling here.
        if (use_dma_for_output) {
            output_y.copy_to_device();
            output_uv.copy_to_device();
        }

        output_y
            .compute_root();

        output_uv
            .compute_root()
            .bound(c, 0, 2)
            .reorder(c, x, y);

        // tweak stride/extent to handle UV deinterleaving
        input_uv.dim(0).set_stride(2);
        input_uv.dim(2).set_stride(1).set_bounds(0, 2);
        output_uv.dim(0).set_stride(2);
        output_uv.dim(2).set_stride(1).set_bounds(0, 2);

        // Break the output into tiles.
        const int bytes_per_pixel = std::max(input_y.type().bytes(), output_y.type().bytes());
        const int tile_width = 128 / bytes_per_pixel;
        const int tile_height = 32;

        switch ((Schedule)schedule) {
        case Schedule::Basic:
        default:
            output_y
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            output_uv
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            input_y_copy
                .compute_at(output_y, tx)
                .copy_to_host();

            input_uv_copy
                .compute_at(output_uv, tx)
                .copy_to_host()
                .reorder_storage(c, x, y);
            break;
        case Schedule::Fold:
            output_y
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            output_uv
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            input_y_copy
                .copy_to_host()
                .compute_at(output_y, tx)
                .store_at(output_y, ty)
                .fold_storage(x, tile_width * 2);

            input_uv_copy
                .copy_to_host()
                .compute_at(output_uv, tx)
                .store_at(output_uv, ty)
                .reorder_storage(c, x, y)
                .fold_storage(x, tile_width * 2);
            break;
        case Schedule::Async:
            output_y
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            output_uv
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            input_y_copy
                .copy_to_host()
                .async()
                .compute_at(output_y, tx)
                .store_at(output_y, ty)
                .fold_storage(x, tile_width * 2);

            input_uv_copy
                .copy_to_host()
                .async()
                .compute_at(output_uv, tx)
                .store_at(output_uv, ty)
                .reorder_storage(c, x, y)
                .fold_storage(x, tile_width * 2);
            break;
        case Schedule::Split: {
            Var yo, yi;

            Expr fac_y = output_y.dim(1).extent() / 2;
            output_y
                .split(y, yo, yi, fac_y)
                .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                .parallel(yo);

            Expr fac_uv = output_uv.dim(1).extent() / 2;
            output_uv
                .split(y, yo, yi, fac_uv)
                .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                .parallel(yo);

            input_y_copy
                .copy_to_host()
                .compute_at(output_y, tx);

            input_uv_copy
                .copy_to_host()
                .compute_at(output_uv, tx)
                .reorder_storage(c, x, y);
        } break;
        case Schedule::Split_Async: {
            Var yo, yi;

            Expr fac_y = output_y.dim(1).extent() / 2;
            output_y
                .split(y, yo, yi, fac_y)
                .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                .parallel(yo);

            Expr fac_uv = output_uv.dim(1).extent() / 2;
            output_uv
                .split(y, yo, yi, fac_uv)
                .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                .parallel(yo);

            input_y_copy
                .copy_to_host()
                .compute_at(output_y, tx)
                .store_at(output_y, ty)
                .async()
                .fold_storage(x, tile_width * 2);

            input_uv_copy
                .copy_to_host()
                .compute_at(output_uv, tx)
                .store_at(output_uv, ty)
                .async()
                .reorder_storage(c, x, y)
                .fold_storage(x, tile_width * 2);
        } break;
        }

        // async tiled output
        if (use_dma_for_output && ((Schedule)schedule == Schedule::Async || (Schedule)schedule == Schedule::Split_Async)) {
            work_y
                .async()
                .store_at(output_y, ty)
                .fold_storage(x, tile_width * 2);

            work_uv
                .async()
                .store_at(output_uv, ty)
                .fold_storage(x, tile_width * 2);
        }

        // Schedule the work in tiles (same for all DMA schedules).
        work_y.compute_at(output_y, tx);

        work_uv
            .compute_at(output_uv, tx)
            .bound(c, 0, 2)
            .reorder_storage(c, x, y);
    }
};

HALIDE_REGISTER_GENERATOR(DmaPipeline, pipeline_yuv_linear_basic)
