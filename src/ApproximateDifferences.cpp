#include "ApproximateDifferences.h"
#include "Error.h"
#include "IR.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"

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

// Taken from Simplify_Let.cpp
// TODO: handle Let variables better...
class CountVarUses : public IRVisitor {
    std::map<std::string, int> &var_uses;

    void visit(const Variable *var) override {
        var_uses[var->name]++;
    }

    void visit(const Load *op) override {
        var_uses[op->name]++;
        IRVisitor::visit(op);
    }

    void visit(const Store *op) override {
        var_uses[op->name]++;
        IRVisitor::visit(op);
    }

    using IRVisitor::visit;

public:
    CountVarUses(std::map<std::string, int> &var_uses)
        : var_uses(var_uses) {
    }
};

class StripUnboundedTerms : public IRMutator {
    Direction direction;
    const Scope<Interval> *scope_ptr;
    std::map<std::string, int> &var_uses;
    int32_t unbounded_vars = 0;
    void flip_direction() {
       direction = flip(direction);
    }

    using IRMutator::visit;

    Expr visit(const Variable *op) override {
        // A variable is unbounded if it appears only once *and* has no constant bounds.
        internal_assert(var_uses.count(op->name) > 0) << "Encountered uncounted variable: " << Expr(op) << "\n";
        const int uses = var_uses[op->name];
        if (uses == 1) {
            if (scope_ptr->contains(op->name)) {
                const Interval interval = scope_ptr->get(op->name);
                if (interval.is_everything()) {
                    unbounded_vars++;
                }
            } else {
                unbounded_vars++;
            }
        }
        return op;
    }

    Expr visit(const Add *op) override {
        Expr a_new = mutate(op->a);
        Expr b_new = mutate(op->b);

        if (a_new.same_as(op->a) && b_new.same_as(op->b)) {
            return op;
        } else {
            return Add::make(std::move(a_new), std::move(b_new));
        }
    }

    Expr visit(const Sub *op) override {
        Expr a_new = mutate(op->a);
        flip_direction();
        Expr b_new = mutate(op->b);
        flip_direction();

        if (a_new.same_as(op->a) && b_new.same_as(op->b)) {
            return op;
        } else {
            return Sub::make(std::move(a_new), std::move(b_new));
        }
    }

    Expr visit(const Mul *op) override {
        // Assume constant is on the right due to simplification
        if (is_const(op->b)) {
            const bool neg_const = is_negative_const(op->b);
            if (neg_const) {
                flip_direction();
            }

            Expr a_new = mutate(op->a);

            if (neg_const) {
                flip_direction();
            }

            if (a_new.same_as(op->a)) {
                return op;
            } else {
                return Mul::make(std::move(a_new), op->b);
            }
        }
        return op;
    }

    Expr visit(const Div *op) override {
        if (const IntImm *constant = op->b.as<IntImm>()) {
            if (constant->value > 0) {
                Expr a = mutate(op->a);
                if (a.same_as(op->a)) {
                    return op;
                } else {
                    return Div::make(std::move(a), op->b);
                }
            }
        }
        return op;
    }

    Expr visit(const Min *op) override {
        // If we are trying to Lower bound a Min, we merge the lower bounds of the two sides.
        if (direction == Direction::Lower) {
            Expr a_new = mutate(op->a);
            Expr b_new = mutate(op->b);
            if (!a_new.same_as(op->a) || !b_new.same_as(op->b)) {
                return Min::make(std::move(a_new), std::move(b_new));
            } else {
                return op;
            }
        }

        // If we are trying to Lower bound a Min, then we can take either of the two sides of a Min,
        // so choose the relevant one if possible.
        const int32_t original_count = unbounded_vars;

        Expr a_new = mutate(op->a);
        const int32_t a_count = unbounded_vars;

        // short circuit if a contains at least one unbounded var
        if (a_count > original_count) {
            unbounded_vars = original_count;
            return mutate(op->b);
        }

        // Otherwise try to get rid of b

        Expr b_new = mutate(op->b);
        const int32_t b_count = unbounded_vars;

        // Check if b contains at least one unbounded var
        if (b_count > a_count) {
            unbounded_vars = a_count;
            return a_new;
        }

        // No luck, return the mutated Min
        if (!a_new.same_as(op->a) || !b_new.same_as(op->b)) {
            return Min::make(std::move(a_new), std::move(b_new));
        } else {
            return op;
        }
    }

    Expr visit(const Max *op) override {
        // If we are trying to Upper bound a Max, we merge the upper bounds of the two sides.
        if (direction == Direction::Upper) {
            Expr a_new = mutate(op->a);
            Expr b_new = mutate(op->b);
            if (!a_new.same_as(op->a) || !b_new.same_as(op->b)) {
                return Max::make(std::move(a_new), std::move(b_new));
            } else {
                return op;
            }
        }

        // If we are trying to Lower bound a Max, then we can take either of the two sides of a Max,
        // so choose the relevant one if possible.
        int32_t original_count = unbounded_vars;

        Expr a_new = mutate(op->a);
        int32_t a_count = unbounded_vars;

        // short circuit if a contains at least one unbounded var
        if (a_count > original_count) {
            unbounded_vars = original_count;
            return mutate(op->b);
        }

        // Otherwise try to get rid of b

        Expr b_new = mutate(op->b);
        int32_t b_count = unbounded_vars;

        // Check if b contains at least one unbounded var
        if (b_count > a_count) {
            unbounded_vars = a_count;
            return a_new;
        }

        // No luck, return the mutated Min
        if (!a_new.same_as(op->a) || !b_new.same_as(op->b)) {
            return Max::make(std::move(a_new), std::move(b_new));
        } else {
            return op;
        }
    }

    // TODO: we might want to disable some IR types.

public:
    StripUnboundedTerms(Direction _direction, const Scope<Interval> *_scope_ptr,
                              std::map<std::string, int> &_var_uses)
      : direction(_direction), scope_ptr(_scope_ptr), var_uses(_var_uses) {}

};

} // namespace


Expr push_rationals(const Expr &expr, const Direction direction) {
    if (expr.type() == Int(32)) {
      return handle_push_none(expr, direction);
    } else {
      return expr;
    }
}


Expr strip_unbounded_terms(const Expr &expr, Direction direction, const Scope<Interval> &scope) {
    std::map<std::string, int> var_uses;
    CountVarUses counter(var_uses);
    expr.accept(&counter);
    StripUnboundedTerms tool(direction, &scope, var_uses);
    return tool.mutate(expr);
}

}  // namespace Internal
}  // namespace Halide
