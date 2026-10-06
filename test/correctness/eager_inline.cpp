#include "Halide.h"
#include <set>
#include <sstream>
#include <string>

using namespace Halide;

// eager_inline() performs the substitution immediately, so the caller's
// definition no longer references the inlined Funcs (they are inlined by value).
// Verify that one call flattens chains of any shape among the passed Funcs,
// leaves calls to other Funcs (and their schedules) alone, composes with
// rfactor() and hoist_invariants(), and preserves the pipeline's numerics.

namespace {

// Does the printed form of `e` contain a direct call to Func `name`?
bool mentions(const Expr &e, const std::string &name) {
    std::ostringstream os;
    os << e;
    return os.str().find(name + "(") != std::string::npos;
}

}  // namespace

int main(int argc, char **argv) {
    Var x{"x"};
    Func a{"a"}, b{"b"}, c{"c"};
    a(x) = x + 1;
    b(x) = a(x) * 2;     // calls a
    c(x) = b(x) + a(x);  // calls b (which calls a) and a directly

    // Pass the Funcs in "wrong" (callee-before-caller) order: a before b, even
    // though b's body calls a. eager_inline() topologically sorts them, so both
    // are fully folded regardless of the argument order.
    c.eager_inline(a, b);

    Expr c_body = c.function().definition().values()[0];
    Expr c_expected = x * 3 + 3;
    internal_assert(Internal::can_prove(c_body == c_expected))
        << "eager_inline chain failed to fold all calls to a and b into c\n"
        << "Saw: " << c_body << "\nExpected: " << c_expected << "\n";

    Buffer<int> out = c.realize({8});
    for (int i = 0; i < 8; i++) {
        int ref = (i + 1) * 2 + (i + 1);
        if (out(i) != ref) {
            printf("eager_inline chain mismatch at %d: %d vs %d\n", i, out(i), ref);
            return 1;
        }
    }

    // A longer chain, also passed in a scrambled order, to exercise the
    // topological sort more thoroughly: each Func's body calls the previous one.
    {
        Func d0{"d0"}, d1{"d1"}, d2{"d2"}, d3{"d3"}, sink{"sink"};
        d0(x) = x + 1;
        d1(x) = d0(x) * 2;    // calls d0
        d2(x) = d1(x) + 3;    // calls d1
        d3(x) = d2(x) * 5;    // calls d2
        sink(x) = d3(x) - 4;  // calls d3

        // Scrambled order (not caller-first): the sort must still order them so
        // every call gets folded.
        sink.eager_inline(d2, d0, d3, d1);

        Expr sink_body = sink.function().definition().values()[0];
        internal_assert(!mentions(sink_body, "d0") && !mentions(sink_body, "d1") &&
                        !mentions(sink_body, "d2") && !mentions(sink_body, "d3"))
            << "eager_inline left residual calls after a scrambled-order chain\n"
            << "Saw: " << sink_body << "\n";

        Buffer<int> sout = sink.realize({8});
        for (int i = 0; i < 8; i++) {
            int ref = (((i + 1) * 2 + 3) * 5) - 4;
            if (sout(i) != ref) {
                printf("eager_inline scrambled-chain mismatch at %d: %d vs %d\n", i, sout(i), ref);
                return 1;
            }
        }
    }

    // Passing a Func that this stage does not call is silently ignored: there
    // are no direct calls to fold, so the definition is left unchanged.
    {
        Func p{"p"}, unrelated{"unrelated"}, q{"q"};
        p(x) = x + 1;
        unrelated(x) = x * 100;  // never referenced by q
        q(x) = p(x) + 2;         // calls p, but not unrelated

        // Mix a reachable Func (p) with an unreachable one (unrelated): p is
        // inlined, unrelated is a no-op rather than an error.
        q.eager_inline(unrelated, p);

        Expr q_body = q.function().definition().values()[0];
        internal_assert(!mentions(q_body, "p"))
            << "eager_inline should have inlined the reachable Func p\n"
            << "Saw: " << q_body << "\n";

        Buffer<int> qout = q.realize({8});
        for (int i = 0; i < 8; i++) {
            if (qout(i) != (i + 1) + 2) {
                printf("eager_inline unreachable-arg mismatch at %d: %d vs %d\n", i, qout(i), (i + 1) + 2);
                return 1;
            }
        }
    }

    // eager_inline() is stage-scoped: inlining into one stage leaves the other
    // definitions of the same Func untouched.
    {
        RDom r(0, 4);

        // Stage-level: inline into the update only; the init definition still
        // calls prod.
        Func prod{"prod"}, f{"f"};
        prod(x) = x + 1;
        f(x) = prod(x);       // init definition calls prod
        f(x) += prod(x) * r;  // update definition also calls prod

        f.update(0).eager_inline(prod);

        internal_assert(mentions(f.function().definition().values()[0], "prod"))
            << "Stage::eager_inline on update(0) should not touch the init definition\n";
        internal_assert(!mentions(f.function().update(0).values()[0], "prod"))
            << "Stage::eager_inline on update(0) should have inlined prod into the update\n";

        // Semantics preserved: f(x) = (x+1) + sum_{r=0..3} (x+1)*r = 7*(x+1).
        Buffer<int> fout = f.realize({8});
        for (int i = 0; i < 8; i++) {
            if (fout(i) != 7 * (i + 1)) {
                printf("stage eager_inline mismatch at %d: %d vs %d\n", i, fout(i), 7 * (i + 1));
                return 1;
            }
        }

        // Func-level: targets the init definition only, leaving updates alone.
        Func g{"g"};
        g(x) = prod(x);
        g(x) += prod(x) * r;
        g.eager_inline(prod);
        internal_assert(!mentions(g.function().definition().values()[0], "prod"))
            << "Func::eager_inline should inline prod into the init definition\n";
        internal_assert(mentions(g.function().update(0).values()[0], "prod"))
            << "Func::eager_inline should not touch update definitions\n";
    }

    // A single call flattens every pass-through level, including diamonds and
    // Tuple-valued intermediates, no matter how deep the chain is.
    {
        Func base{"dm_base"}, left{"dm_left"}, right{"dm_right"}, pair{"dm_pair"},
            join{"dm_join"}, sink{"dm_sink"};
        base(x) = x + 1;
        left(x) = base(x) * 2;
        right(x) = base(x) + 3;
        pair(x) = Tuple(left(x), right(x));  // diamond: both sides call base
        join(x) = pair(x)[0] * pair(x)[1];   // calls both Tuple elements
        sink(x) = join(x) - base(x);

        sink.eager_inline(base, pair, right, join, left);

        Expr sink_body = sink.function().definition().values()[0];
        for (const char *name : {"dm_base", "dm_left", "dm_right", "dm_pair", "dm_join"}) {
            internal_assert(!mentions(sink_body, name))
                << "eager_inline left a residual call to " << name << " in a diamond\n"
                << "Saw: " << sink_body << "\n";
        }

        Buffer<int> out = sink.realize({8});
        for (int i = 0; i < 8; i++) {
            int ref = ((i + 1) * 2) * ((i + 1) + 3) - (i + 1);
            if (out(i) != ref) {
                printf("eager_inline diamond mismatch at %d: %d vs %d\n", i, out(i), ref);
                return 1;
            }
        }
    }

    // A Func that is not passed is a boundary: calls to it are left in place, and
    // it keeps its own schedule. Here mid is computed at root, while the levels
    // above it are flattened into sink and the level below it is not reachable.
    {
        Func low{"cb_low"}, mid{"cb_mid"}, high{"cb_high"}, top{"cb_top"}, sink{"cb_sink"};
        low(x) = x + 1;
        mid(x) = low(x) * 2;
        high(x) = mid(x) + 3;
        top(x) = high(x) * 5;
        sink(x) = top(x) - 4;

        mid.compute_root();
        sink.eager_inline(top, high);

        Expr sink_body = sink.function().definition().values()[0];
        internal_assert(!mentions(sink_body, "cb_top") && !mentions(sink_body, "cb_high"))
            << "eager_inline should have flattened top and high into sink\n"
            << "Saw: " << sink_body << "\n";
        internal_assert(mentions(sink_body, "cb_mid"))
            << "eager_inline should have left the call to mid, which was not passed\n"
            << "Saw: " << sink_body << "\n";

        // mid is still realized according to its compute_root() schedule.
        struct FindProducers : public Internal::IRMutator {
            using IRMutator::visit;
            std::set<std::string> names;
            Internal::Stmt visit(const Internal::ProducerConsumer *op) override {
                if (op->is_producer) {
                    names.insert(op->name);
                }
                return IRMutator::visit(op);
            }
        } producers;
        sink.add_custom_lowering_pass(&producers, nullptr);

        Buffer<int> out = sink.realize({8});
        for (int i = 0; i < 8; i++) {
            int ref = (((i + 1) * 2) + 3) * 5 - 4;
            if (out(i) != ref) {
                printf("eager_inline boundary mismatch at %d: %d vs %d\n", i, out(i), ref);
                return 1;
            }
        }
        internal_assert(producers.names.count("cb_mid"))
            << "mid should still be computed at root after eager_inline\n";
        sink.clear_custom_lowering_passes();
    }

    // Schedule directives that are merely meaningless for an inlined Func, such
    // as split(), are allowed on the inlined Funcs; so is scheduling them after
    // eager_inline() has already replaced this stage's calls to them.
    {
        Func p{"sp_p"}, q{"sp_q"};
        Var xo{"xo"}, xi{"xi"};
        p(x) = x * 3;
        q(x) = p(x) + 1;

        p.split(x, xo, xi, 4);
        q.eager_inline(p);
        p.compute_root().vectorize(xi);

        internal_assert(!mentions(q.function().definition().values()[0], "sp_p"))
            << "eager_inline should have inlined a Func with a split\n";
        Buffer<int> out = q.realize({8});
        for (int i = 0; i < 8; i++) {
            if (out(i) != i * 3 + 1) {
                printf("eager_inline split mismatch at %d: %d vs %d\n", i, out(i), i * 3 + 1);
                return 1;
            }
        }
    }

    // Inlining a multi-level chain into the intermediate produced by rfactor()
    // exposes a loop-invariant factor to hoist_invariants(), even when that factor
    // is itself a Func with its own (compute_root) schedule.
    {
        const int width = 6, extent = 16;
        Buffer<int> in(width, extent);
        for (int k = 0; k < extent; k++) {
            for (int i = 0; i < width; i++) {
                in(i, k) = (i * 7 + k * 3) % 11 - 5;
            }
        }
        Buffer<int> vec(extent);
        for (int k = 0; k < extent; k++) {
            vec(k) = k % 5 - 2;
        }

        Var k{"k"}, u{"u"};
        RDom r(0, extent, "r");
        Func scale{"rf_scale"}, load{"rf_load"}, scaled{"rf_scaled"}, weight{"rf_weight"};
        scale(x) = x + 2;
        load(x, k) = in(x, k);
        scaled(x, k) = scale(x) * load(x, k);
        weight(x, k) = scaled(x, k);

        Func acc{"rf_acc"};
        acc(x) = 0;
        acc(x) += weight(x, r) * vec(r);

        RVar ro{"ro"}, ri{"ri"};
        acc.update().split(r, ro, ri, 4);
        Func intm = acc.update().rfactor(ro, u);

        scale.compute_root();
        intm.update().eager_inline(weight, scaled, load);

        Expr update = intm.function().update(0).values()[0];
        internal_assert(!mentions(update, "rf_weight") && !mentions(update, "rf_scaled") &&
                        !mentions(update, "rf_load"))
            << "eager_inline should have flattened the whole chain into the rfactor intermediate\n"
            << "Saw: " << update << "\n";
        internal_assert(mentions(update, "rf_scale"))
            << "eager_inline should have left the call to scale in place\n"
            << "Saw: " << update << "\n";

        FuncVec hoisted = intm.update().hoist_invariants();
        internal_assert(hoisted.size() == 1)
            << "hoist_invariants should have produced one intermediate, got " << hoisted.size() << "\n";
        internal_assert(!mentions(hoisted[0].function().update(0).values()[0], "rf_scale"))
            << "hoist_invariants should have hoisted scale out of the reduction\n";

        Buffer<int> out = acc.realize({width});
        for (int i = 0; i < width; i++) {
            int ref = 0;
            for (int j = 0; j < extent; j++) {
                ref += (i + 2) * in(i, j) * vec(j);
            }
            if (out(i) != ref) {
                printf("eager_inline + rfactor + hoist_invariants mismatch at %d: %d vs %d\n", i, out(i), ref);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
