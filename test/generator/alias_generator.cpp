#include "Halide.h"

namespace {

class Alias : public Halide::Generator<Alias> {
public:
    GeneratorParam<int32_t> offset{"offset", 0};
    Input<Buffer<int32_t, 1>> input{"input"};
    Output<Buffer<int32_t, 1>> output{"output"};

    void generate() {
        Var x;
        output(x) = input(x) + offset;

        // set estimates for the autoschedulers
        input.set_estimates({{0, 32}});
        output.set_estimates({{0, 32}});

        if (!using_autoscheduler()) {
            // Don't really need a default schedule for something this simple, but sure, why not
            output.vectorize(x, natural_vector_size<int32_t>()).compute_root();
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Alias, alias)
HALIDE_REGISTER_GENERATOR_ALIAS(alias_with_offset_42, alias, {{"offset", "42"}})
// Since autoscheduler-to-use is an ordinary GeneratorParam, we can specify it in Aliases for convenience.
// (Set unique offsets just to verify these are all separate calls.)
HALIDE_REGISTER_GENERATOR_ALIAS(alias_Adams2019, alias, {{"autoscheduler", "Adams2019"}, {"offset", "2019"}})
HALIDE_REGISTER_GENERATOR_ALIAS(alias_Li2018, alias, {{"autoscheduler", "Li2018"}, {"offset", "2018"}})
HALIDE_REGISTER_GENERATOR_ALIAS(alias_Mullapudi2016, alias, {{"autoscheduler", "Mullapudi2016"}, {"offset", "2016"}})
