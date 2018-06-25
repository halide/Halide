#include "Halide.h"
#include "process.h"
using namespace Halide;

// To use scatter-gathers add hvx_v65, hvx_scatter and hvx_gather
// features to the target. Set HL_VTCM_SIZE environment value
// to appropriate size (example 65536 -> 64KB)
class Histogram : public Halide::Generator<Histogram> {
public:

    Input<Buffer<uint16_t>> input{"input", 1};
    Output<Buffer<HIST_TYPE>> output{"output", 1};

    void generate() {
        Expr img_size = input.dim(0).extent();

        Func histogram("histogram");
        histogram(x) = cast<HIST_TYPE>(0);
        RDom r(0, img_size);

        Expr idx = clamp(input(r.x), 0, cast<uint16_t>(HIST_SIZE-1));
        histogram(idx) += cast<HIST_TYPE>(1);
        // Wrapper for the output
        output(x) = histogram(x);

        const int vector_size = get_target().has_feature(Target::HVX_128) ? 128 : 64;

        histogram.vectorize(x, vector_size);
        if (get_target().has_feature(Target::HVX_v65) &&
            get_target().has_feature(Target::HVX_scatter)) {
            histogram
                .update(0)
                .allow_race_conditions()
                .vectorize(r.x, vector_size);
        }
    }

    void schedule() {
        input.dim(0).set_min(0);
        input.dim(0).set_extent(IMG_SIZE);
        output.dim(0).set_min(0);
        output.dim(0).set_extent(HIST_SIZE);

        if (get_target().features_any_of({Target::HVX_64, Target::HVX_128})) {
            const int vector_size = get_target().has_feature(Target::HVX_128) ? 128 : 64;
            // Set the expected alignment of the host pointer in bytes.
            input.set_host_alignment(vector_size);
            output.set_host_alignment(vector_size);

            output
                .hexagon()
#if 0 // Don't vectorize output, curently causes scatter stage to not vectorize
                .vectorize(x, vector_size)
#endif
                ;
        }
    }
private:
    Var x{"x"}, y{"y"};
};

HALIDE_REGISTER_GENERATOR(Histogram, histogram);
