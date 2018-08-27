#include "Halide.h"

using namespace Halide;

class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint8_t>> input{"input", 3};
    Output<Buffer<uint8_t>> output{"output", 3};

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
        const int tile_width = 64;
        const int tile_height = 32;

        Expr fac = output.dim(1).extent()/2;
        Var yo, yi;
        output.split(y, yo, yi, fac);
        output.compute_root()
              .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
              .parallel(yo);
        Stage(output)
            .set_dim_device_api(tx, DeviceAPI::HexagonDma);

        // Schedule the copy to be computed at tiles with a
        // circular buffer of two tiles.
        input_copy.compute_at(output, tx)
                  .store_at(output, ty)
                  .copy_to_host().fold_storage(x, tile_width *2);
 
       work.compute_at(output, tx);

        output_copy.compute_at(output, tx)
                   .copy_to_device();

    }

};

HALIDE_REGISTER_GENERATOR(DmaPipeline, pipeline_raw_linear_rw_split_fold_planar)
