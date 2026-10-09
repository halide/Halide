#include "Halide.h"
#include <cstdio>
#include <limits>

using namespace Halide;
using namespace Halide::Internal;

// When an RVar is split with TailStrategy::GuardWithIf and the inner RVar is
// preserved by rfactor, the elements of the intermediate that the tail never
// reaches still hold the identity. If combining the identity into the result
// is a no-op, the stage that combines the intermediate into the result
// doesn't need the tail guard, and so shouldn't depend on the extent of the
// reduction domain.

namespace {

// Does the producer of `func` contain any if statements or loops with
// non-constant extents?
bool producer_is_guarded(const Module &m, const std::string &func) {
    bool guarded = false;
    for (const auto &f : m.functions()) {
        visit_with(
            f.body,
            [&](auto *self, const ProducerConsumer *op) {
                if (op->is_producer && op->name == func) {
                    visit_with(
                        op->body,
                        [&](auto *self, const IfThenElse *op) {
                            guarded = true;
                            self->visit_base(op);
                        },
                        [&](auto *self, const For *op) {
                            guarded |= !is_const(simplify(op->max - op->min));
                            self->visit_base(op);
                        });
                }
                self->visit_base(op);
            });
    }
    return guarded;
}

// A sum with a few independent accumulators, combined by an unrolled loop.
int check_sum(int factor) {
    ImageParam in(Float(32), 1, "in");
    RDom r(0, in.dim(0).extent(), "r");
    RVar ro("ro"), ri("ri");
    Var u("u");
    Func out("out");
    out() = 0.0f;
    out() += in(r);
    out.update().split(r, ro, ri, factor, TailStrategy::GuardWithIf);
    Func intm = out.update().rfactor(ri, u);
    intm.compute_root().bound(u, 0, factor).unroll(u).update().unroll(u);
    out.update().unroll(ri);

    Target t = get_jit_target_from_environment();
    Module m = out.compile_to_module({in}, "out", t);
    if (producer_is_guarded(m, "out")) {
        printf("Combining the intermediate into the result depends on the extent "
               "of the reduction domain (factor %d)\n",
               factor);
        return 1;
    }

    out.compile_jit(t);
    for (int n = 0; n <= 3 * factor + 1; n++) {
        Buffer<float> buf(std::max(n, 1));
        buf.crop(0, 0, n);
        float correct = 0.0f;
        for (int i = 0; i < n; i++) {
            buf(i) = (float)(i % 7) - 3.0f;
            correct += buf(i);
        }
        in.set(buf);
        Buffer<float> result = out.realize();
        if (result() != correct) {
            printf("Sum with factor %d, n = %d: %f instead of %f\n",
                   factor, n, result(), correct);
            return 1;
        }
    }
    return 0;
}

// An argmax that keeps the index of the latest maximum. Combining the
// identity into the result would replace the index with the identity's when
// the maximum is the smallest int, so the guard must stay. (We only use
// extents up to the split factor, because rfactor treats this op as
// commutative, which changes which of several tied maxima wins.)
int check_argmax(int factor) {
    ImageParam in(Int(32), 1, "in");
    RDom r(5, in.dim(0).extent(), "r");
    RVar ro("ro"), ri("ri");
    Var u("u");
    Func out("out");
    out() = {Int(32).min(), -1};
    out() = {max(out()[0], in(r - 5)), select(out()[0] > in(r - 5), out()[1], r)};
    out.update().split(r, ro, ri, factor, TailStrategy::GuardWithIf);
    Func intm = out.update().rfactor(ri, u);
    intm.compute_root();

    out.compile_jit();
    for (int n = 1; n <= factor; n++) {
        Buffer<int> buf(n);
        buf.fill(std::numeric_limits<int>::min());
        in.set(buf);
        Realization result = out.realize();
        Buffer<int> index = result[1];
        int correct = 5 + n - 1;
        if (index() != correct) {
            printf("Argmax with factor %d, n = %d: index %d instead of %d\n",
                   factor, n, index(), correct);
            return 1;
        }
    }
    return 0;
}

// A user predicate on the reduction domain must still apply.
int check_sum_with_predicate(int factor) {
    ImageParam in(Int(32), 1, "in");
    RDom r(0, in.dim(0).extent(), "r");
    r.where(r % factor != 1);
    RVar ro("ro"), ri("ri");
    Var u("u");
    Func out("out");
    out() = 0;
    out() += in(r);
    out.update().split(r, ro, ri, factor, TailStrategy::GuardWithIf);
    Func intm = out.update().rfactor(ri, u);
    intm.compute_root();

    out.compile_jit();
    for (int n = 0; n <= 3 * factor + 1; n++) {
        Buffer<int> buf(std::max(n, 1));
        buf.crop(0, 0, n);
        int correct = 0;
        for (int i = 0; i < n; i++) {
            buf(i) = i * 3 + 1;
            if (i % factor != 1) {
                correct += buf(i);
            }
        }
        in.set(buf);
        Buffer<int> result = out.realize();
        if (result() != correct) {
            printf("Sum with predicate with factor %d, n = %d: %d instead of %d\n",
                   factor, n, result(), correct);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    for (int factor : {2, 3, 4}) {
        if (check_sum(factor) ||
            check_argmax(factor) ||
            check_sum_with_predicate(factor)) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
