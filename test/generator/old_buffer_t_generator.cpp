#include "Halide.h"

// This is a test of using the old buffer_t struct and having it
// auto-upgrade to the new one. We need some generator for which
// bounds inference does something, and with an extern definition.
class OldBufferT : public Halide::Generator<OldBufferT> {
public:
    ImageParam in1 {Int(32), 2, "in1"};
    ImageParam in2 {Int(32), 2, "in2"};
    Param<int> scalar_param {"scalar_param", 1, 0, 64};

    Func build() {
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
        return g;
    }
};

Halide::RegisterGenerator<OldBufferT> reg{"old_buffer_t"};
