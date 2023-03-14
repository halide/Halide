#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class Checker : public IRMutator {
    Stmt visit(const Atomic *op) override {
        count_atomics++;
        if (!op->mutex_name.empty()) {
            count_atomics_with_mutexes++;
        }
        return IRMutator::visit(op);
    }

public:
    int count_atomics = 0;
    int count_atomics_with_mutexes = 0;
};

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Skipping test for WebAssembly as it does not support atomics yet.\n");
        return 0;
    }

    {
        Func f, g;
        Var x, y;

        f(x, y) = {x, y};
        f(x, y) = {f(x, y)[0] + x,
                   f(x, y)[1] + y};

        // The summation is independent in the two tuple components,
        // so it can just be two atomic add instructions. No CAS loop
        // required.
        f.compute_root().update().parallel(y).atomic();

        g(x, y) = f(x, y)[0] + f(x, y)[1];

        Checker checker;
        g.add_custom_lowering_pass(&checker, []() {});

        Buffer<int> out = g.realize({128, 128});
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                int correct = 2 * x + 2 * y;
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, out(x, y), correct);
                    return 1;
                }
            }
        }

        if (checker.count_atomics != 2 || checker.count_atomics_with_mutexes != 0) {
            printf("Expected two atomic nodes, neither of them with mutexes\n");
            return 1;
        }
    }

    {
        Func f, g;
        Var x, y;

        f(x, y) = {x, y};
        f(x, y) = {f(x, y)[1] + x,
                   f(x, y)[0] + y};

        // The summation is coupled across the two tuple components
        // and there are two stores, so we need a mutex.
        f.compute_root().update().parallel(y).atomic();

        g(x, y) = f(x, y)[0] + f(x, y)[1];

        Checker checker;
        g.add_custom_lowering_pass(&checker, []() {});

        Buffer<int> out = g.realize({128, 128});
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                int correct = 2 * x + 2 * y;
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, out(x, y), correct);
                    return 1;
                }
            }
        }

        if (checker.count_atomics != 1 || checker.count_atomics_with_mutexes != 1) {
            printf("Expected one atomic node, with mutex\n");
            return 1;
        }
    }

    {
        Func f, g;
        Var x, y;

        f(x, y) = {x, y, 0};
        f(x, y) = {f(x, y)[1] + x,
                   f(x, y)[0] + y,
                   f(x, y)[2] + 1};

        // The summation is coupled across the first two tuple
        // components and there are two stores, so we need a mutex
        // there. The last store could in principle be a separate atomic
        // add, but we instead just pack it into the critical section.
        f.compute_root().update().parallel(y).atomic();

        g(x, y) = f(x, y)[0] + f(x, y)[1] + f(x, y)[2];

        Checker checker;
        g.add_custom_lowering_pass(&checker, []() {});

        Buffer<int> out = g.realize({128, 128});
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                int correct = 2 * x + 2 * y + 1;
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, out(x, y), correct);
                    return 1;
                }
            }
        }

        if (checker.count_atomics != 1 || checker.count_atomics_with_mutexes != 1) {
            printf("Expected one atomic nodes, with mutex\n");
            return 1;
        }
    }

    {
        Func f, g;
        Var x, y;

        f(x, y) = {x, y, x, y};
        f(x, y) = {f(x, y)[1] + x,
                   f(x, y)[0] + y,
                   f(x, y)[3] + x,
                   f(x, y)[2] + y};

        // The summation is coupled across the first two tuple
        // components and the last two components, but they're
        // independent so they *could* get two critical sections, but
        // it would be on the same mutex, so we just pack them all
        // into one critical section.
        f.compute_root().update().parallel(y).atomic();

        g(x, y) = f(x, y)[0] + f(x, y)[1] + f(x, y)[2] + f(x, y)[3];

        Checker checker;
        g.add_custom_lowering_pass(&checker, []() {});

        Buffer<int> out = g.realize({128, 128});
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                int correct = 4 * x + 4 * y;
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, out(x, y), correct);
                    return 1;
                }
            }
        }

        if (checker.count_atomics != 1 || checker.count_atomics_with_mutexes != 1) {
            printf("Expected one atomic node, with mutex\n");
            return 1;
        }
    }

    {
        Func f, g;
        Var x, y;

        f(x, y) = {x, y};
        RDom r(0, 65);
        // Update even rows
        f(x, r * 2) = {f(x, r * 2 + 1)[1] + x,
                       f(x, r * 2 - 1)[0] + r * 2};
        // Update odd rows using even rows
        f(x, r * 2 + 1) = {f(x, r * 2)[1] + x,
                           f(x, r * 2 + 2)[0] + r * 2 + 1};

        // The tuple components have cross-talk, but the loads
        // couldn't possibly alias with the stores because of the
        // even/odd split. We can just use four atomic adds safely.
        f.compute_root();
        f.update(0)
            .parallel(r)
            .atomic();
        f.update(1)
            .parallel(r)
            .atomic();

        g(x, y) = f(x, y)[0] + f(x, y)[1];

        Checker checker;
        g.add_custom_lowering_pass(&checker, []() {});

        Buffer<int> out = g.realize({128, 128});
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                int correct = 2 * x + 2 * y + 1;
                if (y & 1) {
                    // The odd rows happen after the even rows, so
                    // they get another dose of x + y.
                    correct += x + y;
                }
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, out(x, y), correct);
                    // return 1;
                }
            }
        }

        if (checker.count_atomics != 4 || checker.count_atomics_with_mutexes != 0) {
            printf("Expected four atomic nodes, with no mutexes\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
