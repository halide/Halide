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
        Func copy("copy");

        copy(x, y) = input(x, y);

        output(x, y) = copy(x, y) * 2;

        Var tx("tx"), ty("ty");

        // Break the output into tiles.
        const int tile_width = 256;
        const int tile_height = 128;

        output
            .compute_root()
            .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

        // Schedule the copy to be computed at tiles with a
        // circular buffer of two tiles.
        copy
            .compute_at(output, tx)
            .store_root()
            .fold_storage(x, tile_width * 2)
            .copy_to_host();
    }

};

HALIDE_REGISTER_GENERATOR(DmaPipeline, dma_pipeline)
