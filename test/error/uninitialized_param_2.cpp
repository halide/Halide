#include "Halide.h"
#include "halide_test_dirs.h"
#include "halide_test_error.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

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

namespace {
void TestUninitializedParam2() {
    Halide::Internal::ExecuteGeneratorArgs args;
    args.output_dir = Internal::get_test_tmp_dir();
    args.output_types = std::set<OutputFileType>{OutputFileType::object};
    args.targets = std::vector<Target>{get_target_from_environment()};
    args.generator_name = "PleaseFail";
    execute_generator(args);
}
}  // namespace

TEST(ErrorTests, UninitializedParam2) {
    EXPECT_COMPILE_ERROR(TestUninitializedParam2, MatchesPattern(R"(Parameter scalar_input does not have a valid scalar value\.)"));
}
