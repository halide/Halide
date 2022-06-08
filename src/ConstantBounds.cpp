#include "ConstantBounds.h"
#include "Bounds.h"
#include "Error.h"
#include "ExprUsesVar.h"
#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "SimplifyCorrelatedDifferences.h"
#include "UniquifyVariableNames.h"

#include <limits>
#include <vector>

namespace Halide {
namespace Internal {

// For debugging purposes.
std::ostream &operator<<(std::ostream &s, const Direction &d) {
    if (d == Direction::Lower) {
        s << "Direction::Lower";
    } else {
        s << "Direction::Upper";
    }
    return s;
}

namespace {

Direction flip(const Direction &direction) {
    if (direction == Direction::Lower) {
        return Direction::Upper;
    } else {
        return Direction::Lower;
    }
}

/*
 * Methods that push divisions or multiplications by constants inwards
 * These are approximation techniques that approximate in a given direction.
 */
Expr handle_push_div(const Expr &expr, Direction direction, int64_t denom);
Expr handle_push_mul(const Expr &expr, Direction direction, int64_t factor);
Expr handle_push_none(const Expr &expr, Direction direction);

/*
 * Helper functions for pushing a multiplication inside of a division.
 */
Expr push_div_inside_mul_helper(const Expr &a, const IntImm *b, const Direction direction, const int64_t denom) {
    // Can only go inside a mul if the constant of multiplication is divisible by the denominator
    // TODO: are there any other cases that we can handle?

    // strip off mul-by-one.
    if (b->value == 1) {
        return handle_push_div(a, direction, denom);
    }

    // We will have to change direction of the approximation if multiplying by a negative constant.
    const Direction new_direction = (b->value > 0) ? direction : flip(direction);

    if ((b->value % denom) == 0) {
        // (c0 * x) / c1 when c0 % c1 == 0 -> x * (c0 / c1)
        const int64_t new_factor = div_imp(b->value, denom);
        if (new_factor == 1) {
            // (c0 * x) / c0 -> x
            return handle_push_none(a, new_direction);
        } else {
            // Push the x * (c0 / c1)
            return handle_push_mul(a, new_direction, new_factor);
        }
    } else if ((denom % b->value) == 0 && b->value > 0) {
        // (c0 * x) / c1 when c1 % c0 == 0 and c1 > 0 -> x / (c1 / c0)
        const int64_t new_denom = div_imp(denom, b->value);
        if (new_denom == 1) {
            return handle_push_none(a, new_direction);
        } else {
            // Push the x / (c1 / c0)
            return handle_push_div(a, new_direction, new_denom);
        }
    } else {
        // Just push the multiply inwards, and stop pushing division.
        // Essentially the base case for handle_push_div, without needing to call handle_push_none.
        Expr expr_denom = IntImm::make(a.type(), denom);
        Expr recurse = handle_push_mul(a, new_direction, b->value);
        return Div::make(std::move(recurse), std::move(expr_denom));
    }
}

Expr push_div_inside_mul(const Mul *op, const Direction direction, const int64_t denom) {
    if (const IntImm *constant = op->b.as<IntImm>()) {
        return push_div_inside_mul_helper(op->a, constant, direction, denom);
    } else if (const IntImm *constant = op->a.as<IntImm>()) {
        return push_div_inside_mul_helper(op->b, constant, direction, denom);
    } else {
        return Expr();
    }
}

/*
 * Approximate method. Push division by a constant inside a number of possible IR nodes.
 * Requires that the denominator being pushed is positive. This is the common case anyways.
 * Requires that the denom is not 1 - if denom=1, handle_push_none should be called.
 */
Expr handle_push_div(const Expr &expr, const Direction direction, const int64_t denom) {
    debug(3) << "push_div(" << expr << ", " << direction << ", " << denom << ")\n";
    internal_assert(denom != 1) << "handle_push_div called with denom=1 on Expr: " << expr << "\n";
    internal_assert(denom > 0) << "handle_push_div can only handle positive denominators, received: " << expr << " / " << denom << "\n";

    if (const IntImm *op = expr.as<IntImm>()) {
        // Pushing division to a constant is just applying the division of constants.
        int64_t value = div_imp(op->value, denom);
        return IntImm::make(op->type, value);
    } else if (const Add *op = expr.as<Add>()) {
        // A positive denominator (n) implies the following relationships:
        // Lower bound: (a / n) + (b / n) <= (a + b) / n
        // Upper bound: (a + b) / n <= (a / n) + (b / n) + 1
        Expr lhs = handle_push_div(op->a, direction, denom);
        Expr rhs = handle_push_div(op->b, direction, denom);
        Expr rec = Add::make(std::move(lhs), std::move(rhs));
        if (direction == Direction::Lower) {
            return rec;
        } else {
            return rec + 1;
        }
    } else if (const Sub *op = expr.as<Sub>()) {
        // A positive denominator (n) implies the following relationships:
        // Lower bound: (a / n) - (b / n) - 1 <= (a - b) / n
        // Upper bound: (a - b) / n <= (a / n) - (b / n)
        Expr lhs = handle_push_div(op->a, direction, denom);
        Expr rhs = handle_push_div(op->b, flip(direction), denom);
        Expr rec = Sub::make(std::move(lhs), std::move(rhs));
        if (direction == Direction::Lower) {
            return rec - 1;
        } else {
            return rec;
        }
    } else if (const Div *op = expr.as<Div>()) {
        if (const IntImm *imm = op->b.as<IntImm>()) {
            // We can keep pushing as long as the denominator remains a positive constant.
            if (imm->value > 0) {
                return handle_push_div(op->a, direction, denom * imm->value);
            }
        }
        // otherwise let it fall to the base case.
    } else if (const Min *op = expr.as<Min>()) {
        // Can always push a positive division inside of a min
        Expr lhs = handle_push_div(op->a, direction, denom);
        Expr rhs = handle_push_div(op->b, direction, denom);
        return Min::make(std::move(lhs), std::move(rhs));
    } else if (const Max *op = expr.as<Max>()) {
        // Can always push a positive division inside of a max
        Expr lhs = handle_push_div(op->a, direction, denom);
        Expr rhs = handle_push_div(op->b, direction, denom);
        return Max::make(std::move(lhs), std::move(rhs));
    } else if (const Select *op = expr.as<Select>()) {
        // Can always push a positive division inside of a select
        Expr true_value = handle_push_div(op->true_value, direction, denom);
        Expr false_value = handle_push_div(op->false_value, direction, denom);
        return select(op->condition, std::move(true_value), std::move(false_value));
    } else if (const Mul *op = expr.as<Mul>()) {
        Expr handled = push_div_inside_mul(op, direction, denom);
        if (handled.defined()) {
            return handled;
        }
        // otherwise let it fall to the base case.
    }

    // Base case. Stop pushing the division inwards,
    // but recurse on the inside if possible.
    Expr expr_denom = IntImm::make(expr.type(), denom);
    Expr recurse = handle_push_none(expr, direction);
    return Div::make(std::move(recurse), std::move(expr_denom));
}

/*
 * Approximate method. Push multiplication by a constant inside a number of possible IR nodes.
 * Requires that the factor is not 1. If factor=1, handle_push_none should be called.
 */
Expr handle_push_mul(const Expr &expr, Direction direction, const int64_t factor) {
    debug(3) << "push_mul(" << expr << ", " << direction << ", " << factor << ")\n";
    internal_assert(factor != 1) << "handle_push_mul called with factor=1 on Expr: " << expr << "\n";

    if (const IntImm *op = expr.as<IntImm>()) {
        // Pushing multiplication to a constant is just the constant-folded multiplication.
        int64_t value = op->value * factor;
        return IntImm::make(op->type, value);
    } else if (const Add *op = expr.as<Add>()) {
        // Can always cleanly push a mul inside of an addition.
        Expr lhs = handle_push_mul(op->a, direction, factor);
        Expr rhs = handle_push_mul(op->b, direction, factor);
        return Add::make(std::move(lhs), std::move(rhs));
    } else if (const Sub *op = expr.as<Sub>()) {
        // Can always cleanly push a mul inside of a subtraction,
        // but need to flip the direction of approximation of the second argument.
        Expr lhs = handle_push_mul(op->a, direction, factor);
        Expr rhs = handle_push_mul(op->b, flip(direction), factor);
        return Sub::make(std::move(lhs), std::move(rhs));
    } else if (const Min *op = expr.as<Min>()) {
        Expr lhs = handle_push_mul(op->a, direction, factor);
        Expr rhs = handle_push_mul(op->b, direction, factor);
        // If pushing a negative multiplication, need to convert to max.
        // c < 0 implies c * min(a, b) = max(c * a, c * b)
        if (factor > 0) {
            return Min::make(std::move(lhs), std::move(rhs));
        } else {
            return Max::make(std::move(lhs), std::move(rhs));
        }
    } else if (const Max *op = expr.as<Max>()) {
        Expr lhs = handle_push_mul(op->a, direction, factor);
        Expr rhs = handle_push_mul(op->b, direction, factor);
        // If pushing a negative multiplication, need to convert to min.
        // c < 0 implies c * max(a, b) = min(c * a, c * b)
        if (factor > 0) {
            return Max::make(std::move(lhs), std::move(rhs));
        } else {
            return Min::make(std::move(lhs), std::move(rhs));
        }
    } else if (const Select *op = expr.as<Select>()) {
        // Can always cleanly push a mul inside of a Select.
        const Expr true_value = handle_push_mul(op->true_value, direction, factor);
        const Expr false_value = handle_push_mul(op->false_value, direction, factor);
        return select(op->condition, true_value, false_value);
    } else if (const Mul *op = expr.as<Mul>()) {
        if (const IntImm *constant = op->b.as<IntImm>()) {
            // stip off mul-by-one
            if (constant->value == 1) {
                return handle_push_mul(op->a, direction, factor);
            }
            const int32_t new_factor = factor * constant->value;
            const Direction new_direction = (constant->value > 0) ? direction : flip(direction);
            if (new_factor == 1) {
                return handle_push_none(op->a, new_direction);
            } else {
                return handle_push_mul(op->a, new_direction, new_factor);
            }
        } else if (const IntImm *constant = op->a.as<IntImm>()) {
            // stip off mul-by-one
            if (constant->value == 1) {
                return handle_push_mul(op->b, direction, factor);
            }
            const int32_t new_factor = factor * constant->value;
            const Direction new_direction = (constant->value > 0) ? direction : flip(direction);
            if (new_factor == 1) {
                return handle_push_none(op->b, new_direction);
            } else {
                return handle_push_mul(op->b, new_direction, new_factor);
            }
        }
        // otherwise fall to base case.
    } else if (const Div *op = expr.as<Div>()) {
        // TODO: Is there anything we can do other than the positive and positive case?
        if (const IntImm *constant = op->b.as<IntImm>()) {
            // This is an approximation.
            if (constant->value > 0 && factor > 0) {
                // Do some factoring simplification.
                int64_t gcd_val = gcd(constant->value, factor);

                // For positive c0 and c1,
                //   (x * c1) / c0 - (c1 + 1) <= (x / c0) * c1  <= (x * c1) / c0

                if (gcd_val == 1) {
                    // Can't do factoring simplification, do default behavior.
                    Expr recurse = (handle_push_mul(op->a, direction, factor) / op->b);
                    if (direction == Direction::Lower) {
                        Expr offset = IntImm::make(op->type, factor - 1);
                        return Sub::make(std::move(recurse), std::move(offset));
                    } else {
                        return recurse;
                    }
                } else {
                    // Do GCD simplification.
                    // All constants (factor, denominator, gcd) must be positive at this point.
                    internal_assert(gcd_val > 0) << "GCD is non-positive: " << gcd_val << "For expression: " << expr << " with factor: " << factor << " bound: " << direction << "\n";
                    // Factor out the greatest common divisior from both the factor and the denominator.
                    const int64_t new_factor = factor / gcd_val;
                    const int64_t new_denom = constant->value / gcd_val;
                    Expr expr_denom = IntImm::make(expr.type(), new_denom);
                    // If we have a multiplication by a constant to push, then push it.
                    Expr recurse = (new_factor == 1) ? handle_push_none(op->a, direction) : handle_push_mul(op->a, direction, new_factor);
                    // If we still have a denominator to push, make a division.
                    Expr recurse_div = (new_denom == 1) ? std::move(recurse) : Div::make(std::move(recurse), std::move(expr_denom));
                    if (direction == Direction::Lower) {
                        // Lower bound: (x * c1) / c0 - (c1 + 1) <= (x / c0) * c1
                        Expr offset = IntImm::make(op->type, factor - 1);
                        return Sub::make(std::move(recurse_div), std::move(offset));
                    } else {
                        // Upper bound: (x / c0) * c1  <= (x * c1) / c0
                        return recurse_div;
                    }
                }
            }
        }
    }

    // Base case. Stop pushing the multiplication inwards,
    // but recurse on the inside if possible.
    Expr expr_factor = IntImm::make(expr.type(), factor);
    Expr recurse = handle_push_none(expr, direction);
    return Mul::make(std::move(recurse), std::move(expr_factor));
}

Expr handle_push_none(const Expr &expr, Direction direction) {
    debug(3) << "push_none(" << expr << ", " << direction << ")\n";
    // Upper bound or lower bound without a factor or denominator
    if (const Add *op = expr.as<Add>()) {
        return handle_push_none(op->a, direction) + handle_push_none(op->b, direction);
    } else if (const Sub *op = expr.as<Sub>()) {
        return handle_push_none(op->a, direction) - handle_push_none(op->b, flip(direction));
    } else if (const Min *op = expr.as<Min>()) {
        return min(handle_push_none(op->a, direction), handle_push_none(op->b, direction));
    } else if (const Max *op = expr.as<Max>()) {
        return max(handle_push_none(op->a, direction), handle_push_none(op->b, direction));
    } else if (const Select *op = expr.as<Select>()) {
        const Expr true_value = handle_push_none(op->true_value, direction);
        const Expr false_value = handle_push_none(op->false_value, direction);
        return select(op->condition, true_value, false_value);
    } else if (const Mul *op = expr.as<Mul>()) {
        if (const IntImm *constant = op->b.as<IntImm>()) {
            if (constant->value == 1) {
                // strip off a mul-by-one.
                return handle_push_none(op->a, direction);
            } else {
                return handle_push_mul(op->a, direction, constant->value);
            }
        } else if (const IntImm *constant = op->a.as<IntImm>()) {
            if (constant->value == 1) {
                // strip off a mul-by-one.
                return handle_push_none(op->b, direction);
            } else {
                return handle_push_mul(op->b, direction, constant->value);
            }
        }
    } else if (const Div *op = expr.as<Div>()) {
        if (const IntImm *constant = op->b.as<IntImm>()) {
            if (constant->value > 0) {
                return handle_push_div(op->a, direction, constant->value);
            } else {
                Expr recurse = handle_push_none(op->a, flip(direction));
                return Div::make(std::move(recurse), op->b);
            }
        }
    }
    return expr;
}

std::pair<size_t, Expr>
strip_unbounded(const Expr &expr, const Direction &direction,
                const Scope<Interval> &scope, const std::map<std::string, int> &var_uses);

// Helper method for Add, and Min/Max.
template<typename BinOp>
std::pair<size_t, Expr>
strip_unbounded_binop(const BinOp *op, const Direction &direction,
                      const Scope<Interval> &scope, const std::map<std::string, int> &var_uses) {
    auto [a_count, a_new] = strip_unbounded(op->a, direction, scope, var_uses);
    auto [b_count, b_new] = strip_unbounded(op->b, direction, scope, var_uses);
    const size_t unbounded_vars = a_count + b_count;

    if (a_new.same_as(op->a) && b_new.same_as(op->b)) {
        return {unbounded_vars, op};
    } else {
        Expr repl_expr = BinOp::make(std::move(a_new), std::move(b_new));
        return {unbounded_vars, std::move(repl_expr)};
    }
}

// Helper method for special cases of Min/Max.
template<typename MinMax>
std::pair<size_t, Expr>
strip_unbounded_minmax(const MinMax *op, const Direction &direction,
                       const Scope<Interval> &scope, const std::map<std::string, int> &var_uses) {
    auto [a_count, a_new] = strip_unbounded(op->a, direction, scope, var_uses);

    if (a_count > 0) {
        // We can strip a, replace Expr with b.
        return strip_unbounded(op->b, direction, scope, var_uses);
    }

    auto [b_count, b_new] = strip_unbounded(op->b, direction, scope, var_uses);

    if (b_count > 0) {
        // We can strip b, replace Expr with a.
        return {a_count, std::move(a_new)};
    }

    // No luck, return the mutated Min/Max with no unbounded vars.
    if (a_new.same_as(op->a) && b_new.same_as(op->b)) {
        return {0, op};
    } else {
        Expr repl_expr = MinMax::make(std::move(a_new), std::move(b_new));
        return {0, std::move(repl_expr)};
    }
}

/*
 * Visitor for remove terms that are completely unbounded from
 * a min or a max.
 * @param expr : Expr to strip terms from.
 * @param direction : Which direction we are approximating.
 * @param scope : Used to check if a variable has a constant bound or not.
 * @param var_uses : Counts of var use, used to strip uncorrelated terms.
 * @param unbounded_vars : Count of the number of unbounded vars in a given sub expression.
 */
std::pair<size_t, Expr>
strip_unbounded(const Expr &expr, const Direction &direction,
                const Scope<Interval> &scope, const std::map<std::string, int> &var_uses) {

    if (const Variable *op = expr.as<Variable>()) {
        // A variable is unbounded if both are true:
        // 1) appears only once (can't be correlated with other expressions)
        // 2) has no constant bounds (on either side).
        const auto iter = var_uses.find(op->name);
        internal_assert(iter != var_uses.end()) << "Encountered uncounted variable: " << Expr(op) << "\n";
        const int uses = iter->second;
        size_t unbounded_vars = 0;
        if (uses == 1) {
            if (scope.contains(op->name)) {
                const Interval interval = scope.get(op->name);
                if (interval.is_everything()) {
                    unbounded_vars++;
                }
            } else {
                unbounded_vars++;
            }
        }
        return {unbounded_vars, op};
    } else if (const Sub *op = expr.as<Sub>()) {
        // Sub nodes must flip the approximation direction for the second argument.
        auto [a_count, a_new] = strip_unbounded(op->a, direction, scope, var_uses);
        auto [b_count, b_new] = strip_unbounded(op->b, flip(direction), scope, var_uses);
        const size_t unbounded_vars = a_count + b_count;

        if (a_new.same_as(op->a) && b_new.same_as(op->b)) {
            return {unbounded_vars, op};
        } else {
            Expr repl_expr = Sub::make(std::move(a_new), std::move(b_new));
            return {unbounded_vars, std::move(repl_expr)};
        }
    } else if (const Add *op = expr.as<Add>()) {
        return strip_unbounded_binop(op, direction, scope, var_uses);
    } else if (const Mul *op = expr.as<Mul>()) {
        // Can only recurse on mul by constants, otherwise it's impossible
        // to know which direction we should be approximating.
        if (is_const(op->b)) {
            Direction new_direction = is_negative_const(op->b) ? flip(direction) : direction;

            auto [a_count, a_new] = strip_unbounded(op->a, new_direction, scope, var_uses);

            if (a_new.same_as(op->a)) {
                return {a_count, op};
            } else {
                Expr repl_expr = Mul::make(std::move(a_new), op->b);
                return {a_count, std::move(repl_expr)};
            }
        } else if (is_const(op->a)) {
            Direction new_direction = is_negative_const(op->a) ? flip(direction) : direction;

            auto [b_count, b_new] = strip_unbounded(op->b, new_direction, scope, var_uses);

            if (b_new.same_as(op->b)) {
                return {b_count, op};
            } else {
                Expr repl_expr = Mul::make(std::move(b_new), op->a);
                return {b_count, std::move(repl_expr)};
            }
        }
        // Fall to base case.
    } else if (const Div *op = expr.as<Div>()) {
        // Likewise, can only recurse on div by constants,
        // otherwise it's impossible to know which direction
        // we should be approximating.
        if (const IntImm *constant = op->b.as<IntImm>()) {
            if (constant->value > 0) {
                auto [a_count, a_new] = strip_unbounded(op->a, direction, scope, var_uses);
                if (a_new.same_as(op->a)) {
                    return {a_count, op};
                } else {
                    Expr repl_expr = Div::make(std::move(a_new), op->b);
                    return {a_count, std::move(repl_expr)};
                }
            }
            // We could handle the negative denominator case,
            // but that is very uncommon.
        }
        // Fall to base case.
    } else if (const Select *op = expr.as<Select>()) {
        // Do not visit condition, don't care about those variables.
        auto [t_count, t_new] = strip_unbounded(op->true_value, direction, scope, var_uses);
        auto [f_count, f_new] = strip_unbounded(op->false_value, direction, scope, var_uses);
        const size_t unbounded_vars = t_count + f_count;
        if (t_new.same_as(op->true_value) &&
            f_new.same_as(op->false_value)) {
            // No values stripped.
            return {unbounded_vars, op};
        } else {
            Expr repl_expr = Select::make(op->condition, std::move(t_new), std::move(f_new));
            return {unbounded_vars, std::move(repl_expr)};
        }
    } else if (const Min *op = expr.as<Min>()) {
        if (direction == Direction::Lower) {
            // If we are trying to lower bound a Min,
            // we merge the lower bounds of the two sides.
            return strip_unbounded_binop(op, direction, scope, var_uses);
        } else {
            // However, if we are trying to upper bound a Min,
            // then we can take either of the two sides of a Min,
            // so remove a side with unbounded variables.
            return strip_unbounded_minmax(op, direction, scope, var_uses);
        }
    } else if (const Max *op = expr.as<Max>()) {
        if (direction == Direction::Upper) {
            // If we are trying to upper bound a Max,
            // we merge the upper bounds of the two sides.
            return strip_unbounded_binop(op, direction, scope, var_uses);
        } else {
            // However, if we are trying to lower bound a Max,
            // then we can take either of the two sides of a Max,
            // so remove a side with unbounded variables.
            return strip_unbounded_minmax(op, direction, scope, var_uses);
        }
    } else if (const Let *op = expr.as<Let>()) {
        // We want to recurse through Lets.
        auto [v_count, v_new] = strip_unbounded(op->value, direction, scope, var_uses);
        auto [b_count, b_new] = strip_unbounded(op->body, direction, scope, var_uses);
        // We might want to only count b_count.
        const size_t unbounded_vars = v_count + b_count;

        if (v_new.same_as(op->value) &&
            b_new.same_as(op->body)) {
            return {unbounded_vars, op};
        } else {
            Expr repl_expr = Let::make(op->name, std::move(v_new), std::move(b_new));
            return {unbounded_vars, std::move(repl_expr)};
        }
    }

    // Base case: return 0 unbounded variables
    // because this term should not be removed.
    return {0, expr};
}
/*
 * Visitor for reordering summations and removing like-terms from min/max/select nodes.
 * This is an exact method, no approximations.
 * Summations are represented as a vector of affine terms.
 */
class ReorderTerms : public IRGraphMutator {
    using IRGraphMutator::visit;

    // Directly taken from Simplify_Internal.h. May want to make this a generic function.
    HALIDE_ALWAYS_INLINE
    bool should_commute(const Expr &a, const Expr &b) {
        if (a.node_type() < b.node_type()) {
            return true;
        }
        if (a.node_type() > b.node_type()) {
            return false;
        }

        if (a.node_type() == IRNodeType::Variable) {
            const Variable *va = a.as<Variable>();
            const Variable *vb = b.as<Variable>();
            return va->name.compare(vb->name) > 0;
        }

        return false;
    }

    // This is very similar to code in LICM, but we don't care about depth.
    struct AffineTerm {
        Expr expr;
        int32_t coefficient;
    };

    // Simple helper function for negating a term.
    AffineTerm negate(const AffineTerm &term) {
        return AffineTerm{term.expr, -term.coefficient};
    }

    bool is_valid_int(int64_t value) {
        return value >= (int64_t)std::numeric_limits<int32_t>::min() && value <= (int64_t)std::numeric_limits<int32_t>::max();
    }

    // Useful for extracting an affine term from an (x * c) term
    bool is_mul_by_int_const(const Expr &expr, AffineTerm &term) {
        const Mul *mul = expr.as<Mul>();
        if (mul) {
            if (const IntImm *a = mul->a.as<IntImm>()) {
                if (is_valid_int(a->value) && is_valid_int(-a->value)) {
                    term.coefficient = a->value;
                    term.expr = mul->b;
                    return true;
                }
            } else if (const IntImm *b = mul->b.as<IntImm>()) {
                if (is_valid_int(b->value) && is_valid_int(-b->value)) {
                    term.coefficient = b->value;
                    term.expr = mul->a;
                    return true;
                }
            }
        }
        return false;
    }

    // Given a summation, extract it into a series of affine terms,
    // sorted by `should_commute`.
    std::vector<AffineTerm> extract_summation(const Expr &e) {
        std::vector<AffineTerm> pending, terms;
        pending.push_back({e, 1});

        // In case this is not a sum
        const Add *add = e.as<Add>();
        const Sub *sub = e.as<Sub>();
        if (!(add || sub)) {
            return pending;  // Can't do anything with non-sum.
        }

        while (!pending.empty()) {
            AffineTerm next = pending.back();
            pending.pop_back();
            const Add *add = next.expr.as<Add>();
            const Sub *sub = next.expr.as<Sub>();
            if (add) {
                pending.push_back({add->a, next.coefficient});
                pending.push_back({add->b, next.coefficient});
            } else if (sub) {
                pending.push_back({sub->a, next.coefficient});
                pending.push_back({sub->b, -next.coefficient});
            } else {
                // Try to recurse, the node might turn into a summation.
                next.expr = mutate(next.expr);

                if (next.expr.as<Add>() || next.expr.as<Sub>()) {
                    // After mutation it became an add or sub, throw it back on the pending queue.
                    pending.push_back(next);
                } else {
                    // If this term is a mul by a constant, we can extract out the constant and keep trying.
                    AffineTerm term;
                    if (is_mul_by_int_const(next.expr, term) && !mul_would_overflow(32, next.coefficient, term.coefficient)) {
                        internal_assert(term.expr.defined());
                        term.coefficient *= next.coefficient;
                        pending.push_back(term);
                    } else {
                        // This is a leaf.
                        terms.push_back(next);
                    }
                }
            }
        }

        std::stable_sort(terms.begin(), terms.end(),
                         [&](const AffineTerm &a, const AffineTerm &b) {
                             return should_commute(a.expr, b.expr);
                         });

        return terms;
    }

    inline std::vector<AffineTerm> simplify_summation(const std::vector<AffineTerm> &terms) {
        // TODO: for now, only do linear-term simplification.
        // There are other simplifications that can be done
        // (i.e. pushing terms into a Min or Max to cancel terms),
        // but we need a good polynomial-time algorithm for it.
        return simplify_linear_summation(terms);
    }

    // Two-finger O(n) algorithm for simplifying sorted sums.
    // Because the input is sorted, we only need to check equality
    // on adjacent terms, i.e. the following:
    // terms: [ {a, 1}, {a, 2}, {a, 4}, {b, 4} ] -> simplified : [ {a, 7}, {b, 4} ]
    std::vector<AffineTerm> simplify_linear_summation(const std::vector<AffineTerm> &terms) {
        if (terms.empty()) {
            // Nothing to do here.
            return terms;
        }

        std::vector<AffineTerm> simplified = {terms[0]};

        int i_simpl = 0;
        int j_terms = 1;

        const int n = terms.size();

        while (j_terms < n) {
            AffineTerm current_term = terms[j_terms];
            if (graph_equal(simplified[i_simpl].expr, current_term.expr)) {
                simplified[i_simpl].coefficient += current_term.coefficient;
            } else {
                simplified.push_back(current_term);
                i_simpl++;
            }
            j_terms++;
        }
        return simplified;
    }

    Expr reconstruct_summation(std::vector<AffineTerm> &terms, Type t) {
        Expr result = make_zero(t);

        while (!terms.empty()) {
            AffineTerm next = terms.back();
            internal_assert(next.expr.defined()) << "Expr with coefficient: " << next.coefficient << " is undefined for partial result: " << result << "\n";
            terms.pop_back();
            if (next.coefficient == 0 || is_const_zero(next.expr)) {
                continue;
            } else {
                if (is_const_zero(result)) {
                    if (next.coefficient == 1) {
                        result = next.expr;
                    } else {
                        result = (next.expr * next.coefficient);
                    }
                } else {
                    if (next.coefficient == 1) {
                        result += next.expr;
                    } else if (next.coefficient == -1) {
                        result -= next.expr;
                    } else {
                        result += (next.expr * next.coefficient);
                    }
                }
            }
        }

        return result;
    }

    Expr reassociate_summation(const Expr &e) {
        std::vector<AffineTerm> terms = extract_summation(e);
        terms = simplify_summation(terms);
        return reconstruct_summation(terms, e.type());
    }

    Expr visit(const Add *op) override {
        if (op->type == Int(32)) {
            Expr ret = reassociate_summation(op);
            return ret;
        } else {
            return IRGraphMutator::visit(op);
        }
    }

    Expr visit(const Sub *op) override {
        if (op->type == Int(32)) {
            Expr ret = reassociate_summation(op);
            return ret;
        } else {
            return IRGraphMutator::visit(op);
        }
    }

    std::vector<AffineTerm> extract_and_simplify(const Expr &expr) {
        auto extract = extract_summation(expr);
        return simplify_summation(extract);
    }

    // Used to split Op(sum1, sum2) into sum3 + Op(sum1', sum2'),
    // where sum1 = sum3 + sum1', sum2 = sum3 + sum1', and Op
    // is a Min, Max, or Select
    struct AffineTermsGather {
        std::vector<AffineTerm> a;
        std::vector<AffineTerm> b;
        std::vector<AffineTerm> c;
    };

    // Assumes terms are simplified already.
    AffineTermsGather extract_like_terms(const std::vector<AffineTerm> &a, const std::vector<AffineTerm> &b) {
        internal_assert(!a.empty() && !b.empty()) << "Terms to extract should not be empty\n";

        AffineTermsGather gatherer;

        size_t i = 0, j = 0;

        while (i < a.size() && j < b.size()) {
            // Both sets contain the same non-zero value.
            if (a[i].coefficient == b[j].coefficient && graph_equal(a[i].expr, b[j].expr) && !is_const_zero(a[i].expr)) {
                gatherer.c.push_back(a[i]);
                i++;
                j++;
            } else if (should_commute(a[i].expr, b[j].expr)) {
                // i term is earlier than j term
                gatherer.a.push_back(a[i]);
                i++;
            } else {
                // j term is earlier than i term
                gatherer.b.push_back(b[j]);
                j++;
            }
        }

        // Wrap up tail conditions
        while (i < a.size()) {
            gatherer.a.push_back(a[i]);
            i++;
        }

        while (j < b.size()) {
            gatherer.b.push_back(b[j]);
            j++;
        }

        return gatherer;
    }

    template<class BinOp>
    Expr visit_binary_op(const BinOp *op) {
        if (op->type == Int(32)) {
            const Expr mutate_a = mutate(op->a);
            const Expr mutate_b = mutate(op->b);
            std::vector<AffineTerm> a_terms = extract_and_simplify(mutate_a);
            std::vector<AffineTerm> b_terms = extract_and_simplify(mutate_b);
            AffineTermsGather gathered = extract_like_terms(a_terms, b_terms);
            if (gathered.c.empty()) {
                Expr a = reconstruct_summation(a_terms, op->type);
                Expr b = reconstruct_summation(b_terms, op->type);
                return BinOp::make(a, b);
            } else {
                Expr like_terms = reconstruct_summation(gathered.c, op->type);
                Expr a = reconstruct_summation(gathered.a, op->type);
                Expr b = reconstruct_summation(gathered.b, op->type);
                return BinOp::make(a, b) + like_terms;
            }
        } else {
            return IRGraphMutator::visit(op);
        }
    }

    Expr visit(const Min *op) override {
        return visit_binary_op<Min>(op);
    }

    Expr visit(const Max *op) override {
        return visit_binary_op<Max>(op);
    }

    Expr visit(const Select *op) override {
        if (op->type == Int(32)) {
            const Expr true_value = mutate(op->true_value);
            const Expr false_value = mutate(op->false_value);
            std::vector<AffineTerm> a_terms = extract_and_simplify(true_value);
            std::vector<AffineTerm> b_terms = extract_and_simplify(false_value);
            AffineTermsGather gathered = extract_like_terms(a_terms, b_terms);
            if (gathered.c.empty()) {
                Expr a = reconstruct_summation(a_terms, op->type);
                Expr b = reconstruct_summation(b_terms, op->type);
                return Select::make(op->condition, a, b);
            } else {
                Expr like_terms = reconstruct_summation(gathered.c, op->type);
                Expr a = reconstruct_summation(gathered.a, op->type);
                Expr b = reconstruct_summation(gathered.b, op->type);
                return Select::make(op->condition, a, b) + like_terms;
            }
        } else {
            return IRGraphMutator::visit(op);
        }
    }
};

// Used to bound the number of substitutions.
class SubstituteSomeLets : public IRMutator {
    using IRMutator::visit;

    Scope<Expr> scope;
    size_t count;

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        ScopedBinding<Expr> bind(scope, op->name, value);
        Expr body = mutate(op->body);
        // Let simplify() handle the case that this var was removed.
        return Let::make(op->name, value, body);
    }

    Expr visit(const Variable *op) override {
        if (count > 0 && scope.contains(op->name)) {
            count--;
            return scope.get(op->name);
        } else {
            return op;
        }
    }

public:
    SubstituteSomeLets(size_t _count)
        : count(_count) {
    }
};

void make_const_interval(Interval &interval) {
    // Note that we can get non-const but well-defined results (e.g. signed_integer_overflow);
    // for our purposes here, treat anything non-const as no-bound.
    if (!is_const(interval.min)) {
        interval.min = Interval::neg_inf();
    }
    if (!is_const(interval.max)) {
        interval.max = Interval::pos_inf();
    }
}

Expr push_rationals(const Expr &expr, const Direction direction) {
    if (expr.type() == Int(32)) {
        return handle_push_none(expr, direction);
    } else {
        return expr;
    }
}

std::pair<Expr, bool> remove_unbounded_terms(const Expr &expr, Direction direction, const Scope<Interval> &scope) {
    std::map<std::string, int> var_uses;
    // To strip unbounded/uncorrelated terms, we need to know use counts.
    count_var_uses(expr, var_uses);

    auto [unbounded_vars, mut] = strip_unbounded(expr, direction, scope, var_uses);

    return {mut, unbounded_vars != 0};
}

Expr reorder_terms(const Expr &expr) {
    return ReorderTerms().mutate(expr);
}

Expr substitute_some_lets(const Expr &expr) {
    // Empirically appears to be a good value, most real
    // expressions we see won't have more substitutions
    // than this, but for some expressions, larger values
    // can cause Expr size explosion.
    static const size_t count = 16;
    return SubstituteSomeLets(count).mutate(expr);
}

Expr approximate_optimizations(const Expr &expr, Direction direction, const Scope<Interval> &scope) {
    Expr subs = substitute_some_lets(expr);
    if (!subs.same_as(expr)) {
        subs = simplify(subs);
    }

    auto [cleaned, unbounded] = remove_unbounded_terms(subs, direction, scope);

    if (unbounded) {
        return Expr();
    } else if (!cleaned.same_as(subs)) {
        cleaned = simplify(cleaned);
    }

    subs = reorder_terms(cleaned);
    // TODO: only do push_rationals if correlated divisions exist.
    Expr pushed = push_rationals(subs, direction);
    if (!pushed.same_as(subs)) {
        pushed = simplify(pushed);
    }
    return pushed;
}

bool possibly_correlated(const Expr &expr) {
    // TODO: is there a better/cheaper way to detect possible correlations?
    std::map<std::string, int> var_uses;
    count_var_uses(expr, var_uses);
    for (const auto &iter : var_uses) {
        if (iter.second > 1) {
            return true;
        }
    }
    return false;
}

bool should_use_approximate_methods(const Expr &expr) {
    return !is_const(expr) && expr.type() == Int(32) && possibly_correlated(expr);
}

Interval approximate_constant_bounds(const Expr &expr, const Scope<Interval> &scope) {
    Interval interval = Interval::everything();
    if (should_use_approximate_methods(expr)) {
        Expr lower = approximate_optimizations(expr, Direction::Lower, scope);
        Expr upper = approximate_optimizations(expr, Direction::Upper, scope);

        // Should these use `same_as`?
        const bool defined = lower.defined() || upper.defined();
        const bool equiv = lower.defined() && upper.defined() && equal(lower, upper);
        const bool lower_equiv = lower.defined() && !upper.defined() && equal(lower, expr);
        const bool upper_equiv = !lower.defined() && upper.defined() && equal(upper, expr);

        const bool early_out = !defined || lower_equiv || upper_equiv || (equiv && equal(expr, lower));

        if (early_out) {
            // Not worth calling bounds_of_expr_in_scope.
            return interval;
        }

        if (equiv) {
            // upper and lower are equivalent, but not equal to the original expr.
            interval = bounds_of_expr_in_scope(lower, scope, FuncValueBounds(), true);
        } else {
            interval.min = lower.defined() ? bounds_of_expr_in_scope(lower, scope, FuncValueBounds(), true).min : interval.min;
            interval.max = upper.defined() ? bounds_of_expr_in_scope(upper, scope, FuncValueBounds(), true).max : interval.max;
        }

        interval.min = simplify(interval.min);
        interval.max = simplify(interval.max);
        make_const_interval(interval);
    }
    return interval;
}

// The same as the above, but uni-directional.
Expr approximate_constant_bound(const Expr &expr, const Direction d, const Scope<Interval> &scope) {
    Expr failure = (d == Direction::Upper) ? Interval::pos_inf() : Interval::neg_inf();

    if (should_use_approximate_methods(expr)) {
        Expr opt = approximate_optimizations(expr, d, scope);
        if (!opt.defined()) {
            return failure;
        }
        Interval bounds = bounds_of_expr_in_scope(opt, scope, FuncValueBounds(), true);

        Expr sol = (d == Direction::Upper) ? bounds.max : bounds.min;
        sol = simplify(sol);

        if (is_const(sol)) {
            return sol;
        }
    }

    // Either failed to find a const bound, or was not a good Expr for approximate opts.
    return failure;
}

}  // namespace

Expr find_constant_bound(const Expr &e, Direction d, const Scope<Interval> &scope) {
    Expr expr = bound_correlated_differences(simplify(remove_likelies(e)));
    Interval interval = bounds_of_expr_in_scope(expr, scope, FuncValueBounds(), true);
    Expr sol;
    if (interval.has_upper_bound() && (d == Direction::Upper)) {
        sol = simplify(interval.max);
    } else if (interval.has_lower_bound() && (d == Direction::Lower)) {
        sol = simplify(interval.min);
    }

    Expr approx = approximate_constant_bound(expr, d, scope);
    // Take the interesection of the previous method and the aggresive method.
    if (sol.defined()) {
        if (d == Direction::Upper) {
            sol = Interval::make_min(sol, approx);
        } else {
            sol = Interval::make_max(sol, approx);
        }
    } else {
        sol = approx;
    }

    if (!is_const(sol)) {
        // The original method returns an undefined Expr, so we do too.
        return Expr();
    } else {
        return sol;
    }
}

Interval find_constant_bounds(const Expr &e, const Scope<Interval> &scope) {
    Expr expr = bound_correlated_differences(simplify(remove_likelies(e)));
    Interval interval = bounds_of_expr_in_scope(expr, scope, FuncValueBounds(), true);
    interval.min = simplify(interval.min);
    interval.max = simplify(interval.max);
    make_const_interval(interval);

    Interval approx_interval = approximate_constant_bounds(expr, scope);
    // Take the interesection of the previous method and the aggresive method.
    interval.min = Interval::make_max(interval.min, approx_interval.min);
    interval.max = Interval::make_min(interval.max, approx_interval.max);

    return interval;
}

}  // namespace Internal
}  // namespace Halide
