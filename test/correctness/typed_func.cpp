#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    {
        Func f("f");

        assert(!f.defined());
        // undefined funcs assert-fail for these calls.
        // but return 0 for outputs() and dimensions().
        // assert(f.output_type() == Int(32));
        // assert(f.outputs() == 0);
        // assert(f.dimensions() == 0);
    }

    // Verify that func with type-and-dim specifications
    // return appropriate types, dims, etc even though the func is "undefined"
    {
        Func f(Int(32), 2, "f");

        assert(!f.defined());
        const std::vector<Type> expected = {Int(32)};
        assert(f.output_type() == expected[0]);
        assert(f.output_types() == expected);
        assert(f.outputs() == 1);
        assert(f.dimensions() == 2);
    }

    // Same, but for Tuples.
    {
        Func f({Int(32), Float(64)}, 3, "f");

        const std::vector<Type> expected = {Int(32), Float(64)};
        assert(!f.defined());
        // assert(f.output_type() == expected[0]);  // will assert-fail
        assert(f.output_types() == expected);
        assert(f.outputs() == 2);
        assert(f.dimensions() == 3);
    }

    // Verify that the Func for an ImageParam gets required-types, etc, set.
    {
        ImageParam im(Int(32), 2, "im");
        Func f = im;

        // Have to peek directly at 'required_type', etc since the Func
        // actually is defined to peek at a buffer of the right types
        const std::vector<Type> expected = {Int(32)};
        assert(f.function().required_types() == expected);
        assert(f.function().required_dimensions() == 2);
    }

    // Verify that we can call output_buffer() on an undefined Func,
    // but only if it has type-and-dim specifications.
    {
        Func f(Int(32), 2, "f");

        const auto o = f.output_buffer();
        f.output_buffer().dim(0).set_bounds(0, 10).dim(1).set_bounds(0, 10);

        // And now we can define the Func *after* setting values in output_buffer()
        f(x, y) = x + y;

        auto r = f.realize({10, 10});  // will assert-fail for values other than 10x10
        Buffer<int> b = r[0];
        b.for_each_element([&](int x, int y) {
            assert(b(x, y) == x + y);
        });
    }

    printf("Success!\n");
    return 0;
}
