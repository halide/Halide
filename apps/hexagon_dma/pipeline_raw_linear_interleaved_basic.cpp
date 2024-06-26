#include "Halide.h"

using namespace Halide;

// Generate a pipeline that reads 4-channel data via DMA, scales it by
// 2, and (optionally) writes it back via DMA.
class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint8_t, 3>> input{"input"};
    Output<Buffer<uint8_t, 3>> output{"output"};

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
        Var x{"x"}, y{"y"}, c{"c"};

        // We need a wrapper for the output so we can schedule the
        // multiply update in tiles.
        Func input_copy("input_copy");

        Func work("work");
        input_copy(x, y, c) = input(x, y, c);
        work(x, y, c) = input_copy(x, y, c) * 2;
        output(x, y, c) = work(x, y, c);

        Var tx("tx"), ty("ty");

        // Do some common scheduling here.
        if (use_dma_for_output) {
            output.copy_to_device();
        }

        output
            .compute_root()
            .bound(c, 0, 4)
            .reorder(c, x, y);
        input.dim(0).set_stride(4);
        output.dim(0).set_stride(4);
        // Break the output into tiles.
        const int bytes_per_pixel = std::max(input.type().bytes(), output.type().bytes());
        const int tile_width = 128 / bytes_per_pixel;
        const int tile_height = 32;

        switch ((Schedule)schedule) {
        case Schedule::Basic:
        default:
            output
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            input_copy
                .compute_at(output, tx)
                .copy_to_host()
                .reorder_storage(c, x, y);
            break;
        case Schedule::Fold:
            output
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            input_copy
                .copy_to_host()
                .compute_at(output, tx)
                .store_at(output, ty)
                .reorder_storage(c, x, y)
                .fold_storage(x, tile_width * 2);
            break;
        case Schedule::Async:
            output
                .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

            input_copy
                .copy_to_host()
                .async()
                .compute_at(output, tx)
                .store_at(output, ty)
                .reorder_storage(c, x, y)
                .fold_storage(x, tile_width * 2);
            break;
        case Schedule::Split: {
            Var yo, yi;
            Expr fac = output.dim(1).extent() / 2;
            output
                .split(y, yo, yi, fac)
                .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                .parallel(yo);

            input_copy
                .copy_to_host()
                .compute_at(output, tx)
                .reorder_storage(c, x, y);
        } break;
        case Schedule::Split_Async: {
            Var yo, yi;
            Expr fac = output.dim(1).extent() / 2;
            output
                .split(y, yo, yi, fac)
                .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                .parallel(yo);

            input_copy
                .copy_to_host()
                .compute_at(output, tx)
                .store_at(output, ty)
                .async()
                .reorder_storage(c, x, y)
                .fold_storage(x, tile_width * 2);
        } break;
        }

        // async tiled output
        if (use_dma_for_output && ((Schedule)schedule == Schedule::Async || (Schedule)schedule == Schedule::Split_Async)) {
            work
                .async()
                .store_at(output, ty)
                .fold_storage(x, tile_width * 2);
        }

        // Schedule the work in tiles (same for all DMA schedules).
        work
            .compute_at(output, tx)
            .bound(c, 0, 4)
            .reorder_storage(c, x, y);
    }
};

HALIDE_REGISTER_GENERATOR(DmaPipeline, pipeline_raw_linear_interleaved_basic)
