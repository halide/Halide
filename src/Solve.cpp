#include "Solve.h"
#include "Simplify.h"
#include "IRMutator.h"
#include "IREquality.h"
#include "Substitute.h"
#include "CSE.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::pair;
using std::make_pair;

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
    SolveExpression(const string &v, const Scope<Expr> &es) :
        failed(false), var(v), uses_var(false), external_scope(es) {}

    using IRMutator::mutate;

    Expr mutate(Expr e) {
        // If the solve has already failed. Bail out.
        if (failed) {
            return e;
        }

        map<Expr, CacheEntry, ExprCompare>::iterator iter = cache.find(e);
        if (iter == cache.end()) {
            // Not in the cache, call the base class version.
            Expr new_e = IRMutator::mutate(e);
            CacheEntry entry = {new_e, uses_var};
            cache[e] = entry;
            return new_e;
        } else {
            // Cache hit.
            uses_var = uses_var || iter->second.uses_var;
            return iter->second.expr;
        }
    }

    // Has the solve failed.
    bool failed;

private:

    // The variable we're solving for.
    string var;

    // Whether or not the just-mutated expression uses the variable.
    bool uses_var;

    // A cache of mutated results. Fortunately the mutator is
    // stateless, so we can cache everything.
    struct CacheEntry {
        Expr expr;
        bool uses_var;
    };
    map<Expr, CacheEntry, ExprCompare> cache;

    // Internal lets. Already mutated.
    Scope<CacheEntry> scope;

    // External lets.
    const Scope<Expr> &external_scope;

    // Return the negative of an expr. Does some eager simplification
    // to avoid injecting pointless -1s.
    Expr negate(Expr e) {
        const Mul *mul = e.as<Mul>();
        if (mul && is_const(mul->b)) {
            return mul->a * simplify(-1*mul->b);
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
    void fail(Expr e) {
        debug(3) << "Failed to solve: " << e << "\n";
        failed = true;
    }

    void visit(const Add *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;

        uses_var = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        uses_var = old_uses_var || a_uses_var || b_uses_var;

        if (b_uses_var && !a_uses_var) {
            std::swap(a, b);
            std::swap(a_uses_var, b_uses_var);
        }

        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();

        expr = Expr();

        if (a_uses_var && !b_uses_var) {
            if (sub_a) {
                // (f(x) - a) + b -> f(x) + (b - a)
                expr = mutate(sub_a->a + (b - sub_a->b));
            } else if (add_a) {
                // (f(x) + a) + b -> f(x) + (a + b)
                expr = mutate(add_a->a + (add_a->b + b));
            }
        } else if (a_uses_var && b_uses_var) {
            if (equal(a, b)) {
                expr = mutate(a*2);
            } else if (add_a) {
                // (f(x) + a) + g(x) -> (f(x) + g(x)) + a
                expr = mutate((add_a->a + b) + add_a->b);
            } else if (add_b) {
                // f(x) + (g(x) + a) -> (f(x) + g(x)) + a
                expr = mutate((a + add_b->a) + add_b->b);
            } else if (sub_a) {
                // (f(x) - a) + g(x) -> (f(x) + g(x)) - a
                expr = mutate((sub_a->a + b) - sub_a->b);
            } else if (sub_b) {
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
            } else {
                fail(a + b);
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

    }

    void visit(const Sub *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;

        uses_var = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        uses_var = old_uses_var || a_uses_var || b_uses_var;

        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();

        expr = Expr();

        if (a_uses_var && !b_uses_var) {
            if (sub_a) {
                // (f(x) - a) - b -> f(x) - (a + b)
                expr = mutate(sub_a->a - (sub_a->b + b));
            } else if (add_a) {
                // (f(x) + a) - b -> f(x) + (a - b)
                expr = mutate(add_a->a + (add_a->b - b));
            }
        } else if (b_uses_var && !a_uses_var) {
            if (sub_b) {
                // a - (f(x) - b) -> -f(x) + (a + b)
                expr = mutate(negate(sub_b->a) + (a + sub_b->b));
            } else if (add_b) {
                // a - (f(x) + b) -> -f(x) + (a - b)
                expr = mutate(negate(add_b->a) + (a - add_b->b));
            } else {
                expr = mutate(negate(b) + a);
            }
        } else if (a_uses_var && b_uses_var) {
            if (add_a) {
                // (f(x) + a) - g(x) -> (f(x) - g(x)) + a
                expr = mutate(add_a->a - b + add_a->b);
            } else if (add_b) {
                // f(x) - (g(x) + a) -> (f(x) - g(x)) - a
                expr = mutate(a - add_b->a - add_b->b);
            } else if (sub_a) {
                // (f(x) - a) - g(x) -> (f(x) - g(x)) - a
                expr = mutate(sub_a->a - b - sub_a->b);
            } else if (sub_b) {
                // f(x) - (g(x) - a) -> (f(x) - g(x)) - a
                expr = mutate(a - sub_b->a - sub_b->b);
            } else if (mul_a && mul_b && equal(mul_a->a, mul_b->a)) {
                // f(x)*a - f(x)*b -> f(x)*(a - b)
                expr = mutate(mul_a->a * (mul_a->b - mul_b->b));
            } else if (mul_a && mul_b && equal(mul_a->b, mul_b->b)) {
                // f(x)*a - g(x)*a -> (f(x) - g(x)*a);
                expr = mutate((mul_a->a - mul_b->a) * mul_a->b);
            } else {
                fail(a - b);
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
    }

    void visit(const Mul *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;

        uses_var = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        uses_var = old_uses_var || a_uses_var || b_uses_var;

        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Mul *mul_a = a.as<Mul>();

        if (b_uses_var && !a_uses_var) {
            std::swap(a, b);
            std::swap(a_uses_var, b_uses_var);
        }

        expr = Expr();
        if (a_uses_var && !b_uses_var) {
            if (add_a) {
                // (f(x) + a) * b -> f(x) * b + a * b
                expr = mutate(add_a->a * b + add_a->b * b);
            } else if (sub_a) {
                // (f(x) - a) * b -> f(x) * b - a * b
                expr = mutate(sub_a->a * b - sub_a->b * b);
            } else if (mul_a) {
                // (f(x) * a) * b -> f(x) * (a * b)
                expr = mutate(mul_a->a * (mul_a->b * b));
            }
        } else if (a_uses_var && b_uses_var) {
            // It's a quadratic. We could continue but this is
            // unlikely to ever occur. Code will be added here as
            // these cases actually pop up.
            fail(a * b);
        } else {
            // Do some constant-folding
            if (is_const(a) && is_const(b)) {
                expr = simplify(a * b);
            }
        }

        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = a * b;
            }
        }
    }

    void visit(const Call *op) {
        // Ignore likely intrinsics
        if (op->name == Call::likely && op->call_type == Call::Intrinsic) {
            expr = mutate(op->args[0]);
        } else {
            IRMutator::visit(op);
        }
    }

    template<typename T>
    void visit_commutative_op(const T *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;

        uses_var = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        uses_var = old_uses_var || a_uses_var || b_uses_var;

        if (b_uses_var && !a_uses_var) {
            std::swap(a, b);
        } else if (a_uses_var && b_uses_var) {
            fail(T::make(a, b));
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            expr = op;
        } else {
            expr = T::make(a, b);
        }
    }

    void visit(const Min *op) {
        visit_commutative_op(op);
    }

    void visit(const Max *op) {
        visit_commutative_op(op);
    }

    void visit(const Or *op) {
        visit_commutative_op(op);
    }

    void visit(const And *op) {
        visit_commutative_op(op);
    }

    template<typename Cmp, typename Opp>
    void visit_cmp(const Cmp *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        Expr a = mutate(op->a);
        bool a_uses_var = uses_var;

        uses_var = false;
        Expr b = mutate(op->b);
        bool b_uses_var = uses_var;
        uses_var = old_uses_var || a_uses_var || b_uses_var;

        if (b_uses_var && !a_uses_var) {
            expr = mutate(Opp::make(b, a));
            return;
        }

        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Mul *mul_a = a.as<Mul>();

        bool is_eq = Expr(op).as<EQ>() != NULL;
        bool is_ne = Expr(op).as<NE>() != NULL;
        bool is_lt = Expr(op).as<LT>() != NULL;
        bool is_le = Expr(op).as<LE>() != NULL;
        bool is_ge = Expr(op).as<GE>() != NULL;
        bool is_gt = Expr(op).as<GT>() != NULL;

        expr = Expr();

        if (a_uses_var && !b_uses_var) {
            // We have f(x) < y. Try to unwrap f(x)
            if (add_a) {
                // f(x) + b < c -> f(x) < c - b
                expr = mutate(Cmp::make(add_a->a, (b - add_a->b)));
            } else if (sub_a) {
                // f(x) - b < c -> f(x) < c + b
                expr = mutate(Cmp::make(sub_a->a, (b + sub_a->b)));
            } else if (mul_a) {
                if (op->type.is_float()) {
                    // f(x) * b == c -> f(x) == c / b
                    if (is_eq || is_ne || is_positive_const(mul_a->b)) {
                        expr = mutate(Cmp::make(mul_a->a, (b / mul_a->b)));
                    } else if (is_negative_const(mul_a->b)) {
                        expr = mutate(Opp::make(mul_a->a, (b / mul_a->b)));
                    }
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
                    } else if (is_le) {
                        if (is_positive_const(mul_a->b)) {
                            expr = mutate(mul_a->a <= div);
                        } else if (is_negative_const(mul_a->b)) {
                            expr = mutate(mul_a->a >= div);
                        } else {
                            fail(Cmp::make(a, b));
                        }
                    } else if (is_lt) {
                        if (is_positive_const(mul_a->b)) {
                            expr = mutate(mul_a->a < (b + (mul_a->b - 1)) / mul_a->b);
                        } else if (is_negative_const(mul_a->b)) {
                            expr = mutate(mul_a->a > (b - (mul_a->b + 1)) / mul_a->b);
                        } else {
                            fail(Cmp::make(a, b));
                        }
                    } else if (is_gt) {
                        if (is_positive_const(mul_a->b)) {
                            expr = mutate(mul_a->a > div);
                        } else if (is_negative_const(mul_a->b)) {
                            expr = mutate(mul_a->a < div);
                        } else {
                            fail(Cmp::make(a, b));
                        }
                    } else if (is_ge) {
                        if (is_positive_const(mul_a->b)) {
                            expr = mutate(mul_a->a >= (b + (mul_a->b - 1)) / mul_a->b);
                        } else if (is_negative_const(mul_a->b)) {
                            expr = mutate(mul_a->a <= (b - (mul_a->b + 1)) / mul_a->b);
                        } else {
                            fail(Cmp::make(a, b));
                        }
                    }
                }
            }
        } else if (a_uses_var && b_uses_var) {
            // Convert to f(x) - g(x) == 0 and let the subtract mutator clean up.
            expr = mutate(Cmp::make(a - b, 0));
        }

        if (!expr.defined()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                expr = op;
            } else {
                expr = Cmp::make(a, b);
            }
        }
    }

    void visit(const LT *op) {
        visit_cmp<LT, GT>(op);
    }

    void visit(const LE *op) {
        visit_cmp<LE, GE>(op);
    }

    void visit(const GE *op) {
        visit_cmp<GE, LE>(op);
    }

    void visit(const GT *op) {
        visit_cmp<GT, LT>(op);
    }

    void visit(const EQ *op) {
        visit_cmp<EQ, EQ>(op);
    }

    void visit(const NE *op) {
        visit_cmp<NE, NE>(op);
    }

    void visit(const Variable *op) {
        if (op->name == var) {
            uses_var = true;
            expr = op;
        } else if (scope.contains(op->name)) {
            CacheEntry e = scope.get(op->name);
            expr = e.expr;
            uses_var = uses_var || e.uses_var;
        } else if (external_scope.contains(op->name)) {
            Expr e = external_scope.get(op->name);
            // Expressions in the external scope haven't been solved
            // yet.
            expr = mutate(e);
        } else {
            expr = op;
        }
    }

    void visit(const Let *op) {
        bool old_uses_var = uses_var;
        uses_var = false;
        Expr value = mutate(op->value);
        CacheEntry e = {value, uses_var};

        uses_var = old_uses_var;
        scope.push(op->name, e);
        expr = mutate(op->body);
        scope.pop(op->name);
    }

};

Expr solve_expression(Expr e, const std::string &variable, const Scope<Expr> &scope) {
    SolveExpression solver(variable, scope);
    e = solver.mutate(e);
    if (solver.failed) {
        return Expr();
    } else {
        // The process has expanded lets. Re-collect them.
        return common_subexpression_elimination(e);
    }
}


// Testing code
void check(Expr a, Expr b) {
    Expr c = solve_expression(a, "x");
    internal_assert(equal(c, b)) << "Expression: " << a << "\n solved to " << c << "\n instead of " << b << "\n";
}

void solve_test() {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    Expr z = Variable::make(Int(32), "z");

    // Check some simple cases
    check(3 - 4*x, x*(-4) + 3);
    check(min(5, x), min(x, 5));
    check(max(5, (5+x)*y), max(x*y + 5*y, 5));
    check(5*y + 3*x == 2, ((x == ((2 - (5*y))/3)) && (((2 - (5*y)) % 3) == 0)));

    // A let statement
    check(Let::make("z", 3 + 5*x, y + z < 8),
          x < (((8 - (3 + y)) + 4)/5));

    // A let statement where the variable gets used twice.
    check(Let::make("z", 3 + 5*x, y + (z + z) < 8),
          x < (((8 - (6 + y)) + 9)/10));

    // Something where we expect a let in the output.
    {
        Expr e = y+1;
        for (int i = 0; i < 10; i++) {
            e *= (e + 1);
        }
        Expr c = solve_expression(x + e < e*e, "x");
        internal_assert(c.as<Let>());
    }

    // Solving inequalities for integers is a pain to get right with
    // all the rounding rules. Check we didn't make a mistake with
    // brute force.
    for (int den = -3; den <= 3; den ++) {
        if (den == 0) continue;
        for (int num = 5; num <= 10; num++) {
            Expr in[] = {x*den < num, x*den <= num, x*den == num, x*den != num, x*den >= num, x*den > num};
            for (int j = 0; j < 6; j++) {
                Expr out = simplify(solve_expression(in[j], "x"));
                for (int i = -10; i < 10; i++) {
                    Expr in_val = substitute("x", i, in[j]);
                    Expr out_val = substitute("x", i, out);
                    in_val = simplify(in_val);
                    out_val = simplify(out_val);
                    internal_assert(equal(in_val, out_val))
                        << "Error: "
                        << in[j] << " is not equivalent to "
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
    e = solve_expression(e, "x");
    internal_assert(e.defined());

    // Check some things that we don't expect to work.

    // Quadratics:
    internal_assert(!solve_expression(x*x < 4, "x").defined());

    // Multiplication by things of unknown sign:
    internal_assert(!solve_expression(x*y < 4, "x").defined());

    // Function calls and cast nodes don't get inverted, but the bit
    // containing x still gets moved leftwards.
    check(4.0f > sqrt(x), sqrt(x) < 4.0f);

}

}
}
