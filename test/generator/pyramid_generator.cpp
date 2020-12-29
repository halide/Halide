#include "Halide.h"

namespace {

class Pyramid : public Halide::Generator<Pyramid> {
public:
    GeneratorParam<int> levels{"levels", 1};  // deliberately wrong value, must be overridden to 10

    Input<Func> input{"input", Float(32), 2};
    Output<Func[]> pyramid{"pyramid", Float(32), 2};

    void generate() {
        Var x{"x"}, y{"y"};

        pyramid.resize(levels);
        pyramid[0](x, y) = input(x, y);
        for (size_t i = 1; i < pyramid.size(); i++) {
            Func p = pyramid[i - 1];
            pyramid[i](x, y) = (p(2 * x, 2 * y) +
                                p(2 * x + 1, 2 * y) +
                                p(2 * x, 2 * y + 1) +
                                p(2 * x + 1, 2 * y + 1)) /
                               4;
        }

        // Be sure we set the 'schedule' member before we finish.
        schedule = [=]() mutable {
            for (Func p : pyramid) {
                // No need to specify compute_root() for outputs
                p.parallel(y);
                // Vectorize if we're still wide enough at this level
                const int v = natural_vector_size<float>();
                p.specialize(p.output_buffer().width() >= v).vectorize(x, v);
            }
        };
    }

    // Note that you can define the schedule() method either as a conventional
    // member method, *or*, a public std::function; for the latter approach,
    // you must ensure the value is set by the generate() method.
    // The main reason to do this is to capture the scheduling instructions
    // via a lambda function, allowing you to keep intermediate Funcs and Vars
    // as captured automatic variables in the generate() method, rather than
    // member variables at class scope. There is no intrinsic reason to prefer
    // either approach; it is purely a stylistic preference.
    std::function<void()> schedule;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Pyramid, pyramid)
