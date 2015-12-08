#include "Halide.h"

namespace {

// This Generator exists solely to do testing of GeneratorParam/ImageParam/Param
// introspection; the actual operation done in build() matters very little
// (except for setting the type of the input image, which is critical)
// This test also verifies that various operators using GeneratorPAram work.
class ParamTest : public Halide::Generator<ParamTest> {
public:
    GeneratorParam<Type> input_type{ "input_type", UInt(8) };
    GeneratorParam<Type> output_type{ "output_type", Float(32) };

    GeneratorParam<float> float_gen_arg{ "float_val", 1.0f };
    GeneratorParam<int32_t> int_gen_arg{ "int_val", 1 };
    GeneratorParam<bool> bool_gen_arg{ "bool_val", true };

    ImageParam input{ UInt(8), 3, "input" };
    Param<float> float_arg{ "float_arg", 1.0f, 0.0f, 100.0f };
    Param<int32_t> int_arg{ "int_arg", 1 };

    Pipeline build() {
        input = ImageParam(input_type, input.dimensions(), input.name());

        using std::min;
        using std::max;

        float test_float = float_gen_arg + 42; // Intentionally use integer 42
        int32_t test_int = 42 + int_gen_arg;
        test_float = float_gen_arg - 42;
        test_int = 42 - int_gen_arg;
        test_float = float_gen_arg * 42;
        test_int = 42 * int_gen_arg;
        test_float = float_gen_arg / 42;
        test_int = 42 / int_gen_arg;
        test_int = 42 % int_gen_arg;
        test_int = 42 & int_gen_arg;
        test_int = 42 | int_gen_arg;
        test_float = float_gen_arg && 42;
        test_int = 42 && int_gen_arg;
        test_float = float_gen_arg || 42;
        test_int = 42 || int_gen_arg;
        test_float = max(float_gen_arg, 42.0f);
        test_int = max(42, int_gen_arg);
        test_float = min(float_gen_arg, 42.0f);
        test_int = min(42, int_gen_arg);
        bool flag_float = float_gen_arg > 42;
        bool flag_int = 42 > int_gen_arg;
        flag_float = float_gen_arg < 42;
        flag_int = 42 < int_gen_arg;
        flag_float = float_gen_arg >= 42;
        flag_int = 42 >= int_gen_arg;
        flag_float = float_gen_arg <= 42;
        flag_int = 42 <= int_gen_arg;
        flag_float = float_gen_arg == 42;
        flag_int = 42 == int_gen_arg;
        flag_float = float_gen_arg != 42;
        flag_int = 42 != int_gen_arg;
        bool not_result = !float_gen_arg;
        not_result = !int_gen_arg;

        Expr forty_two = 42;
        Expr test_expr = float_gen_arg + forty_two;
        test_expr = forty_two + int_gen_arg;
        test_expr = float_gen_arg - forty_two;
        test_expr = forty_two - int_gen_arg;
        test_expr = float_gen_arg * forty_two;
        test_expr = forty_two * int_gen_arg;
        test_expr = float_gen_arg / forty_two;
        test_expr = forty_two / int_gen_arg;
        test_expr = forty_two % int_gen_arg;
        test_expr = forty_two & int_gen_arg;
        test_expr = forty_two | int_gen_arg;
        test_expr = bool_gen_arg && (forty_two != 0);
        test_expr = bool_gen_arg && (forty_two != 0);
        test_expr = bool_gen_arg || (forty_two != 0);
        test_expr = (forty_two != 0) || bool_gen_arg;
        test_expr = max(float_gen_arg, forty_two);
        test_expr = max(forty_two, int_gen_arg);
        test_expr = min(float_gen_arg, forty_two);
        test_expr = min(forty_two, int_gen_arg);
        test_expr = float_gen_arg > forty_two;
        test_expr = forty_two > int_gen_arg;
        test_expr = float_gen_arg < forty_two;
        test_expr = forty_two < int_gen_arg;
        test_expr = float_gen_arg >= forty_two;
        test_expr = forty_two >= int_gen_arg;
        test_expr = float_gen_arg <= forty_two;
        test_expr = forty_two <= int_gen_arg;
        test_expr = float_gen_arg == forty_two;
        test_expr = forty_two == int_gen_arg;
        test_expr = float_gen_arg != forty_two;
        test_expr = forty_two != int_gen_arg;

        Var x, y, c;

        Func f;
        f(x, y, c) = Tuple(
                input(x, y, c),
                cast(output_type, input(x, y, c) * float_arg + int_arg +
                     0 * (test_float + test_int + flag_float + flag_int +
                          not_result + test_expr)));

        Func g;
        g(x, y) = cast<int16_t>(input(x, y, 0));

        return Pipeline({f, g});
    }
};

Halide::RegisterGenerator<ParamTest> register_paramtest{"paramtest"};

}  // namespace
