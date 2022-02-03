#include "Halide.h"

#if HALIDE_PREFER_G2_GENERATORS

namespace {

using namespace Halide;

Func ArgvCall(Target target, Expr f1, Expr f2) {
    Var x, y, c;

    Func f("f");
    f(x, y) = max(x, y);

    Func output("output");
    output(x, y, c) = cast<int32_t>(f(x, y) * c * f1 / f2);

    output.bound(c, 0, 3).reorder(c, x, y).unroll(c);
    output.vectorize(x, target.natural_vector_size<float>());

    return output;
}

}  // namespace

HALIDE_REGISTER_G2(
    ArgvCall,  // actual C++ fn
    argvcall,  // build-system name
    Target(),
    Input("f1", Float(32)),     // Can just omit dimension arg to declare a scalar input...
    Input("f2", Float(32), 0),  // ...or use 0 to explicitly denote scalar input
    Output("output", Int(32), 3))

#else

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

#endif
