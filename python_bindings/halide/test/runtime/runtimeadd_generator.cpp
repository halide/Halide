#include "Halide.h"

using namespace Halide;

// A deliberately tiny AOT kernel used to exercise the standalone `halide.runtime`
// loader: one input buffer, one scalar, one output buffer.
class RuntimeAddGenerator : public Generator<RuntimeAddGenerator> {
public:
    Input<Buffer<uint8_t, 1>> input{"input"};
    Input<int32_t> offset{"offset"};
    Output<Buffer<uint8_t, 1>> output{"output"};

    Var x;

    void generate() {
        output(x) = cast<uint8_t>(input(x) + offset);
    }

    void schedule() {
    }
};

HALIDE_REGISTER_GENERATOR(RuntimeAddGenerator, runtimeadd)
