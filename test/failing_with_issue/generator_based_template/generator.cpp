#include <Halide.h>
using namespace Halide;

class TestGenerator : public Generator<TestGenerator> {
public:
    // Define Input/Outputs.
    Output<float> output{"output"};

    void generate() {
        output() = 0.0f;
        // Fill in.
    }

    void schedule() {
        // Fill in.
    }
};

HALIDE_REGISTER_GENERATOR(TestGenerator, test_generator)
