#include "Halide.h"
#include "configure.stub.h"
#include "stubtest.stub.h"

using Halide::Buffer;
using StubNS1::StubNS2::StubTest;

namespace {

template<typename Type, int size = 32>
Buffer<Type, 3> make_image() {
    Buffer<Type, 3> im(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c);
            }
        }
    }
    return im;
}

class StubUser : public Halide::Generator<StubUser> {
public:
    GeneratorParam<int32_t> int_arg{"int_arg", 33};

    Input<Buffer<uint8_t, 3>> input{"input"};
    Output<Buffer<uint8_t, 3>> calculated_output{"calculated_output"};
    Output<Buffer<float, 3>> float32_buffer_output{"float32_buffer_output"};
    Output<Buffer<int32_t, 3>> int32_buffer_output{"int32_buffer_output"};
    Output<Buffer<uint8_t, 3>> array_test_output{"array_test_output"};
    // We can infer the tupled-output-type from the Stub
    Output<Buffer<void, 3>> tupled_output{"tupled_output"};
    Output<Buffer<int, 3>> int_output{"int_output"};
    Output<Buffer<Halide::float16_t, 3>> float16_output{"float16_output"};
    Output<Buffer<Halide::bfloat16_t, 3>> bfloat16_output{"bfloat16_output"};

    void generate() {
        Var x{"x"}, y{"y"}, c{"c"};

        Buffer<uint8_t, 3> constant_image = make_image<uint8_t>();

        // We'll explicitly fill in the struct fields by name, just to show
        // it as an option. (Alternately, we could fill it in by using
        // aggregate-initialization syntax.)
        StubTest::Inputs inputs;
        inputs.typed_buffer_input = constant_image;
        inputs.untyped_buffer_input = input;
        inputs.array_buffer_input = {input, input};
        inputs.simple_input = input;
        inputs.array_input = {input};
        inputs.float_arg = 1.234f;
        inputs.int_arg = {int_arg};

        StubTest::GeneratorParams gp;
        gp.untyped_buffer_output_type = int32_buffer_output.type();
        gp.intermediate_level.set(LoopLevel(calculated_output, Var("y")));
        gp.vectorize = true;
        gp.str_param = "2 x * y +";

        // Stub outputs that are Output<Buffer> (rather than Output<Func>)
        // can really only be assigned to another Output<Buffer>; this is
        // nevertheless useful, as we can still set stride (etc) constraints
        // on the Output.
        StubTest::Outputs out = StubTest::generate(context(), inputs, gp);

        float32_buffer_output = out.typed_buffer_output;
        int32_buffer_output = out.untyped_buffer_output;
        array_test_output = out.array_buffer_output[1];
        tupled_output = out.tupled_output;
        float16_output = out.float16_output;
        bfloat16_output = out.bfloat16_output;

        const float kOffset = 2.f;
        calculated_output(x, y, c) = cast<uint8_t>(out.tuple_output(x, y, c)[1] + kOffset);

        Buffer<int, 3> configure_input = make_image<int>();
        const int bias = 1;
        Buffer<uint8_t, 2> extra_u8(32, 32);
        extra_u8.fill(0);
        Buffer<int16_t, 2> extra_i16(32, 32);
        extra_i16.fill(0);
        Func extra_func;
        extra_func(x, y, c) = cast<uint16_t>(3);
        const int extra_scalar = 0;
        const int8_t extra_dynamic_scalar = 0;
        int_output = configure::generate(context(), {configure_input,
                                                     bias,
                                                     extra_u8,
                                                     extra_u8,
                                                     extra_u8,
                                                     extra_i16,
                                                     extra_func,
                                                     extra_scalar,
                                                     cast<int8_t>(extra_dynamic_scalar)})
                         .output;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(StubUser, stubuser)
