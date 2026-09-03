#include "Halide.h"

#include <cstdio>
#include <sstream>

namespace {
std::string to_str(const Halide::Internal::Stmt &s) {
    std::ostringstream ss;
    ss << s;
    return ss.str();
}
std::string to_str(const Halide::Expr &e) {
    std::ostringstream ss;
    ss << e;
    return ss.str();
}
}  // namespace

// rewrite_interleavings() (src/Deinterleave.cpp) deinterleaves a vector
// expression whenever it spots a Ramp/Mod/Div pattern that implies the
// vector is really an interleaving of a few narrower ones. When the
// expression being deinterleaved refers to a vector-typed Let bound
// further out, ExtractLanes doesn't eagerly expand that reference: it
// requests a lane-slice of the Let (Scope<vector<VectorSlice>>
// requested_sliced_lets) and defers materializing it until
// Interleaver::visit_let processes that Let's LetStmt, which then injects
// one new LetStmt per requested slice.
//
// That injection loop isn't reached by any of this repo's ordinary
// Func-scheduling-based tests: it needs a vector Let that both survives
// simplification (so it must be genuinely reused) and is referenced
// alongside a literal Ramp/4-style trigger in the same Store value -- a
// combination Halide's front end doesn't happen to produce for any
// existing schedule. So, in the spirit of correctness/spirv_ir.cpp (which
// also reaches past the public API to test an internal mechanism
// directly), this builds the trigger shape by hand and calls
// rewrite_interleavings() on it directly, then checks both that the
// slice-Let injection fired and that the values it computes are correct.

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Find the Store inside a Stmt built only from For/LetStmt/Store nesting.
const Store *find_store(const Stmt &s) {
    if (const For *f = s.as<For>()) {
        return find_store(f->body);
    }
    if (const LetStmt *l = s.as<LetStmt>()) {
        return find_store(l->body);
    }
    return s.as<Store>();
}

// Get the Store's value with every enclosing LetStmt inlined into it, so
// the result depends only on free variables outside this Stmt (here,
// "xo"). Substituting each Let into its body before recursing means a
// later Let that references an earlier one (as the injected slice Lets
// reference "shared_t") sees the already-inlined definition.
Expr fully_inlined_store_value(const Stmt &s) {
    if (const For *f = s.as<For>()) {
        return fully_inlined_store_value(f->body);
    }
    if (const LetStmt *l = s.as<LetStmt>()) {
        return fully_inlined_store_value(substitute(l->name, l->value, l->body));
    }
    return s.as<Store>()->value;
}

}  // namespace

int main(int argc, char **argv) {
    Expr xo = Variable::make(Int(32), "xo");
    Expr lane_ramp = Ramp::make(xo * 8, 1, 8);

    // A literal Div(Ramp, 4): the shape Interleaver::visit(const Div *)
    // recognizes as needing deinterleaving.
    Expr div_trigger = lane_ramp / 4;

    // A nontrivial, vector-typed value bound via LetStmt, referenced
    // alongside div_trigger in the same Store value -- this Let is what
    // should end up sliced via the injection loop.
    Expr shared_val = lane_ramp * lane_ramp + 1;
    Expr shared_ref = Variable::make(Int(32).with_lanes(8), "shared_t");

    Expr value = div_trigger + shared_ref * 3;
    Stmt store = Store::make("out", value, lane_ramp);
    Stmt let_stmt = LetStmt::make("shared_t", shared_val, store);
    Stmt loop = For::make("xo", 0, 3, ForType::Serial, Partition::Auto, DeviceAPI::None, let_stmt);

    Stmt rewritten = rewrite_interleavings(loop);

    const Store *orig_store = find_store(loop);
    const Store *new_store = find_store(rewritten);
    if (!orig_store || !new_store) {
        printf("Could not find Store in original or rewritten Stmt.\n");
        printf("Rewritten:\n%s\n", to_str(rewritten).c_str());
        return 1;
    }

    // The rewrite should have introduced at least one extra LetStmt beyond
    // the original "shared_t" -- i.e. an injected lane-slice -- as a direct
    // structural signal that the injection loop actually ran.
    int orig_let_depth = 0;
    for (Stmt s = loop; s.as<For>() || s.as<LetStmt>();) {
        if (const For *f = s.as<For>()) {
            s = f->body;
        } else {
            orig_let_depth++;
            s = s.as<LetStmt>()->body;
        }
    }
    int new_let_depth = 0;
    for (Stmt s = rewritten; s.as<For>() || s.as<LetStmt>();) {
        if (const For *f = s.as<For>()) {
            s = f->body;
        } else {
            new_let_depth++;
            s = s.as<LetStmt>()->body;
        }
    }
    if (new_let_depth <= orig_let_depth) {
        printf("Expected rewrite_interleavings to inject additional LetStmts for sliced "
               "requests on 'shared_t', but Let depth went from %d to %d.\n",
               orig_let_depth, new_let_depth);
        printf("Rewritten:\n%s\n", to_str(rewritten).c_str());
        return 1;
    }

    // Now check that the rewrite is semantically equivalent to the
    // original, lane by lane, for a couple of values of the outer loop
    // variable -- by fully constant-folding both Stmts with xo bound to a
    // literal, and comparing what each one stores.
    for (int xo_val : {0, 2}) {
        // substitute() treats the enclosing For's name as a binding site for
        // "xo" and won't shadow into its own loop variable, so substitute
        // into the loop bodies directly rather than the For-wrapped Stmts.
        Stmt orig_folded = substitute("xo", xo_val, loop.as<For>()->body);
        Stmt new_folded = substitute("xo", xo_val, rewritten.as<For>()->body);

        Expr ov_full = fully_inlined_store_value(orig_folded);
        Expr nv_full = fully_inlined_store_value(new_folded);

        for (int lane = 0; lane < 8; lane++) {
            Expr ov = simplify(extract_lane(ov_full, lane));
            Expr nv = simplify(extract_lane(nv_full, lane));
            auto oc = as_const_int(ov);
            auto nc = as_const_int(nv);
            if (!oc || !nc || *oc != *nc) {
                printf("Mismatch at xo=%d, lane=%d\n", xo_val, lane);
                printf("  original lane expr: %s\n", to_str(ov).c_str());
                printf("  rewritten lane expr: %s\n", to_str(nv).c_str());
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
