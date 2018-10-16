#include "Halide.h"

using namespace Halide;

// Generate a pipeline that reads 4-channel data via DMA, scales it by
// 2, and writes it back (without DMA).
class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint8_t>> input{"input", 3};
    Output<Buffer<uint8_t>> output{"output", 3};

    enum class Schedule { Basic, Fold, Async, Split, Split_Fold };
    GeneratorParam<Schedule> schedule{"schedule",
            /* default value */
             Schedule::Basic,
            /* map from names to values */
            {{ "none", Schedule::Basic },
             { "fold", Schedule::Fold },
             { "async", Schedule::Async },
             { "split", Schedule::Split },
             { "split_fold", Schedule::Split_Fold }}};

    void generate() {
        Var x{"x"}, y{"y"}, c{"c"};

        // We need a wrapper for the output so we can schedule the
        // multiply update in tiles.
        Func input_copy("input_copy");

        input_copy(x, y, c) = input(x, y, c);

        output(x, y, c) = input_copy(x, y, c) * 2;


        Var tx("tx"), ty("ty");


        output
            .compute_root()
            .bound(c, 0, 4)
            .reorder(c, x, y);
        input.dim(0).set_stride(4);
        output.dim(0).set_stride(4);  
        // Break the output into tiles.
        const int tile_width = 128;
        const int tile_height = 32;

        switch ((Schedule)schedule) {
            case Schedule::Basic:
            default:
                output
                    .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

                input_copy
                    .compute_at(output, tx)
                    .copy_to_host()
                    .bound(c, 0, 4)
                    .reorder_storage(c, x, y);
            break;
            case Schedule::Fold:
                output
                    .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

                input_copy
                    .copy_to_host()
                    .compute_at(output, tx)
                    .store_at(output, ty)
                    .bound(c, 0, 4)
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
                    .bound(c, 0, 4)
                    .reorder_storage(c, x, y)
                    .fold_storage(x, tile_width * 2);
            break;
            case Schedule::Split: {
                Var yo, yi;
                Expr fac = output.dim(1).extent()/2;
                output
                    .split(y, yo, yi, fac)
                    .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                    .parallel(yo);

                input_copy
                    .copy_to_host()
                    .compute_at(output, tx)
                    .bound(c, 0, 4)
                    .reorder_storage(c, x, y);
            }
            break;
            case Schedule::Split_Fold: {
                Var yo, yi;
                Expr fac = output.dim(1).extent()/2;
                output
                    .split(y, yo, yi, fac)
                    .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                    .parallel(yo);

                input_copy
                    .copy_to_host()
                    .compute_at(output, tx)
                    .store_at(output, ty)
                    .async()
                    .bound(c, 0, 4)
                    .reorder_storage(c, x, y)
                    .fold_storage(x, tile_width * 2);
            }
            break;
        }
    }
};

HALIDE_REGISTER_GENERATOR(DmaPipeline, pipeline_raw_linear_interleaved_ro_basic)
