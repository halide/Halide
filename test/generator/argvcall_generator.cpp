#include "Halide.h"

namespace {

class ArgvCall : public Halide::Generator<ArgvCall> {
public:
    Param<float> f1{ "f1", 1.0 };
    Param<float> f2{ "f2", 1.0 };

    Func build() {
        Var x, y, c;
        Func f("f"), g("g");

        f(x, y) = max(x, y);
        g(x, y, c) = cast<int32_t>(f(x, y) * c * f1 / f2);

        g.bound(c, 0, 3).reorder(c, x, y).unroll(c);

        g.vectorize(x, natural_vector_size<float>());

        return g;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ArgvCall, argvcall)
