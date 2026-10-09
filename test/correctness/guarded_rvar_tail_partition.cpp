#include "Halide.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Internal;

// When an RVar is split with TailStrategy::GuardWithIf, and the inner RVar is
// then rfactored, the RVar is clamped to the reduction domain in the
// intermediate, so bounds inference clamps the region a producer computed at
// the outer RVar must compute to the end of the reduction domain. That clamp
// only has an effect in the last (tail) iteration, so it should be
// partitioned out of the steady state, leaving constant-extent producer
// loops, constant-size allocations, and no loop-dependent guards there.

namespace {

// Is the body of this loop free of loop-dependent if statements, inner loops
// with non-constant extents, and non-constant-size allocations?
bool is_clean(const For *loop) {
    Scope<> loops;
    loops.push(loop->name);
    bool clean = true;
    visit_with(
        loop->body,
        [&](auto *self, const For *op) {
            if (!is_const(simplify(op->max - op->min))) {
                clean = false;
            }
            ScopedBinding<> bind(loops, op->name);
            self->visit_base(op);
        },
        [&](auto *self, const IfThenElse *op) {
            if (expr_uses_vars(op->condition, loops)) {
                clean = false;
            }
            self->visit_base(op);
        },
        [&](auto *self, const Allocate *op) {
            if (op->constant_allocation_size() == 0) {
                clean = false;
            }
            self->visit_base(op);
        });
    return clean;
}

bool ends_with(const std::string &str, const std::string &suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Loop partitioning may make several copies of a loop. Check that one of them
// with a non-constant trip count (the steady state) is clean.
bool has_clean_steady_state(const Module &m, const std::string &loop_suffix) {
    bool found_loop = false, found_clean = false;
    for (const auto &f : m.functions()) {
        visit_with(
            f.body,
            [&](auto *self, const For *op) {
                if (ends_with(op->name, loop_suffix) ||
                    ends_with(op->name, loop_suffix + ".rebased")) {
                    found_loop = true;
                    found_clean |= !is_const(simplify(op->max - op->min)) && is_clean(op);
                }
                self->visit_base(op);
            });
    }
    if (!found_loop) {
        printf("Did not find any loop ending in %s\n", loop_suffix.c_str());
    }
    return found_clean;
}

struct Pipeline {
    Func f;
    ImageParam a, b;
    Param<int> n;
    std::string outer_loop;
};

// A dot product of rows, blocked over rows. The per-row sums are an rfactor
// intermediate computed per block of rows of a vectorized accumulator.
Pipeline rfactor_pipeline(int factor) {
    Pipeline p{Func("f"), ImageParam(Int(32), 2, "a"), ImageParam(Int(32), 2, "b"), Param<int>("n"), ".ryo"};
    RDom r(0, 16, 0, p.n, "r");
    p.f() = 0;
    p.f() += p.a(r.x, r.y) * p.b(r.x, r.y);

    Var u("u"), v("v");
    RVar ryo("ryo"), ryi("ryi");
    Func rows = p.f.update().rfactor(r.y, u);
    p.f.update().split(r.y, ryo, ryi, factor, TailStrategy::GuardWithIf);
    Func acc = p.f.update().rfactor(ryi, v);
    acc.compute_root().vectorize(v);
    acc.update().vectorize(v);
    rows.compute_at(acc, ryo);
    rows.update().atomic().vectorize(r.x, 8);
    return p;
}

// A GuardWithIf split of the pure var of an rfactor intermediate, with a
// pure producer computed at the outer var. Here the clamp comes from the split
// of a pure var, and must stay out of the steady state too.
Pipeline rfactor_split_pure_var_pipeline(int factor) {
    Pipeline p{Func("f"), ImageParam(Int(32), 2, "a"), ImageParam(Int(32), 2, "b"), Param<int>("n"), ".uo"};
    Var y("y"), u("u"), uo("uo"), ui("ui");
    Func rows("rows");
    Expr row_sum = 0;
    for (int k = 0; k < 16; k++) {
        row_sum += p.a(k, y) * p.b(k, y);
    }
    rows(y) = row_sum;

    RDom r(0, p.n, "r");
    p.f() = 0;
    p.f() += rows(r);

    Func intm = p.f.update().rfactor(r, u);
    intm.compute_root();
    intm.update().split(u, uo, ui, factor, TailStrategy::GuardWithIf).vectorize(ui);
    rows.compute_at(intm, uo).unroll(y);
    return p;
}

int check(const char *name, Pipeline (*make)(int)) {
    for (int factor : {3, 4, 8}) {
        Pipeline p = make(factor);
        Target t = get_jit_target_from_environment();

        Module m = p.f.compile_to_module({p.a, p.b, p.n}, "f", t);
        if (!has_clean_steady_state(m, p.outer_loop)) {
            printf("%s, factor %d: the clamp to the end of the reduction domain "
                   "was not partitioned out of the steady state of the %s loop\n",
                   name, factor, p.outer_loop.c_str());
            return 1;
        }

        p.f.compile_jit(t);
        for (int n = 1; n <= 3 * factor + 1; n++) {
            Buffer<int> a(16, n), b(16, n);
            int correct = 0;
            for (int y = 0; y < n; y++) {
                for (int x = 0; x < 16; x++) {
                    a(x, y) = (x * 7 + y * 3) % 11 - 5;
                    b(x, y) = (x * 5 + y * 13) % 9 - 4;
                    correct += a(x, y) * b(x, y);
                }
            }
            p.a.set(a);
            p.b.set(b);
            p.n.set(n);
            Buffer<int> out = p.f.realize();
            if (out() != correct) {
                printf("%s, factor %d, n = %d: %d instead of %d\n",
                       name, factor, n, out(), correct);
                return 1;
            }
        }
    }
    return 0;
}

// Marking the clamp as rarely having an effect must not get in the way of
// the sliding window optimization and storage folding of a producer stored
// outside the outer RVar.
int check_storage_folding() {
    ImageParam a(Int(32), 1, "a");
    Func g("g"), f("f");
    Var x("x"), v("v");
    RVar ro("ro"), ri("ri");
    g(x) = a(x) * 3;
    RDom r(0, 1001, "r");
    f() = 0;
    f() += g(r) + g(r + 1);
    f.update().split(r, ro, ri, 8, TailStrategy::GuardWithIf);
    Func intm = f.update().rfactor(ri, v);
    intm.compute_root();
    g.compute_at(intm, ro).store_root();

    Module m = f.compile_to_module({a}, "f", get_jit_target_from_environment());
    bool folded = false;
    for (const auto &fn : m.functions()) {
        visit_with(
            fn.body,
            [&](auto *self, const Allocate *op) {
                if (op->name == "g") {
                    auto size = as_const_int(simplify(op->extents[0]));
                    folded = op->extents.size() == 1 && size && *size <= 16;
                }
                self->visit_base(op);
            });
    }
    if (!folded) {
        printf("The producer computed at the outer RVar was not folded\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (check("rfactor", rfactor_pipeline) ||
        check("rfactor with split pure var", rfactor_split_pure_var_pipeline) ||
        check_storage_folding()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
