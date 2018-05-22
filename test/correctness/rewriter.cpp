#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// Intercept the calls to the rewriter so that we can see which rules match
template<typename Rewriter, typename Before, typename After, typename Predicate>
bool rewrite(Rewriter &r, Before b, After a, Predicate p) {
    /*
    std::cout << "Checking expression " << Expr(&r.instance.expr)
              << " against pattern " << b
              << " with predicate " << p << "\n";
    */
    if (r(b, a, p)) {
        //std::cout << "Matched!: " << a << "\n";
        return true;
    } else {
        //std::cout << "No match\n";
        return false;
    }
}

template<typename Rewriter, typename Before, typename After>
bool rewrite(Rewriter &r, Before b, After a) {
    return rewrite(r, b, a, IRMatcher::Const(1));
}

bool should_commute(const Expr &a, const Expr &b) {
    if (a.node_type() < b.node_type()) return true;
    if (a.node_type() > b.node_type()) return false;

    if (a.node_type() == IRNodeType::Variable) {
        const Variable *va = a.as<Variable>();
        const Variable *vb = b.as<Variable>();
        return va->name.compare(vb->name) > 0;
    }

    return false;
}

class Simplify : public IRMutator2 {
public:
    Expr mutate(const Expr &e) override {
        // Call the base-class mutate, which just recursively mutates the children
        Expr new_e = IRMutator2::mutate(e);

        // Do some canonicalizations
        if (const LE *le = new_e.as<LE>()) {
            Expr c = le->b < le->a;
            assert(c.type().is_bool());
            return mutate(!c);
        } else if (const GE *ge = new_e.as<GE>()) {
            return mutate(!(ge->a < ge->b));
        } else if (const GT *gt = new_e.as<GT>()) {
            return mutate(ge->b < ge->a);
        } else if (const Add *add = new_e.as<Add>()) {
            if (should_commute(add->a, add->b)) {
                return mutate(add->b + add->a);
            }
        } else if (const Mul *mul = new_e.as<Mul>()) {
            if (should_commute(mul->a, mul->b)) {
                return mutate(mul->b + mul->a);
            }
        } else if (const Min *min = new_e.as<Min>()) {
            if (should_commute(min->a, min->b)) {
                return mutate(min->b + min->a);
            }
        } else if (const Max *max = new_e.as<Max>()) {
            if (should_commute(max->a, max->b)) {
                return mutate(max->b + max->a);
            }
        } else if (const Or *o = new_e.as<Or>()) {
            if (should_commute(o->a, o->b)) {
                return mutate(o->b + o->a);
            }
        } else if (const And *a = new_e.as<And>()) {
            if (should_commute(a->a, a->b)) {
                return mutate(a->b + a->a);
            }
        }

        // Now see if any rewrite rules match

        IRMatcher::Wild<0> v0;
        IRMatcher::Wild<1> v1;
        IRMatcher::Wild<2> v2;
        IRMatcher::Wild<3> v3;
        IRMatcher::Wild<4> v4;
        IRMatcher::Wild<5> v5;
        IRMatcher::WildConst<0> c0;
        IRMatcher::WildConst<1> c1;
        IRMatcher::WildConst<2> c2;

        auto r = IRMatcher::rewriter(new_e, Int(32));


        if (rewrite(r, v0 && true, v0)) return mutate(r.result);
        if (rewrite(r, v0 && false, false)) return mutate(r.result);
        if (rewrite(r, v0 && v0, v0)) return mutate(r.result);
        if (rewrite(r, v0 != v1 && v0 == v1, false)) return mutate(r.result);
        if (rewrite(r, v0 != v1 && v1 == v0, false)) return mutate(r.result);
        if (rewrite(r, (v2 && v0 != v1) && v0 == v1, false)) return mutate(r.result);
        if (rewrite(r, (v2 && v0 != v1) && v1 == v0, false)) return mutate(r.result);
        if (rewrite(r, (v0 != v1 && v2) && v0 == v1, false)) return mutate(r.result);
        if (rewrite(r, (v0 != v1 && v2) && v1 == v0, false)) return mutate(r.result);
        if (rewrite(r, (v2 && v0 == v1) && v0 != v1, false)) return mutate(r.result);
        if (rewrite(r, (v2 && v0 == v1) && v1 != v0, false)) return mutate(r.result);
        if (rewrite(r, (v0 == v1 && v2) && v0 != v1, false)) return mutate(r.result);
        if (rewrite(r, (v0 == v1 && v2) && v1 != v0, false)) return mutate(r.result);
        if (rewrite(r, v0 && !v0, false)) return mutate(r.result);
        if (rewrite(r, !v0 && v0, false)) return mutate(r.result);
        if (rewrite(r, v1 <= v0 && v0 < v1, false)) return mutate(r.result);
        if (rewrite(r, c0 < v0 && v0 < c1, false, !is_float(v0) && c1 <= c0 + 1)) return mutate(r.result);
        if (rewrite(r, v0 < c1 && c0 < v0, false, !is_float(v0) && c1 <= c0 + 1)) return mutate(r.result);
        if (rewrite(r, v0 <= c1 && c0 < v0, false, c1 <= c0)) return mutate(r.result);
        if (rewrite(r, c0 <= v0 && v0 < c1, false, c1 <= c0)) return mutate(r.result);
        if (rewrite(r, c0 <= v0 && v0 <= c1, false, c1 < c0)) return mutate(r.result);
        if (rewrite(r, v0 <= c1 && c0 <= v0, false, c1 < c0)) return mutate(r.result);
        if (rewrite(r, c0 < v0 && c1 < v0, fold(max(c0, c1)) < v0)) return mutate(r.result);
        if (rewrite(r, c0 <= v0 && c1 <= v0, fold(max(c0, c1)) <= v0)) return mutate(r.result);
        if (rewrite(r, v0 < c0 && v0 < c1, v0 < fold(min(c0, c1)))) return mutate(r.result);
        if (rewrite(r, v0 <= c0 && v0 <= c1, v0 <= fold(min(c0, c1)))) return mutate(r.result);

        if (rewrite(r, v0 || true, true)) return mutate(r.result);
        if (rewrite(r, v0 || false, v0)) return mutate(r.result);
        if (rewrite(r, v0 || v0, v0)) return mutate(r.result);
        if (rewrite(r, v0 != v1 || v0 == v1, true)) return mutate(r.result);
        if (rewrite(r, v0 != v1 || v1 == v0, true)) return mutate(r.result);
        if (rewrite(r, (v2 || v0 != v1) || v0 == v1, true)) return mutate(r.result);
        if (rewrite(r, (v2 || v0 != v1) || v1 == v0, true)) return mutate(r.result);
        if (rewrite(r, (v0 != v1 || v2) || v0 == v1, true)) return mutate(r.result);
        if (rewrite(r, (v0 != v1 || v2) || v1 == v0, true)) return mutate(r.result);
        if (rewrite(r, (v2 || v0 == v1) || v0 != v1, true)) return mutate(r.result);
        if (rewrite(r, (v2 || v0 == v1) || v1 != v0, true)) return mutate(r.result);
        if (rewrite(r, (v0 == v1 || v2) || v0 != v1, true)) return mutate(r.result);
        if (rewrite(r, (v0 == v1 || v2) || v1 != v0, true)) return mutate(r.result);
        if (rewrite(r, v0 || !v0, true)) return mutate(r.result);
        if (rewrite(r, !v0 || v0, true)) return mutate(r.result);
        if (rewrite(r, v1 <= v0 || v0 < v1, true)) return mutate(r.result);
        if (rewrite(r, v0 <= c0 || c1 <= v0, true, !is_float(v0) && c1 <= c0 + 1)) return mutate(r.result);
        if (rewrite(r, c1 <= v0 || v0 <= c0, true, !is_float(v0) && c1 <= c0 + 1)) return mutate(r.result);
        if (rewrite(r, v0 <= c0 || c1 < v0, true, c1 <= c0)) return mutate(r.result);
        if (rewrite(r, c1 <= v0 || v0 < c0, true, c1 <= c0)) return mutate(r.result);
        if (rewrite(r, v0 < c0 || c1 < v0, true, c1 < c0)) return mutate(r.result);
        if (rewrite(r, c1 < v0 || v0 < c0, true, c1 < c0)) return mutate(r.result);
        if (rewrite(r, c0 < v0 || c1 < v0, fold(min(c0, c1)) < v0)) return mutate(r.result);
        if (rewrite(r, c0 <= v0 || c1 <= v0, fold(min(c0, c1)) <= v0)) return mutate(r.result);
        if (rewrite(r, v0 < c0 || v0 < c1, v0 < fold(max(c0, c1)))) return mutate(r.result);
        if (rewrite(r, v0 <= c0 || v0 <= c1, v0 <= fold(max(c0, c1)))) return mutate(r.result);

        if (rewrite(r, !c0, fold(!c0))) return mutate(r.result);
        if (rewrite(r, (c0 + c1), fold((c0 + c1)))) return mutate(r.result);
        if (rewrite(r, (v0 + 0), v0)) return mutate(r.result);
        if (rewrite(r, (0 + v0), v0)) return mutate(r.result);
        if (rewrite(r, (v0 + v0), (v0 * 2))) return mutate(r.result);
        if (rewrite(r, (select(v0, v1, v2) + select(v0, v3, v4)), select(v0, (v1 + v3), (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, (select(v0, c0, c1) + c2), select(v0, fold((c0 + c2)), fold((c1 + c2))))) return mutate(r.result);
        if (rewrite(r, (select(v0, v1, c1) + c2), select(v0, (v1 + c2), fold((c1 + c2))))) return mutate(r.result);
        if (rewrite(r, (select(v0, c0, v1) + c2), select(v0, fold((c0 + c2)), (v1 + c2)))) return mutate(r.result);
        if (rewrite(r, ((select(v0, v1, v2) + v3) + select(v0, v4, v5)), (select(v0, (v1 + v4), (v2 + v5)) + v3))) return mutate(r.result);
        if (rewrite(r, ((v3 + select(v0, v1, v2)) + select(v0, v4, v5)), (select(v0, (v1 + v4), (v2 + v5)) + v3))) return mutate(r.result);
        if (rewrite(r, (select(v0, v1, v2) + (select(v0, v4, v5) + v3)), (select(v0, (v1 + v4), (v2 + v5)) + v3))) return mutate(r.result);
        if (rewrite(r, (select(v0, v1, v2) + (v3 + select(v0, v4, v5))), (select(v0, (v1 + v4), (v2 + v5)) + v3))) return mutate(r.result);
        if (rewrite(r, ((select(v0, v1, v2) - v3) + select(v0, v4, v5)), (select(v0, (v1 + v4), (v2 + v5)) - v3))) return mutate(r.result);
        if (rewrite(r, (select(v0, v1, v2) + (select(v0, v4, v5) - v3)), (select(v0, (v1 + v4), (v2 + v5)) - v3))) return mutate(r.result);
        if (rewrite(r, ((v3 - select(v0, v1, v2)) + select(v0, v4, v5)), (select(v0, (v4 - v1), (v5 - v2)) + v3))) return mutate(r.result);
        if (rewrite(r, (select(v0, v1, v2) + (v3 - select(v0, v4, v5))), (select(v0, (v1 - v4), (v2 - v5)) + v3))) return mutate(r.result);
        if (rewrite(r, ((v0 + c0) + c1), (v0 + fold((c0 + c1))))) return mutate(r.result);
        if (rewrite(r, ((v0 + c0) + v1), ((v0 + v1) + c0))) return mutate(r.result);
        if (rewrite(r, (v0 + (v1 + c0)), ((v0 + v1) + c0))) return mutate(r.result);
        if (rewrite(r, ((c0 - v0) + c1), (fold((c0 + c1)) - v0))) return mutate(r.result);
        if (rewrite(r, ((c0 - v0) + v1), ((v1 - v0) + c0))) return mutate(r.result);
        if (rewrite(r, ((v0 - v1) + v1), v0)) return mutate(r.result);
        if (rewrite(r, (v0 + (v1 - v0)), v1)) return mutate(r.result);
        if (rewrite(r, (v0 + (c0 - v1)), ((v0 - v1) + c0))) return mutate(r.result);
        if (rewrite(r, ((v0 - v1) + (v1 - v2)), (v0 - v2))) return mutate(r.result);
        if (rewrite(r, ((v0 - v1) + (v2 - v0)), (v2 - v1))) return mutate(r.result);
        if (rewrite(r, (v0 + (v1 * c0)), (v0 - (v1 * -c0)), ((c0 < 0) && (-c0 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) + v1), (v1 - (v0 * -c0)), (((c0 < 0) && (-c0 > 0)) && !(is_const(v1))))) return mutate(r.result);
        if (rewrite(r, ((v0 * v1) + (v2 * v1)), ((v0 + v2) * v1))) return mutate(r.result);
        if (rewrite(r, ((v0 * v1) + (v1 * v2)), ((v0 + v2) * v1))) return mutate(r.result);
        if (rewrite(r, ((v1 * v0) + (v2 * v1)), (v1 * (v0 + v2)))) return mutate(r.result);
        if (rewrite(r, ((v1 * v0) + (v1 * v2)), (v1 * (v0 + v2)))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) + (v1 * c1)), ((v0 + (v1 * fold((c1 / c0)))) * c0), ((c1 % c0) == 0))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) + (v1 * c1)), (((v0 * fold((c0 / c1))) + v1) * c1), ((c0 % c1) == 0))) return mutate(r.result);
        if (rewrite(r, (v0 + (v0 * v1)), (v0 * (v1 + 1)))) return mutate(r.result);
        if (rewrite(r, (v0 + (v1 * v0)), ((v1 + 1) * v0))) return mutate(r.result);
        if (rewrite(r, ((v0 * v1) + v0), (v0 * (v1 + 1)))) return mutate(r.result);
        if (rewrite(r, ((v1 * v0) + v0), ((v1 + 1) * v0), !(is_const(v0)))) return mutate(r.result);
        if (rewrite(r, (((v0 + c0) / c1) + c2), ((v0 + fold((c0 + (c1 * c2)))) / c1))) return mutate(r.result);
        if (rewrite(r, ((v0 + ((v1 + c0) / c1)) + c2), (v0 + ((v1 + (c0 + (c1 * c2))) / c1)))) return mutate(r.result);
        if (rewrite(r, ((((v1 + c0) / c1) + v0) + c2), (v0 + ((v1 + (c0 + (c1 * c2))) / c1)))) return mutate(r.result);
        if (rewrite(r, (((c0 - v0) / c1) + c2), ((fold((c0 + (c1 * c2))) - v0) / c1), (c0 != 0))) return mutate(r.result);
        if (rewrite(r, (v0 + ((v0 + v1) / c0)), (((fold((c0 + 1)) * v0) + v1) / c0))) return mutate(r.result);
        if (rewrite(r, (v0 + ((v1 + v0) / c0)), (((fold((c0 + 1)) * v0) + v1) / c0))) return mutate(r.result);
        if (rewrite(r, (v0 + ((v1 - v0) / c0)), (((fold((c0 - 1)) * v0) + v1) / c0))) return mutate(r.result);
        if (rewrite(r, (v0 + ((v0 - v1) / c0)), (((fold((c0 + 1)) * v0) - v1) / c0))) return mutate(r.result);
        if (rewrite(r, (((v0 - v1) / c0) + v0), (((fold((c0 + 1)) * v0) - v1) / c0))) return mutate(r.result);
        if (rewrite(r, (((v1 - v0) / c0) + v0), ((v1 + (fold((c0 - 1)) * v0)) / c0))) return mutate(r.result);
        if (rewrite(r, (((v0 + v1) / c0) + v0), (((fold((c0 + 1)) * v0) + v1) / c0))) return mutate(r.result);
        if (rewrite(r, (((v1 + v0) / c0) + v0), ((v1 + (fold((c0 + 1)) * v0)) / c0))) return mutate(r.result);
        if (rewrite(r, (min(v0, (v1 - v2)) + v2), min((v0 + v2), v1))) return mutate(r.result);
        if (rewrite(r, (min((v1 - v2), v0) + v2), min(v1, (v0 + v2)))) return mutate(r.result);
        if (rewrite(r, (min(v0, (v1 + c0)) + c1), min((v0 + c1), v1), ((c0 + c1) == 0))) return mutate(r.result);
        if (rewrite(r, (min((v1 + c0), v0) + c1), min(v1, (v0 + c1)), ((c0 + c1) == 0))) return mutate(r.result);
        if (rewrite(r, (v2 + min(v0, (v1 - v2))), min((v2 + v0), v1))) return mutate(r.result);
        if (rewrite(r, (v2 + min((v1 - v2), v0)), min(v1, (v2 + v0)))) return mutate(r.result);
        if (rewrite(r, (v2 + max(v0, (v1 - v2))), max((v2 + v0), v1))) return mutate(r.result);
        if (rewrite(r, (v2 + max((v1 - v2), v0)), max(v1, (v2 + v0)))) return mutate(r.result);
        if (rewrite(r, (max(v0, (v1 - v2)) + v2), max((v0 + v2), v1))) return mutate(r.result);
        if (rewrite(r, (max((v1 - v2), v0) + v2), max(v1, (v0 + v2)))) return mutate(r.result);
        if (rewrite(r, (max(v0, (v1 + c0)) + c1), max((v0 + c1), v1), ((c0 + c1) == 0))) return mutate(r.result);
        if (rewrite(r, (max((v1 + c0), v0) + c1), max(v1, (v0 + c1)), ((c0 + c1) == 0))) return mutate(r.result);
        if (rewrite(r, (max(v0, v1) + min(v0, v1)), (v0 + v1))) return mutate(r.result);
        if (rewrite(r, (max(v0, v1) + min(v1, v0)), (v0 + v1))) return mutate(r.result);
        if (rewrite(r, (((v0 / v1) * v1) + (v0 % v1)), v0)) return mutate(r.result);
        if (rewrite(r, (((v2 + (v0 / v1)) * v1) + (v0 % v1)), ((v2 * v1) + v0))) return mutate(r.result);
        if (rewrite(r, ((((v0 / v1) + v2) * v1) + (v0 % v1)), (v0 + (v2 * v1)))) return mutate(r.result);
        if (rewrite(r, ((v0 % v1) + (((v0 / v1) * v1) + v2)), (v0 + v2))) return mutate(r.result);
        if (rewrite(r, ((v0 % v1) + (((v0 / v1) * v1) - v2)), (v0 - v2))) return mutate(r.result);
        if (rewrite(r, ((v0 % v1) + (v2 + ((v0 / v1) * v1))), (v0 + v2))) return mutate(r.result);
        if (rewrite(r, (((v0 / v1) * v1) + ((v0 % v1) + v2)), (v0 + v2))) return mutate(r.result);
        if (rewrite(r, (((v0 / v1) * v1) + ((v0 % v1) - v2)), (v0 - v2))) return mutate(r.result);
        if (rewrite(r, (((v0 / v1) * v1) + (v2 + (v0 % v1))), (v0 + v2))) return mutate(r.result);
        if (rewrite(r, ((v0 / 2) + (v0 % 2)), ((v0 + 1) / 2))) return mutate(r.result);
        if (rewrite(r, (v0 + (((c0 - v0) / c1) * c1)), (c0 - ((c0 - v0) % c1)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 + ((((c0 - v0) / c1) + v1) * c1)), (((v1 * c1) - ((c0 - v0) % c1)) + c0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 + ((v1 + ((c0 - v0) / c1)) * c1)), (((v1 * c1) - ((c0 - v0) % c1)) + c0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (c0 % c1), fold((c0 % c1)))) return mutate(r.result);
        if (rewrite(r, (0 % v0), 0)) return mutate(r.result);
        if (rewrite(r, (v0 % 0), IRMatcher::Indeterminate())) return mutate(r.result);
        if (rewrite(r, (v0 % 1), 0)) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) % c1), ((v0 * fold((c0 % c1))) % c1), ((c1 > 0) && ((c0 >= c1) || (c0 < 0))))) return mutate(r.result);
        if (rewrite(r, ((v0 + c0) % c1), ((v0 + fold((c0 % c1))) % c1), ((c1 > 0) && ((c0 >= c1) || (c0 < 0))))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) % c1), ((v0 % fold((c1 / c0))) * c0), ((c0 > 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, (((v0 * c0) + v1) % c1), (v1 % c1), ((c0 % c1) == 0))) return mutate(r.result);
        if (rewrite(r, ((v1 + (v0 * c0)) % c1), (v1 % c1), ((c0 % c1) == 0))) return mutate(r.result);
        if (rewrite(r, (c0 * c1), fold((c0 * c1)))) return mutate(r.result);
        if (rewrite(r, (0 * v0), 0)) return mutate(r.result);
        if (rewrite(r, (1 * v0), v0)) return mutate(r.result);
        if (rewrite(r, (v0 * 0), 0)) return mutate(r.result);
        if (rewrite(r, (v0 * 1), v0)) return mutate(r.result);
        if (rewrite(r, ((v0 + c0) * c1), ((v0 * c1) + fold((c0 * c1))), !(overflows((c0 * c1))))) return mutate(r.result);
        if (rewrite(r, ((v0 - v1) * c0), ((v1 - v0) * fold(-c0)), ((c0 < 0) && (-c0 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) * c1), (v0 * fold((c0 * c1))), !(overflows((c0 * c1))))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) * v1), ((v0 * v1) * c0), !(is_const(v1)))) return mutate(r.result);
        if (rewrite(r, (v0 * (v1 * c0)), ((v0 * v1) * c0))) return mutate(r.result);
        if (rewrite(r, (max(v0, v1) * min(v0, v1)), (v0 * v1))) return mutate(r.result);
        if (rewrite(r, (max(v0, v1) * min(v1, v0)), (v1 * v0))) return mutate(r.result);
        if (rewrite(r, (v0 / 1), v0)) return mutate(r.result);
        if (rewrite(r, (v0 / 0), IRMatcher::Indeterminate())) return mutate(r.result);
        if (rewrite(r, (0 / v0), 0)) return mutate(r.result);
        if (rewrite(r, (v0 / v0), 1)) return mutate(r.result);
        if (rewrite(r, (c0 / c1), fold((c0 / c1)))) return mutate(r.result);
        if (rewrite(r, (select(v0, c0, c1) / c2), select(v0, fold((c0 / c2)), fold((c1 / c2))))) return mutate(r.result);
        if (rewrite(r, ((v0 / c0) / c2), (v0 / fold((c0 * c2))), (((c0 > 0) && (c2 > 0)) && !(overflows((c0 * c2)))))) return mutate(r.result);
        if (rewrite(r, (((v0 / c0) + c1) / c2), ((v0 + fold((c1 * c0))) / fold((c0 * c2))), ((((c0 > 0) && (c2 > 0)) && !(overflows((c0 * c2)))) && !(overflows((c0 * c1)))))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) / c1), (v0 / fold((c1 / c0))), (((c1 % c0) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) / c1), (v0 * fold((c0 / c1))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((v0 * c0) + v1) / c1), ((v1 / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((v0 * c0) - v1) / c1), ((-v1 / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v1 + (v0 * c0)) / c1), ((v1 / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v1 - (v0 * c0)) / c1), ((v1 / c1) - (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((((v0 * c0) + v1) + v2) / c1), (((v1 + v2) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((((v0 * c0) - v1) + v2) / c1), (((v2 - v1) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((((v0 * c0) + v1) - v2) / c1), (((v1 - v2) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((((v0 * c0) - v1) - v2) / c1), (((-v1 - v2) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((v1 + (v0 * c0)) + v2) / c1), (((v1 + v2) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((v1 + (v0 * c0)) - v2) / c1), (((v1 - v2) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((v1 - (v0 * c0)) - v2) / c1), (((v1 - v2) / c1) - (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((v1 - (v0 * c0)) + v2) / c1), (((v1 + v2) / c1) - (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v2 + ((v0 * c0) + v1)) / c1), (((v2 + v1) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v2 + ((v0 * c0) - v1)) / c1), (((v2 - v1) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v2 - ((v0 * c0) - v1)) / c1), (((v2 + v1) / c1) - (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v2 - ((v0 * c0) + v1)) / c1), (((v2 - v1) / c1) - (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v2 + (v1 + (v0 * c0))) / c1), (((v2 + v1) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v2 - (v1 + (v0 * c0))) / c1), (((v2 - v1) / c1) - (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v2 + (v1 - (v0 * c0))) / c1), (((v2 + v1) / c1) - (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v2 - (v1 - (v0 * c0))) / c1), (((v2 - v1) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((((v0 * c0) + v1) + v2) + v3) / c1), ((((v1 + v2) + v3) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((((v1 + (v0 * c0)) + v2) + v3) / c1), ((((v1 + v2) + v3) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((v2 + ((v0 * c0) + v1)) + v3) / c1), ((((v1 + v2) + v3) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, (((v2 + (v1 + (v0 * c0))) + v3) / c1), ((((v1 + v2) + v3) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (((v0 * c0) + v1) + v2)) / c1), ((((v1 + v2) + v3) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v3 + ((v1 + (v0 * c0)) + v2)) / c1), ((((v1 + v2) + v3) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v2 + ((v0 * c0) + v1))) / c1), ((((v1 + v2) + v3) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v2 + (v1 + (v0 * c0)))) / c1), ((((v1 + v2) + v3) / c1) + (v0 * fold((c0 / c1)))), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 + c0) / c1), ((v0 / c1) + fold((c0 / c1))), ((c0 % c1) == 0))) return mutate(r.result);
        if (rewrite(r, ((v0 + v1) / v0), ((v1 / v0) + 1))) return mutate(r.result);
        if (rewrite(r, ((v1 + v0) / v0), ((v1 / v0) + 1))) return mutate(r.result);
        if (rewrite(r, ((v0 - v1) / v0), ((-v1 / v0) + 1))) return mutate(r.result);
        if (rewrite(r, ((v1 - v0) / v0), ((v1 / v0) - 1))) return mutate(r.result);
        if (rewrite(r, (((v0 + v1) + v2) / v0), (((v1 + v2) / v0) + 1))) return mutate(r.result);
        if (rewrite(r, (((v1 + v0) + v2) / v0), (((v1 + v2) / v0) + 1))) return mutate(r.result);
        if (rewrite(r, ((v2 + (v0 + v1)) / v0), (((v2 + v1) / v0) + 1))) return mutate(r.result);
        if (rewrite(r, ((v2 + (v1 + v0)) / v0), (((v2 + v1) / v0) + 1))) return mutate(r.result);
        if (rewrite(r, ((v0 * v1) / v0), v1)) return mutate(r.result);
        if (rewrite(r, ((v1 * v0) / v0), v1)) return mutate(r.result);
        if (rewrite(r, (((v0 * v1) + v2) / v0), (v1 + (v2 / v0)))) return mutate(r.result);
        if (rewrite(r, (((v1 * v0) + v2) / v0), (v1 + (v2 / v0)))) return mutate(r.result);
        if (rewrite(r, ((v2 + (v0 * v1)) / v0), ((v2 / v0) + v1))) return mutate(r.result);
        if (rewrite(r, ((v2 + (v1 * v0)) / v0), ((v2 / v0) + v1))) return mutate(r.result);
        if (rewrite(r, (((v0 * v1) - v2) / v0), (v1 + (-v2 / v0)))) return mutate(r.result);
        if (rewrite(r, (((v1 * v0) - v2) / v0), (v1 + (-v2 / v0)))) return mutate(r.result);
        if (rewrite(r, ((v2 - (v0 * v1)) / v0), ((v2 / v0) - v1))) return mutate(r.result);
        if (rewrite(r, ((v2 - (v1 * v0)) / v0), ((v2 / v0) - v1))) return mutate(r.result);
        if (rewrite(r, (v0 / -1), -v0)) return mutate(r.result);
        if (rewrite(r, (c0 / v1), select((v1 < 0), fold(-c0), c0), (c0 == -1))) return mutate(r.result);
        if (rewrite(r, (((v0 * c0) + c1) / c2), ((v0 + fold((c1 / c0))) / fold((c2 / c0))), (((c2 > 0) && (c0 > 0)) && ((c2 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, (((v0 * c0) + c1) / c2), ((v0 * fold((c0 / c2))) + fold((c1 / c2))), ((c2 > 0) && ((c0 % c2) == 0)))) return mutate(r.result);
        if (rewrite(r, (((v0 % 2) + c0) / 2), ((v0 % 2) + fold((c0 / 2))), ((c0 % 2) == 1))) return mutate(r.result);
        if (rewrite(r, min(v0, v0), v0)) return mutate(r.result);
        if (rewrite(r, min(c0, c1), fold(min(c0, c1)))) return mutate(r.result);
        if (rewrite(r, min(min(v0, c0), c1), min(v0, fold(min(c0, c1))))) return mutate(r.result);
        if (rewrite(r, min(min(v0, c0), v1), min(min(v0, v1), c0))) return mutate(r.result);
        if (rewrite(r, min(min(v0, v1), min(v0, v2)), min(min(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, min(min(v1, v0), min(v0, v2)), min(min(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, min(min(v0, v1), min(v2, v0)), min(min(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, min(min(v1, v0), min(v2, v0)), min(min(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, min(min(v0, v1), min(v2, v3)), min(min(min(v0, v1), v2), v3))) return mutate(r.result);
        if (rewrite(r, min(max(v0, v1), max(v0, v2)), max(v0, min(v1, v2)))) return mutate(r.result);
        if (rewrite(r, min(max(v0, v1), max(v2, v0)), max(v0, min(v1, v2)))) return mutate(r.result);
        if (rewrite(r, min(max(v1, v0), max(v0, v2)), max(min(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, min(max(v1, v0), max(v2, v0)), max(min(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, min(max(min(v0, v1), v2), v1), min(max(v0, v2), v1))) return mutate(r.result);
        if (rewrite(r, min(max(min(v1, v0), v2), v1), min(v1, max(v0, v2)))) return mutate(r.result);
        if (rewrite(r, min(max(v0, c0), c1), max(min(v0, c1), c0), (c0 <= c1))) return mutate(r.result);
        if (rewrite(r, min((v0 + c0), c1), (min(v0, fold((c1 - c0))) + c0))) return mutate(r.result);
        if (rewrite(r, min((v0 + c0), (v1 + c1)), (min(v0, (v1 + fold((c1 - c0)))) + c0), (c1 > c0))) return mutate(r.result);
        if (rewrite(r, min((v0 + c0), (v1 + c1)), (min((v0 + fold((c0 - c1))), v1) + c1), (c0 > c1))) return mutate(r.result);
        if (rewrite(r, min((v0 + v1), (v0 + v2)), (v0 + min(v1, v2)))) return mutate(r.result);
        if (rewrite(r, min((v0 + v1), (v2 + v0)), (v0 + min(v1, v2)))) return mutate(r.result);
        if (rewrite(r, min((v1 + v0), (v0 + v2)), (min(v1, v2) + v0))) return mutate(r.result);
        if (rewrite(r, min((v1 + v0), (v2 + v0)), (min(v1, v2) + v0))) return mutate(r.result);
        if (rewrite(r, min(v0, (v0 + v2)), (v0 + min(v2, 0)))) return mutate(r.result);
        if (rewrite(r, min(v0, (v2 + v0)), (v0 + min(v2, 0)))) return mutate(r.result);
        if (rewrite(r, min((v1 + v0), v0), (min(v1, 0) + v0))) return mutate(r.result);
        if (rewrite(r, min((v0 + v1), v0), (v0 + min(v1, 0)))) return mutate(r.result);
        if (rewrite(r, min(min((v0 + v1), v2), (v0 + v3)), min((v0 + min(v1, v3)), v2))) return mutate(r.result);
        if (rewrite(r, min(min(v2, (v0 + v1)), (v0 + v3)), min((v0 + min(v1, v3)), v2))) return mutate(r.result);
        if (rewrite(r, min(min((v0 + v1), v2), (v3 + v0)), min((v0 + min(v1, v3)), v2))) return mutate(r.result);
        if (rewrite(r, min(min(v2, (v0 + v1)), (v3 + v0)), min((v0 + min(v1, v3)), v2))) return mutate(r.result);
        if (rewrite(r, min(min((v1 + v0), v2), (v0 + v3)), min((min(v1, v3) + v0), v2))) return mutate(r.result);
        if (rewrite(r, min(min(v2, (v1 + v0)), (v0 + v3)), min((min(v1, v3) + v0), v2))) return mutate(r.result);
        if (rewrite(r, min(min((v1 + v0), v2), (v3 + v0)), min((min(v1, v3) + v0), v2))) return mutate(r.result);
        if (rewrite(r, min(min(v2, (v1 + v0)), (v3 + v0)), min((min(v1, v3) + v0), v2))) return mutate(r.result);
        if (rewrite(r, min(((v0 + v3) + v1), (v0 + v2)), (v0 + min((v3 + v1), v2)))) return mutate(r.result);
        if (rewrite(r, min(((v3 + v0) + v1), (v0 + v2)), (min((v3 + v1), v2) + v0))) return mutate(r.result);
        if (rewrite(r, min(((v0 + v3) + v1), (v2 + v0)), (v0 + min((v3 + v1), v2)))) return mutate(r.result);
        if (rewrite(r, min(((v3 + v0) + v1), (v2 + v0)), (min((v3 + v1), v2) + v0))) return mutate(r.result);
        if (rewrite(r, min(((v0 + v3) + v1), v0), (v0 + min((v3 + v1), 0)))) return mutate(r.result);
        if (rewrite(r, min(((v3 + v0) + v1), v0), (v0 + min((v3 + v1), 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 + v1), ((v3 + v0) + v2)), (v0 + min((v3 + v2), v1)))) return mutate(r.result);
        if (rewrite(r, min((v0 + v1), ((v0 + v3) + v2)), (v0 + min((v3 + v2), v1)))) return mutate(r.result);
        if (rewrite(r, min((v1 + v0), ((v3 + v0) + v2)), (min((v3 + v2), v1) + v0))) return mutate(r.result);
        if (rewrite(r, min((v1 + v0), ((v0 + v3) + v2)), (min((v3 + v2), v1) + v0))) return mutate(r.result);
        if (rewrite(r, min(v0, ((v3 + v0) + v2)), (v0 + min((v3 + v2), 0)))) return mutate(r.result);
        if (rewrite(r, min(v0, ((v0 + v3) + v2)), (v0 + min((v3 + v2), 0)))) return mutate(r.result);
        if (rewrite(r, min((v1 - v0), (v2 - v0)), (min(v1, v2) - v0))) return mutate(r.result);
        if (rewrite(r, min((v0 - v1), (v0 - v2)), (v0 - max(v1, v2)))) return mutate(r.result);
        if (rewrite(r, min(v0, (v0 - v1)), (v0 - max(0, v1)))) return mutate(r.result);
        if (rewrite(r, min((v0 - v1), v0), (v0 - max(0, v1)))) return mutate(r.result);
        if (rewrite(r, min(v0, ((v0 - v1) + v2)), (v0 + min(0, (v2 - v1))))) return mutate(r.result);
        if (rewrite(r, min(v0, (v2 + (v0 - v1))), (v0 + min(0, (v2 - v1))))) return mutate(r.result);
        if (rewrite(r, min(v0, ((v0 - v1) - v2)), (v0 - max(0, (v1 + v2))))) return mutate(r.result);
        if (rewrite(r, min(((v0 - v1) + v2), v0), (min(0, (v2 - v1)) + v0))) return mutate(r.result);
        if (rewrite(r, min((v2 + (v0 - v1)), v0), (min(0, (v2 - v1)) + v0))) return mutate(r.result);
        if (rewrite(r, min(((v0 - v1) - v2), v0), (v0 - max(0, (v1 + v2))))) return mutate(r.result);
        if (rewrite(r, min((v0 * c0), c1), (min(v0, fold((c1 / c0))) * c0), ((c0 > 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 * c0), c1), (max(v0, fold((c1 / c0))) * c0), ((c0 < 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 * c0), (v1 * c1)), (min(v0, (v1 * fold((c1 / c0)))) * c0), ((c0 > 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 * c0), (v1 * c1)), (max(v0, (v1 * fold((c1 / c0)))) * c0), ((c0 < 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 * c0), (v1 * c1)), (min((v0 * fold((c0 / c1))), v1) * c1), ((c1 > 0) && ((c0 % c1) == 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 * c0), (v1 * c1)), (max((v0 * fold((c0 / c1))), v1) * c1), ((c1 < 0) && ((c0 % c1) == 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 * c0), ((v1 * c0) + c1)), (min(v0, (v1 + fold((c1 / c0)))) * c0), ((c0 > 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 * c0), ((v1 * c0) + c1)), (max(v0, (v1 + fold((c1 / c0)))) * c0), ((c0 < 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, min((v0 / c0), (v1 / c0)), (min(v0, v1) / c0), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, min((v0 / c0), (v1 / c0)), (max(v0, v1) / c0), (c0 < 0))) return mutate(r.result);
        if (rewrite(r, min((v0 / c0), ((v1 / c0) + c1)), (min(v0, (v1 + fold((c1 * c0)))) / c0), ((c0 > 0) && !(overflows((c1 * c0)))))) return mutate(r.result);
        if (rewrite(r, min((v0 / c0), ((v1 / c0) + c1)), (max(v0, (v1 + fold((c1 * c0)))) / c0), ((c0 < 0) && !(overflows((c1 * c0)))))) return mutate(r.result);
        if (rewrite(r, min(select(v0, v1, v2), select(v0, v3, v4)), select(v0, min(v1, v3), min(v2, v4)))) return mutate(r.result);
        if (rewrite(r, min((c0 - v0), c1), (c0 - max(v0, fold((c0 - c1)))))) return mutate(r.result);
        if (rewrite(r, max(v0, v0), v0)) return mutate(r.result);
        if (rewrite(r, max(c0, c1), fold(max(c0, c1)))) return mutate(r.result);
        if (rewrite(r, max(max(v0, c0), c1), max(v0, fold(max(c0, c1))))) return mutate(r.result);
        if (rewrite(r, max(max(v0, c0), v1), max(max(v0, v1), c0))) return mutate(r.result);
        if (rewrite(r, max(max(v0, v1), max(v0, v2)), max(max(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, max(max(v1, v0), max(v0, v2)), max(max(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, max(max(v0, v1), max(v2, v0)), max(max(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, max(max(v1, v0), max(v2, v0)), max(max(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, max(max(v0, v1), max(v2, v3)), max(max(max(v0, v1), v2), v3))) return mutate(r.result);
        if (rewrite(r, max(min(v0, v1), min(v0, v2)), min(v0, max(v1, v2)))) return mutate(r.result);
        if (rewrite(r, max(min(v0, v1), min(v2, v0)), min(v0, max(v1, v2)))) return mutate(r.result);
        if (rewrite(r, max(min(v1, v0), min(v0, v2)), min(max(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, max(min(v1, v0), min(v2, v0)), min(max(v1, v2), v0))) return mutate(r.result);
        if (rewrite(r, max(min(max(v0, v1), v2), v1), max(min(v0, v2), v1))) return mutate(r.result);
        if (rewrite(r, max(min(max(v1, v0), v2), v1), max(v1, min(v0, v2)))) return mutate(r.result);
        if (rewrite(r, max((v0 + c0), c1), (max(v0, fold((c1 - c0))) + c0))) return mutate(r.result);
        if (rewrite(r, max((v0 + c0), (v1 + c1)), (max(v0, (v1 + fold((c1 - c0)))) + c0), (c1 > c0))) return mutate(r.result);
        if (rewrite(r, max((v0 + c0), (v1 + c1)), (max((v0 + fold((c0 - c1))), v1) + c1), (c0 > c1))) return mutate(r.result);
        if (rewrite(r, max((v0 + v1), (v0 + v2)), (v0 + max(v1, v2)))) return mutate(r.result);
        if (rewrite(r, max((v0 + v1), (v2 + v0)), (v0 + max(v1, v2)))) return mutate(r.result);
        if (rewrite(r, max((v1 + v0), (v0 + v2)), (max(v1, v2) + v0))) return mutate(r.result);
        if (rewrite(r, max((v1 + v0), (v2 + v0)), (max(v1, v2) + v0))) return mutate(r.result);
        if (rewrite(r, max(v0, (v0 + v2)), (v0 + max(v2, 0)))) return mutate(r.result);
        if (rewrite(r, max(v0, (v2 + v0)), (v0 + max(v2, 0)))) return mutate(r.result);
        if (rewrite(r, max((v1 + v0), v0), (max(v1, 0) + v0))) return mutate(r.result);
        if (rewrite(r, max((v0 + v1), v0), (v0 + max(v1, 0)))) return mutate(r.result);
        if (rewrite(r, max(max((v0 + v1), v2), (v0 + v3)), max((v0 + max(v1, v3)), v2))) return mutate(r.result);
        if (rewrite(r, max(max(v2, (v0 + v1)), (v0 + v3)), max((v0 + max(v1, v3)), v2))) return mutate(r.result);
        if (rewrite(r, max(max((v0 + v1), v2), (v3 + v0)), max((v0 + max(v1, v3)), v2))) return mutate(r.result);
        if (rewrite(r, max(max(v2, (v0 + v1)), (v3 + v0)), max((v0 + max(v1, v3)), v2))) return mutate(r.result);
        if (rewrite(r, max(max((v1 + v0), v2), (v0 + v3)), max((max(v1, v3) + v0), v2))) return mutate(r.result);
        if (rewrite(r, max(max(v2, (v1 + v0)), (v0 + v3)), max((max(v1, v3) + v0), v2))) return mutate(r.result);
        if (rewrite(r, max(max((v1 + v0), v2), (v3 + v0)), max((max(v1, v3) + v0), v2))) return mutate(r.result);
        if (rewrite(r, max(max(v2, (v1 + v0)), (v3 + v0)), max((max(v1, v3) + v0), v2))) return mutate(r.result);
        if (rewrite(r, max(((v0 + v3) + v1), (v0 + v2)), (v0 + max((v3 + v1), v2)))) return mutate(r.result);
        if (rewrite(r, max(((v3 + v0) + v1), (v0 + v2)), (max((v3 + v1), v2) + v0))) return mutate(r.result);
        if (rewrite(r, max(((v0 + v3) + v1), (v2 + v0)), (v0 + max((v3 + v1), v2)))) return mutate(r.result);
        if (rewrite(r, max(((v3 + v0) + v1), (v2 + v0)), (max((v3 + v1), v2) + v0))) return mutate(r.result);
        if (rewrite(r, max(((v0 + v3) + v1), v0), (v0 + max((v3 + v1), 0)))) return mutate(r.result);
        if (rewrite(r, max(((v3 + v0) + v1), v0), (v0 + max((v3 + v1), 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 + v1), ((v3 + v0) + v2)), (v0 + max((v3 + v2), v1)))) return mutate(r.result);
        if (rewrite(r, max((v0 + v1), ((v0 + v3) + v2)), (v0 + max((v3 + v2), v1)))) return mutate(r.result);
        if (rewrite(r, max((v1 + v0), ((v3 + v0) + v2)), (max((v3 + v2), v1) + v0))) return mutate(r.result);
        if (rewrite(r, max((v1 + v0), ((v0 + v3) + v2)), (max((v3 + v2), v1) + v0))) return mutate(r.result);
        if (rewrite(r, max(v0, ((v3 + v0) + v2)), (v0 + max((v3 + v2), 0)))) return mutate(r.result);
        if (rewrite(r, max(v0, ((v0 + v3) + v2)), (v0 + max((v3 + v2), 0)))) return mutate(r.result);
        if (rewrite(r, max((v1 - v0), (v2 - v0)), (max(v1, v2) - v0))) return mutate(r.result);
        if (rewrite(r, max((v0 - v1), (v0 - v2)), (v0 - min(v1, v2)))) return mutate(r.result);
        if (rewrite(r, max(v0, (v0 - v1)), (v0 - min(0, v1)))) return mutate(r.result);
        if (rewrite(r, max((v0 - v1), v0), (v0 - min(0, v1)))) return mutate(r.result);
        if (rewrite(r, max(v0, ((v0 - v1) + v2)), (v0 + max(0, (v2 - v1))))) return mutate(r.result);
        if (rewrite(r, max(v0, (v2 + (v0 - v1))), (v0 + max(0, (v2 - v1))))) return mutate(r.result);
        if (rewrite(r, max(v0, ((v0 - v1) - v2)), (v0 - min(0, (v1 + v2))))) return mutate(r.result);
        if (rewrite(r, max(((v0 - v1) + v2), v0), (max(0, (v2 - v1)) + v0))) return mutate(r.result);
        if (rewrite(r, max((v2 + (v0 - v1)), v0), (max(0, (v2 - v1)) + v0))) return mutate(r.result);
        if (rewrite(r, max(((v0 - v1) - v2), v0), (v0 - min(0, (v1 + v2))))) return mutate(r.result);
        if (rewrite(r, max((v0 * c0), c1), (max(v0, fold((c1 / c0))) * c0), ((c0 > 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 * c0), c1), (min(v0, fold((c1 / c0))) * c0), ((c0 < 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 * c0), (v1 * c1)), (max(v0, (v1 * fold((c1 / c0)))) * c0), ((c0 > 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 * c0), (v1 * c1)), (min(v0, (v1 * fold((c1 / c0)))) * c0), ((c0 < 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 * c0), (v1 * c1)), (max((v0 * fold((c0 / c1))), v1) * c1), ((c1 > 0) && ((c0 % c1) == 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 * c0), (v1 * c1)), (min((v0 * fold((c0 / c1))), v1) * c1), ((c1 < 0) && ((c0 % c1) == 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 * c0), ((v1 * c0) + c1)), (max(v0, (v1 + fold((c1 / c0)))) * c0), ((c0 > 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 * c0), ((v1 * c0) + c1)), (min(v0, (v1 + fold((c1 / c0)))) * c0), ((c0 < 0) && ((c1 % c0) == 0)))) return mutate(r.result);
        if (rewrite(r, max((v0 / c0), (v1 / c0)), (max(v0, v1) / c0), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, max((v0 / c0), (v1 / c0)), (min(v0, v1) / c0), (c0 < 0))) return mutate(r.result);
        if (rewrite(r, max((v0 / c0), ((v1 / c0) + c1)), (max(v0, (v1 + fold((c1 * c0)))) / c0), ((c0 > 0) && !(overflows((c1 * c0)))))) return mutate(r.result);
        if (rewrite(r, max((v0 / c0), ((v1 / c0) + c1)), (min(v0, (v1 + fold((c1 * c0)))) / c0), ((c0 < 0) && !(overflows((c1 * c0)))))) return mutate(r.result);
        if (rewrite(r, max(select(v0, v1, v2), select(v0, v3, v4)), select(v0, max(v1, v3), max(v2, v4)))) return mutate(r.result);
        if (rewrite(r, max((c0 - v0), c1), (c0 - min(v0, fold((c0 - c1)))))) return mutate(r.result);
        if (rewrite(r, (c0 == 0), fold((c0 == 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 + c0) == 0), (v0 == fold(-c0)))) return mutate(r.result);
        if (rewrite(r, ((c0 - v0) == 0), (v0 == c0))) return mutate(r.result);
        if (rewrite(r, ((v0 * v1) == 0), ((v0 == 0) || (v1 == 0)))) return mutate(r.result);
        if (rewrite(r, (select(v0, 0, v1) == 0), (v0 || (v1 == 0)))) return mutate(r.result);
        if (rewrite(r, (select(v0, c0, v1) == 0), (!(v0) && (v1 == 0)), (c0 != 0))) return mutate(r.result);
        if (rewrite(r, (select(v0, v1, 0) == 0), (!(v0) || (v1 == 0)))) return mutate(r.result);
        if (rewrite(r, (select(v0, v1, c0) == 0), (v0 && (v1 == 0)), (c0 != 0))) return mutate(r.result);
        if (rewrite(r, (c0 < c1), fold((c0 < c1)))) return mutate(r.result);
        if (rewrite(r, (v0 < v0), false)) return mutate(r.result);
        if (rewrite(r, (max(v0, v1) < v0), false)) return mutate(r.result);
        if (rewrite(r, (max(v1, v0) < v0), false)) return mutate(r.result);
        if (rewrite(r, (v0 < min(v0, v1)), false)) return mutate(r.result);
        if (rewrite(r, (v0 < min(v1, v0)), false)) return mutate(r.result);
        if (rewrite(r, ((v0 + c0) < v1), (v0 < (v1 + fold(-c0))))) return mutate(r.result);
        if (rewrite(r, (c0 < (v0 + c1)), (fold((c0 - c1)) < v0))) return mutate(r.result);
        if (rewrite(r, ((v0 - v1) < v2), (v0 < (v2 + v1)))) return mutate(r.result);
        if (rewrite(r, (v2 < (v0 - v1)), ((v2 + v1) < v0))) return mutate(r.result);
        if (rewrite(r, (((v0 - v1) + v2) < v3), ((v0 + v2) < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, ((v2 + (v0 - v1)) < v3), ((v0 + v2) < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, (v3 < ((v0 - v1) + v2)), ((v3 + v1) < (v0 + v2)))) return mutate(r.result);
        if (rewrite(r, (v3 < (v2 + (v0 - v1))), ((v3 + v1) < (v0 + v2)))) return mutate(r.result);
        if (rewrite(r, ((((v0 - v1) + v2) + v4) < v3), (((v0 + v2) + v4) < (v3 + v1)))) return mutate(r.result);
        if (rewrite(r, (((v2 + (v0 - v1)) + v4) < v3), (((v0 + v2) + v4) < (v3 + v1)))) return mutate(r.result);
        if (rewrite(r, ((v4 + ((v0 - v1) + v2)) < v3), (((v0 + v2) + v4) < (v3 + v1)))) return mutate(r.result);
        if (rewrite(r, ((v4 + (v2 + (v0 - v1))) < v3), (((v0 + v2) + v4) < (v3 + v1)))) return mutate(r.result);
        if (rewrite(r, (v3 < (((v0 - v1) + v2) + v4)), ((v3 + v1) < ((v0 + v2) + v4)))) return mutate(r.result);
        if (rewrite(r, (v3 < ((v2 + (v0 - v1)) + v4)), ((v3 + v1) < ((v0 + v2) + v4)))) return mutate(r.result);
        if (rewrite(r, (v3 < (v4 + ((v0 - v1) + v2))), ((v3 + v1) < ((v0 + v2) + v4)))) return mutate(r.result);
        if (rewrite(r, (v3 < (v4 + (v2 + (v0 - v1)))), ((v3 + v1) < ((v0 + v2) + v4)))) return mutate(r.result);
        if (rewrite(r, (v0 < (v0 + v1)), (0 < v1))) return mutate(r.result);
        if (rewrite(r, ((v0 + v1) < v0), (v1 < 0))) return mutate(r.result);
        if (rewrite(r, ((v0 + v1) < (v0 + v2)), (v1 < v2))) return mutate(r.result);
        if (rewrite(r, ((v0 + v1) < (v2 + v0)), (v1 < v2))) return mutate(r.result);
        if (rewrite(r, ((v1 + v0) < (v0 + v2)), (v1 < v2))) return mutate(r.result);
        if (rewrite(r, ((v1 + v0) < (v2 + v0)), (v1 < v2))) return mutate(r.result);
        if (rewrite(r, (((v0 + v1) + v3) < (v0 + v2)), ((v1 + v3) < v2))) return mutate(r.result);
        if (rewrite(r, (((v1 + v0) + v3) < (v0 + v2)), ((v1 + v3) < v2))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v0 + v1)) < (v0 + v2)), ((v1 + v3) < v2))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v1 + v0)) < (v0 + v2)), ((v1 + v3) < v2))) return mutate(r.result);
        if (rewrite(r, (((v0 + v1) + v3) < (v2 + v0)), ((v1 + v3) < v2))) return mutate(r.result);
        if (rewrite(r, (((v1 + v0) + v3) < (v2 + v0)), ((v1 + v3) < v2))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v0 + v1)) < (v2 + v0)), ((v1 + v3) < v2))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v1 + v0)) < (v2 + v0)), ((v1 + v3) < v2))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < ((v0 + v1) + v3)), (v2 < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < ((v1 + v0) + v3)), (v2 < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < (v3 + (v0 + v1))), (v2 < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < (v3 + (v1 + v0))), (v2 < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < ((v0 + v1) + v3)), (v2 < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < ((v1 + v0) + v3)), (v2 < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < (v3 + (v0 + v1))), (v2 < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < (v3 + (v1 + v0))), (v2 < (v1 + v3)))) return mutate(r.result);
        if (rewrite(r, (((v0 + v1) + v3) < ((v0 + v2) + v4)), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, (((v1 + v0) + v3) < ((v0 + v2) + v4)), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, (((v0 + v1) + v3) < ((v2 + v0) + v4)), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, (((v1 + v0) + v3) < ((v2 + v0) + v4)), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v0 + v1)) < ((v0 + v2) + v4)), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v1 + v0)) < ((v0 + v2) + v4)), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v0 + v1)) < ((v2 + v0) + v4)), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v1 + v0)) < ((v2 + v0) + v4)), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, (((v0 + v1) + v3) < (v4 + (v0 + v2))), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, (((v1 + v0) + v3) < (v4 + (v0 + v2))), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, (((v0 + v1) + v3) < (v4 + (v2 + v0))), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, (((v1 + v0) + v3) < (v4 + (v2 + v0))), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v0 + v1)) < (v4 + (v0 + v2))), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v1 + v0)) < (v4 + (v0 + v2))), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v0 + v1)) < (v4 + (v2 + v0))), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v3 + (v1 + v0)) < (v4 + (v2 + v0))), ((v1 + v3) < (v2 + v4)))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) < (v1 * c0)), (v0 < v1), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) < (v1 * c0)), (v1 < v0), (c0 < 0))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) < c1), (v0 < fold((((c1 + c0) - 1) / c0))), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, (c1 < (v0 * c0)), (fold((c1 / c0)) < v0), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, ((v0 / c0) < c1), (v0 < (c1 * c0)), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, (c0 < (v0 / c1)), (fold((((c0 + 1) * c1) - 1)) < v0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (min((v0 + c0), v1) < (v0 + c1)), (fold((c0 < c1)) || (v1 < (v0 + c1))))) return mutate(r.result);
        if (rewrite(r, (min(v1, (v0 + c0)) < (v0 + c1)), (fold((c0 < c1)) || (v1 < (v0 + c1))))) return mutate(r.result);
        if (rewrite(r, (max((v0 + c0), v1) < (v0 + c1)), (fold((c0 < c1)) && (v1 < (v0 + c1))))) return mutate(r.result);
        if (rewrite(r, (max(v1, (v0 + c0)) < (v0 + c1)), (fold((c0 < c1)) && (v1 < (v0 + c1))))) return mutate(r.result);
        if (rewrite(r, (v0 < (min((v0 + c0), v1) + c1)), (fold((0 < (c0 + c1))) && (v0 < (v1 + c1))))) return mutate(r.result);
        if (rewrite(r, (v0 < (min(v1, (v0 + c0)) + c1)), (fold((0 < (c0 + c1))) && (v0 < (v1 + c1))))) return mutate(r.result);
        if (rewrite(r, (v0 < (max((v0 + c0), v1) + c1)), (fold((0 < (c0 + c1))) || (v0 < (v1 + c1))))) return mutate(r.result);
        if (rewrite(r, (v0 < (max(v1, (v0 + c0)) + c1)), (fold((0 < (c0 + c1))) || (v0 < (v1 + c1))))) return mutate(r.result);
        if (rewrite(r, (min(v0, v1) < (v0 + c1)), (fold((0 < c1)) || (v1 < (v0 + c1))))) return mutate(r.result);
        if (rewrite(r, (min(v1, v0) < (v0 + c1)), (fold((0 < c1)) || (v1 < (v0 + c1))))) return mutate(r.result);
        if (rewrite(r, (max(v0, v1) < (v0 + c1)), (fold((0 < c1)) && (v1 < (v0 + c1))))) return mutate(r.result);
        if (rewrite(r, (max(v1, v0) < (v0 + c1)), (fold((0 < c1)) && (v1 < (v0 + c1))))) return mutate(r.result);
        if (rewrite(r, (v0 < (min(v0, v1) + c1)), (fold((0 < c1)) && (v0 < (v1 + c1))))) return mutate(r.result);
        if (rewrite(r, (v0 < (min(v1, v0) + c1)), (fold((0 < c1)) && (v0 < (v1 + c1))))) return mutate(r.result);
        if (rewrite(r, (v0 < (max(v0, v1) + c1)), (fold((0 < c1)) || (v0 < (v1 + c1))))) return mutate(r.result);
        if (rewrite(r, (v0 < (max(v1, v0) + c1)), (fold((0 < c1)) || (v0 < (v1 + c1))))) return mutate(r.result);
        if (rewrite(r, (min((v0 + c0), v1) < v0), (fold((c0 < 0)) || (v1 < v0)))) return mutate(r.result);
        if (rewrite(r, (min(v1, (v0 + c0)) < v0), (fold((c0 < 0)) || (v1 < v0)))) return mutate(r.result);
        if (rewrite(r, (max((v0 + c0), v1) < v0), (fold((c0 < 0)) && (v1 < v0)))) return mutate(r.result);
        if (rewrite(r, (max(v1, (v0 + c0)) < v0), (fold((c0 < 0)) && (v1 < v0)))) return mutate(r.result);
        if (rewrite(r, (v0 < min((v0 + c0), v1)), (fold((0 < c0)) && (v0 < v1)))) return mutate(r.result);
        if (rewrite(r, (v0 < min(v1, (v0 + c0))), (fold((0 < c0)) && (v0 < v1)))) return mutate(r.result);
        if (rewrite(r, (v0 < max((v0 + c0), v1)), (fold((0 < c0)) || (v0 < v1)))) return mutate(r.result);
        if (rewrite(r, (v0 < max(v1, (v0 + c0))), (fold((0 < c0)) || (v0 < v1)))) return mutate(r.result);
        if (rewrite(r, (min(v0, v1) < v0), (v1 < v0))) return mutate(r.result);
        if (rewrite(r, (min(v1, v0) < v0), (v1 < v0))) return mutate(r.result);
        if (rewrite(r, (v0 < max(v0, v1)), (v0 < v1))) return mutate(r.result);
        if (rewrite(r, (v0 < max(v1, v0)), (v0 < v1))) return mutate(r.result);
        if (rewrite(r, (min(v1, c0) < c1), (fold((c0 < c1)) || (v1 < c1)))) return mutate(r.result);
        if (rewrite(r, (max(v1, c0) < c1), (fold((c0 < c1)) && (v1 < c1)))) return mutate(r.result);
        if (rewrite(r, (c1 < min(v1, c0)), (fold((c1 < c0)) && (c1 < v1)))) return mutate(r.result);
        if (rewrite(r, (c1 < max(v1, c0)), (fold((c1 < c0)) || (c1 < v1)))) return mutate(r.result);
        if (rewrite(r, (v0 < select(v1, (v0 + c0), v2)), (!(v1) && (v0 < v2)), (c0 <= 0))) return mutate(r.result);
        if (rewrite(r, (v0 < select(v1, (v0 + c0), v2)), (v1 || (v0 < v2)), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < select(v1, v2, (v0 + c0))), (v1 && (v0 < v2)), (c0 <= 0))) return mutate(r.result);
        if (rewrite(r, (v0 < select(v1, v2, (v0 + c0))), (!(v1) || (v0 < v2)), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < (select(v1, (v0 + c0), v2) + c1)), (!(v1) && (v0 < (v2 + c1))), ((c0 + c1) <= 0))) return mutate(r.result);
        if (rewrite(r, (v0 < (select(v1, (v0 + c0), v2) + c1)), (v1 || (v0 < (v2 + c1))), ((c0 + c1) > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < (select(v1, v2, (v0 + c0)) + c1)), (v1 && (v0 < (v2 + c1))), ((c0 + c1) <= 0))) return mutate(r.result);
        if (rewrite(r, (v0 < (select(v1, v2, (v0 + c0)) + c1)), (!(v1) || (v0 < (v2 + c1))), ((c0 + c1) > 0))) return mutate(r.result);
        if (rewrite(r, (select(v1, (v0 + c0), v2) < v0), (!(v1) && (v2 < v0)), (c0 >= 0))) return mutate(r.result);
        if (rewrite(r, (select(v1, (v0 + c0), v2) < v0), (v1 || (v2 < v0)), (c0 < 0))) return mutate(r.result);
        if (rewrite(r, (select(v1, v2, (v0 + c0)) < v0), (v1 && (v2 < v0)), (c0 >= 0))) return mutate(r.result);
        if (rewrite(r, (select(v1, v2, (v0 + c0)) < v0), (!(v1) || (v2 < v0)), (c0 < 0))) return mutate(r.result);
        if (rewrite(r, (select(v1, (v0 + c0), v2) < (v0 + c1)), (!(v1) && (v2 < (v0 + c1))), (c0 >= c1))) return mutate(r.result);
        if (rewrite(r, (select(v1, (v0 + c0), v2) < (v0 + c1)), (v1 || (v2 < (v0 + c1))), (c0 < c1))) return mutate(r.result);
        if (rewrite(r, (select(v1, v2, (v0 + c0)) < (v0 + c1)), (v1 && (v2 < (v0 + c1))), (c0 >= c1))) return mutate(r.result);
        if (rewrite(r, (select(v1, v2, (v0 + c0)) < (v0 + c1)), (!(v1) || (v2 < (v0 + c1))), (c0 < c1))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) < (v1 * c1)), (v0 < (v1 * fold((c1 / c0)))), (((c1 % c0) == 0) && (c0 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) < (v1 * c1)), ((v0 * fold((c0 / c1))) < v1), (((c0 % c1) == 0) && (c1 > 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 * c0) < ((v1 * c0) + c1)), (v0 < (v1 + fold((((c1 + c0) - 1) / c0)))), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, (((v0 * c0) + c1) < (v1 * c0)), ((v0 + fold((c1 / c0))) < v1), (c0 > 0))) return mutate(r.result);
        if (rewrite(r, (((((v0 + c0) / c1) * c1) + v3) < (v0 + v2)), ((v3 + c0) < (((v0 + c0) % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v3 + (((v0 + c0) / c1) * c1)) < (v0 + v2)), ((v3 + c0) < (((v0 + c0) % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (((((v0 + c0) / c1) * c1) + v3) < (v2 + v0)), ((v3 + c0) < (((v0 + c0) % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v3 + (((v0 + c0) / c1) * c1)) < (v2 + v0)), ((v3 + c0) < (((v0 + c0) % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < ((((v0 + c0) / c1) * c1) + v3)), ((((v0 + c0) % c1) + v2) < (v3 + c0)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < (v3 + (((v0 + c0) / c1) * c1))), ((((v0 + c0) % c1) + v2) < (v3 + c0)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < ((((v0 + c0) / c1) * c1) + v3)), ((((v0 + c0) % c1) + v2) < (v3 + c0)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < (v3 + (((v0 + c0) / c1) * c1))), ((((v0 + c0) % c1) + v2) < (v3 + c0)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((((v0 + c0) / c1) * c1) < (v0 + v2)), (c0 < (((v0 + c0) % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((((v0 + c0) / c1) * c1) < (v2 + v0)), (c0 < (((v0 + c0) % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < (((v0 + c0) / c1) * c1)), ((((v0 + c0) % c1) + v2) < c0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < (((v0 + c0) / c1) * c1)), ((((v0 + c0) % c1) + v2) < c0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (((((v0 + c0) / c1) * c1) + v3) < v0), ((v3 + c0) < ((v0 + c0) % c1)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v3 + (((v0 + c0) / c1) * c1)) < v0), ((v3 + c0) < ((v0 + c0) % c1)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < ((((v0 + c0) / c1) * c1) + v3)), (((v0 + c0) % c1) < (v3 + c0)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < (v3 + (((v0 + c0) / c1) * c1))), (((v0 + c0) % c1) < (v3 + c0)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((((v0 / c1) * c1) + v3) < (v0 + v2)), (v3 < ((v0 % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v3 + ((v0 / c1) * c1)) < (v0 + v2)), (v3 < ((v0 % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((((v0 / c1) * c1) + v3) < (v2 + v0)), (v3 < ((v0 % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v3 + ((v0 / c1) * c1)) < (v2 + v0)), (v3 < ((v0 % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < (((v0 / c1) * c1) + v3)), (((v0 % c1) + v2) < v3), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < (v3 + ((v0 / c1) * c1))), (((v0 % c1) + v2) < v3), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < (((v0 / c1) * c1) + v3)), (((v0 % c1) + v2) < v3), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < (v3 + ((v0 / c1) * c1))), (((v0 % c1) + v2) < v3), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((((v0 + c0) / c1) * c1) < v0), (c0 < ((v0 + c0) % c1)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < (((v0 + c0) / c1) * c1)), (((v0 + c0) % c1) < c0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (((v0 / c1) * c1) < (v0 + v2)), (0 < ((v0 % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (((v0 / c1) * c1) < (v2 + v0)), (0 < ((v0 % c1) + v2)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v0 + v2) < ((v0 / c1) * c1)), (((v0 % c1) + v2) < 0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v2 + v0) < ((v0 / c1) * c1)), (((v0 % c1) + v2) < 0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((((v0 / c1) * c1) + v3) < v0), (v3 < (v0 % c1)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, ((v3 + ((v0 / c1) * c1)) < v0), (v3 < (v0 % c1)), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < (((v0 / c1) * c1) + v3)), ((v0 % c1) < v3), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < (v3 + ((v0 / c1) * c1))), ((v0 % c1) < v3), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (((v0 / c1) * c1) < v0), ((v0 % c1) != 0), (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (v0 < ((v0 / c1) * c1)), false, (c1 > 0))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < ((v0 + c2) / c0)), false, ((c0 > 0) && (c1 >= c2)))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < ((v0 + c2) / c0)), true, ((c0 > 0) && (c1 <= (c2 - c0))))) return mutate(r.result);
        if (rewrite(r, ((v0 / c0) < ((v0 + c2) / c0)), false, ((c0 > 0) && (0 >= c2)))) return mutate(r.result);
        if (rewrite(r, ((v0 / c0) < ((v0 + c2) / c0)), true, ((c0 > 0) && (0 <= (c2 - c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < (v0 / c0)), false, ((c0 > 0) && (c1 >= 0)))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < (v0 / c0)), true, ((c0 > 0) && (c1 <= (0 - c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < ((v0 / c0) + c2)), false, ((c0 > 0) && (c1 >= (c2 * c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < ((v0 / c0) + c2)), true, ((c0 > 0) && (c1 <= ((c2 * c0) - c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < (min((v0 / c0), v1) + c2)), false, ((c0 > 0) && (c1 >= (c2 * c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < (max((v0 / c0), v1) + c2)), true, ((c0 > 0) && (c1 <= ((c2 * c0) - c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < min(((v0 + c2) / c0), v1)), false, ((c0 > 0) && (c1 >= c2)))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < max(((v0 + c2) / c0), v1)), true, ((c0 > 0) && (c1 <= (c2 - c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < min((v0 / c0), v1)), false, ((c0 > 0) && (c1 >= 0)))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < max((v0 / c0), v1)), true, ((c0 > 0) && (c1 <= (0 - c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < (min(v1, (v0 / c0)) + c2)), false, ((c0 > 0) && (c1 >= (c2 * c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < (max(v1, (v0 / c0)) + c2)), true, ((c0 > 0) && (c1 <= ((c2 * c0) - c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < min(v1, ((v0 + c2) / c0))), false, ((c0 > 0) && (c1 >= c2)))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < max(v1, ((v0 + c2) / c0))), true, ((c0 > 0) && (c1 <= (c2 - c0))))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < min(v1, (v0 / c0))), false, ((c0 > 0) && (c1 >= 0)))) return mutate(r.result);
        if (rewrite(r, (((v0 + c1) / c0) < max(v1, (v0 / c0))), true, ((c0 > 0) && (c1 <= (0 - c0))))) return mutate(r.result);
        if (rewrite(r, (max(((v0 + c2) / c0), v1) < ((v0 + c1) / c0)), false, ((c0 > 0) && (c2 >= c1)))) return mutate(r.result);
        if (rewrite(r, (min(((v0 + c2) / c0), v1) < ((v0 + c1) / c0)), true, ((c0 > 0) && (c2 <= (c1 - c0))))) return mutate(r.result);
        if (rewrite(r, (max((v0 / c0), v1) < ((v0 + c1) / c0)), false, ((c0 > 0) && (0 >= c1)))) return mutate(r.result);
        if (rewrite(r, (min((v0 / c0), v1) < ((v0 + c1) / c0)), true, ((c0 > 0) && (0 <= (c1 - c0))))) return mutate(r.result);
        if (rewrite(r, (max(v1, ((v0 + c2) / c0)) < ((v0 + c1) / c0)), false, ((c0 > 0) && (c2 >= c1)))) return mutate(r.result);
        if (rewrite(r, (min(v1, ((v0 + c2) / c0)) < ((v0 + c1) / c0)), true, ((c0 > 0) && (c2 <= (c1 - c0))))) return mutate(r.result);
        if (rewrite(r, (max(v1, (v0 / c0)) < ((v0 + c1) / c0)), false, ((c0 > 0) && (0 >= c1)))) return mutate(r.result);
        if (rewrite(r, (min(v1, (v0 / c0)) < ((v0 + c1) / c0)), true, ((c0 > 0) && (0 <= (c1 - c0))))) return mutate(r.result);
        if (rewrite(r, (max(((v0 + c2) / c0), v1) < ((v0 / c0) + c1)), false, ((c0 > 0) && (c2 >= (c1 * c0))))) return mutate(r.result);
        if (rewrite(r, (min(((v0 + c2) / c0), v1) < ((v0 / c0) + c1)), true, ((c0 > 0) && (c2 <= ((c1 * c0) - c0))))) return mutate(r.result);
        if (rewrite(r, (max(v1, ((v0 + c2) / c0)) < ((v0 / c0) + c1)), false, ((c0 > 0) && (c2 >= (c1 * c0))))) return mutate(r.result);
        if (rewrite(r, (min(v1, ((v0 + c2) / c0)) < ((v0 / c0) + c1)), true, ((c0 > 0) && (c2 <= ((c1 * c0) - c0))))) return mutate(r.result);
        if (rewrite(r, ((v0 / c0) < min(((v0 + c2) / c0), v1)), false, ((c0 > 0) && (c2 < 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 / c0) < max(((v0 + c2) / c0), v1)), true, ((c0 > 0) && (c0 <= c2)))) return mutate(r.result);
        if (rewrite(r, ((v0 / c0) < min(v1, ((v0 + c2) / c0))), false, ((c0 > 0) && (c2 < 0)))) return mutate(r.result);
        if (rewrite(r, ((v0 / c0) < max(v1, ((v0 + c2) / c0))), true, ((c0 > 0) && (c0 <= c2)))) return mutate(r.result);
        if (rewrite(r, (max(((v0 + c2) / c0), v1) < (v0 / c0)), false, ((c0 > 0) && (c2 >= 0)))) return mutate(r.result);
        if (rewrite(r, (min(((v0 + c2) / c0), v1) < (v0 / c0)), true, ((c0 > 0) && ((c2 + c0) <= 0)))) return mutate(r.result);
        if (rewrite(r, (max(v1, ((v0 + c2) / c0)) < (v0 / c0)), false, ((c0 > 0) && (c2 >= 0)))) return mutate(r.result);
        if (rewrite(r, (min(v1, ((v0 + c2) / c0)) < (v0 / c0)), true, ((c0 > 0) && ((c2 + c0) <= 0)))) return mutate(r.result);
        if (rewrite(r, select(true, v0, v1), v0)) return mutate(r.result);
        if (rewrite(r, select(false, v0, v1), v1)) return mutate(r.result);
        if (rewrite(r, select(v0, v1, v1), v1)) return mutate(r.result);
        if (rewrite(r, select((v0 != v1), v2, v3), select((v0 == v1), v3, v2))) return mutate(r.result);
        if (rewrite(r, select((v0 <= v1), v2, v3), select((v1 < v0), v3, v2))) return mutate(r.result);
        if (rewrite(r, select(v0, select(v1, v2, v3), v2), select((v0 && !(v1)), v3, v2))) return mutate(r.result);
        if (rewrite(r, select(v0, select(v1, v2, v3), v3), select((v0 && v1), v2, v3))) return mutate(r.result);
        if (rewrite(r, select(v0, v1, select(v2, v1, v3)), select((v0 || v2), v1, v3))) return mutate(r.result);
        if (rewrite(r, select(v0, v1, select(v2, v3, v1)), select((v0 || !(v2)), v1, v3))) return mutate(r.result);
        if (rewrite(r, select(v0, select(v0, v1, v2), v3), select(v0, v1, v3))) return mutate(r.result);
        if (rewrite(r, select(v0, v1, select(v0, v2, v3)), select(v0, v1, v3))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 + v2), (v1 + v3)), (v1 + select(v0, v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 + v2), (v3 + v1)), (v1 + select(v0, v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v2 + v1), (v1 + v3)), (v1 + select(v0, v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v2 + v1), (v3 + v1)), (select(v0, v2, v3) + v1))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 - v2), (v1 - v3)), (v1 - select(v0, v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 - v2), (v1 + v3)), (v1 + select(v0, -v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 + v2), (v1 - v3)), (v1 + select(v0, v2, -v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 - v2), (v3 + v1)), (v1 + select(v0, -v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v2 + v1), (v1 - v3)), (v1 + select(v0, v2, -v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v2 - v1), (v3 - v1)), (select(v0, v2, v3) - v1))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 * v2), (v1 * v3)), (v1 * select(v0, v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 * v2), (v3 * v1)), (v1 * select(v0, v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v2 * v1), (v1 * v3)), (v1 * select(v0, v2, v3)))) return mutate(r.result);
        if (rewrite(r, select(v0, (v2 * v1), (v3 * v1)), (select(v0, v2, v3) * v1))) return mutate(r.result);
        if (rewrite(r, select((v0 < v1), v0, v1), min(v0, v1))) return mutate(r.result);
        if (rewrite(r, select((v0 < v1), v1, v0), max(v0, v1))) return mutate(r.result);
        if (rewrite(r, select(v0, (v1 * c0), c1), (select(v0, v1, fold((c1 / c0))) * c0), ((c1 % c0) == 0))) return mutate(r.result);
        if (rewrite(r, select(v0, c0, (v1 * c1)), (select(v0, fold((c0 / c1)), v1) * c1), ((c0 % c1) == 0))) return mutate(r.result);
        if (rewrite(r, select((c0 < v0), (v0 + c1), c2), max((v0 + c1), c2), ((c2 == (c0 + c1)) || (c2 == ((c0 + c1) + 1))))) return mutate(r.result);
        if (rewrite(r, select((v0 < c0), c1, (v0 + c2)), max((v0 + c2), c1), ((c1 == (c0 + c2)) || ((c1 + 1) == (c0 + c2))))) return mutate(r.result);
        if (rewrite(r, select((c0 < v0), c1, (v0 + c2)), min((v0 + c2), c1), ((c1 == (c0 + c2)) || (c1 == ((c0 + c2) + 1))))) return mutate(r.result);
        if (rewrite(r, select((v0 < c0), (v0 + c1), c2), min((v0 + c1), c2), ((c2 == (c0 + c1)) || ((c2 + 1) == (c0 + c1))))) return mutate(r.result);
        if (rewrite(r, select((c0 < v0), v0, c1), max(v0, c1), (c1 == (c0 + 1)))) return mutate(r.result);
        if (rewrite(r, select((v0 < c0), c1, v0), max(v0, c1), ((c1 + 1) == c0))) return mutate(r.result);
        if (rewrite(r, select((c0 < v0), c1, v0), min(v0, c1), (c1 == (c0 + 1)))) return mutate(r.result);
        if (rewrite(r, select((v0 < c0), v0, c1), min(v0, c1), ((c1 + 1) == c0))) return mutate(r.result);
        return new_e;
    }
};

Expr apply_rewrite_rules(Expr e) {
    return Simplify().mutate(e);
}

int main(int argc, char **argv) {

    Var v0, v1, v2, v3, v4;

    // Some successful cases
    Expr good[] = {
        (((((((v0 + v1) + -2) <= ((v0 + v1) + -2))) && ((((v2 + v0) + v1) + -1) >= (((v2 + v0) + v1) + -1))) && (((v0 - v1) + -2) <= ((v0 - v1) + -2))) && ((((v2 + v0) - v1) + 1) >= (((v2 + v0) - v1) + 1))),
        (((((((min(select((4 < v0), v1, ((v0 + v1) + -5)), ((min(v0, 4) + v1) + -5)) <= ((min(v0, 4) + v1) + -5))) && (((min((((v0 + -1)/4)*4), (v0 + -4)) + v1) + 3) >= ((min((((v0 + -1)/4)*4), (v0 + -4)) + v1) + 3))) && ((v2 + -1) <= (v2 + -1))) && ((v3 + v2) >= (v3 + v2))) && (v4 <= v4)) && (v4 >= v4)),
        (((v0*2) + 1) < ((v0 + 1)*2)),
        ((v0*2) >= (v0*2)),
        ((v0*8) >= (v0*8)),
        ((((((v0*2) <= (v0*2))) && (((v0*2) + 1) >= ((v0*2) + 1))) && (0 <= 0)) && ((2 - 1) >= 1)),
        (((((((min(select((v0 < ((v1 + v0) + -1)), v0, (v0 + -1)), (v0 + -1)) <= (v0 + -1))) && (((v1 + v0) + -1) >= ((v1 + v0) + -1))) && ((v2 + -1) <= (v2 + -1))) && ((v3 + v2) >= (v3 + v2))) && (v4 <= v4)) && (v4 >= v4)),
        ((((v0 + v1) - v2) + 1) <= (((v0 + v1) - v2) + 1)),
        ((((((((v0 + -1) <= (v0 + -1))) && (((v1 + v0) + -1) >= ((v1 + v0) + -1))) && ((v2 + -1) <= (v2 + -1))) && ((v3 + v2) >= (v3 + v2))) && (v4 <= v4)) && (v4 >= v4)),
        ((v0/3) >= (v0/3)),
        (v0 >= ((v0 - 1) + 1)),
        (((v0 + v1) + -2) >= ((v0 + v1) + -2)),
        (v0 < (v0 + 1)),
        (((v0*8) + 7) < ((v0 + 1)*8)),
        ((v0/2) >= (v0/2)),
        ((min((v0*4), (v1 + -4)) + v2) >= (min((v0*4), (v1 + -4)) + v2)),
        ((((((v0*2) <= (v0*2))) && ((((v1 + v0)*2) + -1) >= (((v1 + v0)*2) + -1))) && ((v2*2) <= (v2*2))) && (((v2*2) + 1) >= ((v2*2) + 1))),
        (((v0 - v1) + -2) >= ((v0 - v1) + -2)),
        ((v0 + -1) >= (v0 + -1)),
        (v0 >= min(v0, 0)),
        (v0 >= v0),
        (((((min(select((v0 < ((v1 + v0) + -1)), (v0 + 3), v0), v0) <= v0)) && (((v1 + v0) + 2) >= ((v1 + v0) + 2))) && ((v2*2) <= (v2*2))) && (((v2*2) + 1) >= ((v2*2) + 1))),
        ((((v0 + 3)/4) - max((v0/4), 0)) <= 1),
        ((v0*2) >= ((((v0 - 1)*2) + 1) + 1)),
        ((((v0 + 7)/8) - max((v0/8), 0)) <= 1),
        ((((v0 + v1) + 21)/8) >= (((v0 + v1) + -14)/8)),
        ((((v0 + v1) + 45)/16) >= (((v0 + v1) + -30)/16)),
        ((((v0 + v1) + 9)/4) >= (((v0 + v1) + -6)/4)),
        ((((((v1 - v2) + 9)/8) - max((((v1 - v2) + 2)/8), 0))) <= 1),
        ((((((v1 - v2)/8) - max((((v1 - v2) + 1)/8), 0)) + 1)) <= 1),
    };

    // Some failure cases, simplified down to the point where they get stuck
    Expr bad[] = {
        ((min(min((min((min((v0*4), 63) + (v1*65)), (v2 + 7)) + 2), ((v1*65) + (v0*4))), (v1*65)) + -4) <=
         min(min((v1*65), (min((v1*65), (v2 + 3)) + 2)), (min((v1*65), (v2 + 5)) + 3))),
        ((max(min((((v0 + 36)/16) - v1), 11), -1) + -1) <= max(min((((v0 + 20)/16) - v1), 11), -1)),
        ((min(((((v0 - v1) + 145)/2)*2), ((v0 - v1) + 144)) + 2) <= min(((((v0 - v1) + 147)/2)*2), ((v0 - v1) + 146))),
        (((min(((((v0 - v1) + 278)/4)*4), ((v0 - v1) + 275)) + v1) + -275) <= v0),
        ((min(min((min((v0 + 5), ((v1*65) + (v2*258))) + 4), ((v2*258) + (v1*65))), (v2*258)) + -9) <= min(((v2*258) + 56), v0)),
        ((min((v0*75), 224) + ((v1*256) + v2)) < (select((-1 < v0), (((v0*75) + ((v1*256) + v2)) + 61), (((v1*256) + v2) + -29)) + -60)),
        ((min(min((((v0*15) + v1)*2), ((v2 - (v3*117)) + 25)), 115) + (min((((v2 - (((v0*15) + v1)*2)) + 26)/117), v3)*117)) <=
         min((min((((v0*15) + v1)*2), 115) + (v3*117)), (v2 + 25))),
        ((min(min((min((v0 + 16), v1) + 19), v0), (v2*261)) + -19) <= min(min(((v2*261) + 47), v1), (min(((v2*261) + 49), v1) + 4))),
        ((min((v0*8), 135) + v1) < (min(select((-1 < v0), (((v0*8) + v1) + 8), v1), (((v0*8) + v1) + 8)) + -7)),
        ((min((v0*4), 286) + ((v1*256) + v2)) < (select((-1 < v0), (((v0*4) + ((v1*256) + v2)) + -10), (((v1*256) + v2) + -19)) + 7)),
        ((v0 + 14) <= (((((v0 - (v1 + v2)) + 21)/8)*8) + (v1 + v2))),
        ((min((v0*19), 132) + v1) < (min(select((-1 < v0), (((v0*19) + v1) + 19), v1), (((v0*19) + v1) + 19)) + -18)),
        ((min((v0*4), 127) + ((v1*128) + v2)) < (select((-1 < v0), (((v0*4) + ((v1*128) + v2)) + 5), (((v1*128) + v2) + -3)) + -4)),
        ((min(min((min((v0 + 13), v1) + 4), v1), (v2*130)) + -17) <= min(((v2*130) + 16), v0)),
        ((min((v0*8), 126) + v1) < (select((-1 < v0), (((v0*8) + v1) + 3), (v1 + -11)) + -2)),
        ((min((((((v0 + 6)/8)*8) + ((v1*257) + v2)) + 1), (v3 + v2)) + -121) <=
         (((((min(((((v0 + 14)/8)*8) + ((v1*257) + v2)), ((v3 + v2) + 7)) - (v2 + v4)) + 3)/132)*132) + (v2 + v4))),
        (min(min(((v0*262) + 258), v1), 2) <= min((min(v1, 258) + 16), v1)),
        ((min((v0*68), 474) + ((v1*512) + v2)) < (select((-1 < v0), (((v0*68) + ((v1*512) + v2)) + 51), (((v1*512) + v2) + -23)) + -50)),
        (((((v0 + 33)/73)*73) + 4) <= ((((((v0 + 106)/73)*73) + 7)/77)*77)),
        (min(min(((v0*140) + 99), v1), -25) <= min((min(v1, 99) + 16), v1))
    };

    std::cout << "Successful cases:\n";
    for (Expr e : good) {
        Expr new_e = apply_rewrite_rules(e);
        std::cout << "GOOD: " << e << " -> " << new_e << "\n";
        assert(is_one(new_e));
    }

    std::cout << "Failure cases:\n";
    for (Expr e : bad) {
        Expr new_e = apply_rewrite_rules(e);
        std::cout << "BAD: " << e << " -> " << new_e << "\n";
    }
}
