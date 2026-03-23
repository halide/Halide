#include "Halide.h"

namespace {

class VariableNumThreads : public Halide::Generator<VariableNumThreads> {
public:
    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        // A job with lots of nested parallelism
        Var x, y;

        output(x, y) = sqrt(sqrt(x * y));
        output.parallel(x).parallel(y);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(VariableNumThreads, variable_num_threads)
