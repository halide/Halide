#include "ApproximateDifferences.h"
#include "Error.h"
#include "IR.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

Direction flip(const Direction &direction) {
    if (direction == Direction::Lower) {
        return Direction::Upper;
    } else {
        return Direction::Lower;
    }
}

// For debugging purposes.
std::ostream &operator<<(std::ostream &s, const Direction &d) {
    if (d == Direction::Lower) {
        s << "Direction::Lower";
    } else {
        s << "Direction::Upper";
    }
    return s;
}

Expr handle_push_div(const Expr &expr, Direction direction, int64_t denom);
Expr handle_push_mul(const Expr &expr, Direction direction, int64_t factor);
Expr handle_push_none(const Expr &expr, Direction direction);

Expr handle_push_div(const Expr &expr, const Direction direction, const int64_t denom) {
    debug(3) << "push_div(" << expr << ", " << direction << ", " << denom << ")\n";
    internal_assert(denom != 1) << "handle_push_div called with denom=1 on Expr: " << expr << "\n";
    internal_assert(denom > 0) << "handle_push_div can only handle positive denominators, received: " << expr << " / " << denom << "\n";

    if (const IntImm *op = expr.as<IntImm>()) {
        int64_t value = div_imp(op->value, denom);
        return IntImm::make(op->type, value);
    } else if (const Add *op = expr.as<Add>()) {
        // n > 0 -> (a / n) + (b / n) <= (a + b) / n <= (a / n) + (b / n) + 1
        Expr rec = handle_push_div(op->a, direction, denom) + handle_push_div(op->b, direction, denom);
        if (direction == Direction::Lower) {
            return rec;
        } else {
            return rec + 1;
        }
    } else if (const Sub *op = expr.as<Sub>()) {
        // n > 0 -> (a / n) - (b / n) - 1 <= (a - b) / n <= (a / n) - (b / n)
        Expr rec = handle_push_div(op->a, direction, denom) - handle_push_div(op->b, flip(direction), denom);
        if (direction == Direction::Lower) {
            return rec - 1;
        } else {
            return rec;
        }
    } else if (const Div *op = expr.as<Div>()) {
        if (const IntImm *imm = op->b.as<IntImm>()) {
            if (imm->value > 0) {
                // TODO: is it reasonable to be concerned about overflow here?
                return handle_push_div(op->a, direction, denom * imm->value);
            }
        }
        // otherwise let it fall to the base case.
    } else if (const Min *op = expr.as<Min>()) {
        return min(handle_push_div(op->a, direction, denom), handle_push_div(op->b, direction, denom));
    } else if (const Max *op = expr.as<Max>()) {
        return max(handle_push_div(op->a, direction, denom), handle_push_div(op->b, direction, denom));
    } else if (const Select *op = expr.as<Select>()) {
        const Expr true_value = handle_push_div(op->true_value, direction, denom);
        const Expr false_value = handle_push_div(op->false_value, direction, denom);
        return select(op->condition, true_value, false_value);
    } else if (const Mul *op = expr.as<Mul>()) {
        // Can only go inside a mul if the constant of multiplication is divisible by the denominator
        // TODO: are there any other cases that we can handle?

        // Assume constant is on the RHS of Mul due to simplification.
        if (const IntImm *constant = op->b.as<IntImm>()) {
            // We will have to change direction if multiplying by a negative constant.
            const Direction new_direction = (constant->value > 0) ? direction : flip(direction);
            
            if ((constant->value % denom) == 0) {
                // Keep pushing.
                const int64_t new_factor = div_imp(constant->value, denom);
                if (new_factor == 1) {
                    return handle_push_none(op->a, new_direction);
                } else {
                    return handle_push_mul(op->a, new_direction, new_factor);
                }
            } else if ((denom % constant->value) == 0 && constant->value > 0) {
                const int64_t new_denom = div_imp(denom, constant->value);
                if (new_denom == 1) {
                    return handle_push_none(op->a, new_direction);
                } else {
                    return handle_push_div(op->a, new_direction, new_denom);
                }
            } else {
                // Just push the multiply inwards.
                // Essentially the base case below, without needing to call handle_push_none.
                Expr expr_denom = IntImm::make(expr.type(), denom);
                Expr recurse = handle_push_mul(op->a, new_direction, constant->value);
                return Div::make(std::move(recurse), std::move(expr_denom));
            }
        }
        // otherwise let it fall to the base case.
    }

    // Base case.
    Expr expr_denom = IntImm::make(expr.type(), denom);
    Expr recurse = handle_push_none(expr, direction);
    return Div::make(std::move(recurse), std::move(expr_denom));
}

Expr handle_push_mul(const Expr &expr, Direction direction, int64_t factor) {
    debug(3) << "push_mul(" << expr << ", " << direction << ", " << factor << ")\n";
    internal_assert(factor != 1) << "handle_push_mul called with factor=1 on Expr: " << expr << "\n";

    if (const IntImm *op = expr.as<IntImm>()) {
        // TODO: check for overflow.
        int64_t value = op->value * factor;
        return IntImm::make(op->type, value);
    } else if (const Add *op = expr.as<Add>()) {
        Expr a = handle_push_mul(op->a, direction, factor);
        Expr b = handle_push_mul(op->b, direction, factor);
        return Add::make(std::move(a), std::move(b));
    } else if (const Sub *op = expr.as<Sub>()) {
        Expr a = handle_push_mul(op->a, direction, factor);
        Expr b = handle_push_mul(op->b, flip(direction), factor);
        return Sub::make(std::move(a), std::move(b));
    } else if (const Min *op = expr.as<Min>()) {
        Expr a = handle_push_mul(op->a, direction, factor);
        Expr b = handle_push_mul(op->b, direction, factor);
        if (factor > 0) {
            return Min::make(std::move(a), std::move(b));
        } else {
            return Max::make(std::move(a), std::move(b));
        }
    } else if (const Max *op = expr.as<Max>()) {
        Expr a = handle_push_mul(op->a, direction, factor);
        Expr b = handle_push_mul(op->b, direction, factor);
        if (factor > 0) {
            return Max::make(std::move(a), std::move(b));
        } else {
            return Min::make(std::move(a), std::move(b));
        }
    } else if (const Select *op = expr.as<Select>()) {
        const Expr true_value = handle_push_mul(op->true_value, direction, factor);
        const Expr false_value = handle_push_mul(op->false_value, direction, factor);
        return select(op->condition, true_value, false_value);
    } else if (const Mul *op = expr.as<Mul>()) {
        // Assume constant is on the RHS of Mul due to simplification.
        if (const IntImm *constant = op->b.as<IntImm>()) {
            // TODO: check for overflow
            const Direction new_direction = (constant->value > 0) ? direction : flip(direction);
            return handle_push_mul(op->a, new_direction, factor * constant->value);
        }
        // otherwise fall to base case.
    } else if (const Div *op = expr.as<Div>()) {
        // TODO: Is there anything we can do other than the positive and positive case?
        if (const IntImm *constant = op->b.as<IntImm>()) {
            // Only do this if not trying to be exact.
            if (constant->value > 0 && factor > 0) {
                // Do some factoring simplification
                int64_t gcd_val = gcd(constant->value, factor);

                // For positive c0 and c1,
                //   (x * c1) / c0 - (c1 + 1)  <= (x / c0) * c1  <= (x * c1) / c0

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
                    // Do GCD simplification
                    // All constants (factor, denominator, gcd) must be positive at this point.
                    internal_assert(gcd_val > 0) << "GCD is non-positive: " << gcd_val << "For expression: " << expr << " with factor: " << factor << " bound: " << direction << "\n";
                    const int64_t new_factor = factor / gcd_val;
                    const int64_t new_denom = constant->value / gcd_val;
                    Expr expr_denom = IntImm::make(expr.type(), new_denom);
                    Expr recurse = (new_factor == 1) ? handle_push_none(op->a, direction) : handle_push_mul(op->a, direction, new_factor);
                    Expr recurse_div = (new_denom == 1) ? std::move(recurse) : Div::make(std::move(recurse), std::move(expr_denom));
                    if (direction == Direction::Lower) {
                        Expr offset = IntImm::make(op->type, factor - 1);
                        return Sub::make(std::move(recurse_div), std::move(offset));
                    } else {
                        return recurse_div;
                    }
                }
            }
        }
    }

    // Base case.
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
        // Assume constant is on the RHS of Mul due to simplification.
        if (const IntImm *constant = op->b.as<IntImm>()) {
            return handle_push_mul(op->a, direction, constant->value);
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

Expr push_rationals(const Expr &expr, const Direction direction) {
    if (expr.type() == Int(32)) {
      return handle_push_none(expr, direction);
    } else {
      return expr;
    }
}

}  // namespace Internal
}  // namespace Halide
