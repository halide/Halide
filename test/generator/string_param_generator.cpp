#include "Halide.h"

using namespace Halide;

namespace {
class StringParam : public Halide::Generator<StringParam> {
public:
    GeneratorParam<std::string> param{"param", ""};

    Input<Buffer<float>> input{ "input", 1 };
    Output<Buffer<float>> output{ "output", 1 };

    void generate() {
        Var x;
        if (param.value() == "add_one") {
            output(x) = 1 + input(x);
        } else {
            output(x) = input(x);
        }
    }
};
}  // namespace

HALIDE_REGISTER_GENERATOR(StringParam, string_param);
