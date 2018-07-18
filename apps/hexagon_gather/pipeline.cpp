#include "Halide.h"
#include "process.h"
using namespace Halide;

// To use scatter-gathers add hvx_v65, hvx_scatter and hvx_gather
// features to the target. Set HL_VTCM_SIZE environment value
// to appropriate size (example 65536 -> 64KB)
class Gather : public Halide::Generator<Gather> {
public:

    Input<Buffer<DTYPE>> input{"input", 1};
    Input<Buffer<DTYPE>> lut{"lut", 1};
    Output<Buffer<DTYPE>> output{"output", 1};

    void generate() {
        output(x) = cast<DTYPE>(lut(clamp(input(x), 0, TBL_SIZE-1)));
    }

    void schedule() {
        input.dim(0).set_min(0);
        output.dim(0).set_min(0);
        lut.dim(0).set_min(0);
        lut.dim(0).set_extent(TBL_SIZE);

        if (get_target().features_any_of({Target::HVX_64, Target::HVX_128})) {
            const int vector_size = get_target().has_feature(Target::HVX_128) ? 128 : 64;
            // Set the expected alignment of the host pointer in bytes.
            input.set_host_alignment(vector_size);
            output.set_host_alignment(vector_size);

            output
                .hexagon()
                .vectorize(x, vector_size);
        }
    }
private:
    Var x{"x"};

};
HALIDE_REGISTER_GENERATOR(Gather, gather);
