#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("rvar_fuse_reorder", "can't reorder RVars", []() {
        Func f("f");
        Var x("x");
        RDom r(0, 4, 0, 3, "r");
        RVar rxo("rxo"), rxi("rxi"), fused("fused");
        f(x) = 1;
        f(x) = f(x) * 2 + r.x * 5 + r.y;

        // this fuse reorders rvars despite the update being non-commutative
        f.update().split(r.x, rxo, rxi, 2).fuse(rxi, r.y, fused);
    });
}
