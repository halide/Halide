#include "Halide.h"
#include "stubtest.stub.h"

using Halide::Buffer;
using StubNS1::StubNS2::StubTest;

namespace {

template<typename Type, int size = 32>
Buffer<Type> make_image() {
    Buffer<Type> im(size, size, 3);
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
    GeneratorParam<int32_t> int_arg{ "int_arg", 33 };

    Input<Buffer<uint8_t>> input{ "input", 3 };
    Output<Buffer<uint8_t>> calculated_output{"calculated_output" };
    Output<Buffer<float>> float32_buffer_output{"float32_buffer_output" };
    Output<Buffer<int32_t>> int32_buffer_output{"int32_buffer_output" };

    void generate() {

        Buffer<uint8_t> constant_image = make_image<uint8_t>();

        // We'll explicitly fill in the struct fields by name, just to show
        // it as an option. (Alternately, we could fill it in by using
        // C++11 aggregate-initialization syntax.)
        StubTest::Inputs inputs;
        inputs.typed_buffer_input = constant_image;
        inputs.untyped_buffer_input = input;
        inputs.simple_input = input;
        inputs.array_input = { input };
        inputs.float_arg = 1.234f;
        inputs.int_arg = { int_arg };

        StubTest::GeneratorParams gp;
        gp.untyped_buffer_output_type = int32_buffer_output.type();

        stub = StubTest(this, inputs, gp);

        const float kOffset = 2.f;
        calculated_output(x, y, c) = cast<uint8_t>(stub.tuple_output(x, y, c)[1] + kOffset);

        // Stub outputs that are Output<Buffer> (rather than Output<Func>)
        // can really only be assigned to another Output<Buffer>; this is 
        // nevertheless useful, as we can still set stride (etc) constraints
        // on the Output.
        float32_buffer_output = stub.typed_buffer_output;
        int32_buffer_output = stub.untyped_buffer_output;
    }

    void schedule() {
        const bool vectorize = true;
        stub.schedule({ vectorize, LoopLevel(calculated_output, Var("y")) });
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
    StubTest stub;
};

// Note that HALIDE_REGISTER_GENERATOR() with just two args is functionally
// identical to the old Halide::RegisterGenerator<> syntax: no stub being defined,
// just AOT usage. (If you try to generate a stub for this class you'll
// fail with an error at generation time.)
HALIDE_REGISTER_GENERATOR(StubUser, "stubuser")

}  // namespace
