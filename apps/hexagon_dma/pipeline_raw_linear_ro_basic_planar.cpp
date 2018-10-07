#include "Halide.h"

using namespace Halide;

class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint8_t>> input{"input", 3};
    Output<Buffer<uint8_t>> output{"output", 3};

    enum class UserOptions { Basic, Fold, Async, Split, Split_Fold };
    GeneratorParam<UserOptions> options{"options",
            /* default value */
             UserOptions::Basic,
            /* map from names to values */
            {{ "none", UserOptions::Basic },
             { "fold", UserOptions::Fold },
             { "async", UserOptions::Async },
             { "split", UserOptions::Split },
             { "split_fold", UserOptions::Split_Fold }}};

    void generate() {
        Var x{"x"}, y{"y"}, c{"c"};

        // We need a wrapper for the output so we can schedule the
        // multiply update in tiles.
        Func copy("copy");

        copy(x, y, c) = input(x, y, c);

        output(x, y, c) = copy(x, y, c) * 2;

        Var tx("tx"), ty("ty");
        Var ta("ta"), tb("tb");

        // Break the output into tiles.
        const int tile_width = 128;
        const int tile_height = 32;

        switch ((UserOptions)options) {
            case UserOptions::Basic:
            default:
                output.compute_root()
                      .tile(x, y, tx, ty, ta, tb, tile_width, tile_height, TailStrategy::RoundUp);

                copy.compute_at(output, tx)
                    .store_at(output, tx)
                    .copy_to_host();
            break;
            case UserOptions::Fold:
                output.compute_root()
                      .tile(x, y, tx, ty, ta, tb, tile_width, tile_height, TailStrategy::RoundUp);

                copy.compute_at(output, tx)
                    .store_at(output, tx)
                    .copy_to_host()
                    .fold_storage(x, tile_width * 2);
            break;
            case UserOptions::Async:
                output.compute_root()
                      .tile(x, y, tx, ty, ta, tb, tile_width, tile_height, TailStrategy::RoundUp);

                copy.compute_at(output, tx)
                    .store_at(output, tx)
                    .copy_to_host()
                    .async()
                    .fold_storage(x, tile_width * 2);
            break;
            case  UserOptions::Split: {
                Expr fac = output.dim(1).extent()/2;
                Var yo, yi;
                output.split(y, yo, yi, fac);

                output.compute_root()
                      .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                      .parallel(yo);

                copy.compute_at(output, tx)
                    .store_at(output, ty)
                    .copy_to_host();
            }
            break;
            case  UserOptions::Split_Fold: {
                Expr fac = output.dim(1).extent()/2;
                Var yo, yi;
                output.split(y, yo, yi, fac);

                output.compute_root()
                      .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                      .parallel(yo);

                copy.compute_at(output, tx)
                    .store_at(output, ty)
                    .copy_to_host()
                    .async()
                    .fold_storage(x, tile_width * 2);
            }
            break;
        }
    }
};

HALIDE_REGISTER_GENERATOR(DmaPipeline, pipeline_raw_linear_ro_basic_planar)
