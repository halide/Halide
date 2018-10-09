#include "Halide.h"

using namespace Halide;

class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    void generate() {
        Var x{"x"}, y{"y"};

        // We need a wrapper for the output so we can schedule the
        // multiply update in tiles.
        Func input_copy("copy");
        Func output_copy("copy");
        Func work("copy");

        input_copy(x, y) = input(x, y);
        work(x, y) = input_copy(x, y) * 2;
        output_copy(x, y) = work(x, y);
        output(x, y) = output_copy(x, y);

        Var tx("tx"), ty("ty");

        // Break the output into tiles.
        const int tile_width = 256;
        const int tile_height = 128;

        Expr fac = output.dim(1).extent()/2;


        output.compute_root()
              .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);
        Stage(output)
            .set_dim_device_api(tx, DeviceAPI::HexagonDma);

        // Schedule the copy to be computed at tiles with a
        // circular buffer of two tiles.
        input_copy.compute_at(output, tx)
                  .copy_to_host();

        work.compute_at(output, tx);

        output_copy.compute_at(output, tx)
                   .copy_to_device();

    }

};

HALIDE_REGISTER_GENERATOR(DmaPipeline, dma_pipeline)
