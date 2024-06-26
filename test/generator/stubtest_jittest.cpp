#include "Halide.h"

#include "stubtest.stub.h"

using namespace Halide;

using StubNS1::StubNS2::StubTest;

const int kSize = 32;

Var x, y, c;

template<typename Type>
Buffer<Type, 3> make_image(int extra) {
    Buffer<Type, 3> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c + extra);
            }
        }
    }
    return im;
}

template<typename InputType, typename OutputType, int Dims>
void verify(const Buffer<InputType, Dims> &input, float float_arg, int int_arg, const Buffer<OutputType, Dims> &output) {
    if (input.width() != output.width() ||
        input.height() != output.height()) {
        fprintf(stderr, "size mismatch: %dx%d vs %dx%d\n", input.width(), input.height(), output.width(), output.height());
        exit(1);
    }
    int channels = std::max(1, std::min(input.channels(), output.channels()));
    for (int x = 0; x < output.width(); x++) {
        for (int y = 0; y < output.height(); y++) {
            for (int c = 0; c < channels; c++) {
                const OutputType expected = static_cast<OutputType>(input(x, y, c) * float_arg + int_arg);
                const OutputType actual = output(x, y, c);
                if (expected != actual) {
                    fprintf(stderr, "img[%d, %d, %d] = %f, expected %f (input = %f)\n", x, y, c, (double)actual, (double)expected, (double)input(x, y, c));
                    exit(1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    constexpr int kArrayCount = 2;

    Buffer<uint8_t, 3> buffer_input = make_image<uint8_t>(0);
    Buffer<float, 3> simple_input = make_image<float>(0);
    Buffer<float, 3> array_input[kArrayCount] = {
        make_image<float>(0),
        make_image<float>(1)};

    std::vector<int> int_args = {33, 66};

    // the Stub wants Expr, so make a conversion in place
    std::vector<Expr> int_args_expr(int_args.begin(), int_args.end());

    GeneratorContext context(get_jit_target_from_environment());

    {
        // Pass in a set of GeneratorParams: even though we aren't customizing
        // the values, we can set the LoopLevel values after-the-fact.
        StubTest::GeneratorParams gp;
        auto gen = StubTest::generate(
            context,
            // Use aggregate-initialization syntax to fill in an Inputs struct.
            {
                buffer_input,  // typed_buffer_input
                buffer_input,  // untyped_buffer_input
                {buffer_input, buffer_input},
                Func(simple_input),
                {Func(array_input[0]), Func(array_input[1])},
                1.25f,
                int_args_expr},
            gp);

        gp.intermediate_level.set(LoopLevel(gen.tuple_output, gen.tuple_output.args().at(1)));

        Realization simple_output_realized = gen.simple_output.realize({kSize, kSize, 3});
        Buffer<float, 3> s0 = simple_output_realized;
        verify(array_input[0], 1.f, 0, s0);

        Realization tuple_output_realized = gen.tuple_output.realize({kSize, kSize, 3});
        Buffer<float, 3> f0 = tuple_output_realized[0];
        Buffer<float, 3> f1 = tuple_output_realized[1];
        verify(array_input[0], 1.25f, 0, f0);
        verify(array_input[0], 1.25f, 33, f1);

        for (int i = 0; i < kArrayCount; ++i) {
            Realization array_output_realized = gen.array_output[i].realize({kSize, kSize, 3}, gen.target);
            Buffer<int16_t, 3> g0 = array_output_realized;
            verify(array_input[i], 1.0f, int_args[i], g0);
        }

        Realization typed_buffer_output_realized = gen.typed_buffer_output.realize({kSize, kSize, 3});
        Buffer<float, 3> b0 = typed_buffer_output_realized;
        verify(buffer_input, 1.f, 0, b0);

        Realization untyped_buffer_output_realized = gen.untyped_buffer_output.realize({kSize, kSize, 3});
        Buffer<float, 3> b1 = untyped_buffer_output_realized;
        verify(buffer_input, 1.f, 0, b1);

        Realization static_compiled_buffer_output_realized = gen.static_compiled_buffer_output.realize({kSize, kSize, 3});
        Buffer<uint8_t, 3> b2 = static_compiled_buffer_output_realized;
        verify(buffer_input, 1.f, 42, b2);

        for (int i = 0; i < 2; ++i) {
            Realization array_buffer_output_realized = gen.array_buffer_output[i].realize({kSize, kSize, 3});
            Buffer<uint8_t, 3> b2 = array_buffer_output_realized;
            verify(buffer_input, 1.f, 1 + i, b2);
        }
    }

    // Alternately, instead of using Generator Stubs, we can just use the Callable interface.
    // We can call this on any Generator that is registered in the current process.
    {
        Buffer<uint8_t, 3> buffer_input = make_image<uint8_t>(0);
        Buffer<float, 3> simple_input = make_image<float>(0);
        Buffer<float, 3> array_input0 = make_image<float>(0);
        Buffer<float, 3> array_input1 = make_image<float>(1);
        Buffer<float, 3> typed_buffer_output(kSize, kSize, 3);
        Buffer<float, 3> untyped_buffer_output(kSize, kSize, 3);
        Buffer<float, 3> tupled_output0(kSize, kSize, 3);
        Buffer<int32_t, 3> tupled_output1(kSize, kSize, 3);
        Buffer<uint8_t, 3> array_buffer_input0 = make_image<uint8_t>(0);
        Buffer<uint8_t, 3> array_buffer_input1 = make_image<uint8_t>(1);
        Buffer<float, 3> simple_output(kSize, kSize, 3);
        // TODO: see Issues #3709, #3967
        Buffer<void, 3> float16_output(halide_type_t(halide_type_float, 16), kSize, kSize, 3);
        Buffer<void, 3> bfloat16_output(halide_type_t(halide_type_bfloat, 16), kSize, kSize, 3);
        Buffer<float, 3> tuple_output0(kSize, kSize, 3), tuple_output1(kSize, kSize, 3);
        Buffer<int16_t, 3> array_output0(kSize, kSize, 3), array_output1(kSize, kSize, 3);
        Buffer<uint8_t, 3> static_compiled_buffer_output(kSize, kSize, 3);
        Buffer<uint8_t, 3> array_buffer_output0(kSize, kSize, 3), array_buffer_output1(kSize, kSize, 3);

        // Note that this Generator has several GeneratorParams that need to be set correctly
        // before compilation -- in the Stub case above, the values end up being inferred
        // from the specific inputs we provide, but for the JIT (and AOT) cases, there are
        // no such inputs available, so we must be explicit. (Note that these are the same
        // values specified in our Make/CMake files.)
        const GeneratorParamsMap gp = {
            {"untyped_buffer_input.type", "uint8"},
            {"untyped_buffer_input.dim", "3"},
            {"simple_input.type", "float32"},
            {"array_input.type", "float32"},
            {"array_input.size", "2"},
            {"int_arg.size", "2"},
            {"tuple_output.type", "float32,float32"},
            {"vectorize", "true"},
        };

        Callable stubtest = create_callable_from_generator(context, "stubtest", gp);

        int r = stubtest(
            buffer_input,
            buffer_input,
            array_buffer_input0, array_buffer_input1,
            simple_input,
            array_input0, array_input1,
            1.25f,
            33,
            66,
            simple_output,
            tuple_output0, tuple_output1,
            array_output0, array_output1,
            typed_buffer_output,
            untyped_buffer_output,
            tupled_output0, tupled_output1,
            static_compiled_buffer_output,
            array_buffer_output0, array_buffer_output1,
            float16_output,
            bfloat16_output);
        assert(r == 0);

        verify(buffer_input, 1.f, 0, typed_buffer_output);
        verify(buffer_input, 1.f, 0, untyped_buffer_output);
        verify(simple_input, 1.f, 0, simple_output);
        verify(simple_input, 1.f, 0, tupled_output0);
        verify(simple_input, 1.f, 1, tupled_output1);
        verify(array_input0, 1.f, 0, simple_output);
        verify(array_input0, 1.25f, 0, tuple_output0);
        verify(array_input0, 1.25f, 33, tuple_output1);
        verify(array_input0, 1.0f, 33, array_output0);
        verify(array_input1, 1.0f, 66, array_output1);
        verify(buffer_input, 1.0f, 42, static_compiled_buffer_output);
        verify(array_buffer_input0, 1.f, 1, array_buffer_output0);
        verify(array_buffer_input1, 1.f, 2, array_buffer_output1);
    }

    // We can also make an explicitly-typed std::function if we prefer.
    {
        Buffer<uint8_t, 3> buffer_input = make_image<uint8_t>(0);
        Buffer<float, 3> simple_input = make_image<float>(0);
        Buffer<float, 3> array_input0 = make_image<float>(0);
        Buffer<float, 3> array_input1 = make_image<float>(1);
        Buffer<float, 3> typed_buffer_output(kSize, kSize, 3);
        Buffer<float, 3> untyped_buffer_output(kSize, kSize, 3);
        Buffer<float, 3> tupled_output0(kSize, kSize, 3);
        Buffer<int32_t, 3> tupled_output1(kSize, kSize, 3);
        Buffer<uint8_t, 3> array_buffer_input0 = make_image<uint8_t>(0);
        Buffer<uint8_t, 3> array_buffer_input1 = make_image<uint8_t>(1);
        Buffer<float, 3> simple_output(kSize, kSize, 3);
        // TODO: see Issues #3709, #3967
        Buffer<void, 3> float16_output(halide_type_t(halide_type_float, 16), kSize, kSize, 3);
        Buffer<void, 3> bfloat16_output(halide_type_t(halide_type_bfloat, 16), kSize, kSize, 3);
        Buffer<float, 3> tuple_output0(kSize, kSize, 3), tuple_output1(kSize, kSize, 3);
        Buffer<int16_t, 3> array_output0(kSize, kSize, 3), array_output1(kSize, kSize, 3);
        Buffer<uint8_t, 3> static_compiled_buffer_output(kSize, kSize, 3);
        Buffer<uint8_t, 3> array_buffer_output0(kSize, kSize, 3), array_buffer_output1(kSize, kSize, 3);

        // Note that this Generator has several GeneratorParams that need to be set correctly
        // before compilation -- in the Stub case above, the values end up being inferred
        // from the specific inputs we provide, but for the JIT (and AOT) cases, there are
        // no such inputs available, so we must be explicit. (Note that these are the same
        // values specified in our Make/CMake files.)
        const GeneratorParamsMap gp = {
            {"untyped_buffer_input.type", "uint8"},
            {"untyped_buffer_input.dim", "3"},
            {"simple_input.type", "float32"},
            {"array_input.type", "float32"},
            {"array_input.size", "2"},
            {"int_arg.size", "2"},
            {"tuple_output.type", "float32,float32"},
            {"vectorize", "true"},
        };

        auto stubtest = create_callable_from_generator(context, "stubtest", gp)
                            .make_std_function<
                                Buffer<uint8_t, 3>,
                                Buffer<uint8_t, 3>,
                                Buffer<uint8_t, 3>, Buffer<uint8_t, 3>,
                                Buffer<float, 3>,
                                Buffer<float, 3>, Buffer<float, 3>,
                                float,
                                int32_t,
                                int32_t,
                                Buffer<float, 3>,
                                Buffer<float, 3>, Buffer<float, 3>,
                                Buffer<int16_t, 3>, Buffer<int16_t, 3>,
                                Buffer<float, 3>,
                                Buffer<float, 3>,
                                Buffer<float, 3>, Buffer<int32_t, 3>,
                                Buffer<uint8_t, 3>,
                                Buffer<uint8_t, 3>, Buffer<uint8_t, 3>,
                                Buffer<void, 3>,
                                Buffer<void, 3>>();

        int r = stubtest(
            buffer_input,
            buffer_input,
            array_buffer_input0, array_buffer_input1,
            simple_input,
            array_input0, array_input1,
            1.25f,
            33,
            66,
            simple_output,
            tuple_output0, tuple_output1,
            array_output0, array_output1,
            typed_buffer_output,
            untyped_buffer_output,
            tupled_output0, tupled_output1,
            static_compiled_buffer_output,
            array_buffer_output0, array_buffer_output1,
            float16_output,
            bfloat16_output);
        assert(r == 0);

        verify(buffer_input, 1.f, 0, typed_buffer_output);
        verify(buffer_input, 1.f, 0, untyped_buffer_output);
        verify(simple_input, 1.f, 0, simple_output);
        verify(simple_input, 1.f, 0, tupled_output0);
        verify(simple_input, 1.f, 1, tupled_output1);
        verify(array_input0, 1.f, 0, simple_output);
        verify(array_input0, 1.25f, 0, tuple_output0);
        verify(array_input0, 1.25f, 33, tuple_output1);
        verify(array_input0, 1.0f, 33, array_output0);
        verify(array_input1, 1.0f, 66, array_output1);
        verify(buffer_input, 1.0f, 42, static_compiled_buffer_output);
        verify(array_buffer_input0, 1.f, 1, array_buffer_output0);
        verify(array_buffer_input1, 1.f, 2, array_buffer_output1);
    }

    printf("Success!\n");
    return 0;
}
