#include "Halide.h"

#if HALIDE_PREFER_G2_GENERATORS

namespace {

using namespace Halide;

Func Alias(Func input, int offset) {
    Var x;

    Func output("output");
    output(x) = input(x) + offset;

    return output;
}

}  // namespace

HALIDE_REGISTER_G2(
    Alias,  // actual C++ fn
    alias,  // build-system name
    Input("input", Int(32), 1),
    Constant("offset", 0),
    Output("output", Int(32), 1))

HALIDE_REGISTER_G2(
    Alias,                 // actual C++ fn
    alias_with_offset_42,  // build-system name
    Input("input", Int(32), 1),
    Constant("offset", 42),
    Output("output", Int(32), 1))

#else

namespace {

class Alias : public Halide::Generator<Alias> {
public:
    GeneratorParam<int32_t> offset{"offset", 0};
    Input<Buffer<int32_t, 1>> input{"input"};
    Output<Buffer<int32_t, 1>> output{"output"};

    void generate() {
        Var x;
        output(x) = input(x) + offset;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Alias, alias)
HALIDE_REGISTER_GENERATOR_ALIAS(alias_with_offset_42, alias, {{"offset", "42"}})

#endif
