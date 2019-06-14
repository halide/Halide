#include <cstdlib>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f0{"f0"}, f1{"f1"}, cost{"cost"};
    Var x;
    f0(x) = x;
    f1(x) = x;

    RDom r(0, 100);
    cost() = 0.f;
    cost() += f0(r.x);
    cost() += f1(r.x);

    f0.compute_root();
    f1.compute_root();

    // Move the reductions into their own Funcs
    Func cost_intm = cost.update(0).rfactor({});
    Func cost_intm_1 = cost.update(1).rfactor({});

    cost_intm.compute_root();
    cost_intm_1.compute_root();

    // Now that they're independent funcs, we can fuse the loops using compute_with
    cost_intm.update().compute_with(cost_intm_1.update(), r.x);

    Buffer<float> result = cost.realize();
    const float expected_result = 9900.f; // 2 * sum(0..99)

    return result(0) == expected_result ? EXIT_SUCCESS : EXIT_FAILURE;
}
