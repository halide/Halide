#include <algorithm>

#include "SpecializeClampedRamps.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "LinearSolve.h"
#include "IREquality.h"
#include "ExprUsesVar.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::pair;
using std::make_pair;

namespace {
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

private:

    vector<Expr> min_vals, max_vals;

    string loop_var;
    Scope<Expr> bound_vars;
    Scope<int> free_vars;
    Scope<int> inner_loop_vars;

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
        return Expr();
    }

    void visit(const Call *op) {
        IRMutator::visit(op);
        if (op->call_type == Call::Intrinsic && op->name == Call::likely) {
            // We encountered a likely intrinsic, which means this
            // branch of any selects or mins we're in is the
            // steady-state one.
            likely = true;
            expr = expr.as<Call>()->args[0];
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
        Expr solved = solve_for_linear_variable(cond, loop_var, free_vars, bound_vars);
        debug(3) << "Solved condition: " << solved << "\n";

        // The solve failed.
        if (solved.same_as(cond)) {
            return cond;
        }

        // Conditions in terms of an inner loop var can't be pulled outside
        if (expr_uses_vars(cond, inner_loop_vars)) {
            return cond;
        }

        if (const LT *lt = solved.as<LT>()) {
            if (is_loop_var(lt->a)) {
                max_vals.push_back(lt->b);
                return const_true();
            }
        } else if (const LE *le = solved.as<LE>()) {
            if (is_loop_var(le->a)) {
                max_vals.push_back(le->b + 1);
                return const_true();
            }
        } else if (const GE *ge = solved.as<GE>()) {
            if (is_loop_var(ge->a)) {
                min_vals.push_back(ge->b);
                return const_true();
            }
        } else if (const GT *gt = solved.as<GT>()) {
            if (is_loop_var(gt->a)) {
                min_vals.push_back(gt->b + 1);
                return const_true();
            }
        }

        debug(3) << "Failed to apply constraint (3): " << cond << "\n";

        return cond;
    }

    void visit_min_or_max(Expr op, Expr op_a, Expr op_b, bool is_min) {
        bool old_likely = likely;
        likely = false;
        Expr a = mutate(op_a);
        bool a_likely = likely;
        likely = false;
        Expr b = mutate(op_b);
        bool b_likely = likely;

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
            std::swap(a, b);
            std::swap(b_likely, a_likely);
        }

        if (a_likely && !b_likely) {
            // If the following condition is true, then we can
            // simplify the min to just the case marked as likely.
            Expr condition = (is_min ? a <= b : b <= a);

            if (is_one(simplify_to_true(condition))) {
                expr = a;
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

    void visit(const Select *op) {
        Expr condition = mutate(op->condition);
        bool old_likely = likely;
        likely = false;
        Expr true_value = mutate(op->true_value);
        bool a_likely = likely;
        likely = false;
        Expr false_value = mutate(op->false_value);
        bool b_likely = likely;
        likely = old_likely || a_likely || b_likely;

        if (a_likely && !b_likely) {
            // Figure out bounds on the loop var which makes the condition true.
            debug(3) << "Attempting to make this condition true: " << condition << "\n";
            Expr new_condition = simplify_to_true(condition);
            debug(3) << "Attempted to make this condition true: " << condition << " Got: " << new_condition << "\n";
            if (is_one(new_condition)) {
                // We succeeded!
                expr = true_value;
            } else {
                // Might have partially succeeded, so still use the new condition.
                expr = Select::make(new_condition, true_value, false_value);
            }
        } else if (b_likely && !a_likely) {
            debug(3) << "Attempting to make this condition false: " << condition << "\n";
            Expr new_condition = simplify_to_false(condition);
            debug(3) << "Attempted to make this condition false: " << condition << " Got: " << new_condition << "\n";
            if (is_zero(new_condition)) {
                expr = false_value;
            } else {
                expr = Select::make(new_condition, true_value, false_value);
            }
        } else if (condition.same_as(op->condition) &&
                   true_value.same_as(op->true_value) &&
                   false_value.same_as(op->false_value)) {
            expr = op;
        } else {
            expr = Select::make(condition, true_value, false_value);
        }

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

class SpecializeClampedRamps : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {

        Stmt body = mutate(op->body);

        // Conservatively only apply this optimization at one loop
        // level.
        if (!body.same_as(op->body)) {
            stmt = For::make(op->name, op->min, op->extent,
                             op->for_type, op->device_api, body);
            return;
        }

        // TODO: If we have task parallel blocks, then we can do this
        // to parallel loops too. Could be a big win on the GPU
        // (generating separate kernels for the nasty cases near the
        // boundaries).

        FindSteadyState f(op->name);
        Stmt simpler_body = f.mutate(body);
        if (!body.same_as(simpler_body)) {
            debug(3) << "\nOld body: " << body << "\n";

            Expr min_steady = f.min_steady_val();
            Expr max_steady = f.max_steady_val();

            bool make_prologue = min_steady.defined();
            bool make_epilogue = max_steady.defined();

            vector<pair<string, Expr> > lets;

            if (make_prologue) {
                min_steady = clamp(min_steady, op->min, op->min + op->extent);
                string min_steady_name = op->name + ".prologue";
                lets.push_back(make_pair(min_steady_name, min_steady));
                min_steady = Variable::make(Int(32), min_steady_name);

            } else {
                min_steady = op->min;
            }

            if (make_epilogue) {
                max_steady = clamp(max_steady, min_steady, op->min + op->extent);
                string max_steady_name = op->name + ".epilogue";
                lets.push_back(make_pair(max_steady_name, max_steady));
                max_steady = Variable::make(Int(32), max_steady_name);
            } else {
                max_steady = op->extent + op->min;
            }


            debug(3) << "\nSimpler body: " << simpler_body << "\n";
            // Steady state
            Stmt new_loop;

            if (op->for_type == ForType::Serial) {
                new_loop = For::make(op->name, min_steady, max_steady - min_steady,
                                          op->for_type, op->device_api, simpler_body);

                if (make_prologue) {
                    Stmt prologue = For::make(op->name, op->min, min_steady - op->min,
                                              op->for_type, op->device_api, body);
                    new_loop = Block::make(prologue, new_loop);
                }

                if (make_epilogue) {
                    Stmt epilogue = For::make(op->name, max_steady, op->min + op->extent - max_steady,
                                              op->for_type, op->device_api, body);
                    new_loop = Block::make(new_loop, epilogue);
                }
            } else {
                //internal_assert(op->for_type == ForType::Parallel);
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

            while (!lets.empty()) {
                new_loop = LetStmt::make(lets.back().first, lets.back().second, new_loop);
                lets.pop_back();
            }

            stmt = new_loop;
        } else if (body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, op->min, op->extent,
                             op->for_type, op->device_api, body);
        }
    }
};
}

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

Stmt specialize_clamped_ramps(Stmt s) {
    return RemoveLikelyTags().mutate(SpecializeClampedRamps().mutate(s));
}

}
}
