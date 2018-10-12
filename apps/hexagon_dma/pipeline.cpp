#include "Halide.h"

using namespace Halide;

class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    void generate() {
        Var x{"x"}, y{"y"};

        Func work("work");
        work(x, y) = input(x, y) * 2;

        output(x, y) = work(x, y);

        Var tx("tx"), ty("ty");

        // Break the output into tiles.
        const int tile_width = 256;
        const int tile_height = 128;

        Expr fac = output.dim(1).extent()/2;

        // The output is a tiled DMA write.
        output.compute_root()
            .copy_to_device()
            .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

        // Schedule the work to be computed at tiles of the output.
        work.compute_at(output, tx);

        // Read the input with a DMA read.
        input.in().compute_at(output, tx)
            .copy_to_host();
    }
};

HALIDE_REGISTER_GENERATOR(DmaPipeline, dma_pipeline)
