#include "Halide.h"

#include "stubtest.stub.h"

using Halide::Argument;
using Halide::Expr;
using Halide::Func;
using Halide::Buffer;
using Halide::JITGeneratorContext;
using Halide::LoopLevel;
using Halide::Var;
using StubNS1::StubNS2::StubTest;

const int kSize = 32;

Halide::Var x, y, c;

template<typename Type>
Buffer<Type> make_image(int extra) {
    Buffer<Type> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
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

    Buffer<float> src[kArrayCount] = {
        make_image<float>(0),
        make_image<float>(1)
    };

    std::vector<int> int_args = { 33, 66 };

    // the Stub wants Expr, so make a conversion in place
    std::vector<Expr> int_args_expr(int_args.begin(), int_args.end());

    auto gen = StubTest(
        JITGeneratorContext(Halide::get_target_from_environment()),
        // Use aggregate-initialization syntax to fill in an Inputs struct.
        {
            { Func(src[0]), Func(src[1]) },
            1.25f,
            int_args_expr
        });

    StubTest::ScheduleParams sp;
    // This generator defaults intermediate_level to "undefined",
    // so we *must* specify something for it (else we'll crater at
    // Halide compile time). We'll use this:
    sp.intermediate_level = LoopLevel(gen.f, gen.f.args().at(1));
    // ...but any of the following would also be OK:
    // sp.intermediate_level = LoopLevel::root();
    // sp.intermediate_level = LoopLevel(gen.f, Var("x"));
    // sp.intermediate_level = LoopLevel(gen.f, Var("c"));
    gen.schedule(sp);

    Halide::Realization f_realized = gen.realize(kSize, kSize, 3);
    Buffer<float> f0 = f_realized[0];
    Buffer<float> f1 = f_realized[1];
    verify(src[0], 1.25f, 0, f0);
    verify(src[0], 1.25f, 33, f1);

    for (int i = 0; i < kArrayCount; ++i) {
        Halide::Realization g_realized = gen.g[i].realize(kSize, kSize, gen.get_target());
        Buffer<int16_t> g0 = g_realized;
        verify(src[i], 1.0f, int_args[i], g0);
    }

    printf("Success!\n");
    return 0;
}
