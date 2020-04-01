#include <memory>
#include <type_traits>

#include "Halide.h"

namespace halide_register_generator {
struct halide_global_ns;
}  // namespace halide_register_generator

namespace {

class VariableNumThreads : public Halide::Generator<VariableNumThreads> {
public:
    Output<Buffer<float>> output{"output", 2};

    void generate() {
        // A job with lots of nested parallelism
        Var x, y;

        output(x, y) = sqrt(sqrt(x * y));
        output.parallel(x).parallel(y);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(VariableNumThreads, variable_num_threads)
