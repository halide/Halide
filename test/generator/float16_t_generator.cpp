#include "Halide.h"

class Float16T : public Halide::Generator<Float16T> {
public:
    Output<Buffer<int32_t, 1>> output{"output"};

    void generate() {
        // Currently the float16 aot test just exercises the
        // runtime. More interesting code may go here in the future.
        Var x;
        output(x) = x;
    }
};

HALIDE_REGISTER_GENERATOR(Float16T, float16_t)
