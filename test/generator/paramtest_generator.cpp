#include "Halide.h"

namespace {

// This Generator exists solely to do testing of GeneratorParam/ImageParam/Param
// introspection; the actual operation done in build() matters very little
// (except for setting the type of the input image, which is critical)
class ParamTest : public Halide::Generator<ParamTest> {
public:
    GeneratorParam<Type> input_type{ "input_type", UInt(8) };
    GeneratorParam<Type> output_type{ "output_type", Float(32) };

    ImageParam input{ UInt(8), 3, "input" };
    Param<float> float_arg{ "float_arg", 1.0f, 0.0f, 100.0f };
    Param<int32_t> int_arg{ "int_arg", 1 };

    Pipeline build() {
        input = ImageParam(input_type, input.dimensions(), input.name());

        Var x, y, c;

        Func f;
        f(x, y, c) = Tuple(
                input(x, y, c),
                cast(output_type, input(x, y, c) * float_arg + int_arg));

        Func g;
        g(x, y) = cast<int16_t>(input(x, y, 0));

        return Pipeline({f, g});
    }
};

Halide::RegisterGenerator<ParamTest> register_paramtest{"paramtest"};

}  // namespace
