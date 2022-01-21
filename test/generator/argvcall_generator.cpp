#include "Halide.h"

namespace {

class ArgvCall : public Halide::Generator<ArgvCall> {
public:
    Input<float> f1{"f1", 1.0};
    Input<float> f2{"f2", 1.0};
    Output<Buffer<int32_t, 3>> output{"output"};

    void generate() {
        Var x, y, c;
        Func f("f");

        f(x, y) = max(x, y);
        output(x, y, c) = cast<int32_t>(f(x, y) * c * f1 / f2);

        output.bound(c, 0, 3).reorder(c, x, y).unroll(c);

        output.vectorize(x, natural_vector_size<float>());
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ArgvCall, argvcall)
