#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {

    // There were scalability problems with taking bounds of nested pure
    // intrinsics. This test hangs if those problems still exist, using the
    // strict float intrinsics. https://github.com/halide/Halide/issues/8686

    Param<float> p1, p2, p2_min, p2_max;
    Scope<Interval> scope;
    scope.push(p2.name(), Interval{p2_min, p2_max});

    for (int limit = 1; limit < 500; limit++) {
        Expr e1 = p1, e2 = p2;
        for (int i = 0; i < limit; i++) {
            e1 = e1 * p1 + (i + 1);
            e2 = e2 * p2 + (i + 1);
        }
        Expr e = e1 + e2;
        bounds_of_expr_in_scope(e, scope);
        e = strictify_float(e);
        bounds_of_expr_in_scope(e, scope);
    }

    printf("Success!\n");

    return 0;
}
