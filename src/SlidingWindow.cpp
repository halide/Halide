#include "SlidingWindow.h"

#include "Bounds.h"
#include "CSE.h"
#include "Debug.h"
#include "ExprUsesVar.h"
#include "FindCalls.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Monotonic.h"
#include "Scope.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include <list>
#include <set>
#include <utility>

// Sliding window is the optimization that turns a stencil pipeline like this:
//
//   for y:
//     produce f over [y-1, y+1]   // three rows, one per iteration
//     consume f to make g(y)
//
// into this:
//
//   for y:
//     produce f over [y+1, y+1]   // one row, reusing the two from last time
//     consume f to make g(y)
//
// It applies when a Func's storage outlives its computation (store_at is
// outside compute_at), and the region of it required moves monotonically as
// some serial loop between those two levels advances. Then each iteration only
// needs to compute the sliver of the Func that the previous iteration didn't.
//
// The wrinkle is the first iteration, which has no previous iteration to
// inherit from. There are two ways this pass deals with that:
//
// - Explicit warm-up. The region computed becomes a select: on the first
//   iteration compute the whole thing, and after that just the sliver. This
//   always applies, but it leaves a select in the bounds, which is bad for
//   loop partitioning and for anything downstream that wants simple bounds.
//
// - Implicit warm-up, by rewinding the loop. We instead back the loop min up
//   by however many iterations it takes for the slivers to add up to the whole
//   region, and leave the steady-state bounds alone. The extra iterations at
//   the front produce nothing but the warm-up, so everything in the loop body
//   that isn't part of warming a window up must be guarded to only run from
//   the original loop min onwards. This is the preferred form, and is what
//   makes this pass tricky, because several Funcs may want to rewind the same
//   loop by different amounts, and their warm-ups have to interleave
//   correctly: a Func's warm-up iterations may need values from a Func
//   upstream of it, which must therefore start warming up even earlier.
//
// So for each serial loop we:
//
//   1. Order the Funcs realized around it consumers-first, so that by the time
//      we slide a Func we know how far back its consumers start, and can warm
//      it up in time for them (SlidingWindow::consumers_first).
//   2. Slide each one, rewinding the loop min further as needed
//      (SlidingWindowOnFunctionAndLoop).
//   3. Work out, for every statement in the body, which iteration it starts at,
//      and guard it accordingly (InjectWarmupGuards). Statements don't all
//      share one start: a Func needed by a warming-up Func has to start when
//      that Func does, which is before the real work starts but after the
//      earliest warm-up iteration.
//
// A Func with MemoryType::Register slides differently. The region it writes
// doesn't shrink at all. Instead each store becomes a select: on the leading
// edge of the window it computes the value, and everywhere else it copies the
// value the previous iteration left in a register. With the loops unrolled,
// those copies cost nothing (RollFunc).

// Worked example. For:
//
//   f(x) = ...;
//   g(x) = f(x) + f(x - 1);
//   f.store_root().compute_at(g, x);
//
// the loop nest arrives here looking like this:
//
//   for (g.s0.x, g.s0.x.loop_min, g.s0.x.loop_max) {
//     let f.s0.x.min = g.s0.x + -1
//     let f.s0.x.max = g.s0.x
//     produce f { for (f.s0.x, f.s0.x.min, f.s0.x.max) { f(f.s0.x) = ... } }
//     consume f { g(g.s0.x) = f(g.s0.x) + f(g.s0.x - 1) }
//   }
//
// and leaves looking like this. The region required of f has shrunk to the one
// value per iteration that the previous iteration didn't compute, the loop
// starts an iteration earlier so that there always is a previous iteration,
// and g is guarded so that it doesn't run on that extra iteration:
//
//   let g.s0.x.$n.loop_min = g.s0.x.loop_min + -1
//   for (g.s0.x.$n, g.s0.x.$n.loop_min, g.s0.x.loop_max) {
//     let f.s0.x.min = max(g.s0.x.loop_min + -1, g.s0.x.$n)
//     let f.s0.x.max = g.s0.x.$n
//     produce f { for (f.s0.x, f.s0.x.min, f.s0.x.max) { f(f.s0.x) = ... } }
//     consume f {
//       if (likely_if_innermost(g.s0.x.loop_min <= g.s0.x.$n)) {
//         g(g.s0.x.$n) = f(g.s0.x.$n) + f(g.s0.x.$n - 1)
//       }
//     }
//   }
//
// The max in f's new min is what stops the warm-up iteration computing values
// from before the start of the region g will ever ask for.
//
// Second worked example, showing why one guard isn't enough. For:
//
//   e(x) = ...;
//   f(x) = e(x) + e(x - 3);
//   g(x) = f(x) + f(x - 1);
//   h(x) = g(x) + g(x - 1) + e(x);
//   e.store_root().compute_at(h, x);
//   f.compute_at(h, x);            // not sliding
//   g.store_root().compute_at(h, x);
//
// e needs five warm-up iterations and g needs one, so the loop rewinds by
// five, and three different things start at three different iterations:
//
//   for (h.s0.x.$n.$n, min(h.s0.x.loop_min - 5, h.s0.x.loop_min - 1), ...) {
//     produce e { ... }                             // warming up: no guard
//     consume e {
//       if (likely_if_innermost(h.s0.x.loop_min + -1 <= h.s0.x.$n.$n)) {
//         produce f { ... }                         // g's warm-up needs this,
//       }                                           // e's does not
//       consume f {
//         produce g { ... }                         // warming up: no guard
//         consume g {
//           if (likely_if_innermost(h.s0.x.loop_min <= h.s0.x.$n.$n)) {
//             h(h.s0.x.$n.$n) = ...                 // the real work
//           }
//         }
//       }
//     }
//   }
//
// f must run during g's warm-up, or g warms up with garbage. It must not run
// during e's, both because that's wasted work and because a Func that is
// itself sliding over some enclosing loop would have its own window corrupted
// by the extra iterations.

namespace Halide {
namespace Internal {

using std::list;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

class ExpandExpr : public IRMutator {
    using IRMutator::visit;
    const Scope<Expr> &scope;

    Expr visit(const Variable *var) override {
        if (const Expr *expr = scope.find(var->name)) {
            debug(4) << "Fully expanded " << var->name << " -> " << *expr << "\n";
            return *expr;
        } else {
            return var;
        }
    }

public:
    ExpandExpr(const Scope<Expr> &s)
        : scope(s) {
    }
};

// Perform all the substitutions in a scope
Expr expand_expr(const Expr &e, const Scope<Expr> &scope) {
    ExpandExpr ee(scope);
    Expr result = common_subexpression_elimination(ee(e));
    debug(4) << "Expanded " << e << " into " << result << "\n";
    return result;
}

// This mutator rewrites calls and provides to a particular
// func:
// - Calls and Provides are shifted to be relative to the min.
// - Provides additionally are rewritten to load values from the
//   previous iteration of the loop if they were computed in the
//   last iteration.
class RollFunc : public IRMutator {
    const Function &func;
    int dim;
    const string &loop_var;
    const Interval &old_bounds;
    const Interval &new_bounds;

    Scope<Expr> scope;

    // It helps simplify the shifted calls/provides to rebase the
    // loops that are subtracted from to have a min of 0.
    set<string> loops_to_rebase;
    bool in_produce = false;

    using IRMutator::visit;

    Stmt visit(const ProducerConsumer *op) override {
        bool produce_func = op->name == func.name() && op->is_producer;
        ScopedValue<bool> old_in_produce(in_produce, in_produce || produce_func);
        return IRMutator::visit(op);
    }

    Stmt visit(const Provide *op) override {
        if (!(in_produce && op->name == func.name())) {
            return IRMutator::visit(op);
        }
        vector<Expr> values = op->values;
        for (Expr &i : values) {
            i = mutate(i);
        }
        vector<Expr> args = op->args;
        for (Expr &i : args) {
            i = mutate(i);
        }
        bool sliding_up = old_bounds.max.same_as(new_bounds.max);
        Expr is_new = sliding_up ? new_bounds.min <= args[dim] : args[dim] <= new_bounds.max;
        args[dim] -= old_bounds.min;
        vector<Expr> old_args = args;
        Expr old_arg_dim = expand_expr(old_args[dim], scope);
        old_args[dim] = substitute(loop_var, Variable::make(Int(32), loop_var) - 1, old_arg_dim);
        for (int i = 0; i < (int)values.size(); i++) {
            Type t = values[i].type();
            Expr old_value =
                Call::make(t, op->name, old_args, Call::Halide, func.get_contents(), i);
            values[i] = Call::make(values[i].type(), Call::if_then_else, {is_new, values[i], likely(old_value)}, Call::PureIntrinsic);
        }
        if (const Variable *v = op->args[dim].as<Variable>()) {
            // The subtractions above simplify more easily if the loop is rebased to 0.
            loops_to_rebase.insert(v->name);
        }
        return Provide::make(func.name(), values, args, op->predicate);
    }

    Expr visit(const Call *op) override {
        if (!(op->call_type == Call::Halide && op->name == func.name())) {
            return IRMutator::visit(op);
        }
        vector<Expr> args = op->args;
        for (Expr &i : args) {
            i = mutate(i);
        }
        args[dim] -= old_bounds.min;
        return Call::make(op->type, op->name, args, Call::Halide, op->func, op->value_index, op->image, op->param);
    }

    Stmt visit(const For *op) override {
        Stmt result = IRMutator::visit(op);
        op = result.as<For>();
        internal_assert(op);
        if (loops_to_rebase.count(op->name)) {
            string new_name = op->name + ".rebased";
            Stmt body = substitute(op->name, Variable::make(Int(32), new_name) + op->min, op->body);
            // use op->name *before* the re-assignment of result, which will clobber it
            loops_to_rebase.erase(op->name);
            result = For::make(new_name, 0, op->max - op->min, op->for_type, op->partition_policy, op->device_api, body);
        }
        return result;
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<Expr> bind(scope, op->name, simplify(expand_expr(op->value, scope)));
        return IRMutator::visit(op);
    }

public:
    RollFunc(const Function &func, int dim, const string &loop_var,
             const Interval &old_bounds, const Interval &new_bounds)
        : func(func), dim(dim), loop_var(loop_var), old_bounds(old_bounds), new_bounds(new_bounds) {
    }
};

// Everything sliding window decided to do with one Func over one loop. This
// is the entire output of the analysis - the rewrite consumes it and makes no
// decisions of its own, and the guards injected afterwards are derived from
// the decisions for all the Funcs around a loop.
// The name the schedule used for the dimension a loop or let carries. Loop
// and let names in the IR are mangled, so error messages use this instead.
string slide_level_name(const Function &f, const string &loop) {
    for (const LoopLevel &l : f.schedule().slide_levels()) {
        if (l.defined() && !l.is_inlined() && !l.is_root() && l.match(loop)) {
            return l.to_string();
        }
    }
    return loop;
}

// A window can only slide over one schedule dimension per dimension of the
// Func's storage. Naming two that move the same one is a scheduling error, so
// this rejection reason gets compared against by pointer.
constexpr const char *already_slid = "this dimension has already been slid over";

struct SlideDecision {
    // What we were asked to consider. Today we always slide a Func over a
    // serial loop that we picked ourselves, but the schedule will eventually
    // be able to name what to slide over, and it won't necessarily be a loop -
    // it may be a Var that got split into several of them. Anything the
    // schedule gets to say belongs in this group.
    string func_name;
    string slide_over;

    // Everything below is what the analysis concluded.

    // Whether we got as far as looking at this Func's producer.
    bool considered = false;

    // Why we're not sliding, or null if we are.
    const char *rejected = nullptr;

    bool slid() const {
        return considered && rejected == nullptr;
    }

    // Ref-qualified, so that the reference returned can't outlive a temporary.
    SlideDecision &reject(const char *why) & {
        rejected = why;
        return *this;
    }

    // The dimension we slide along.
    string dim;
    int dim_idx = 0;

    // Whether the window moves up (the min increases) or down.
    bool slide_up = true;

    // The region of this Func required at each iteration of the loop, before
    // and after sliding. Sliding shrinks it to just the sliver the previous
    // iteration didn't compute.
    Interval old_bounds, new_bounds;

    // If defined, the iteration the loop should rewind to, so that the
    // steady-state new_bounds above are warmed up by the time the real work
    // starts. If undefined, new_bounds instead contains a select that computes
    // the whole region on the first iteration. Software pipelining would push
    // this earlier still, and would want a matching iteration to stop at.
    Expr warmup_start;
};

std::ostream &operator<<(std::ostream &s, const SlideDecision &d) {
    if (!d.considered) {
        return s << "Never reached " << d.func_name << "'s producer\n";
    } else if (!d.slid()) {
        return s << "Not sliding " << d.func_name << " over " << d.slide_over
                 << " because " << d.rejected << "\n";
    }
    s << "Sliding " << d.func_name << "." << d.dim << " over " << d.slide_over
      << (d.slide_up ? " upwards\n" : " downwards\n")
      << "  Region required per iteration: [" << d.old_bounds.min << ", "
      << d.old_bounds.max << "]\n"
      << "  Shrunk to:                     [" << d.new_bounds.min << ", "
      << d.new_bounds.max << "]\n";
    if (d.warmup_start.defined()) {
        s << "  Rewinding the loop to: " << d.warmup_start << "\n";
    } else {
        s << "  Warming up on the first iteration instead of rewinding\n";
    }
    return s;
}

// Perform sliding window optimization for a function over a particular serial
// for loop. Rewrites the lets that give the region required of the func to
// only cover the sliver newly required this iteration. If it can work out how
// many iterations of warm-up that needs, it reports that back in the decision's
// warmup_start and the caller rewinds the loop; otherwise the new bounds get a
// select in them that computes the whole region on the first iteration.
class SlidingWindowOnFunctionAndLoop : public IRMutator {
    Function func;
    string loop_var;
    // The first iteration at which anything consumes this func. This is the
    // original loop min, unless a consumer is itself warming up a sliding
    // window over this loop, in which case it's the iteration that consumer
    // starts at.
    Expr loop_min;
    set<int> &slid_dimensions;
    Scope<Expr> scope;
    Scope<Interval> &bounds_scope;

    // Whether the thing being slid over can have iterations prepended to it.
    // A loop can. A dimension spread across several loops can too, by
    // prepending them to the outermost loop it depends on, but only if that
    // loop moves it by a fixed amount. Anything else warms up with a select.
    bool can_rewind = true;

    // Whether we're sliding over a dimension rather than a loop. The warm-up
    // iterations then come from a loop that moves the dimension in steps, so
    // they can reach further back than asked for. The producer is guarded
    // instead of having its bounds clamped, which keeps the region it computes
    // the plain steady state.
    bool over_dimension = false;

    // For loops strictly between the loop being slid over and the current
    // node (not including the loop being slid over itself).
    Scope<> enclosing_loops;

    // How many of those actually run more than once. A loop over a single
    // point can't carry anything from one iteration to the next, so it doesn't
    // count as somewhere a window could slide.
    int enclosing_real_loops = 0;

    map<string, Expr> replacements;

    // The immediately-enclosing For node, and the one enclosing the target
    // producer. Replacements are only applied to LetStmts directly inside
    // producer_for.
    const For *current_for = nullptr;
    Stmt producer_for;

    using IRMutator::visit;

    // Check if the dimension at index 'dim_idx' is always pure (i.e. equal to 'dim')
    // in the definition (including in its specializations)
    bool is_dim_always_pure(const Definition &def, const string &dim, int dim_idx) {
        const Variable *var = def.args()[dim_idx].as<Variable>();
        if ((!var) || (var->name != dim)) {
            return false;
        }

        for (const auto &s : def.specializations()) {
            bool pure = is_dim_always_pure(s.definition, dim, dim_idx);
            if (!pure) {
                return false;
            }
        }
        return true;
    }

    // Decide what to do with this producer, using only the scopes gathered on
    // the way in. Makes no changes to the IR.
    SlideDecision plan_slide() {
        SlideDecision result = decision;

        // We're interested in the case where exactly one of the dimensions of
        // the buffer has a min/extent that depends on the loop_var.
        string dim;
        int dim_idx = 0;
        Expr min_required, max_required;

        debug(3) << "Considering sliding " << func.name()
                 << " along loop variable " << loop_var << "\n"
                 << "Region provided:\n";

        string prefix = func.name() + ".s" + std::to_string(func.updates().size()) + ".";
        const std::vector<string> func_args = func.args();
        for (int i = 0; i < func.dimensions(); i++) {
            // Look up the region required of this function's last stage
            string var = prefix + func_args[i];
            const auto *min_val = scope.find(var + ".min");
            const auto *max_val = scope.find(var + ".max");
            internal_assert(min_val && max_val);
            Expr min_req = *min_val;
            Expr max_req = *max_val;
            min_req = simplify(expand_expr(min_req, scope), bounds_scope);
            max_req = simplify(expand_expr(max_req, scope), bounds_scope);

            debug(3) << func_args[i] << ":" << min_req << ", " << max_req << "\n";
            if (expr_uses_var(min_req, loop_var) ||
                expr_uses_var(max_req, loop_var)) {
                if (!dim.empty()) {
                    dim = "";
                    min_required = Expr();
                    max_required = Expr();
                    break;
                } else {
                    dim = func_args[i];
                    dim_idx = i;
                    min_required = min_req;
                    max_required = max_req;
                }
            } else if (!min_required.defined() &&
                       i == func.dimensions() - 1 &&
                       is_pure(min_req) &&
                       is_pure(max_req)) {
                // The footprint doesn't depend on the loop var. Just compute
                // everything on the first loop iteration.
                dim = func_args[i];
                dim_idx = i;
                min_required = min_req;
                max_required = max_req;
            }
        }

        if (!dim.empty() && slid_dimensions.count(dim_idx)) {
            result.dim = dim;
            result.dim_idx = dim_idx;
            return result.reject(already_slid);
        }
        if (func.schedule().memory_type() == MemoryType::Register &&
            enclosing_real_loops > 0) {
            // A rolled register array can only carry values across the
            // innermost loop it lives in. By the time an outer loop advances,
            // everything the array held has been overwritten by the sweep of
            // the loop below it, so a second roll over this loop would read
            // values that are no longer there. Leave it for the inner loop.
            return result.reject("it should roll over an inner loop instead");
        }
        if (!min_required.defined()) {
            return result.reject("more than one dimension depends on the loop var");
        }

        if (!can_rewind &&
            (expr_uses_vars(min_required, enclosing_loops) ||
             expr_uses_vars(max_required, enclosing_loops))) {
            // Sliding over a dimension only advances the window once per value
            // of that dimension. If the region required also moves with a loop
            // between here and the producer, the window would have to advance
            // within an iteration too, and the values a later iteration wants
            // have been passed over by then.
            user_error
                << "Func " << func.name() << " was told to slide over "
                << slide_level_name(func, loop_var) << ", but the "
                << "region of it required also depends on a loop between that "
                << "dimension and where " << func.name() << " is computed, so "
                << "the window would have to move within a single value of "
                << "that dimension. Compute " << func.name() << " at a "
                << "coarser level, or slide over a dimension the region "
                << "required moves with.\n";
        }

        // If the function is not pure in the given dimension, give up. We also
        // need to make sure that it is pure in all the specializations
        for (const Definition &def : func.updates()) {
            if (!is_dim_always_pure(def, dim, dim_idx)) {
                return result.reject("the function scatters along the related axis");
            }
        }

        result.dim = dim;
        result.dim_idx = dim_idx;
        result.old_bounds = Interval(min_required, max_required);

        Monotonic monotonic_min = is_monotonic(min_required, loop_var);
        Monotonic monotonic_max = is_monotonic(max_required, loop_var);
        bool can_slide_up = (monotonic_min == Monotonic::Increasing ||
                             monotonic_min == Monotonic::Constant);
        bool can_slide_down = (monotonic_max == Monotonic::Decreasing ||
                               monotonic_max == Monotonic::Constant);

        if (!can_slide_up && !can_slide_down) {
            return result.reject("the region required doesn't move monotonically");
        }
        result.slide_up = can_slide_up;

        Expr loop_var_expr = Variable::make(Int(32), loop_var);
        Expr prev_max_plus_one = substitute(loop_var, loop_var_expr - 1, max_required) + 1;
        Expr prev_min_minus_one = substitute(loop_var, loop_var_expr - 1, min_required) - 1;

        if (can_prove(min_required >= prev_max_plus_one) ||
            can_prove(max_required <= prev_min_minus_one)) {
            return result.reject("adjacent iterations require disjoint regions");
        }

        // The region newly required this iteration, assuming the previous
        // iteration has already run.
        Expr new_min, new_max;
        if (can_slide_up) {
            new_min = prev_max_plus_one;
            new_max = max_required;
        } else {
            new_min = min_required;
            new_max = prev_min_minus_one;
        }

        // See if we can find a new min for the loop that can warm up the
        // sliding window. We're going to do this by building an equation
        // that describes the constraints we have on our new loop min. The
        // first constraint is that the new loop min is not after the
        // loop min.
        string new_loop_min_name = unique_name('x');
        Expr new_loop_min_var = Variable::make(Int(32), new_loop_min_name);
        Expr new_loop_min_eq = new_loop_min_var <= loop_min;
        Expr new_min_at_new_loop_min = substitute(loop_var, new_loop_min_var, new_min);
        Expr new_max_at_new_loop_min = substitute(loop_var, new_loop_min_var, new_max);
        if (can_slide_up) {
            // We need to find a new loop min that satisfies these constraints:
            // - The new min at the new loop min needs to be before the min
            //   required at the original min.
            // - The new max needs to be greater than the new min, both at the
            //   new loop min. This guarantees that the sliding window.
            // Together, these conditions guarantee the sliding window is warmed
            // up. The first condition checks that we reached the original loop
            // min, and the second condition checks that the iterations before
            // the original min weren't empty.
            Expr min_required_at_loop_min = substitute(loop_var, loop_min, min_required);
            new_loop_min_eq = new_loop_min_eq &&
                              new_min_at_new_loop_min <= min_required_at_loop_min &&
                              new_max_at_new_loop_min >= new_min_at_new_loop_min;
        } else {
            // When sliding down, the constraints are similar, just swapping
            // the roles of the min and max.
            Expr max_required_at_loop_min = substitute(loop_var, loop_min, max_required);
            new_loop_min_eq = new_loop_min_eq &&
                              new_max_at_new_loop_min >= max_required_at_loop_min &&
                              new_min_at_new_loop_min <= new_max_at_new_loop_min;
        }
        // Try to solve the equation.
        new_loop_min_eq = simplify(new_loop_min_eq);
        Interval solve_result = solve_for_inner_interval(new_loop_min_eq, new_loop_min_name);
        // The solver returns something even when the constraints can't all be
        // met, so check its answer really does satisfy them. That's the whole
        // correctness condition for rewinding: the warm-up iterations tile the
        // region with copies of the steady state, and reach back far enough to
        // cover what's required at the loop min.
        bool solved = (can_rewind &&
                       solve_result.has_upper_bound() &&
                       can_prove(substitute(new_loop_min_name, solve_result.max,
                                            new_loop_min_eq)));
        if (solved &&
            !expr_uses_vars(solve_result.max, enclosing_loops)) {
            result.warmup_start = simplify(solve_result.max);

            // We have a new loop min, so we an assume every iteration has
            // a previous iteration. In order for this to be safe, we need
            // the new min/max at the new loop min to be less than or equal to
            // the min/max required at the loop min. Note that loop_min is
            // the first iteration at which anything consumes this func,
            // which is before the original loop min if a consumer is
            // warming up a sliding window of its own. The region this func
            // must retain has to cover what those warm-up iterations ask
            // for too.
            // Sliding over a loop clamps the region here, so that the
            // rewound iterations stay empty until they have something to
            // contribute. That leaves the producer's bounds depending on how
            // far through the warm-up we are, and only loop partitioning takes
            // it back out. Sliding over a dimension guards the producer
            // instead, so its bounds stay the steady state and nothing has to
            // be peeled to see that.
            if (!over_dimension) {
                if (can_slide_up) {
                    Expr min_required_at_loop_min = substitute(loop_var, loop_min, min_required);
                    new_min = max(new_min, min_required_at_loop_min);
                } else {
                    Expr max_required_at_loop_min = substitute(loop_var, loop_min, max_required);
                    new_max = min(new_max, max_required_at_loop_min);
                }
            }
        } else {
            // We couldn't find a suitable new loop min, so we can't assume
            // every iteration has a previous iteration. The first iteration
            // will warm up the loop instead.
            Expr need_explicit_warmup = loop_var_expr <= loop_min;
            // When sliding over a dimension, the condition is on loops outside
            // the innermost one, so likely_if_innermost wouldn't get the
            // warm-up peeled off. Ask for the loop to be partitioned instead.
            auto mark = [&](const Expr &e) {
                return can_rewind ? likely_if_innermost(e) : likely(e);
            };
            if (can_slide_up) {
                new_min = select(need_explicit_warmup, min_required, mark(new_min));
            } else {
                new_max = select(need_explicit_warmup, max_required, mark(new_max));
            }
        }

        result.new_bounds = Interval(simplify(new_min), simplify(new_max));
        return result;
    }

    // Rewrite the producer to compute only the newly-required region. Makes no
    // decisions; everything it needs is in the decision.
    Stmt apply_slide(const ProducerConsumer *op) {
        string prefix = func.name() + ".s" + std::to_string(func.updates().size()) + ".";
        const string &dim = decision.dim;

        internal_assert(replacements.empty());
        if (decision.slide_up) {
            replacements[prefix + dim + ".min"] = decision.new_bounds.min;
        } else {
            replacements[prefix + dim + ".max"] = decision.new_bounds.max;
        }

        for (size_t i = 0; i < func.updates().size(); i++) {
            string n = func.name() + ".s" + std::to_string(i) + "." + dim;
            replacements[n + ".min"] = Variable::make(Int(32), prefix + dim + ".min");
            replacements[n + ".max"] = Variable::make(Int(32), prefix + dim + ".max");
        }
        producer_for = Stmt(current_for);

        // The lets that define the bounds required get rewritten on the way
        // back out (see visit(LetStmt)). Additionally, expand the bounds
        // required of the last stage to cover values produced by earlier
        // stages, because e.g. an intermediate stage may be unrolled,
        // expanding its bounds provided.
        Stmt result = op;
        if (!func.updates().empty()) {
            Box b = box_provided(op->body, func.name());
            if (decision.slide_up) {
                string n = prefix + dim + ".min";
                Expr var = Variable::make(Int(32), n);
                result = LetStmt::make(n, min(var, b[decision.dim_idx].min), result);
            } else {
                string n = prefix + dim + ".max";
                Expr var = Variable::make(Int(32), n);
                result = LetStmt::make(n, max(var, b[decision.dim_idx].max), result);
            }
        }
        return result;
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (!(op->is_producer && op->name == func.name())) {
            return IRMutator::visit(op);
        }

        internal_assert(!decision.slid()) << "Slid " << func.name()
                                          << "'s producer twice\n";
        decision = plan_slide();
        decision.considered = true;
        debug(3) << decision;

        if (!decision.slid()) {
            return op;
        }
        slid_dimensions.insert(decision.dim_idx);

        if (func.schedule().memory_type() == MemoryType::Register) {
            // Sliding in registers leaves the region written alone. RollFunc
            // instead rewrites the store so that everything but the leading
            // edge is a copy of a value the previous iteration already has in
            // a register, which unrolling turns into nothing at all.
            return op;
        }

        return apply_slide(op);
    }

    Stmt visit(const For *op) override {
        // It's not safe to enter an inner loop whose bounds depend on
        // the var we're sliding over.
        Expr min = expand_expr(op->min, scope);
        Expr max = expand_expr(op->max, scope);
        ScopedBinding<> bind(enclosing_loops, op->name);
        ScopedValue<const For *> bind_for(current_for, op);
        if (equal(min, max)) {
            // Just treat it like a let
            Stmt s = LetStmt::make(op->name, min, op->body);
            s = mutate(s);
            // Unpack it back into the for
            const LetStmt *l = s.as<LetStmt>();
            internal_assert(l);
            return op->with(op->min, op->max, l->body);
        } else if (is_monotonic(min, loop_var) != Monotonic::Constant ||
                   is_monotonic(max, loop_var) != Monotonic::Constant) {
            debug(3) << "Not entering loop over " << op->name
                     << " because the bounds depend on the var we're sliding over: "
                     << min << ", " << max << "\n";
            return op;
        } else {
            ScopedValue<int> bind_count(enclosing_real_loops, enclosing_real_loops + 1);
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<Interval> bind_bounds(bounds_scope, op->name,
                                            bounds_of_expr_in_scope(op->value, bounds_scope));
        ScopedBinding<Expr> bind(scope, op->name, simplify(expand_expr(op->value, scope), bounds_scope));

        Stmt new_body = mutate(op->body);

        Expr value = op->value;

        map<string, Expr>::iterator iter = replacements.find(op->name);
        if (iter != replacements.end() && current_for == producer_for.get()) {
            value = iter->second;
            replacements.erase(iter);
        }

        return op->with(value, new_body);
    }

public:
    SlidingWindowOnFunctionAndLoop(Function f, string v, Expr v_min, set<int> &slid_dimensions,
                                   Scope<Interval> &bounds_scope, bool can_rewind = true,
                                   bool over_dimension = false)
        : func(std::move(f)), loop_var(std::move(v)), loop_min(std::move(v_min)),
          slid_dimensions(slid_dimensions), bounds_scope(bounds_scope),
          can_rewind(can_rewind), over_dimension(over_dimension) {
        decision.func_name = func.name();
        decision.slide_over = loop_var;
    }

    // What we decided to do, filled in when we reach the producer.
    SlideDecision decision;

    // Slide in registers instead of in memory, by shifting values along an
    // unrolled array. Only valid once we've decided to slide.
    Stmt translate_loop(const Stmt &s) {
        internal_assert(decision.slid());
        return RollFunc(func, decision.dim_idx, loop_var,
                        decision.old_bounds, decision.new_bounds)(s);
    }
};

// Which Funcs a Func's production depends on, according to the Function DAG.
// We could instead ask which Funcs are called under a produce node in the
// Stmt, but that gives the same answers - we only ever ask about Funcs whose
// producers are both present - and it costs a traversal of the loop body per
// query.
class FuncDependencies {
    const map<string, Function> &env;
    map<string, map<string, Function>> cache;

public:
    FuncDependencies(const map<string, Function> &env)
        : env(env) {
    }

    // Does producing b require a?
    bool depends_on(const string &a, const string &b) {
        auto cached = cache.find(b);
        if (cached == cache.end()) {
            auto f = env.find(b);
            internal_assert(f != env.end()) << "Not in the environment: " << b << "\n";
            cached = cache.emplace(b, find_transitive_calls(f->second)).first;
        }
        return cached->second.count(a) != 0;
    }
};

// Some producers slide by rewinding the start of the loop, so that the
// iterations before the original loop min warm up their window. Every
// statement in the body then needs to say which iteration it starts at:
//
// - A warming-up producer needs no guard. Its bounds are empty until it has
//   something to do.
//
// - Anything a warming-up producer consumes has to be ready in time for it, so
//   it starts when that producer starts. Not earlier: it may be warming up a
//   window of its own over some enclosing loop, and running it extra times
//   would corrupt that.
//
// - Everything else is real work, and starts at the original loop min.
class InjectWarmupGuards : public IRMutator {
    // For each func that rewound the loop, the iteration it starts at.
    const map<string, Expr> &warmup_starts;
    const string &loop_var;
    // The loop min before anything rewound it.
    const Expr &orig_loop_min;
    FuncDependencies &deps;
    // Whether the thing guarded on is the innermost loop. When it's a
    // dimension spread across several loops it isn't, and asking only for
    // innermost partitioning would leave the warm-up folded into the steady
    // state as a clamp on the region required, which keeps the producer's
    // trip count data-dependent.
    bool innermost;
    // Whether a producer that is warming up needs a guard of its own.
    bool guard_warming_producers;

    // Memoized, because answering the question requires a traversal. An
    // undefined Expr means the func's producer needs no guard.
    map<string, Expr> start_cache;

    Expr start_of(const string &func) {
        auto cached = start_cache.find(func);
        if (cached != start_cache.end()) {
            return cached->second;
        }
        Expr start = orig_loop_min;
        auto w = warmup_starts.find(func);
        if (w != warmup_starts.end()) {
            // A producer warming up needs no guard when the loop was rewound
            // to exactly where it starts, because its bounds are empty until
            // it has work. Rewinding a dimension moves in steps and can
            // overshoot, so it gets a guard saying where its work begins.
            start = guard_warming_producers ? w->second : Expr();
        } else {
            for (const auto &w : warmup_starts) {
                if (deps.depends_on(func, w.first)) {
                    start = simplify(min(start, w.second));
                }
            }
        }
        start_cache[func] = start;
        return start;
    }

    // The iteration at which the statements we're currently looking at run,
    // if nothing inside them says otherwise. Undefined means no guard, which
    // is the case inside a producer that's warming up.
    Expr current_start;

    // The iteration everything produced in here starts at, or an undefined
    // Expr if they don't all agree with each other and with current_start, in
    // which case we need to descend and guard them separately.
    // Is there a single start shared by every producer in this Stmt? Only the
    // existence matters to the callers, not the value.
    bool has_common_start(const Stmt &s) {
        Expr start = current_start;
        visit_with(s, [&](auto *self, const ProducerConsumer *op) {
            if (op->is_producer) {
                Expr f_start = start_of(op->name);
                if (!f_start.defined() || !start.defined() || !equal(f_start, start)) {
                    start = Expr();
                }
            }
            self->visit_base(op);
        });
        return start.defined();
    }

    Stmt wrap(const Stmt &s) {
        if (!current_start.defined()) {
            return s;
        }
        Expr cond = current_start <= Variable::make(Int(32), loop_var);
        Expr guard = innermost ? likely_if_innermost(cond) : likely(cond);
        debug(3) << "Guarding body " << guard << "\n";
        return IfThenElse::make(guard, s);
    }

    using IRMutator::visit;

    Stmt visit(const ProducerConsumer *op) override {
        if (!op->is_producer) {
            // Recurse rather than wrapping, so that any synchronization
            // attached to a consume node stays outside the guard.
            return IRMutator::visit(op);
        }
        // Anything computed inside this producer is an input to it, so by
        // default it runs when this producer does.
        ScopedValue<Expr> bind(current_start, start_of(op->name));
        return has_common_start(op->body) ? wrap(op) : IRMutator::visit(op);
    }

    Stmt visit(const For *op) override {
        return has_common_start(op) ? wrap(op) : IRMutator::visit(op);
    }

    Stmt visit(const IfThenElse *op) override {
        return has_common_start(op) ? wrap(op) : IRMutator::visit(op);
    }

    Stmt visit(const Provide *op) override {
        return wrap(op);
    }

    Stmt visit(const Evaluate *op) override {
        // Sliding window markers are notes to storage folding, not work.
        if (Call::as_intrinsic(op->value, {Call::sliding_window_marker})) {
            return op;
        }
        return wrap(op);
    }

public:
    InjectWarmupGuards(const map<string, Expr> &warmup_starts, const string &loop_var,
                       const Expr &orig_loop_min, FuncDependencies &deps,
                       bool innermost = true, bool guard_warming_producers = false)
        : warmup_starts(warmup_starts), loop_var(loop_var),
          orig_loop_min(orig_loop_min), deps(deps), innermost(innermost),
          guard_warming_producers(guard_warming_producers),
          current_start(orig_loop_min) {
    }
};
// Update the loop variable referenced by prefetch directives.
class SubstitutePrefetchVar : public IRMutator {
    const string &old_var;
    const string &new_var;

    using IRMutator::visit;

    Stmt visit(const Prefetch *op) override {
        Stmt new_body = mutate(op->body);
        if (op->prefetch.at == old_var || op->prefetch.from == old_var) {
            PrefetchDirective p = op->prefetch;
            if (op->prefetch.at == old_var) {
                p.at = new_var;
            }
            if (op->prefetch.from == old_var) {
                p.from = new_var;
            }
            return Prefetch::make(op->name, op->types, op->bounds, p, op->condition, std::move(new_body));
        } else {
            return op->with(op->bounds, op->condition, new_body);
        }
    }

public:
    SubstitutePrefetchVar(const string &old_var, const string &new_var)
        : old_var(old_var), new_var(new_var) {
    }
};

// Perform sliding window optimization for all functions
class SlidingWindow : public IRMutator {
    const map<string, Function> &env;
    FuncDependencies deps;

    // A map of which dimensions we've already slid over, by Func name.
    map<string, set<int>> slid_dimensions;
    // For funcs told which dimensions to slide over, which of those claimed
    // each dimension of the func's storage. Only used to explain errors.
    map<string, map<int, string>> slid_over_by;
    // A dimension slides at the let that carries it, but the iterations that
    // warm its window up have to come from a loop. This maps that loop to the
    // number of its iterations to prepend. The loop reads it on the way back
    // out, once the let inside has worked out how far back it needs to reach.
    map<string, Expr> rewind_requests;

    // Keep track of realizations we want to slide, from innermost to
    // outermost.
    list<Function> sliding;
    // For each of those, how many loops we were already inside when we
    // reached its Realize. Loops outside that point don't have its storage
    // live across them.
    list<size_t> sliding_loop_depth;
    vector<string> loop_stack;
    Scope<Expr> let_values;
    Scope<Interval> bounds_scope;

    using IRMutator::visit;

    // A window only ever moves forwards, so a dimension can only be slid over
    // if it increases as the loop nest runs. A dimension reassembled from
    // several loops has no such guarantee. Reordering the loops of a split
    // makes the outer one carry the small term, and the dimension lurches
    // backwards every time it advances. A tail strategy that shifts the last
    // iteration back so it fits does the same thing once, at the end.
    //
    // The condition is the one that makes a mixed-radix number count upwards.
    // Each contributing loop has to advance the dimension by at least as much
    // as the loops inside it can take back when they wrap around.
    void check_dimension_is_monotonic(const Function &f, const string &let_name,
                                      const Expr &value) {
        Expr v = expand_expr(value, let_values);

        vector<string> contributing;
        for (const string &loop : loop_stack) {
            if (expr_uses_var(v, loop)) {
                contributing.push_back(loop);
            }
        }

        for (size_t i = 0; i < contributing.size(); i++) {
            const string &outer = contributing[i];
            Expr var = Variable::make(Int(32), outer);
            // The dimension just after this loop advances, with everything
            // inside it wrapped back to the start...
            Expr next = substitute(outer, var + 1, v);
            // ...against the largest it reaches before that happens.
            Expr prev = v;
            for (size_t j = i + 1; j < contributing.size(); j++) {
                const Interval *b = bounds_scope.find(contributing[j]);
                if (!b || !b->is_bounded()) {
                    next = Expr();
                    break;
                }
                next = substitute(contributing[j], b->min, next);
                prev = substitute(contributing[j], b->max, prev);
            }
            if (next.defined() && can_prove(next >= prev, bounds_scope)) {
                continue;
            }
            user_error
                << "Func " << f.name() << " was told to slide over "
                << slide_level_name(f, let_name) << ", but that dimension does "
                << "not always increase as the loop nest runs: the loop "
                << outer << " advances it by less than the loops inside it "
                << "take back when they wrap around. A window only moves "
                << "forwards, so sliding would read values it has already "
                << "passed over. Reordering the loops of a split puts the "
                << "smaller term on the outer loop, and ShiftInwards moves the "
                << "last iteration backwards unless the extent is a multiple "
                << "of the split factor. Slide over a dimension that counts "
                << "upwards instead.\n";
        }
    }

    // A window can only slide over a dimension if the storage lives across
    // every loop that dimension varies over. Sliding assumes the previous
    // iteration's values are still there, and an allocation inside one of
    // those loops is thrown away and remade as they run.
    void check_storage_outlives_dimension(const Function &f, const string &let_name,
                                          const Expr &value) {
        size_t depth = 0;
        auto d = sliding_loop_depth.begin();
        for (auto it = sliding.begin(); it != sliding.end(); it++, d++) {
            if (it->name() == f.name()) {
                depth = *d;
                break;
            }
        }
        Expr expanded = expand_expr(value, let_values);
        for (size_t i = 0; i < depth; i++) {
            if (expr_uses_var(expanded, loop_stack[i])) {
                user_error
                    << "Func " << f.name() << " was told to slide over "
                    << slide_level_name(f, let_name) << ", but that "
                    << "dimension varies over the loop " << loop_stack[i]
                    << ", which is outside " << f.name() << "'s storage. Sliding "
                    << "would read values from a previous iteration of that "
                    << "loop, which have been thrown away by then. Store "
                    << f.name() << " at a coarser level, or slide over a "
                    << "dimension that doesn't span that loop.\n";
            }
        }
    }

    // Two dimensions a func was told to slide over move the same dimension of
    // its storage. The window can only advance along that dimension once, so
    // one of the two requests would be silently dropped.
    void check_not_entangled(const Function &f, const string &dim,
                             const SlideDecision &decision) {
        if (decision.rejected != already_slid) {
            return;
        }
        const string &other = slid_over_by[f.name()][decision.dim_idx];
        user_error
            << "Func " << f.name() << " was told to slide over both "
            << slide_level_name(f, other) << " and " << slide_level_name(f, dim)
            << ", but both of those move the same dimension ("
            << decision.dim << ") of " << f.name() << ", so the window would "
            << "have to advance along it twice. Slide over dimensions that "
            << "move different dimensions of " << f.name() << ".\n";
    }

    // Was this func told to slide over the dimension this let carries? After
    // splitting there's no loop with this name, but there is a let, because
    // the original var still has to be computed to evaluate the func's args.
    static bool slides_over(const Function &f, const string &let_name) {
        const auto &levels = f.schedule().slide_levels();
        return std::any_of(levels.begin(), levels.end(), [&](const LoopLevel &l) {
            return l.defined() && !l.is_inlined() && !l.is_root() && l.match(let_name);
        });
    }

    // How much of a Func has to be kept while sliding over a dimension.
    //
    // The region required per iteration is the obvious answer, and it is wrong
    // for a recurrence. A Func defined in terms of itself one step back
    // requires, transitively, everything it has ever computed, so the region
    // reaches all the way to the base case and the window looks like the whole
    // scan. Sliding itself is not fooled by this - it still computes one new
    // value per step - but the width handed to storage folding would be.
    //
    // What has to be kept is what will still be read once this step's value
    // has been written: the values the consumers ask for, and the value the
    // recurrence reaches back to.
    Interval window_for(const Function &func, const SlideDecision &d,
                        const string &dim_name, const Stmt &body) {
        // The step the recurrence reaches back by, as a number of values of
        // the dimension. Zero if the Func does not read itself, in which case
        // the region required was the right answer already. The Func's own
        // definition names the dimension by its pure Var.
        int reach_back = 0;
        Expr pure_var = Variable::make(Int(32), d.dim);
        auto note_self_calls = [&](const Definition &def) {
            for (const Expr &v : def.values()) {
                visit_with(v, [&](auto *self, const Call *op) {
                    if (op->name == func.name() && op->call_type == Call::Halide &&
                        d.dim_idx < (int)op->args.size()) {
                        auto step = as_const_int(simplify(pure_var - op->args[d.dim_idx]));
                        // A step we can't pin down means we can't say how wide
                        // the window is either.
                        reach_back = step ? std::max(reach_back, (int)*step) : -1;
                    }
                    self->visit_base(op);
                });
            }
        };
        note_self_calls(func.definition());
        for (const Definition &def : func.updates()) {
            note_self_calls(def);
        }
        if (reach_back <= 0) {
            return d.old_bounds;
        }

        // What the consumers ask for at one value of the dimension. Pinning it
        // to itself keeps it symbolic: left to the enclosing bounds it would
        // be replaced by its whole range, which is the union over every step
        // rather than the region at any one of them.
        Expr dim_var = Variable::make(Int(32), dim_name);
        Scope<Interval> one_step;
        one_step.set_containing_scope(&bounds_scope);
        one_step.push(dim_name, Interval::single_point(dim_var));
        // Scrub the self-references of everything sliding here, not just the
        // Func being measured. Another recurrence sliding over the same
        // dimension has the same whole-scan region, and if it reads this Func
        // it drags this Func's region back to the start with it.
        Stmt scrubbed = body;
        for (const Function &other : sliding) {
            scrubbed = scrub_self_reads(scrubbed, other.name());
        }
        Box ext = box_required(scrubbed, func.name(), one_step);
        if (d.dim_idx >= (int)ext.size() || !ext[d.dim_idx].is_bounded()) {
            return d.old_bounds;
        }

        Interval live(simplify(min(ext[d.dim_idx].min, dim_var - reach_back), one_step),
                      simplify(max(ext[d.dim_idx].max, dim_var), one_step));
        return live;
    }

    Stmt visit(const LetStmt *op) override {
        Interval let_bounds = bounds_of_expr_in_scope(op->value, bounds_scope);
        // For a dimension we're about to slide over, simplify the ends before
        // they go into scope. Cancelling the two ends of the window needs what
        // we know about the dimension, and that arrives as unsimplified
        // arithmetic on loop mins and extents - a min of ((0*2) + 0) + 0
        // rather than 0 - which nothing downstream can use. Only worth doing
        // for those lets: the bounds of every let in the program nest, and
        // simplifying all of them costs more than the whole rest of the pass.
        if (std::any_of(sliding.begin(), sliding.end(),
                        [&](const Function &f) { return slides_over(f, op->name); })) {
            if (let_bounds.has_lower_bound()) {
                let_bounds.min = simplify(let_bounds.min);
            }
            if (let_bounds.has_upper_bound()) {
                let_bounds.max = simplify(let_bounds.max);
            }
        }
        ScopedBinding<Interval> bind(bounds_scope, op->name, let_bounds);
        ScopedBinding<Expr> bind_value(let_values, op->name, op->value);

        // The window warms up by prepending iterations to the outermost loop
        // the dimension depends on. One iteration of that loop moves the
        // dimension by this much, which is what turns a distance to reach back
        // along the dimension into a number of iterations to prepend.
        string rewind_loop;
        Expr stride;
        {
            Expr v = expand_expr(op->value, let_values);
            for (const string &loop : loop_stack) {
                if (expr_uses_var(v, loop)) {
                    rewind_loop = loop;
                    break;
                }
            }
            if (!rewind_loop.empty()) {
                Expr var = Variable::make(Int(32), rewind_loop);
                stride = simplify(substitute(rewind_loop, var + 1, v) - v);
            }
        }
        // Without a fixed stride there's no way to say how many iterations
        // reach back far enough, so those warm up with a select instead.
        const bool can_rewind = stride.defined() && is_positive_const(stride);

        Stmt body = op->body;
        bool slid_any = false;
        // For each func warming up here, the value of the dimension it starts
        // at. Everything else waits for the dimension to reach its real min.
        map<string, Expr> warming_up;
        for (const Function &func : consumers_first(sliding)) {
            if (!slides_over(func, op->name)) {
                continue;
            }
            check_dimension_is_monotonic(func, op->name, op->value);
            check_storage_outlives_dimension(func, op->name, op->value);
            debug(3) << "Sliding " << func.name() << " over dimension "
                     << op->name << "\n";
            set<int> &slid_dims = slid_dimensions[func.name()];
            SlidingWindowOnFunctionAndLoop slider(func, op->name, let_bounds.min,
                                                  slid_dims, bounds_scope,
                                                  can_rewind, /* over_dimension */ true);
            body = slider(body);
            debug(3) << slider.decision;
            check_not_entangled(func, op->name, slider.decision);
            if (slider.decision.slid()) {
                slid_over_by[func.name()][slider.decision.dim_idx] = op->name;
            }
            if (slider.decision.warmup_start.defined()) {
                // Reaching back to here along the dimension takes this many
                // iterations of the loop, rounded up. Rewinding further than
                // asked for is harmless: the extra iterations are guarded off
                // below, and the window is empty until it has work to do.
                Expr steps = simplify(
                    (let_bounds.min - slider.decision.warmup_start + stride - 1) / stride);
                Expr &request = rewind_requests[rewind_loop];
                request = request.defined() ? simplify(max(request, steps)) : steps;
                warming_up[func.name()] = slider.decision.warmup_start;
            }
            slid_any = true;

            // Storage folding can't work this out for itself. The dimension
            // is gone by the time it runs, because the simplifier substitutes
            // the let away, and measuring the footprint over any of the loops
            // the dimension is spread across counts values that are never
            // simultaneously live. Tell it the window width we just derived.
            const SlideDecision &d = slider.decision;
            if (d.slid()) {
                // An upper bound, not the exact width. Two forms of the same
                // difference are worth trying, because they fail for
                // different reasons. Written in terms of the dimension, the
                // two ends of the window cancel against each other, which is
                // what a consumer that clamps its index needs. Written in
                // terms of the loops the dimension was split across, it
                // reaches the constants in their bounds. Take whichever gives
                // a bound, or the tighter of the two.
                Interval window = window_for(func, d, op->name, body);
                Expr raw = window.max - window.min + 1;
                Expr width;
                for (const Expr &form : {simplify(raw, bounds_scope),
                                         expand_expr(raw, let_values)}) {
                    Expr bound = find_constant_bound(form, Direction::Upper, bounds_scope);
                    if (bound.defined() && (!width.defined() || can_prove(bound < width))) {
                        width = bound;
                    }
                }
                if (width.defined()) {
                    Expr marker = Call::make(Int(32), Call::sliding_window_marker,
                                             {func.name(), Variable::make(Int(32), op->name),
                                              d.dim_idx, width},
                                             Call::Intrinsic);
                    body = Block::make(Evaluate::make(marker), body);
                }
            }
        }
        if (!warming_up.empty()) {
            // The dimension now starts before its real min. Everything the
            // extra iterations aren't there to warm up has to be skipped over.
            // The guard is on the dimension, not on a loop var, because no
            // single loop corresponds to it.
            body = InjectWarmupGuards(warming_up, op->name, let_bounds.min, deps,
                                      /* innermost */ false,
                                      /* guard_warming_producers */ true)(body);
        }
        if (slid_any) {
            return LetStmt::make(op->name, op->value, mutate(body));
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Realize *op) override {
        // Find the args for this function
        map<string, Function>::const_iterator iter = env.find(op->name);

        // If it's not in the environment it's some anonymous
        // realization that we should skip (e.g. an inlined reduction)
        if (iter == env.end()) {
            return IRMutator::visit(op);
        }

        // If the Function in question has the same compute_at level
        // as its store_at level, skip it.
        const FuncSchedule &sched = iter->second.schedule();
        if (sched.compute_level() == sched.store_level()) {
            return IRMutator::visit(op);
        }

        // We want to slide innermost first, so put it on the front of
        // the list.
        sliding.push_front(iter->second);
        sliding_loop_depth.push_front(loop_stack.size());
        Stmt new_body = mutate(op->body);
        sliding.pop_front();
        sliding_loop_depth.pop_front();
        // Remove tracking of slid dimensions when we're done realizing
        // it in case a realization appears elsewhere.
        auto slid_it = slid_dimensions.find(iter->second.name());
        if (slid_it != slid_dimensions.end()) {
            slid_dimensions.erase(slid_it);
        }

        return op->with(op->bounds, op->condition, new_body);
    }

    // Order the funcs realized around a loop so that consumers come before the
    // funcs they consume. Note that this is not the same as the order of the
    // realizations, because e.g. a store_root'd producer is realized outside
    // its consumers.
    vector<Function> consumers_first(const list<Function> &funcs) {
        vector<Function> in(funcs.begin(), funcs.end()), out;
        const size_t n = in.size();
        // consumes[i * n + j] is whether producing in[i] uses in[j].
        vector<bool> consumes(n * n, false);
        for (size_t i = 0; i < n; i++) {
            for (size_t j = 0; j < n; j++) {
                consumes[i * n + j] = i != j && deps.depends_on(in[j].name(), in[i].name());
            }
        }
        // Topologically sort by repeatedly taking a func that nothing left in
        // the list consumes.
        vector<bool> done(n, false);
        while (out.size() < n) {
            size_t next = n;
            for (size_t i = 0; i < n && next == n; i++) {
                if (done[i]) {
                    continue;
                }
                next = i;
                for (size_t k = 0; k < n; k++) {
                    if (!done[k] && consumes[k * n + i]) {
                        next = n;
                        break;
                    }
                }
            }
            // The dependency graph is acyclic, so we must have found one.
            internal_assert(next < n);
            done[next] = true;
            out.push_back(in[next]);
        }
        // If this comes out in the wrong order, a Func won't know how far back
        // its consumers start by the time we slide it, and will warm up too
        // late.
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                internal_assert(!deps.depends_on(out[i].name(), out[j].name()))
                    << "Sliding window is about to slide " << out[i].name()
                    << " before " << out[j].name() << ", which consumes it\n";
            }
        }
        return out;
    }

    Stmt visit(const For *op) override {
        if (!(op->for_type == ForType::Serial || op->for_type == ForType::Unrolled)) {
            return IRMutator::visit(op);
        }
        debug(3) << "Doing sliding window analysis on loop " << op->name << "\n";

        string name = op->name;
        Stmt body = op->body;
        Expr loop_min = op->min;
        Expr loop_max = op->max;

        // For each func slid so far that rewound the loop, the iteration it
        // now starts running at.
        list<pair<string, Expr>> warmup_starts;
        list<pair<string, Expr>> new_lets;
        // The funcs that slide by rewinding the loop min, and the iteration
        // each of them starts at.
        map<string, Expr> warming_up;
        // What we decided to do with each func around this loop, in the order
        // we decided it.
        vector<SlideDecision> plan;
        // The loop min before any warm-up iterations were prepended. This
        // gets its own name, because <loop>.loop_min follows the loop as we
        // rewind it, and anything that wants to know where the real work
        // starts needs a reference that stays put. Most of these lets get
        // simplified away.
        string orig_loop_min_name = op->name + ".loop_min.orig";
        Expr orig_loop_min = Variable::make(Int(32), orig_loop_min_name);

        for (const Function &func : consumers_first(sliding)) {
            if (!func.schedule().slide_levels().empty() &&
                !slides_over(func, op->name)) {
                // Told explicitly which dimensions to slide over, and this
                // loop isn't one of them. A named dimension that no loop
                // corresponds to slides at the let that carries it instead.
                continue;
            }
            debug(3) << "Doing sliding window analysis on function " << func.name() << "\n";

            // Figure out the first iteration at which this func is consumed.
            // If nothing that consumes it is warming up a window of its own,
            // that's just the loop min.
            Expr consumed_from = orig_loop_min;
            // Otherwise we need to have this func warmed up in time for the
            // earliest-starting consumer, which means rewinding the loop even
            // further than they did.
            for (const auto &i : warmup_starts) {
                if (deps.depends_on(func.name(), i.first)) {
                    consumed_from = simplify(min(consumed_from, i.second));
                }
            }

            set<int> &slid_dims = slid_dimensions[func.name()];
            size_t old_slid_dims_size = slid_dims.size();

            Interval min_bounds = bounds_of_expr_in_scope(loop_min, bounds_scope);
            Interval max_bounds = bounds_of_expr_in_scope(loop_max, bounds_scope);
            ScopedBinding<Interval> bind_bounds(bounds_scope, op->name,
                                                Interval(min_bounds.min, max_bounds.max));

            SlidingWindowOnFunctionAndLoop slider(func, name, consumed_from, slid_dims, bounds_scope);

            body = slider(body);
            const SlideDecision &decision = slider.decision;
            if (!func.schedule().slide_levels().empty()) {
                check_not_entangled(func, op->name, decision);
                if (decision.slid()) {
                    slid_over_by[func.name()][decision.dim_idx] = op->name;
                }
            }
            if (decision.considered) {
                plan.push_back(decision);
            }

            if (func.schedule().memory_type() == MemoryType::Register &&
                decision.slid() && decision.old_bounds.has_lower_bound()) {
                body = slider.translate_loop(body);
            }

            if (decision.warmup_start.defined()) {
                // This func starts here, but an earlier func may already have
                // rewound the loop further than this, so keep the earlier of
                // the two as the loop min.
                Expr warmup_start = decision.warmup_start;
                warmup_starts.emplace_front(func.name(), warmup_start);
                warming_up[func.name()] = warmup_start;
                Expr new_loop_min =
                    loop_min.same_as(op->min) ? warmup_start : min(warmup_start, loop_min);

                // Rename the loop var in the body, and with it any reference
                // to the loop's min, so that <loop>.loop_min keeps meaning the
                // min of that loop. Storage folding relies on it: it separates
                // the first iteration from the steady state by substituting
                // into (loop min < loop var), which only simplifies if the
                // select we leave in the bounds names the same variable.
                //
                // Anything that wants the iteration at which the real work
                // starts holds the Expr for it rather than looking up a name,
                // so it is unaffected by this.
                string new_name = name + ".$n";
                loop_min = Variable::make(Int(32), new_name + ".loop_min");
                body = substitute({{name, Variable::make(Int(32), new_name)},
                                   {name + ".loop_min", loop_min}},
                                  body);
                body = SubstitutePrefetchVar(name, new_name)(body);

                name = new_name;

                // The new loop interval is the new loop min to the old loop max.
                new_lets.emplace_front(name + ".loop_min", new_loop_min);
            }

            if (slid_dims.size() > old_slid_dims_size) {
                // Let storage folding know there's now a read-after-write hazard here
                Expr marker = Call::make(Int(32),
                                         Call::sliding_window_marker,
                                         // Use name rather than op->name: the loop may
                                         // have been renamed above when it was rewound.
                                         {func.name(), Variable::make(Int(32), name)},
                                         Call::Intrinsic);
                body = Block::make(Evaluate::make(marker), body);
            }
        }

        for (const SlideDecision &d : plan) {
            debug(2) << d;
        }

        if (!warming_up.empty()) {
            // The loop now starts before the original loop min. Everything the
            // extra iterations aren't there to warm up must be skipped over.
            body = InjectWarmupGuards(warming_up, name, orig_loop_min, deps)(body);
        }

        {
            // Keep this loop's range in scope while we descend, so that a let
            // carrying a dimension spread across several loops can be bounded
            // when we come to slide over it.
            Interval lo = bounds_of_expr_in_scope(loop_min, bounds_scope);
            Interval hi = bounds_of_expr_in_scope(loop_max, bounds_scope);
            ScopedBinding<Interval> bind(bounds_scope, name, Interval(lo.min, hi.max));
            loop_stack.push_back(name);
            body = mutate(body);
            loop_stack.pop_back();
        }

        {
            // A let inside this loop slid a dimension over it and needs
            // iterations prepended to warm the window up. How many wasn't
            // known until we descended, so the count is a symbol here and
            // gets its value below, outside the loop.
            auto it = rewind_requests.find(name);
            if (it != rewind_requests.end()) {
                string steps_name = name + ".warmup_steps";
                loop_min = loop_min - Variable::make(Int(32), steps_name);
                new_lets.emplace_front(steps_name, simplify(max(it->second, 0)));
                rewind_requests.erase(it);
            }
        }

        if (body.same_as(op->body) && loop_min.same_as(op->min) && loop_max.same_as(op->max) && name == op->name) {
            return op;
        } else {
            Stmt result = For::make(name, loop_min, loop_max, op->for_type, op->partition_policy, op->device_api, body);
            for (const auto &i : new_lets) {
                result = LetStmt::make(i.first, i.second, result);
            }
            return LetStmt::make(orig_loop_min_name, op->min, result);
        }
    }

    Stmt visit(const IfThenElse *op) override {
        // Don't let specializations corrupt the tracking of which
        // dimensions have been slid.
        map<string, set<int>> old_slid_dimensions = slid_dimensions;
        Stmt then_case = mutate(op->then_case);
        slid_dimensions = old_slid_dimensions;
        Stmt else_case = mutate(op->else_case);
        slid_dimensions = old_slid_dimensions;
        if (then_case.same_as(op->then_case) && else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(op->condition, then_case, else_case);
        }
    }

public:
    SlidingWindow(const map<string, Function> &e)
        : env(e), deps(e) {
    }
};

}  // namespace

Stmt sliding_window(const Stmt &s, const map<string, Function> &env) {
    return SlidingWindow(env)(s);
}

}  // namespace Internal
}  // namespace Halide
