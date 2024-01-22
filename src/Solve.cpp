#include "Solve.h"

#include "CSE.h"
#include "ConciseCasts.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;

namespace {

// Returns true iff t is an integral type where overflow is undefined
bool no_overflow_int(Type t) {
    return t.is_int() && t.bits() >= 32;
}

/** A mutator that moves all instances of a free variable as far left
 * and as far outermost as possible. See the test cases at the bottom
 * of this file.
 *
 * This mutator substitutes in lets. This means two things:
 * 1) The mutate method must cache partial results
 * 2) Users of this had better immediately run
 * common-subexpression-elimination. Fortunately this isn't a
 * public class, so the only user is in this file.
 */
class SolveExpression : public IRMutator {
public:
    SolveExpression(const string &v, const Scope<Expr> &es)
        : var(v), external_scope(es) {
    }

    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        map<Expr, CacheEntry, ExprCompare>::iterator iter = cache.find(e);
        if (iter == cache.end()) {
            // Not in the cache, call the base class version.
            debug(4) << "Mutating " << e << " (" << uses_var << ", " << failed << ")\n";
            bool old_uses_var = uses_var;
            uses_var = false;
            bool old_failed = failed;
            failed = false;
            Expr new_e = IRMutator::mutate(e);
            CacheEntry entry = {new_e, uses_var, failed};
            uses_var = old_uses_var || uses_var;
            failed = old_failed || failed;
            cache[e] = entry;
            debug(4) << "(Miss) Rewrote " << e << " -> " << new_e << " (" << uses_var << ", " << failed << ")\n";
            return new_e;
        } else {
            // Cache hit.
            uses_var = uses_var || iter->second.uses_var;
            failed = failed || iter->second.failed;
            debug(4) << "(Hit) Rewrote " << e << " -> " << iter->second.expr << " (" << uses_var << ")\n";
            return iter->second.expr;
        }
    }

    // Has the solve failed.
    bool failed = false;

private:
    // The variable we're solving for.
    string var;

    // Whether or not the just-mutated expression uses the variable.
    bool uses_var = false;

    // A cache of mutated results. Fortunately the mutator is
    // stateless, so we can cache everything.
    struct CacheEntry {
        Expr expr;
        bool uses_var, failed;
    };
    map<Expr, CacheEntry, ExprCompare> cache;

    // Internal lets. Already mutated.
    Scope<CacheEntry> scope;

    // External lets.
    const Scope<Expr> &external_scope;

    // Return the negative of an expr. Does some eager simplification
    // to avoid injecting pointless -1s.
    Expr negate(const Expr &e) {
        internal_assert(!e.type().is_uint()) << "Negating unsigned is not legal\n";
        const Mul *mul = e.as<Mul>();
        if (mul && is_const(mul->b)) {
            return mul->a * simplify(-1 * mul->b);
        } else {
            return e * -1;
        }
    }

    // The invariant here is that for all the nodes we peephole
    // recognize in each visitor, recursively calling mutate has
    // already moved the part that contains the variable to the left,
    // so the right of the subexpression can be considered a
    // constant. The mutator must preserve this property or set the
    // flag "failed" to true.
    using IRMutator::visit;

    // Admit defeat. Isolated in a method for ease of debugging.
    Expr fail(const Expr &e) {
        debug(3) << "Failed to solve: " << e << "\n";
        failed = true;
        return Expr();
    }

    Expr visit(const Add *op) override {
        bool old_uses_var = uses_var;
        uses_var = false;
        bool old_failed = failed;
        failed = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;
        bool a_failed = failed;

        uses_var = false;
        failed = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        bool b_failed = failed;
        uses_var = old_uses_var || a_uses_var || b_uses_var;
        failed = old_failed || a_failed || b_failed;

        if (b_uses_var && !a_uses_var) {
            std::swap(a, b);
            std::swap(a_uses_var, b_uses_var);
            std::swap(a_failed, b_failed);
        }

        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();

        Expr expr;

        if (a_uses_var && !b_uses_var) {
            if (sub_a && !a_failed) {
                // (f(x) - a) + b -> f(x) + (b - a)
                expr = mutate(sub_a->a + (b - sub_a->b));
            } else if (add_a && !a_failed) {
                // (f(x) + a) + b -> f(x) + (a + b)
                expr = mutate(add_a->a + (add_a->b + b));
            }
        } else if (a_uses_var && b_uses_var) {
            if (equal(a, b)) {
                expr = mutate(a * 2);
            } else if (add_a && !a_failed) {
                // (f(x) + a) + g(x) -> (f(x) + g(x)) + a
                expr = mutate((add_a->a + b) + add_a->b);
            } else if (add_b && !b_failed) {
                // f(x) + (g(x) + a) -> (f(x) + g(x)) + a
                expr = mutate((a + add_b->a) + add_b->b);
            } else if (sub_a && !a_failed) {
                // (f(x) - a) + g(x) -> (f(x) + g(x)) - a
                expr = mutate((sub_a->a + b) - sub_a->b);
            } else if (sub_b && !b_failed) {
                // f(x) + (g(x) - a) -> (f(x) + g(x)) - a
                expr = mutate((a + sub_b->a) - sub_b->b);
            } else if (mul_a && mul_b && equal(mul_a->a, mul_b->a)) {
                // f(x)*a + f(x)*b -> f(x)*(a + b)
                expr = mutate(mul_a->a * (mul_a->b + mul_b->b));
            } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b)) {
                // f(x)*a + g(x)*a -> (f(x) + g(x))*a;
                expr = mutate(mul_a->a + mul_b->a) * mul_a->b;
            } else if (mul_a && equal(mul_a->a, b)) {
                // f(x)*a + f(x) -> f(x) * (a + 1)
                expr = mutate(b * (mul_a->b + 1));
            } else if (mul_b && equal(mul_b->a, a)) {
                // f(x) + f(x)*a -> f(x) * (a + 1)
                expr = mutate(a * (mul_b->b + 1));
            } else if (div_a && !a_failed) {
                // f(x)/a + g(x) -> (f(x) + g(x) * a) / b
                expr = mutate((div_a->a + b * div_a->b) / div_a->b);
            } else if (div_b && !b_failed) {
                // f(x) + g(x)/b -> (f(x) * b + g(x)) / b
                expr = mutate((a * div_b->b + div_b->a) / div_b->b);
            } else {
                expr = fail(a + b);
            }
        } else {
            // Do some constant-folding
            if (is_const(a) && is_const(b)) {
                expr = simplify(a + b);
            }
        }

        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = a + b;
            }
        }
        return expr;
    }

    Expr visit(const Sub *op) override {
        bool old_uses_var = uses_var;
        uses_var = false;
        bool old_failed = failed;
        failed = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;
        bool a_failed = failed;

        uses_var = false;
        failed = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        bool b_failed = failed;
        uses_var = old_uses_var || a_uses_var || b_uses_var;
        failed = old_failed || a_failed || b_failed;

        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Div *div_a = a.as<Div>();

        Expr expr;

        if (a_uses_var && !b_uses_var) {
            if (sub_a && !a_failed) {
                // (f(x) - a) - b -> f(x) - (a + b)
                expr = mutate(sub_a->a - (sub_a->b + b));
            } else if (add_a && !a_failed) {
                // (f(x) + a) - b -> f(x) + (a - b)
                expr = mutate(add_a->a + (add_a->b - b));
            }
        } else if (b_uses_var && !a_uses_var) {
            if (op->type.is_uint()) {
                if (sub_b && b_failed) {
                    // a - (b - f(x)) -> f(x) + (a - b)
                    failed = old_failed || a_failed;
                    expr = mutate(sub_b->b + (a - sub_b->a));
                } else {
                    // Negating unsigned is not legal
                    expr = fail(a - b);
                }
            } else if (sub_b && !b_failed) {
                // a - (f(x) - b) -> -f(x) + (a + b)
                expr = mutate(negate(sub_b->a) + (a + sub_b->b));
            } else if (add_b && !b_failed) {
                // a - (f(x) + b) -> -f(x) + (a - b)
                expr = mutate(negate(add_b->a) + (a - add_b->b));
            } else {
                expr = mutate(negate(b) + a);
            }
        } else if (a_uses_var && b_uses_var) {
            if (add_a && !a_failed) {
                // (f(x) + a) - g(x) -> (f(x) - g(x)) + a
                expr = mutate(add_a->a - b + add_a->b);
            } else if (add_b && !b_failed) {
                // f(x) - (g(x) + a) -> (f(x) - g(x)) - a
                expr = mutate(a - add_b->a - add_b->b);
            } else if (sub_a && !a_failed) {
                // (f(x) - a) - g(x) -> (f(x) - g(x)) - a
                expr = mutate(sub_a->a - b - sub_a->b);
            } else if (sub_b && !b_failed) {
                // f(x) - (g(x) - a) -> (f(x) - g(x)) + a
                expr = mutate(a - sub_b->a + sub_b->b);
            } else if (mul_a && mul_b && equal(mul_a->a, mul_b->a)) {
                // f(x)*a - f(x)*b -> f(x)*(a - b)
                expr = mutate(mul_a->a * (mul_a->b - mul_b->b));
            } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b)) {
                // f(x)*a - g(x)*a -> (f(x) - g(x))*a;
                expr = mutate((mul_a->a - mul_b->a) * mul_a->b);
            } else if (div_a && !a_failed) {
                // f(x)/a - g(x) -> (f(x) - g(x) * a) / b
                expr = mutate((div_a->a - b * div_a->b) / div_a->b);
            } else {
                expr = fail(a - b);
            }
        } else {
            // Do some constant-folding
            if (is_const(a) && is_const(b)) {
                expr = simplify(a - b);
            }
        }

        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = a - b;
            }
        }
        return expr;
    }

    Expr visit(const Mul *op) override {
        bool old_uses_var = uses_var;
        uses_var = false;
        bool old_failed = failed;
        failed = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;
        bool a_failed = failed;

        internal_assert(!is_const(op->a) || !a_uses_var) << op->a << ", " << uses_var << "\n";

        uses_var = false;
        failed = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        bool b_failed = failed;
        uses_var = old_uses_var || a_uses_var || b_uses_var;
        failed = old_failed || a_failed || b_failed;

        if (b_uses_var && !a_uses_var) {
            std::swap(a, b);
            std::swap(a_uses_var, b_uses_var);
            std::swap(a_failed, b_failed);
        }

        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Mul *mul_a = a.as<Mul>();

        Expr expr;
        if (a_uses_var && !b_uses_var) {
            if (add_a && !a_failed) {
                // (f(x) + a) * b -> f(x) * b + a * b
                expr = mutate(add_a->a * b + add_a->b * b);
            } else if (sub_a && !a_failed) {
                // (f(x) - a) * b -> f(x) * b - a * b
                expr = mutate(sub_a->a * b - sub_a->b * b);
            } else if (mul_a && !a_failed) {
                // (f(x) * a) * b -> f(x) * (a * b)
                expr = mutate(mul_a->a * (mul_a->b * b));
            }
        } else if (a_uses_var && b_uses_var) {
            // It's a quadratic. We could continue but this is
            // unlikely to ever occur. Code will be added here as
            // these cases actually pop up.
            expr = fail(a * b);
        } else if (is_const(a) && is_const(b)) {
            // Do some constant-folding
            expr = simplify(a * b);
            internal_assert(!uses_var && !a_uses_var && !b_uses_var);
        }

        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = a * b;
            }
        }

        return expr;
    }

    Expr visit(const Div *op) override {
        bool old_uses_var = uses_var;
        uses_var = false;
        bool old_failed = failed;
        failed = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;
        bool a_failed = failed;
        internal_assert(!is_const(op->a) || !a_uses_var)
            << op->a << ", " << uses_var << "\n";
        uses_var = false;
        failed = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        bool b_failed = failed;
        uses_var = old_uses_var || a_uses_var || b_uses_var;
        failed = old_failed || a_failed || b_failed;
        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        Expr expr;
        if (a_uses_var && !b_uses_var) {
            const int64_t *ib = as_const_int(b);
            auto is_multiple_of_b = [&](const Expr &e) {
                if (ib) {
                    int64_t r = 0;
                    return reduce_expr_modulo(e, *ib, &r) && r == 0;
                } else {
                    return can_prove(e / b * b == e);
                }
            };
            if (add_a && !a_failed &&
                is_multiple_of_b(add_a->a)) {
                // (f(x) + a) / b -> f(x) / b + a / b
                expr = mutate(simplify(add_a->a / b) + add_a->b / b);
            } else if (sub_a && !a_failed &&
                       is_multiple_of_b(sub_a->a)) {
                // (f(x) - a) / b -> f(x) / b - a / b
                expr = mutate(simplify(sub_a->a / b) - sub_a->b / b);
            } else if (mul_a && !a_failed && no_overflow_int(op->type) &&
                       is_multiple_of_b(mul_a->b)) {
                // (f(x) * a) / b -> f(x) * (a / b)
                expr = mutate(mul_a->a * (mul_a->b / b));
            }
        } else if (is_const(a) && is_const(b)) {
            // Do some constant-folding
            expr = simplify(a / b);
            internal_assert(!uses_var && !a_uses_var && !b_uses_var);
        }
        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = a / b;
            }
        }
        return expr;
    }

    Expr visit(const Call *op) override {
        // Ignore intrinsics that shouldn't affect the results.
        if (Call::as_tag(op)) {
            return mutate(op->args[0]);
        } else {
            return IRMutator::visit(op);
        }
    }

    template<typename T, typename Other>
    Expr visit_min_max_op(const T *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        bool old_failed = failed;
        failed = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;
        bool a_failed = failed;

        uses_var = false;
        failed = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        bool b_failed = failed;
        uses_var = old_uses_var || a_uses_var || b_uses_var;
        failed = old_failed || a_failed || b_failed;

        if (b_uses_var && !a_uses_var) {
            std::swap(a, b);
            std::swap(a_uses_var, b_uses_var);
            std::swap(a_failed, b_failed);
        }

        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const T *t_a = a.as<T>();
        const T *t_b = b.as<T>();

        Expr expr;

        if (a_uses_var && !b_uses_var) {
            if (t_a && !a_failed) {
                // op(op(f(x), a), b) -> op(f(x), op(a, b))
                expr = mutate(T::make(t_a->a, T::make(t_a->b, b)));
            }
        } else if (a_uses_var && b_uses_var) {
            if (equal(a, b)) {
                // op(f(x), f(x)) -> f(x)
                expr = a;
            } else if (t_a && !a_failed) {
                // op(op(f(x), a), g(x)) -> op(op(f(x), g(x)), a)
                expr = mutate(T::make(T::make(t_a->a, b), t_a->b));
            } else if (t_b && !b_failed) {
                // op(f(x), op(g(x), a)) -> op(op(f(x), g(x)), a)
                expr = mutate(T::make(T::make(a, t_b->a), t_b->b));
            } else if (add_a && add_b && equal(add_a->a, add_b->a)) {
                // op(f(x) + a, f(x) + b) -> f(x) + op(a, b)
                expr = mutate(add_a->a + T::make(add_a->b, add_b->b));
            } else if (add_a && add_b && equal(add_a->b, add_b->b)) {
                // op(f(x) + a, g(x) + a) -> op(f(x), g(x)) + a;
                expr = mutate(T::make(add_a->a, add_b->a)) + add_a->b;
            } else if (add_a && equal(add_a->a, b)) {
                // op(f(x) + a, f(x)) -> f(x) + op(a, 0)
                expr = mutate(b + T::make(add_a->b, make_zero(op->type)));
            } else if (add_b && equal(add_b->a, a)) {
                // op(f(x), f(x) + a) -> f(x) + op(a, 0)
                expr = mutate(a + T::make(add_b->b, make_zero(op->type)));
            } else if (sub_a && sub_b && equal(sub_a->a, sub_b->a)) {
                // min(f(x) - a, f(x) - b) -> f(x) - max(a, b)
                expr = mutate(sub_a->a - Other::make(sub_a->b, sub_b->b));
            } else if (sub_a && add_b && equal(sub_a->a, add_b->a)) {
                // min(f(x) - a, f(x) + b) -> f(x) + min(0 - a, b)
                expr = mutate(sub_a->a + T::make(make_zero(op->type) - sub_a->b, add_b->b));
            } else if (add_a && sub_b && equal(add_a->a, sub_b->a)) {
                // min(f(x) + a, f(x) - b) -> f(x) + min(a, 0 - b)
                expr = mutate(add_a->a + T::make(add_a->b, make_zero(op->type) - sub_b->b));
            } else if (sub_a && sub_b && equal(sub_a->b, sub_b->b)) {
                // op(f(x) - a, g(x) - a) -> op(f(x), g(x)) - a
                expr = mutate(T::make(sub_a->a, sub_b->a)) - sub_a->b;
            } else if (sub_a && equal(sub_a->a, b)) {
                // op(f(x) - a, f(x)) -> f(x) - op(a, 0)
                expr = mutate(b - Other::make(sub_a->b, make_zero(op->type)));
            } else if (sub_b && equal(sub_b->a, a)) {
                // op(f(x), f(x) - a) -> f(x) - op(a, 0)
                expr = mutate(a - Other::make(sub_b->b, make_zero(op->type)));
            } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b) && is_positive_const(mul_a->b)) {
                // Positive a: min(f(x)*a, g(x)*a) -> min(f(x), g(x))*a
                //             max(f(x)*a, g(x)*a) -> max(f(x), g(x))*a
                expr = mutate(T::make(mul_a->a, mul_b->a)) * mul_a->b;
            } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b) && is_negative_const(mul_a->b)) {
                // Negative a: min(f(x)*a, g(x)*a) -> max(f(x), g(x))*a
                expr = mutate(Other::make(mul_a->a, mul_b->a)) * mul_a->b;
            } else {
                expr = fail(T::make(a, b));
            }
        } else {
            // Do some constant-folding
            if (is_const(a) && is_const(b)) {
                expr = simplify(T::make(a, b));
            }
        }

        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = T::make(a, b);
            }
        }
        return expr;
    }

    Expr visit(const Min *op) override {
        return visit_min_max_op<Min, Max>(op);
    }

    Expr visit(const Max *op) override {
        return visit_min_max_op<Max, Min>(op);
    }

    template<typename T>
    Expr visit_and_or_op(const T *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        bool old_failed = failed;
        failed = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;
        bool a_failed = failed;

        uses_var = false;
        failed = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        bool b_failed = failed;
        uses_var = old_uses_var || a_uses_var || b_uses_var;
        failed = old_failed || a_failed || b_failed;

        if (b_uses_var && !a_uses_var) {
            std::swap(a, b);
            std::swap(a_uses_var, b_uses_var);
            std::swap(a_failed, b_failed);
        }

        const T *t_a = a.as<T>();
        const T *t_b = b.as<T>();

        Expr expr;

        if (a_uses_var && !b_uses_var) {
            if (t_a && !a_failed) {
                // op(op(f(x), a), b) -> op(f(x), op(a, b))
                expr = mutate(T::make(t_a->a, T::make(t_a->b, b)));
            }
        } else if (a_uses_var && b_uses_var) {
            if (equal(a, b)) {
                // op(f(x), f(x)) -> f(x)
                expr = a;
            } else if (t_a && !a_failed) {
                // op(op(f(x), a), g(x)) -> op(op(f(x), g(x)), a)
                expr = mutate(T::make(T::make(t_a->a, b), t_a->b));
            } else if (t_b && !b_failed) {
                // op(f(x), op(g(x), a)) -> op(op(f(x), g(x)), a)
                expr = mutate(T::make(T::make(a, t_b->a), t_b->b));
            } else {
                expr = fail(T::make(a, b));
            }
        } else {
            // Do some constant-folding
            if (is_const(a) && is_const(b)) {
                expr = simplify(T::make(a, b));
            }
        }

        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = T::make(a, b);
            }
        }
        return expr;
    }

    Expr visit(const Or *op) override {
        return visit_and_or_op(op);
    }

    Expr visit(const And *op) override {
        return visit_and_or_op(op);
    }

    template<typename Cmp, typename Opp>
    Expr visit_cmp(const Cmp *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        bool old_failed = failed;
        failed = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;
        bool a_failed = failed;

        uses_var = false;
        failed = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        bool b_failed = failed;
        uses_var = old_uses_var || a_uses_var || b_uses_var;
        failed = old_failed || a_failed || b_failed;

        if (b_uses_var && !a_uses_var) {
            return mutate(Opp::make(b, a));
        }

        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Div *div_a = a.as<Div>();

        bool is_eq = Expr(op).as<EQ>() != nullptr;
        bool is_ne = Expr(op).as<NE>() != nullptr;
        bool is_lt = Expr(op).as<LT>() != nullptr;
        bool is_le = Expr(op).as<LE>() != nullptr;
        bool is_ge = Expr(op).as<GE>() != nullptr;
        bool is_gt = Expr(op).as<GT>() != nullptr;

        Expr expr;

        if (a_uses_var && !b_uses_var) {
            // We have f(x) < y. Try to unwrap f(x)
            if (add_a && !a_failed) {
                // f(x) + b < c -> f(x) < c - b
                expr = mutate(Cmp::make(add_a->a, (b - add_a->b)));
            } else if (sub_a && !a_failed) {
                // f(x) - b < c -> f(x) < c + b
                expr = mutate(Cmp::make(sub_a->a, (b + sub_a->b)));
            } else if (mul_a) {
                if (a.type().is_float()) {
                    // f(x) * b == c -> f(x) == c / b
                    if (is_eq || is_ne || is_positive_const(mul_a->b)) {
                        expr = mutate(Cmp::make(mul_a->a, (b / mul_a->b)));
                    } else if (is_negative_const(mul_a->b)) {
                        expr = mutate(Opp::make(mul_a->a, (b / mul_a->b)));
                    }
                } else if (is_const(mul_a->b, -1)) {
                    expr = mutate(Opp::make(mul_a->a, make_zero(b.type()) - b));
                } else if (is_negative_const(mul_a->b)) {
                    // It shouldn't have been unsigned since the is_negative_const
                    // check is true, but put an assertion anyway.
                    internal_assert(!b.type().is_uint()) << "Negating unsigned is not legal\n";
                    expr = mutate(Opp::make(mul_a->a * negate(mul_a->b), negate(b)));
                } else {
                    // Don't use operator/ and operator % to sneak
                    // past the division-by-zero check. We'll only
                    // actually use these when mul_a->b is a positive
                    // or negative constant.
                    Expr div = Div::make(b, mul_a->b);
                    Expr rem = Mod::make(b, mul_a->b);
                    if (is_eq) {
                        // f(x) * c == b -> f(x) == b/c && b%c == 0
                        expr = mutate((mul_a->a == div) && (rem == 0));
                    } else if (is_ne) {
                        // f(x) * c != b -> f(x) != b/c || b%c != 0
                        expr = mutate((mul_a->a != div) || (rem != 0));
                    } else if (is_positive_const(mul_a->b)) {
                        if (is_le) {
                            expr = mutate(mul_a->a <= div);
                        } else if (is_lt) {
                            expr = mutate(mul_a->a <= (b - 1) / mul_a->b);
                        } else if (is_gt) {
                            expr = mutate(mul_a->a > div);
                        } else if (is_ge) {
                            expr = mutate(mul_a->a > (b - 1) / mul_a->b);
                        }
                    }
                }
            } else if (div_a) {
                if (a.type().is_float()) {
                    if (is_positive_const(div_a->b)) {
                        expr = mutate(Cmp::make(div_a->a, b * div_a->b));
                    } else if (is_negative_const(div_a->b)) {
                        expr = mutate(Opp::make(div_a->a, b * div_a->b));
                    }
                } else if (a.type().is_int() && a.type().bits() >= 32) {
                    if (is_eq || is_ne) {
                        // Can't do anything with this
                    } else if (is_negative_const(div_a->b)) {
                        // It shouldn't have been unsigned since the is_negative_const
                        // check is true, but put an assertion anyway.
                        internal_assert(!a.type().is_uint()) << "Negating unsigned is not legal\n";
                        // With Euclidean division, (a/(-b)) == -(a/b)
                        expr = mutate(Cmp::make(negate(div_a->a / negate(div_a->b)), b));
                    } else if (is_positive_const(div_a->b)) {
                        if (is_lt) {
                            // f(x) / b < c  <==>  f(x) < c * b
                            expr = mutate(div_a->a < b * div_a->b);
                        } else if (is_le) {
                            // f(x) / b <= c  <==>  f(x) < (c + 1) * b
                            expr = mutate(div_a->a < (b + 1) * div_a->b);
                        } else if (is_gt) {
                            // f(x) / b > c  <==>  f(x) >= (c + 1) * b
                            expr = mutate(div_a->a >= (b + 1) * div_a->b);
                        } else if (is_ge) {
                            // f(x) / b >= c  <==>  f(x) >= c * b
                            expr = mutate(div_a->a >= b * div_a->b);
                        }
                    }
                }
            }
        } else if (a_uses_var && b_uses_var && a.type().is_int() && a.type().bits() >= 32) {
            // Convert to f(x) - g(x) == 0 and let the subtract mutator clean up.
            // Only safe if the type is not subject to overflow.
            expr = mutate(Cmp::make(a - b, make_zero(a.type())));
        }

        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = Cmp::make(a, b);
            }
        }
        return expr;
    }

    Expr visit(const LT *op) override {
        return visit_cmp<LT, GT>(op);
    }

    Expr visit(const LE *op) override {
        return visit_cmp<LE, GE>(op);
    }

    Expr visit(const GE *op) override {
        return visit_cmp<GE, LE>(op);
    }

    Expr visit(const GT *op) override {
        return visit_cmp<GT, LT>(op);
    }

    Expr visit(const EQ *op) override {
        return visit_cmp<EQ, EQ>(op);
    }

    Expr visit(const NE *op) override {
        return visit_cmp<NE, NE>(op);
    }

    Expr visit(const Variable *op) override {
        if (op->name == var) {
            uses_var = true;
            return op;
        } else if (scope.contains(op->name)) {
            CacheEntry e = scope.get(op->name);
            uses_var = uses_var || e.uses_var;
            failed = failed || e.failed;
            return e.expr;
        } else if (external_scope.contains(op->name)) {
            Expr e = external_scope.get(op->name);
            // Expressions in the external scope haven't been solved
            // yet. This will either pull its solution from the cache,
            // or solve it and then put it into the cache.
            return mutate(e);
        } else {
            return op;
        }
    }

    Expr visit(const Let *op) override {
        bool old_uses_var = uses_var;
        bool old_failed = failed;
        uses_var = false;
        failed = false;
        Expr value = mutate(op->value);
        CacheEntry e = {value, uses_var, failed};
        uses_var = old_uses_var;
        failed = old_failed;

        ScopedBinding<CacheEntry> bind(scope, op->name, e);
        return mutate(op->body);
    }
};

class SolveForInterval : public IRVisitor {
    // The var we're solving for
    const string &var;

    // Whether we're trying to make the condition true or false
    bool target = true;

    // Whether we want an outer bound or an inner bound
    bool outer;

    // Track lets expressions. Initially empty.
    Scope<Expr> scope;

    // Lazily populated with solved intervals for boolean sub-expressions.
    map<pair<string, bool>, Interval> solved_vars;

    // Has this expression already been rearranged by solve_expression?
    bool already_solved = false;

    using IRVisitor::visit;

    void fail() {
        if (outer) {
            // If we're looking for an outer bound, then return an infinite interval.
            result = Interval::everything();
        } else {
            // If we're looking for an inner bound, return an empty interval
            result = Interval::nothing();
        }
    }

    void visit(const UIntImm *op) override {
        internal_assert(op->type.is_bool());
        if ((op->value && target) ||
            (!op->value && !target)) {
            result = Interval::everything();
        } else if ((!op->value && target) ||
                   (op->value && !target)) {
            result = Interval::nothing();
        } else {
            fail();
        }
    }

    Interval interval_union(Interval ia, Interval ib) const {
        if (outer) {
            // The regular union is already conservative in the right direction
            return Interval::make_union(ia, ib);
        } else {
            // If we can prove there's overlap, we can still use the regular union
            Interval intersection = Interval::make_intersection(ia, ib);
            if (!intersection.is_empty() &&
                (!intersection.is_bounded() ||
                 can_prove(intersection.min <= intersection.max))) {
                return Interval::make_union(ia, ib);
            } else {
                // Just take one of the two sides
                if (ia.is_empty()) {
                    return ib;
                } else {
                    return ia;
                }
            }
        }
    }

    void visit(const And *op) override {
        op->a.accept(this);
        Interval ia = result;
        op->b.accept(this);
        Interval ib = result;
        if (target) {
            debug(3) << "And intersecting: " << Expr(op) << "\n"
                     << "  " << ia.min << " " << ia.max << "\n"
                     << "  " << ib.min << " " << ib.max << "\n";
            result = Interval::make_intersection(ia, ib);
        } else {
            debug(3) << "And union:" << Expr(op) << "\n"
                     << "  " << ia.min << " " << ia.max << "\n"
                     << "  " << ib.min << " " << ib.max << "\n";
            result = interval_union(ia, ib);
        }
    }

    void visit(const Or *op) override {
        op->a.accept(this);
        Interval ia = result;
        op->b.accept(this);
        Interval ib = result;
        if (!target) {
            debug(3) << "Or intersecting:" << Expr(op) << "\n"
                     << "  " << ia.min << " " << ia.max << "\n"
                     << "  " << ib.min << " " << ib.max << "\n";
            result = Interval::make_intersection(ia, ib);
        } else {
            debug(3) << "Or union:" << Expr(op) << "\n"
                     << "  " << ia.min << " " << ia.max << "\n"
                     << "  " << ib.min << " " << ib.max << "\n";
            result = interval_union(ia, ib);
        }
    }

    void visit(const Not *op) override {
        target = !target;
        op->a.accept(this);
        target = !target;
    }

    void visit(const Let *op) override {
        internal_assert(op->type.is_bool());
        // If it's a bool, we might need to know the intervals over
        // which it's definitely or definitely false. We'll do this
        // lazily and populate a map. See the Variable visitor.
        bool uses_var = expr_uses_var(op->value, var) || expr_uses_vars(op->value, scope);
        if (uses_var) {
            scope.push(op->name, op->value);
        }
        op->body.accept(this);
        if (uses_var) {
            scope.pop(op->name);
        }
        if (result.has_lower_bound() && expr_uses_var(result.min, op->name)) {
            result.min = Let::make(op->name, op->value, result.min);
        }
        if (result.has_upper_bound() && expr_uses_var(result.max, op->name)) {
            result.max = Let::make(op->name, op->value, result.max);
        }
    }

    void visit(const Variable *op) override {
        internal_assert(op->type.is_bool());
        if (scope.contains(op->name)) {
            pair<string, bool> key = {op->name, target};
            auto it = solved_vars.find(key);
            if (it != solved_vars.end()) {
                result = it->second;
            } else {
                scope.get(op->name).accept(this);
                solved_vars[key] = result;
            }
        } else {
            fail();
        }
    }

    void visit(const LT *lt) override {
        // Normalize to le
        Expr cond = lt->a <= (lt->b - 1);
        cond.accept(this);
    }

    void visit(const GT *gt) override {
        // Normalize to ge
        Expr cond = gt->a >= (gt->b + 1);
        cond.accept(this);
    }

    // The LE and GE visitors, when applied to min and max nodes,
    // expand into larger expressions that duplicate each term. This
    // can create combinatorially large expressions to solve. The part
    // of each expression that depends on the variable is fixed, there
    // are just many different right-hand-sides. If we solve the
    // expressions once for a symbolic RHS, we can cache and reuse
    // that solution over and over, taming the exponential beast.
    std::map<Expr, Interval, IRDeepCompare> cache_f, cache_t;

    // Solve an expression, or set result to the previously found solution.
    void cached_solve(const Expr &cond) {
        auto &cache = target ? cache_t : cache_f;
        auto it = cache.find(cond);
        if (it == cache.end()) {
            // Cache miss
            already_solved = false;
            cond.accept(this);
            already_solved = true;
            cache[cond] = result;
        } else {
            // Cache hit
            result = it->second;
        }
    }

    void visit(const LE *le) override {
        static string b_name = unique_name('b');
        static string c_name = unique_name('c');

        const Variable *v = le->a.as<Variable>();
        if (!already_solved) {
            SolverResult solved = solve_expression(le, var, scope);
            if (!solved.fully_solved) {
                fail();
            } else {
                already_solved = true;
                solved.result.accept(this);
                already_solved = false;
            }
        } else if (v && v->name == var) {
            if (target) {
                result = Interval(Interval::neg_inf(), le->b);
            } else {
                result = Interval(le->b + 1, Interval::pos_inf());
            }
        } else if (const Max *max_a = le->a.as<Max>()) {
            // Rewrite (max(a, b) <= c) <==> (a <= c && (b <= c || a >= b))
            Expr a = max_a->a, b = max_a->b, c = le->b;

            // To avoid exponential behaviour, make b and c abstract
            // variables, and see if we've solved something like this
            // before...
            Expr b_var = Variable::make(b.type(), b_name);
            Expr c_var = Variable::make(c.type(), c_name);
            cached_solve((a <= c_var) && (b_var <= c_var || a >= b_var));
            if (result.has_lower_bound()) {
                result.min = graph_substitute(b_name, b, result.min);
                result.min = graph_substitute(c_name, c, result.min);
            }
            if (result.has_upper_bound()) {
                result.max = graph_substitute(b_name, b, result.max);
                result.max = graph_substitute(c_name, c, result.max);
            }
        } else if (const Min *min_a = le->a.as<Min>()) {
            // Rewrite (min(a, b) <= c) <==> (a <= c || (b <= c && a >= b))
            Expr a = min_a->a, b = min_a->b, c = le->b;
            Expr b_var = Variable::make(b.type(), b_name);
            Expr c_var = Variable::make(c.type(), c_name);
            cached_solve((a <= c_var) || (b_var <= c_var && a >= b_var));
            if (result.has_lower_bound()) {
                result.min = graph_substitute(b_name, b, result.min);
                result.min = graph_substitute(c_name, c, result.min);
            }
            if (result.has_upper_bound()) {
                result.max = graph_substitute(b_name, b, result.max);
                result.max = graph_substitute(c_name, c, result.max);
            }
        } else {
            fail();
        }
    }

    void visit(const GE *ge) override {
        static string b_name = unique_name('b');
        static string c_name = unique_name('c');

        const Variable *v = ge->a.as<Variable>();
        if (!already_solved) {
            SolverResult solved = solve_expression(ge, var, scope);
            if (!solved.fully_solved) {
                fail();
            } else {
                already_solved = true;
                solved.result.accept(this);
                already_solved = false;
            }
        } else if (v && v->name == var) {
            if (target) {
                result = Interval(ge->b, Interval::pos_inf());
            } else {
                result = Interval(Interval::neg_inf(), ge->b - 1);
            }
        } else if (const Max *max_a = ge->a.as<Max>()) {
            // Rewrite (max(a, b) >= c) <==> (a >= c || (b >= c && a <= b))
            // Also allow re-solving the new equations.
            Expr a = max_a->a, b = max_a->b, c = ge->b;
            Expr b_var = Variable::make(b.type(), b_name);
            Expr c_var = Variable::make(c.type(), c_name);
            cached_solve((a >= c_var) || (b_var >= c_var && a <= b_var));
            if (result.has_lower_bound()) {
                result.min = graph_substitute(b_name, b, result.min);
                result.min = graph_substitute(c_name, c, result.min);
            }
            if (result.has_upper_bound()) {
                result.max = graph_substitute(b_name, b, result.max);
                result.max = graph_substitute(c_name, c, result.max);
            }
        } else if (const Min *min_a = ge->a.as<Min>()) {
            // Rewrite (min(a, b) >= c) <==> (a >= c && (b >= c || a <= b))
            Expr a = min_a->a, b = min_a->b, c = ge->b;
            Expr b_var = Variable::make(b.type(), b_name);
            Expr c_var = Variable::make(c.type(), c_name);
            cached_solve((a >= c_var) && (b_var >= c_var || a <= b_var));
            if (result.has_lower_bound()) {
                result.min = graph_substitute(b_name, b, result.min);
                result.min = graph_substitute(c_name, c, result.min);
            }
            if (result.has_upper_bound()) {
                result.max = graph_substitute(b_name, b, result.max);
                result.max = graph_substitute(c_name, c, result.max);
            }
        } else {
            fail();
        }
    }

    void visit(const EQ *op) override {
        Expr cond;
        if (op->a.type().is_bool()) {
            internal_assert(op->a.type().is_bool() == op->b.type().is_bool());
            // Boolean (A == B) <=> (A and B) || (~A and ~B)
            cond = (op->a && op->b) && (!op->a && !op->b);
        } else {
            // Normalize to le and ge
            cond = (op->a <= op->b) && (op->a >= op->b);
        }
        cond.accept(this);
    }

    void visit(const NE *op) override {
        Expr cond;
        if (op->a.type().is_bool()) {
            internal_assert(op->a.type().is_bool() == op->b.type().is_bool());
            // Boolean (A != B) <=> (A and ~B) || (~A and B)
            cond = (op->a && !op->b) && (!op->a && op->b);
        } else {
            // Normalize to lt and gt
            cond = (op->a < op->b) || (op->a > op->b);
        }
        cond.accept(this);
    }

    // Other unhandled sources of bools
    void visit(const Cast *op) override {
        fail();
    }

    void visit(const Reinterpret *op) override {
        fail();
    }

    void visit(const Load *op) override {
        fail();
    }

    void visit(const Call *op) override {
        fail();
    }

public:
    Interval result;

    SolveForInterval(const string &v, bool o)
        : var(v), outer(o) {
    }
};

}  // Anonymous namespace

SolverResult solve_expression(const Expr &e, const std::string &variable, const Scope<Expr> &scope) {
    SolveExpression solver(variable, scope);
    Expr new_e = solver.mutate(e);
    // The process has expanded lets. Re-collect them.
    new_e = common_subexpression_elimination(new_e);
    debug(3) << "Solved expr for " << variable << " :\n"
             << "  " << e << "\n"
             << "  " << new_e << "\n";
    return {new_e, !solver.failed};
}

Interval solve_for_inner_interval(const Expr &c, const std::string &var) {
    SolveForInterval s(var, false);
    c.accept(&s);
    internal_assert(s.result.min.defined() && s.result.max.defined())
        << "solve_for_inner_interval returned undefined Exprs: " << c << "\n";
    s.result.min = simplify(common_subexpression_elimination(s.result.min));
    s.result.max = simplify(common_subexpression_elimination(s.result.max));
    if (s.result.is_bounded() &&
        can_prove(s.result.min > s.result.max)) {
        return Interval::nothing();
    }
    return s.result;
}

Interval solve_for_outer_interval(const Expr &c, const std::string &var) {
    SolveForInterval s(var, true);
    c.accept(&s);
    internal_assert(s.result.min.defined() && s.result.max.defined())
        << "solve_for_outer_interval returned undefined Exprs: " << c << "\n";
    s.result.min = simplify(common_subexpression_elimination(s.result.min));
    s.result.max = simplify(common_subexpression_elimination(s.result.max));
    if (s.result.is_bounded() &&
        can_prove(s.result.min > s.result.max)) {
        return Interval::nothing();
    }
    return s.result;
}

Expr and_condition_over_domain(const Expr &e, const Scope<Interval> &varying) {
    internal_assert(e.type().is_bool()) << "Expr provided to and_condition_over_domain is not boolean: " << e << "\n";
    Interval bounds = bounds_of_expr_in_scope(e, varying);
    internal_assert(bounds.has_lower_bound()) << "Failed to produce bound on boolean value in and_condition_over_domain" << e << "\n";
    // Minimum of a boolean value is sufficient condition, implies expression.
    return simplify(bounds.min);
}

// Testing code

namespace {

void check_solve(const Expr &a, const Expr &b) {
    SolverResult solved = solve_expression(a, "x");
    internal_assert(equal(solved.result, b))
        << "Expression: " << a << "\n"
        << " solved to " << solved.result << "\n"
        << " instead of " << b << "\n";
}

void check_interval(const Expr &a, const Interval &i, bool outer) {
    Interval result =
        outer ? solve_for_outer_interval(a, "x") : solve_for_inner_interval(a, "x");
    result.min = simplify(result.min);
    result.max = simplify(result.max);
    internal_assert(equal(result.min, i.min) && equal(result.max, i.max))
        << "Expression " << a << " solved to the interval:\n"
        << "  min: " << result.min << "\n"
        << "  max: " << result.max << "\n"
        << " instead of:\n"
        << "  min: " << i.min << "\n"
        << "  max: " << i.max << "\n";
}

void check_outer_interval(const Expr &a, const Expr &min, const Expr &max) {
    check_interval(a, Interval(min, max), true);
}

void check_inner_interval(const Expr &a, const Expr &min, const Expr &max) {
    check_interval(a, Interval(min, max), false);
}

void check_and_condition(const Expr &orig, const Expr &result, const Interval &i) {
    Scope<Interval> s;
    s.push("x", i);
    Expr cond = and_condition_over_domain(orig, s);
    internal_assert(equal(cond, result))
        << "Expression " << orig
        << " reduced to " << cond
        << " instead of " << result << "\n";
}
}  // namespace

void solve_test() {
    using ConciseCasts::i16;

    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    Expr z = Variable::make(Int(32), "z");

    // Check some simple cases
    check_solve(3 - 4 * x, x * (-4) + 3);
    check_solve(min(5, x), min(x, 5));
    check_solve(max(5, (5 + x) * y), max(x * y + 5 * y, 5));
    check_solve(5 * y + 3 * x == 2, ((x == ((2 - (5 * y)) / 3)) && (((2 - (5 * y)) % 3) == 0)));
    check_solve(min(min(z, x), min(x, y)), min(x, min(y, z)));
    check_solve(min(x + y, x + 5), x + min(y, 5));

    // Check solver with expressions containing division
    check_solve(x + (x * 2) / 2, x * 2);
    check_solve(x + (x * 2 + y) / 2, x * 2 + (y / 2));
    check_solve(x + (x * 2 - y) / 2, x * 2 - (y / 2));
    check_solve(x + (-(x * 2) / 2), x * 0 + 0);
    check_solve(x + (-(x * 2 + -3)) / 2, x * 0 + 1);
    check_solve(x + (z - (x * 2 + -3)) / 2, x * 0 + (z - (-3)) / 2);
    check_solve(x + (y * 16 + (z - (x * 2 + -1))) / 2,
                (x * 0) + (((z - -1) + (y * 16)) / 2));

    check_solve((x * 9 + 3) / 4 - x * 2, (x * 1 + 3) / 4);
    check_solve((x * 9 + 3) / 4 + x * 2, (x * 17 + 3) / 4);
    check_solve(x * 2 + (x * 9 + 3) / 4, (x * 17 + 3) / 4);

    // Check the solver doesn't perform transformations that change integer overflow behavior.
    check_solve(i16(x + y) * i16(2) / i16(2), i16(x + y) * i16(2) / i16(2));

    // A let statement
    check_solve(Let::make("z", 3 + 5 * x, y + z < 8),
                x <= (((8 - (3 + y)) - 1) / 5));

    // A let statement where the variable gets used twice.
    check_solve(Let::make("z", 3 + 5 * x, y + (z + z) < 8),
                x <= (((8 - (6 + y)) - 1) / 10));

    // Something where we expect a let in the output.
    {
        Expr e = y + 1;
        for (int i = 0; i < 10; i++) {
            e *= (e + 1);
        }
        SolverResult solved = solve_expression(x + e < e * e, "x");
        internal_assert(solved.fully_solved && solved.result.as<Let>());
    }

    // Solving inequalities for integers is a pain to get right with
    // all the rounding rules. Check we didn't make a mistake with
    // brute force.
    for (int den = -3; den <= 3; den++) {
        if (den == 0) {
            continue;
        }
        for (int num = 5; num <= 10; num++) {
            Expr in[] = {x * den<num, x * den <= num, x * den == num, x * den != num, x * den >= num, x * den> num,
                         x / den<num, x / den <= num, x / den == num, x / den != num, x / den >= num, x / den> num};
            for (const auto &e : in) {
                SolverResult solved = solve_expression(e, "x");
                internal_assert(solved.fully_solved) << "Error: failed to solve for x in " << e << "\n";
                Expr out = simplify(solved.result);
                for (int i = -10; i < 10; i++) {
                    Expr in_val = substitute("x", i, e);
                    Expr out_val = substitute("x", i, out);
                    in_val = simplify(in_val);
                    out_val = simplify(out_val);
                    internal_assert(equal(in_val, out_val))
                        << "Error: "
                        << e << " is not equivalent to "
                        << out << " when x == " << i << "\n";
                }
            }
        }
    }

    // Check for combinatorial explosion
    Expr e = x + y;
    for (int i = 0; i < 20; i++) {
        e += (e + 1) * y;
    }
    SolverResult solved = solve_expression(e, "x");
    internal_assert(solved.fully_solved && solved.result.defined());

    // Check some things that we don't expect to work.

    // Quadratics:
    internal_assert(!solve_expression(x * x < 4, "x").fully_solved);

    // Function calls, cast nodes, or multiplications by unknown sign
    // don't get inverted, but the bit containing x still gets moved
    // leftwards.
    check_solve(4.0f > sqrt(x), sqrt(x) < 4.0f);

    check_solve(4 > y * x, x * y < 4);

    // Now test solving for an interval
    check_inner_interval(x > 0, 1, Interval::pos_inf());
    check_inner_interval(x < 100, Interval::neg_inf(), 99);
    check_outer_interval(x > 0 && x < 100, 1, 99);
    check_inner_interval(x > 0 && x < 100, 1, 99);

    Expr c = Variable::make(Bool(), "c");
    check_outer_interval(Let::make("y", 0, x > y && x < 100), 1, 99);
    check_outer_interval(Let::make("c", x > 0, c && x < 100), 1, 99);

    check_outer_interval((x >= 10 && x <= 90) && sin(x) > 0.5f, 10, 90);
    check_inner_interval((x >= 10 && x <= 90) && sin(x) > 0.6f, Interval::pos_inf(), Interval::neg_inf());

    check_inner_interval(x == 10, 10, 10);
    check_outer_interval(x == 10, 10, 10);

    check_inner_interval(!(x != 10), 10, 10);
    check_outer_interval(!(x != 10), 10, 10);

    check_inner_interval(3 * x + 4 < 27, Interval::neg_inf(), 7);
    check_outer_interval(3 * x + 4 < 27, Interval::neg_inf(), 7);

    check_inner_interval(min(x, y) > 17, 18, y);
    check_outer_interval(min(x, y) > 17, 18, Interval::pos_inf());

    check_inner_interval(x / 5 < 17, Interval::neg_inf(), 84);
    check_outer_interval(x / 5 < 17, Interval::neg_inf(), 84);

    // Test anding a condition over a domain
    check_and_condition(x > 0, const_true(), Interval(1, y));
    check_and_condition(x > 0, const_true(), Interval(5, y));
    check_and_condition(x > 0, const_false(), Interval(-5, y));
    check_and_condition(x > 0 && x < 10, const_true(), Interval(1, 9));
    check_and_condition(x > 0 || sin(x) == 0.5f, const_true(), Interval(100, 200));

    check_and_condition(x <= 0, const_true(), Interval(-100, 0));
    check_and_condition(x <= 0, const_false(), Interval(-100, 1));

    check_and_condition(x <= 0 || y > 2, const_true(), Interval(-100, 0));
    check_and_condition(x > 0 || y > 2, 2 < y, Interval(-100, 0));

    check_and_condition(x == 0, const_true(), Interval(0, 0));
    check_and_condition(x == 0, const_false(), Interval(-10, 10));
    check_and_condition(x != 0, const_false(), Interval(-10, 10));
    check_and_condition(x != 0, const_true(), Interval(-20, -10));

    check_and_condition(y == 0, y == 0, Interval(-10, 10));
    check_and_condition(y != 0, y != 0, Interval(-10, 10));
    check_and_condition((x == 5) && (y != 0), const_false(), Interval(-10, 10));
    check_and_condition((x == 5) && (y != 3), y != 3, Interval(5, 5));
    check_and_condition((x != 0) && (y != 0), const_false(), Interval(-10, 10));
    check_and_condition((x != 0) && (y != 0), y != 0, Interval(-20, -10));

    {
        // This case used to break due to signed integer overflow in
        // the simplifier.
        Expr a16 = Load::make(Int(16), "a", {x}, Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        Expr b16 = Load::make(Int(16), "b", {x}, Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        Expr lhs = pow(cast<int32_t>(a16), 2) + pow(cast<int32_t>(b16), 2);

        Scope<Interval> s;
        s.push("x", Interval(-10, 10));
        Expr cond = and_condition_over_domain(lhs < 0, s);
        internal_assert(!is_const_one(simplify(cond)));
    }

    {
        // This cause use to cause infinite recursion:
        Expr t = Variable::make(Int(32), "t");
        Expr test = (x <= min(max((y - min(((z * x) + t), t)), 1), 0));
        Interval result = solve_for_outer_interval(test, "z");
    }

    {
        // This case caused exponential behavior
        Expr t = Variable::make(Int(32), "t");
        for (int i = 0; i < 50; i++) {
            t = min(t, Variable::make(Int(32), unique_name('v')));
            t = max(t, Variable::make(Int(32), unique_name('v')));
        }
        solve_for_outer_interval(t <= 5, "t");
        solve_for_inner_interval(t <= 5, "t");
    }

    // Check for partial results
    check_solve(max(min(y, x), x), max(min(x, y), x));
    check_solve(min(y, x) + max(y, 2 * x), min(x, y) + max(x * 2, y));
    check_solve((min(x, y) + min(y, x)) * max(y, x), (min(x, y) * 2) * max(x, y));
    check_solve(max((min((y * x), x) + min((1 + y), x)), (y + 2 * x)),
                max((min((x * y), x) + min(x, (1 + y))), (x * 2 + y)));

    {
        Expr x = Variable::make(UInt(32), "x");
        Expr y = Variable::make(UInt(32), "y");
        Expr z = Variable::make(UInt(32), "z");
        check_solve(5 - (4 - 4 * x), x * (4) + 1);
        check_solve(z - (y - x), x + (z - y));
        check_solve(z - (y - x) == 2, x == 2 - (z - y));

        check_solve(x - (x - y), (x - x) + y);

        // This is used to cause infinite recursion
        Expr expr = Add::make(z, Sub::make(x, y));
        SolverResult solved = solve_expression(expr, "y");
    }

    // This case was incorrect due to canonicalization of the multiply
    // occuring after unpacking the LHS.
    check_solve((y - z) * x, x * (y - z));

    // These cases were incorrectly not flipping min/max when moving
    // it out of the RHS of a subtract.
    check_solve(min(x - y, x - z), x - max(y, z));
    check_solve(min(x - y, x), x - max(y, 0));
    check_solve(min(x, x - y), x - max(y, 0));
    check_solve(max(x - y, x - z), x - min(y, z));
    check_solve(max(x - y, x), x - min(y, 0));
    check_solve(max(x, x - y), x - min(y, 0));

    // Check mixed add/sub
    check_solve(min(x - y, x + z), x + min(0 - y, z));
    check_solve(max(x - y, x + z), x + max(0 - y, z));
    check_solve(min(x + y, x - z), x + min(y, 0 - z));
    check_solve(max(x + y, x - z), x + max(y, 0 - z));

    debug(0) << "Solve test passed\n";
}

}  // namespace Internal
}  // namespace Halide
