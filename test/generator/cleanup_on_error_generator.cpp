#include "Halide.h"

namespace {

class CleanupOnError : public Halide::Generator<CleanupOnError> {
public:
    Func build() {
        Var x;

        // This allocation is going to succeed

        Func f;
        f(x) = x;
        f.compute_root();

        Target target = get_target();
        if (target.has_gpu_feature()) {
            Var xo, xi;
            f.gpu_tile(x, xo, xi, 16);
        }

        // This one is going to fail (because we'll override
        // halide_malloc to make it fail). The first allocation should
        // be cleaned up when the second one fails.
        Func g;
        g(x) = f(2*x) + f(2*x+1);

        g.compute_root();

        Func h;
        h(x) = g(x) + 1;

        return h;
    }
};

Halide::RegisterGenerator<CleanupOnError> register_my_gen{"cleanup_on_error"};

}  // namespace
