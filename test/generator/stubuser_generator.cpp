#include "Halide.h"
#include "stubtest.stub.h"

using StubNS1::StubNS2::StubTest;

namespace {

class StubUser : public Halide::Generator<StubUser> {
public:
    GeneratorParam<int32_t> int_arg{ "int_arg", 33 };

    Input<Func> input{ "input", UInt(8), 3 };
    Output<Func> output{"output", UInt(8), 3};

    void generate() {

        // We'll explicit fill in the struct fields by name, just to show
        // it as an option. (Alternately, we could fill it in by using
        // C++11 aggregate-initialization syntax.)
        StubTest::Inputs inputs;
        inputs.input = { input };
        inputs.float_arg = 1.234f;
        inputs.int_arg = { int_arg };

        stub = StubTest(this, inputs);

        const float kOffset = 2.f;
        output(x, y, c) = cast<uint8_t>(stub.f(x, y, c)[1] + kOffset);
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
// identical to the old Halide::RegisterGenerator<> syntax: no stub being defined,
// just AOT usage. (If you try to generate a stub for this class you'll
// fail with an error at generation time.)
HALIDE_REGISTER_GENERATOR(StubUser, "stubuser")

}  // namespace
