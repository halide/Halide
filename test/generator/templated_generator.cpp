#include "Halide.h"

using namespace Halide;

// Define a templated generator. Normally this is a bad idea, and your template
// parameters (e.g. the type of the input) should be GeneratorParams
// instead. Sometimes, however, it's more convenient to have the C++ type
// available as a template parameter. Or maybe you want to template a Generator
// on something not expressible as a GeneratorParam. Or maybe you have a
// deficient build system and it's difficult to specify GeneratorParams in the
// build (note that HALIDE_REGISTER_GENERATOR_ALIAS also exists for this
// purpose).
template<typename T1, typename T2>
class Templated : public Generator<Templated<T1, T2>> {
public:
    // A major downside of templated generators is that because we use CRTP, you
    // must manually import names of types from the base class. For Input and
    // Output you can also just use these equivalent globally-scoped names:
    GeneratorInput<Buffer<T1, 2>> input{"input"};
    GeneratorOutput<Buffer<T2, 2>> output{"output"};

    void generate() {
        Var x, y;
        output(x, y) = cast<T2>(input(x, y) + (T1)2);

        // Again, due to CRTP, we must manually tell C++ how to look up a method
        // from the base class using this->template.
        output.vectorize(x, this->template natural_vector_size<T2>());
    }
};

// To pass a comma-separated template parameter list to a macro, we must enclose
// the type argument in parentheses.
HALIDE_REGISTER_GENERATOR((Templated<float, double>), templated)
HALIDE_REGISTER_GENERATOR((Templated<uint8_t, uint16_t>), templated_uint8)
