#include "Halide.h"
#include <stdio.h>

using namespace Halide;

namespace {

// A Func declared as a member is constructed before the Input<Buffer>
// member (declaration order), so by the time the Input's Parameter is
// created, the Func has already reserved "foo". The Parameter still keeps
// its literal name, so they collide in the resulting pipeline.
class GenFuncBeforeInputBuffer : public Halide::Generator<GenFuncBeforeInputBuffer> {
public:
    Func foo{"foo"};
    Input<Buffer<int, 1>> input_foo{"foo"};
    Output<Buffer<int, 1>> out{"out"};

    void generate() {
        Var x;
        foo(x) = x;
        out(x) = input_foo(x) + foo(x);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(GenFuncBeforeInputBuffer, gen_input_buffer_func_collision)

int main(int argc, char **argv) {
    GeneratorContext ctx(get_jit_target_from_environment());
    (void)create_callable_from_generator(ctx, "gen_input_buffer_func_collision");

    printf("Should not get here\n");
    return 0;
}
