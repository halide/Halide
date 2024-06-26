#include "Halide.h"

namespace {

// Compile a simple pipeline to an object and to C code.
HalideExtern_2(int, an_extern_func, int, int);

class Pipeline : public Halide::Generator<Pipeline> {
public:
    Input<Buffer<uint16_t, 2>> input{"input"};
    Output<Buffer<uint16_t, 2>> output{"output"};

    void generate() {
        Var x, y;

        Func f, h;
        f(x, y) = (input(clamp(x + 2, 0, input.dim(0).extent() - 1), clamp(y - 2, 0, input.dim(1).extent() - 1)) * 17) / 13;
        h.define_extern("an_extern_stage", {f}, Int(16), 0, NameMangling::C);
        output(x, y) = cast<uint16_t>(max(0, f(y, x) + f(x, y) + an_extern_func(x, y) + h()));

        f.compute_root().vectorize(x, 8);
        h.compute_root();
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Pipeline, pipeline)
