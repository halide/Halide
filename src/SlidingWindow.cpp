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
#include <algorithm>
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
        // When sliding over a let, it's the let's variable that steps back by
        // one, so leave it unexpanded.
        ScopedBinding<Expr> bind(op->name != loop_var, scope, op->name,
                                 simplify(expand_expr(op->value, scope)));
        return IRMutator::visit(op);
    }

public:
    RollFunc(const Function &func, int dim, const string &loop_var,
             const Interval &old_bounds, const Interval &new_bounds)
        : func(func), dim(dim), loop_var(loop_var), old_bounds(old_bounds), new_bounds(new_bounds) {
    }
};

// The level f was told to slide over that this loop or let carries, if any.
const LoopLevel *slides_over(const Function &f, const string &name) {
    for (const LoopLevel &l : f.schedule().slide_levels()) {
        if (l.defined() && !l.is_inlined() && !l.is_root() && l.match(name)) {
            return &l;
        }
    }
    return nullptr;
}

// Loop and let names in the IR are mangled, so error messages use the name
// the schedule used instead.
string slide_level_name(const Function &f, const string &name) {
    const LoopLevel *l = slides_over(f, name);
    return l ? l->to_string() : name;
}

// Compared against by pointer, to report naming two dimensions that move the
// same dimension of a Func's storage.
constexpr const char *already_slid = "this dimension has already been slid over";

// What sliding window decided to do with one Func over one loop or let. The
// rewrite makes no decisions of its own.
struct SlideDecision {
    string func_name;
    string slide_over;

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

    // The region required per iteration, before and after sliding.
    Interval old_bounds, new_bounds;

    // If defined, the iteration the loop should rewind to so that the window
    // is warm by the time the real work starts. If undefined, new_bounds
    // instead contains a select that computes the whole region on the first
    // iteration.
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
// for loop, or a let inside it. Rewrites the lets that give the region
// required of the func to only cover the sliver newly required this iteration.
class SlidingWindowOnFunctionAndLoop : public IRMutator {
    Function func;
    string loop_var;
    // The value of loop_var at the first iteration anything consumes this
    // func, which is earlier than the loop min if a consumer is warming up.
    Expr loop_min;
    set<int> &slid_dimensions;
    Scope<Expr> scope;
    Scope<Interval> &bounds_scope;

    // Whether the warm-up can prepend iterations to the loop. Otherwise it
    // warms up with a select.
    bool can_rewind;

    // Whether loop_var is the variable of the loop whose body we're
    // mutating, rather than a let inside it.
    bool over_loop;

    // The loops loop_var is built from.
    Scope<> loops_building_var;

    // Whether we're inside the definition of loop_var.
    bool var_in_scope;

    // For loops strictly between the loop being slid over and the current
    // node (not including the loop being slid over itself).
    Scope<> enclosing_loops;

    // How many of those inside the definition of loop_var run more than once.
    int enclosing_real_loops = 0;

    map<string, Expr> replacements;

    // Bounds inference emits a .min/.max let per enclosing loop level, and a
    // consumer staged at an outer level reads the outer one, so every one of
    // them enclosing the producer gets rewritten. Lets in sibling subtrees
    // must not be.
    vector<const LetStmt *> let_stack;
    set<const LetStmt *> producer_let_ancestors;

    // The loops (and the let slid over) we're inside of, with their bounds,
    // and how many of them enclose each let. An outer declaration has to bound
    // the replacement over the ones in between.
    vector<pair<string, Interval>> loop_bounds_stack;
    map<const LetStmt *, size_t> let_loop_depth;
    vector<pair<string, Interval>> producer_loop_bounds;

    // Likewise every name bound, because a replacement mentioning one from
    // further in can't be used at an outer declaration at all.
    vector<string> binding_stack;
    map<const LetStmt *, size_t> let_binding_depth;
    vector<string> producer_bindings;

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
            // By the time an outer loop advances, the inner loop has
            // overwritten everything a rolled register array held.
            return result.reject("it should roll over an inner loop instead");
        }
        if (!min_required.defined()) {
            return result.reject("more than one dimension depends on the loop var");
        }

        if (expr_uses_vars(min_required, loops_building_var) ||
            expr_uses_vars(max_required, loops_building_var)) {
            return result.reject("the region required depends on the loops the "
                                 "dimension is built from, not just the dimension");
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
        // met, so check its answer.
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
            // the min/max required at the loop min. This also keeps the
            // region empty on any warm-up iterations before the one we solved
            // for, which a let moving by more than one per iteration can have.
            if (can_slide_up) {
                Expr min_required_at_loop_min = substitute(loop_var, loop_min, min_required);
                new_min = max(new_min, min_required_at_loop_min);
            } else {
                Expr max_required_at_loop_min = substitute(loop_var, loop_min, max_required);
                new_max = min(new_max, max_required_at_loop_min);
            }
        } else {
            // We couldn't find a suitable new loop min, so we can't assume
            // every iteration has a previous iteration. The first iteration
            // will warm up the loop instead.
            Expr need_explicit_warmup = loop_var_expr <= loop_min;
            // A let slid over varies with loops outside the innermost one.
            auto mark = [&](const Expr &e) {
                return over_loop ? likely_if_innermost(e) : likely(e);
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

        replacements.clear();
        producer_let_ancestors.clear();
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
        producer_let_ancestors.insert(let_stack.begin(), let_stack.end());
        producer_loop_bounds = loop_bounds_stack;
        producer_bindings = binding_stack;

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
        if (!var_in_scope) {
            // Computed outside the let we're sliding over, so it doesn't move
            // with it.
            return op;
        }

        internal_assert(!decision.slid()) << "Slid " << func.name()
                                          << "'s producer twice\n";
        decision = plan_slide();
        decision.considered = true;
        debug(2) << decision;

        if (!decision.slid()) {
            return op;
        }
        slid_dimensions.insert(decision.dim_idx);

        if (func.schedule().memory_type() == MemoryType::Register) {
            // Sliding in registers leaves the region written alone, and
            // RollFunc rewrites the stores instead.
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
        ScopedBinding<> bind_building(!var_in_scope, loops_building_var, op->name);
        // The loops loop_var is built from bound it.
        Interval loop_bounds;
        if (!var_in_scope) {
            loop_bounds = Interval(bounds_of_expr_in_scope(op->min, bounds_scope).min,
                                   bounds_of_expr_in_scope(op->max, bounds_scope).max);
        }
        ScopedBinding<Interval> bind_bounds(!var_in_scope, bounds_scope, op->name, loop_bounds);
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
            ScopedValue<int> bind_count(enclosing_real_loops, enclosing_real_loops + var_in_scope);
            loop_bounds_stack.emplace_back(op->name, Interval(min, max));
            binding_stack.push_back(op->name);
            Stmt s = IRMutator::visit(op);
            binding_stack.pop_back();
            loop_bounds_stack.pop_back();
            return s;
        }
    }

    // The replacement describes the region required at the producer, so to
    // use it at an outer declaration, bound it over the loops in between.
    // Returns an undefined Expr if that can't be done, in which case the
    // declaration is left alone.
    Expr bound_over_inner_loops(const Expr &replacement, const string &let_name,
                                size_t loop_depth, size_t binding_depth) {
        Expr result = replacement;
        if (loop_depth < producer_loop_bounds.size()) {
            // Only the loops in between. Widening over the loop slid over too
            // would give the whole walk.
            Scope<Interval> inner;
            for (size_t i = loop_depth; i < producer_loop_bounds.size(); i++) {
                inner.push(producer_loop_bounds[i].first, producer_loop_bounds[i].second);
            }
            Interval b = bounds_of_expr_in_scope(replacement, inner);
            if (ends_with(let_name, ".min")) {
                result = b.has_lower_bound() ? b.min : Expr();
            } else {
                result = b.has_upper_bound() ? b.max : Expr();
            }
            if (!result.defined()) {
                return result;
            }
            result = simplify(result);
        }

        // Anything else bound between here and the producer isn't in scope.
        Scope<> inner_names;
        for (size_t i = binding_depth; i < producer_bindings.size(); i++) {
            inner_names.push(producer_bindings[i]);
        }
        if (expr_uses_vars(result, inner_names)) {
            return Expr();
        }
        return result;
    }

    Stmt visit(const LetStmt *op) override {
        if (op->name == loop_var) {
            // Keep the let we're sliding over opaque, so that the region
            // required is written in terms of it.
            Interval b = bounds_of_expr_in_scope(op->value, bounds_scope);
            if (b.has_lower_bound()) {
                b.min = simplify(b.min);
            }
            if (b.has_upper_bound()) {
                b.max = simplify(b.max);
            }
            ScopedBinding<Interval> bind_bounds(bounds_scope, op->name, b);
            ScopedValue<bool> bind_in_scope(var_in_scope, true);
            Scope<Interval> inner;
            for (const auto &l : loop_bounds_stack) {
                inner.push(l.first, l.second);
            }
            loop_bounds_stack.emplace_back(
                op->name, bounds_of_expr_in_scope(expand_expr(op->value, scope), inner));
            binding_stack.push_back(op->name);
            Stmt new_body = mutate(op->body);
            binding_stack.pop_back();
            loop_bounds_stack.pop_back();
            return op->with(op->value, new_body);
        }

        ScopedBinding<Interval> bind_bounds(bounds_scope, op->name,
                                            bounds_of_expr_in_scope(op->value, bounds_scope));
        ScopedBinding<Expr> bind(scope, op->name, simplify(expand_expr(op->value, scope), bounds_scope));

        let_stack.push_back(op);
        let_loop_depth[op] = loop_bounds_stack.size();
        let_binding_depth[op] = binding_stack.size();
        binding_stack.push_back(op->name);
        Stmt new_body = mutate(op->body);
        binding_stack.pop_back();
        let_stack.pop_back();

        Expr value = op->value;

        map<string, Expr>::iterator iter = replacements.find(op->name);
        if (iter != replacements.end() && producer_let_ancestors.count(op)) {
            Expr replacement = bound_over_inner_loops(iter->second, op->name,
                                                      let_loop_depth[op],
                                                      let_binding_depth[op]);
            if (replacement.defined()) {
                value = replacement;
            }
        }

        return op->with(value, new_body);
    }

public:
    // Slide over v, which starts at v_min. It is either the variable of the
    // loop whose body we mutate, or a let inside it, in which case outer_loop
    // names that loop.
    SlidingWindowOnFunctionAndLoop(Function f, string v, Expr v_min, set<int> &slid_dimensions,
                                   Scope<Interval> &bounds_scope, bool can_rewind,
                                   const string &outer_loop)
        : func(std::move(f)), loop_var(std::move(v)), loop_min(std::move(v_min)),
          slid_dimensions(slid_dimensions), bounds_scope(bounds_scope),
          can_rewind(can_rewind), over_loop(outer_loop.empty()),
          var_in_scope(outer_loop.empty()) {
        if (!over_loop) {
            loops_building_var.push(outer_loop);
        }
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

// Which Funcs a Func's production depends on, transitively.
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
    // Whether the loop guarded is the innermost one anything slid over, which
    // it isn't if a Func slid over a let inside it.
    bool innermost;

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
            // A producer warming up needs no guard, because its bounds are
            // empty until it has work.
            start = Expr();
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

    // Does every producer in this Stmt start at current_start? If not, we
    // need to descend and guard them separately.
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
                       bool innermost)
        : warmup_starts(warmup_starts), loop_var(loop_var),
          orig_loop_min(orig_loop_min), deps(deps), innermost(innermost),
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

// A let a Func was told to slide over, found in the body of a loop. A split
// Var has no loop of its own, but it still has a let.
struct SlideLet {
    string name;
    // Its value, and the loops between the loop and it (outermost first) with
    // their bounds, all written in terms of names in scope at the loop.
    Expr value;
    vector<pair<string, Interval>> loops;
    vector<ForType> for_types;
};

// Find the lets inside a Stmt that a Func was told to slide over, and that
// enclose its producer.
class FindSlideLets : public IRVisitor {
    const Function &func;
    Scope<Expr> lets;
    vector<pair<string, Interval>> loops;
    vector<ForType> for_types;

    using IRVisitor::visit;

    void visit(const For *op) override {
        loops.emplace_back(op->name, Interval(expand_expr(op->min, lets),
                                              expand_expr(op->max, lets)));
        for_types.push_back(op->for_type);
        op->body.accept(this);
        for_types.pop_back();
        loops.pop_back();
    }

    void visit(const LetStmt *op) override {
        Expr value = expand_expr(op->value, lets);
        if (slides_over(func, op->name)) {
            bool contains_producer = false;
            visit_with(op->body, [&](auto *self, const ProducerConsumer *pc) {
                contains_producer |= pc->is_producer && pc->name == func.name();
                self->visit_base(pc);
            });
            if (contains_producer) {
                result.push_back({op->name, value, loops, for_types});
            }
        }
        ScopedBinding<Expr> bind(lets, op->name, value);
        op->body.accept(this);
    }

    void visit(const ProducerConsumer *op) override {
        if (!(op->is_producer && op->name == func.name())) {
            IRVisitor::visit(op);
        }
    }

public:
    vector<SlideLet> result;

    FindSlideLets(const Function &func)
        : func(func) {
    }
};

// Something a Func slides over at a particular serial loop: that loop's
// variable, or a let inside it built from that loop and some loops inside it.
// The window advances by one each time it does, and warms up by prepending
// iterations to the loop.
struct SlideTarget {
    string var;
    // Its value and the loops between the loop and it, as found by
    // FindSlideLets. For the loop itself, that's just the loop variable.
    Expr value;
    vector<pair<string, Interval>> inner_loops;
    // How much one iteration of the loop advances var.
    Expr stride = 1;

    bool is_loop() const {
        const Variable *v = value.as<Variable>();
        return v && v->name == var;
    }

    // The value var first takes on the given iteration of the loop.
    Expr at_iteration(const string &loop, const Expr &i) const {
        Expr e = value;
        for (auto it = inner_loops.rbegin(); it != inner_loops.rend(); it++) {
            e = Let::make(it->first, it->second.min, e);
        }
        return simplify(Let::make(loop, i, e));
    }

    // The latest iteration of the loop no later than `from` on which var is
    // at or before q, which it never passes.
    Expr iteration_reaching(const string &loop, const Expr &from, const Expr &q) const {
        internal_assert(is_positive_const(stride));
        Expr steps = (at_iteration(loop, from) - q + stride - 1) / stride;
        return simplify(from - steps);
    }
};

// A window advances along what it slides over one step at a time. Given the
// value of a let, and the serial loops enclosing it from outermost to
// innermost, check that it goes up by exactly one each time the innermost of
// those loops advances, including when that makes the loops outside it advance
// and the ones inside it wrap around. Loops outside the outermost one it
// depends on are ignored. The scope bounds any variables in the loop bounds.
// Returns the name of a loop that breaks this, or an empty string if none
// does.
string loop_that_breaks_single_steps(const Expr &e,
                                     const vector<pair<string, Interval>> &loops,
                                     const Scope<Interval> &scope) {
    // The loops e moves with, outermost first, with how far e moves each time
    // one of them advances, and how many iterations it runs for.
    struct Digit {
        string name;
        int64_t step;
        Expr extent;
    };
    vector<Digit> digits;
    for (const auto &[name, b] : loops) {
        Expr extent;
        if (b.is_bounded()) {
            extent = simplify(b.max - b.min + 1);
            Interval eb = bounds_of_expr_in_scope(extent, scope);
            if (eb.is_bounded()) {
                eb.min = simplify(eb.min);
                if (is_const(eb.min) && equal(eb.min, simplify(eb.max))) {
                    extent = eb.min;
                }
            }
            if (is_const_one(extent)) {
                // It never advances.
                continue;
            }
        }
        ConstantInterval d = derivative_bounds(e, name);
        if (!d.is_single_point()) {
            return name;
        }
        if (digits.empty() && d.min == 0) {
            continue;
        }
        for (const Digit &outer : digits) {
            if (!b.is_bounded() ||
                !derivative_bounds(b.min, outer.name).is_single_point(0) ||
                !derivative_bounds(b.max, outer.name).is_single_point(0)) {
                return name;
            }
        }
        digits.push_back({name, d.min, extent});
    }

    // Each loop advancing, with every loop inside it wrapping back around to
    // its first iteration, has to move e on by exactly one from where the
    // loops inside it left it.
    Expr span = 0;
    for (size_t i = digits.size(); i-- > 0;) {
        if (!can_prove(make_const(Int(32), digits[i].step) == span + 1, scope)) {
            debug(3) << "Advancing " << digits[i].name << " moves " << e << " by "
                     << digits[i].step << " instead of " << span + 1 << "\n";
            return digits[i].name;
        }
        if (i > 0 && digits[i].step != 0) {
            span = simplify(span + (int)digits[i].step * (digits[i].extent - 1));
        }
    }
    return "";
}

// Perform sliding window optimization for all functions
class SlidingWindow : public IRMutator {
    const map<string, Function> &env;
    FuncDependencies deps;

    // A map of which dimensions we've already slid over, by Func name.
    map<string, set<int>> slid_dimensions;
    // For funcs told which dimensions to slide over, which of those claimed
    // each dimension of the func's storage. Only used to explain errors.
    map<string, map<int, string>> slid_over_by;

    // Keep track of realizations we want to slide, from innermost to
    // outermost.
    list<Function> sliding;
    // For each of those, how many loops we were already inside when we
    // reached its Realize. Loops outside that point don't have its storage
    // live across them.
    list<size_t> sliding_loop_depth;
    // The loops we're inside of, outermost first.
    vector<pair<string, ForType>> loop_stack;
    // For each let in scope, the loops its value depends on, directly or
    // through other lets.
    Scope<set<string>> let_loops;
    Scope<Interval> bounds_scope;

    using IRMutator::visit;

    // The loops an Expr depends on, directly or through lets.
    set<string> loops_used(const Expr &e) {
        set<string> result;
        visit_with(
            e,
            [&](auto *self, const Let *op) {
                ScopedBinding<set<string>> bind(let_loops, op->name, loops_used(op->value));
                op->body.accept(self);
            },
            [&](auto *self, const Variable *v) {
                if (const set<string> *s = let_loops.find(v->name)) {
                    result.insert(s->begin(), s->end());
                } else if (std::any_of(loop_stack.begin(), loop_stack.end(),
                                       [&](const auto &l) { return l.first == v->name; })) {
                    result.insert(v->name);
                }
            });
        return result;
    }

    size_t storage_depth(const Function &f) {
        auto d = sliding_loop_depth.begin();
        for (auto it = sliding.begin(); it != sliding.end(); it++, d++) {
            if (it->name() == f.name()) {
                return *d;
            }
        }
        internal_error << "Not sliding " << f.name() << "\n";
        return 0;
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

    // The things func can slide over at the serial loop op, currently called
    // name and with the given body. A func not told what to slide over slides
    // over every loop. A func told what to slide over slides over each loop
    // and let it names, at the outermost loop it varies over.
    vector<SlideTarget> slide_targets(const Function &func, const For *op,
                                      const string &name, const Stmt &body) {
        vector<SlideTarget> targets;
        const bool named = !func.schedule().slide_levels().empty();
        if (!named || slides_over(func, op->name)) {
            targets.push_back({name, Variable::make(Int(32), name)});
        }
        if (!named) {
            return targets;
        }

        FindSlideLets finder(func);
        body.accept(&finder);
        const size_t depth = storage_depth(func);
        for (const SlideLet &l : finder.result) {
            set<string> used = loops_used(l.value);
            size_t outer = 0;
            while (outer < loop_stack.size() && !used.count(loop_stack[outer].first)) {
                outer++;
            }
            if (outer < depth) {
                // Sliding assumes the previous iteration's values are still
                // there, and an allocation inside one of the loops the
                // dimension varies over is thrown away and remade as they run.
                user_error
                    << "Func " << func.name() << " was told to slide over "
                    << slide_level_name(func, l.name) << ", but that "
                    << "dimension varies over the loop " << loop_stack[outer].first
                    << ", which is outside " << func.name() << "'s storage. Sliding "
                    << "would read values from a previous iteration of that "
                    << "loop, which have been thrown away by then. Store "
                    << func.name() << " at a coarser level, or slide over a "
                    << "dimension that doesn't span that loop.\n";
            }
            auto check_serial = [&](const string &loop, ForType t) {
                user_assert(t == ForType::Serial || t == ForType::Unrolled)
                    << "Func " << func.name() << " was told to slide over "
                    << slide_level_name(func, l.name) << ", but that dimension "
                    << "varies over the loop " << loop << ", which is " << t
                    << ". A window can only slide along loops that run in "
                    << "order, one iteration at a time.\n";
            };
            if (outer < loop_stack.size()) {
                // It slides at that outer loop instead.
                check_serial(loop_stack[outer].first, loop_stack[outer].second);
                continue;
            }
            if (!expr_uses_var(l.value, name)) {
                // It slides at an inner loop instead.
                continue;
            }
            for (size_t i = 0; i < l.loops.size(); i++) {
                check_serial(l.loops[i].first, l.for_types[i]);
            }

            // The window advances by one each time the let does, so the let
            // has to step through its values one at a time as the loops run,
            // including on any warm-up iterations prepended to this one.
            vector<pair<string, Interval>> loops = l.loops;
            loops.insert(loops.begin(), {name, Interval::everything()});
            string bad = loop_that_breaks_single_steps(l.value, loops, bounds_scope);
            user_assert(bad.empty())
                << "Func " << func.name() << " was told to slide over "
                << slide_level_name(func, l.name) << ", but that dimension does "
                << "not step through its values one at a time as the loop nest "
                << "runs: the loop " << bad << " breaks the sequence. A window "
                << "moves forwards one value at a time, so sliding would skip "
                << "or repeat values. Reordering the loops of a split, putting "
                << "another loop between them (as tile does), or a ShiftInwards "
                << "tail strategy where the extent isn't a multiple of the split "
                << "factor all do this. Slide over a dimension that counts "
                << "upwards instead.\n";

            SlideTarget t{l.name, l.value, l.loops};
            ConstantInterval stride = derivative_bounds(l.value, name);
            internal_assert(stride.is_single_point());
            t.stride = make_const(Int(32), stride.min);
            targets.push_back(t);
        }
        return targets;
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<Interval> bind(bounds_scope, op->name,
                                     bounds_of_expr_in_scope(op->value, bounds_scope));
        ScopedBinding<set<string>> bind_loops(let_loops, op->name, loops_used(op->value));
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
    // funcs they consume. This is not the order of the realizations, because
    // e.g. a store_root'd producer is realized outside its consumers.
    vector<Function> consumers_first(const list<Function> &funcs) {
        // depends_on is transitive, so a Func depends on strictly more of the
        // others than anything it consumes does.
        vector<pair<int, Function>> keyed;
        for (const Function &f : funcs) {
            int n = 0;
            for (const Function &g : funcs) {
                n += g.name() != f.name() && deps.depends_on(g.name(), f.name());
            }
            keyed.emplace_back(n, f);
        }
        std::stable_sort(keyed.begin(), keyed.end(),
                         [](const auto &a, const auto &b) { return a.first > b.first; });
        vector<Function> result;
        for (const auto &k : keyed) {
            result.push_back(k.second);
        }
        return result;
    }

    Stmt visit(const For *op) override {
        if (!(op->for_type == ForType::Serial || op->for_type == ForType::Unrolled)) {
            for (const Function &func : sliding) {
                user_assert(!slides_over(func, op->name))
                    << "Func " << func.name() << " was told to slide over "
                    << slide_level_name(func, op->name) << ", but that loop is "
                    << op->for_type << ". A window can only slide along loops "
                    << "that run in order, one iteration at a time.\n";
            }
            loop_stack.emplace_back(op->name, op->for_type);
            Stmt s = IRMutator::visit(op);
            loop_stack.pop_back();
            return s;
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
        // Whether everything that rewound the loop slid over the loop itself,
        // rather than over a let inside it.
        bool rewound_for_loop_only = true;
        // The loop min before any warm-up iterations were prepended.
        // <loop>.loop_min follows the loop as we rewind it, so this gets a
        // name of its own.
        string orig_loop_min_name = op->name + ".loop_min.orig";
        Expr orig_loop_min = Variable::make(Int(32), orig_loop_min_name);

        for (const Function &func : consumers_first(sliding)) {
            // Rewinding the loop renames it, so find the targets afresh each
            // time.
            for (size_t k = 0;; k++) {
                vector<SlideTarget> targets = slide_targets(func, op, name, body);
                if (k >= targets.size()) {
                    break;
                }
                const SlideTarget &target = targets[k];
                debug(3) << "Doing sliding window analysis on function " << func.name()
                         << " over " << target.var << "\n";

                // Figure out the first iteration at which this func is
                // consumed. If nothing that consumes it is warming up a window
                // of its own, that's just the loop min.
                Expr consumed_from = orig_loop_min;
                // Otherwise we need to have this func warmed up in time for
                // the earliest-starting consumer, which means rewinding the
                // loop even further than they did.
                for (const auto &i : warmup_starts) {
                    if (deps.depends_on(func.name(), i.first)) {
                        consumed_from = simplify(min(consumed_from, i.second));
                    }
                }

                set<int> &slid_dims = slid_dimensions[func.name()];
                size_t old_slid_dims_size = slid_dims.size();

                Interval min_bounds = bounds_of_expr_in_scope(loop_min, bounds_scope);
                Interval max_bounds = bounds_of_expr_in_scope(loop_max, bounds_scope);
                ScopedBinding<Interval> bind_bounds(bounds_scope, name,
                                                    Interval(min_bounds.min, max_bounds.max));

                SlidingWindowOnFunctionAndLoop slider(func, target.var,
                                                      target.at_iteration(name, consumed_from),
                                                      slid_dims, bounds_scope,
                                                      is_positive_const(target.stride),
                                                      target.is_loop() ? "" : name);

                body = slider(body);
                const SlideDecision &decision = slider.decision;
                if (!func.schedule().slide_levels().empty()) {
                    check_not_entangled(func, target.var, decision);
                    if (decision.slid()) {
                        slid_over_by[func.name()][decision.dim_idx] = target.var;
                    }
                }
                if (func.schedule().memory_type() == MemoryType::Register &&
                    decision.slid() && decision.old_bounds.has_lower_bound()) {
                    body = slider.translate_loop(body);
                }

                if (decision.warmup_start.defined()) {
                    // An earlier func may already have rewound the loop
                    // further than this, so keep the earlier of the two.
                    Expr warmup_start = target.iteration_reaching(name, orig_loop_min,
                                                                  decision.warmup_start);
                    rewound_for_loop_only &= target.is_loop();
                    warmup_starts.emplace_front(func.name(), warmup_start);
                    warming_up[func.name()] = warmup_start;
                    Expr new_loop_min =
                        loop_min.same_as(op->min) ? warmup_start : min(warmup_start, loop_min);

                    // Rename the loop var in the body, and with it any
                    // reference to the loop's min, so that <loop>.loop_min
                    // keeps meaning the min of that loop. Storage folding
                    // relies on it.
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
                    // Let storage folding know there's now a read-after-write
                    // hazard here, where the variable slid over is defined. A
                    // loop may have been renamed above.
                    string var = target.is_loop() ? name : target.var;
                    Stmt marker = Evaluate::make(
                        Call::make(Int(32), Call::sliding_window_marker,
                                   {func.name(), Variable::make(Int(32), var)},
                                   Call::Intrinsic));
                    if (target.is_loop()) {
                        body = Block::make(marker, body);
                    } else {
                        body = mutate_with(body, [&](auto *self, const LetStmt *l) -> Stmt {
                            if (l->name == var) {
                                return LetStmt::make(l->name, l->value, Block::make(marker, l->body));
                            }
                            return self->visit_base(l);
                        });
                    }
                }
            }
        }

        if (!warming_up.empty()) {
            // The loop now starts before the original loop min. Everything the
            // extra iterations aren't there to warm up must be skipped over.
            body = InjectWarmupGuards(warming_up, name, orig_loop_min, deps,
                                      rewound_for_loop_only)(body);
        }

        {
            Interval lo = bounds_of_expr_in_scope(loop_min, bounds_scope);
            Interval hi = bounds_of_expr_in_scope(loop_max, bounds_scope);
            ScopedBinding<Interval> bind(bounds_scope, name, Interval(lo.min, hi.max));
            loop_stack.emplace_back(name, op->for_type);
            body = mutate(body);
            loop_stack.pop_back();
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
