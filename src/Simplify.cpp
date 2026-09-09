#include "Simplify.h"
#include "Simplify_Internal.h"

#include "CSE.h"
#include "IRMutator.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

Simplify::Simplify(const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai) {
    // Only respect the constant bounds from the containing scope.
    for (auto iter = bi->cbegin(); iter != bi->cend(); ++iter) {
        ExprInfo info;
        if (auto i_min = as_const_int(iter.value().min)) {
            info.bounds.min_defined = true;
            info.bounds.min = *i_min;
        }
        if (auto i_max = as_const_int(iter.value().max)) {
            info.bounds.max_defined = true;
            info.bounds.max = *i_max;
        }

        if (const auto *a = ai->find(iter.name())) {
            info.alignment = *a;
        }

        if (info.bounds.min_defined ||
            info.bounds.max_defined ||
            info.alignment.modulus != 1) {
            bounds_and_alignment_info.push(iter.name(), info);
        }
    }

    for (auto iter = ai->cbegin(); iter != ai->cend(); ++iter) {
        if (bounds_and_alignment_info.contains(iter.name())) {
            // Already handled
            continue;
        }
        ExprInfo info;
        info.alignment = iter.value();
        bounds_and_alignment_info.push(iter.name(), info);
    }
}

std::pair<std::vector<Expr>, bool> Simplify::mutate_with_changes(const std::vector<Expr> &old_exprs) {
    vector<Expr> new_exprs(old_exprs.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < old_exprs.size(); i++) {
        const Expr &old_e = old_exprs[i];
        Expr new_e = mutate(old_e, nullptr);
        if (!new_e.same_as(old_e)) {
            changed = true;
        }
        new_exprs[i] = std::move(new_e);
    }

    return {std::move(new_exprs), changed};
}

void Simplify::found_buffer_reference(const string &name, size_t dimensions) {
    for (size_t i = 0; i < dimensions; i++) {
        string stride = name + ".stride." + std::to_string(i);
        if (auto *info = var_info.shallow_find(stride)) {
            info->old_uses++;
        }

        string min = name + ".min." + std::to_string(i);
        if (auto *info = var_info.shallow_find(min)) {
            info->old_uses++;
        }
    }

    if (auto *info = var_info.shallow_find(name)) {
        info->old_uses++;
    }
}

namespace {

// Each peeled division multiplies denom by its divisor, so nested ones grow it
// geometrically. Stop well before that stops fitting.
constexpr int64_t max_peel_denominator = 1 << 20;

// Peel constant add/mul/div terms off e, maintaining
//
//     denom * coeff_in * e_in == coeff * e + off + err
//
// All five accumulate into the caller's running totals, so peels compose: an
// additive term under an already-peeled factor is scaled by it first ((x + c0)
// * c1 -> coeff = c1, off = c1 * c0, e = x). Division is the inexact one --
// e / c drops a remainder in [0, c - 1] -- so it scales everything by c and
// banks the remainder in err. Walks existing nodes; builds nothing.
void peel_affine_term(const BaseExprNode *&e, int64_t &coeff, int64_t &off,
                      int64_t &denom, ConstantInterval &err) {
    bool progress = true;
    while (progress) {
        progress = false;
        if (e->node_type == IRNodeType::Add) {
            const Add *add = (const Add *)e;
            if (const IntImm *i = add->b.as<IntImm>()) {
                if (mul_would_overflow(64, coeff, i->value)) {
                    break;
                }
                int64_t term = coeff * i->value;
                if (add_would_overflow(64, off, term)) {
                    break;
                }
                off += term;
                e = add->a.get();
                progress = true;
            } else if (const IntImm *i = add->a.as<IntImm>()) {
                if (mul_would_overflow(64, coeff, i->value)) {
                    break;
                }
                int64_t term = coeff * i->value;
                if (add_would_overflow(64, off, term)) {
                    break;
                }
                off += term;
                e = add->b.get();
                progress = true;
            }
        } else if (e->node_type == IRNodeType::Sub) {
            const Sub *sub = (const Sub *)e;
            if (const IntImm *i = sub->b.as<IntImm>()) {
                if (mul_would_overflow(64, coeff, i->value)) {
                    break;
                }
                int64_t term = coeff * i->value;
                if (sub_would_overflow(64, off, term)) {
                    break;
                }
                off -= term;
                e = sub->a.get();
                progress = true;
            }
        } else if (e->node_type == IRNodeType::Mul) {
            const Mul *mul = (const Mul *)e;
            if (const IntImm *i = mul->b.as<IntImm>()) {
                if (mul_would_overflow(64, coeff, i->value)) {
                    break;
                }
                coeff *= i->value;
                e = mul->a.get();
                progress = true;
            } else if (const IntImm *i = mul->a.as<IntImm>()) {
                if (mul_would_overflow(64, coeff, i->value)) {
                    break;
                }
                coeff *= i->value;
                e = mul->b.get();
                progress = true;
            }
        } else if (e->node_type == IRNodeType::Div) {
            const Div *div = (const Div *)e;
            const IntImm *i = div->b.as<IntImm>();
            // Positive divisors only; a negative one floors the other way.
            if (i && i->value > 0 && i->value <= max_peel_denominator) {
                const int64_t c = i->value;
                if (mul_would_overflow(64, denom, c) || denom * c > max_peel_denominator ||
                    mul_would_overflow(64, off, c) || mul_would_overflow(64, coeff, c - 1)) {
                    break;
                }
                // c * coeff * (a / c) == coeff * a - coeff * r, r == a % c.
                denom *= c;
                off *= c;
                err *= c;
                err -= ConstantInterval(0, c - 1) * coeff;
                e = div->a.get();
                progress = true;
            }
        }
    }
}

// Rewrite (ca * a - cb * b) as
//
//     denom * (ca * a - cb * b) == coeff_a * a' - coeff_b * b' + offset + err
//
// peeling each side independently, so that facts and queries meet at a common
// pair however each was spelled: x vs y + 3, 2 * x vs 4 * x, x vs y / c
// against c * x vs y. Absent a division denom is 1 and err is 0.
void peel_affine_terms(const BaseExprNode *&a, const BaseExprNode *&b,
                       int64_t &coeff_a, int64_t &coeff_b, int64_t &offset,
                       int64_t &denom, ConstantInterval &err) {
    const BaseExprNode *const a_in = a;
    const BaseExprNode *const b_in = b;
    const int64_t ca_in = coeff_a, cb_in = coeff_b;

    int64_t off_a = 0, off_b = 0, denom_a = 1, denom_b = 1;
    ConstantInterval err_a(0, 0), err_b(0, 0);
    peel_affine_term(a, coeff_a, off_a, denom_a, err_a);
    peel_affine_term(b, coeff_b, off_b, denom_b, err_b);

    // Put the two sides over a common denominator.
    if (mul_would_overflow(64, denom_a, denom_b) ||
        mul_would_overflow(64, coeff_a, denom_b) || mul_would_overflow(64, coeff_b, denom_a) ||
        mul_would_overflow(64, off_a, denom_b) || mul_would_overflow(64, off_b, denom_a)) {
        // Nothing useful to say about numbers this large.
        a = a_in;
        b = b_in;
        coeff_a = ca_in;
        coeff_b = cb_in;
        denom = 1;
        err = ConstantInterval(0, 0);
        offset = 0;
        return;
    }
    denom = denom_a * denom_b;
    coeff_a *= denom_b;
    coeff_b *= denom_a;
    off_a *= denom_b;
    off_b *= denom_a;
    err = err_a * denom_b - err_b * denom_a;
    if (!sub_would_overflow(64, off_a, off_b)) {
        offset = off_a - off_b;
    } else {
        offset = 0;
    }
}

// Reduce (ca, cb) to a coprime, sign-canonical (pa, pb) and a scale s with
// (ca, cb) == s * (pa, pb). False if both coefficients are zero. Facts and
// queries both go through this, so a fact about 2 * x - 4 * y and a query
// about 3 * x - 6 * y meet at the pair (1, 2) with scales 2 and 3.
bool reduce_affine_coeffs(int64_t ca, int64_t cb, int64_t &pa, int64_t &pb, int64_t &s) {
    if (ca == 0 && cb == 0) {
        return false;
    }
    int64_t g = gcd(ca, cb);
    pa = ca / g;
    pb = cb / g;
    s = g;
    if (pa < 0 || (pa == 0 && pb < 0)) {
        pa = -pa;
        pb = -pb;
        s = -s;
    }
    return true;
}

}  // namespace

namespace {
// Lowering is single-threaded per pipeline, but several pipelines can be
// lowered at once, so this is per-thread rather than global.
thread_local bool t_regions_have_been_inferred = false;
}  // namespace

bool regions_have_been_inferred() {
    return t_regions_have_been_inferred;
}

ScopedRegionsInferred::ScopedRegionsInferred()
    : old_value(t_regions_have_been_inferred) {
    t_regions_have_been_inferred = true;
}

ScopedRegionsInferred::~ScopedRegionsInferred() {
    t_regions_have_been_inferred = old_value;
}

void Simplify::ScopedFact::learn_difference(const Expr &a, const Expr &b,
                                            const ConstantInterval &diff, bool invert) {
    // Nothing may be ordered from a fact until lowering has finished reading
    // regions and allocation sizes out of the IR. A clamp around an index is
    // part of how those are derived, so removing one on the strength of
    // something we happen to know leaves the region asked for as wide as the
    // unclamped index could reach.
    if (!regions_have_been_inferred()) {
        return;
    }
    // Differences are only meaningful where they can't wrap.
    if (!simplify->no_overflow_int(a.type()) || a.type() != b.type()) {
        return;
    }

    const BaseExprNode *pa = a.get(), *pb = b.get();
    int64_t coeff_a = 1, coeff_b = 1, offset = 0, denom = 1;
    ConstantInterval err(0, 0);
    peel_affine_terms(pa, pb, coeff_a, coeff_b, offset, denom, err);

    // denom * (a - b) == (coeff_a * pa - coeff_b * pb) + offset + err, so
    // solve for the peeled quantity: scale the given bound up by denom and
    // take back the offset and the remainder any peeled division discarded.
    ConstantInterval peeled = diff * denom - offset - err;

    int64_t prim_a, prim_b, scale;
    if (!reduce_affine_coeffs(coeff_a, coeff_b, prim_a, prim_b, scale)) {
        // Both coefficients vanished (something peeled down to 0 * ...).
        return;
    }

    if (invert) {
        // Only a single point is representable, and only on a lattice point:
        // off-lattice, no integer primitive quantity could have hit it anyway.
        if (!peeled.is_single_point() || peeled.min % scale != 0) {
            return;
        }
    }

    // Down from a bound on (scale * primitive) to one on the primitive. Sound
    // but not tightest: [5, 9] / 3 keeps [1, 3] where [2, 3] would do.
    ConstantInterval primitive_bound = peeled / scale;

    simplify->add_difference_key(Simplify::difference_key(pa->hash, pb->hash));
    simplify->known_bounds.push_back(
        Simplify::KnownBound{Expr(pa), Expr(pb), primitive_bound, invert, prim_a, prim_b});
}

void Simplify::ScopedFact::learn_false(const Expr &fact) {
    // Canonicalize the direction of comparisons, so that facts are stored in
    // the same form the simplifier produces when it visits them.
    if (const GT *gt = fact.as<GT>()) {
        learn_false(gt->b < gt->a);
        return;
    } else if (const GE *ge = fact.as<GE>()) {
        learn_false(!(ge->a < ge->b));
        return;
    }

    // Record what this says about the difference between the two sides. And,
    // Not, and the tag intrinsic are handled by the recursion below instead.
    if (const LT *lt = fact.as<LT>()) {
        // !(a < b) -> a - b >= 0
        learn_difference(lt->a, lt->b, ConstantInterval::bounded_below(0), false);
    } else if (const LE *le = fact.as<LE>()) {
        // !(a <= b) -> a - b >= 1
        learn_difference(le->a, le->b, ConstantInterval::bounded_below(1), false);
    } else if (const EQ *eq = fact.as<EQ>()) {
        // !(a == b) -> a - b is anything but zero
        learn_difference(eq->a, eq->b, ConstantInterval::single_point(0), true);
    } else if (const NE *ne = fact.as<NE>()) {
        // !(a != b) -> a - b == 0
        learn_difference(ne->a, ne->b, ConstantInterval::single_point(0), false);
    }

    Simplify::VarInfo info;
    info.old_uses = info.new_uses = 0;
    if (const Variable *v = fact.as<Variable>()) {
        info.replacement = Halide::Internal::const_false(fact.type().lanes());
        simplify->var_info.push(v->name, info);
        pop_list.push_back(v);
    } else if (const NE *ne = fact.as<NE>()) {
        const Variable *v = ne->a.as<Variable>();
        if (v && is_const(ne->b)) {
            info.replacement = ne->b;
            simplify->var_info.push(v->name, info);
            pop_list.push_back(v);
        }
    } else if (const LT *lt = fact.as<LT>()) {
        const Variable *v = lt->a.as<Variable>();
        Simplify::ExprInfo i;
        if (v) {
            simplify->mutate(lt->b, &i);
            if (i.bounds.min_defined) {
                // !(v < i)
                learn_lower_bound(v, i.bounds.min);
            }
        }
        v = lt->b.as<Variable>();
        if (v) {
            simplify->mutate(lt->a, &i);
            if (i.bounds.max_defined) {
                // !(i < v)
                learn_upper_bound(v, i.bounds.max);
            }
        }
    } else if (const LE *le = fact.as<LE>()) {
        const Variable *v = le->a.as<Variable>();
        Simplify::ExprInfo i;
        if (v && v->type.is_int() && v->type.bits() >= 32) {
            simplify->mutate(le->b, &i);
            if (i.bounds.min_defined) {
                // !(v <= i)
                learn_lower_bound(v, i.bounds.min + 1);
            }
        }
        v = le->b.as<Variable>();
        if (v && v->type.is_int() && v->type.bits() >= 32) {
            simplify->mutate(le->a, &i);
            if (i.bounds.max_defined) {
                // !(i <= v)
                learn_upper_bound(v, i.bounds.max - 1);
            }
        }
    } else if (const Call *c = Call::as_tag(fact)) {
        learn_false(c->args[0]);
        return;
    } else if (const Or *o = fact.as<Or>()) {
        // Both must be false
        learn_false(o->a);
        learn_false(o->b);
        return;
    } else if (const Not *n = fact.as<Not>()) {
        learn_true(n->a);
        return;
    }
    if (simplify->falsehoods.insert(fact).second) {
        falsehoods.insert(fact);
    }
}

void Simplify::ScopedFact::learn_upper_bound(const Variable *v, int64_t val) {
    ExprInfo b;
    b.bounds = ConstantInterval::bounded_above(val);
    if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
        b.intersect(*info);
    }
    simplify->bounds_and_alignment_info.push(v->name, b);
    bounds_pop_list.push_back(v);
}

void Simplify::ScopedFact::learn_lower_bound(const Variable *v, int64_t val) {
    ExprInfo b;
    b.bounds = ConstantInterval::bounded_below(val);
    if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
        b.intersect(*info);
    }
    simplify->bounds_and_alignment_info.push(v->name, b);
    bounds_pop_list.push_back(v);
}

void Simplify::ScopedFact::learn_true(const Expr &fact) {
    // Canonicalize the direction of comparisons, so that facts are stored in
    // the same form the simplifier produces when it visits them.
    if (const GT *gt = fact.as<GT>()) {
        learn_true(gt->b < gt->a);
        return;
    } else if (const GE *ge = fact.as<GE>()) {
        learn_true(!(ge->a < ge->b));
        return;
    }

    // Record what this says about the difference between the two sides. And,
    // Not, and the tag intrinsic are handled by the recursion below instead.
    if (const LT *lt = fact.as<LT>()) {
        // a < b -> a - b <= -1
        learn_difference(lt->a, lt->b, ConstantInterval::bounded_above(-1), false);
    } else if (const LE *le = fact.as<LE>()) {
        // a <= b -> a - b <= 0
        learn_difference(le->a, le->b, ConstantInterval::bounded_above(0), false);
    } else if (const EQ *eq = fact.as<EQ>()) {
        // a == b -> a - b == 0
        learn_difference(eq->a, eq->b, ConstantInterval::single_point(0), false);
    } else if (const NE *ne = fact.as<NE>()) {
        // a != b -> a - b is anything but zero
        learn_difference(ne->a, ne->b, ConstantInterval::single_point(0), true);
    }

    Simplify::VarInfo info;
    info.old_uses = info.new_uses = 0;
    if (const Variable *v = fact.as<Variable>()) {
        info.replacement = Halide::Internal::const_true(fact.type().lanes());
        simplify->var_info.push(v->name, info);
        pop_list.push_back(v);
    } else if (const EQ *eq = fact.as<EQ>()) {
        const Variable *v = eq->a.as<Variable>();
        const Mod *m = eq->a.as<Mod>();
        auto modulus = m ? as_const_int(m->b) : std::nullopt;
        auto remainder = m ? as_const_int(eq->b) : std::nullopt;
        // TODO(mcourteaux): A lot of the logic below is hard-coded to let information
        // propagate either from the LHS to the RHS or the other way. There is also
        // special case for when varA == varB is given, to let the info cross-propagate.
        // All of this feels a little conflated and might get clearer if we figure out a
        // neat way to write this down where info can just transparently flow
        // in whichever direction is relevant without having to list all these cases.
        if (v) {
            if (is_const(eq->b)) {
                info.replacement = eq->b;
                simplify->var_info.push(v->name, info);
                pop_list.push_back(v);
            } else if (const auto *vb = eq->b.as<Variable>()) {
                // TODO: consider other cases where we might want to entirely substitute
                info.replacement = eq->b;
                simplify->var_info.push(v->name, info);
                pop_list.push_back(v);

                // Cross-merge the expression info of both variables.
                Simplify::ExprInfo expr_info;
                if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
                    // We already know something about the variable on the LHS
                    expr_info = *info;
                }
                if (const auto *info = simplify->bounds_and_alignment_info.find(vb->name)) {
                    // We already know something about the variable on the RHS
                    expr_info.intersect(*info);
                }

                simplify->bounds_and_alignment_info.push(v->name, expr_info);
                simplify->bounds_and_alignment_info.push(vb->name, expr_info);

                bounds_pop_list.push_back(v);
                bounds_pop_list.push_back(vb);
            } else if (v->type.is_int()) {
                // Visit the rhs again to get bounds and alignment info to propagate to the LHS
                // TODO: Visiting it again is inefficient
                Simplify::ExprInfo expr_info;
                simplify->mutate(eq->b, &expr_info);
                if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
                    // We already know something about this variable and don't want to suppress it.
                    expr_info.intersect(*info);
                }
                simplify->bounds_and_alignment_info.push(v->name, expr_info);
                bounds_pop_list.push_back(v);
            }
        } else if (const Variable *vb = eq->b.as<Variable>()) {
            // ... == x
            // We know that LHS is not a const due to
            // canonicalization, and that the LHS is not a variable or
            // the case above would have triggered. Learn from the
            // bounds and alignment of the LHS.
            // TODO: Visiting it again is inefficient
            Simplify::ExprInfo expr_info;
            simplify->mutate(eq->a, &expr_info);
            if (const auto *info = simplify->bounds_and_alignment_info.find(vb->name)) {
                // We already know something about this variable and don't want to suppress it.
                expr_info.intersect(*info);
            }
            simplify->bounds_and_alignment_info.push(vb->name, expr_info);
            bounds_pop_list.push_back(vb);
        } else if (modulus && remainder && (v = m->a.as<Variable>())) {
            // Learn from expressions of the form x % 8 == 3
            Simplify::ExprInfo expr_info;
            expr_info.alignment.modulus = *modulus;
            expr_info.alignment.remainder = *remainder;
            if (const auto *info = simplify->bounds_and_alignment_info.find(v->name)) {
                // We already know something about this variable and don't want to suppress it.
                expr_info.intersect(*info);
            }
            simplify->bounds_and_alignment_info.push(v->name, expr_info);
            bounds_pop_list.push_back(v);
        }
    } else if (const LT *lt = fact.as<LT>()) {
        const Variable *v = lt->a.as<Variable>();
        Simplify::ExprInfo i;
        if (v && v->type.is_int() && v->type.bits() >= 32) {
            simplify->mutate(lt->b, &i);
            if (i.bounds.max_defined) {
                // v < i
                learn_upper_bound(v, i.bounds.max - 1);
            }
        }
        v = lt->b.as<Variable>();
        if (v && v->type.is_int() && v->type.bits() >= 32) {
            simplify->mutate(lt->a, &i);
            if (i.bounds.min_defined) {
                // i < v
                learn_lower_bound(v, i.bounds.min + 1);
            }
        }
        const Min *min = lt->b.as<Min>();
        if (min) {
            // c < min(a, b) -> c < a, c < b
            learn_true(lt->a < min->a);
            learn_true(lt->a < min->b);
            // c < min(a, b) -> !(a <= c), !(b <= c)
            learn_false(min->a <= lt->a);
            learn_false(min->b <= lt->a);
        }
        const Max *max = lt->a.as<Max>();
        if (max) {
            // max(a, b) < c -> a < c, b < c
            learn_true(max->a < lt->b);
            learn_true(max->b < lt->b);
            // max(a, b) < c -> !(c <= a), !(c <= b)
            learn_false(lt->b <= max->a);
            learn_false(lt->b <= max->b);
        }
    } else if (const LE *le = fact.as<LE>()) {
        const Variable *v = le->a.as<Variable>();
        Simplify::ExprInfo i;
        if (v) {
            simplify->mutate(le->b, &i);
            if (i.bounds.max_defined) {
                // v <= i
                learn_upper_bound(v, i.bounds.max);
            }
        }
        v = le->b.as<Variable>();
        if (v) {
            simplify->mutate(le->a, &i);
            if (i.bounds.min_defined) {
                // i <= v
                learn_lower_bound(v, i.bounds.min);
            }
        }
        const Min *min = le->b.as<Min>();
        if (min) {
            // c <= min(a, b) -> c <= a, c <= b
            learn_true(le->a <= min->a);
            learn_true(le->a <= min->b);
            // c <= min(a, b) -> !(a < c), !(b < c)
            learn_false(min->a < le->a);
            learn_false(min->b < le->a);
        }
        const Max *max = le->a.as<Max>();
        if (max) {
            // max(a, b) <= c -> a <= c, b <= c
            learn_true(max->a <= le->b);
            learn_true(max->b <= le->b);
            // max(a, b) <= c -> !(c < a), !(c < b)
            learn_false(le->b < max->a);
            learn_false(le->b < max->b);
        }
    } else if (const Call *c = Call::as_tag(fact)) {
        learn_true(c->args[0]);
        return;
    } else if (const And *a = fact.as<And>()) {
        // Both must be true
        learn_true(a->a);
        learn_true(a->b);
        return;
    } else if (const Not *n = fact.as<Not>()) {
        learn_false(n->a);
        return;
    }
    if (simplify->truths.insert(fact).second) {
        truths.insert(fact);
    }
}

namespace {
// Is a boolean Expr known to be true or false? Facts are stored in the same
// form the simplifier itself produces, so a comparison has to be canonicalized
// the same way before looking it up.
std::optional<bool> lookup_fact(const Expr &e,
                                const std::set<Expr, IRDeepCompare> &truths,
                                const std::set<Expr, IRDeepCompare> &falsehoods) {
    if (const Not *n = e.as<Not>()) {
        auto known = lookup_fact(n->a, truths, falsehoods);
        return known ? std::make_optional(!*known) : known;
    } else if (const GT *gt = e.as<GT>()) {
        return lookup_fact(gt->b < gt->a, truths, falsehoods);
    } else if (const GE *ge = e.as<GE>()) {
        return lookup_fact(!(ge->a < ge->b), truths, falsehoods);
    }

    if (truths.count(e)) {
        return true;
    } else if (falsehoods.count(e)) {
        return false;
    }

    // A comparison may also be settled by the other strictness of the same
    // comparison, in either direction.
    if (const LT *lt = e.as<LT>()) {
        // a < b is implied by !(b <= a), and ruled out by b <= a and by b < a.
        if (falsehoods.count(lt->b <= lt->a)) {
            return true;
        } else if (truths.count(lt->b <= lt->a) || truths.count(lt->b < lt->a)) {
            return false;
        }
    } else if (const LE *le = e.as<LE>()) {
        // a <= b is implied by a < b and by !(b < a), and ruled out by b < a.
        if (truths.count(le->a < le->b) || falsehoods.count(le->b < le->a)) {
            return true;
        } else if (truths.count(le->b < le->a)) {
            return false;
        }
    }

    return std::nullopt;
}

template<typename T>
T substitute_facts_impl(const T &t,
                        const std::set<Expr, IRDeepCompare> &truths,
                        const std::set<Expr, IRDeepCompare> &falsehoods) {
    return mutate_with(t, [&](auto *self, const Expr &e) {
        if (e.type().is_bool()) {
            if (auto known = lookup_fact(e, truths, falsehoods)) {
                return *known ? make_one(e.type()) : make_zero(e.type());
            }
        }
        return self->mutate_base(e);
    });
}
}  // namespace

Expr Simplify::ScopedFact::substitute_facts(const Expr &e) {
    return substitute_facts_impl(e, truths, falsehoods);
}

Stmt Simplify::ScopedFact::substitute_facts(const Stmt &s) {
    return substitute_facts_impl(s, truths, falsehoods);
}

namespace {

// Intersect acc with d, reporting whether the result would be empty rather than
// constructing it. make_intersection asserts on an empty result, and empty means
// the facts contradict each other, which means this code is unreachable. We
// don't try to exploit that here; we just decline to tighten any further.
bool intersect_if_nonempty(ConstantInterval &acc, const ConstantInterval &d) {
    ConstantInterval result = acc;
    if (d.min_defined && (!result.min_defined || d.min > result.min)) {
        result.min = d.min;
        result.min_defined = true;
    }
    if (d.max_defined && (!result.max_defined || d.max < result.max)) {
        result.max = d.max;
        result.max_defined = true;
    }
    if (result.min_defined && result.max_defined && result.min > result.max) {
        return false;
    }
    acc = result;
    return true;
}

// What the shape of the two sides says about (a - b) on its own, with no facts
// involved: a min is at most either of its operands, and a max is at least
// either of them. Only the immediate operands are inspected, so this stays a
// couple of pointer comparisons rather than a search.
ConstantInterval structural_difference(const BaseExprNode *a, const BaseExprNode *b) {
    ConstantInterval result;

    // Same restriction as learning a fact: a difference only means what we take
    // it to mean for integers that don't wrap. It keeps floats, where a NaN
    // makes even min(p, q) <= p false, out of it too.
    if (!(a->type.is_int() && a->type.bits() >= 32) || a->type != b->type) {
        return result;
    }

    auto is_operand_of = [](const BaseExprNode *e, const BaseExprNode *node) {
        if (node->node_type == IRNodeType::Min) {
            const Min *m = (const Min *)node;
            return equal(*m->a.get(), *e) || equal(*m->b.get(), *e);
        } else if (node->node_type == IRNodeType::Max) {
            const Max *m = (const Max *)node;
            return equal(*m->a.get(), *e) || equal(*m->b.get(), *e);
        }
        return false;
    };

    // min(p, q) - b <= 0 and max(p, q) - b >= 0, when b is one of the operands.
    if (a->node_type == IRNodeType::Min && is_operand_of(b, a)) {
        result = ConstantInterval::bounded_above(0);
    } else if (a->node_type == IRNodeType::Max && is_operand_of(b, a)) {
        result = ConstantInterval::bounded_below(0);
    } else if (b->node_type == IRNodeType::Min && is_operand_of(a, b)) {
        // a - min(p, q) >= 0
        result = ConstantInterval::bounded_below(0);
    } else if (b->node_type == IRNodeType::Max && is_operand_of(a, b)) {
        result = ConstantInterval::bounded_above(0);
    }

    return result;
}

}  // namespace

ConstantInterval Simplify::known_difference(const BaseExprNode *a, const BaseExprNode *b) {
    return known_affine_difference(a, 1, b, 1);
}

ConstantInterval Simplify::known_affine_difference(const BaseExprNode *a, int64_t ca,
                                                   const BaseExprNode *b, int64_t cb) {
    ConstantInterval result;

    // Canonicalize the query the way facts are canonicalized when learned.
    // ca/cb seed the coefficients: they already apply to the unpeeled a/b (a
    // matched WildConst, say), so peeling can't discover them itself.
    int64_t coeff_a = ca, coeff_b = cb, offset = 0, denom = 1;
    ConstantInterval err(0, 0);
    peel_affine_terms(a, b, coeff_a, coeff_b, offset, denom, err);

    if (coeff_a == coeff_b && equal(*a, *b)) {
        result = ConstantInterval::single_point(0);
    } else if (a->node_type == IRNodeType::IntImm && b->node_type == IRNodeType::IntImm) {
        // Two constants need no facts to compare.
        int64_t va = ((const IntImm *)a)->value, vb = ((const IntImm *)b)->value;
        if (!mul_would_overflow(64, coeff_a, va) && !mul_would_overflow(64, coeff_b, vb)) {
            int64_t ta = coeff_a * va, tb = coeff_b * vb;
            if (!sub_would_overflow(64, ta, tb)) {
                result = ConstantInterval::single_point(ta - tb);
            }
        }
    } else if (coeff_a == 1 && coeff_b == 1) {
        // The structural heuristic is about (a - b) alone; it doesn't
        // generalize to a scaled combination.
        intersect_if_nonempty(result, structural_difference(a, b));
    }

    if (!result.is_single_point() && !known_bounds.empty()) {
        int64_t prim_a, prim_b, scale;
        if (reduce_affine_coeffs(coeff_a, coeff_b, prim_a, prim_b, scale)) {
            // A hole only bites once the ends are known, so collect and apply
            // them below. There are hardly ever any.
            constexpr int max_holes = 4;
            int64_t holes[max_holes];
            int num_holes = 0;

            const uint32_t fa = a->hash, fb = b->hash;
            // One test against the whole table before looking at any record.
            if (difference_key_present(difference_key(fa, fb))) {
                for (const KnownBound &kb : known_bounds) {
                    // Hashes first: a record about another pair costs two
                    // integer compares, not a walk over two Exprs.
                    const uint32_t kba = kb.a.get()->hash, kbb = kb.b.get()->hash;
                    const bool same_order = (fa == kba && fb == kbb);
                    const bool swapped = (fa == kbb && fb == kba);
                    if (!same_order && !swapped) {
                        continue;
                    }

                    ConstantInterval d;
                    if (same_order && equal(*a, *kb.a.get()) && equal(*b, *kb.b.get()) &&
                        prim_a == kb.coeff_a && prim_b == kb.coeff_b) {
                        d = kb.diff * scale;
                    } else if (swapped && equal(*a, *kb.b.get()) && equal(*b, *kb.a.get())) {
                        // The fact runs the other way. Reduce (coeff_b,
                        // coeff_a) -- this query in the fact's operand order --
                        // then negate to flip back.
                        int64_t sw_prim_a, sw_prim_b, sw_scale;
                        if (reduce_affine_coeffs(coeff_b, coeff_a, sw_prim_a, sw_prim_b, sw_scale) &&
                            sw_prim_a == kb.coeff_a && sw_prim_b == kb.coeff_b) {
                            d = -(kb.diff * sw_scale);
                        } else {
                            continue;
                        }
                    } else {
                        continue;
                    }

                    if (kb.invert) {
                        if (num_holes < max_holes) {
                            holes[num_holes++] = d.min;
                        }
                    } else if (!intersect_if_nonempty(result, d)) {
                        break;
                    }
                }
            }

            for (int i = 0; i < num_holes; i++) {
                const int64_t hole = holes[i];
                // A point only narrows the bounds from an end, and only if
                // something survives: a hole swallowing the interval means the
                // facts contradict and the code is unreachable. Say nothing
                // rather than hand back a backwards interval.
                if (result.min_defined && result.max_defined &&
                    result.min == hole && result.max == hole) {
                    continue;
                }
                if (result.min_defined && result.min == hole &&
                    !add_would_overflow(64, hole, 1)) {
                    result.min = hole + 1;
                }
                if (result.max_defined && result.max == hole &&
                    !sub_would_overflow(64, hole, 1)) {
                    result.max = hole - 1;
                }
            }
        }
    }

    // Undo the canonicalization. The final divide floors where it could ceil,
    // so the low end is sound but not tightest.
    result = (result + offset + err) / denom;

    return result;
}

bool Simplify::known_min_diff(const BaseExprNode *a, const BaseExprNode *b, int64_t *result) {
    ConstantInterval bounds = known_difference(a, b);
    if (bounds.min_defined) {
        *result = bounds.min;
        return true;
    }
    return false;
}

bool Simplify::known_min_diff(const BaseExprNode *a, int64_t ca, const BaseExprNode *b, int64_t cb, int64_t *result) {
    ConstantInterval bounds = known_affine_difference(a, ca, b, cb);
    if (bounds.min_defined) {
        *result = bounds.min;
        return true;
    }
    return false;
}

bool Simplify::known_max_diff(const BaseExprNode *a, int64_t ca, const BaseExprNode *b, int64_t cb, int64_t *result) {
    ConstantInterval bounds = known_affine_difference(a, ca, b, cb);
    if (bounds.max_defined) {
        *result = bounds.max;
        return true;
    }
    return false;
}

bool Simplify::known_max_diff(const BaseExprNode *a, const BaseExprNode *b, int64_t *result) {
    ConstantInterval bounds = known_difference(a, b);
    if (bounds.max_defined) {
        *result = bounds.max;
        return true;
    }
    return false;
}

bool Simplify::is_known_true(const Expr &e) {
    if (truths.empty() && falsehoods.empty()) {
        return false;
    }
    auto known = lookup_fact(e, truths, falsehoods);
    return known && *known;
}

Expr Simplify::simplify_can_prove_condition(const Expr &e) {
    if (can_prove_depth >= max_can_prove_depth) {
        // Too deep to safely recurse into the full simplifier. The only thing
        // the caller does with the result is check whether it is the literal
        // constant true, and nothing here can fold a compound expression (an
        // And of two known-true operands stays an unfolded And, not true) --
        // that folding is exactly the recursive work we're declining to do.
        // So a substitute_facts tree walk can't prove anything a direct
        // lookup of the condition itself couldn't already: skip the walk.
        if (is_known_true(e)) {
            return const_true(e.type().lanes(), nullptr);
        }
        return e;
    }
    ScopedValue<int> guard(can_prove_depth, can_prove_depth + 1);
    return mutate(substitute_facts(e), nullptr);
}

Expr Simplify::substitute_facts(const Expr &e) {
    if (truths.empty() && falsehoods.empty()) {
        return e;
    }
    return substitute_facts_impl(e, truths, falsehoods);
}

Simplify::ScopedFact::~ScopedFact() {
    if (!simplify) {
        // Moved from; the object that took over owns the cleanup.
        return;
    }
    for (const auto *v : pop_list) {
        simplify->var_info.pop(v->name);
    }
    for (const auto *v : bounds_pop_list) {
        simplify->bounds_and_alignment_info.pop(v->name);
    }
    internal_assert(simplify->known_bounds.size() >= known_bounds_size);
    simplify->known_bounds.resize(known_bounds_size);
    for (int i = 0; i < Simplify::difference_key_words; i++) {
        simplify->difference_keys[i] = saved_difference_keys[i];
    }
    for (const auto &e : truths) {
        simplify->truths.erase(e);
    }
    for (const auto &e : falsehoods) {
        simplify->falsehoods.erase(e);
    }
}

Expr simplify(const Expr &e,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment,
              const std::vector<Expr> &assumptions) {
    Simplify m(&bounds, &alignment);
    std::vector<Simplify::ScopedFact> facts;
    facts.reserve(assumptions.size());
    for (const Expr &a : assumptions) {
        facts.push_back(m.scoped_truth(a));
    }
    Expr result = m.mutate(e, nullptr);
    if (m.in_unreachable) {
        return unreachable(e.type());
    }
    return result;
}

Stmt simplify(const Stmt &s,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment,
              const std::vector<Expr> &assumptions) {
    Simplify m(&bounds, &alignment);
    std::vector<Simplify::ScopedFact> facts;
    facts.reserve(assumptions.size());
    for (const Expr &a : assumptions) {
        facts.push_back(m.scoped_truth(a));
    }
    Stmt result = m.mutate(s);
    if (m.in_unreachable) {
        return Evaluate::make(unreachable());
    }
    return result;
}

class SimplifyExprs : public IRMutator {
public:
    using IRMutator::mutate;
    Expr mutate(const Expr &e) override {
        return simplify(e);
    }
};

Stmt simplify_exprs(const Stmt &s) {
    return SimplifyExprs().mutate(s);
}

bool can_prove(Expr e, const Scope<Interval> &bounds) {
    internal_assert(e.type().is_bool())
        << "Argument to can_prove is not a boolean Expr: " << e << "\n";

    e = remove_likelies(e);
    e = common_subexpression_elimination(e);

    Expr orig = e;

    e = simplify(e, bounds);

    // Take a closer look at all failed proof attempts to hunt for
    // simplifier weaknesses
    if (!is_const(e)) {
        debug(1, "counterexample") << [&]() -> std::string {
            struct RenameVariables : public IRMutator {
                using IRMutator::visit;

                Expr visit(const Variable *op) override {
                    auto it = vars.find(op->name);
                    if (const std::string *n = lets.find(op->name)) {
                        return Variable::make(op->type, *n);
                    } else if (it == vars.end()) {
                        std::string name = "v" + std::to_string(count++);
                        vars[op->name] = name;
                        out_vars.emplace_back(op->type, name);
                        return Variable::make(op->type, name);
                    } else {
                        return Variable::make(op->type, it->second);
                    }
                }

                Expr visit(const Let *op) override {
                    std::string name = "v" + std::to_string(count++);
                    ScopedBinding<string> bind(lets, op->name, name);
                    return Let::make(name, mutate(op->value), mutate(op->body));
                }

                int count = 0;
                map<string, string> vars;
                Scope<string> lets;
                std::vector<pair<Type, string>> out_vars;
            } renamer;

            Expr renamed = renamer(e);

            // Look for a concrete counter-example with random probing
            static std::mt19937 rng(0);
            for (int i = 0; i < 100; i++) {
                map<string, Expr> s;
                for (const auto &p : renamer.out_vars) {
                    if (p.first.is_handle()) {
                        // This aint gonna work
                        return "";
                    }
                    s[p.second] = make_const(p.first, (int)(rng() & 0xffff) - 0x7fff);
                }
                Expr probe = unwrap_tags(simplify(substitute(s, renamed)));
                if (!is_const_one(probe)) {
                    // Found a counter-example, or something that fails to fold
                    return "";
                }
            }

            ostringstream ss;
            ss << "Failed to prove, but could not find a counter-example:\n " << renamed << "\n"
               << "Original expression:\n"
               << orig << "\n";
            return ss.str();
        }();
    }

    return is_const_one(e);
}

Simplify::ExprInfo::BitsKnown Simplify::ExprInfo::to_bits_known(const Type &type) const {
    BitsKnown result = {0, 0};

    if (!(type.is_int() || type.is_uint())) {
        // Let's not claim we know anything about the bit patterns of
        // non-integer types for now.
        return result;
    }

    // Identify the largest power of two in the modulus to get some low bits
    if (alignment.modulus) {
        result.mask = largest_power_of_two_factor(alignment.modulus) - 1;
        result.value = result.mask & alignment.remainder;
    } else {
        // This value is just a constant
        result.mask = (uint64_t)(-1);
        result.value = alignment.remainder;
        return result;
    }

    // Compute a mask which is 1 for all the leading zeros of a uint64
    auto leading_zeros_mask = [](uint64_t x) {
        if (x == 0) {
            // They're all leading zeros, but clz64 is UB on zero. Really we
            // should have returned early above, but it's hard to guarantee that
            // the alignment analysis catches constants at the same time as
            // bounds analysis does.
            return (uint64_t)-1;
        } else if ((int64_t)x < 0) {
            // There are no leading zeros, but we can't shift left by 64
            return (uint64_t)0;
        }
        return (uint64_t)(-1) << (64 - clz64(x));
    };

    if (bounds.min_defined && bounds.max_defined) {
        // Any leading bits in common between the min and the max are known.
        result.mask |= leading_zeros_mask(bounds.min ^ bounds.max);
        result.value |= bounds.min & result.mask;
    } else {
        // If we only have a bound on one side, we may still be able to infer
        // something about high bits.

        // The bounds and the type tell us a bunch of high bits are zero or one
        if (type.is_uint()) {
            // Narrow uints are always zero-extended.
            if (type.bits() < 64) {
                result.mask |= (uint64_t)(-1) << type.bits();
            }

            // A lower bound might tell us that there are some leading ones, and an
            // upper bound might tell us that there are some leading
            // zeros. Unfortunately we'll never learn about leading ones, because to
            // know that there's a leading one from the bounds would require knowing
            // that the min is at least 2^63, and ConstantInterval can't represent
            // mins that large.
            if (bounds.max_defined) {
                result.mask |= leading_zeros_mask(bounds.max);
            }

        } else {
            internal_assert(type.is_int());
            // A mask which is 1 for the sign bit and above.
            uint64_t sign_bit_and_above = (uint64_t)(-1) << (type.bits() - 1);
            if (bounds >= 0) {
                // We know this int is positive, so the sign bit and above are zero.
                result.mask |= sign_bit_and_above;
            } else if (bounds < 0) {
                // This int is negative, so the sign bit and above are one.
                result.mask |= sign_bit_and_above;
                result.value |= sign_bit_and_above;
            }
        }
    }

    return result;
}

void Simplify::ExprInfo::from_bits_known(Simplify::ExprInfo::BitsKnown known, const Type &type) {
    // Normalize everything to 64-bits by sign- or zero-extending known bits for
    // the type.

    // A mask which is one for all the new bits resulting from sign or zero
    // extension.
    uint64_t missing_bits = 0;
    if (type.bits() < 64) {
        missing_bits = (uint64_t)(-1) << type.bits();
    }

    if (missing_bits) {
        if (type.is_uint()) {
            // For a uint the high bits are known to be zero
            known.mask |= missing_bits;
            known.value &= ~missing_bits;
        } else if (type.is_int()) {
            // For an int we need to know the sign to know the high bits
            bool sign_bit_known = (known.mask >> (type.bits() - 1)) & 1;
            bool negative = (known.value >> (type.bits() - 1)) & 1;
            if (!sign_bit_known) {
                // We don't know the sign bit, so we don't know any of the
                // extended bits. Mark them as unknown in the mask and zero them
                // out in the value too just for ease of debugging.
                known.mask &= ~missing_bits;
                known.value &= ~missing_bits;
            } else if (negative) {
                // We know the sign bit is 1, so all of the extended bits are 1
                // too.
                known.mask |= missing_bits;
                known.value |= missing_bits;
            } else if (!negative) {
                // We know the sign bit is zero, so all of the extended bits are
                // zero too.
                known.mask |= missing_bits;
                known.value &= ~missing_bits;
            }
        }
    }

    // We can get the trailing one bits by adding one and taking the largest
    // power of two factor. Note that this works out correctly when we know all
    // the bits - the modulus comes out as zero, and the remainder is the entire
    // number, which is how we represent constants in ModulusRemainder.
    alignment.modulus = largest_power_of_two_factor(known.mask + 1);
    alignment.remainder = known.value & (alignment.modulus - 1);

    if ((int64_t)known.mask < 0) {
        // We know some leading bits

        // Set all unknown bits to zero
        uint64_t min_val = known.value & known.mask;
        // Set all unknown bits to one
        uint64_t max_val = known.value | ~known.mask;

        if (type.is_uint() && (int64_t)known.value < 0) {
            // We know it's out of range at the top end for our ConstantInterval
            // class. At the time of writing, to_bits_known can't produce this
            // directly, and bits_known is never propagated through other
            // operations, so this code is unreachable. Nonetheless we'll do the
            // best job we can at representing this case in case this code
            // becomes reachable in future.
            bounds = ConstantInterval::bounded_below((1ULL << 63) - 1);
        } else {
            // In all other cases, the bounds are representable as an int64
            // and don't span zero (because we know the high bit).
            bounds = ConstantInterval{(int64_t)min_val, (int64_t)max_val};
        }
    }
}

}  // namespace Internal
}  // namespace Halide
