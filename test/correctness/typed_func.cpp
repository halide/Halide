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
        // assert(f.type() == Int(32));
        // assert(f.outputs() == 0);
        // assert(f.dimensions() == 0);
    }

    // Verify that func with type-and-dim specifications
    // return appropriate types, dims, etc even though the func is "undefined"
    {
        Func f(Int(32), 2, "f");

        assert(!f.defined());
        const std::vector<Type> expected = {Int(32)};
        assert(f.type() == expected[0]);
        assert(f.types() == expected);
        assert(f.outputs() == 1);
        assert(f.dimensions() == 2);
    }

    // Same, but for Tuples.
    {
        Func f({Int(32), Float(64)}, 3, "f");

        const std::vector<Type> expected = {Int(32), Float(64)};
        assert(!f.defined());
        // assert(f.type() == expected[0]);  // will assert-fail
        assert(f.types() == expected);
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
        Buffer<int32_t> b = r[0];
        b.for_each_element([&](int x, int y) {
            assert(b(x, y) == x + y);
        });
    }

    // Verify that update stages defined via += and friends *don't* require
    // the RHS type to match the LHS type (whether or not the pure definition
    // is implicitly defined)
    {
        Func f(Int(32), 2, "f");

        f(x, y) = cast<int32_t>(1);
        f(x, y) += cast<uint8_t>(x + y);

        auto r = f.realize({10, 10});
        Buffer<int32_t> b = r[0];
        b.for_each_element([&](int x, int y) {
            assert(b(x, y) == 1 + (uint8_t)(x + y));
        });
    }

    {
        Func f(Int(32), 2, "f");

        // f(x, y) = cast<int32_t>(0);  // leave out, so Halide injects the implicit init
        f(x, y) += cast<uint8_t>(x + y);

        auto r = f.realize({10, 10});
        Buffer<int32_t> b = r[0];
        b.for_each_element([&](int x, int y) {
            assert(b(x, y) == 0 + (uint8_t)(x + y));
        });
    }

    // Same, but with Tuples
    {
        Func f({Int(32), Int(8)}, 2, "f");

        f(x, y) = Tuple(cast<int32_t>(1), cast<int8_t>(2));
        f(x, y) += Tuple(cast<uint8_t>(x + y), cast<int8_t>(x - y));

        auto r = f.realize({10, 10});
        Buffer<int32_t> b0 = r[0];
        Buffer<int8_t> b1 = r[1];
        b0.for_each_element([&](int x, int y) {
            assert(b0(x, y) == 1 + (uint8_t)(x + y));
        });
        b1.for_each_element([&](int x, int y) {
            assert(b1(x, y) == 2 + (int8_t)(x - y));
        });
    }

    {
        Func f({Int(32), Int(8)}, 2, "f");

        // f(x, y) = Tuple(cast<int32_t>(1), cast<int8_t>(2));  // leave out, so Halide injects the implicit init
        f(x, y) += Tuple(cast<uint8_t>(x + y), cast<int8_t>(x - y));

        auto r = f.realize({10, 10});
        Buffer<int32_t> b0 = r[0];
        Buffer<int8_t> b1 = r[1];
        b0.for_each_element([&](int x, int y) {
            assert(b0(x, y) == 0 + (uint8_t)(x + y));
        });
        b1.for_each_element([&](int x, int y) {
            assert(b1(x, y) == 0 + (int8_t)(x - y));
        });
    }

    printf("Success!\n");
    return 0;
}
