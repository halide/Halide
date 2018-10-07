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
        Var x("x"), y("y"), c("c"); 
        Func input_copy("input_copy"), output_copy("output_copy");
        Func work("work");

        input_copy(x, y, c) = input(x, y, c);
        work(x, y, c) = input_copy(x, y, c) * 2;
        output_copy(x, y, c) = work(x, y, c);
        output(x, y, c) = output_copy(x, y, c);

        Var tx("tx"), ty("ty");

        // Break the output into tiles.
        const int tile_width = 128;
        const int tile_height = 32;

        switch ((UserOptions)options) {
            case UserOptions::Basic:
            default:
                output.compute_root()
                      .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);
                Stage(output).set_dim_device_api(tx, DeviceAPI::HexagonDma);

                input_copy.compute_at(output, tx)
                          .copy_to_host();

                work.compute_at(output, tx);

                output_copy.compute_at(output, tx)
                           .copy_to_device();
            break;
            case UserOptions::Fold:
                output.compute_root()
                      .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);
                Stage(output).set_dim_device_api(tx, DeviceAPI::HexagonDma);

                input_copy.compute_at(output, tx)
                          .copy_to_host()
                          .fold_storage(x, tile_width * 2);

                work.compute_at(output, tx);

                output_copy.compute_at(output, tx)
                           .copy_to_device();
            break;
            case UserOptions::Async:
                output.compute_root()
                      .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);
                Stage(output).set_dim_device_api(tx, DeviceAPI::HexagonDma);

                input_copy.compute_at(output, tx)
                          .copy_to_host()
                          .async()
                          .fold_storage(x, tile_width * 2);

                work.compute_at(output, tx);

                output_copy.compute_at(output, tx)
                           .copy_to_device();
            break;
            case UserOptions::Split: {
                Expr fac = output.dim(1).extent()/2;
                Var yo, yi;
                output.split(y, yo, yi, fac);
                output.compute_root()
                      .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                      .parallel(yo);

                Stage(output).set_dim_device_api(tx, DeviceAPI::HexagonDma);

                input_copy.compute_at(output, tx)
                          .copy_to_host();

                work.compute_at(output, tx);

                output_copy.compute_at(output, tx)
                           .copy_to_device();
            }
            break;
            case UserOptions::Split_Fold: {
                Expr fac = output.dim(1).extent()/2;
                Var yo, yi;
                output.split(y, yo, yi, fac);
                output.compute_root()
                      .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
                      .parallel(yo);

                Stage(output).set_dim_device_api(tx, DeviceAPI::HexagonDma);

                input_copy.compute_at(output, tx)
                          .copy_to_host()
                          .async()
                          .fold_storage(x, tile_width * 2);

                work.compute_at(output, tx);

                output_copy.compute_at(output, tx)
                           .copy_to_device();
            }
            break;
        }
    }
};

HALIDE_REGISTER_GENERATOR(DmaPipeline, pipeline_raw_linear_rw_basic_planar)
