#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

bool has_loop(const Stage &s, ForType t) {
    for (const Dim &d : s.get_schedule().dims()) {
        if (d.for_type == t) {
            return true;
        }
    }
    return false;
}

size_t num_specializations(const Func &f, int stage) {
    const Definition &def = stage == 0 ? f.function().definition() : f.function().update(stage - 1);
    return def.specializations().size();
}

}  // namespace

int main(int argc, char **argv) {
    Var x("x");
    RDom r(0, 16);
    Param<int> p;

    Func f("f");
    f(x) = x;
    f(r) += 2;

    // An undefined condition specializes nothing: the directives apply to the
    // stage that specialize() was called on.
    f.compute_root();
    f.specialize(Expr()).vectorize(x, 8);
    f.update().specialize(Expr()).unroll(r, 4);
    if (num_specializations(f, 0) != 0 || num_specializations(f, 1) != 0) {
        printf("specialize(Expr()) added a specialization\n");
        return 1;
    }
    if (!has_loop(f, ForType::Vectorized) || !has_loop(f.update(), ForType::Unrolled)) {
        printf("specialize(Expr()) did not return the stage itself\n");
        return 1;
    }

    // Likewise on a specialization: it returns that specialization.
    Stage s = f.specialize(p > 0);
    s.specialize(Expr()).unroll(x, 2);
    if (num_specializations(f, 0) != 1 ||
        !f.function().definition().specializations()[0].definition.specializations().empty() ||
        !has_loop(s, ForType::Unrolled) || has_loop(f, ForType::Unrolled)) {
        printf("specialize(Expr()) on a specialization did not return it\n");
        return 1;
    }

    for (int v : {0, 1}) {
        p.set(v);
        Buffer<int> out = f.realize({32});
        for (int i = 0; i < 32; i++) {
            int correct = i < 16 ? i + 2 : i;
            if (out(i) != correct) {
                printf("p = %d: out(%d) = %d instead of %d\n", v, i, out(i), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
