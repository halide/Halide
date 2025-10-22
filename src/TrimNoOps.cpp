#include <algorithm>

#include "CSE.h"
#include "CodeGen_GPU_Dev.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include "TrimNoOps.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

/** Remove identity functions, even if they have side-effects. */
class StripIdentities : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (Call::as_tag(op) || op->is_intrinsic(Call::return_second)) {
            return mutate(op->args.back());
        } else {
            return IRMutator::visit(op);
        }
    }
};

/** Check if an Expr loads from the given buffer. */
class LoadsFromBuffer : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Load *op) override {
        if (op->name == buffer) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

    string buffer;

public:
    bool result = false;
    LoadsFromBuffer(const string &b)
        : buffer(b) {
    }
};

bool loads_from_buffer(const Expr &e, const string &buf) {
    LoadsFromBuffer l(buf);
    e.accept(&l);
    return l.result;
}

/** Construct a sufficient condition for the visited stmt to be a no-op. */
class IsNoOp : public IRVisitor {
    using IRVisitor::visit;

    Expr make_and(Expr a, Expr b) {
        if (is_const_zero(a) || is_const_one(b)) {
            return a;
        }
        if (is_const_zero(b) || is_const_one(a)) {
            return b;
        }
        return a && b;
    }

    Expr make_or(Expr a, Expr b) {
        if (is_const_zero(a) || is_const_one(b)) {
            return b;
        }
        if (is_const_zero(b) || is_const_one(a)) {
            return a;
        }
        return a || b;
    }

    void visit(const Store *op) override {
        if (op->value.type().is_handle() || is_const_zero(op->predicate)) {
            condition = const_false();
        } else {
            if (is_const_zero(condition)) {
                return;
            }
            // If the value being stored is the same as the value loaded,
            // this is a no-op
            debug(3) << "Considering store: " << Stmt(op) << "\n";

            // Early-out: There's no way for that to be true if the
            // RHS does not load from the buffer being stored to.
            if (!loads_from_buffer(op->value, op->name)) {
                condition = const_false();
                return;
            }

            Expr equivalent_load = Load::make(op->value.type(), op->name, op->index,
                                              Buffer<>(), Parameter(), op->predicate, op->alignment);
            Expr is_no_op = equivalent_load == op->value;
            is_no_op = StripIdentities().mutate(is_no_op);
            // We need to call CSE since sometimes we have "let" stmt on the RHS
            // that makes the expr harder to solve, i.e. the solver will just give up
            // and return a conservative false on call to and_condition_over_domain().
            is_no_op = simplify(common_subexpression_elimination(is_no_op));
            debug(3) << "Anding condition over domain... " << is_no_op << "\n";
            is_no_op = and_condition_over_domain(is_no_op, Scope<Interval>::empty_scope());
            condition = make_and(condition, is_no_op);
            debug(3) << "Condition is now " << condition << "\n";
        }
    }

    void visit(const For *op) override {
        if (is_const_zero(condition)) {
            return;
        }
        Expr old_condition = condition;
        condition = const_true();
        op->body.accept(this);
        Scope<Interval> varying;
        varying.push(op->name, Interval(op->min, op->min + op->extent - 1));
        condition = simplify(common_subexpression_elimination(condition));
        debug(3) << "About to relax over " << op->name << " : " << condition << "\n";
        condition = and_condition_over_domain(condition, varying);
        debug(3) << "Relaxed: " << condition << "\n";
        condition = make_and(old_condition, make_or(condition, simplify(op->extent <= 0)));
    }

    void visit(const IfThenElse *op) override {
        if (is_const_zero(condition)) {
            return;
        }
        Expr total_condition = condition;
        condition = const_true();
        op->then_case.accept(this);
        // This is a no-op if we're previously a no-op, and the
        // condition is false or the if body is a no-op.
        total_condition = make_and(total_condition, make_or(!op->condition, condition));
        condition = const_true();
        if (op->else_case.defined()) {
            op->else_case.accept(this);
            total_condition = make_and(total_condition, make_or(op->condition, condition));
        }
        condition = total_condition;
    }

    void visit(const Call *op) override {
        // If the loop calls an impure function, we can't remove the
        // call to it. Most notably: image_store.
        if (!op->is_pure()) {
            condition = const_false();
            return;
        }
        IRVisitor::visit(op);
    }

    void visit(const Acquire *op) override {
        condition = const_false();
    }

    template<typename LetOrLetStmt>
    void visit_let(const LetOrLetStmt *op) {
        IRVisitor::visit(op);
        if (expr_uses_var(condition, op->name)) {
            condition = Let::make(op->name, op->value, condition);
        }
    }

    void visit(const LetStmt *op) override {
        visit_let(op);
    }

    void visit(const Let *op) override {
        visit_let(op);
    }

public:
    Expr condition = const_true();
};

class SimplifyUsingBounds : public IRMutator {
    struct ContainingLoop {
        string var;
        Interval i;
    };
    vector<ContainingLoop> containing_loops;

    using IRMutator::visit;

    // Can we prove a condition over the non-rectangular domain of the for loops we're in?
    bool provably_true_over_domain(Expr test) {
        debug(3) << "Attempting to prove: " << test << "\n";
        for (const auto &[var, interval] : reverse_view(containing_loops)) {
            // Because the domain is potentially non-rectangular, we
            // need to take each variable one-by-one, simplifying in
            // between to allow for cancellations of the bounds of
            // inner loops with outer loop variables.
            if (is_const(test)) {
                break;
            } else if (!expr_uses_var(test, var)) {
                continue;
            } else if (interval.is_bounded() &&
                       can_prove(interval.min == interval.max) &&
                       expr_uses_var(test, var)) {
                // If min == max then either the domain only has one correct value, which we
                // can substitute directly.
                // Need to call CSE here since simplify() is sometimes unable to simplify expr with
                // non-trivial 'let' value, e.g. (let x = min(10, y-1) in (x < y))
                test = common_subexpression_elimination(Let::make(var, interval.min, test));
            } else if (interval.is_bounded() &&
                       can_prove(interval.min >= interval.max) &&
                       expr_uses_var(test, var)) {
                // If min >= max then either the domain only has one correct value,
                // or the domain is empty, which implies both min/max are true under
                // the domain.
                // Need to call CSE here since simplify() is sometimes unable to simplify expr with
                // non-trivial 'let' value, e.g. (let x = 10 in x < y) || (let x = min(10, y-1) in (x < y))
                test = common_subexpression_elimination(Let::make(var, interval.min, test) ||
                                                        Let::make(var, interval.max, test));
            } else {
                Scope<Interval> s;
                // Rearrange the expression if possible so that the
                // loop var only occurs once.
                SolverResult solved = solve_expression(test, var);
                if (solved.fully_solved) {
                    test = solved.result;
                }
                s.push(var, interval);
                test = and_condition_over_domain(test, s);
            }
            test = simplify(test);
            debug(3) << " -> " << test << "\n";
        }
        return is_const_one(test);
    }

    Expr visit(const Min *op) override {
        if (!op->type.is_int() || op->type.bits() < 32) {
            return IRMutator::visit(op);
        } else {
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);
            Expr test = a <= b;
            if (provably_true_over_domain(a <= b)) {
                return a;
            } else if (provably_true_over_domain(b <= a)) {
                return b;
            } else {
                return Min::make(a, b);
            }
        }
    }

    Expr visit(const Max *op) override {
        if (!op->type.is_int() || op->type.bits() < 32) {
            return IRMutator::visit(op);
        } else {
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);
            if (provably_true_over_domain(a >= b)) {
                return a;
            } else if (provably_true_over_domain(b >= a)) {
                return b;
            } else {
                return Max::make(a, b);
            }
        }
    }

    template<typename Cmp>
    Expr visit_cmp(const Cmp *op) {
        Expr expr = IRMutator::visit(op);
        if (provably_true_over_domain(expr)) {
            expr = make_one(op->type);
        } else if (provably_true_over_domain(!expr)) {
            expr = make_zero(op->type);
        }
        return expr;
    }

    Expr visit(const LE *op) override {
        return visit_cmp(op);
    }

    Expr visit(const LT *op) override {
        return visit_cmp(op);
    }

    Expr visit(const GE *op) override {
        return visit_cmp(op);
    }

    Expr visit(const GT *op) override {
        return visit_cmp(op);
    }

    Expr visit(const EQ *op) override {
        return visit_cmp(op);
    }

    Expr visit(const NE *op) override {
        return visit_cmp(op);
    }

    template<typename LetStmtOrLet>
    auto visit_let(const LetStmtOrLet *op) -> decltype(op->body) {
        Expr value = mutate(op->value);
        decltype(op->body) body;
        if (value.type() == Int(32) && is_pure(value)) {
            containing_loops.push_back({op->name, {value, value}});
            body = mutate(op->body);
            containing_loops.pop_back();
        } else {
            body = mutate(op->body);
        }
        return LetStmtOrLet::make(op->name, value, body);
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Stmt visit(const For *op) override {
        // Simplify the loop bounds.
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        containing_loops.push_back({op->name, {min, min + extent - 1}});
        Stmt body = mutate(op->body);
        containing_loops.pop_back();
        return For::make(op->name, min, extent, op->for_type, op->partition_policy, op->device_api, body);
    }

public:
    SimplifyUsingBounds(const string &v, const Interval &i) {
        containing_loops.push_back({v, i});
    }

    SimplifyUsingBounds() = default;
};

class TrimNoOps : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        // Bounds of GPU loops can't depend on outer gpu loop vars
        if (is_gpu(op->for_type)) {
            debug(3) << "TrimNoOps found gpu loop var: " << op->name << "\n";
            return IRMutator::visit(op);
        }

        Stmt body = mutate(op->body);

        debug(3) << "\n\n ***** Trim no ops in loop over " << op->name << "\n";

        IsNoOp is_no_op;
        body.accept(&is_no_op);
        debug(3) << "Condition is " << is_no_op.condition << "\n";
        is_no_op.condition = simplify(common_subexpression_elimination(is_no_op.condition));

        debug(3) << "Simplified condition is " << is_no_op.condition << "\n";

        if (is_const_one(is_no_op.condition)) {
            // This loop is definitely useless
            debug(3) << "Removed empty loop.\n"
                     << "Old: " << Stmt(op) << "\n";
            return Evaluate::make(0);
        } else if (is_const_zero(is_no_op.condition)) {
            // This loop is definitely needed
            if (body.same_as(op->body)) {
                return op;
            } else {
                return For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api, body);
            }
        }

        // The condition is something interesting. Try to see if we
        // can trim the loop bounds over which the loop does
        // something.
        Interval i = solve_for_outer_interval(!is_no_op.condition, op->name);

        debug(3) << "Interval is: " << i.min << ", " << i.max << "\n";

        if (i.is_everything()) {
            // Nope.
            return For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api, body);
        }

        if (i.is_empty()) {
            // Empty loop
            debug(3) << "Removed empty loop.\n"
                     << "Old: " << Stmt(op) << "\n";
            return Evaluate::make(0);
        }

        // Simplify the body to take advantage of the fact that the
        // loop range is now truncated
        body = simplify(SimplifyUsingBounds(op->name, i).mutate(body));

        string new_min_name = unique_name(op->name + ".new_min");
        string new_max_name = unique_name(op->name + ".new_max");
        string old_max_name = unique_name(op->name + ".old_max");
        Expr new_min_var = Variable::make(Int(32), new_min_name);
        Expr new_max_var = Variable::make(Int(32), new_max_name);
        Expr old_max_var = Variable::make(Int(32), old_max_name);

        // Convert max to max-plus-one
        if (i.has_upper_bound()) {
            i.max = i.max + 1;
        }

        // Truncate the loop bounds to the region over which it's not
        // a no-op.
        Expr old_max = op->min + op->extent;
        Expr new_min, new_max;
        if (i.has_lower_bound()) {
            new_min = clamp(i.min, op->min, old_max_var);
        } else {
            new_min = op->min;
        }
        if (i.has_upper_bound()) {
            new_max = clamp(i.max, new_min_var, old_max_var);
        } else {
            new_max = old_max;
        }

        Expr new_extent = new_max_var - new_min_var;

        Stmt stmt = For::make(op->name, new_min_var, new_extent, op->for_type, op->partition_policy, op->device_api, body);
        stmt = LetStmt::make(new_max_name, new_max, stmt);
        stmt = LetStmt::make(new_min_name, new_min, stmt);
        stmt = LetStmt::make(old_max_name, old_max, stmt);
        stmt = simplify(stmt);

        debug(3) << "Rewrote loop.\n"
                 << "Old: " << Stmt(op) << "\n"
                 << "New: " << stmt << "\n";

        return stmt;
    }
};

}  // namespace

Stmt trim_no_ops(Stmt s) {
    s = TrimNoOps().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
