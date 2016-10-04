#include "Halide.h"
#include "stubtest.stub.h"

using StubNS1::StubNS2::StubTest;

namespace {

class StubUser : public Halide::Generator<StubUser> {
public:
    GeneratorParam<Type> input_type{ "input_type", UInt(8) };
    GeneratorParam<Type> output_type{ "output_type", UInt(8) };
    GeneratorParam<int32_t> int_arg{ "int_arg", 33 };

    Input<Func> input{ "input", input_type, 3 };

    Output<Func> output{"output", output_type, 3};

    void generate() {

        // We'll explicit fill in the struct fields by name, just to show
        // it as an option. (Alternately, we could fill it in by using
        // C++11 aggregate-initialization syntax.)
        StubTest::Inputs inputs;
        inputs.input = { input };
        inputs.float_arg = 1.234f;
        inputs.int_arg = { int_arg };

        // Since we need to propagate types passed to us via our own GeneratorParams,
        // we can't (easily) use the templated constructor; instead, pass on the 
        // values via the Stub's GeneratorParams struct.
        StubTest::GeneratorParams gp;
        gp.input_type = input_type;
        gp.output_type = output_type;
        // Override array_count to only expect 1 input and provide one output for g
        gp.array_count = 1;

        stub = StubTest(context(), inputs, gp);

        const float kOffset = 2.f;
        output(x, y, c) = cast(output_type, stub.f(x, y, c)[1] + kOffset);
    }

    void schedule() {
        const bool vectorize = true;
        stub.schedule({ vectorize, LoopLevel(output, Var("y")) });
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
    StubTest stub;
};

// Note that HALIDE_REGISTER_GENERATOR() with just two args is functionally
// identical to the old HalideRegister<> syntax: no stub being defined,
// just AOT usage. (If you try to generate a stub for this class you'll
// fail with an error at generation time.)
auto register_me = HALIDE_REGISTER_GENERATOR(StubUser, "stubuser");

}  // namespace
