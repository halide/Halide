#include <algorithm>
#include <numeric>
#include <utility>

#include "CSE.h"
#include "CodeGen_GPU_Dev.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "PartitionLoops.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::pair;
using std::string;
using std::vector;

namespace {

// Loop partitioning only applies to things marked as 'likely'. Loads
// through hand-written boundary conditions will produce clamped
// ramps, which will turn into gathers. This pass injects likely
// intrinsics so that these clamped ramps are picked up by loop
// partitioning.
class MarkClampedRampsAsLikely : public IRMutator {
    using IRMutator::visit;
    Expr visit(const Min *op) override {
        if (in_index && op->a.as<Ramp>()) {
            // No point recursing into the ramp - it can't contain
            // another ramp.
            return min(likely(op->a), mutate(op->b));
        } else if (in_index && op->b.as<Ramp>()) {
            return min(mutate(op->a), likely(op->b));
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Max *op) override {
        if (in_index && op->a.as<Ramp>()) {
            return max(likely(op->a), mutate(op->b));
        } else if (in_index && op->b.as<Ramp>()) {
            return max(mutate(op->a), likely(op->b));
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Load *op) override {
        bool old_in_index = in_index;
        in_index = true;
        Expr expr = IRMutator::visit(op);
        in_index = old_in_index;
        return expr;
    }

    Stmt visit(const Store *op) override {
        bool old_in_index = in_index;
        in_index = true;
        Expr index = mutate(op->index);
        in_index = old_in_index;
        Expr value = mutate(op->value);
        Expr predicate = mutate(op->predicate);
        if (predicate.same_as(op->predicate) && index.same_as(op->index) && value.same_as(op->value)) {
            return op;
        } else {
            return Store::make(op->name, value, index, op->param, predicate, op->alignment);
        }
    }

    bool in_index = false;
};

// Check if an expression or statement uses a likely tag
class HasLikelyTag : public IRVisitor {
protected:
    using IRVisitor::visit;
    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::likely)) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    bool result = false;
};

class HasUncapturedLikelyTag : public HasLikelyTag {
    using HasLikelyTag::visit;

    // Any likelies buried inside the following ops are captured the by respective ops
    void visit(const Select *op) override {
    }
    void visit(const Min *op) override {
    }
    void visit(const Max *op) override {
    }
};

// The goal of loop partitioning is to split loops up into a prologue,
// a clean steady state, and an epilogue. The next visitor
// (FindSimplifications) finds a list of simplifications that can be
// applied to produce that clean steady-state version of the loop
// body. It tries to simplify selects, mins, and maxes to just their
// likely branch. For example:
//
//   select(a, likely(b), c)     -> b
//   select(a, b, 5 + likely(c)) -> 5 + c
//   max(a, likely(b))           -> b
//
// These three simplifications are only valid if a is true, false, or
// less than b, respectively. So we visit the loop body looking for
// these sort of things, record the associated conditions, and try to
// solve for a range of the loop variable for which all of our
// conditions are true (by solving for each one and then taking the
// intersection). That gives us the clean steady state.
//
// It may be that we can also make some simplifications to the
// prologue or epilogue. For example, consider the case:
//
//   select(x > 0, likely(expr_t), expr_f)
//
// It can simplify to expr_t when x > 0. However, if this is the sole
// simplification which gives us a lower bound on x for the steady
// state, we can also simplify this select in the prologue to just be
// expr_f.
//
// Now consider this case:
//
//   (select(x > a, likely(expr_t1), expr_f1) +
//    select(x > b, likely(expr_t2), expr_f2))
//
// The steady state starts at x == max(a, b), which we get from the
// intersection of the intervals derived from each condition: x > a
// and x > b. In the steady state, the expression simplifies to
// expr_t1 + expr_t2. In the prologue we know that either x <= a or x
// <= b, but we don't know which one might be true, so we can't make
// any simplifications to the prologue.
//
// We may also encounter single conditions where we can simplify the
// steady-state but not the prologue. Say we're splitting up a loop
// over x and we encounter a condition that depends on a variable
// introduced in some inner loop:
//
// for x:
//   for z from 0 to 10:
//     ... select(x > z, likely(expr_t), expr_f) ...
//
// This select definitely simplifies to expr_t when x > 9, because
// that's the maximum value z could be, so we'll start the steady
// state at x == 10. This means the prologue covers values like x ==
// 5, where the select could be either true or false, so we can't make
// any simplifications to the prologue.
//
// There are some simplifications that we won't be able to do. For
// example, if we're partitioning the loop over x, and we encounter:
//
// for x:
//   for z from 0 to 10:
//     ... select(z < 5, likely(expr_t), expr_f)
//
// Restricting the range of x isn't going to simplify that expression
// - it doesn't even depend on x. We just make all the simplifications
// that we can, and take the intersection of the resulting regions. In
// this case, we'll make that simplification later, when we do loop
// partitioning over the loop in z. Some cases we'll never
// handle. E.g. consider:
//
// for x:
//   ... select(a + x*(b + x*(c + x*(d + x*e))) > 0, likely(expr_t), expr_f)
//
// In order to simplify that we'd have to come up with a formula that
// tells us an interval where a quintic is strictly positive. No such
// general formula exists (because no formula exists for the roots),
// so there's no programmatic way we can partition the loop over x to
// make that condition simplify. Finally my Galois theory course pays
// off. For failures like this, we just drop the likely tag. So loop
// partitioning is best-effort, but it should always work for things
// like x > a. A simpler case for which we bail is:
//
// for x:
//   ... select(x == 5, expr_t, likely(expr_f))
//
// This simplifies to the likely case in two disjoint ranges, but
// we're only producing one steady state, and we have no reason to
// believe one side is better than the other, so we just bail and drop
// the likely tag.

// First we define the struct that represents a single simplification
// that can be applied to the steady state of the loop.
struct Simplification {
    // This condition is sufficient for the simplification to occur.
    Expr condition;
    // The expression we're simplifying
    Expr old_expr;
    // The replacement if the condition is true
    Expr likely_value;
    // The replacement if the condition is false. Not useful
    // unless it's tight.
    Expr unlikely_value;
    // Is the condition necessary (as well as sufficient)?
    bool tight;
    // The interval over which this simplification applies. Comes from solving the condition.
    Interval interval;
};

class ExprUsesInvalidBuffers : public IRVisitor {
    using IRVisitor::visit;

    const Scope<> &invalid_buffers;

    void visit(const Load *op) override {
        if (invalid_buffers.contains(op->name)) {
            invalid = true;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    ExprUsesInvalidBuffers(const Scope<> &buffers)
        : invalid_buffers(buffers), invalid(false) {
    }
    bool invalid;
};

/** Check if any references to buffers in an expression is invalid. */
bool expr_uses_invalid_buffers(const Expr &e, const Scope<> &invalid_buffers) {
    ExprUsesInvalidBuffers uses(invalid_buffers);
    e.accept(&uses);
    return uses.invalid;
}

// Then we define the visitor that hunts for them.
class FindSimplifications : public IRVisitor {
    using IRVisitor::visit;

    Scope<> depends_on_loop_var, depends_on_invalid_buffers;
    Scope<> buffers;

    void visit(const Allocate *op) override {
        buffers.push(op->name);
        IRVisitor::visit(op);
    }

    void new_simplification(Expr condition, Expr old, Expr likely_val, Expr unlikely_val) {
        if (!expr_uses_vars(condition, depends_on_loop_var)) {
            return;
        }

        if (expr_uses_vars(condition, depends_on_invalid_buffers) ||
            expr_uses_invalid_buffers(condition, buffers)) {
            // The condition refers to buffer allocated in the inner loop.
            // We should throw away the condition
            return;
        }
        condition = remove_likelies(condition);
        Simplification s = {condition, std::move(old), std::move(likely_val), std::move(unlikely_val), true};
        if (s.condition.type().is_vector()) {
            s.condition = simplify(s.condition);
            if (const Broadcast *b = s.condition.as<Broadcast>()) {
                s.condition = b->value;
            } else {
                // Devectorize the condition
                s.condition = and_condition_over_domain(s.condition, Scope<Interval>::empty_scope());
                s.tight = false;
            }
        }
        internal_assert(s.condition.type().is_scalar()) << s.condition << "\n";
        simplifications.push_back(s);
    }

    void visit(const Min *op) override {
        bool likely_a = has_uncaptured_likely_tag(op->a);
        bool likely_b = has_uncaptured_likely_tag(op->b);

        // If one side has an uncaptured likely, don't hunt for
        // simplifications in the other side.
        if (!likely_a) {
            op->b.accept(this);
        }
        if (!likely_b) {
            op->a.accept(this);
        }

        // Prefer the side that has an uncaptured top-level likely
        // call. If neither does, prefer the side that contains any
        // likely call at all.
        if (!likely_a && !likely_b) {
            likely_a = has_likely_tag(op->a);
            likely_b = has_likely_tag(op->b);
        }

        if (likely_b && !likely_a) {
            new_simplification(op->b <= op->a, op, op->b, op->a);
        } else if (likely_a && !likely_b) {
            new_simplification(op->a <= op->b, op, op->a, op->b);
        }
    }

    void visit(const Max *op) override {
        bool likely_a = has_uncaptured_likely_tag(op->a);
        bool likely_b = has_uncaptured_likely_tag(op->b);

        if (!likely_a) {
            op->b.accept(this);
        }
        if (!likely_b) {
            op->a.accept(this);
        }

        if (!likely_a && !likely_b) {
            likely_a = has_likely_tag(op->a);
            likely_b = has_likely_tag(op->b);
        }

        if (likely_b && !likely_a) {
            new_simplification(op->b >= op->a, op, op->b, op->a);
        } else if (likely_a && !likely_b) {
            new_simplification(op->a >= op->b, op, op->a, op->b);
        }
    }

    void visit_select(const Expr &condition, const Expr &old, const Expr &true_value, const Expr &false_value) {
        condition.accept(this);

        bool likely_t = has_uncaptured_likely_tag(true_value);
        bool likely_f = has_uncaptured_likely_tag(false_value);

        if (!likely_t && !likely_f) {
            likely_t = has_likely_tag(true_value);
            likely_f = has_likely_tag(false_value);
        }

        if (!likely_t) {
            false_value.accept(this);
        }
        if (!likely_f) {
            true_value.accept(this);
        }

        if (likely_t && !likely_f) {
            new_simplification(condition, old, true_value, false_value);
        } else if (likely_f && !likely_t) {
            new_simplification(!condition, old, false_value, true_value);
        }
    }

    void visit(const Select *op) override {
        visit_select(op->condition, op, op->true_value, op->false_value);
    }

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else)) {
            if (op->args.size() == 3) {
                visit_select(op->args[0], op, op->args[1], op->args[2]);
            } else {
                visit_select(op->args[0], op, op->args[1], make_zero(op->type));
            }
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const IfThenElse *op) override {
        // For select statements, mins, and maxes, you can mark the
        // likely branch with likely. For if statements there's no way
        // to mark the likely stmt. So if the condition of an if
        // statement is marked as likely, treat it as likely true and
        // partition accordingly.
        IRVisitor::visit(op);
        if (has_uncaptured_likely_tag(op->condition)) {
            new_simplification(op->condition, op->condition, const_true(), const_false());
        }
    }

    void visit(const For *op) override {
        vector<Simplification> old;
        old.swap(simplifications);
        IRVisitor::visit(op);

        // Relax all the new conditions using the loop bounds
        for (Simplification &s : simplifications) {
            if (expr_uses_var(s.condition, op->name)) {
                Scope<Interval> varying;
                varying.push(op->name, Interval(op->min, op->min + op->extent - 1));
                Expr relaxed = and_condition_over_domain(s.condition, varying);
                internal_assert(!expr_uses_var(relaxed, op->name))
                    << "Should not have had used the loop var (" << op->name
                    << ") any longer\n  before: " << s.condition << "\n  after: "
                    << relaxed << "\n";
                if (!equal(relaxed, s.condition)) {
                    s.tight = false;
                }
                s.condition = relaxed;
            }
        }

        simplifications.insert(simplifications.end(), old.begin(), old.end());
    }

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        if (has_uncaptured_likely_tag(op->predicate)) {
            const int lanes = op->predicate.type().lanes();
            new_simplification(op->predicate, op->predicate, const_true(lanes), op->predicate);
        }
    }

    void visit(const Load *op) override {
        IRVisitor::visit(op);
        if (has_uncaptured_likely_tag(op->predicate)) {
            const int lanes = op->predicate.type().lanes();
            new_simplification(op->predicate, op->predicate, const_true(lanes), op->predicate);
        }
    }

    template<typename LetOrLetStmt>
    void visit_let(const LetOrLetStmt *op) {
        ScopedBinding<> bind_varying(expr_uses_vars(op->value, depends_on_loop_var),
                                     depends_on_loop_var, op->name);
        ScopedBinding<> bind_invalid(expr_uses_invalid_buffers(op->value, buffers) ||
                                         expr_uses_vars(op->value, depends_on_invalid_buffers),
                                     depends_on_invalid_buffers, op->name);
        vector<Simplification> old;
        old.swap(simplifications);
        IRVisitor::visit(op);
        for (Simplification &s : simplifications) {
            if (expr_uses_var(s.condition, op->name)) {
                s.condition = Let::make(op->name, op->value, s.condition);
            }
        }
        simplifications.insert(simplifications.end(), old.begin(), old.end());
    }

    void visit(const LetStmt *op) override {
        visit_let(op);
    }

    void visit(const Let *op) override {
        visit_let(op);
    }

public:
    vector<Simplification> simplifications;

    FindSimplifications(const std::string &v) {
        depends_on_loop_var.push(v);
    }
};

// Blindly apply a list of simplifications.
class MakeSimplifications : public IRMutator {
    using IRMutator::visit;

    const vector<Simplification> &simplifications;

public:
    MakeSimplifications(const vector<Simplification> &s)
        : simplifications(s) {
    }

    using IRMutator::mutate;
    Expr mutate(const Expr &e) override {
        for (auto const &s : simplifications) {
            if (e.same_as(s.old_expr)) {
                return mutate(s.likely_value);
            }
        }
        return IRMutator::mutate(e);
    }
};

class ContainsWarpSynchronousLogic : public IRVisitor {
public:
    bool result = false;

protected:
    using IRVisitor::visit;
    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::gpu_thread_barrier)) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const For *op) override {
        if (op->for_type == ForType::GPULane) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) override {
    }
};

bool contains_warp_synchronous_logic(const Stmt &s) {
    ContainsWarpSynchronousLogic c;
    s.accept(&c);
    return c.result;
}

class PartitionLoops : public IRMutator {
    using IRMutator::visit;

    bool in_gpu_loop = false;

    Stmt visit(const For *op) override {
        Stmt body = op->body;

        ScopedValue<bool> old_in_gpu_loop(in_gpu_loop, in_gpu_loop ||
                                                           CodeGen_GPU_Dev::is_gpu_var(op->name));

        // If we're inside GPU kernel, and the body contains thread
        // barriers or warp shuffles, it's not safe to partition loops.
        if (in_gpu_loop && contains_warp_synchronous_logic(op)) {
            return IRMutator::visit(op);
        }

        // Find simplifications in this loop body
        FindSimplifications finder(op->name);
        body.accept(&finder);

        if (finder.simplifications.empty()) {
            return IRMutator::visit(op);
        }

        debug(3) << "\n\n**** Partitioning loop over " << op->name << "\n";

        vector<Expr> min_vals, max_vals;
        vector<Simplification> middle_simps, prologue_simps, epilogue_simps;
        bool lower_bound_is_tight = true, upper_bound_is_tight = true;
        for (auto &s : finder.simplifications) {
            // Solve for the interval over which this simplification is true.
            s.interval = solve_for_inner_interval(s.condition, op->name);
            if (s.tight) {
                // Check if the solve is tight. I.e. the condition is
                // definitely false outside of the interval.
                Interval outer = solve_for_outer_interval(s.condition, op->name);
                s.tight &= equal(outer.min, s.interval.min) && equal(outer.max, s.interval.max);
            }

            debug(3) << "\nSimplification: \n"
                     << "  condition: " << s.condition << "\n"
                     << "  old: " << s.old_expr << "\n"
                     << "  new: " << s.likely_value << "\n"
                     << "  min: " << s.interval.min << "\n"
                     << "  max: " << s.interval.max << "\n"
                     << "  tight: " << s.tight << "\n";

            // Accept all non-empty intervals
            if (!s.interval.is_empty()) {
                if (s.interval.has_lower_bound()) {
                    Expr m = s.interval.min;
                    if (!s.tight) {
                        lower_bound_is_tight = false;
                    }
                    if (min_vals.empty()) {
                        min_vals.push_back(m);
                    } else if (equal(m, min_vals.back())) {
                        // We already have this min val
                    } else {
                        // This is a new distinct min val
                        min_vals.push_back(m);
                        lower_bound_is_tight = false;
                    }
                }
                if (s.interval.has_upper_bound()) {
                    Expr m = s.interval.max;
                    if (!s.tight) {
                        upper_bound_is_tight = false;
                    }
                    if (max_vals.empty()) {
                        max_vals.push_back(m);
                    } else if (equal(m, max_vals.back())) {
                        // We already have this max val
                    } else {
                        // This is a new distinct max val
                        max_vals.push_back(m);
                        upper_bound_is_tight = false;
                    }
                }

                // We'll apply this simplification to the
                // steady-state.
                middle_simps.push_back(s);
            }
        }

        // In general we can't simplify the prologue - it may run up
        // to after the epilogue starts for small images. However if
        // we can prove the epilogue starts after the prologue ends,
        // we're OK.
        bool can_simplify_prologue = true;
        for (const Expr &min_val : min_vals) {
            for (const Expr &max_val : max_vals) {
                Expr test = simplify(common_subexpression_elimination(min_val - 1 < max_val + 1));
                if (!is_const_one(test)) {
                    can_simplify_prologue = false;
                }
            }
        }

        // Find simplifications we can apply to the prologue and epilogue.
        for (const auto &s : middle_simps) {
            // If it goes down to minus infinity, we can also
            // apply it to the prologue
            if (can_simplify_prologue &&
                !s.interval.has_lower_bound()) {
                prologue_simps.push_back(s);
            }

            // If it goes up to positive infinity, we can also
            // apply it to the epilogue
            if (!s.interval.has_upper_bound()) {
                epilogue_simps.push_back(s);
            }

            // If our simplifications only contain one lower bound, and
            // it's tight, then the reverse rule can be applied to the
            // prologue.
            if (can_simplify_prologue &&
                s.interval.has_lower_bound() &&
                lower_bound_is_tight) {
                internal_assert(s.tight);
                Simplification s2 = s;
                // This condition is never used (we already solved
                // for the interval), but it's nice for it to be
                // correct.
                s2.condition = !s2.condition;
                std::swap(s2.likely_value, s2.unlikely_value);
                prologue_simps.push_back(s2);
            }
            if (s.interval.has_upper_bound() &&
                upper_bound_is_tight) {
                internal_assert(s.tight);
                Simplification s2 = s;
                s2.condition = !s2.condition;
                std::swap(s2.likely_value, s2.unlikely_value);
                epilogue_simps.push_back(s2);
            }
        }

        // Simplify each section of the loop.
        Stmt simpler_body = MakeSimplifications(middle_simps).mutate(body);
        Stmt prologue = MakeSimplifications(prologue_simps).mutate(body);
        Stmt epilogue = MakeSimplifications(epilogue_simps).mutate(body);

        bool make_prologue = !equal(prologue, simpler_body);
        bool make_epilogue = !equal(epilogue, simpler_body);

        // Recurse on the middle section.
        simpler_body = mutate(simpler_body);

        // Construct variables for the bounds of the simplified middle section
        Expr min_steady = op->min, max_steady = op->extent + op->min;
        Expr prologue_val, epilogue_val;
        string prologue_name = unique_name(op->name + ".prologue");
        string epilogue_name = unique_name(op->name + ".epilogue");

        if (make_prologue) {
            // They'll simplify better if you put them in
            // lexicographic order. This puts things like (x+1) and
            // (x+3) next to each other so that the simplifier sees
            // them together and can drop one of them.
            std::sort(min_vals.begin(), min_vals.end(), IRDeepCompare());
            min_vals.push_back(op->min);
            prologue_val = fold_left(min_vals, Max::make);
            // Stop the prologue from running past the end of the loop
            prologue_val = min(prologue_val, op->extent + op->min);
            // prologue_val = print(prologue_val, prologue_name);
            min_steady = Variable::make(Int(32), prologue_name);

            internal_assert(!expr_uses_var(prologue_val, op->name));
        }
        if (make_epilogue) {
            std::sort(max_vals.begin(), max_vals.end(), IRDeepCompare());
            max_vals.push_back(op->min + op->extent - 1);
            epilogue_val = fold_left(max_vals, Min::make) + 1;
            // Stop the epilogue from running before the start of the loop/prologue
            if (make_prologue) {
                epilogue_val = max(epilogue_val, prologue_val);
            } else {
                epilogue_val = max(op->min, epilogue_val);
            }
            // epilogue_val = print(epilogue_val, epilogue_name);
            max_steady = Variable::make(Int(32), epilogue_name);

            internal_assert(!expr_uses_var(epilogue_val, op->name));
        }

        Stmt stmt;
        // Bust simple serial for loops up into three.
        if (op->for_type == ForType::Serial && !op->body.as<Acquire>()) {
            stmt = For::make(op->name, min_steady, max_steady - min_steady,
                             op->for_type, op->device_api, simpler_body);

            if (make_prologue) {
                prologue = For::make(op->name, op->min, min_steady - op->min,
                                     op->for_type, op->device_api, prologue);
                stmt = Block::make(prologue, stmt);
            }
            if (make_epilogue) {
                epilogue = For::make(op->name, max_steady, op->min + op->extent - max_steady,
                                     op->for_type, op->device_api, epilogue);
                stmt = Block::make(stmt, epilogue);
            }
        } else {
            // For parallel for loops we could use a Fork node here,
            // but that would introduce the more complicated parallel
            // runtime into code that doesn't use async(), which may
            // interfere with legacy overrides of
            // halide_do_par_for. So for parallel for loops just put
            // an if-then-else in the loop body. It should
            // branch-predict to the steady state pretty well.
            //
            // Simple serial for loops that contain an Acquire node go
            // into the task system as a single entity, but Block
            // nodes do not, so we get a flatter task graph if we do
            // the same trick.
            Expr loop_var = Variable::make(Int(32), op->name);
            stmt = simpler_body;
            if (make_epilogue && make_prologue && equal(prologue, epilogue)) {
                stmt = IfThenElse::make(min_steady <= loop_var && loop_var < max_steady, stmt, prologue);
            } else {
                if (make_epilogue) {
                    stmt = IfThenElse::make(loop_var < max_steady, stmt, epilogue);
                }
                if (make_prologue) {
                    stmt = IfThenElse::make(loop_var < min_steady, prologue, stmt);
                }
            }
            stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, stmt);
        }

        if (make_epilogue) {
            // Uncomment to include code that prints the epilogue value
            // epilogue_val = print(epilogue_val, op->name, "epilogue");
            stmt = LetStmt::make(epilogue_name, epilogue_val, stmt);
        } else {
            epilogue_val = op->min + op->extent;
        }
        if (make_prologue) {
            // Uncomment to include code that prints the prologue value
            // prologue_val = print(prologue_val, op->name, "prologue");
            stmt = LetStmt::make(prologue_name, prologue_val, stmt);
        } else {
            prologue_val = op->min;
        }

        if (can_prove(epilogue_val <= prologue_val)) {
            // The steady state is empty. I've made a huge
            // mistake. Try to partition a loop further in.
            return IRMutator::visit(op);
        }

        debug(3) << "Partition loop.\n"
                 << "Old: " << Stmt(op) << "\n"
                 << "New: " << stmt << "\n";

        return stmt;
    }
};

class ExprContainsLoad : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Load *op) override {
        result = true;
    }

public:
    bool result = false;
};

bool expr_contains_load(const Expr &e) {
    ExprContainsLoad l;
    e.accept(&l);
    return l.result;
}

// The loop partitioning logic can introduce if and let statements in
// between GPU loop levels. This pass moves them inwards or outwards.
class RenormalizeGPULoops : public IRMutator {
    bool in_gpu_loop = false, in_thread_loop = false;

    using IRMutator::visit;

    // Track all vars that depend on GPU loop indices or loops inside GPU kernels.
    Scope<> gpu_vars;

    vector<pair<string, Expr>> lifted_lets;

    Stmt visit(const For *op) override {
        bool old_in_gpu_loop = in_gpu_loop;
        Stmt stmt;

        if (in_gpu_loop || CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            gpu_vars.push(op->name);
            in_gpu_loop = true;
        }

        if (ends_with(op->name, "__thread_id_x")) {
            internal_assert(!in_thread_loop);
            in_thread_loop = true;
            stmt = IRMutator::visit(op);
            in_thread_loop = false;
        } else {
            stmt = IRMutator::visit(op);
        }

        if (in_gpu_loop && !old_in_gpu_loop) {
            // This was the outermost GPU loop. Dump any lifted lets here.
            while (!lifted_lets.empty()) {
                stmt = LetStmt::make(lifted_lets.back().first,
                                     lifted_lets.back().second,
                                     stmt);
                lifted_lets.pop_back();
            }
        }

        in_gpu_loop = old_in_gpu_loop;
        return stmt;
    }

    Stmt visit(const LetStmt *op) override {
        if (!in_gpu_loop) {
            return IRMutator::visit(op);
        }

        if (!expr_uses_vars(op->value, gpu_vars) && !expr_contains_load(op->value)) {
            // This let value doesn't depend in the gpu vars. We
            // should lift it outermost. Note that this might expand
            // its scope to encompass other uses of the same name, so
            // we'd better give it a new name.
            string new_name = unique_name('t');
            Expr new_var = Variable::make(op->value.type(), new_name);
            lifted_lets.emplace_back(new_name, op->value);
            return mutate(substitute(op->name, new_var, op->body));
        }

        gpu_vars.push(op->name);

        if (in_thread_loop) {
            return IRMutator::visit(op);
        }

        Stmt body = mutate(op->body);
        const For *f = body.as<For>();
        const Allocate *a = body.as<Allocate>();
        // Move lets in-between gpu loop levels inwards.
        if (f && in_gpu_loop && !in_thread_loop) {
            internal_assert(!expr_uses_var(f->min, op->name) &&
                            !expr_uses_var(f->extent, op->name));
            Stmt inner = LetStmt::make(op->name, op->value, f->body);
            inner = For::make(f->name, f->min, f->extent, f->for_type, f->device_api, inner);
            return mutate(inner);
        } else if (a && in_gpu_loop && !in_thread_loop) {
            internal_assert(a->extents.size() == 1);
            if (expr_uses_var(a->extents[0], op->name)) {
                // This var depends on the block index, and is used to
                // define the size of shared memory. Can't move it
                // inwards or outwards. Codegen will have to deal with
                // it when it deduces how much shared or warp-level
                // memory to allocate.
                return IRMutator::visit(op);
            } else {
                Stmt inner = LetStmt::make(op->name, op->value, a->body);
                inner = Allocate::make(a->name, a->type, a->memory_type, a->extents, a->condition, inner);
                return mutate(inner);
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const IfThenElse *op) override {
        if (!in_gpu_loop || in_thread_loop) {
            return IRMutator::visit(op);
        }

        internal_assert(op->else_case.defined())
            << "PartitionLoops should only introduce if statements with an else branch\n";

        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        if (equal(then_case, else_case)) {
            // This can happen if the only difference between the
            // cases was a let statement that we pulled out of the if.
            return then_case;
        }

        const Allocate *allocate_a = then_case.as<Allocate>();
        const Allocate *allocate_b = else_case.as<Allocate>();
        const For *for_a = then_case.as<For>();
        const For *for_b = else_case.as<For>();
        const LetStmt *let_a = then_case.as<LetStmt>();
        const LetStmt *let_b = else_case.as<LetStmt>();
        if (allocate_a && allocate_b) {
            Stmt inner = IfThenElse::make(op->condition, allocate_a->body, allocate_b->body);
            inner = Allocate::make(allocate_a->name, allocate_a->type,
                                   allocate_a->memory_type, allocate_a->extents,
                                   allocate_a->condition, inner);
            return mutate(inner);
        } else if (let_a && let_b && let_a->name == let_b->name) {
            string condition_name = unique_name('t');
            Expr condition = Variable::make(op->condition.type(), condition_name);
            Stmt inner = IfThenElse::make(condition, let_a->body, let_b->body);
            inner = LetStmt::make(let_a->name, select(condition, let_a->value, let_b->value), inner);
            inner = LetStmt::make(condition_name, op->condition, inner);
            return mutate(inner);
        } else if (let_a) {
            string new_name = unique_name(let_a->name);
            Stmt inner = let_a->body;
            inner = substitute(let_a->name, Variable::make(let_a->value.type(), new_name), inner);
            inner = IfThenElse::make(op->condition, inner, else_case);
            inner = LetStmt::make(new_name, let_a->value, inner);
            return mutate(inner);
        } else if (let_b) {
            string new_name = unique_name(let_b->name);
            Stmt inner = let_b->body;
            inner = substitute(let_b->name, Variable::make(let_b->value.type(), new_name), inner);
            inner = IfThenElse::make(op->condition, then_case, inner);
            inner = LetStmt::make(new_name, let_b->value, inner);
            return mutate(inner);
        } else if (for_a && for_b &&
                   for_a->name == for_b->name &&
                   for_a->min.same_as(for_b->min) &&
                   for_a->extent.same_as(for_b->extent)) {
            Stmt inner = IfThenElse::make(op->condition, for_a->body, for_b->body);
            inner = For::make(for_a->name, for_a->min, for_a->extent, for_a->for_type, for_a->device_api, inner);
            return mutate(inner);
        } else {
            internal_error << "Unexpected construct inside if statement: " << Stmt(op) << "\n";
            return Stmt();
        }
    }
};

// Expand selects of boolean conditions so that the partitioner can
// consider them one-at-a-time.
class ExpandSelects : public IRMutator {
    using IRMutator::visit;

    bool is_trivial(const Expr &e) {
        return e.as<Variable>() || is_const(e);
    }

    Expr visit(const Select *op) override {
        Expr condition = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        if (const Or *o = condition.as<Or>()) {
            if (is_trivial(true_value)) {
                return mutate(Select::make(o->a, true_value, Select::make(o->b, true_value, false_value)));
            } else {
                string var_name = unique_name('t');
                Expr var = Variable::make(true_value.type(), var_name);
                Expr expr = mutate(Select::make(o->a, var, Select::make(o->b, var, false_value)));
                return Let::make(var_name, true_value, expr);
            }
        } else if (const And *a = condition.as<And>()) {
            if (is_trivial(false_value)) {
                return mutate(Select::make(a->a, Select::make(a->b, true_value, false_value), false_value));
            } else {
                string var_name = unique_name('t');
                Expr var = Variable::make(false_value.type(), var_name);
                Expr expr = mutate(Select::make(a->a, Select::make(a->b, true_value, var), var));
                return Let::make(var_name, false_value, expr);
            }
        } else if (const Not *n = condition.as<Not>()) {
            return mutate(Select::make(n->a, false_value, true_value));
        } else if (condition.same_as(op->condition) &&
                   true_value.same_as(op->true_value) &&
                   false_value.same_as(op->false_value)) {
            return op;
        } else {
            return Select::make(condition, true_value, false_value);
        }
    }
};

// Collapse selects back together
class CollapseSelects : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Select *op) override {
        const Select *t = op->true_value.as<Select>();
        const Select *f = op->false_value.as<Select>();

        if (t && equal(t->false_value, op->false_value)) {
            // select(a, select(b, t, f), f) -> select(a && b, t, f)
            return mutate(select(op->condition && t->condition, t->true_value, op->false_value));
        } else if (f && equal(op->true_value, f->true_value)) {
            // select(a, t, select(b, t, f)) -> select(a || b, t, f)
            return mutate(select(op->condition || f->condition, op->true_value, f->false_value));
        } else {
            return IRMutator::visit(op);
        }
    }
};

class ContainsHotLoop : public IRVisitor {
    using IRVisitor::visit;
    void visit(const For *op) override {
        result = true;
    }

    void visit(const IfThenElse *op) override {
        op->then_case.accept(this);

        // Don't count loops that appear in cold paths
        const Call *c = op->condition.as<Call>();
        bool else_case_is_cold =
            (c &&
             (c->is_intrinsic(Call::likely_if_innermost) ||
              c->is_intrinsic(Call::likely)));
        if (op->else_case.defined() &&
            !else_case_is_cold) {
            op->else_case.accept(this);
        }
    }

public:
    bool result = false;
};

class LowerLikelyIfInnermost : public IRMutator {
    using IRMutator::visit;

    bool inside_innermost_loop = false;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::likely_if_innermost)) {
            internal_assert(op->args.size() == 1);
            if (inside_innermost_loop) {
                return Call::make(op->type, Call::likely, {mutate(op->args[0])}, Call::PureIntrinsic);
            } else {
                return mutate(op->args[0]);
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const For *op) override {
        ContainsHotLoop c;
        op->body.accept(&c);
        inside_innermost_loop = !c.result;
        Stmt stmt = IRMutator::visit(op);
        inside_innermost_loop = false;
        return stmt;
    }
};

}  // namespace

bool has_uncaptured_likely_tag(const Expr &e) {
    HasUncapturedLikelyTag h;
    e.accept(&h);
    return h.result;
}

bool has_likely_tag(const Expr &e) {
    HasLikelyTag h;
    e.accept(&h);
    return h.result;
}

Stmt partition_loops(Stmt s) {
    s = LowerLikelyIfInnermost().mutate(s);

    // Walk inwards to the first loop before doing any more work.
    class Mutator : public IRMutator {
        using IRMutator::visit;
        Stmt visit(const For *op) override {
            Stmt s = op;
            s = MarkClampedRampsAsLikely().mutate(s);
            s = ExpandSelects().mutate(s);
            s = PartitionLoops().mutate(s);
            s = RenormalizeGPULoops().mutate(s);
            s = CollapseSelects().mutate(s);
            return s;
        }
    } mutator;
    s = mutator.mutate(s);

    s = remove_likelies(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
