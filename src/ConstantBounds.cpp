#include "ConstantBounds.h"
#include "Bounds.h"
#include "Error.h"
#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "SimplifyCorrelatedDifferences.h"

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
                // (c0 * x) / c1 when c0 % c1 == 0 -> x * (c0 / c1)
                const int64_t new_factor = div_imp(constant->value, denom);
                if (new_factor == 1) {
                    return handle_push_none(op->a, new_direction);
                } else {
                    return handle_push_mul(op->a, new_direction, new_factor);
                }
            } else if ((denom % constant->value) == 0 && constant->value > 0) {
                // (c0 * x) / c1 when c1 % c0 == 0 and c1 > 0 -> x / (c1 / c0)
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
    CountVarUses(std::map<std::string, int> &_var_uses)
        : var_uses(_var_uses) {
    }
};

class StripUnboundedTerms : public IRMutator {
    Direction direction;
    const Scope<Interval> *scope_ptr;
    const std::map<std::string, int> &var_uses;
    int32_t unbounded_vars = 0;
    void flip_direction() {
        direction = flip(direction);
    }

    using IRMutator::visit;

    // Do nothing for constants.
    // Don't recurse on casts, casts can bound the size of an Expr.
    Expr visit(const Cast *op) override {
        return op;
    }

    Expr visit(const Variable *op) override {
        // A variable is unbounded if it appears only once *and* has no constant bounds.
        const auto iter = var_uses.find(op->name);
        internal_assert(iter != var_uses.end()) << "Encountered uncounted variable: " << Expr(op) << "\n";
        const int uses = iter->second;
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

    Expr visit(const Select *op) override {
        // Do not visit condition, don't care about those variables.
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        if (true_value.same_as(op->true_value) &&
            false_value.same_as(op->false_value)) {
            return op;
        }
        return Select::make(op->condition, std::move(true_value), std::move(false_value));
    }

    // Do nothing for Loads, Ramps, Broadcasts, Calls, Shuffles, VectorReduce
    Expr visit(const Load *op) override {
        return op;
    }

    Expr visit(const Ramp *op) override {
        return op;
    }

    Expr visit(const Broadcast *op) override {
        return op;
    }

    Expr visit(const Call *op) override {
        return op;
    }

    Expr visit(const Shuffle *op) override {
        return op;
    }

    Expr visit(const VectorReduce *op) override {
        return op;
    }

public:
    StripUnboundedTerms(Direction _direction, const Scope<Interval> *_scope_ptr,
                        const std::map<std::string, int> &_var_uses)
        : direction(_direction), scope_ptr(_scope_ptr), var_uses(_var_uses) {
    }
};

class ReorderTerms : public IRGraphMutator {
    using IRGraphMutator::visit;

    // Directly taken from Simplify_Internal.h
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
        int coefficient;
    };

    AffineTerm negate(const AffineTerm &term) {
        return AffineTerm{term.expr, -term.coefficient};
    }

    bool is_mul_by_int_const(const Expr &expr, AffineTerm &term) {
        const Mul *mul = expr.as<Mul>();
        if (mul) {
            // Assume constant is on the RHS due to simplification.
            const IntImm *b = mul->b.as<IntImm>();
            if (b) {
                term.coefficient = b->value;
                term.expr = mul->a;
                return true;
            }
        }
        return false;
    }

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
                next.expr = mutate(next.expr);

                if (next.expr.as<Add>() || next.expr.as<Sub>()) {
                    // After mutation it became an add or sub, throw it back on the pending queue.
                    pending.push_back(next);
                } else {
                    AffineTerm term;
                    if (is_mul_by_int_const(next.expr, term)) {
                        internal_assert(term.expr.defined());
                        term.coefficient *= next.coefficient;
                        pending.push_back(term);
                    } else {
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

    // Two-finger O(n) algorithm for simplifying sums
    std::vector<AffineTerm> simplify_linear_summation(const std::vector<AffineTerm> &terms) {
        if (terms.empty()) {
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
            return mutate(scope.get(op->name));
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

}  // namespace

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

Expr reorder_terms(const Expr &expr) {
    return ReorderTerms().mutate(expr);
}

Expr substitute_some_lets(const Expr &expr, size_t count = 100) {
    return SubstituteSomeLets(count).mutate(expr);
}

Expr approximate_optimizations(const Expr &expr, Direction direction, const Scope<Interval> &scope) {
    Expr simpl = substitute_some_lets(expr);
    simpl = simplify(reorder_terms(simpl));
    // TODO: only do push_rationals if correlated divisions exist.
    simpl = push_rationals(simpl, direction);
    simpl = simplify(simpl);
    simpl = strip_unbounded_terms(simpl, direction, scope);
    return simplify(simpl);
}

bool possibly_correlated(const Expr &expr) {
    // TODO: find a better (cheaper) way to detect possible correlations
    std::map<std::string, int> var_uses;
    CountVarUses counter(var_uses);
    expr.accept(&counter);
    for (const auto &iter : var_uses) {
        if (iter.second > 1) {
            return true;
        }
    }
    return false;
}

Interval approximate_constant_bounds(const Expr &expr, const Scope<Interval> &scope) {
    Interval interval;
    if (!is_const(expr) && expr.type() == Int(32) && possibly_correlated(expr)) {
        Expr lower = approximate_optimizations(expr, Direction::Lower, scope);
        Expr upper = approximate_optimizations(expr, Direction::Upper, scope);
        interval.min = bounds_of_expr_in_scope(lower, scope, FuncValueBounds(), true).min;
        interval.max = bounds_of_expr_in_scope(upper, scope, FuncValueBounds(), true).max;
        interval.min = simplify(interval.min);
        interval.max = simplify(interval.max);
        make_const_interval(interval);
    }
    return interval;
}

Expr find_constant_bound(const Expr &e, Direction d, const Scope<Interval> &scope) {
    Interval interval = find_constant_bounds(e, scope);
    Expr bound;
    if (interval.has_lower_bound() && (d == Direction::Lower)) {
        bound = interval.min;
    } else if (interval.has_upper_bound() && (d == Direction::Upper)) {
        bound = interval.max;
    }
    return bound;
}

static bool disable_approximate_methods() {
    return get_env_variable("HL_DISABLE_APPROX_CBOUNDS") == "1";
}

Interval find_constant_bounds(const Expr &e, const Scope<Interval> &scope) {
    Expr expr = bound_correlated_differences(simplify(remove_likelies(e)));
    Interval interval = bounds_of_expr_in_scope(expr, scope, FuncValueBounds(), true);
    interval.min = simplify(interval.min);
    interval.max = simplify(interval.max);
    make_const_interval(interval);

    if (!disable_approximate_methods()) {
        Interval approx_interval = approximate_constant_bounds(expr, scope);
        // Take the interesection of the previous method and the aggresive method.
        interval.min = Interval::make_max(interval.min, approx_interval.min);
        interval.max = Interval::make_min(interval.max, approx_interval.max);
    }

    return interval;
}

}  // namespace Internal
}  // namespace Halide
