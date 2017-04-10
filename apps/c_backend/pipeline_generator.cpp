#include "Halide.h"

namespace {

// Compile a simple pipeline to an object and to C code.
HalideExtern_2(int, an_extern_func, int, int);

class Pipeline : public Halide::Generator<Pipeline> {
public:
    ImageParam input{UInt(16), 2, "input"};
    Func build() {
        Var x, y;

        Func f, g, h;
        f(x, y) = (input(clamp(x+2, 0, input.width()-1), clamp(y-2, 0, input.height()-1)) * 17)/13;
        h.define_extern("an_extern_stage", {f}, Int(16), 0, NameMangling::C);
        g(x, y) = cast<uint16_t>(f(y, x) + f(x, y) + an_extern_func(x, y) + h());

        f.compute_root();
        h.compute_root();

        return g;
    }
};

Halide::RegisterGenerator<Pipeline> register_me{"pipeline"};

}  // namespace
