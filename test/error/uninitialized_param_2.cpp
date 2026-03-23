#include "Halide.h"
#include "halide_test_dirs.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

#include "Halide.h"

namespace {

Var x;

class PleaseFail : public Halide::Generator<PleaseFail> {
public:
    Input<Buffer<uint8_t, 1>> input{"input"};
    Input<float> scalar_input{"scalar_input"};
    Output<Buffer<uint8_t, 1>> output{"output"};

    void generate() {
        Func lut_fn("lut_fn");
        lut_fn(x) = u8_sat(x * scalar_input / 255.f);

        // This should always fail, because it depends on a scalar input
        // that *cannot* have a valid value at this point.
        auto lut = lut_fn.realize({256});

        output(x) = input(x) + lut[0](x);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(PleaseFail, PleaseFail)

int main(int argc, char **argv) {
    Halide::Internal::ExecuteGeneratorArgs args;
    args.output_dir = Internal::get_test_tmp_dir();
    args.output_types = std::set<OutputFileType>{OutputFileType::object};
    args.targets = std::vector<Target>{get_target_from_environment()};
    args.generator_name = "PleaseFail";
    execute_generator(args);

    printf("Success!\n");
    return 0;
}
