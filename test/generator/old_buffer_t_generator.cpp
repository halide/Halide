#include "Halide.h"

// This is a test of using the old buffer_t struct and having it
// auto-upgrade to the new one. We need some generator for which
// bounds inference does something, and with an extern definition.
class OldBufferT : public Halide::Generator<OldBufferT> {
public:
    Input<Buffer<int32_t>> in1{"in1", 2};
    Input<Buffer<int32_t>> in2{"in2", 2};
    Input<int>             scalar_param{"scalar_param", 1, 0, 64};

    Output<Buffer<int32_t>>  output{"output", 2};

    void generate() {
        Func f, g;
        Var x, y;
        f(x, y) = in1(x-1, y-1) + in1(x+1, y+3) + in2(x, y) + scalar_param;
        f.compute_root();

        if (get_target().has_gpu_feature()) {
            Var xi, yi;
            f.gpu_tile(x, y, xi, yi, 16, 16);
        }

        g.define_extern("extern_stage", {in2, f}, Int(32), 2,
                        NameMangling::Default,
                        true /* uses old buffer_t */);

        // Schedule the extern stage per tile of the output to give
        // the buffers a non-trivial min
        output(x, y) = g(x, y);
        Var xi, yi;
        output.tile(x, y, xi, yi, 8, 8);
        g.compute_at(output, x);
    }
};

HALIDE_REGISTER_GENERATOR(OldBufferT, old_buffer_t)
