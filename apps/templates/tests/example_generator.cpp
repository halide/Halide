#include "Halide.h"

namespace {

// Note the inheritance using the Curiously Recurring Template Pattern
class Example : public Halide::Generator<Example> {
public:
    GeneratorParam<Halide::Type> output_type{ "output_type", UInt(8) };
    GeneratorParam<int> channels{ "channels", 4 };
    GeneratorParam<float> compiletime_factor{ "compiletime_factor", 1, 0, 100 };

    Param<float> runtime_factor{ "runtime_factor", 1.0 };

    // The build() method of a generator defines the actual pipeline
    // and returns the output Func.
    Func build() override {
        Func f("f"), g("g");
        Var x, y, c;

        f(x, y) = max(x, y);

        // Produce a float expression for the filter
        Expr value = f(x, y) * c * compiletime_factor * runtime_factor;

        // Float to integer conversion for unrepresentable values is undefined
        // in C++, e.g. if value is 256, it cannot be represented by a uint8_t.
        if (Type(output_type).is_uint() && Type(output_type).bits != 32) {
          Expr int_value = cast<unsigned int>(value);
          value = int_value % Type(output_type).max();
        }

        // Cast to the output type
        g(x, y, c) = cast(output_type, value);

        g.bound(c, 0, channels).reorder(c, x, y).unroll(c);

        if (get_target().has_feature(Target::OpenGL)) {
          g.glsl(x, y, c);
        } else {
            if (get_target().arch != Target::PNaCl) {
               g.vectorize(x, natural_vector_size(output_type));
            }
        }
        return g;
    }
};

Halide::RegisterGenerator<Example> register_example{"example"};

}  // namespace
