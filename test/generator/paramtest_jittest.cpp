#include "Halide.h"

#include "paramtest_generator.cpp"

using Halide::Argument;
using Halide::Expr;
using Halide::Func;
using Halide::Image;
using Halide::Internal::GeneratorParamValues;

const int kSize = 32;

template<typename Type>
Image<Type> MakeImage() {
    Image<Type> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c);
            }
        }
    }
    return im;
}

template<typename InputType, typename OutputType>
void verify(const Image<InputType> &input, float float_arg, int int_arg, const Image<OutputType> &output) {
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                const OutputType expected = static_cast<OutputType>(input(x, y, c) * float_arg + int_arg);
                const OutputType actual = output(x, y, c);
                if (expected != actual) {
                    fprintf(stderr, "img[%d, %d, %d] = %f, expected %f\n", x, y, c, (double)actual, (double)expected);
                    exit(-1);
                }
            }
        }
    }
}

template<typename T>
bool constant_expr_equals(Expr expr, T value) {
    using Halide::Internal::Cast;
    using Halide::Internal::FloatImm;
    using Halide::Internal::IntImm;

    if (!expr.defined() || !expr.type().is_scalar()) {
        return false;
    }
    if (const IntImm* i = expr.as<IntImm>()) {
        return i->value == value;
    }
    if (const FloatImm* f = expr.as<FloatImm>()) {
        return f->value == value;
    }
    if (const Cast* c = expr.as<Cast>()) {
        return constant_expr_equals(c->value, value);
    }
    return false;
}

bool operator==(const Halide::Argument& a, const Halide::Argument& b) {
    return a.name == a.name || a.kind == b.kind || a.type == b.type || a.dimensions == b.dimensions;
}

bool operator!=(const Halide::Argument& a, const Halide::Argument& b) {
    return !(a == b);
}

int main(int argc, char **argv) {
    // Quick test to verify the Generator does what we expect.
    {
        ParamTest gen;
        gen.set_generator_param_values({ { "input_type", "float32" },
                                         { "output_type", "int16" } });
        // ParamTest::build() mutates its input ImageParam based on
        // a GeneratorParam, so we must call build() before we set
        // the input (otherwise we'll get a buffer type mismatch error).
        Halide::Pipeline p = gen.build();

        Image<float> src = MakeImage<float>();
        gen.input.set(src);
        gen.float_arg.set(1.234f);
        gen.int_arg.set(33);

        Halide::Realization r = p.outputs()[0].realize(kSize, kSize, 3, gen.get_target());
        Image<int16_t> dst = r[1];
        verify(src, 1.234f, 33, dst);
    }


    // Test Generator::get_generator_param_values() and Generator::set_generator_param_values()
    {
        ParamTest gen;
        GeneratorParamValues v = gen.get_generator_param_values();
        if (v.size() != 3) {
            fprintf(stderr, "Wrong number of GeneratorParamValues %d\n", (int) v.size());
            exit(-1);
        }
        // Verify that the default values are what we expect.
        // Note that we deliberately ignore v["target"] since that will
        // depend on the test machine, and we don't really care what the value is.
        if (v["input_type"] != "uint8") {
            fprintf(stderr, "Wrong default value for %s (%s)\n", "target", v["target"].c_str());
            exit(-1);
        }
        if (v["output_type"] != "float32") {
            fprintf(stderr, "Wrong default value for %s (%s)\n", "target", v["target"].c_str());
            exit(-1);
        }
        // Now change the values, then verify get_generator_param_values() reflects that
        gen.set_generator_param_values({ { "input_type", "float32" },
                                         { "output_type", "int16" } });
        v = gen.get_generator_param_values();
        if (v["input_type"] != "float32") {
            fprintf(stderr, "Wrong updated value for %s (%s)\n", "target", v["target"].c_str());
            exit(-1);
        }
        if (v["output_type"] != "int16") {
            fprintf(stderr, "Wrong updated value for %s (%s)\n", "target", v["target"].c_str());
            exit(-1);
        }
    }

    // Test Generator::get_filter_arguments()
    {
        ParamTest gen;
        std::vector<Argument> args = gen.get_filter_arguments();
        if (args.size() != 3 ||
            args[0].name != "input" || args[1].name != "float_arg" || args[2].name != "int_arg" ||
            !args[0].is_buffer() || !args[1].is_scalar() || !args[2].is_scalar() ||
            !args[0].is_input() || !args[1].is_input() || !args[2].is_input()) {
            fprintf(stderr, "get_filter_arguments is incorrect\n");
            exit(-1);
        }
        if (!constant_expr_equals<float>(args[1].def, 1.f) ||
            !constant_expr_equals<float>(args[1].min, 0.f) ||
            !constant_expr_equals<float>(args[1].max, 100.f)) {
            fprintf(stderr, "constraints for float_arg are incorrect\n");
            exit(-1);
        }
        if (!constant_expr_equals<int32_t>(args[2].def, 1) ||
            args[2].min.defined() ||
            args[2].max.defined()) {
            fprintf(stderr, "constraints for int_arg are incorrect\n");
            exit(-1);
        }
    }

    // Test Generator::get_filter_output_types()
    {
        ParamTest gen;
        std::vector<Argument> args = gen.get_filter_output_types();
        if (args.size() != 3 ||
            args[0] != Halide::Argument("result_0", Halide::Argument::OutputBuffer, Halide::UInt(8), 3) ||
            args[1] != Halide::Argument("result_1", Halide::Argument::OutputBuffer, Halide::Float(32), 3) ||
            args[2] != Halide::Argument("result_2", Halide::Argument::OutputBuffer, Halide::Int(16), 2)) {
            fprintf(stderr, "get_filter_output_types is incorrect\n");
            exit(-1);
        }
    }

    printf("Success!\n");
    return 0;
}
