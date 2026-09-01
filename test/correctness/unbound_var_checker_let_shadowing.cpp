#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Stage::specialize, Pipeline::add_requirement, and RDom's region bounds all
// reject conditions/bounds that depend on a free Var or RVar. The checks
// backing that must respect Let-shadowing: a Let that binds its own
// variable has no free variable at all, and must not be rejected. Prior to
// this test, the checks used by specialize() and add_requirement() did not
// track Let bindings, so they flagged the mere presence of any Variable
// node -- including one entirely bound by an enclosing Let -- as "free".
int main(int argc, char **argv) {
    // A condition/bounds Expr with a Let that binds its own variable: it
    // has no real free variable, since "t" never escapes this Let.
    Expr self_contained_let = Internal::Let::make("t", 3, Internal::Variable::make(Int(32), "t") == 3);

    Var x;
    Func f("f");
    f(x) = x;

    // Stage::specialize
    f.specialize(self_contained_let);

    // Pipeline::add_requirement
    Pipeline(f).add_requirement(self_contained_let, {});

    // RDom region bounds
    Expr let_min = Internal::Let::make("t", 0, Internal::Variable::make(Int(32), "t"));
    RDom r(let_min, 10);

    Buffer<int> result = f.realize({10});
    for (int i = 0; i < 10; i++) {
        if (result(i) != i) {
            printf("result(%d) = %d instead of %d\n", i, result(i), i);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
