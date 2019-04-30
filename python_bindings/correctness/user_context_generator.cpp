#include "Halide.h"

using namespace Halide;

class UserContextGenerator : public Halide::Generator<UserContextGenerator> {
public:
    Input<uint8_t> constant{"constant"};
    Output<Buffer<uint8_t>> output{"output", 1};

    Var x;

    void generate() {
        output(x) = constant;
    }

    void schedule() {
    }
};

HALIDE_REGISTER_GENERATOR(UserContextGenerator, user_context)
