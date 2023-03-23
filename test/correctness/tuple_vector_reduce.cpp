#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    // Make sure a tuple-valued associative reduction can be
    // horizontally vectorized.

    {
        // Tuple addition
        Func in;
        Var x;
        in(x) = {x, 2 * x};

        Func f;
        f() = {0, 0};

        const int N = 100;

        RDom r(1, N);
        f() = {f()[0] + in(r)[0], f()[1] + in(r)[1]};

        in.compute_root();
        f.update().atomic().vectorize(r, 8).parallel(r);

        class CheckIR : public IRMutator {
            using IRMutator::visit;
            Expr visit(const VectorReduce *op) override {
                vector_reduces++;
                return IRMutator::visit(op);
            }
            Stmt visit(const Atomic *op) override {
                atomics++;
                mutexes += (!op->mutex_name.empty());
                return IRMutator::visit(op);
            }

        public:
            int atomics = 0, mutexes = 0, vector_reduces = 0;
        } checker;

        f.add_custom_lowering_pass(&checker, []() {});

        Realization result = f.realize();
        int a = Buffer<int>(result[0])();
        int b = Buffer<int>(result[1])();
        if (a != (N * (N + 1)) / 2 || b != N * (N + 1)) {
            printf("Incorrect output: %d %d\n", a, b);
            return 1;
        }

        if (!checker.vector_reduces) {
            printf("Expected VectorReduce nodes\n");
            return 1;
        }

        if (!checker.atomics) {
            printf("Expected atomic nodes\n");
            return 1;
        }

        if (checker.mutexes) {
            printf("Did not expect mutexes\n");
            return 1;
        }
    }

    {
        // Complex multiplication is associative. Let's multiply a bunch
        // of complex numbers together.
        Func in;
        Var x;
        in(x) = {cos(cast<float>(x)), sin(cast<float>(x))};

        Func f;
        f() = {1.0f, 0.0f};

        RDom r(1, 50);
        Expr a_real = f()[0];
        Expr a_imag = f()[1];
        Expr b_real = in(r)[0];
        Expr b_imag = in(r)[1];
        f() = {a_real * b_real - a_imag * b_imag,
               a_real * b_imag + b_real * a_imag};

        in.compute_root();
        f.update().atomic().vectorize(r, 8);

        // Sadly, this won't actually vectorize, because it's not
        // expressible as a horizontal reduction op on a single
        // vector. You'd need to rfactor. We can at least check we get
        // the right value back though.
        Realization result = f.realize();
        float a = Buffer<float>(result[0])();
        float b = Buffer<float>(result[1])();
        // We multiplied a large number of complex numbers of magnitude 1.
        float mag = a * a + b * b;
        if (mag <= 0.9 || mag >= 1.1) {
            printf("Should have been magnitude one: %f + %f i\n", a, b);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
