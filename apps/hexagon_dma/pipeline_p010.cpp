#include "Halide.h"

using namespace Halide;

class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint16_t>> input{"input", 3};
    Output<Buffer<uint16_t>> output_y{"output_y", 3};
    Output<Buffer<uint16_t>> output_uv{"output_uv", 3};

    void generate() {
        Var x{"x"}, y{"y"}, c{"c"};

        // We need a wrapper for the output so we can schedule the
        // multiply update in tiles.
        Func copy_y("copy_y");
        Func copy_uv("copy_uv");

        copy_y(x, y, c) = input(x, y, c);
        copy_uv(x, y, c) = input(x, y, c);

        output_y(x, y, c) = copy_y(x, y, c) * 2;
        output_uv(x, y, c) = copy_uv(x, y, c) * 2;

        Var tx("tx"), ty("ty");

        // Break the output into tiles.
        const int tile_width = 256;
        const int tile_height = 128;

        output_y
            .compute_root()
            .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

        output_uv
            .compute_root()
            .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

        // Schedule the copy to be computed at tiles with a
        // circular buffer of two tiles.
        copy_y
            .compute_at(output_y, tx)
            .store_root()
            .fold_storage(x, tile_width * 2)
            .copy_to_host();

        copy_uv
            .compute_at(output_uv, tx)
            .store_root()
            .fold_storage(x, tile_width * 2)
            .copy_to_host();
    }

};

HALIDE_REGISTER_GENERATOR(DmaPipeline, dma_pipeline_p010)
