#include "Halide.h"

// By convention, Generators always go in a .cpp file, usually with no
// corresponding .h file. They can be enclosed in any C++ namespaces
// you like, but the anonymous namespace is often the best choice.
//
// It's normally considered Best Practice to have exactly one Generator
// per .cpp file, and to have the .cpp file name match the generator name
// with a "_generator" suffix (e.g., Generator with name "foo" should
// live in "foo_generator.cpp"), as it tends to simplify build rules,
// but neither of these are required.

namespace {

enum SomeEnum { Foo, Bar };

// Note the inheritance using the Curiously Recurring Template Pattern
class Example : public Halide::Generator<Example> {
public:
    // GeneratorParamss, Inputs, and Outputs are (by convention)
    // always public and always declared at the top of the Generator,
    // in the order
    //    GeneratorParam(s)
    //    Input(s)
    //    Output(s)
    //
    // Note that the Inputs will appear in the C function
    // call in the order they are declared. (GeneratorParams
    // are always referenced by name, not position, so their order is irrelevant.)
    //
    // All Input variants declared as Generator members must have explicit
    // names, and all such names must match the regex [A-Za-z_][A-Za-z_0-9]*
    // (i.e., essentially a C/C++ variable name). By convention, the name should
    // match the member-variable name.

    // GeneratorParams can be float or ints: {default} or {default, min, max}
    // (Note that if you want to specify min and max, you must specify both.)
    GeneratorParam<float> compiletime_factor{ "compiletime_factor", 1, 0, 100 };
    GeneratorParam<int> channels{ "channels", 3 };
    // ...or enums: {default, name->value map}
    GeneratorParam<SomeEnum> enummy{ "enummy",
                                     Foo,
                                     { { "foo", Foo },
                                       { "bar", Bar } } };
    // ...or bools: {default}
    GeneratorParam<bool> vectorize{ "vectorize", true };
    GeneratorParam<bool> parallelize{ "parallelize", true };

    // These are bad names that will produce errors at build time:
    // GeneratorParam<bool> badname{ " flag", true };
    // GeneratorParam<bool> badname{ "flag ", true };
    // GeneratorParam<bool> badname{ "0flag ", true };
    // GeneratorParam<bool> badname{ "", true };
    // GeneratorParam<bool> badname{ "\001", true };
    // GeneratorParam<bool> badname{ "a name? with! stuff*", true };

    // Note that a leading underscore is legal-but-reserved in C,
    // but it's outright forbidden here. (underscore after first char is ok.)
    // GeneratorParam<bool> badname{ "_flag", true };

    // We also forbid two underscores in a row.
    // GeneratorParam<bool> badname{ "f__lag", true };

    // Input<> are arguments passed to the filter when
    // it is executed (as opposed to the Generator, during compilation).
    // When jitting, there is effectively little difference between the
    // two (at least for scalar values). Note that we set a default value of
    // 1.0 so that invocations that don't set it explicitly use a predictable value.
    Input<float> runtime_factor{ "runtime_factor", 1.0 };

    Output<Func> output{ "output", Int(32), 3 };

    void generate() {
        Func f;
        f(x, y) = max(x, y);
        output(x, y, c) = cast(output.type(), f(x, y) * c * compiletime_factor * runtime_factor);
    }

    void schedule() {
        runtime_factor.set_estimate(1);
        output.set_estimates({{0, 32}, {0, 32}, {0, 3}});

        if (!auto_schedule) {
            output
                .bound(c, 0, channels)
                .reorder(c, x, y)
                .unroll(c);
            // Note that we can use the Generator method natural_vector_size()
            // here; this produces the width of the SIMD vector being targeted
            // divided by the width of the data type.
            const int v = natural_vector_size(output.type());
            if (parallelize && vectorize) {
                output.parallel(y).vectorize(x, v);
            } else if (parallelize) {
                output.parallel(y);
            } else if (vectorize) {
                output.vectorize(x, v);
            }
        }
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Example, example)
