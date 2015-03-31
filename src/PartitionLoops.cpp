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

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::pair;
using std::make_pair;

namespace {

// Simplify an expression by assuming that certain mins, maxes, and
// select statements always evaluate down one path for the bulk of a
// loop body - the "steady state". Also solves for the bounds of the
// steady state. The likely path is deduced by looking for clamped
// ramps, or the 'likely' intrinsic.
class FindSteadyState : public IRMutator {
public:

    Expr min_steady_val() {
        if (min_vals.empty()) {
            return Expr();
        }

        // Lexicographically sort the vals, to help out the simplifier
        std::sort(min_vals.begin(), min_vals.end(), IRDeepCompare());

        Expr e = min_vals[0];
        for (size_t i = 1; i < min_vals.size(); i++) {
            e = max(e, min_vals[i]);
        }
        return e;
    }

    Expr max_steady_val() {
        if (max_vals.empty()) {
            return Expr();
        }

        std::sort(max_vals.begin(), max_vals.end(), IRDeepCompare());

        Expr e = max_vals[0];
        for (size_t i = 1; i < max_vals.size(); i++) {
            e = min(e, max_vals[i]);
        }
        return e;
    }

    FindSteadyState(const string &l) : loop_var(l), likely(false) {
        free_vars.push(l, 0);
    }

    Stmt simplify_prologue(Stmt s) {
        if (min_vals.size() == 1 &&
            prologue_replacements.size() == 1) {
            // If there is more than one min_val, then the boundary
            // between the prologue and steady state is not tight for
            // the prologue. The steady state starts at the max of
            // multiple min_vals, so is the intersection of multiple
            // conditions, which means the prologue is the union of
            // multiple negated conditions, so any individual one of
            // them might not apply.
            return substitute(prologue_replacements[0].old_expr,
                              prologue_replacements[0].new_expr,
                              s);
        } else {
            return s;
        }
    }

    Stmt simplify_epilogue(Stmt s) {
        if (max_vals.size() == 1 &&
            epilogue_replacements.size() == 1) {
            return substitute(epilogue_replacements[0].old_expr,
                              epilogue_replacements[0].new_expr,
                              s);
        } else {
            return s;
        }
    }

    /** Wrap a statement in any common subexpressions used by the
     * minvals or maxvals. */
    Stmt add_containing_lets(Stmt s) {
        for (size_t i = containing_lets.size(); i > 0; i--) {
            const string &name = containing_lets[i-1].first;
            Expr value = containing_lets[i-1].second;

            // Subexpressions are commonly shared between minval
            // expressions and the maxval expressions.
            for (size_t j = 0; j < i-1; j++) {
                // Just refer to the other let.
                if (equal(containing_lets[j].second, value)) {
                    value = Variable::make(value.type(), containing_lets[j].first);
                    break;
                }
            }

            s = LetStmt::make(name, value, s);
        }
        return s;
    }

private:

    vector<Expr> min_vals, max_vals;

    string loop_var;
    Scope<Expr> bound_vars;
    Scope<int> free_vars;
    Scope<int> inner_loop_vars;

    struct Replacement {
        Expr old_expr, new_expr;
    };
    vector<Replacement> prologue_replacements, epilogue_replacements;

    // A set of let statements for common subexpressions inside the
    // min_vals and max_vals.
    vector<pair<string, Expr> > containing_lets;

    bool likely;

    using IRVisitor::visit;

    // The horizontal maximum of a vector expression. Returns an
    // undefined Expr if it can't be statically determined.
    Expr max_lane(Expr e) {
        if (e.type().is_scalar()) return e;
        if (const Broadcast *b = e.as<Broadcast>()) return b->value;
        if (const Ramp *r = e.as<Ramp>()) {
            if (is_positive_const(r->stride)) {
                return r->base + (r->width-1)*r->stride;
            } else if (is_negative_const(r->stride)) {
                return r->base;
            }
        }
        if (const Variable *v = e.as<Variable>()) {
            if (bound_vars.contains(v->name)) {
                return max_lane(bound_vars.get(v->name));
            }
        }
        if (const Call *c = e.as<Call>()) {
            if (c->name == Call::likely && c->call_type == Call::Intrinsic) {
                return max_lane(c->args[0]);
            }
        }
        return Expr();
    }

    // The horizontal minimum of a vector expression. Returns an
    // undefined Expr if it can't be statically determined.
    Expr min_lane(Expr e) {
        if (e.type().is_scalar()) return e;
        if (const Broadcast *b = e.as<Broadcast>()) return b->value;
        if (const Ramp *r = e.as<Ramp>()) {
            if (is_positive_const(r->stride)) {
                return r->base;
            } else if (is_negative_const(r->stride)) {
                return r->base + (r->width-1)*r->stride;
            }
        }
        if (const Variable *v = e.as<Variable>()) {
            if (bound_vars.contains(v->name)) {
                return min_lane(bound_vars.get(v->name));
            }
        }
        if (const Call *c = e.as<Call>()) {
            if (c->name == Call::likely && c->call_type == Call::Intrinsic) {
                return min_lane(c->args[0]);
            }
        }
        return Expr();
    }

    void visit(const Call *op) {
        IRMutator::visit(op);
        if (op->call_type == Call::Intrinsic && op->name == Call::likely) {
            // We encountered a likely intrinsic, which means this
            // branch of any selects or mins we're in is the
            // steady-state one.
            likely = true;
        }
    }

    bool is_loop_var(Expr e) {
        const Variable *v = e.as<Variable>();
        return v && v->name == loop_var;
    }

    // Make boolean operators with some eager constant folding
    Expr make_and(Expr a, Expr b) {
        if (is_zero(a)) return a;
        if (is_zero(b)) return b;
        if (is_one(a)) return b;
        if (is_one(b)) return a;
        return a && b;
    }

    Expr make_or(Expr a, Expr b) {
        if (is_zero(a)) return b;
        if (is_zero(b)) return a;
        if (is_one(a)) return a;
        if (is_one(b)) return b;
        return a || b;
    }

    Expr make_not(Expr a) {
        if (is_one(a)) return const_false();
        if (is_zero(a)) return const_true();
        return !a;
    }

    // Try to make a condition false by limiting the range of the loop variable.
    Expr simplify_to_false(Expr cond) {
        if (const Broadcast *b = cond.as<Broadcast>()) {
            return simplify_to_false(b->value);
        } else if (const And *a = cond.as<And>()) {
            return make_and(simplify_to_false(a->a), simplify_to_false(a->b));
        } else if (const Or *o = cond.as<Or>()) {
            return make_or(simplify_to_false(o->a), simplify_to_false(o->b));
        } else if (const Not *n = cond.as<Not>()) {
            return make_not(simplify_to_true(n->a));
        } else if (const LT *lt = cond.as<LT>()) {
            return make_not(simplify_to_true(lt->a >= lt->b));
        } else if (const LE *le = cond.as<LE>()) {
            return make_not(simplify_to_true(le->a > le->b));
        } else if (const GT *gt = cond.as<GT>()) {
            return make_not(simplify_to_true(gt->a <= gt->b));
        } else if (const GE *ge = cond.as<GE>()) {
            return make_not(simplify_to_true(ge->a > ge->b));
        } else if (const Variable *v = cond.as<Variable>()) {
            if (bound_vars.contains(v->name)) {
                return simplify_to_false(bound_vars.get(v->name));
            }
        }

        debug(3) << "Failed to apply constraint (1): " << cond << "\n";
        return cond;
    }

    // Try to make a condition true by limiting the range of the loop variable
    Expr simplify_to_true(Expr cond) {
        if (const Broadcast *b = cond.as<Broadcast>()) {
            return simplify_to_true(b->value);
        } else if (const And *a = cond.as<And>()) {
            return make_and(simplify_to_true(a->a), simplify_to_true(a->b));
        } else if (const Or *o = cond.as<Or>()) {
            return make_or(simplify_to_true(o->a), simplify_to_true(o->b));
        } else if (const Not *n = cond.as<Not>()) {
            return make_not(simplify_to_false(n->a));
        } else if (const Variable *v = cond.as<Variable>()) {
            if (bound_vars.contains(v->name)) {
                return simplify_to_true(bound_vars.get(v->name));
            } else {
                return cond;
            }
        }

        // Convert vector conditions to scalar conditions
        if (cond.type().is_vector()) {
            if (const LT *lt = cond.as<LT>()) {
                Expr a = max_lane(lt->a), b = min_lane(lt->b);
                if (a.defined() && b.defined()) {
                    cond = a < b;
                } else {
                    return cond;
                }
            } else if (const LE *le = cond.as<LE>()) {
                Expr a = max_lane(le->a), b = min_lane(le->b);
                if (a.defined() && b.defined()) {
                    cond = a <= b;
                } else {
                    return cond;
                }
            } else if (const GE *ge = cond.as<GE>()) {
                Expr a = min_lane(ge->a), b = max_lane(ge->b);
                if (a.defined() && b.defined()) {
                    cond = a >= b;
                } else {
                    return cond;
                }
            } else if (const GT *gt = cond.as<GT>()) {
                Expr a = min_lane(gt->a), b = max_lane(gt->b);
                if (a.defined() && b.defined()) {
                    cond = a > b;
                } else {
                    return cond;
                }
            } else {
                debug(3) << "Failed to apply constraint (2): " << cond << "\n";
                return cond;
            }
        }

        // Determine the minimum or maximum value of the loop var for
        // which this condition is true, and update min_max and
        // max_val accordingly.

        debug(3) << "Condition: " << cond << "\n";
        Expr solved = solve_expression(cond, loop_var, bound_vars);
        debug(3) << "Solved condition for " <<  loop_var << ": " << solved << "\n";

        // The solve failed.
        if (!solved.defined()) {
            return cond;
        }

        // Conditions in terms of an inner loop var can't be pulled outside
        if (expr_uses_vars(cond, inner_loop_vars)) {
            return cond;
        }

        // Peel off lets.
        vector<pair<string, Expr> > new_lets;
        while (const Let *let = solved.as<Let>()) {
            new_lets.push_back(make_pair(let->name, let->value) );
            solved = let->body;
        }


        bool success = false;
        if (const LT *lt = solved.as<LT>()) {
            if (is_loop_var(lt->a)) {
                max_vals.push_back(lt->b);
                success = true;
            }
        } else if (const LE *le = solved.as<LE>()) {
            if (is_loop_var(le->a)) {
                max_vals.push_back(le->b + 1);
                success = true;
            }
        } else if (const GE *ge = solved.as<GE>()) {
            if (is_loop_var(ge->a)) {
                min_vals.push_back(ge->b);
                success = true;
            }
        } else if (const GT *gt = solved.as<GT>()) {
            if (is_loop_var(gt->a)) {
                min_vals.push_back(gt->b + 1);
                success = true;
            }
        }

        if (success) {
            containing_lets.insert(containing_lets.end(), new_lets.begin(), new_lets.end());
            return const_true();
        } else {
            debug(3) << "Failed to apply constraint (3): " << cond << "\n";
            return cond;
        }
    }

    void visit_min_or_max(Expr op, Expr op_a, Expr op_b, bool is_min) {
        size_t orig_num_min_vals = min_vals.size();
        size_t orig_num_max_vals = max_vals.size();

        bool old_likely = likely;
        likely = false;
        Expr a = mutate(op_a);
        bool a_likely = likely;
        likely = false;
        Expr b = mutate(op_b);
        bool b_likely = likely;

        bool found_simplification_in_children =
            (min_vals.size() != orig_num_min_vals) ||
            (max_vals.size() != orig_num_max_vals);

        // To handle code that doesn't use the boundary condition
        // helpers, but instead just clamps to edge with "clamp", we
        // always consider a ramp inside a min or max to be likely.
        if (op.type().element_of() == Int(32)) {
            if (op_a.as<Ramp>()) {
                a_likely = true;
            }
            if (op_b.as<Ramp>()) {
                b_likely = true;
            }
        }

        likely = old_likely || a_likely || b_likely;

        if (b_likely && !a_likely) {
            std::swap(op_a, op_b);
            std::swap(a, b);
            std::swap(b_likely, a_likely);
        }

        if (a_likely && !b_likely) {
            // If the following condition is true, then we can
            // simplify the min to just the case marked as likely.
            Expr condition = (is_min ? a <= b : b <= a);

            size_t old_num_min_vals = min_vals.size();
            size_t old_num_max_vals = max_vals.size();

            if (is_one(simplify_to_true(condition))) {
                expr = a;

                if (!found_simplification_in_children && condition.type().is_scalar()) {
                    // If there were no inner mutations and we found a
                    // new min_val, then we have a simplification that
                    // we can apply to the prologue. Vector conditions
                    // don't produce tight bounds though, so they're
                    // not suitable for this.
                    Replacement r = {op, op_b};
                    if (min_vals.size() > old_num_min_vals) {
                        prologue_replacements.push_back(r);
                    }
                    if (max_vals.size() > old_num_max_vals) {
                        epilogue_replacements.push_back(r);
                    }
                }
            } else {
                if (is_min) {
                    expr = Min::make(a, b);
                } else {
                    expr = Max::make(a, b);
                }
            }
        } else if (a.same_as(op_a) && b.same_as(op_b)) {
            expr = op;
        } else {
            if (is_min) {
                expr = Min::make(a, b);
            } else {
                expr = Max::make(a, b);
            }
        }
    }


    void visit(const Max *op) {
        visit_min_or_max(op, op->a, op->b, false);
    }

    void visit(const Min *op) {
        visit_min_or_max(op, op->a, op->b, true);
    }

    template<typename SelectOrIf, typename StmtOrExpr>
    StmtOrExpr visit_select_or_if(const SelectOrIf *op,
                                  StmtOrExpr orig_true_value,
                                  StmtOrExpr orig_false_value) {

        size_t orig_num_min_vals = min_vals.size();
        size_t orig_num_max_vals = max_vals.size();

        Expr condition = mutate(op->condition);
        bool old_likely = likely;
        likely = false;
        StmtOrExpr true_value = mutate(orig_true_value);
        bool a_likely = likely;
        likely = false;
        StmtOrExpr false_value = mutate(orig_false_value);
        bool b_likely = likely;
        likely = old_likely || a_likely || b_likely;

        bool found_simplification_in_children =
            (min_vals.size() != orig_num_min_vals) ||
            (max_vals.size() != orig_num_max_vals);

        size_t old_num_min_vals = min_vals.size();
        size_t old_num_max_vals = max_vals.size();

        if (a_likely && !b_likely) {
            // Figure out bounds on the loop var which makes the condition true.
            debug(3) << "Attempting to make this condition true: " << condition << "\n";
            Expr new_condition = simplify_to_true(condition);
            debug(3) << "Attempted to make this condition true: " << condition << " Got: " << new_condition << "\n";
            if (is_one(new_condition)) {
                // We succeeded!
                if (!found_simplification_in_children && condition.type().is_scalar()) {
                    // Vector conditions are not tight bounds, so we
                    // can't simplify the prologue and epilogue using
                    // them.
                    Replacement r = {op->condition, const_false()};
                    if (min_vals.size() > old_num_min_vals) {
                        prologue_replacements.push_back(r);
                    }
                    if (max_vals.size() > old_num_max_vals) {
                        epilogue_replacements.push_back(r);
                    }
                }
                return true_value;
            } else {
                // Might have partially succeeded, so still use the new condition.
                return SelectOrIf::make(new_condition, true_value, false_value);
            }
        } else if (b_likely && !a_likely) {
            debug(3) << "Attempting to make this condition false: " << condition << "\n";
            Expr new_condition = simplify_to_false(condition);
            debug(3) << "Attempted to make this condition false: " << condition << " Got: " << new_condition << "\n";
            if (is_zero(new_condition)) {
                if (!found_simplification_in_children && condition.type().is_scalar()) {
                    Replacement r = {op->condition, const_true()};
                    if (min_vals.size() > old_num_min_vals) {
                        prologue_replacements.push_back(r);
                    }
                    if (max_vals.size() > old_num_max_vals) {
                        epilogue_replacements.push_back(r);
                    }
                }
                return false_value;
            } else {
                return SelectOrIf::make(new_condition, true_value, false_value);
            }
        } else if (condition.same_as(op->condition) &&
                   true_value.same_as(orig_true_value) &&
                   false_value.same_as(orig_false_value)) {
            return op;
        } else {
            return SelectOrIf::make(condition, true_value, false_value);
        }
    }

    void visit(const Select *op) {
        expr = visit_select_or_if(op, op->true_value, op->false_value);
    }

    void visit(const IfThenElse *op) {
        stmt = visit_select_or_if(op, op->then_case, op->else_case);
    }

    void visit(const Let *op) {
        Expr value = mutate(op->value);
        Expr body;

        bound_vars.push(op->name, value);
        body = mutate(op->body);
        bound_vars.pop(op->name);

        if (value.same_as(op->value) && body.same_as(op->body)) {
            expr = op;
        } else {
            expr = Let::make(op->name, value, body);
        }
    }

    void visit(const LetStmt *op) {
        Expr value = mutate(op->value);
        Stmt body;

        bound_vars.push(op->name, value);
        body = mutate(op->body);
        bound_vars.pop(op->name);

        if (value.same_as(op->value) && body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, value, body);
        }
    }

    void visit(const For *op) {
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        inner_loop_vars.push(op->name, 0);
        Stmt body = mutate(op->body);
        inner_loop_vars.pop(op->name);

        if (min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, min, extent, op->for_type, op->device_api, body);
        }

    }
};

class PartitionLoops : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {

        Stmt body = mutate(op->body);

        // We conservatively only apply this optimization at one loop
        // level, which limits the expansion in code size to a factor
        // of 3.  Comment out the check below to allow full expansion
        // into 3^n cases for n loop levels.
        //
        // Another strategy is to not recursively call mutate on the
        // body in the line above, but instead recursively call mutate
        // only on simpler_body below. This results in a 2*n + 1
        // expansion factor. Testing didn't reveal either of these
        // strategies to be a performance win, so we'll just expand
        // once.
        if (!body.same_as(op->body)) {
            stmt = For::make(op->name, op->min, op->extent,
                             op->for_type, op->device_api, body);
            return;
        }

        // We can't inject logic at the loop over gpu blocks, or in
        // between gpu thread loops.
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            stmt = For::make(op->name, op->min, op->extent,
                             op->for_type, op->device_api, body);
            return;
        }

        FindSteadyState f(op->name);
        Stmt simpler_body = f.mutate(body);
        // Ask for the start and end of the steady state.
        Expr min_steady = f.min_steady_val();
        Expr max_steady = f.max_steady_val();

        if (min_steady.defined() || max_steady.defined()) {
            debug(3) << "\nOld body: " << body << "\n";

            // They're undefined if there's no prologue or epilogue.
            bool make_prologue = min_steady.defined();
            bool make_epilogue = max_steady.defined();

            // Accrue a stack of let statements defining the steady state start and end.
            vector<pair<string, Expr> > lets;

            if (make_prologue) {
                // Clamp the prologue end to within the existing loop bounds,
                // then pull that out as a let statement.
                min_steady = clamp(min_steady, op->min, op->min + op->extent);
                string min_steady_name = op->name + ".prologue";
                lets.push_back(make_pair(min_steady_name, min_steady));
                min_steady = Variable::make(Int(32), min_steady_name);
            } else {
                min_steady = op->min;
            }

            if (make_epilogue) {
                // Clamp the epilogue start to be between the prologue
                // end and the loop end, then pull it out as a let
                // statement.
                max_steady = clamp(max_steady, min_steady, op->min + op->extent);
                string max_steady_name = op->name + ".epilogue";
                lets.push_back(make_pair(max_steady_name, max_steady));
                max_steady = Variable::make(Int(32), max_steady_name);
            } else {
                max_steady = op->extent + op->min;
            }


            debug(3) << "\nSimpler body: " << simpler_body << "\n";

            Stmt new_loop;

            if (op->for_type == ForType::Serial) {
                // Steady state.
                new_loop = For::make(op->name, min_steady, max_steady - min_steady,
                                     op->for_type, op->device_api, simpler_body);

                if (make_prologue) {
                    Stmt prologue = For::make(op->name, op->min, min_steady - op->min,
                                              op->for_type, op->device_api, body);
                    prologue = f.simplify_prologue(prologue);
                    new_loop = Block::make(prologue, new_loop);
                }

                if (make_epilogue) {
                    Stmt epilogue = For::make(op->name, max_steady, op->min + op->extent - max_steady,
                                              op->for_type, op->device_api, body);
                    epilogue = f.simplify_epilogue(epilogue);
                    new_loop = Block::make(new_loop, epilogue);
                }
            } else {
                // For parallel for loops, we inject an if statement
                // instead of splitting up the loop.
                //
                // TODO: If we have task parallel blocks, then we
                // could split out the different bodies as we do
                // above. Could be a big win on the GPU (generating
                // separate kernels for the nasty cases near the
                // boundaries).
                //
                // Rather than having a three-way if for prologue,
                // steady state, or epilogue, we have a two-way if
                // (steady-state or not), and don't bother doing
                // bounds-based simplification of the prologue and
                // epilogue.
                internal_assert(op->for_type == ForType::Parallel);
                Expr loop_var = Variable::make(Int(32), op->name);
                Expr in_steady;
                if (make_prologue) {
                    in_steady = loop_var >= min_steady;
                }
                if (make_epilogue) {
                    Expr cond = loop_var < max_steady;
                    in_steady = in_steady.defined() ? (in_steady && cond) : cond;
                }
                if (in_steady.defined()) {
                    body = IfThenElse::make(in_steady, simpler_body, body);
                }
                new_loop = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);

            }

            // Wrap the statements in the let expressions that define
            // the steady state start and end.
            while (!lets.empty()) {
                new_loop = LetStmt::make(lets.back().first, lets.back().second, new_loop);
                lets.pop_back();
            }

            // Wrap the statements in the lets that the steady state
            // start and end depend on.
            new_loop = f.add_containing_lets(new_loop);

            stmt = new_loop;
        } else if (body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, op->min, op->extent,
                             op->for_type, op->device_api, body);
        }
    }
};

// Remove any remaining 'likely' intrinsics. There may be some left
// behind if we didn't successfully simplify something.
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

}

Stmt partition_loops(Stmt s) {
    s = PartitionLoops().mutate(s);
    s = RemoveLikelyTags().mutate(s);
    return s;
}

}
}
