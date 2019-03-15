#include "Halide.h"

#include "stubtest.stub.h"

using namespace Halide;

using StubNS1::StubNS2::StubTest;

const int kSize = 32;

Var x, y, c;

template<typename Type>
Buffer<Type> make_image(const std::string &name, int channels, int extra = 0) {
    Buffer<Type> im(kSize, kSize, channels, name);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < channels; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c + extra);
            }
        }
    }
    return im;
}

template<typename InputType, typename OutputType>
void verify(const Buffer<InputType> &input, float float_arg, int int_arg, const Buffer<OutputType> &output) {
    if (input.width() != output.width() ||
        input.height() != output.height()) {
        fprintf(stderr, "size mismatch\n");
        exit(-1);
    }
    int channels = std::max(1, std::min(input.channels(), output.channels()));
    for (int x = 0; x < output.width(); x++) {
        for (int y = 0; y < output.height(); y++) {
            for (int c = 0; c < channels; c++) {
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

int main(int argc, char **argv) {
    constexpr int kArrayCount = 2;

    Buffer<uint8_t> buffer_input = make_image<uint8_t>("buffer_inputz", 3);
    Buffer<float> simple_input = make_image<float>("simple_inputz", 5);
    Buffer<float> array_input[kArrayCount] = {
        make_image<float>("array_input_0z", 3, 0),
        make_image<float>("array_input_1z", 3, 1)
    };

    std::vector<int> int_args = { 33, 66 };

    // the Stub wants Expr, so make a conversion in place
    std::vector<Expr> int_args_expr(int_args.begin(), int_args.end());

    // Pass in a set of GeneratorParams: even though we aren't customizing
    // the values, we can set the LoopLevel values after-the-fact.
    StubTest::GeneratorParams gp;
    auto gen = StubTest::generate(
        GeneratorContext(get_jit_target_from_environment()),
        // Use aggregate-initialization syntax to fill in an Inputs struct.
        {
            buffer_input,                                    // typed_buffer_input
            buffer_input,                                    // untyped_buffer_input
            { buffer_input, buffer_input },                  // array_buffer_input
            Func(simple_input),                              // simple_input
            { Func(array_input[0]), Func(array_input[1]) },  // array_input
            1.25f,                                           // float_arg
            int_args_expr                                    // int_arg
        },
        gp);

    gp.intermediate_level.set(LoopLevel(gen.tuple_output, gen.tuple_output.args().at(1)));

    Realization simple_output_realized = gen.simple_output.realize(kSize, kSize, 5);
    Buffer<float> s0 = simple_output_realized;
    verify(array_input[0], 1.f, 0, s0);

    Realization tuple_output_realized = gen.tuple_output.realize(kSize, kSize, 3);
    Buffer<float> f0 = tuple_output_realized[0];
    Buffer<float> f1 = tuple_output_realized[1];
    verify(array_input[0], 1.25f, 0, f0);
    verify(array_input[0], 1.25f, 33, f1);

    for (int i = 0; i < kArrayCount; ++i) {
        Realization array_output_realized = gen.array_output[i].realize(kSize, kSize, gen.target);
        Buffer<int16_t> g0 = array_output_realized;
        verify(array_input[i], 1.0f, int_args[i], g0);
    }

    Realization typed_buffer_output_realized = gen.typed_buffer_output.realize(kSize, kSize, 3);
    Buffer<float> b0 = typed_buffer_output_realized;
    verify(buffer_input, 1.f, 0, b0);

    Realization untyped_buffer_output_realized = gen.untyped_buffer_output.realize(kSize, kSize, 3);
    Buffer<float> b1 = untyped_buffer_output_realized;
    verify(buffer_input, 1.f, 0, b1);

    Realization static_compiled_buffer_output_realized = gen.static_compiled_buffer_output.realize(kSize, kSize, 3);
    Buffer<uint8_t> b2 = static_compiled_buffer_output_realized;
    verify(buffer_input, 1.f, 42, b2);

    for (int i = 0; i < 2; ++i) {
        Realization array_buffer_output_realized = gen.array_buffer_output[i].realize(kSize, kSize, 3);
        Buffer<uint8_t> b2 = array_buffer_output_realized;
        verify(buffer_input, 1.f, 1 + i, b2);
    }

    printf("Success!\n");
    return 0;
}
