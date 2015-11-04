#include <algorithm>

#include "PartitionLoops.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Solve.h"
#include "IREquality.h"
#include "ExprUsesVar.h"
#include "Substitute.h"
#include "CodeGen_GPU_Dev.h"
#include "Var.h"
#include "CSE.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::pair;
using std::make_pair;
using std::map;

namespace {

// Loop partitioning only applies to things marked as 'likely'. Loads
// through hand-written boundary conditions will produce clamped
// ramps, which will turn into gathers. This pass injects likely
// intrinsics so that these clamped ramps are picked up by loop
// partitioning.
class MarkClampedRampsAsLikely : public IRMutator {
    using IRMutator::visit;
    void visit(const Min *op) {
        if (in_index && op->a.as<Ramp>()) {
            // No point recursing into the ramp - it can't contain
            // another ramp.
            expr = min(likely(op->a), mutate(op->b));
        } else if (in_index && op->b.as<Ramp>()) {
            expr = min(mutate(op->a), likely(op->b));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Max *op) {
        if (in_index && op->a.as<Ramp>()) {
            expr = max(likely(op->a), mutate(op->b));
        } else if (in_index && op->b.as<Ramp>()) {
            expr = max(mutate(op->a), likely(op->b));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Load *op) {
        bool old_in_index = in_index;
        in_index = true;
        IRMutator::visit(op);
        in_index = old_in_index;
    }

    void visit(const Store *op) {
        bool old_in_index = in_index;
        in_index = true;
        Expr index = mutate(op->index);
        in_index = old_in_index;
        Expr value = mutate(op->value);
        if (index.same_as(op->index) && value.same_as(op->value)) {
            stmt = op;
        } else {
            stmt = Store::make(op->name, value, index);
        }
    }

    bool in_index = false;
};

// Remove any 'likely' intrinsics.
class RemoveLikelyTags : public IRMutator {
    using IRMutator::visit;

    void visit(const Call *op) {
        if (op->name == Call::likely && op->call_type == Call::Intrinsic) {
            internal_assert(op->args.size() == 1);
            expr = mutate(op->args[0]);
        } else {
            IRMutator::visit(op);
        }
    }
};

class HasLikelyTag : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Call *op) {
        if (op->name == Call::likely &&
            op->call_type == Call::Intrinsic) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }
public:
    bool result = false;
};

bool has_likely_tag(Expr e) {
    HasLikelyTag h;
    e.accept(&h);
    return h.result;
}

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

class FindSimplifications : public IRVisitor {
    using IRVisitor::visit;

public:
    vector<Simplification> simplifications;

private:
    void new_simplification(Expr condition, Expr old, Expr likely_val, Expr unlikely_val) {
        condition = RemoveLikelyTags().mutate(condition);
        Simplification s = {condition, old, likely_val, unlikely_val, true};
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

    void visit(const Min *op) {
        IRVisitor::visit(op);
        bool likely_a = has_likely_tag(op->a);
        bool likely_b = has_likely_tag(op->b);

        if (likely_b && !likely_a) {
            new_simplification(op->b <= op->a, op, op->b, op->a);
        } else if (likely_a && !likely_b) {
            new_simplification(op->a <= op->b, op, op->a, op->b);
        }
    }

    void visit(const Max *op) {
        IRVisitor::visit(op);
        bool likely_a = has_likely_tag(op->a);
        bool likely_b = has_likely_tag(op->b);

        if (likely_b && !likely_a) {
            new_simplification(op->b >= op->a, op, op->b, op->a);
        } else if (likely_a && !likely_b) {
            new_simplification(op->a >= op->b, op, op->a, op->b);
        }
    }

    void visit(const Select *op) {
        IRVisitor::visit(op);
        bool likely_t = has_likely_tag(op->true_value);
        bool likely_f = has_likely_tag(op->false_value);

        if (likely_t && !likely_f) {
            new_simplification(op->condition, op, op->true_value, op->false_value);
        } else if (likely_f && !likely_t) {
            new_simplification(!op->condition, op, op->false_value, op->true_value);
        }
    }

    void visit(const For *op) {
        vector<Simplification> old;
        old.swap(simplifications);
        IRVisitor::visit(op);

        // Relax all the new conditions using the loop bounds
        for (Simplification &s : simplifications) {
            if (expr_uses_var(s.condition, op->name)) {
                Scope<Interval> varying;
                varying.push(op->name, Interval(op->min, op->min + op->extent - 1));
                Expr relaxed = and_condition_over_domain(s.condition, varying);
                if (!equal(relaxed, s.condition)) {
                    s.tight = false;
                }
                s.condition = relaxed;
            }
        }

        simplifications.insert(simplifications.end(), old.begin(), old.end());
    }

    template<typename LetOrLetStmt>
    void visit_let(const LetOrLetStmt *op) {
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

    void visit(const LetStmt *op) {
        visit_let(op);
    }

    void visit(const Let *op) {
        visit_let(op);
    }
};

class MakeSimplifications : public IRMutator {
    using IRMutator::visit;

    const vector<Simplification> &simplifications;

public:

    MakeSimplifications(const vector<Simplification> &s) : simplifications(s) {}

    using IRMutator::mutate;
    Expr mutate(Expr e) {
        for (auto const &s : simplifications) {
            if (e.same_as(s.old_expr)) {
                return mutate(s.likely_value);
            }
        }
        return IRMutator::mutate(e);
    }

};

class PartitionLoops : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {
        Stmt body = op->body;

        FindSimplifications finder;
        body.accept(&finder);

        debug(3) << "\n\n**** Partitioning loop over " << op->name << "\n";

        vector<Expr> min_vals, max_vals;
        vector<Simplification> middle_simps, prologue_simps, epilogue_simps;
        bool lower_bound_is_tight = true, upper_bound_is_tight = true;
        for (auto &s : finder.simplifications) {
            s.interval = solve_for_inner_interval(s.condition, op->name);
            if (s.tight) {
                Interval outer = solve_for_outer_interval(s.condition, op->name);
                s.tight &= equal(outer.min, s.interval.min) && equal(outer.max, s.interval.max);
            }

            debug(3) << "\nSimplification: \n"
                     << "  condition: " << s.condition << "\n"
                     << "  old: " << s.old_expr << "\n"
                     << "  new: " << s.likely_value << "\n"
                     << "  min: " << s.interval.min << "\n"
                     << "  max: " << s.interval.max << "\n";

            // Accept all non-empty intervals
            if (!interval_is_empty(s.interval)) {
                if (interval_has_lower_bound(s.interval)) {
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
                if (interval_has_upper_bound(s.interval)) {
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
        for (Expr min_val : min_vals) {
            for (Expr max_val : max_vals) {
                Expr test = simplify(common_subexpression_elimination(min_val - 1 < max_val + 1));
                if (!is_one(test)) {
                    can_simplify_prologue = false;
                }
            }
        }

        // Find simplifications we can apply to the prologue and epilogue.
        for (auto const &s : middle_simps) {
            // If it goes down to minus infinity, we can also
            // apply it to the prologue
            if (can_simplify_prologue &&
                !interval_has_lower_bound(s.interval)) {
                prologue_simps.push_back(s);
            }

            // If it goes up to positive infinity, we can also
            // apply it to the epilogue
            if (!interval_has_upper_bound(s.interval)) {
                epilogue_simps.push_back(s);
            }

            // If our simplifications only contain one lower bound, and
            // it's tight, then the reverse rule can be applied to the
            // prologue.
            if (can_simplify_prologue &&
                interval_has_lower_bound(s.interval) &&
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
            if (interval_has_upper_bound(s.interval) &&
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
        string prologue_name = unique_name(op->name + ".prologue", false);
        string epilogue_name = unique_name(op->name + ".epilogue", false);

        if (make_prologue) {
            // They'll simplify better if you put them in lexicographic order
            std::sort(min_vals.begin(), min_vals.end(), IRDeepCompare());
            min_vals.push_back(op->min);
            prologue_val = std::accumulate(min_vals.begin() + 1, min_vals.end(), min_vals[0], Max::make);
            min_steady = Variable::make(Int(32), prologue_name);

            internal_assert(!expr_uses_var(prologue_val, op->name));
        }
        if (make_epilogue) {
            std::sort(max_vals.begin(), max_vals.end(), IRDeepCompare());
            max_vals.push_back(op->min + op->extent - 1);
            epilogue_val = std::accumulate(max_vals.begin() + 1, max_vals.end(), max_vals[0], Min::make) + 1;
            if (make_prologue) {
                epilogue_val = max(epilogue_val, prologue_val);
            }
            max_steady = Variable::make(Int(32), epilogue_name);

            internal_assert(!expr_uses_var(epilogue_val, op->name));
        }

        if (op->for_type == ForType::Serial) {
            stmt = For::make(op->name, min_steady, max_steady - min_steady,
                             op->for_type, op->device_api, simpler_body);

            if (make_prologue) {
                prologue = For::make(op->name, op->min, min_steady - op->min,
                                     op->for_type, op->device_api, prologue);
                //prologue = Block::make(Evaluate::make(print(op->name, " prologue")), prologue);
                stmt = Block::make(prologue, stmt);
            }
            if (make_epilogue) {
                epilogue = For::make(op->name, max_steady, op->min + op->extent - max_steady,
                                     op->for_type, op->device_api, epilogue);
                //epilogue = Block::make(Evaluate::make(print(op->name, " epilogue")), epilogue);
                stmt = Block::make(stmt, epilogue);
            }
        } else {
            Expr loop_var = Variable::make(Int(32), op->name);
            stmt = simpler_body;
            if (make_epilogue && make_prologue && equal(prologue, epilogue)) {
                stmt = IfThenElse::make(min_steady < loop_var && loop_var < max_steady, stmt, prologue);
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
            //epilogue_val = print(epilogue_val, op->name, "epilogue");
            stmt = LetStmt::make(epilogue_name, epilogue_val, stmt);
        }
        if (make_prologue) {
            //prologue_val = print(prologue_val, op->name, "prologue");
            stmt = LetStmt::make(prologue_name, prologue_val, stmt);
        }
    }
};

// The loop partitioning logic can introduce if and let statements in
// between GPU loop levels. This pass moves them inwards or outwards.
class RenormalizeGPULoops : public IRMutator {
    bool in_gpu_loop = false, in_thread_loop = false;

    using IRMutator::visit;

    // Track all vars that depend on GPU loop indices
    Scope<int> gpu_vars;

    vector<pair<string, Expr> > lifted_lets;

    void visit(const For *op) {
        if (ends_with(op->name, Var::gpu_threads().name())) {
            in_thread_loop = true;
            IRMutator::visit(op);
            in_thread_loop = false;
            return;
        }

        // TODO: if the bounds of the loop depend on an outer gpu loop, expand them and use an if

        bool old_in_gpu_loop = in_gpu_loop;

        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            gpu_vars.push(op->name, 0);
            in_gpu_loop = true;
        }

        IRMutator::visit(op);

        if (in_gpu_loop && !old_in_gpu_loop) {
            // This was the outermost GPU loop. Dump any lifted lets here.
            while (lifted_lets.size()) {
                stmt = LetStmt::make(lifted_lets.back().first,
                                     lifted_lets.back().second,
                                     stmt);
                lifted_lets.pop_back();
            }
        }

        in_gpu_loop = old_in_gpu_loop;


    }

    void visit(const LetStmt *op) {
        if (!in_gpu_loop) {
            IRMutator::visit(op);
            return;
        }

        if (!expr_uses_vars(op->value, gpu_vars)) {
            // This let value doesn't depend in the gpu vars. We
            // should lift it outermost. Note that this might expand
            // its scope to encompass other uses of the same name, so
            // we'd better give it a new name.
            string new_name = unique_name('t');
            Expr new_var = Variable::make(op->value.type(), new_name);
            lifted_lets.push_back(make_pair(new_name, op->value));
            stmt = mutate(substitute(op->name, new_var, op->body));
            return;
        }

        if (in_thread_loop) {
            IRMutator::visit(op);
            return;
        }

        gpu_vars.push(op->name, 0);

        Stmt body = mutate(op->body);
        const For *f = body.as<For>();
        const Allocate *a = body.as<Allocate>();
        // Move lets in-between gpu loop levels inwards.
        if (f && in_gpu_loop && !in_thread_loop) {
            internal_assert(!expr_uses_var(f->min, op->name) &&
                            !expr_uses_var(f->extent, op->name));
            Stmt inner = LetStmt::make(op->name, op->value, f->body);
            inner = For::make(f->name, f->min, f->extent, f->for_type, f->device_api, inner);
            stmt = mutate(inner);
        } else if (a && in_gpu_loop && !in_thread_loop) {
            internal_assert(a->name == "__shared");
            Stmt inner = LetStmt::make(op->name, op->value, a->body);
            inner = Allocate::make(a->name, a->type, a->extents, a->condition, inner);
            stmt = mutate(inner);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const IfThenElse *op) {
        if (!in_gpu_loop || in_thread_loop) {
            IRMutator::visit(op);
            return;
        }

        internal_assert(op->else_case.defined())
            << "PartitionLoops should only introduce if statements with an else branch\n";

        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        if (equal(then_case, else_case)) {
            // This can happen if the only difference between the
            // cases was a let statement that we pulled out of the if.
            stmt = then_case;
            return;
        }

        const Allocate *allocate_a = then_case.as<Allocate>();
        const Allocate *allocate_b = else_case.as<Allocate>();
        const For *for_a = then_case.as<For>();
        const For *for_b = else_case.as<For>();
        const LetStmt *let_a = then_case.as<LetStmt>();
        const LetStmt *let_b = else_case.as<LetStmt>();
        if (allocate_a && allocate_b &&
            allocate_a->name == "__shared" &&
            allocate_b->name == "__shared") {
            Stmt inner = IfThenElse::make(op->condition, allocate_a->body, allocate_b->body);
            inner = Allocate::make(allocate_a->name, allocate_a->type, allocate_a->extents, allocate_a->condition, inner);
            stmt = mutate(inner);
        } else if (let_a && let_b && let_a->name == let_b->name) {
            string condition_name = unique_name('t');
            Expr condition = Variable::make(op->condition.type(), condition_name);
            Stmt inner = IfThenElse::make(condition, let_a->body, let_b->body);
            inner = LetStmt::make(let_a->name, select(condition, let_a->value, let_b->value), inner);
            inner = LetStmt::make(condition_name, op->condition, inner);
            stmt = mutate(inner);
        } else if (let_a) {
            string new_name = unique_name(let_a->name, false);
            Stmt inner = let_a->body;
            inner = substitute(let_a->name, Variable::make(let_a->value.type(), new_name), inner);
            inner = IfThenElse::make(op->condition, inner, else_case);
            inner = LetStmt::make(new_name, let_a->value, inner);
            stmt = mutate(inner);
        } else if (let_b) {
            string new_name = unique_name(let_b->name, false);
            Stmt inner = let_b->body;
            inner = substitute(let_b->name, Variable::make(let_b->value.type(), new_name), inner);
            inner = IfThenElse::make(op->condition, then_case, inner);
            inner = LetStmt::make(new_name, let_b->value, inner);
            stmt = mutate(inner);
        } else if (for_a && for_b &&
                   for_a->name == for_b->name &&
                   for_a->min.same_as(for_b->min) &&
                   for_a->extent.same_as(for_b->extent)) {
            Stmt inner = IfThenElse::make(op->condition, for_a->body, for_b->body);
            inner = For::make(for_a->name, for_a->min, for_a->extent, for_a->for_type, for_a->device_api, inner);
            stmt = mutate(inner);
        } else {
            internal_error << "Unexpected construct inside if statement: " << Stmt(op) << "\n";
        }

    }

};


// Expand selects of boolean conditions so that the partitioner can
// consider them one-at-a-time.
class ExpandSelects : public IRMutator {
    using IRMutator::visit;

    bool is_trivial(Expr e) {
        return e.as<Variable>() || is_const(e);
    }

    void visit(const Select *op) {
        Expr condition   = mutate(op->condition);
        Expr true_value  = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        if (const Or *o = condition.as<Or>()) {
            if (is_trivial(true_value)) {
                expr = mutate(Select::make(o->a, true_value, Select::make(o->b, true_value, false_value)));
            } else {
                string var_name = unique_name('t');
                Expr var = Variable::make(true_value.type(), var_name);
                expr = mutate(Select::make(o->a, var, Select::make(o->b, var, false_value)));
                expr = Let::make(var_name, true_value, expr);
            }
        } else if (const And *a = condition.as<And>()) {
            if (is_trivial(false_value)) {
                expr = mutate(Select::make(a->a, Select::make(a->b, true_value, false_value), false_value));
            } else {
                string var_name = unique_name('t');
                Expr var = Variable::make(false_value.type(), var_name);
                expr = mutate(Select::make(a->a, Select::make(a->b, true_value, var), var));
                expr = Let::make(var_name, false_value, expr);
            }
        } else if (const Not *n = condition.as<Not>()) {
            expr = mutate(Select::make(n->a, false_value, true_value));
        } else if (condition.same_as(op->condition) &&
                   true_value.same_as(op->true_value) &&
                   false_value.same_as(op->false_value)) {
            expr = op;
        } else {
            expr = Select::make(condition, true_value, false_value);
        }
    }
};

// Collapse selects back together
class CollapseSelects : public IRMutator {
    using IRMutator::visit;

    void visit(const Select *op) {
        const Select *t = op->true_value.as<Select>();
        const Select *f = op->false_value.as<Select>();

        if (t && equal(t->false_value, op->false_value)) {
            // select(a, select(b, t, f), f) -> select(a && b, t, f)
            expr = mutate(select(op->condition && t->condition, t->true_value, op->false_value));
        } else if (f && equal(op->true_value, f->true_value)) {
            // select(a, t, select(b, t, f)) -> select(a || b, t, f)
            expr = mutate(select(op->condition || f->condition, op->true_value, f->false_value));
        } else {
            IRMutator::visit(op);
        }
    }
};

}

Stmt partition_loops(Stmt s) {
    s = MarkClampedRampsAsLikely().mutate(s);
    s = ExpandSelects().mutate(s);
    s = PartitionLoops().mutate(s);
    s = RenormalizeGPULoops().mutate(s);
    s = RemoveLikelyTags().mutate(s);
    s = CollapseSelects().mutate(s);
    return s;
}

}
}
