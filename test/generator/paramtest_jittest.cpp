#include "Halide.h"

#include "paramtest_generator.cpp"

using Halide::Argument;
using Halide::Expr;
using Halide::Func;
using Halide::Image;

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

    printf("Success!\n");
    return 0;
}
