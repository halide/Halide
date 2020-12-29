#include "Halide.h"

namespace {

class Alias : public Halide::Generator<Alias> {
public:
    GeneratorParam<int32_t> offset{"offset", 0};
    Input<Buffer<int32_t>> input{"input", 1};
    Output<Buffer<int32_t>> output{"output", 1};

    void generate() {
        Var x;
        output(x) = input(x) + offset;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Alias, alias)
HALIDE_REGISTER_GENERATOR_ALIAS(alias_with_offset_42, alias, {{"offset", "42"}})
