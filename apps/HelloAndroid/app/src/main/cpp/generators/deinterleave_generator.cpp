#include "Halide.h"

namespace {

class Deinterleave : public Halide::Generator<Deinterleave> {
public:
    Input<Buffer<uint8_t, 2>> uvInterleaved{"uvInterleaved"};
    // There is no way to declare a Buffer<Tuple>, so we must use Output<Func> instead
    Output<Func> result{"result", {UInt(8), UInt(8)}, 2};

    void generate() {
        Var x, y;

        result(x, y) = {uvInterleaved(2 * x, y), uvInterleaved(2 * x + 1, y)};

        // CPU schedule:
        //   Parallelize over scan lines, 4 scanlines per task.
        //   Independently, vectorize over x.
        result
            .parallel(y, 4)
            .vectorize(x, natural_vector_size(UInt(8)));

        // Cope with rotated inputs
        uvInterleaved.dim(0).set_stride(Expr());
        result.specialize(uvInterleaved.dim(0).stride() == 1);
        result.specialize(uvInterleaved.dim(0).stride() == -1);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Deinterleave, deinterleave)
