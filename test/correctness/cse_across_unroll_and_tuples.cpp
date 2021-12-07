#include <Halide.h>

using namespace Halide;
using namespace Halide::Internal;

class CountSqrt : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->name == "sqrt_f32") {
            result++;
        }
        return IRMutator::visit(op);
    }

public:
    int result = 0;
};

int main(int argc, char **argv) {
    // Check that values shared across tuple elements or unrolled loops only get
    // computed once in the Halide IR. LLVM will hoist them if we don't, but
    // compilation can be much faster if we do it earlier, especially if the
    // unrolled loop or tuple is large.
    for (bool use_tuple : {false, true}) {
        Func f;
        Var x, y, c;
        ImageParam in(Float(32), 2);

        Func g;
        g(x, y) = sqrt(in(x, y));

        if (use_tuple) {
            f(x, y) = {g(x, y) + 1, g(x, y) + 2, g(x, y) + 3};
        } else {
            f(x, y, c) = g(x, y);
            f.bound(c, 0, 3).reorder(c, x, y).unroll(c);
        }

        CountSqrt counter;
        f.add_custom_lowering_pass(&counter, nullptr);

        // There should only be one sqrt call, not 1024.
        f.compile_jit();

        if (counter.result != 1) {
            printf("Wrong number of sqrt calls in lowered code: %d instead of 1\n", counter.result);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
