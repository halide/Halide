#include "Halide.h"

#include <cstdio>

using namespace Halide;

// Stage::is_order_independent() says whether atomic() may vectorize or
// parallelize an update's RVars, and whether they may be reordered.

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

int main(int argc, char **argv) {
    Var x("x");
    RDom r(0, 16, "r");
    Func in("in");
    in(x) = cast<float>(x % 7) - 3.0f;

    Func sum("sum"), mx("mx"), argmin("argmin"), strict("strict"), diff("diff"), scatter("scatter");
    sum() = 0.0f;
    sum() += in(r);
    mx() = -1000.0f;
    mx() = max(mx(), in(r));
    argmin() = {1000.0f, 0};
    argmin() = {min(argmin()[0], in(r)), select(argmin()[0] < in(r), argmin()[1], r)};
    strict() = 0.0f;
    strict() = strict_float(strict() + in(r));
    diff() = 0.0f;
    diff() = in(r)-diff();
    scatter(x) = 0.0f;
    scatter(r % 4) = 2.0f;

    CHECK(sum.update().is_order_independent());
    CHECK(mx.update().is_order_independent());
    CHECK(argmin.update().is_order_independent());
    CHECK(!strict.update().is_order_independent());
    CHECK(!diff.update().is_order_independent());
    // Doesn't read what it updates.
    CHECK(scatter.update().is_order_independent());
    // A pure definition has no reduction to order.
    CHECK(Stage(in).is_order_independent());

    // As atomic() requires to vectorize the RVar.
    for (Func f : {sum, mx, scatter}) {
        f.update().atomic().vectorize(r, 8);
    }
    Buffer<float> s = sum.realize();
    CHECK(s() == -5.0f);
    Buffer<float> m = mx.realize();
    CHECK(m() == 3.0f);
    if (Halide::exceptions_enabled()) {
        for (Func f : {strict, diff}) {
            bool error = false;
            try {
                f.update().atomic().vectorize(r, 8);
            } catch (const CompileError &) {
                error = true;
            }
            CHECK(error);
        }
    }

    printf("Success!\n");
    return 0;
}
