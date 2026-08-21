#include "Halide.h"
#include <stdio.h>

using namespace Halide;

namespace {

// Generator with an Input<Buffer> declared before any Func of the same
// name. The Func created inside generate() must be renamed so the
// pipeline compiles without collision.
class GenInputBufferThenFunc : public Halide::Generator<GenInputBufferThenFunc> {
public:
    Input<Buffer<int, 1>> input_foo{"foo"};
    Output<Buffer<int, 1>> out{"out"};

    void generate() {
        Var x;
        Func foo("foo");
        foo(x) = x;
        out(x) = input_foo(x) + foo(x);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(GenInputBufferThenFunc, gen_input_buffer_then_func)

int main(int argc, char **argv) {
    // An ImageParam followed by a Func of the same name: the Func is
    // renamed so the names differ.
    {
        ImageParam ip(Int(32), 1, "foo");
        Func foo("foo");
        assert(ip.name() != foo.name() &&
               "ImageParam should reserve its name against later Funcs");
    }

    // A scalar Param followed by a Func of the same name: the Func is
    // renamed so the names differ.
    {
        Param<int> p("foo");
        Func foo("foo");
        assert(p.name() != foo.name() &&
               "Param should reserve its name against later Funcs");
    }

    // A Generator Input<Buffer> followed by a Func of the same name
    // inside generate(): the Func is renamed and the pipeline compiles.
    {
        GeneratorContext ctx(get_jit_target_from_environment());
        Callable c = create_callable_from_generator(ctx, "gen_input_buffer_then_func");

        Buffer<int> in(10), out(10);
        in.fill(0);
        int r = c(in, out);
        assert(r == 0);
        for (int i = 0; i < 10; i++) {
            assert(out(i) == i);
        }
    }

    printf("Success!\n");
    return 0;
}
