#include "Halide.h"

namespace {

class VariableNumThreads : public Halide::Generator<VariableNumThreads> {
public:
    Func build() {
        // A job with lots of nested parallelism
        Func f;
        Var x, y;

        f(x, y) = sqrt(sqrt(x*y));
        f.parallel(x).parallel(y);

        return f;
    }
};

Halide::RegisterGenerator<VariableNumThreads> register_my_gen{"variable_num_threads"};

}  // namespace
