#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// The for type of the loop named `var` (or split from it) in stage s.
ForType loop_type(const Stage &s, const std::string &var) {
    for (const Dim &d : s.get_schedule().dims()) {
        if (d.var == var || ends_with(d.var, "." + var)) {
            return d.for_type;
        }
    }
    return ForType::Extern;  // Not a loop of s.
}

}  // namespace

int main(int argc, char **argv) {
    Var x("x"), y("y"), yo("yo"), yi("yi");
    RDom r(0, 4, "r");

    // The initial definition loops over x and y, the first update over x and
    // r, and the second over r and y.
    Func f("f");
    f(x, y) = x * y;
    f(x, r) += x;
    f(r, y) = f(r, y) * 2;
    f.compute_root().bound(x, 0, 8).bound(y, 0, 4);
    f.update(1).split(y, yo, yi, 2);

    f.vectorize_all_stages(x).unroll_all_stages(r.x, yi);

    bool ok = loop_type(f, "x") == ForType::Vectorized &&
              loop_type(f.update(0), "x") == ForType::Vectorized &&
              loop_type(f.update(0), r.x.name()) == ForType::Unrolled &&
              loop_type(f.update(1), r.x.name()) == ForType::Unrolled &&
              loop_type(f.update(1), "yi") == ForType::Unrolled &&
              loop_type(f.update(1), "yo") == ForType::Serial &&
              loop_type(f, "y") == ForType::Serial;
    if (!ok) {
        printf("The directives did not apply to exactly the stages with the loops\n");
        return 1;
    }

    Buffer<int> out = f.realize({8, 4});
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 8; i++) {
            int correct = i * j + i;
            if (i < 4) {
                correct *= 2;
            }
            if (out(i, j) != correct) {
                printf("out(%d, %d) = %d instead of %d\n", i, j, out(i, j), correct);
                return 1;
            }
        }
    }

    // A loop that no stage has is ignored.
    Func g("g");
    g(x) = x;
    g(r) += 1;
    g.unroll_all_stages(Var("z")).vectorize_all_stages(y);
    if (loop_type(g, "x") != ForType::Serial || loop_type(g.update(), r.x.name()) != ForType::Serial) {
        printf("A loop no stage has changed another loop\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
