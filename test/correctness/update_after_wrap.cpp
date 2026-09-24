#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("update_after_wrap", "Func g cannot be given a new update definition, because it has already "
                                           "been realized, used in the definition of another Func, or been the "
                                           "target of a wrapper via in()/clone_in().",
                      []() {
                          Func f("f"), g("g");
                          Var x("x"), y("y");

                          f(x, y) = x + y;
                          g(x, y) = f(x, y);

                          // Wrapping f in g redirects g's existing calls to f to the wrapper, and
                          // freezes g.
                          f.in(g);

                          // This update calls f, but the eager rewrite already happened, so it would
                          // call f directly rather than the wrapper -- inconsistent with g's original
                          // definition. Adding updates to a wrapped consumer is therefore an error.
                          RDom r(0, 10);
                          g(r, r) += f(r, r);
                      });
}
