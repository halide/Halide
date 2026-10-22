#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;
using namespace Halide::Internal;

int main() {
    return error_test("wrapper_never_used", "Cannot wrap Func \"f\" in \"g\" because \"g\" does not call \"f\".", []() {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");
        f(x, y) = x + y;
        g(x, y) = 5;
        h(x, y) = f(x, y) + g(x, y);

        f.compute_root();
        f.in(g).compute_root();

        // This should cause an error since f.in(g) was called but 'f' is
        // never used in 'g'.
        h.realize({5, 5});
    });
}
