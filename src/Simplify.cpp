#include <iostream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdio.h>

#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRPrinter.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Var.h"
#include "Debug.h"
#include "ModulusRemainder.h"
#include "Substitute.h"
#include "Bounds.h"
#include "Deinterleave.h"
#include "ExprUsesVar.h"

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::pair;
using std::ostringstream;
using std::vector;

#define LOG_EXPR_MUTATIONS 0
#define LOG_STMT_MUTATIONS 0

namespace {

// Things that we can constant fold: Immediates and broadcasts of immediates.
bool is_simple_const(const Expr &e) {
    if (e.as<IntImm>()) return true;
    if (e.as<UIntImm>()) return true;
    // Don't consider NaN to be a "simple const", since it doesn't obey equality rules assumed elsewere
    const FloatImm *f = e.as<FloatImm>();
    if (f && !std::isnan(f->value)) return true;
    if (const Broadcast *b = e.as<Broadcast>()) {
        return is_simple_const(b->value);
    }
    return false;
}

// If the Expr is (var relop const) or (const relop var),
// fill in the var name and return true.
template<typename RelOp>
bool is_var_relop_simple_const(const Expr &e, string* name) {
    if (const RelOp *r = e.as<RelOp>()) {
        if (is_simple_const(r->b)) {
            const Variable *v = r->a.template as<Variable>();
            if (v) {
                *name = v->name;
                return true;
            }
        } else if (is_simple_const(r->a)) {
            const Variable *v = r->b.template as<Variable>();
            if (v) {
                *name = v->name;
                return true;
            }
        }
    }
    return false;
}

bool is_var_simple_const_comparison(const Expr &e, string* name) {
    // It's not clear if GT, LT, etc would be useful
    // here; leaving them out until proven otherwise.
    return is_var_relop_simple_const<EQ>(e, name) ||
           is_var_relop_simple_const<NE>(e, name);
}

// Returns true iff t is a scalar integral type where overflow is undefined
bool no_overflow_scalar_int(Type t) {
    return (t.is_scalar() && t.is_int() && t.bits() >= 32);
}

// Returns true iff t does not have a well defined overflow behavior.
bool no_overflow(Type t) {
    return t.is_float() || no_overflow_scalar_int(t.element_of());
}

// Make a poison value used when overflow is detected during constant
// folding.
Expr signed_integer_overflow_error(Type t) {
    // Mark each call with an atomic counter, so that the errors can't
    // cancel against each other.
    static std::atomic<int> counter;
    return Call::make(t, Call::signed_integer_overflow, {counter++}, Call::Intrinsic);
}

// Make a poison value used when integer div/mod-by-zero is detected during constant folding.
Expr indeterminate_expression_error(Type t) {
    // Mark each call with an atomic counter, so that the errors can't
    // cancel against each other.
    static std::atomic<int> counter;
    return Call::make(t, Call::indeterminate_expression, {counter++}, Call::Intrinsic);
}

// If 'e' is indeterminate_expression of type t,
//      set *expr to it and return true.
// If 'e' is indeterminate_expression of other type,
//      make a new indeterminate_expression of the proper type, set *expr to it and return true.
// Otherwise, leave *expr untouched and return false.
bool propagate_indeterminate_expression(const Expr &e, Type t, Expr *expr) {
    const Call *call = e.as<Call>();
    if (call && call->is_intrinsic(Call::indeterminate_expression)) {
        if (call->type != t) {
            *expr = indeterminate_expression_error(t);
        } else {
            *expr = e;
        }
        return true;
    }
    return false;
}

bool propagate_indeterminate_expression(const Expr &e0, const Expr &e1, Type t, Expr *expr) {
    return propagate_indeterminate_expression(e0, t, expr) ||
           propagate_indeterminate_expression(e1, t, expr);
}

bool propagate_indeterminate_expression(const Expr &e0, const Expr &e1, const Expr &e2, Type t, Expr *expr) {
    return propagate_indeterminate_expression(e0, t, expr) ||
           propagate_indeterminate_expression(e1, t, expr) ||
           propagate_indeterminate_expression(e2, t, expr);
}

#if LOG_EXPR_MUTATIONS || LOG_STMT_MUTATIONS
static int debug_indent = 0;
#endif

}

class Simplify : public IRMutator2 {
public:
    Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai) :
        simplify_lets(r) {
        alignment_info.set_containing_scope(ai);

        // Only respect the constant bounds from the containing scope.
        for (Scope<Interval>::const_iterator iter = bi->cbegin(); iter != bi->cend(); ++iter) {
            int64_t i_min, i_max;
            if (const_int(iter.value().min, &i_min) &&
                const_int(iter.value().max, &i_max)) {
                bounds_info.push(iter.name(), { i_min, i_max });
            }
        }

    }

#if LOG_EXPR_MUTATIONS
    Expr mutate(const Expr &e) override {
        const std::string spaces(debug_indent, ' ');
        debug(1) << spaces << "Simplifying Expr: " << e << "\n";
        debug_indent++;
        Expr new_e = IRMutator2::mutate(e);
        debug_indent--;
        if (!new_e.same_as(e)) {
            debug(1)
                << spaces << "Before: " << e << "\n"
                << spaces << "After:  " << new_e << "\n";
        }
        return new_e;
    }
#endif

#if LOG_STMT_MUTATIONS
    Stmt mutate(const Stmt &s) override {
        const std::string spaces(debug_indent, ' ');
        debug(1) << spaces << "Simplifying Stmt: " << s << "\n";
        debug_indent++;
        Stmt new_s = IRMutator2::mutate(s);
        debug_indent--;
        if (!new_s.same_as(s)) {
            debug(1)
                << spaces << "Before: " << s << "\n"
                << spaces << "After:  " << new_s << "\n";
        }
        return new_s;
    }
#endif
    using IRMutator2::mutate;


private:
    bool simplify_lets;

    struct VarInfo {
        Expr replacement;
        int old_uses, new_uses;
    };

    Scope<VarInfo> var_info;
    Scope<pair<int64_t, int64_t>> bounds_info;
    Scope<ModulusRemainder> alignment_info;

    // If we encounter a reference to a buffer (a Load, Store, Call,
    // or Provide), there's an implicit dependence on some associated
    // symbols.
    void found_buffer_reference(const string &name, size_t dimensions = 0) {
        for (size_t i = 0; i < dimensions; i++) {
            string stride = name + ".stride." + std::to_string(i);
            if (var_info.contains(stride)) {
                var_info.ref(stride).old_uses++;
            }

            string min = name + ".min." + std::to_string(i);
            if (var_info.contains(min)) {
                var_info.ref(min).old_uses++;
            }
        }

        if (var_info.contains(name)) {
            var_info.ref(name).old_uses++;
        }
    }

    using IRMutator2::visit;

    // Wrappers for as_const_foo that are more convenient to use in
    // the large chains of conditions in the visit methods
    // below. Unlike the versions in IROperator, these only match
    // scalars.
    bool const_float(const Expr &e, double *f) {
        if (e.type().is_vector()) {
            return false;
        } else if (const double *p = as_const_float(e)) {
            *f = *p;
            return true;
        } else {
            return false;
        }
    }

    bool const_int(const Expr &e, int64_t *i) {
        if (e.type().is_vector()) {
            return false;
        } else if (const int64_t *p = as_const_int(e)) {
            *i = *p;
            return true;
        } else {
            return false;
        }
    }

    bool const_uint(const Expr &e, uint64_t *u) {
        if (e.type().is_vector()) {
            return false;
        } else if (const uint64_t *p = as_const_uint(e)) {
            *u = *p;
            return true;
        } else {
            return false;
        }
    }

    // Similar to bounds_of_expr_in_scope, but gives up immediately if
    // anything isn't a constant. This stops rules from taking the
    // bounds of something then having to simplify it to see whether
    // it constant-folds. For some expressions the bounds of the
    // expression is at least as complex as the expression, so
    // recursively mutating the bounds causes havoc.
    bool const_int_bounds(const Expr &e, int64_t *min_val, int64_t *max_val) {
        Type t = e.type();

        if (const int64_t *i = as_const_int(e)) {
            *min_val = *max_val = *i;
            return true;
        } else if (const Variable *v = e.as<Variable>()) {
            if (bounds_info.contains(v->name)) {
                pair<int64_t, int64_t> b = bounds_info.get(v->name);
                *min_val = b.first;
                *max_val = b.second;
                return true;
            }
        } else if (const Broadcast *b = e.as<Broadcast>()) {
            return const_int_bounds(b->value, min_val, max_val);
        } else if (const Max *max = e.as<Max>()) {
            int64_t min_a, min_b, max_a, max_b;
            // We only need to check the LHS for Min expr since simplify would
            // canonicalize min/max to always be in the LHS.
            if (const Min *min = max->a.as<Min>()) {
                // Bound of max(min(x, a), b) : [min_b, max(max_a, max_b)].
                // We need to check both LHS and RHS of the min, since if a is
                // a min/max clamp instead of a constant, simplify would have
                // reordered x and a.
                if (const_int_bounds(max->b, &min_b, &max_b) &&
                    (const_int_bounds(min->b, &min_a, &max_a) ||
                     const_int_bounds(min->a, &min_a, &max_a))) {
                    *min_val = min_b;
                    *max_val = std::max(max_a, max_b);
                    return true;
                }
            } else if (const_int_bounds(max->a, &min_a, &max_a) &&
                       const_int_bounds(max->b, &min_b, &max_b)) {
                *min_val = std::max(min_a, min_b);
                *max_val = std::max(max_a, max_b);
                return true;
            }
        } else if (const Min *min = e.as<Min>()) {
            int64_t min_a, min_b, max_a, max_b;
            // We only need to check the LHS for Max expr since simplify would
            // canonicalize min/max to always be in the LHS.
            if (const Max *max = min->a.as<Max>()) {
                // Bound of min(max(x, a), b) : [min(min_a, min_b), max_b].
                // We need to check both LHS and RHS of the max, since if a is
                // a min/max clamp instead of a constant, simplify would have
                // reordered x and a.
                if (const_int_bounds(min->b, &min_b, &max_b) &&
                    (const_int_bounds(max->b, &min_a, &max_a) ||
                     const_int_bounds(max->a, &min_a, &max_a))) {
                    *min_val = std::min(min_a, min_b);
                    *max_val = max_b;
                    return true;
                }
            } else if (const_int_bounds(min->a, &min_a, &max_a) &&
                       const_int_bounds(min->b, &min_b, &max_b)) {
                *min_val = std::min(min_a, min_b);
                *max_val = std::min(max_a, max_b);
                return true;
            }
        } else if (const Select *sel = e.as<Select>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(sel->true_value, &min_a, &max_a) &&
                const_int_bounds(sel->false_value, &min_b, &max_b)) {
                *min_val = std::min(min_a, min_b);
                *max_val = std::max(max_a, max_b);
                return true;
            }
        } else if (const Add *add = e.as<Add>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(add->a, &min_a, &max_a) &&
                const_int_bounds(add->b, &min_b, &max_b)) {
                *min_val = min_a + min_b;
                *max_val = max_a + max_b;
                return no_overflow_scalar_int(t.element_of()) ||
                       (t.can_represent(*min_val) && t.can_represent(*max_val));
            }
        } else if (const Sub *sub = e.as<Sub>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(sub->a, &min_a, &max_a) &&
                const_int_bounds(sub->b, &min_b, &max_b)) {
                *min_val = min_a - max_b;
                *max_val = max_a - min_b;
                return no_overflow_scalar_int(t.element_of()) ||
                       (t.can_represent(*min_val) && t.can_represent(*max_val));
            }
        } else if (const Mul *mul = e.as<Mul>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(mul->a, &min_a, &max_a) &&
                const_int_bounds(mul->b, &min_b, &max_b)) {
                int64_t
                    t0 = min_a*min_b,
                    t1 = min_a*max_b,
                    t2 = max_a*min_b,
                    t3 = max_a*max_b;
                *min_val = std::min(std::min(t0, t1), std::min(t2, t3));
                *max_val = std::max(std::max(t0, t1), std::max(t2, t3));
                return no_overflow_scalar_int(t.element_of()) ||
                       (t.can_represent(*min_val) && t.can_represent(*max_val));
            }
        } else if (const Mod *mod = e.as<Mod>()) {
            int64_t min_b, max_b;
            if (const_int_bounds(mod->b, &min_b, &max_b) &&
                (min_b > 0 || max_b < 0)) {
                *min_val = 0;
                *max_val = std::max(std::abs(min_b), std::abs(max_b)) - 1;
                return no_overflow_scalar_int(t.element_of()) ||
                       (t.can_represent(*min_val) && t.can_represent(*max_val));
            }
        } else if (const Div *div = e.as<Div>()) {
            int64_t min_a, min_b, max_a, max_b;
            if (const_int_bounds(div->a, &min_a, &max_a) &&
                const_int_bounds(div->b, &min_b, &max_b) &&
                (min_b > 0 || max_b < 0)) {
                int64_t
                    t0 = div_imp(min_a, min_b),
                    t1 = div_imp(min_a, max_b),
                    t2 = div_imp(max_a, min_b),
                    t3 = div_imp(max_a, max_b);
                *min_val = std::min(std::min(t0, t1), std::min(t2, t3));
                *max_val = std::max(std::max(t0, t1), std::max(t2, t3));
                return no_overflow_scalar_int(t.element_of()) ||
                       (t.can_represent(*min_val) && t.can_represent(*max_val));
            }
        } else if (const Ramp *r = e.as<Ramp>()) {
            int64_t min_base, max_base, min_stride, max_stride;
            if (const_int_bounds(r->base, &min_base, &max_base) &&
                const_int_bounds(r->stride, &min_stride, &max_stride)) {
                int64_t min_last_lane = min_base + min_stride * (r->lanes - 1);
                int64_t max_last_lane = max_base + max_stride * (r->lanes - 1);
                *min_val = std::min(min_base, min_last_lane);
                *max_val = std::max(max_base, max_last_lane);
                return no_overflow_scalar_int(t.element_of()) ||
                       (t.can_represent(*min_val) && t.can_represent(*max_val));
            }
        }
        return false;
    }


    // Check if an Expr is integer-division-rounding-up by the given
    // factor. If so, return the core expression.
    Expr is_round_up_div(const Expr &e, int64_t factor) {
        if (!no_overflow(e.type())) return Expr();
        const Div *div = e.as<Div>();
        if (!div) return Expr();
        if (!is_const(div->b, factor)) return Expr();
        const Add *add = div->a.as<Add>();
        if (!add) return Expr();
        if (!is_const(add->b, factor-1)) return Expr();
        return add->a;
    }

    // Check if an Expr is a rounding-up operation, and if so, return
    // the factor.
    Expr is_round_up(const Expr &e, int64_t *factor) {
        if (!no_overflow(e.type())) return Expr();
        const Mul *mul = e.as<Mul>();
        if (!mul) return Expr();
        if (!const_int(mul->b, factor)) return Expr();
        return is_round_up_div(mul->a, *factor);
    }

    Expr visit(const Cast *op) override {
        Expr value = mutate(op->value);
        Expr expr;
        if (propagate_indeterminate_expression(value, op->type, &expr)) {
            return expr;
        }
        const Cast *cast = value.as<Cast>();
        const Broadcast *broadcast_value = value.as<Broadcast>();
        const Ramp *ramp_value = value.as<Ramp>();
        const Add *add = value.as<Add>();
        double f = 0.0;
        int64_t i = 0;
        uint64_t u = 0;
        if (value.type() == op->type) {
            return value;
        } else if (op->type.is_int() &&
                   const_float(value, &f)) {
            // float -> int
            return IntImm::make(op->type, (int64_t)f);
        } else if (op->type.is_uint() &&
                   const_float(value, &f)) {
            // float -> uint
            return UIntImm::make(op->type, (uint64_t)f);
        } else if (op->type.is_float() &&
                   const_float(value, &f)) {
            // float -> float
            return FloatImm::make(op->type, f);
        } else if (op->type.is_int() &&
                   const_int(value, &i)) {
            // int -> int
            return IntImm::make(op->type, i);
        } else if (op->type.is_uint() &&
                   const_int(value, &i)) {
            // int -> uint
            return UIntImm::make(op->type, (uint64_t)i);
        } else if (op->type.is_float() &&
                   const_int(value, &i)) {
            // int -> float
            return FloatImm::make(op->type, (double)i);
        } else if (op->type.is_int() &&
                   const_uint(value, &u)) {
            // uint -> int
            return IntImm::make(op->type, (int64_t)u);
        } else if (op->type.is_uint() &&
                   const_uint(value, &u)) {
            // uint -> uint
            return UIntImm::make(op->type, u);
        } else if (op->type.is_float() &&
                   const_uint(value, &u)) {
            // uint -> float
            return FloatImm::make(op->type, (double)u);
        } else if (cast &&
                   op->type.code() == cast->type.code() &&
                   op->type.bits() < cast->type.bits()) {
            // If this is a cast of a cast of the same type, where the
            // outer cast is narrower, the inner cast can be
            // eliminated.
            return mutate(Cast::make(op->type, cast->value));
        } else if (cast &&
                   (op->type.is_int() || op->type.is_uint()) &&
                   (cast->type.is_int() || cast->type.is_uint()) &&
                   op->type.bits() <= cast->type.bits() &&
                   op->type.bits() <= op->value.type().bits()) {
            // If this is a cast between integer types, where the
            // outer cast is narrower than the inner cast and the
            // inner cast's argument, the inner cast can be
            // eliminated. The inner cast is either a sign extend
            // or a zero extend, and the outer cast truncates the extended bits
            return mutate(Cast::make(op->type, cast->value));
        } else if (broadcast_value) {
            // cast(broadcast(x)) -> broadcast(cast(x))
            return mutate(Broadcast::make(Cast::make(op->type.element_of(), broadcast_value->value), broadcast_value->lanes));
        } else if (ramp_value &&
                   op->type.element_of() == Int(64) &&
                   op->value.type().element_of() == Int(32)) {
            // cast(ramp(a, b, w)) -> ramp(cast(a), cast(b), w)
            return mutate(Ramp::make(Cast::make(op->type.element_of(), ramp_value->base),
                                     Cast::make(op->type.element_of(), ramp_value->stride),
                                     ramp_value->lanes));
        } else if (add &&
                   op->type == Int(64) &&
                   op->value.type() == Int(32) &&
                   is_const(add->b)) {
            // In the interest of moving constants outwards so they
            // can cancel, pull the addition outside of the cast.
            return mutate(Cast::make(op->type, add->a) + add->b);
        } else if (value.same_as(op->value)) {
            return op;
        } else {
            return Cast::make(op->type, value);
        }
    }

    Expr visit(const Variable *op) override {
        if (bounds_info.contains(op->name)) {
            std::pair<int64_t, int64_t> bounds = bounds_info.get(op->name);
            if (bounds.first == bounds.second) {
                return make_const(op->type, bounds.first);
            }
        }

        if (var_info.contains(op->name)) {
            VarInfo &info = var_info.ref(op->name);

            // if replacement is defined, we should substitute it in (unless
            // it's a var that has been hidden by a nested scope).
            if (info.replacement.defined()) {
                internal_assert(info.replacement.type() == op->type) << "Cannot replace variable " << op->name
                    << " of type " << op->type << " with expression of type " << info.replacement.type() << "\n";
                info.new_uses++;
                return info.replacement;
            } else {
                // This expression was not something deemed
                // substitutable - no replacement is defined.
                info.old_uses++;
                return op;
            }
        } else {
            // We never encountered a let that defines this var. Must
            // be a uniform. Don't touch it.
            return op;
        }
    }

    Expr visit(const Add *op) override {
        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;

        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        // Rearrange a few patterns to cut down on the number of cases
        // to check later.
        if ((is_simple_const(a) && !is_simple_const(b)) ||
            (b.as<Min>() && !a.as<Min>()) ||
            (b.as<Max>() && !a.as<Max>())) {
            std::swap(a, b);
        }
        if ((b.as<Min>() && a.as<Max>())) {
            std::swap(a, b);
        }

        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();

        const Shuffle *shuffle_a = a.as<Shuffle>();
        const Shuffle *shuffle_b = b.as<Shuffle>();

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();

        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();

        const Add *add_div_a_a = div_a ? div_a->a.as<Add>(): nullptr;
        const Sub *sub_div_a_a = div_a ? div_a->a.as<Sub>(): nullptr;
        const Add *add_div_b_a = div_b ? div_b->a.as<Add>(): nullptr;
        const Sub *sub_div_b_a = div_b ? div_b->a.as<Sub>(): nullptr;

        const Div *div_a_a = mul_a ? mul_a->a.as<Div>() : nullptr;
        const Mod *mod_a = a.as<Mod>();
        const Mod *mod_b = b.as<Mod>();

        const Mul *mul_a_a = add_a ? add_a->a.as<Mul>(): nullptr;
        const Mod *mod_a_a = add_a ? add_a->a.as<Mod>(): nullptr;
        const Mul *mul_a_b = add_a ? add_a->b.as<Mul>(): nullptr;
        const Mod *mod_a_b = add_a ? add_a->b.as<Mod>(): nullptr;

        const Max *max_b = b.as<Max>();

        const Min *min_a = a.as<Min>();
        const Max *max_a = a.as<Max>();
        const Sub *sub_a_a = min_a ? min_a->a.as<Sub>() : nullptr;
        const Sub *sub_a_b = min_a ? min_a->b.as<Sub>() : nullptr;
        const Add *add_a_a = min_a ? min_a->a.as<Add>() : nullptr;
        const Add *add_a_b = min_a ? min_a->b.as<Add>() : nullptr;
        sub_a_a = max_a ? max_a->a.as<Sub>() : sub_a_a;
        sub_a_b = max_a ? max_a->b.as<Sub>() : sub_a_b;
        add_a_a = max_a ? max_a->a.as<Add>() : add_a_a;
        add_a_b = max_a ? max_a->b.as<Add>() : add_a_b;

        add_a_a = div_a ? div_a->a.as<Add>() : add_a_a;

        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();

        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            if (no_overflow(a.type()) &&
                add_would_overflow(a.type().bits(), ia, ib)) {
                return signed_integer_overflow_error(a.type());
            } else {
                return IntImm::make(a.type(), ia + ib);
            }
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            // const uint + const uint
            return UIntImm::make(a.type(), ua + ub);
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            // const float + const float
            return FloatImm::make(a.type(), fa + fb);
        } else if (is_zero(b)) {
            return a;
        } else if (is_zero(a)) {
            return b;
        } else if (equal(a, b)) {
            // x + x = x*2
            return mutate(a * make_const(op->type, 2));
        } else if (call_a &&
                   call_a->is_intrinsic(Call::signed_integer_overflow)) {
            return a;
        } else if (call_b &&
                   call_b->is_intrinsic(Call::signed_integer_overflow)) {
            return b;
        } else if (shuffle_a && shuffle_b &&
                   shuffle_a->is_slice() &&
                   shuffle_b->is_slice()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return hoist_slice_vector<Add>(op);
            } else {
                return hoist_slice_vector<Add>(Add::make(a, b));
            }
        } else if (ramp_a &&
                   ramp_b) {
            // Ramp + Ramp
            return mutate(Ramp::make(ramp_a->base + ramp_b->base,
                                     ramp_a->stride + ramp_b->stride, ramp_a->lanes));
        } else if (ramp_a &&
                   broadcast_b) {
            // Ramp + Broadcast
            return mutate(Ramp::make(ramp_a->base + broadcast_b->value,
                                     ramp_a->stride, ramp_a->lanes));
        } else if (broadcast_a &&
                   ramp_b) {
            // Broadcast + Ramp
            return mutate(Ramp::make(broadcast_a->value + ramp_b->base,
                                     ramp_b->stride, ramp_b->lanes));
        } else if (broadcast_a &&
                   broadcast_b) {
            // Broadcast + Broadcast
            return Broadcast::make(mutate(broadcast_a->value + broadcast_b->value),
                                   broadcast_a->lanes);

        } else if (select_a &&
                   select_b &&
                   equal(select_a->condition, select_b->condition)) {
            // select(c, a, b) + select(c, d, e) -> select(c, a+d, b+e)
            return mutate(Select::make(select_a->condition,
                                       select_a->true_value + select_b->true_value,
                                       select_a->false_value + select_b->false_value));
        } else if (select_a &&
                   is_simple_const(b) &&
                   (is_simple_const(select_a->true_value) ||
                    is_simple_const(select_a->false_value))) {
            // select(c, c1, c2) + c3 -> select(c, c1+c3, c2+c3)
            return mutate(Select::make(select_a->condition,
                                       select_a->true_value + b,
                                       select_a->false_value + b));
        } else if (add_a &&
                   is_simple_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_simple_const(b)) {
                return mutate(add_a->a + (add_a->b + b));
            } else {
                return mutate((add_a->a + b) + add_a->b);
            }
        } else if (add_b &&
                   is_simple_const(add_b->b)) {
            return mutate((a + add_b->a) + add_b->b);
        } else if (sub_a &&
                   is_simple_const(sub_a->a)) {
            if (is_simple_const(b)) {
                return mutate((sub_a->a + b) - sub_a->b);
            } else {
                return mutate((b - sub_a->b) + sub_a->a);
            }

        } else if (sub_a &&
                   equal(b, sub_a->b)) {
            // Additions that cancel an inner term
            // (a - b) + b
            return sub_a->a;
        } else if (sub_a &&
                   is_zero(sub_a->a)) {
            return mutate(b - sub_a->b);
        } else if (sub_b && equal(a, sub_b->b)) {
            // a + (b - a)
            return sub_b->a;
        } else if (sub_b &&
                   is_simple_const(sub_b->a)) {
            // a + (7 - b) -> (a - b) + 7
            return mutate((a - sub_b->b) + sub_b->a);
        } else if (sub_a &&
                   sub_b &&
                   equal(sub_a->b, sub_b->a)) {
            // (a - b) + (b - c) -> a - c
            return mutate(sub_a->a - sub_b->b);
        } else if (sub_a &&
                   sub_b &&
                   equal(sub_a->a, sub_b->b)) {
            // (a - b) + (c - a) -> c - b
            return mutate(sub_b->a - sub_a->b);
        } else if (mul_b &&
                   is_negative_negatable_const(mul_b->b)) {
            // a + b*-x -> a - b*x
            return mutate(a - mul_b->a * (-mul_b->b));
        } else if (mul_a &&
                   is_negative_negatable_const(mul_a->b)) {
            // a*-x + b -> b - a*x
            return mutate(b - mul_a->a * (-mul_a->b));
        } else if (mul_b &&
                   !is_const(a) &&
                   equal(a, mul_b->a) &&
                   no_overflow(op->type)) {
            // a + a*b -> a*(1 + b)
            return mutate(a * (make_one(op->type) + mul_b->b));
        } else if (mul_b &&
                   !is_const(a) &&
                   equal(a, mul_b->b) &&
                   no_overflow(op->type)) {
            // a + b*a -> (1 + b)*a
            return mutate((make_one(op->type) + mul_b->a) * a);
        } else if (mul_a &&
                   !is_const(b) &&
                   equal(mul_a->a, b) &&
                   no_overflow(op->type)) {
            // a*b + a -> a*(b + 1)
            return mutate(mul_a->a * (mul_a->b + make_one(op->type)));
        } else if (mul_a &&
                   !is_const(b) &&
                   equal(mul_a->b, b) &&
                   no_overflow(op->type)) {
            // a*b + b -> (a + 1)*b
            return mutate((mul_a->a + make_one(op->type)) * b);
        } else if (no_overflow(op->type) &&
                   div_a && add_div_a_a &&
                   is_simple_const(add_div_a_a->b) &&
                   is_simple_const(div_a->b) &&
                   is_simple_const(b)) {
            // (y + c1)/c2 + c3 -> (y + (c1 + c2*c3))/c2
            return mutate((add_div_a_a->a + (add_div_a_a->b + div_a->b*b))/div_a->b);
        } else if (no_overflow(op->type) &&
                   div_a && sub_div_a_a &&
                   !is_zero(sub_div_a_a->a) &&
                   is_simple_const(sub_div_a_a->a) &&
                   is_simple_const(div_a->b) &&
                   is_simple_const(b)) {
            // (c1 - y)/c2 + c3 + -> ((c1 + c2*c3) - y)/c2
            // If c1 == 0, we shouldn't pull in c3 inside the division; otherwise,
            // it will cause a cycle with the division simplification rule.
            return mutate(((sub_div_a_a->a + div_a->b*b) - sub_div_a_a->b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   div_b && add_div_b_a &&
                   is_simple_const(div_b->b) &&
                   equal(a, add_div_b_a->a)) {
            // x + (x + y)/c -> ((c + 1)*x + y)/c
            return mutate(((div_b->b + 1)*a + add_div_b_a->b)/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_b && sub_div_b_a &&
                   is_simple_const(div_b->b) &&
                   equal(a, sub_div_b_a->a)) {
            // x + (x - y)/c -> ((c + 1)*x - y)/c
            return mutate(((div_b->b + 1)*a - sub_div_b_a->b)/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_b && add_div_b_a &&
                   is_simple_const(div_b->b) &&
                   equal(a, add_div_b_a->b)) {
            // x + (y + x)/c -> ((c + 1)*x + y)/c
            return mutate(((div_b->b + 1)*a + add_div_b_a->a)/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_b && sub_div_b_a &&
                   is_simple_const(div_b->b) &&
                   equal(a, sub_div_b_a->b)) {
            // x + (y - x)/c -> ((c - 1)*x + y)/c
            return mutate(((div_b->b - 1)*a + sub_div_b_a->a)/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_a && add_div_a_a &&
                   is_simple_const(div_a->b) &&
                   equal(b, add_div_a_a->a)) {
            // (x + y)/c + x + -> ((c + 1)*x + y)/c
            return mutate(((div_a->b + 1)*b + add_div_a_a->b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   div_a && sub_div_a_a &&
                   is_simple_const(div_a->b) &&
                   equal(b, sub_div_a_a->a)) {
            // (x - y)/c + x + -> ((1 + c)*x - y)/c
            return mutate(((1 + div_a->b)*b - sub_div_a_a->b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   div_a && add_div_a_a &&
                   is_simple_const(div_a->b) &&
                   equal(b, add_div_a_a->b)) {
            // (y + x)/c + x -> (y + (1 + c)*x)/c
            return mutate((add_div_a_a->a + (1 + div_a->b)*b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   div_a && sub_div_a_a &&
                   is_simple_const(div_a->b) &&
                   equal(b, sub_div_a_a->b)) {
            // (y - x)/c + x -> (y + (-1 + c)*x)/c
            return mutate((sub_div_a_a->a + (- 1 + div_a->b)*b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   min_a &&
                   sub_a_b &&
                   equal(sub_a_b->b, b)) {
            // min(a, b-c) + c -> min(a+c, b)
            return mutate(Min::make(Add::make(min_a->a, b), sub_a_b->a));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   sub_a_a &&
                   equal(sub_a_a->b, b)) {
            // min(a-c, b) + c -> min(a, b+c)
            return mutate(Min::make(sub_a_a->a, Add::make(min_a->b, b)));
        } else if (no_overflow(op->type) &&
                   max_a &&
                   sub_a_b &&
                   equal(sub_a_b->b, b)) {
            // max(a, b-c) + c -> max(a+c, b)
            return mutate(Max::make(Add::make(max_a->a, b), sub_a_b->a));
        } else if (no_overflow(op->type) &&
                   max_a &&
                   sub_a_a &&
                   equal(sub_a_a->b, b)) {
            // max(a-c, b) + c -> max(a, b+c)
            return mutate(Max::make(sub_a_a->a, Add::make(max_a->b, b)));

        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_b &&
                   const_int(add_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ia + ib == 0) {
            // min(a, b + (-2)) + 2 -> min(a + 2, b)
            return mutate(Min::make(Add::make(min_a->a, b), add_a_b->a));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_a &&
                   const_int(add_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia + ib == 0) {
            // min(a + (-2), b) + 2 -> min(a, b + 2)
            return mutate(Min::make(add_a_a->a, Add::make(min_a->b, b)));
        } else if (no_overflow(op->type) &&
                   max_a &&
                   add_a_b &&
                   const_int(add_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ia + ib == 0) {
            // max(a, b + (-2)) + 2 -> max(a + 2, b)
            return mutate(Max::make(Add::make(max_a->a, b), add_a_b->a));
        } else if (no_overflow(op->type) &&
                   max_a &&
                   add_a_a &&
                   const_int(add_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia + ib == 0) {
            // max(a + (-2), b) + 2 -> max(a, b + 2)
            return mutate(Max::make(add_a_a->a, Add::make(max_a->b, b)));
        } else if (min_a &&
                   max_b &&
                   equal(min_a->a, max_b->a) &&
                   equal(min_a->b, max_b->b)) {
            // min(x, y) + max(x, y) -> x + y
            return mutate(min_a->a + min_a->b);
        } else if (min_a &&
                   max_b &&
                   equal(min_a->a, max_b->b) &&
                   equal(min_a->b, max_b->a)) {
            // min(x, y) + max(y, x) -> x + y
            return mutate(min_a->a + min_a->b);
        } else if (no_overflow(op->type) &&
                   div_a &&
                   add_a_a &&
                   const_int(add_a_a->b, &ia) &&
                   const_int(div_a->b, &ib) && ib &&
                   const_int(b, &ic)) {
            // ((a + ia) / ib + ic) -> (a + (ia + ib*ic)) / ib
            return mutate((add_a_a->a + IntImm::make(op->type, ia + ib*ic)) / div_a->b);
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->a, mul_b->a)) {
            // Pull out common factors a*x + b*x
            return mutate(mul_a->a * (mul_a->b + mul_b->b));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->b, mul_b->a)) {
            return mutate(mul_a->b * (mul_a->a + mul_b->b));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->b, mul_b->b)) {
            return mutate(mul_a->b * (mul_a->a + mul_b->a));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->a, mul_b->b)) {
            return mutate(mul_a->a * (mul_a->b + mul_b->a));
        } else if (mod_a &&
                   mul_b &&
                   equal(mod_a->b, mul_b->b)) {
            // (x%3) + y*3 -> y*3 + x%3
            return mutate(b + a);
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   mod_b &&
                   div_a_a &&
                   equal(mul_a->b, div_a_a->b) &&
                   equal(mul_a->b, mod_b->b) &&
                   equal(div_a_a->a, mod_b->a)) {
            // (x/3)*3 + x%3 -> x
            return div_a_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_a &&
                   mod_b &&
                   equal(mul_a_a->b, mod_b->b) &&
                   (!mod_a_b || !equal(mod_a_b->b, mod_b->b))) {
            // ((x*3) + y) + z%3 -> (x*3 + z%3) + y
            return mutate((add_a->a + b) + add_a->b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mod_a_a &&
                   mul_b &&
                   equal(mod_a_a->b, mul_b->b) &&
                   (!mod_a_b || !equal(mod_a_b->b, mul_b->b))) {
            // ((x%3) + y) + z*3 -> (z*3 + x%3) + y
            return mutate((b + add_a->a) + add_a->b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_b &&
                   mod_b &&
                   equal(mul_a_b->b, mod_b->b) &&
                   (!mod_a_a || !equal(mod_a_a->b, mod_b->b))) {
            // (y + (x*3)) + z%3 -> y + (x*3 + z%3)
            return mutate(add_a->a + (add_a->b + b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mod_a_b &&
                   mul_b &&
                   equal(mod_a_b->b, mul_b->b) &&
                   (!mod_a_a || !equal(mod_a_a->b, mul_b->b))) {
            // (y + (x%3)) + z*3 -> y + (z*3 + x%3)
            return mutate(add_a->a + (b + add_a->b));
        } else if (mul_a && mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   ia % ib == 0) {
            // x*4 + y*2 -> (x*2 + y)*2
            Expr ratio = make_const(a.type(), div_imp(ia, ib));
            return mutate((mul_a->a * ratio + mul_b->a) * mul_b->b);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            // If we've made no changes, and can't find a rule to apply, return the operator unchanged.
            return op;
        } else {
            return Add::make(a, b);
        }
    }

    Expr visit(const Sub *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        int64_t ia = 0, ib = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;

        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();

        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Div *div_a_a = mul_a ? mul_a->a.as<Div>() : nullptr;
        const Div *div_b_a = mul_b ? mul_b->a.as<Div>() : nullptr;

        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();

        const Add *add_div_a_a = div_a ? div_a->a.as<Add>(): nullptr;
        const Sub *sub_div_a_a = div_a ? div_a->a.as<Sub>(): nullptr;
        const Add *add_div_b_a = div_b ? div_b->a.as<Add>(): nullptr;
        const Sub *sub_div_b_a = div_b ? div_b->a.as<Sub>(): nullptr;

        const Min *min_b = b.as<Min>();
        const Add *add_b_a = min_b ? min_b->a.as<Add>() : nullptr;
        const Add *add_b_b = min_b ? min_b->b.as<Add>() : nullptr;

        const Min *min_a = a.as<Min>();
        const Add *add_a_a = min_a ? min_a->a.as<Add>() : nullptr;
        const Add *add_a_b = min_a ? min_a->b.as<Add>() : nullptr;

        if (add_a) {
            add_a_a = add_a->a.as<Add>();
            add_a_b = add_a->b.as<Add>();
        }

        if (div_a) {
            add_a_a = div_a->a.as<Add>();
            add_a_b = div_a->b.as<Add>();
        }
        if (div_b) {
            add_b_a = div_b->a.as<Add>();
            add_b_b = div_b->b.as<Add>();
        }

        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();

        const Sub *sub_a_a = div_a ? div_a->a.as<Sub>() : nullptr;
        const Sub *sub_b_a = div_b ? div_b->a.as<Sub>() : nullptr;

        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();

        if (is_zero(b)) {
            return a;
        } else if (equal(a, b)) {
            return make_zero(op->type);
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            if (no_overflow(a.type()) &&
                sub_would_overflow(a.type().bits(), ia, ib)) {
                return signed_integer_overflow_error(a.type());
            } else {
                return IntImm::make(a.type(), ia - ib);
            }
        } else if (const_uint(a, &ua) && const_uint(b, &ub)) {
            return UIntImm::make(a.type(), ua - ub);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            return FloatImm::make(a.type(), fa - fb);
        } else if (const_int(b, &ib)) {
            return mutate(a + IntImm::make(a.type(), (-ib)));
        } else if (const_float(b, &fb)) {
            return mutate(a + FloatImm::make(a.type(), (-fb)));
        } else if (call_a &&
                   call_a->is_intrinsic(Call::signed_integer_overflow)) {
            return a;
        } else if (call_b &&
                   call_b->is_intrinsic(Call::signed_integer_overflow)) {
            return b;
        } else if (ramp_a && ramp_b) {
            // Ramp - Ramp
            return mutate(Ramp::make(ramp_a->base - ramp_b->base,
                                     ramp_a->stride - ramp_b->stride, ramp_a->lanes));
        } else if (ramp_a && broadcast_b) {
            // Ramp - Broadcast
            return mutate(Ramp::make(ramp_a->base - broadcast_b->value,
                                     ramp_a->stride, ramp_a->lanes));
        } else if (broadcast_a && ramp_b) {
            // Broadcast - Ramp
            return mutate(Ramp::make(broadcast_a->value - ramp_b->base,
                                     make_zero(ramp_b->stride.type())- ramp_b->stride,
                                     ramp_b->lanes));
        } else if (broadcast_a && broadcast_b) {
            // Broadcast + Broadcast
            return Broadcast::make(mutate(broadcast_a->value - broadcast_b->value),
                                   broadcast_a->lanes);
        } else if (select_a && select_b &&
                   equal(select_a->condition, select_b->condition)) {
            // select(c, a, b) - select(c, d, e) -> select(c, a+d, b+e)
            return mutate(Select::make(select_a->condition,
                                       select_a->true_value - select_b->true_value,
                                       select_a->false_value - select_b->false_value));
        } else if (select_a &&
                   equal(select_a->true_value, b)) {
            // select(c, a, b) - a -> select(c, 0, b-a)
            return mutate(Select::make(select_a->condition,
                                       make_zero(op->type),
                                       select_a->false_value - select_a->true_value));
        } else if (select_a &&
                   equal(select_a->false_value, b)) {
            // select(c, a, b) - b -> select(c, a-b, 0)
            return mutate(Select::make(select_a->condition,
                                       select_a->true_value - select_a->false_value,
                                       make_zero(op->type)));
        } else if (select_b &&
                   equal(select_b->true_value, a)) {
            // a - select(c, a, b) -> select(c, 0, a-b)
            return mutate(Select::make(select_b->condition,
                                       make_zero(op->type),
                                       select_b->true_value - select_b->false_value));
        } else if (select_b &&
                   equal(select_b->false_value, a)) {
            // b - select(c, a, b) -> select(c, b-a, 0)
            return mutate(Select::make(select_b->condition,
                                       select_b->false_value - select_b->true_value,
                                       make_zero(op->type)));
        } else if (add_a && equal(add_a->b, b)) {
            // Ternary expressions where a term cancels
            return add_a->a;
        } else if (add_a &&
                   equal(add_a->a, b)) {
            return add_a->b;
        } else if (add_b &&
                   equal(add_b->b, a)) {
            return mutate(make_zero(add_b->a.type()) - add_b->a);
        } else if (add_b &&
                   equal(add_b->a, a)) {
            return mutate(make_zero(add_b->a.type()) - add_b->b);
        } else if (max_a &&
                   equal(max_a->a, b) &&
                   !is_const(b) &&
                   no_overflow(op->type)) {
            // max(a, b) - a -> max(0, b-a)
            return mutate(Max::make(make_zero(op->type), max_a->b - max_a->a));
        } else if (min_a &&
                   equal(min_a->a, b) &&
                   !is_const(b) &&
                   no_overflow(op->type)) {
            // min(a, b) - a -> min(0, b-a)
            return mutate(Min::make(make_zero(op->type), min_a->b - min_a->a));
        } else if (max_a &&
                   equal(max_a->b, b) &&
                   !is_const(b) &&
                   no_overflow(op->type)) {
            // max(a, b) - b -> max(a-b, 0)
            return mutate(Max::make(max_a->a - max_a->b, make_zero(op->type)));
        } else if (min_a &&
                   equal(min_a->b, b) &&
                   !is_const(b) &&
                   no_overflow(op->type)) {
            // min(a, b) - b -> min(a-b, 0)
            return mutate(Min::make(min_a->a - min_a->b, make_zero(op->type)));
        } else if (max_b &&
                   equal(max_b->a, a) &&
                   !is_const(a) &&
                   no_overflow(op->type)) {
            // a - max(a, b) -> 0 - max(0, b-a) -> min(0, a-b)
            return mutate(Min::make(make_zero(op->type), max_b->a - max_b->b));
        } else if (min_b &&
                   equal(min_b->a, a) &&
                   !is_const(a) &&
                   no_overflow(op->type)) {
            // a - min(a, b) -> 0 - min(0, b-a) -> max(0, a-b)
            return mutate(Max::make(make_zero(op->type), min_b->a - min_b->b));
        } else if (max_b &&
                   equal(max_b->b, a) &&
                   !is_const(a) &&
                   no_overflow(op->type)) {
            // b - max(a, b) -> 0 - max(a-b, 0) -> min(b-a, 0)
            return mutate(Min::make(max_b->b - max_b->a, make_zero(op->type)));
        } else if (min_b &&
                   equal(min_b->b, a) &&
                   !is_const(a) &&
                   no_overflow(op->type)) {
            // b - min(a, b) -> 0 - min(a-b, 0) -> max(b-a, 0)
            return mutate(Max::make(min_b->b - min_b->a, make_zero(op->type)));
        } else if (add_a &&
                   is_simple_const(add_a->b)) {
            // In ternary expressions, pull constants outside
            if (is_simple_const(b)) {
                return mutate(add_a->a + (add_a->b - b));
            } else {
                return mutate((add_a->a - b) + add_a->b);
            }
        } else if (sub_a &&
                   sub_b &&
                   is_const(sub_a->a) &&
                   is_const(sub_b->a)) {
            // (c1 - a) - (c2 - b) -> (b - a) + (c1 - c2)
            return mutate((sub_b->b - sub_a->b) + (sub_a->a - sub_b->a));
        } else if (sub_b) {
            // a - (b - c) -> a + (c - b)
            return mutate(a + (sub_b->b - sub_b->a));
        } else if (mul_b &&
                   is_negative_negatable_const(mul_b->b)) {
            // a - b*-x -> a + b*x
            return mutate(a + mul_b->a * (-mul_b->b));
        } else if (mul_b &&
                   !is_const(a) &&
                   equal(a, mul_b->a) &&
                   no_overflow(op->type)) {
            // a - a*b -> a*(1 - b)
            return mutate(a * (make_one(op->type) - mul_b->b));
        } else if (mul_b &&
                   !is_const(a) &&
                   equal(a, mul_b->b) &&
                   no_overflow(op->type)) {
            // a - b*a -> (1 - b)*a
            return mutate((make_one(op->type) - mul_b->a) * a);
        } else if (mul_a &&
                   !is_const(b) &&
                   equal(mul_a->a, b) &&
                   no_overflow(op->type)) {
            // a*b - a -> a*(b - 1)
            return mutate(mul_a->a * (mul_a->b - make_one(op->type)));
        } else if (mul_a &&
                   !is_const(b) &&
                   equal(mul_a->b, b) &&
                   no_overflow(op->type)) {
            // a*b - b -> (a - 1)*b
            return mutate((mul_a->a - make_one(op->type)) * b);
        } else if (add_b &&
                   is_simple_const(add_b->b)) {
            return mutate((a - add_b->a) - add_b->b);
        } else if (sub_a &&
                   is_simple_const(sub_a->a) &&
                   is_simple_const(b)) {
            return mutate((sub_a->a - b) - sub_a->b);
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->a, mul_b->a)) {
            // Pull out common factors a*x + b*x
            return mutate(mul_a->a * (mul_a->b - mul_b->b));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->b, mul_b->a)) {
            return mutate(mul_a->b * (mul_a->a - mul_b->b));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->b, mul_b->b)) {
            return mutate(mul_a->b * (mul_a->a - mul_b->a));
        } else if (mul_a &&
                   mul_b &&
                   equal(mul_a->a, mul_b->b)) {
            return mutate(mul_a->a * (mul_a->b - mul_b->a));
        } else if (add_a &&
                   add_b &&
                   equal(add_a->b, add_b->b)) {
            // Quaternary expressions where a term cancels
            // (a + b) - (c + b) -> a - c
            return mutate(add_a->a - add_b->a);
        } else if (add_a &&
                   add_b &&
                   equal(add_a->a, add_b->a)) {
            // (a + b) - (a + c) -> b - c
            return mutate(add_a->b - add_b->b);
        } else if (add_a &&
                   add_b &&
                   equal(add_a->a, add_b->b)) {
            // (a + b) - (c + a) -> b - c
            return mutate(add_a->b - add_b->a);
        } else if (add_a &&
                   add_b &&
                   equal(add_a->b, add_b->a)) {
            // (b + a) - (a + c) -> b - c
            return mutate(add_a->a - add_b->b);
        } else if (add_a &&
                   add_a_a &&
                   equal(add_a_a->a, b)) {
            // ((a + b) + c) - a -> b + c
            return mutate(add_a_a->b + add_a->b);
        } else if (add_a &&
                   add_a_a &&
                   equal(add_a_a->b, b)) {
            // ((a + b) + c) - b -> a + c
            return mutate(add_a_a->a + add_a->b);
        } else if (add_a &&
                   add_a_b &&
                   equal(add_a_b->a, b)) {
            // (a + (b + c)) - b -> a + c
            return mutate(add_a->a + add_a_b->b);
        } else if (add_a &&
                   add_a_b &&
                   equal(add_a_b->b, b)) {
            // (a + (b + c)) - c -> a + b
            return mutate(add_a->a + add_a_b->a);
        } else if (no_overflow(op->type) &&
                   div_b && sub_div_b_a &&
                   is_simple_const(a) &&
                   is_simple_const(sub_div_b_a->a) &&
                   is_simple_const(div_b->b) &&
                   is_positive_const(div_b->b)) {
            // c1 - (c2 - y)/c3 and c3 > 0-> ((c1*c3 - c2 + (c3 - 1)) + y)/c3
            return mutate(((a*div_b->b - sub_div_b_a->a) + sub_div_b_a->b + (div_b->b - 1))/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_b && add_div_b_a &&
                   is_simple_const(a) &&
                   is_simple_const(add_div_b_a->b) &&
                   is_simple_const(div_b->b) &&
                   is_positive_const(div_b->b)) {
            // c1 - (y + c2)/c3 and c3 > 0 -> ((c1*c3 - c2 + (c3 - 1)) - y)/c3
            return mutate(((a*div_b->b - add_div_b_a->b) - add_div_b_a->a + (div_b->b - 1))/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_b && add_div_b_a &&
                   is_simple_const(div_b->b) &&
                   is_positive_const(div_b->b) &&
                   equal(a, add_div_b_a->a)) {
            // x - (x + y)/c and c > 0 -> ((c - 1)*x - y + (c - 1))/c
            return mutate(((div_b->b - 1)*a - add_div_b_a->b + (div_b->b - 1))/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_b && sub_div_b_a &&
                   is_simple_const(div_b->b) &&
                   is_positive_const(div_b->b) &&
                   equal(a, sub_div_b_a->a)) {
            // x - (x - y)/c and c > 0 -> ((c - 1)*x + y + (c - 1))/c
            return mutate(((div_b->b - 1)*a + sub_div_b_a->b + (div_b->b - 1))/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_b && add_div_b_a &&
                   is_simple_const(div_b->b) &&
                   is_positive_const(div_b->b) &&
                   equal(a, add_div_b_a->b)) {
            // x - (y + x)/c and c > 0 -> ((c - 1)*x - y + (c - 1))/c
            return mutate(((div_b->b - 1)*a - add_div_b_a->a + (div_b->b - 1))/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_b && sub_div_b_a &&
                   is_simple_const(div_b->b) &&
                   is_positive_const(div_b->b) &&
                   equal(a, sub_div_b_a->b)) {
            // x - (y - x)/c and c > 0 -> ((c + 1)*x - y + (c - 1))/c
            return mutate(((div_b->b + 1)*a - sub_div_b_a->a + (div_b->b - 1))/div_b->b);
        } else if (no_overflow(op->type) &&
                   div_a && add_div_a_a &&
                   is_simple_const(div_a->b) &&
                   equal(b, add_div_a_a->a)) {
            // (x + y)/c - x + -> ((1 - c)*x + y)/c
            return mutate(((1 - div_a->b)*b + add_div_a_a->b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   div_a && sub_div_a_a &&
                   is_simple_const(div_a->b) &&
                   equal(b, sub_div_a_a->a)) {
            // (x - y)/c - x + -> ((1 - c)*x - y)/c
            return mutate(((1 - div_a->b)*b - sub_div_a_a->b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   div_a && add_div_a_a &&
                   is_simple_const(div_a->b) &&
                   equal(b, add_div_a_a->b)) {
            // (y + x)/c - x -> (y + (1 - c)*x)/c
            return mutate((add_div_a_a->a + (1 - div_a->b)*b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   div_a && sub_div_a_a &&
                   is_simple_const(div_a->b) &&
                   equal(b, sub_div_a_a->b)) {
            // (y - x)/c - x -> (y - (c + 1)*x)/c
            return mutate((sub_div_a_a->a - (div_a->b + 1)*b)/div_a->b);
        } else if (no_overflow(op->type) &&
                   min_b &&
                   add_b_a &&
                   equal(a, add_b_a->a)) {
            // Quaternary expressions involving mins where a term
            // cancels. These are important for bounds inference
            // simplifications.
            // a - min(a + b, c) -> max(-b, a-c)
            return mutate(max(0 - add_b_a->b, a - min_b->b));
        } else if (no_overflow(op->type) &&
                   min_b &&
                   add_b_a &&
                   equal(a, add_b_a->b)) {
            // a - min(b + a, c) -> max(-b, a-c)
            return mutate(max(0 - add_b_a->a, a - min_b->b));
        } else if (no_overflow(op->type) &&
                   min_b &&
                   add_b_b &&
                   equal(a, add_b_b->a)) {
            // a - min(c, a + b) -> max(-b, a-c)
            return mutate(max(0 - add_b_b->b, a - min_b->a));
        } else if (no_overflow(op->type) &&
                   min_b &&
                   add_b_b &&
                   equal(a, add_b_b->b)) {
            // a - min(c, b + a) -> max(-b, a-c)
            return mutate(max(0 - add_b_b->a, a - min_b->a));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_a &&
                   equal(b, add_a_a->a)) {
            // min(a + b, c) - a -> min(b, c-a)
            return mutate(min(add_a_a->b, min_a->b - b));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_a &&
                   equal(b, add_a_a->b)) {
            // min(b + a, c) - a -> min(b, c-a)
            return mutate(min(add_a_a->a, min_a->b - b));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_b &&
                   equal(b, add_a_b->a)) {
            // min(c, a + b) - a -> min(b, c-a)
            return mutate(min(add_a_b->b, min_a->a - b));
        } else if (no_overflow(op->type) &&
                   min_a &&
                   add_a_b &&
                   equal(b, add_a_b->b)) {
            // min(c, b + a) - a -> min(b, c-a)
            return mutate(min(add_a_b->a, min_a->a - b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->b) &&
                   equal(min_a->b, min_b->a)) {
            // min(a, b) - min(b, a) -> 0
            return make_zero(op->type);
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->b) &&
                   equal(max_a->b, max_b->a)) {
            // max(a, b) - max(b, a) -> 0
            return make_zero(op->type);
        } else if (no_overflow(op->type) &&
                   min_a &&
                   min_b &&
                   is_zero(mutate((min_a->a + min_b->b) - (min_a->b + min_b->a)))) {
            // min(a, b) - min(c, d) where a-b == c-d -> b - d
            return mutate(min_a->b - min_b->b);
        } else if (no_overflow(op->type) &&
                   max_a &&
                   max_b &&
                   is_zero(mutate((max_a->a + max_b->b) - (max_a->b + max_b->a)))) {
            // max(a, b) - max(c, d) where a-b == c-d -> b - d
            return mutate(max_a->b - max_b->b);
        } else if (no_overflow(op->type) &&
                   min_a &&
                   min_b &&
                   is_zero(mutate((min_a->a + min_b->a) - (min_a->b + min_b->b)))) {
            // min(a, b) - min(c, d) where a-b == d-c -> b - c
            return mutate(min_a->b - min_b->a);
        } else if (no_overflow(op->type) &&
                   max_a &&
                   max_b &&
                   is_zero(mutate((max_a->a + max_b->a) - (max_a->b + max_b->b)))) {
            // max(a, b) - max(c, d) where a-b == d-c -> b - c
            return mutate(max_a->b - max_b->a);
        } else if (no_overflow(op->type) &&
                   (op->type.is_int() || op->type.is_uint()) &&
                   mul_a &&
                   div_a_a &&
                   is_positive_const(mul_a->b) &&
                   equal(mul_a->b, div_a_a->b) &&
                   equal(div_a_a->a, b)) {
            // (x/4)*4 - x -> -(x%4)
            return mutate(make_zero(a.type()) - (b % mul_a->b));
        } else if (no_overflow(op->type) &&
                   (op->type.is_int() || op->type.is_uint()) &&
                   mul_b &&
                   div_b_a &&
                   is_positive_const(mul_b->b) &&
                   equal(mul_b->b, div_b_a->b) &&
                   equal(div_b_a->a, a)) {
            // x - (x/4)*4 -> x%4
            return mutate(a % mul_b->b);
        } else if (mul_a &&
                   mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   ib % ia == 0) {
            // x * a - y * (a * b) -> (x - y * b) * a
            Expr ratio = make_const(a.type(), div_imp(ib, ia));
            return mutate((mul_a->a - mul_b->a * ratio) * mul_a->b);
        } else if (mul_a &&
                   mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   ia % ib == 0) {
            // x * (a * b) - y * a -> (x * b - y) * a
            Expr ratio = make_const(a.type(), div_imp(ia, ib));
            return mutate((mul_a->a * ratio - mul_b->a) * mul_b->b);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   add_a_a &&
                   add_b_a &&
                   equal(add_a_a->a, add_b_a->a) &&
                   (is_simple_const(add_a_a->b) ||
                    is_simple_const(add_b_a->b))) {
            // This pattern comes up in bounds inference on upsampling code:
            // (x + a)/c - (x + b)/c ->
            //    ((c + a - 1 - b) - (x + a)%c)/c (duplicates a)
            // or ((x + b)%c + (a - b))/c         (duplicates b)
            Expr x = add_a_a->a, a = add_a_a->b, b = add_b_a->b, c = div_a->b;
            if (is_simple_const(b)) {
                // Use the version that injects two copies of b
                return mutate((((x + (b % c)) % c) + (a - b))/c);
            } else {
                // Use the version that injects two copies of a
                return mutate((((c + a - 1) - b) - ((x + (a % c)) % c))/c);
            }
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   add_b_a &&
                   equal(div_a->a, add_b_a->a)) {
            // Same as above, where a == 0
            Expr x = div_a->a, b = add_b_a->b, c = div_a->b;
            return mutate(((c - 1 - b) - (x % c))/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   add_a_a &&
                   equal(add_a_a->a, div_b->a)) {
            // Same as above, where b == 0
            Expr x = add_a_a->a, a = add_a_a->b, c = div_a->b;
            return mutate(((x % c) + a)/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   sub_b_a &&
                   equal(div_a->a, sub_b_a->a)) {
            // Same as above, where a == 0 and b is subtracted
            Expr x = div_a->a, b = sub_b_a->b, c = div_a->b;
            return mutate(((c - 1 + b) - (x % c))/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   sub_a_a &&
                   equal(sub_a_a->a, div_b->a)) {
            // Same as above, where b == 0, and a is subtracted
            Expr x = sub_a_a->a, a = sub_a_a->b, c = div_a->b;
            return mutate(((x % c) - a)/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   sub_a_a &&
                   add_b_a &&
                   equal(sub_a_a->a, add_b_a->a) &&
                   is_simple_const(add_b_a->b)) {
            // Same as above, where a is subtracted and b is a constant
            // (x - a)/c - (x + b)/c -> ((x + b)%c - a - b)/c
            Expr x = sub_a_a->a, a = sub_a_a->b, b = add_b_a->b, c = div_a->b;
            return mutate((((x + (b % c)) % c) - a - b)/c);
        } else if (div_a &&
                   div_b &&
                   is_positive_const(div_a->b) &&
                   equal(div_a->b, div_b->b) &&
                   op->type.is_int() &&
                   no_overflow(op->type) &&
                   add_a_a &&
                   sub_b_a &&
                   equal(add_a_a->a, sub_b_a->a) &&
                   is_simple_const(add_a_a->b)) {
            // Same as above, where b is subtracted and a is a constant
            // (x + a)/c - (x - b)/c -> (b - (x + a)%c + (a + c - 1))/c
            Expr x = add_a_a->a, a = add_a_a->b, b = sub_b_a->b, c = div_a->b;
            return mutate((b - (x + (a % c))%c + (a + c - 1))/c);
        } else if (no_overflow(op->type) &&
                   min_a &&
                   min_b &&
                   equal(min_a->a, min_b->a) &&
                   is_simple_const(min_a->b) &&
                   is_simple_const(min_b->b)) {
            // min(x, c1) - min(x, c2) where c1 and c2 are constants
            // if c1 >= c2 -> clamp(x, c2, c1) - c2
            // else -> c1 - clamp(x, c1, c2)
            if (is_one(mutate(min_a->b >= min_b->b))) {
                return mutate(clamp(min_a->a, min_b->b, min_a->b) - min_b->b);
            } else {
                return mutate(min_a->b - clamp(min_a->a, min_a->b, min_b->b));
            }
        } else if (no_overflow(op->type) &&
                   max_a &&
                   max_b &&
                   equal(max_a->a, max_b->a) &&
                   is_simple_const(max_a->b) &&
                   is_simple_const(max_b->b)) {
            // max(x, c1) - max(x, c2) where c1 and c2 are constants
            // if c1 >= c2 -> c1 - clamp(x, c2, c1)
            // else -> clamp(x, c1, c2) - c2
            if (is_one(mutate(max_a->b >= max_b->b))) {
                return mutate(max_a->b - clamp(max_a->a, max_b->b, max_a->b));
            } else {
                return mutate(clamp(max_a->a, max_a->b, max_b->b)- max_b->b);
            }
        } else if (no_overflow(op->type) &&
                   min_a &&
                   min_b) {
            // min(a + c1, b + c2) - min(a + c3, b + c4)
            //     where delta_a = c1 - c3 and delta_b = c2 - c4 are constants
            // if delta_b - delta_a <= 0 -> clamp((b + c2) - (a + c1), delta_b - delta_a, 0) + delta_a
            // else -> delta_b - clamp((b + c2) - (a + c1), 0, delta_b - delta_a)
            Expr delta_a = mutate(min_a->a - min_b->a);
            Expr delta_b = mutate(min_a->b - min_b->b);
            if (is_simple_const(delta_a) &&
                is_simple_const(delta_b)) {
                Expr diff = delta_b - delta_a;
                if (is_one(mutate(diff <= make_zero(op->type)))) {
                    return mutate(clamp(min_a->b - min_a->a, diff, make_zero(op->type)) + delta_a);
                } else {
                    return mutate(delta_b - clamp(min_a->b - min_a->a, make_zero(op->type), diff));
                }
            } else if (is_simple_const(mutate(min_a->a - min_b->b)) &&
                       is_simple_const(mutate(min_a->b - min_b->a))) {
                // Canonicalize min(a + c1, b + c2) - min(b + c4, a + c3)
                //     where c1, c2, c3, and c4 are constants
                // into min(a + c1, b + c2) - min(a + c3, b + c4)
                // so that the previous rule can pick it up
                return mutate(a - Min::make(min_b->b, min_b->a));
            } else if (a.same_as(op->a) && b.same_as(op->b)) {
                return op;
            } else {
                return Sub::make(a, b);
            }
        } else if (no_overflow(op->type) &&
                   max_a &&
                   max_b) {
            // max(a + c1, b + c2) - max(a + c3, b + c4)
            //     where delta_a = c1 - c3 and delta_b = c2 - c4 are constants
            // if delta_b - delta_a <= 0 -> delta_b - clamp((b + c2) - (a + c1), delta_b - delta_a, 0)
            // else -> clamp((b + c2) - (a + c1), 0, delta_b - delta_a) + delta_a
            Expr delta_a = mutate(max_a->a - max_b->a);
            Expr delta_b = mutate(max_a->b - max_b->b);
            if (is_simple_const(delta_a) &&
                is_simple_const(delta_b)) {
                Expr diff = delta_b - delta_a;
                if (is_one(mutate(diff <= make_zero(op->type)))) {
                    return mutate(delta_b - clamp(max_a->b - max_a->a, diff, make_zero(op->type)));
                } else {
                    return mutate(clamp(max_a->b - max_a->a, make_zero(op->type), diff) + delta_a);
                }
            } else if (is_simple_const(mutate(max_a->a - max_b->b)) &&
                       is_simple_const(mutate(max_a->b - max_b->a))) {
                // Canonicalize max(a + c1, b + c2) - max(b + c4, a + c3)
                //     where c1, c2, c3, and c4 are constants
                // into max(a + c1, b + c2) - max(a + c3, b + c4)
                // so that the previous rule can pick it up
                return mutate(a - Max::make(max_b->b, max_b->a));
            } else if (a.same_as(op->a) && b.same_as(op->b)) {
                return op;
            } else {
                return Sub::make(a, b);
            }
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Sub::make(a, b);
        }
    }

    Expr visit(const Mul *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        if (is_simple_const(a) ||
            (b.as<Min>() && a.as<Max>())) {
            std::swap(a, b);
        }

        int64_t ia = 0, ib = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;

        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();
        const Shuffle *shuffle_a = a.as<Shuffle>();
        const Shuffle *shuffle_b = b.as<Shuffle>();
        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Min *min_a = a.as<Min>();
        const Mul *mul_b = b.as<Mul>();
        const Max *max_b = b.as<Max>();

        if (is_zero(a)) {
            return a;
        } else if (is_zero(b)) {
            return b;
        } else if (is_one(a)) {
            return b;
        } else if (is_one(b)) {
            return a;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            if (no_overflow(a.type()) &&
                mul_would_overflow(a.type().bits(), ia, ib)) {
                return signed_integer_overflow_error(a.type());
            } else {
                return IntImm::make(a.type(), ia * ib);
            }
        } else if (const_uint(a, &ua) && const_uint(b, &ub)) {
            return UIntImm::make(a.type(), ua * ub);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            return FloatImm::make(a.type(), fa * fb);
        } else if (call_a &&
                   call_a->is_intrinsic(Call::signed_integer_overflow)) {
            return a;
        } else if (call_b &&
                   call_b->is_intrinsic(Call::signed_integer_overflow)) {
            return b;
        } else if (shuffle_a && shuffle_b &&
                   shuffle_a->is_slice() &&
                   shuffle_b->is_slice()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return hoist_slice_vector<Mul>(op);
            } else {
                return hoist_slice_vector<Mul>(Mul::make(a, b));
            }
        }else if (broadcast_a && broadcast_b) {
            return Broadcast::make(mutate(broadcast_a->value * broadcast_b->value), broadcast_a->lanes);
        } else if (ramp_a && broadcast_b) {
            Expr m = broadcast_b->value;
            return mutate(Ramp::make(ramp_a->base * m, ramp_a->stride * m, ramp_a->lanes));
        } else if (broadcast_a && ramp_b) {
            Expr m = broadcast_a->value;
            return mutate(Ramp::make(m * ramp_b->base, m * ramp_b->stride, ramp_b->lanes));
        } else if (add_a &&
                   !(add_a->b.as<Ramp>() && ramp_b) &&
                   is_simple_const(add_a->b) &&
                   is_simple_const(b)) {
            return mutate(add_a->a * b + add_a->b * b);
        } else if (sub_a && is_negative_negatable_const(b)) {
            return mutate(Mul::make(Sub::make(sub_a->b, sub_a->a), -b));
        } else if (mul_a && is_simple_const(mul_a->b) && is_simple_const(b)) {
            return mutate(mul_a->a * (mul_a->b * b));
        } else if (mul_b && is_simple_const(mul_b->b)) {
            // Pull constants outside
            return mutate((a * mul_b->a) * mul_b->b);
        } else if (min_a &&
                   max_b &&
                   equal(min_a->a, max_b->a) &&
                   equal(min_a->b, max_b->b)) {
            // min(x, y) * max(x, y) -> x*y
            return mutate(min_a->a * min_a->b);
        } else if (min_a &&
                   max_b &&
                   equal(min_a->a, max_b->b) &&
                   equal(min_a->b, max_b->a)) {
            // min(x, y) * max(y, x) -> x*y
            return mutate(min_a->a * min_a->b);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Mul::make(a, b);
        }
    }

    Expr visit(const Div *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        int64_t ia = 0, ib = 0, ic = 0, id = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;

        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Div *div_a = a.as<Div>();
        const Div *div_a_a = nullptr;
        const Mul *mul_a_a = nullptr;
        const Mul *mul_a_b = nullptr;
        const Add *add_a_a = nullptr;
        const Add *add_a_b = nullptr;
        const Sub *sub_a_a = nullptr;
        const Sub *sub_a_b = nullptr;
        const Mul *mul_a_a_a = nullptr;
        const Mul *mul_a_b_a = nullptr;
        const Mul *mul_a_b_b = nullptr;

        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Ramp *ramp_a = a.as<Ramp>();
        const Broadcast *broadcast_b = b.as<Broadcast>();

        if (add_a) {
            div_a_a = add_a->a.as<Div>();
            mul_a_a = add_a->a.as<Mul>();
            mul_a_b = add_a->b.as<Mul>();
            add_a_a = add_a->a.as<Add>();
            add_a_b = add_a->b.as<Add>();
            sub_a_a = add_a->a.as<Sub>();
            sub_a_b = add_a->b.as<Sub>();
        } else if (sub_a) {
            mul_a_a = sub_a->a.as<Mul>();
            mul_a_b = sub_a->b.as<Mul>();
            add_a_a = sub_a->a.as<Add>();
            add_a_b = sub_a->b.as<Add>();
            sub_a_a = sub_a->a.as<Sub>();
            sub_a_b = sub_a->b.as<Sub>();
        }

        if (add_a_a) {
            mul_a_a_a = add_a_a->a.as<Mul>();
        } else if (sub_a_a) {
            mul_a_a_a = sub_a_a->a.as<Mul>();
        }

        if (add_a_b) {
            mul_a_b_a = add_a_b->a.as<Mul>();
            mul_a_b_b = add_a_b->b.as<Mul>();
        } else if (sub_a_b) {
            mul_a_b_a = sub_a_b->a.as<Mul>();
            mul_a_b_b = sub_a_b->b.as<Mul>();
        }

        if (ramp_a) {
            mul_a_a = ramp_a->base.as<Mul>();
        }

        // Check for bounded numerators divided by constant
        // denominators.
        int64_t num_min, num_max;
        if (const_int(b, &ib) && ib &&
            const_int_bounds(a, &num_min, &num_max) &&
            div_imp(num_max, ib) == div_imp(num_min, ib)) {
            return make_const(op->type, div_imp(num_max, ib));
        }

        ModulusRemainder mod_rem(0, 1);
        if (ramp_a && no_overflow_scalar_int(ramp_a->base.type())) {
            // Do modulus remainder analysis on the base.
            mod_rem = modulus_remainder(ramp_a->base, alignment_info);
        }

        if (is_zero(b) && !op->type.is_float()) {
            return indeterminate_expression_error(op->type);
        } else if (is_zero(a)) {
            return a;
        } else if (is_one(b)) {
            return a;
        } else if (equal(a, b)) {
            return make_one(op->type);
        } else if (const_int(a, &ia) &&
                   const_int(b, &ib)) {
            return IntImm::make(op->type, div_imp(ia, ib));
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            return UIntImm::make(op->type, ua / ub);
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb) &&
                   fb != 0.0f) {
            return FloatImm::make(op->type, fa / fb);
        } else if (broadcast_a && broadcast_b) {
            return mutate(Broadcast::make(Div::make(broadcast_a->value, broadcast_b->value), broadcast_a->lanes));
        } else if (no_overflow_scalar_int(op->type) &&
                   is_const(a, -1)) {
            // -1/x -> select(x < 0, 1, -1)
            return mutate(select(b < make_zero(op->type),
                                 make_one(op->type),
                                 make_const(op->type, -1)));
        } else if (ramp_a &&
                   no_overflow_scalar_int(ramp_a->base.type()) &&
                   const_int(ramp_a->stride, &ia) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib &&
                   ia % ib == 0) {
            // ramp(x, 4, w) / broadcast(2, w) -> ramp(x / 2, 2, w)
            Type t = op->type.element_of();
            return mutate(Ramp::make(ramp_a->base / broadcast_b->value,
                                     IntImm::make(t, div_imp(ia, ib)),
                                     ramp_a->lanes));
        } else if (ramp_a &&
                   no_overflow_scalar_int(ramp_a->base.type()) &&
                   const_int(ramp_a->stride, &ia) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib != 0 &&
                   (ic = gcd(mod_rem.modulus, ib)) > 1 &&
                   div_imp((int64_t)mod_rem.remainder, ic) == div_imp(mod_rem.remainder + (ramp_a->lanes-1)*ia, ic)) {
            // ramp(k*(a*c) + x, y, w) / (b*c) = broadcast(k/b, w) if x/c == (x + (w-1)*y)/c
            // The ramp lanes can't actually change the result, so we
            // can just divide the base and broadcast it.
            return mutate(Broadcast::make(ramp_a->base / broadcast_b->value, ramp_a->lanes));
        } else if (no_overflow(op->type) &&
                   div_a &&
                   const_int(div_a->b, &ia) &&
                   ia >= 0 &&
                   const_int(b, &ib) &&
                   ib >= 0) {
            // (x / 3) / 4 -> x / 12
            return mutate(div_a->a / make_const(op->type, ia * ib));
        } else if (no_overflow(op->type) &&
                   div_a_a &&
                   add_a &&
                   const_int(div_a_a->b, &ia) &&
                   ia >= 0 &&
                   const_int(add_a->b, &ib) &&
                   const_int(b, &ic) &&
                   ic >= 0) {
            // (x / ia + ib) / ic -> (x + ia*ib) / (ia*ic)
            return mutate((div_a_a->a + make_const(op->type, ia*ib)) / make_const(op->type, ia*ic));
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(mul_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia > 0 &&
                   ib > 0 &&
                   (ia % ib == 0 || ib % ia == 0)) {
            if (ia % ib == 0) {
                // (x * 4) / 2 -> x * 2
                return mutate(mul_a->a * make_const(op->type, div_imp(ia, ib)));
            } else {
                // (x * 2) / 4 -> x / 2
                return mutate(mul_a->a / make_const(op->type, div_imp(ib, ia)));
            }
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_a &&
                   const_int(mul_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // (x*4 + y) / 2 -> x*2 + y/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((mul_a_a->a * ratio) + (add_a->b / b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_b &&
                   const_int(mul_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (y + x*4) / 2 -> y/2 + x*2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((add_a->a / b) + (mul_a_b->a * ratio));
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   mul_a_a &&
                   const_int(mul_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // (x*4 - y) / 2 -> x*2 + (-y)/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((mul_a_a->a * ratio) + (-sub_a->b) / b);
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   mul_a_b &&
                   const_int(mul_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (y - x*4) / 2 -> y/2 - x*2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((sub_a->a / b) - (mul_a_b->a * ratio));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_a_a &&
                   mul_a_a_a &&
                   const_int(mul_a_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // Pull terms that are a multiple of the divisor out
            // ((x*4 + y) + z) / 2 -> x*2 + (y + z)/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((mul_a_a_a->a * ratio) + (add_a_a->b  + add_a->b) / b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   sub_a_a &&
                   mul_a_a_a &&
                   const_int(mul_a_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // ((x*4 - y) + z) / 2 -> x*2 + (z - y)/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((mul_a_a_a->a * ratio) + (add_a->b - sub_a_a->b) / b);
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   add_a_a &&
                   mul_a_a_a &&
                   const_int(mul_a_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // ((x*4 + y) - z) / 2 -> x*2 + (y - z)/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((mul_a_a_a->a * ratio) + (add_a_a->b - sub_a->b) / b);
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   sub_a_a &&
                   mul_a_a_a &&
                   const_int(mul_a_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // ((x*4 - y) - z) / 2 -> x*2 + (0 - y - z)/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((mul_a_a_a->a * ratio) + (- sub_a_a->b - sub_a->b) / b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_a_b &&
                   mul_a_b_a &&
                   const_int(mul_a_b_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (x + (y*4 + z)) / 2 -> y*2 + (x + z)/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((mul_a_b_a->a * ratio) + (add_a->a + add_a_b->b) / b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   sub_a_b &&
                   mul_a_b_a &&
                   const_int(mul_a_b_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (x + (y*4 - z)) / 2 -> y*2 + (x - z)/2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((mul_a_b_a->a * ratio) + (add_a->a - sub_a_b->b) / b);
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   add_a_b &&
                   mul_a_b_a &&
                   const_int(mul_a_b_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (x - (y*4 + z)) / 2 -> (x - z)/2 - y*2
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((sub_a->a - add_a_b->b) / b - (mul_a_b_a->a * ratio));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   sub_a_b &&
                   mul_a_b_b &&
                   const_int(mul_a_b_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (x - (z*4 - y)) / 2 -> (x + (y - z*4)) / 2  -- by a rule from Sub
            // (x + (y - z*4)) / 2 -> (x + y)/2 - z*2  -- by this rule
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((add_a->a + sub_a_b->a) / b - (mul_a_b_b->a * ratio));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib > 0 &&
                   (ia % ib == 0)) {
            // (y + 8) / 2 -> y/2 + 4
            Expr ratio = make_const(op->type, div_imp(ia, ib));
            return mutate((add_a->a / b) + ratio);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ib) &&
                   mul_a_a &&
                   const_int(mul_a_a->b, &ia) &&
                   const_int(b, &ic) &&
                   ic > 0 &&
                   (id = gcd(ia, ic)) != 1) {
            // In expressions of the form (x*a + b)/c, we can divide all the constants by gcd(a, c)
            // E.g. (y*12 + 5)/9 = (y*4 + 2)/3
            ia = div_imp(ia, id);
            ib = div_imp(ib, id);
            ic = div_imp(ic, id);
            return mutate((mul_a_a->a * make_const(op->type, ia) + make_const(op->type, ib)) / make_const(op->type, ic));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   equal(add_a->a, b)) {
            // (x + y)/x -> y/x + 1
            return mutate(add_a->b/b + make_one(op->type));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   equal(add_a->b, b)) {
            // (y + x)/x -> y/x + 1
            return mutate(add_a->a/b + make_one(op->type));
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   !is_zero(b) &&
                   equal(sub_a->a, b)) {
            // (x - y)/x -> (-y)/x + 1
            return mutate((make_zero(op->type) - sub_a->b)/b + make_one(op->type));
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   equal(sub_a->b, b)) {
            // (y - x)/x -> y/x - 1
            return mutate(sub_a->a/b + make_const(op->type, -1));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_a_a &&
                   equal(add_a_a->a, b)) {
            // ((x + y) + z)/x -> ((y + z) + x)/x -> (y+z)/x + 1
            return mutate((add_a_a->b + add_a->b)/b + make_one(op->type));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_a_a &&
                   equal(add_a_a->b, b)) {
            // ((y + x) + z)/x -> ((y + z) + x)/x -> (y+z)/x + 1
            return mutate((add_a_a->a + add_a->b)/b + make_one(op->type));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_a_b &&
                   equal(add_a_b->b, b)) {
            // (y + (z + x))/x -> ((y + z) + x)/x -> (y+z)/x + 1
            return mutate((add_a->a + add_a_b->a)/b + make_one(op->type));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_a_b &&
                   equal(add_a_b->a, b)) {
            // (y + (x + z))/x -> ((y + z) + x)/x -> (y+z)/x + 1
            return mutate((add_a->a + add_a_b->b)/b + make_one(op->type));
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   equal(mul_a->b, b)) {
            // (x*y)/y
            return mul_a->a;
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   equal(mul_a->a, b)) {
            // (y*x)/y
            return mul_a->b;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_a &&
                   equal(mul_a_a->b, b)) {
            // (x*a + y) / a -> x + y/a
            return mutate(mul_a_a->a + (add_a->b / b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_a &&
                   equal(mul_a_a->a, b)) {
            // (a*x + y) / a -> x + y/a
            return mutate(mul_a_a->b + (add_a->b / b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_b &&
                   equal(mul_a_b->b, b)) {
            // (y + x*a) / a -> y/a + x
            return mutate((add_a->a / b) + mul_a_b->a);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_b &&
                   equal(mul_a_b->a, b)) {
            // (y + a*x) / a -> y/a + x
            return mutate((add_a->a / b) + mul_a_b->b);

        } else if (no_overflow(op->type) &&
                   sub_a &&
                   mul_a_a &&
                   equal(mul_a_a->b, b)) {
            // (x*a - y) / a -> x + (-y)/a
            return mutate(mul_a_a->a + ((make_zero(op->type) - sub_a->b) / b));
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   mul_a_a &&
                   equal(mul_a_a->a, b)) {
            // (a*x - y) / a -> x + (-y)/a
            return mutate(mul_a_a->b + ((make_zero(op->type) - sub_a->b) / b));
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   mul_a_b &&
                   equal(mul_a_b->b, b)) {
            // (y - x*a) / a -> y/a - x
            return mutate((sub_a->a / b) - mul_a_b->a);
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   mul_a_b &&
                   equal(mul_a_b->a, b)) {
            // (y - a*x) / a -> y/a - x
            return mutate((sub_a->a / b) - mul_a_b->b);

        } else if (b.type().is_float() && is_simple_const(b)) {
            // Convert const float division to multiplication
            // x / 2 -> x * 0.5
            return mutate(a * (make_one(b.type()) / b));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Div::make(a, b);
        }
    }

    Expr visit(const Mod *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        int64_t ia = 0, ib = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Mul *mul_a = a.as<Mul>();
        const Add *add_a = a.as<Add>();
        const Mul *mul_a_a = add_a ? add_a->a.as<Mul>() : nullptr;
        const Mul *mul_a_b = add_a ? add_a->b.as<Mul>() : nullptr;
        const Ramp *ramp_a = a.as<Ramp>();

        // If the RHS is a constant, do modulus remainder analysis on the LHS
        ModulusRemainder mod_rem(0, 1);

        if (const_int(b, &ib) &&
            ib &&
            no_overflow_scalar_int(op->type)) {

            // If the LHS is bounded, we can possibly bail out early
            int64_t a_min, a_max;
            if (const_int_bounds(a, &a_min, &a_max) &&
                a_max < ib && a_min >= 0) {
                return a;
            }

            mod_rem = modulus_remainder(a, alignment_info);
        }

        // If the RHS is a constant and the LHS is a ramp, do modulus
        // remainder analysis on the base.
        if (broadcast_b &&
            const_int(broadcast_b->value, &ib) &&
            ib &&
            ramp_a &&
            no_overflow_scalar_int(ramp_a->base.type())) {
            mod_rem = modulus_remainder(ramp_a->base, alignment_info);
        }

        if (is_zero(b) && !op->type.is_float()) {
            return indeterminate_expression_error(op->type);
        } else if (is_one(b) && !op->type.is_float()) {
            return make_zero(op->type);
        } else if (is_zero(a)) {
            return a;
        } else if (const_int(a, &ia) && const_int(b, &ib)) {
            return IntImm::make(op->type, mod_imp(ia, ib));
        } else if (const_uint(a, &ua) && const_uint(b, &ub)) {
            return UIntImm::make(op->type, ua % ub);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            return FloatImm::make(op->type, mod_imp(fa, fb));
        } else if (broadcast_a && broadcast_b) {
            return mutate(Broadcast::make(Mod::make(broadcast_a->value, broadcast_b->value), broadcast_a->lanes));
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(b, &ib) &&
                   ib &&
                   const_int(mul_a->b, &ia) &&
                   (ia % ib == 0)) {
            // (x * (b*a)) % b -> 0
            return make_zero(op->type);
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(b, &ib) &&
                   ib &&
                   const_int(mul_a->b, &ia) &&
                   ia > 0 &&
                   (ib % ia == 0)) {
            // (x * a) % (a * b) -> (x % b) * a
            Expr ratio = make_const(a.type(), div_imp(ib, ia));
            return mutate((mul_a->a % ratio) * mul_a->b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_a &&
                   const_int(mul_a_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib &&
                   (ia % ib == 0)) {
            // (x * (b*a) + y) % b -> (y % b)
            return mutate(add_a->b % b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ib &&
                   (ia % ib == 0)) {
            // (y + (b*a)) % b -> (y % b)
            return mutate(add_a->a % b);
        } else if (no_overflow(op->type) &&
                   add_a &&
                   mul_a_b &&
                   const_int(mul_a_b->b, &ia) &&
                   const_int(b, &ib) &&
                   ib &&
                   (ia % ib == 0)) {
            // (y + x * (b*a)) % b -> (y % b)
            return mutate(add_a->a % b);
        } else if (no_overflow_scalar_int(op->type) &&
                   const_int(b, &ib) &&
                   ib &&
                   mod_rem.modulus % ib == 0) {
            // ((a*b)*x + c) % a -> c % a
            return make_const(op->type, mod_imp((int64_t)mod_rem.remainder, ib));
        } else if (no_overflow(op->type) &&
                   ramp_a &&
                   const_int(ramp_a->stride, &ia) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib &&
                   ia % ib == 0) {
            // ramp(x, 4, w) % broadcast(2, w)
            return mutate(Broadcast::make(ramp_a->base % broadcast_b->value, ramp_a->lanes));
        } else if (ramp_a &&
                   no_overflow_scalar_int(ramp_a->base.type()) &&
                   const_int(ramp_a->stride, &ia) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib != 0 &&
                   mod_rem.modulus % ib == 0 &&
                   div_imp((int64_t)mod_rem.remainder, ib) == div_imp(mod_rem.remainder + (ramp_a->lanes-1)*ia, ib)) {
            // ramp(k*z + x, y, w) % z = ramp(x, y, w) if x/z == (x + (w-1)*y)/z
            Expr new_base = make_const(ramp_a->base.type(), mod_imp((int64_t)mod_rem.remainder, ib));
            return mutate(Ramp::make(new_base, ramp_a->stride, ramp_a->lanes));
        } else if (ramp_a &&
                   no_overflow_scalar_int(ramp_a->base.type()) &&
                   const_int(ramp_a->stride, &ia) &&
                   !is_const(ramp_a->base) &&
                   broadcast_b &&
                   const_int(broadcast_b->value, &ib) &&
                   ib != 0 &&
                   mod_rem.modulus % ib == 0) {
            // ramp(k*z + x, y, w) % z = ramp(x, y, w) % z
            Type t = ramp_a->base.type();
            Expr new_base = make_const(t, mod_imp((int64_t)mod_rem.remainder, ib));
            return mutate(Ramp::make(new_base, ramp_a->stride, ramp_a->lanes) % b);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Mod::make(a, b);
        }
    }

    Expr visit(const Min *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        // Move constants to the right to cut down on number of cases to check
        if (is_simple_const(a) && !is_simple_const(b)) {
            std::swap(a, b);
        } else if (a.as<Broadcast>() && !b.as<Broadcast>()) {
            std::swap(a, b);
        } else if (!a.as<Max>() && b.as<Max>()) {
            std::swap(a, b);
        }

        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;
        int64_t a_min, a_max, b_min, b_max;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Ramp *ramp_a = a.as<Ramp>();
        const Add *add_a = a.as<Add>();
        const Add *add_a_a = add_a ? add_a->a.as<Add>() : nullptr;
        const Add *add_a_b = add_a ? add_a->b.as<Add>() : nullptr;
        const Add *add_b = b.as<Add>();
        const Add *add_b_a = add_b ? add_b->a.as<Add>() : nullptr;
        const Add *add_b_b = add_b ? add_b->b.as<Add>() : nullptr;
        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Min *min_a_a = min_a ? min_a->a.as<Min>() : nullptr;
        const Min *min_a_a_a = min_a_a ? min_a_a->a.as<Min>() : nullptr;
        const Min *min_a_a_a_a = min_a_a_a ? min_a_a_a->a.as<Min>() : nullptr;
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();
        const Shuffle *shuffle_a = a.as<Shuffle>();
        const Shuffle *shuffle_b = b.as<Shuffle>();
        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();
        const Broadcast *broadcast_a_b = min_a ? min_a->b.as<Broadcast>() : nullptr;

        min_a_a = max_a ? max_a->a.as<Min>() : min_a_a;

        // Detect if the lhs or rhs is a rounding-up operation
        int64_t a_round_up_factor = 0, b_round_up_factor = 0;
        Expr a_round_up = is_round_up(a, &a_round_up_factor);
        Expr b_round_up = is_round_up(b, &b_round_up_factor);

        int64_t ramp_min, ramp_max;

        if (equal(a, b)) {
            return a;
        } else if (const_int(a, &ia) &&
                   const_int(b, &ib)) {
            return IntImm::make(op->type, std::min(ia, ib));
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            return UIntImm::make(op->type, std::min(ua, ub));
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            return FloatImm::make(op->type, std::min(fa, fb));
        } else if (const_int(b, &ib) &&
                   b.type().is_max(ib)) {
            // Compute minimum of expression of type and maximum of type --> expression
            return a;
        } else if (const_int(b, &ib) &&
                   b.type().is_min(ib)) {
            // Compute minimum of expression of type and minimum of type --> min of type
            return b;
        } else if (const_uint(b, &ub) &&
                   b.type().is_max(ub)) {
            // Compute minimum of expression of type and maximum of type --> expression
            return a;
        } else if (op->type.is_uint() &&
                   is_zero(b)) {
            // Compute minimum of expression of type and minimum of type --> min of type
            return b;
        } else if (broadcast_a &&
                   broadcast_b) {
            return mutate(Broadcast::make(Min::make(broadcast_a->value, broadcast_b->value), broadcast_a->lanes));
        } else if (const_int_bounds(a, &a_min, &a_max) &&
                   const_int_bounds(b, &b_min, &b_max)) {
            if (a_min >= b_max) {
                return b;
            } else if (b_min >= a_max) {
                return a;
            }
        } else if (no_overflow(op->type) &&
                   ramp_a &&
                   broadcast_b &&
                   const_int_bounds(ramp_a, &ramp_min, &ramp_max) &&
                   const_int(broadcast_b->value, &ic)) {
            // min(ramp(a, b, n), broadcast(c, n))
            if (ramp_min <= ic && ramp_max <= ic) {
                // ramp dominates
                return a;
            } if (ramp_min >= ic && ramp_max >= ic) {
                // broadcast dominates
                return b;
            }
        }

        if (no_overflow(op->type) &&
            add_a &&
            const_int(add_a->b, &ia) &&
            add_b &&
            const_int(add_b->b, &ib) &&
            equal(add_a->a, add_b->a)) {
            // min(x + 3, x - 2) -> x - 2
            if (ia > ib) {
                return b;
            } else {
                return a;
            }
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ia) &&
                   equal(add_a->a, b)) {
            // min(x + 5, x) -> x
            if (ia > 0) {
                return b;
            } else {
                return a;
            }
        } else if (no_overflow(op->type) &&
                   add_b &&
                   const_int(add_b->b, &ib) &&
                   equal(add_b->a, a)) {
            // min(x, x + 5) -> x
            if (ib > 0) {
                return a;
            } else {
                return b;
            }
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   sub_b &&
                   equal(sub_a->b, sub_b->b) &&
                   const_int(sub_a->a, &ia) &&
                   const_int(sub_b->a, &ib)) {
            // min (100-x, 101-x) -> 100-x
            if (ia < ib) {
                return a;
            } else {
                return b;
            }
        } else if (a_round_up.defined() &&
                   equal(a_round_up, b)) {
            // min(((a + 3)/4)*4, a) -> a
            return b;
        } else if (a_round_up.defined() &&
                   max_b &&
                   equal(a_round_up, max_b->a) &&
                   is_const(max_b->b, a_round_up_factor)) {
            // min(((a + 3)/4)*4, max(a, 4)) -> max(a, 4)
            return b;
        } else if (b_round_up.defined() &&
                   equal(b_round_up, a)) {
            // min(a, ((a + 3)/4)*4) -> a
            return a;
        } else if (b_round_up.defined() &&
                   max_a &&
                   equal(b_round_up, max_a->a) &&
                   is_const(max_a->b, b_round_up_factor)) {
            // min(max(a, 4), ((a + 3)/4)*4) -> max(a, 4)
            return a;
        } else if (max_a &&
                   min_b &&
                   equal(max_a->a, min_b->a) &&
                   equal(max_a->b, min_b->b)) {
            // min(max(x, y), min(x, y)) -> min(x, y)
            return mutate(min(max_a->a, max_a->b));
        } else if (max_a &&
                   min_b &&
                   equal(max_a->a, min_b->b) &&
                   equal(max_a->b, min_b->a)) {
            // min(max(x, y), min(y, x)) -> min(x, y)
            return mutate(min(max_a->a, max_a->b));
        } else if (max_a &&
                   (equal(max_a->a, b) || equal(max_a->b, b))) {
            // min(max(x, y), x) -> x
            // min(max(x, y), y) -> y
            return b;
        } else if (min_a &&
                   (equal(min_a->b, b) || equal(min_a->a, b))) {
            // min(min(x, y), y) -> min(x, y)
            return a;
        } else if (min_b &&
                   (equal(min_b->b, a) || equal(min_b->a, a))) {
            // min(y, min(x, y)) -> min(x, y)
            return b;
        } else if (min_a &&
                   broadcast_a_b &&
                   broadcast_b ) {
            // min(min(x, broadcast(y, n)), broadcast(z, n))) -> min(x, broadcast(min(y, z), n))
            return mutate(Min::make(min_a->a, Broadcast::make(Min::make(broadcast_a_b->value, broadcast_b->value), broadcast_b->lanes)));
        } else if (min_a &&
                   min_a_a &&
                   equal(min_a_a->b, b)) {
            // min(min(min(x, y), z), y) -> min(min(x, y), z)
            return a;
        } else if (min_a &&
                   min_a_a_a &&
                   equal(min_a_a_a->b, b)) {
            // min(min(min(min(x, y), z), w), y) -> min(min(min(x, y), z), w)
            return a;
        } else if (min_a &&
                   min_a_a_a_a &&
                   equal(min_a_a_a_a->b, b)) {
            // min(min(min(min(min(x, y), z), w), l), y) -> min(min(min(min(x, y), z), w), l)
            return a;
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->a)) {
            // Distributive law for min/max
            // min(max(x, y), max(x, z)) -> max(min(y, z), x)
            return mutate(Max::make(Min::make(max_a->b, max_b->b), max_a->a));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->b)) {
            // min(max(x, y), max(z, x)) -> max(min(y, z), x)
            return mutate(Max::make(Min::make(max_a->b, max_b->a), max_a->a));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->b, max_b->a)) {
            // min(max(y, x), max(x, z)) -> max(min(y, z), x)
            return mutate(Max::make(Min::make(max_a->a, max_b->b), max_a->b));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->b, max_b->b)) {
            // min(max(y, x), max(z, x)) -> max(min(y, z), x)
            return mutate(Max::make(Min::make(max_a->a, max_b->a), max_a->b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->a)) {
            // min(min(x, y), min(x, z)) -> min(min(y, z), x)
            return mutate(Min::make(Min::make(min_a->b, min_b->b), min_a->a));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->b)) {
            // min(min(x, y), min(z, x)) -> min(min(y, z), x)
            return mutate(Min::make(Min::make(min_a->b, min_b->a), min_a->a));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->b, min_b->a)) {
            // min(min(y, x), min(x, z)) -> min(min(y, z), x)
            return mutate(Min::make(Min::make(min_a->a, min_b->b), min_a->b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->b, min_b->b)) {
            // min(min(y, x), min(z, x)) -> min(min(y, z), x)
            return mutate(Min::make(Min::make(min_a->a, min_b->a), min_a->b));
        } else if (max_a &&
                   min_a_a &&
                   equal(min_a_a->b, b)) {
            // min(max(min(x, y), z), y) -> min(max(x, z), y)
            return mutate(min(max(min_a_a->a, max_a->b), b));
        } else if (max_a &&
                   min_a_a &&
                   equal(min_a_a->a, b)) {
            // min(max(min(y, x), z), y) -> min(max(x, z), y)
            return mutate(min(max(min_a_a->b, max_a->b), b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->b, add_b->b)) {
            // Distributive law for addition
            // min(a + b, c + b) -> min(a, c) + b
            return mutate(min(add_a->a, add_b->a)) + add_a->b;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->a, add_b->a)) {
            // min(b + a, b + c) -> min(a, c) + b
            return mutate(min(add_a->b, add_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->a, add_b->b)) {
            // min(b + a, c + b) -> min(a, c) + b
            return mutate(min(add_a->b, add_b->a)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->b, add_b->a)) {
            // min(a + b, b + c) -> min(a, c) + b
            return mutate(min(add_a->a, add_b->b)) + add_a->b;
        } else if (no_overflow(op->type) &&
                   add_a_a &&
                   add_b &&
                   equal(add_a_a->a, add_b->a)) {
            // min((a + b) + c, a + d) -> min(b + c, d) + a
            return mutate(min(add_a_a->b + add_a->b, add_b->b)) + add_b->a;
        } else if (no_overflow(op->type) &&
                   add_a_a &&
                   add_b &&
                   equal(add_a_a->b, add_b->a)) {
            // min((b + a) + c, a + d) -> min(b + c, d) + a
            return mutate(min(add_a_a->a + add_a->b, add_b->b)) + add_b->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b_a &&
                   equal(add_a->a, add_b_a->a)) {
            // min(a + d, (a + b) + c) -> min(d, b + c) + a
            return mutate(min(add_a->b, add_b_a->b + add_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b_a &&
                   equal(add_a->a, add_b_a->b)) {
            // min(a + d, (b + a) + c) -> min(d, b + c) + a
            return mutate(min(add_a->b, add_b_a->a + add_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a_b &&
                   add_b &&
                   equal(add_a_b->a, add_b->a)) {
            // min(a + (b + c), b + d) -> min(a + c, d) + b
            return mutate(min(add_a->a + add_a_b->b, add_b->b)) + add_b->a;
        } else if (no_overflow(op->type) &&
                   add_a_b &&
                   add_b &&
                   equal(add_a_b->b, add_b->a)) {
            // min(a + (c + b), b + d) -> min(a + c, d) + b
            return mutate(min(add_a->a + add_a_b->a, add_b->b)) + add_b->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b_b &&
                   equal(add_a->a, add_b_b->a)) {
            // min(b + d, a + (b + c)) -> min(d, a + c) + b
            return mutate(min(add_a->b, add_b->a + add_b_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b_b &&
                   equal(add_a->a, add_b_b->b)) {
            // min(b + d, a + (c + b)) -> min(d, a + c) + b
            return mutate(min(add_a->b, add_b->a + add_b_b->a)) + add_a->a;
        } else if (min_a &&
                   is_simple_const(min_a->b)) {
            if (is_simple_const(b)) {
                // min(min(x, 4), 5) -> min(x, 4)
                return Min::make(min_a->a, mutate(Min::make(b, min_a->b)));
            } else {
                // min(min(x, 4), y) -> min(min(x, y), 4)
                return mutate(Min::make(Min::make(min_a->a, b), min_a->b));
            }
        } else if (no_overflow(op->type) &&
                   div_a &&
                   div_b &&
                   const_int(div_a->b, &ia) &&
                   ia &&
                   const_int(div_b->b, &ib) &&
                   (ia == ib)) {
            // min(a / 4, b / 4) -> min(a, b) / 4
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                return mutate(min(div_a->a, div_b->a) / factor);
            } else {
                return mutate(max(div_a->a, div_b->a) / factor);
            }
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   (ia == ib)) {
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                return mutate(min(mul_a->a, mul_b->a) * factor);
            } else {
                return mutate(max(mul_a->a, mul_b->a) * factor);
            }
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(mul_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia &&
                   (ib % ia == 0)) {
            // min(x*8, 24) -> min(x, 3)*8
            Expr ratio  = make_const(op->type, ib / ia);
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                return mutate(min(mul_a->a, ratio) * factor);
            } else {
                return mutate(max(mul_a->a, ratio) * factor);
            }
        } else if (call_a &&
                   call_a->is_intrinsic(Call::likely) &&
                   equal(call_a->args[0], b)) {
            // min(likely(b), b) -> likely(b)
            return a;
        } else if (call_b &&
                   call_b->is_intrinsic(Call::likely) &&
                   equal(call_b->args[0], a)) {
            // min(a, likely(a)) -> likely(a)
            return b;
        } else if (shuffle_a && shuffle_b &&
                   shuffle_a->is_slice() &&
                   shuffle_b->is_slice()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return hoist_slice_vector<Min>(op);
            } else {
                return hoist_slice_vector<Min>(min(a, b));
            }
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   is_const(sub_a->a) &&
                   is_const(b)) {
            // min(8 - x, 3) -> 8 - max(x, 5)
            return mutate(sub_a->a - max(sub_a->b, sub_a->a - b));
        } else if (select_a &&
                   select_b &&
                   equal(select_a->condition, select_b->condition)) {
            return mutate(select(select_a->condition,
                                 min(select_a->true_value, select_b->true_value),
                                 min(select_a->false_value, select_b->false_value)));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Min::make(a, b);
        }
    }

    Expr visit(const Max *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        // Move constants to the right to cut down on number of cases to check
        if (is_simple_const(a) && !is_simple_const(b)) {
            std::swap(a, b);
        } else if (a.as<Broadcast>() && !b.as<Broadcast>()) {
            std::swap(a, b);
        } else if (!a.as<Min>() && b.as<Min>()) {
            std::swap(a, b);
        }

        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0.0f, fb = 0.0f;
        int64_t a_min, a_max, b_min, b_max;
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Ramp *ramp_a = a.as<Ramp>();
        const Add *add_a = a.as<Add>();
        const Add *add_a_a = add_a ? add_a->a.as<Add>() : nullptr;
        const Add *add_a_b = add_a ? add_a->b.as<Add>() : nullptr;
        const Add *add_b = b.as<Add>();
        const Add *add_b_a = add_b ? add_b->a.as<Add>() : nullptr;
        const Add *add_b_b = add_b ? add_b->b.as<Add>() : nullptr;
        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Max *max_a_a = max_a ? max_a->a.as<Max>() : nullptr;
        const Max *max_a_a_a = max_a_a ? max_a_a->a.as<Max>() : nullptr;
        const Max *max_a_a_a_a = max_a_a_a ? max_a_a_a->a.as<Max>() : nullptr;
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();
        const Shuffle *shuffle_a = a.as<Shuffle>();
        const Shuffle *shuffle_b = b.as<Shuffle>();
        const Select *select_a = a.as<Select>();
        const Select *select_b = b.as<Select>();
        const Broadcast *broadcast_a_b = max_a ? max_a->b.as<Broadcast>() : nullptr;

        max_a_a = min_a ? min_a->a.as<Max>() : max_a_a;

        int64_t ramp_min, ramp_max;

        if (equal(a, b)) {
            return a;
        } else if (const_int(a, &ia) &&
                   const_int(b, &ib)) {
            return IntImm::make(op->type, std::max(ia, ib));
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            return UIntImm::make(op->type, std::max(ua, ub));
        } else if (const_float(a, &fa) &&
                   const_float(b, &fb)) {
            return FloatImm::make(op->type, std::max(fa, fb));
        } else if (const_int(b, &ib) &&
                   b.type().is_min(ib)) {
            // Compute maximum of expression of type and minimum of type --> expression
            return a;
        } else if (const_int(b, &ib) &&
                   b.type().is_max(ib)) {
            // Compute maximum of expression of type and maximum of type --> max of type
            return b;
        } else if (op->type.is_uint() &&
                   is_zero(b)) {
            // Compute maximum of expression of type and minimum of type --> expression
            return a;
        } else if (const_uint(b, &ub) &&
                   b.type().is_max(ub)) {
            // Compute maximum of expression of type and maximum of type --> max of type
            return b;
        } else if (broadcast_a && broadcast_b) {
            return mutate(Broadcast::make(Max::make(broadcast_a->value, broadcast_b->value), broadcast_a->lanes));
        } else if (const_int_bounds(a, &a_min, &a_max) &&
                   const_int_bounds(b, &b_min, &b_max)) {
            if (a_min >= b_max) {
                return a;
            } else if (b_min >= a_max) {
                return b;
            }
        } else if (no_overflow(op->type) &&
                   ramp_a &&
                   broadcast_b &&
                   const_int_bounds(ramp_a, &ramp_min, &ramp_max) &&
                   const_int(broadcast_b->value, &ic)) {
            // max(ramp(a, b, n), broadcast(c, n))
            if (ramp_min >= ic && ramp_max >= ic) {
                // ramp dominates
                return a;
            }
            if (ramp_min <= ic && ramp_max <= ic) {
                // broadcast dominates
                return b;
            }
        }

        if (no_overflow(op->type) &&
            add_a &&
            const_int(add_a->b, &ia) &&
            add_b &&
            const_int(add_b->b, &ib) &&
            equal(add_a->a, add_b->a)) {
            // max(x + 3, x - 2) -> x - 2
            if (ia > ib) {
                return a;
            } else {
                return b;
            }
        } else if (no_overflow(op->type) &&
                   add_a &&
                   const_int(add_a->b, &ia) &&
                   equal(add_a->a, b)) {
            // max(x + 5, x)
            if (ia > 0) {
                return a;
            } else {
                return b;
            }
        } else if (no_overflow(op->type) &&
                   add_b &&
                   const_int(add_b->b, &ib) &&
                   equal(add_b->a, a)) {
            // max(x, x + 5)
            if (ib > 0) {
                return b;
            } else {
                return a;
            }
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   sub_b &&
                   equal(sub_a->b, sub_b->b) &&
                   const_int(sub_a->a, &ia) &&
                   const_int(sub_b->a, &ib)) {
            // max (100-x, 101-x) -> 101-x
            if (ia > ib) {
                return a;
            } else {
                return b;
            }
        } else if (min_a &&
                   max_b &&
                   equal(min_a->a, max_b->a) &&
                   equal(min_a->b, max_b->b)) {
            // max(min(x, y), max(x, y)) -> max(x, y)
            return mutate(max(min_a->a, min_a->b));
        } else if (min_a &&
                   max_b &&
                   equal(min_a->a, max_b->b) &&
                   equal(min_a->b, max_b->a)) {
            // max(min(x, y), max(y, x)) -> max(x, y)
            return mutate(max(min_a->a, min_a->b));
        } else if (min_a &&
                   (equal(min_a->a, b) || equal(min_a->b, b))) {
            // max(min(x, y), x) -> x
            // max(min(x, y), y) -> y
            return b;
        } else if (max_a &&
                   (equal(max_a->b, b) || equal(max_a->a, b))) {
            // max(max(x, y), y) -> max(x, y)
            return a;
        } else if (max_b &&
                   (equal(max_b->b, a) || equal(max_b->a, a))) {
            // max(y, max(x, y)) -> max(x, y)
            return b;
        } else if (max_a &&
                   broadcast_a_b &&
                   broadcast_b ) {
            // max(max(x, broadcast(y, n)), broadcast(z, n))) -> max(x, broadcast(max(y, z), n))
            return mutate(Max::make(max_a->a, Broadcast::make(Max::make(broadcast_a_b->value, broadcast_b->value), broadcast_b->lanes)));
        } else if (max_a &&
                   max_a_a &&
                   equal(max_a_a->b, b)) {
            // max(max(max(x, y), z), y) -> max(max(x, y), z)
            return a;
        } else if (max_a_a_a &&
                   equal(max_a_a_a->b, b)) {
            // max(max(max(max(x, y), z), w), y) -> max(max(max(x, y), z), w)
            return a;
        } else if (max_a_a_a_a &&
                   equal(max_a_a_a_a->b, b)) {
            // max(max(max(max(max(x, y), z), w), l), y) -> max(max(max(max(x, y), z), w), l)
            return a;
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->a)) {
            // Distributive law for min/max
            // max(max(x, y), max(x, z)) -> max(max(y, z), x)
            return mutate(Max::make(Max::make(max_a->b, max_b->b), max_a->a));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->a, max_b->b)) {
            // max(max(x, y), max(z, x)) -> max(max(y, z), x)
            return mutate(Max::make(Max::make(max_a->b, max_b->a), max_a->a));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->b, max_b->a)) {
            // max(max(y, x), max(x, z)) -> max(max(y, z), x)
            return mutate(Max::make(Max::make(max_a->a, max_b->b), max_a->b));
        } else if (max_a &&
                   max_b &&
                   equal(max_a->b, max_b->b)) {
            // max(max(y, x), max(z, x)) -> max(max(y, z), x)
            return mutate(Max::make(Max::make(max_a->a, max_b->a), max_a->b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->a)) {
            // max(min(x, y), min(x, z)) -> min(max(y, z), x)
            return mutate(Min::make(Max::make(min_a->b, min_b->b), min_a->a));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->a, min_b->b)) {
            // max(min(x, y), min(z, x)) -> min(max(y, z), x)
            return mutate(Min::make(Max::make(min_a->b, min_b->a), min_a->a));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->b, min_b->a)) {
            // max(min(y, x), min(x, z)) -> min(max(y, z), x)
            return mutate(Min::make(Max::make(min_a->a, min_b->b), min_a->b));
        } else if (min_a &&
                   min_b &&
                   equal(min_a->b, min_b->b)) {
            // max(min(y, x), min(z, x)) -> min(max(y, z), x)
            return mutate(Min::make(Max::make(min_a->a, min_b->a), min_a->b));
        } else if (min_a &&
                   max_a_a &&
                   equal(max_a_a->b, b)) {
            // max(min(max(x, y), z), y) -> max(min(x, z), y)
            return mutate(max(min(max_a_a->a, min_a->b), b));
        } else if (min_a &&
                   max_a_a &&
                   equal(max_a_a->a, b)) {
            // max(min(max(y, x), z), y) -> max(min(x, z), y)
            return mutate(max(min(max_a_a->b, min_a->b), b));
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->b, add_b->b)) {
            // Distributive law for addition
            // max(a + b, c + b) -> max(a, c) + b
            return mutate(max(add_a->a, add_b->a)) + add_a->b;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->a, add_b->a)) {
            // max(b + a, b + c) -> max(a, c) + b
            return mutate(max(add_a->b, add_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->a, add_b->b)) {
            // max(b + a, c + b) -> max(a, c) + b
            return mutate(max(add_a->b, add_b->a)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b &&
                   equal(add_a->b, add_b->a)) {
            // max(a + b, b + c) -> max(a, c) + b
            return mutate(max(add_a->a, add_b->b)) + add_a->b;
        } else if (no_overflow(op->type) &&
                   add_a_a &&
                   add_b &&
                   equal(add_a_a->a, add_b->a)) {
            // max((a + b) + c, a + d) -> max(b + c, d) + a
            return mutate(max(add_a_a->b + add_a->b, add_b->b)) + add_b->a;
        } else if (no_overflow(op->type) &&
                   add_a_a &&
                   add_b &&
                   equal(add_a_a->b, add_b->a)) {
            // max((b + a) + c, a + d) -> max(b + c, d) + a
            return mutate(max(add_a_a->a + add_a->b, add_b->b)) + add_b->a;
        } else if (no_overflow(op->type) &&
                   add_a_b &&
                   add_b &&
                   equal(add_a_b->a, add_b->a)) {
            // max(a + (b + c), b + d) -> max(a + c, d) + b
            return mutate(max(add_a->a + add_a_b->b, add_b->b)) + add_b->a;
        } else if (no_overflow(op->type) &&
                   add_a_b &&
                   add_b &&
                   equal(add_a_b->b, add_b->a)) {
            // max(a + (c + b), b + d) -> max(a + c, d) + b
            return mutate(max(add_a->a + add_a_b->a, add_b->b)) + add_b->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b_a &&
                   equal(add_a->a, add_b_a->a)) {
            // max(a + d, (a + b) + c) -> max(d, b + c) + a
            return mutate(max(add_a->b, add_b_a->b + add_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b_a &&
                   equal(add_a->a, add_b_a->b)) {
            // max(a + d, (b + a) + c) -> max(d, b + c) + a
            return mutate(max(add_a->b, add_b_a->a + add_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b_b &&
                   equal(add_a->a, add_b_b->a)) {
            // max(b + d, a + (b + c)) -> max(d, a + c) + b
            return mutate(max(add_a->b, add_b->a + add_b_b->b)) + add_a->a;
        } else if (no_overflow(op->type) &&
                   add_a &&
                   add_b_b &&
                   equal(add_a->a, add_b_b->b)) {
            // max(b + d, a + (c + b)) -> max(d, a + c) + b
            return mutate(max(add_a->b, add_b->a + add_b_b->a)) + add_a->a;
        } else if (max_a && is_simple_const(max_a->b)) {
            if (is_simple_const(b)) {
                // max(max(x, 4), 5) -> max(x, 4)
                return Max::make(max_a->a, mutate(Max::make(b, max_a->b)));
            } else {
                // max(max(x, 4), y) -> max(max(x, y), 4)
                return mutate(Max::make(Max::make(max_a->a, b), max_a->b));
            }
        } else if (no_overflow(op->type) &&
                   div_a &&
                   div_b &&
                   const_int(div_a->b, &ia) &&
                   ia &&
                   const_int(div_b->b, &ib) &&
                   (ia == ib)) {
            // max(a / 4, b / 4) -> max(a, b) / 4
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                return mutate(max(div_a->a, div_b->a) / factor);
            } else {
                return mutate(min(div_a->a, div_b->a) / factor);
            }
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   mul_b &&
                   const_int(mul_a->b, &ia) &&
                   const_int(mul_b->b, &ib) &&
                   (ia == ib)) {
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                return mutate(max(mul_a->a, mul_b->a) * factor);
            } else {
                return mutate(min(mul_a->a, mul_b->a) * factor);
            }
        } else if (no_overflow(op->type) &&
                   mul_a &&
                   const_int(mul_a->b, &ia) &&
                   const_int(b, &ib) &&
                   ia &&
                   (ib % ia == 0)) {
            // max(x*8, 24) -> max(x, 3)*8
            Expr ratio = make_const(op->type, ib / ia);
            Expr factor = make_const(op->type, ia);
            if (ia > 0) {
                return mutate(max(mul_a->a, ratio) * factor);
            } else {
                return mutate(min(mul_a->a, ratio) * factor);
            }
        } else if (call_a &&
                   call_a->is_intrinsic(Call::likely) &&
                   equal(call_a->args[0], b)) {
            // max(likely(b), b) -> likely(b)
            return a;
        } else if (call_b &&
                   call_b->is_intrinsic(Call::likely) &&
                   equal(call_b->args[0], a)) {
            // max(a, likely(a)) -> likely(a)
            return b;
        } else if (shuffle_a && shuffle_b &&
                   shuffle_a->is_slice() &&
                   shuffle_b->is_slice()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return hoist_slice_vector<Max>(op);
            } else {
                return hoist_slice_vector<Max>(max(a, b));
            }
        } else if (no_overflow(op->type) &&
                   sub_a &&
                   is_simple_const(sub_a->a) &&
                   is_simple_const(b)) {
            // max(8 - x, 3) -> 8 - min(x, 5)
            return mutate(sub_a->a - min(sub_a->b, sub_a->a - b));
        } else if (select_a &&
                   select_b &&
                   equal(select_a->condition, select_b->condition)) {
            return mutate(select(select_a->condition,
                                 max(select_a->true_value, select_b->true_value),
                                 max(select_a->false_value, select_b->false_value)));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Max::make(a, b);
        }
    }

    Expr visit(const EQ *op) override {
        Expr delta = mutate(op->a - op->b);
        Expr expr;
        if (propagate_indeterminate_expression(delta, op->type, &expr)) {
            return expr;
        }

        const Broadcast *broadcast = delta.as<Broadcast>();
        const Add *add = delta.as<Add>();
        const Sub *sub = delta.as<Sub>();
        const Mul *mul = delta.as<Mul>();
        const Select *sel = delta.as<Select>();

        Expr zero = make_zero(delta.type());

        if (is_zero(delta)) {
            return const_true(op->type.lanes());
        } else if (is_const(delta)) {
            bool t = true;
            bool f = true;
            for (int i = 0; i < delta.type().lanes(); i++) {
                Expr deltai = extract_lane(delta, i);
                if (is_zero(deltai)) {
                    f = false;
                } else {
                    t = false;
                }
            }
            if (t) {
                return const_true(op->type.lanes());
            } else if (f) {
                return const_false(op->type.lanes());
            }
        } else if (no_overflow_scalar_int(delta.type())) {
            // Attempt to disprove using modulus remainder analysis
            ModulusRemainder mod_rem = modulus_remainder(delta, alignment_info);
            if (mod_rem.remainder) {
                return const_false();
            }

            // Attempt to disprove using bounds analysis
            int64_t delta_min, delta_max;
            if (const_int_bounds(delta, &delta_min, &delta_max) &&
                (delta_min > 0 || delta_max < 0)) {
                return const_false();
            }
        }

        if (broadcast) {
            // Push broadcasts outwards
            return Broadcast::make(mutate(broadcast->value ==
                                          make_zero(broadcast->value.type())),
                                   broadcast->lanes);
        } else if (add && is_const(add->b)) {
            // x + const = 0 -> x = -const
            return (add->a == mutate(make_zero(delta.type()) - add->b));
        } else if (sub) {
            if (is_const(sub->a)) {
                // const - x == 0 -> x == const
                return sub->b == sub->a;
            } else if (sub->a.same_as(op->a) && sub->b.same_as(op->b)) {
                return op;
            } else {
                // x - y == 0 -> x == y
                return (sub->a == sub->b);
            }
        } else if (mul &&
                   no_overflow(mul->type)) {
            // Restrict to int32 and greater, because, e.g. 64 * 4 == 0 as a uint8.
            return mutate(mul->a == zero || mul->b == zero);
        } else if (sel && is_zero(sel->true_value)) {
            // select(c, 0, f) == 0 -> c || (f == 0)
            return mutate(sel->condition || (sel->false_value == zero));
        } else if (sel &&
                   (is_positive_const(sel->true_value) || is_negative_const(sel->true_value))) {
            // select(c, 4, f) == 0 -> !c && (f == 0)
            return mutate((!sel->condition) && (sel->false_value == zero));
        } else if (sel && is_zero(sel->false_value)) {
            // select(c, t, 0) == 0 -> !c || (t == 0)
            return mutate((!sel->condition) || (sel->true_value == zero));
        } else if (sel &&
                   (is_positive_const(sel->false_value) || is_negative_const(sel->false_value))) {
            // select(c, t, 4) == 0 -> c && (t == 0)
            return mutate((sel->condition) && (sel->true_value == zero));
        } else {
            return (delta == make_zero(delta.type()));
        }
    }

    Expr visit(const NE *op) override {
        return mutate(Not::make(op->a == op->b));
    }

    Expr visit(const LT *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        int64_t a_min, a_max, b_min, b_max;
        if (const_int_bounds(a, &a_min, &a_max) &&
            const_int_bounds(b, &b_min, &b_max)) {
            if (a_max < b_min) {
                return const_true(op->type.lanes());
            }
            if (a_min >= b_max) {
                return const_false(op->type.lanes());
            }
        }

        Expr delta = mutate(a - b);

        const Ramp *ramp_a = a.as<Ramp>();
        const Ramp *ramp_b = b.as<Ramp>();
        const Ramp *delta_ramp = delta.as<Ramp>();
        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const Add *add_a = a.as<Add>();
        const Add *add_b = b.as<Add>();
        const Sub *sub_a = a.as<Sub>();
        const Sub *sub_b = b.as<Sub>();
        const Mul *mul_a = a.as<Mul>();
        const Mul *mul_b = b.as<Mul>();
        const Div *div_a = a.as<Div>();
        const Div *div_b = b.as<Div>();
        const Min *min_a = a.as<Min>();
        const Min *min_b = b.as<Min>();
        const Max *max_a = a.as<Max>();
        const Max *max_b = b.as<Max>();
        const Div *div_a_a = mul_a ? mul_a->a.as<Div>() : nullptr;
        const Add *add_a_a_a = div_a_a ? div_a_a->a.as<Add>() : nullptr;

        int64_t ia = 0, ib = 0, ic = 0;
        uint64_t ua = 0, ub = 0;

        ModulusRemainder mod_rem(0, 1);
        if (delta_ramp &&
            no_overflow_scalar_int(delta_ramp->base.type())) {
            // Do modulus remainder analysis on the base.
            mod_rem = modulus_remainder(delta_ramp->base, alignment_info);
        }

        // Note that the computation of delta could be incorrect if
        // ia and/or ib are large unsigned integer constants, especially when
        // int is 32 bits on the machine.
        // Explicit comparison is preferred.
        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            return make_bool(ia < ib, op->type.lanes());
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            return make_bool(ua < ub, op->type.lanes());
        } else if (const_int(a, &ia) &&
                   a.type().is_max(ia)) {
            // Comparing maximum of type < expression of type.  This can never be true.
            return const_false(op->type.lanes());
        } else if (const_int(b, &ib) &&
                   b.type().is_min(ib)) {
            // Comparing expression of type < minimum of type.  This can never be true.
            return const_false(op->type.lanes());
        } else if (is_zero(delta) ||
                   (no_overflow(delta.type()) &&
                    is_positive_const(delta))) {
            return const_false(op->type.lanes());
        } else if (no_overflow(delta.type()) &&
                   is_negative_const(delta)) {
            return const_true(op->type.lanes());
        } else if (broadcast_a &&
                   broadcast_b) {
            // Push broadcasts outwards
            return mutate(Broadcast::make(broadcast_a->value < broadcast_b->value, broadcast_a->lanes));
        } else if (no_overflow(delta.type())) {
            if (ramp_a &&
                ramp_b &&
                equal(ramp_a->stride, ramp_b->stride)) {
                // Ramps with matching stride
                Expr bases_lt = (ramp_a->base < ramp_b->base);
                return mutate(Broadcast::make(bases_lt, ramp_a->lanes));
            } else if (add_a &&
                       add_b &&
                       equal(add_a->a, add_b->a)) {
                // Subtract a term from both sides
                return mutate(add_a->b < add_b->b);
            } else if (add_a &&
                       add_b &&
                       equal(add_a->a, add_b->b)) {
                return mutate(add_a->b < add_b->a);
            } else if (add_a &&
                       add_b &&
                       equal(add_a->b, add_b->a)) {
                return mutate(add_a->a < add_b->b);
            } else if (add_a &&
                       add_b &&
                       equal(add_a->b, add_b->b)) {
                return mutate(add_a->a < add_b->a);
            } else if (sub_a &&
                       sub_b &&
                       equal(sub_a->a, sub_b->a)) {
                // Add a term to both sides and negate.
                return mutate(sub_b->b < sub_a->b);
            } else if (sub_a &&
                       sub_b &&
                       equal(sub_a->b, sub_b->b)) {
                return mutate(sub_a->a < sub_b->a);
            } else if (add_a) {
                // Rearrange so that all adds and subs are on the rhs to cut down on further cases
                return mutate(add_a->a < (b - add_a->b));
            } else if (sub_a) {
                return mutate(sub_a->a < (b + sub_a->b));
            } else if (add_b &&
                       equal(add_b->a, a)) {
                // Subtract a term from both sides
                return mutate(make_zero(add_b->b.type()) < add_b->b);
            } else if (add_b &&
                       equal(add_b->b, a)) {
                return mutate(make_zero(add_b->a.type()) < add_b->a);
            } else if (add_b &&
                       is_simple_const(a) &&
                       is_simple_const(add_b->b)) {
                // a < x + b -> (a - b) < x
                return mutate((a - add_b->b) < add_b->a);
            } else if (sub_b &&
                       equal(sub_b->a, a)) {
                // Subtract a term from both sides
                return mutate(sub_b->b < make_zero(sub_b->b.type()));
            } else if (sub_b &&
                       is_const(a) &&
                       is_const(sub_b->a) &&
                       !is_const(sub_b->b)) {
                // (c1 < c2 - x) -> (x < c2 - c1)
                return mutate(sub_b->b < (sub_b->a - a));
            } else if (mul_a &&
                       mul_b &&
                       is_positive_const(mul_a->b) &&
                       is_positive_const(mul_b->b) &&
                       equal(mul_a->b, mul_b->b)) {
                // Divide both sides by a constant
                return mutate(mul_a->a < mul_b->a);
            } else if (mul_a &&
                       is_positive_const(mul_a->b) &&
                       is_const(b)) {
                if (mul_a->type.is_int()) {
                    // (a * c1 < c2) <=> (a < (c2 - 1) / c1 + 1)
                    return mutate(mul_a->a < (((b - 1) / mul_a->b) + 1));
                } else {
                    // (a * c1 < c2) <=> (a < c2 / c1)
                    return mutate(mul_a->a < (b / mul_a->b));
                }
            } else if (mul_b &&
                       is_positive_const(mul_b->b) &&
                       is_simple_const(mul_b->b) &&
                       is_simple_const(a)) {
                // (c1 < b * c2) <=> ((c1 / c2) < b)
                return mutate((a / mul_b->b) < mul_b->a);
            } else if (a.type().is_int() &&
                       div_a &&
                       is_positive_const(div_a->b) &&
                       is_const(b)) {
                // a / c1 < c2 <=> a < c1*c2
                return mutate(div_a->a < (div_a->b * b));
            } else if (a.type().is_int() &&
                       div_b &&
                       is_positive_const(div_b->b) &&
                       is_const(a)) {
                // c1 < b / c2 <=> (c1+1)*c2-1 < b
                Expr one = make_one(a.type());
                return mutate((a + one)*div_b->b - one < div_b->a);
            } else if (min_a) {
                // (min(a, b) < c) <=> (a < c || b < c)
                // See if that would simplify usefully:
                Expr lt_a = mutate(min_a->a < b);
                Expr lt_b = mutate(min_a->b < b);
                if (is_const(lt_a) || is_const(lt_b)) {
                    return mutate(lt_a || lt_b);
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    return op;
                } else {
                    return LT::make(a, b);
                }
            } else if (max_a) {
                // (max(a, b) < c) <=> (a < c && b < c)
                Expr lt_a = mutate(max_a->a < b);
                Expr lt_b = mutate(max_a->b < b);
                if (is_const(lt_a) || is_const(lt_b)) {
                    return mutate(lt_a && lt_b);
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    return op;
                } else {
                    return LT::make(a, b);
                }
            } else if (min_b) {
                // (a < min(b, c)) <=> (a < b && a < c)
                Expr lt_a = mutate(a < min_b->a);
                Expr lt_b = mutate(a < min_b->b);
                if (is_const(lt_a) || is_const(lt_b)) {
                    return mutate(lt_a && lt_b);
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    return op;
                } else {
                    return LT::make(a, b);
                }
            } else if (max_b) {
                // (a < max(b, c)) <=> (a < b || a < c)
                Expr lt_a = mutate(a < max_b->a);
                Expr lt_b = mutate(a < max_b->b);
                if (is_const(lt_a) || is_const(lt_b)) {
                    return mutate(lt_a || lt_b);
                } else if (a.same_as(op->a) && b.same_as(op->b)) {
                    return op;
                } else {
                    return LT::make(a, b);
                }
            } else if (mul_a &&
                       div_a_a &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(div_a_a->a, b)) {
                // subtract (x/c1)*c1 from both sides
                // (x/c1)*c1 < x -> 0 < x % c1
                return mutate(0 < b % make_const(a.type(), ia));
            } else if (mul_a &&
                       div_a_a &&
                       add_b &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(div_a_a->a, add_b->a)) {
                // subtract (x/c1)*c1 from both sides
                // (x/c1)*c1 < x + y -> 0 < x % c1 + y
                return mutate(0 < add_b->a % div_a_a->b + add_b->b);
            } else if (mul_a &&
                       div_a_a &&
                       sub_b &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(div_a_a->a, sub_b->a)) {
                // subtract (x/c1)*c1 from both sides
                // (x/c1)*c1 < x - y -> y < x % c1
                return mutate(sub_b->b < sub_b->a % div_a_a->b);
            } else if (mul_a &&
                       div_a_a &&
                       add_a_a_a &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       const_int(add_a_a_a->b, &ic) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(add_a_a_a->a, b)) {
                // subtract ((x+c2)/c1)*c1 from both sides
                // ((x+c2)/c1)*c1 < x -> c2 < (x+c2) % c1
                return mutate(add_a_a_a->b < div_a_a->a % div_a_a->b);
            } else if (mul_a &&
                       div_a_a &&
                       add_b &&
                       add_a_a_a &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       const_int(add_a_a_a->b, &ic) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(add_a_a_a->a, add_b->a)) {
                // subtract ((x+c2)/c1)*c1 from both sides
                // ((x+c2)/c1)*c1 < x + y -> c2 < (x+c2) % c1 + y
                return mutate(add_a_a_a->b < div_a_a->a % div_a_a->b + add_b->b);
            } else if (mul_a &&
                       div_a_a &&
                       add_a_a_a &&
                       sub_b &&
                       const_int(div_a_a->b, &ia) &&
                       const_int(mul_a->b, &ib) &&
                       const_int(add_a_a_a->b, &ic) &&
                       ia > 0 &&
                       ia == ib &&
                       equal(add_a_a_a->a, sub_b->a)) {
                // subtract ((x+c2)/c1)*c1 from both sides
                // ((x+c2)/c1)*c1 < x - y -> y < (x+c2) % c1 + (-c2)
                return mutate(sub_b->b < div_a_a->a % div_a_a->b + make_const(a.type(), -ic));
            } else if (delta_ramp &&
                       is_positive_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base + delta_ramp->stride*(delta_ramp->lanes - 1) < 0))) {
                return const_true(delta_ramp->lanes);
            } else if (delta_ramp &&
                       is_positive_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base >= 0))) {
                return const_false(delta_ramp->lanes);
            } else if (delta_ramp &&
                       is_negative_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base < 0))) {
                return const_true(delta_ramp->lanes);
            } else if (delta_ramp &&
                       is_negative_const(delta_ramp->stride) &&
                       is_one(mutate(delta_ramp->base + delta_ramp->stride*(delta_ramp->lanes - 1) >= 0))) {
                return const_false(delta_ramp->lanes);
            } else if (delta_ramp && mod_rem.modulus > 0 &&
                       const_int(delta_ramp->stride, &ia) &&
                       0 <= ia * (delta_ramp->lanes - 1) + mod_rem.remainder &&
                       ia * (delta_ramp->lanes - 1) + mod_rem.remainder < mod_rem.modulus) {
                // ramp(x, a, b) < 0 -> broadcast(x < 0, b)
                return Broadcast::make(mutate(LT::make(delta_ramp->base / mod_rem.modulus, 0)), delta_ramp->lanes);
            } else if (a.same_as(op->a) && b.same_as(op->b)) {
                return op;
            } else {
                return LT::make(a, b);
            }
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return LT::make(a, b);
        }
    }

    Expr visit(const LE *op) override {
        return mutate(!(op->b < op->a));
    }

    Expr visit(const GT *op) override {
        return mutate(op->b < op->a);
    }

    Expr visit(const GE *op) override {
        return mutate(!(op->a < op->b));
    }

    Expr visit(const And *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const LE *le_a = a.as<LE>();
        const LE *le_b = b.as<LE>();
        const LT *lt_a = a.as<LT>();
        const LT *lt_b = b.as<LT>();
        const EQ *eq_a = a.as<EQ>();
        const EQ *eq_b = b.as<EQ>();
        const NE *neq_a = a.as<NE>();
        const NE *neq_b = b.as<NE>();
        const Not *not_a = a.as<Not>();
        const Not *not_b = b.as<Not>();
        const Variable *var_a = a.as<Variable>();
        const Variable *var_b = b.as<Variable>();
        int64_t ia = 0, ib = 0;

        if (is_one(a)) {
            return b;
        } else if (is_one(b)) {
            return a;
        } else if (is_zero(a)) {
            return a;
        } else if (is_zero(b)) {
            return b;
        } else if (equal(a, b)) {
            // a && a -> a
            return a;
        } else if (le_a &&
                   le_b &&
                   equal(le_a->a, le_b->a)) {
            // (x <= foo && x <= bar) -> x <= min(foo, bar)
            return mutate(le_a->a <= min(le_a->b, le_b->b));
        } else if (le_a &&
                   le_b &&
                   equal(le_a->b, le_b->b)) {
            // (foo <= x && bar <= x) -> max(foo, bar) <= x
            return mutate(max(le_a->a, le_b->a) <= le_a->b);
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->a, lt_b->a)) {
            // (x < foo && x < bar) -> x < min(foo, bar)
            return mutate(lt_a->a < min(lt_a->b, lt_b->b));
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->b, lt_b->b)) {
            // (foo < x && bar < x) -> max(foo, bar) < x
            return mutate(max(lt_a->a, lt_b->a) < lt_a->b);
        } else if (eq_a &&
                   neq_b &&
                   ((equal(eq_a->a, neq_b->a) && equal(eq_a->b, neq_b->b)) ||
                    (equal(eq_a->a, neq_b->b) && equal(eq_a->b, neq_b->a)))) {
            // a == b && a != b
            return const_false(op->type.lanes());
        } else if (eq_b &&
                   neq_a &&
                   ((equal(eq_b->a, neq_a->a) && equal(eq_b->b, neq_a->b)) ||
                    (equal(eq_b->a, neq_a->b) && equal(eq_b->b, neq_a->a)))) {
            // a != b && a == b
            return const_false(op->type.lanes());
        } else if ((not_a && equal(not_a->a, b)) ||
                   (not_b && equal(not_b->a, a))) {
            // a && !a
            return const_false(op->type.lanes());
        } else if (le_a &&
                   lt_b &&
                   equal(le_a->a, lt_b->b) &&
                   equal(le_a->b, lt_b->a)) {
            // a <= b && b < a
            return const_false(op->type.lanes());
        } else if (lt_a &&
                   le_b &&
                   equal(lt_a->a, le_b->b) &&
                   equal(lt_a->b, le_b->a)) {
            // a < b && b <= a
            return const_false(op->type.lanes());
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->a, lt_b->b) &&
                   const_int(lt_a->b, &ia) &&
                   const_int(lt_b->a, &ib) &&
                   ib + 1 >= ia) {
            // (a < ia && ib < a) where there is no integer a s.t. ib < a < ia
            return const_false(op->type.lanes());
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->b, lt_b->a) &&
                   const_int(lt_b->b, &ia) &&
                   const_int(lt_a->a, &ib) &&
                   ib + 1 >= ia) {
            // (ib < a && a < ia) where there is no integer a s.t. ib < a < ia
            return const_false(op->type.lanes());

        } else if (le_a &&
                   lt_b &&
                   equal(le_a->a, lt_b->b) &&
                   const_int(le_a->b, &ia) &&
                   const_int(lt_b->a, &ib) &&
                   ib >= ia) {
            // (a <= ia && ib < a) where there is no integer a s.t. ib < a <= ia
            return const_false(op->type.lanes());
        } else if (le_a &&
                   lt_b &&
                   equal(le_a->b, lt_b->a) &&
                   const_int(lt_b->b, &ia) &&
                   const_int(le_a->a, &ib) &&
                   ib >= ia) {
            // (ib <= a && a < ia) where there is no integer a s.t. ib < a <= ia
            return const_false(op->type.lanes());

        } else if (lt_a &&
                   le_b &&
                   equal(lt_a->a, le_b->b) &&
                   const_int(lt_a->b, &ia) &&
                   const_int(le_b->a, &ib) &&
                   ib >= ia) {
            // (a < ia && ib <= a) where there is no integer a s.t. ib <= a < ia
            return const_false(op->type.lanes());
        } else if (lt_a &&
                   le_b &&
                   equal(lt_a->b, le_b->a) &&
                   const_int(le_b->b, &ia) &&
                   const_int(lt_a->a, &ib) &&
                   ib >= ia) {
            // (ib < a && a <= ia) where there is no integer a s.t. ib <= a < ia
            return const_false(op->type.lanes());

        } else if (le_a &&
                   le_b &&
                   equal(le_a->a, le_b->b) &&
                   const_int(le_a->b, &ia) &&
                   const_int(le_b->a, &ib) &&
                   ib > ia) {
            // (a <= ia && ib <= a) where there is no integer a s.t. ib <= a <= ia
            return const_false(op->type.lanes());
        } else if (le_a &&
                   le_b &&
                   equal(le_a->b, le_b->a) &&
                   const_int(le_b->b, &ia) &&
                   const_int(le_a->a, &ib) &&
                   ib > ia) {
            // (ib <= a && a <= ia) where there is no integer a s.t. ib <= a <= ia
            return const_false(op->type.lanes());

        } else if (eq_a &&
                   neq_b &&
                   equal(eq_a->a, neq_b->a) &&
                   is_simple_const(eq_a->b) &&
                   is_simple_const(neq_b->b)) {
            // (a == k1) && (a != k2) -> (a == k1) && (k1 != k2)
            // (second term always folds away)
            return mutate(And::make(a, NE::make(eq_a->b, neq_b->b)));
        } else if (neq_a &&
                   eq_b &&
                   equal(neq_a->a, eq_b->a) &&
                   is_simple_const(neq_a->b) &&
                   is_simple_const(eq_b->b)) {
            // (a != k1) && (a == k2) -> (a == k2) && (k1 != k2)
            // (second term always folds away)
            return mutate(And::make(b, NE::make(neq_a->b, eq_b->b)));
        } else if (eq_a &&
                   eq_a->a.as<Variable>() &&
                   is_simple_const(eq_a->b) &&
                   expr_uses_var(b, eq_a->a.as<Variable>()->name)) {
            // (somevar == k) && b -> (somevar == k) && substitute(somevar, k, b)
            return mutate(And::make(a, substitute(eq_a->a.as<Variable>(), eq_a->b, b)));
        } else if (eq_b &&
                   eq_b->a.as<Variable>() &&
                   is_simple_const(eq_b->b) &&
                   expr_uses_var(a, eq_b->a.as<Variable>()->name)) {
            // a && (somevar == k) -> substitute(somevar, k1, a) && (somevar == k)
            return mutate(And::make(substitute(eq_b->a.as<Variable>(), eq_b->b, a), b));
        } else if (broadcast_a &&
                   broadcast_b &&
                   broadcast_a->lanes == broadcast_b->lanes) {
            // x8(a) && x8(b) -> x8(a && b)
            return Broadcast::make(mutate(And::make(broadcast_a->value, broadcast_b->value)), broadcast_a->lanes);
        } else if (var_a && expr_uses_var(b, var_a->name)) {
            return mutate(a && substitute(var_a->name, make_one(a.type()), b));
        } else if (var_b && expr_uses_var(a, var_b->name)) {
            return mutate(substitute(var_b->name, make_one(b.type()), a) && b);
        } else if (a.same_as(op->a) &&
                   b.same_as(op->b)) {
            return op;
        } else {
            return And::make(a, b);
        }
    }

    Expr visit(const Or *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);
        Expr expr;
        if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
            return expr;
        }

        const Broadcast *broadcast_a = a.as<Broadcast>();
        const Broadcast *broadcast_b = b.as<Broadcast>();
        const EQ *eq_a = a.as<EQ>();
        const EQ *eq_b = b.as<EQ>();
        const NE *neq_a = a.as<NE>();
        const NE *neq_b = b.as<NE>();
        const Not *not_a = a.as<Not>();
        const Not *not_b = b.as<Not>();
        const LE *le_a = a.as<LE>();
        const LE *le_b = b.as<LE>();
        const LT *lt_a = a.as<LT>();
        const LT *lt_b = b.as<LT>();
        const Variable *var_a = a.as<Variable>();
        const Variable *var_b = b.as<Variable>();
        const And *and_a = a.as<And>();
        const And *and_b = b.as<And>();
        string name_a, name_b, name_c;
        int64_t ia = 0, ib = 0;

        if (is_one(a)) {
            return a;
        } else if (is_one(b)) {
            return b;
        } else if (is_zero(a)) {
            return b;
        } else if (is_zero(b)) {
            return a;
        } else if (equal(a, b)) {
            return a;
        } else if (eq_a &&
                   neq_b &&
                   ((equal(eq_a->a, neq_b->a) && equal(eq_a->b, neq_b->b)) ||
                    (equal(eq_a->a, neq_b->b) && equal(eq_a->b, neq_b->a)))) {
            // a == b || a != b
            return const_true(op->type.lanes());
        } else if (neq_a &&
                   eq_b &&
                   ((equal(eq_b->a, neq_a->a) && equal(eq_b->b, neq_a->b)) ||
                    (equal(eq_b->a, neq_a->b) && equal(eq_b->b, neq_a->a)))) {
            // a != b || a == b
            return const_true(op->type.lanes());
        } else if ((not_a && equal(not_a->a, b)) ||
                   (not_b && equal(not_b->a, a))) {
            // a || !a
            return const_true(op->type.lanes());
        } else if (le_a &&
                   lt_b &&
                   equal(le_a->a, lt_b->b) &&
                   equal(le_a->b, lt_b->a)) {
            // a <= b || b < a
            return const_true(op->type.lanes());
        } else if (lt_a &&
                   le_b &&
                   equal(lt_a->a, le_b->b) &&
                   equal(lt_a->b, le_b->a)) {
            // a < b || b <= a
            return const_true(op->type.lanes());
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->a, lt_b->b) &&
                   const_int(lt_a->b, &ia) &&
                   const_int(lt_b->a, &ib) &&
                   ib < ia) {
            // (a < ia || ib < a) where ib < ia
            return const_true(op->type.lanes());
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->b, lt_b->a) &&
                   const_int(lt_b->b, &ia) &&
                   const_int(lt_a->a, &ib) &&
                   ib < ia) {
            // (ib < a || a < ia) where ib < ia
            return const_true(op->type.lanes());
        } else if (le_a &&
                   lt_b &&
                   equal(le_a->a, lt_b->b) &&
                   const_int(le_a->b, &ia) &&
                   const_int(lt_b->a, &ib) &&
                   ib <= ia) {
            // (a <= ia || ib < a) where ib <= ia
            return const_true(op->type.lanes());
        } else if (le_a &&
                   lt_b &&
                   equal(le_a->b, lt_b->a) &&
                   const_int(lt_b->b, &ia) &&
                   const_int(le_a->a, &ib) &&
                   ib <= ia) {
            // (ib <= a || a < ia) where ib <= ia
            return const_true(op->type.lanes());
        } else if (lt_a &&
                   le_b &&
                   equal(lt_a->a, le_b->b) &&
                   const_int(lt_a->b, &ia) &&
                   const_int(le_b->a, &ib) &&
                   ib <= ia) {
            // (a < ia || ib <= a) where ib <= ia
            return const_true(op->type.lanes());
        } else if (lt_a &&
                   le_b &&
                   equal(lt_a->b, le_b->a) &&
                   const_int(le_b->b, &ia) &&
                   const_int(lt_a->a, &ib) &&
                   ib <= ia) {
            // (ib < a || a <= ia) where ib <= ia
            return const_true(op->type.lanes());
        } else if (le_a &&
                   le_b &&
                   equal(le_a->a, le_b->b) &&
                   const_int(le_a->b, &ia) &&
                   const_int(le_b->a, &ib) &&
                   ib <= ia + 1) {
            // (a <= ia || ib <= a) where ib <= ia + 1
            return const_true(op->type.lanes());
        } else if (le_a &&
                   le_b &&
                   equal(le_a->b, le_b->a) &&
                   const_int(le_b->b, &ia) &&
                   const_int(le_a->a, &ib) &&
                   ib <= ia + 1) {
            // (ib <= a || a <= ia) where ib <= ia + 1
            return const_true(op->type.lanes());

        } else if (broadcast_a &&
                   broadcast_b &&
                   broadcast_a->lanes == broadcast_b->lanes) {
            // x8(a) || x8(b) -> x8(a || b)
            return Broadcast::make(mutate(Or::make(broadcast_a->value, broadcast_b->value)), broadcast_a->lanes);
        } else if (eq_a &&
                   neq_b &&
                   equal(eq_a->a, neq_b->a) &&
                   is_simple_const(eq_a->b) &&
                   is_simple_const(neq_b->b)) {
            // (a == k1) || (a != k2) -> (a != k2) || (k1 == k2)
            // (second term always folds away)
            return mutate(Or::make(b, EQ::make(eq_a->b, neq_b->b)));
        } else if (neq_a &&
                   eq_b &&
                   equal(neq_a->a, eq_b->a) &&
                   is_simple_const(neq_a->b) &&
                   is_simple_const(eq_b->b)) {
            // (a != k1) || (a == k2) -> (a != k1) || (k1 == k2)
            // (second term always folds away)
            return mutate(Or::make(a, EQ::make(neq_a->b, eq_b->b)));
        } else if (var_a && expr_uses_var(b, var_a->name)) {
            return mutate(a || substitute(var_a->name, make_zero(a.type()), b));
        } else if (var_b && expr_uses_var(a, var_b->name)) {
            return mutate(substitute(var_b->name, make_zero(b.type()), a) || b);
        } else if (is_var_simple_const_comparison(b, &name_c) &&
                   and_a &&
                   ((is_var_simple_const_comparison(and_a->a, &name_a) && name_a == name_c) ||
                   (is_var_simple_const_comparison(and_a->b, &name_b) && name_b == name_c))) {
            // (a && b) || (c) -> (a || c) && (b || c)
            // iff c and at least one of a or b is of the form
            //     (var == const) or (var != const)
            // (and the vars are the same)
            return mutate(And::make(Or::make(and_a->a, b), Or::make(and_a->b, b)));
        } else if (is_var_simple_const_comparison(a, &name_c) &&
                   and_b &&
                   ((is_var_simple_const_comparison(and_b->a, &name_a) && name_a == name_c) ||
                   (is_var_simple_const_comparison(and_b->b, &name_b) && name_b == name_c))) {
            // (c) || (a && b) -> (a || c) && (b || c)
            // iff c and at least one of a or b is of the form
            //     (var == const) or (var != const)
            // (and the vars are the same)
            return mutate(And::make(Or::make(and_b->a, a), Or::make(and_b->b, a)));
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Or::make(a, b);
        }
    }

    Expr visit(const Not *op) override {
        Expr a = mutate(op->a);
        Expr expr;
        if (propagate_indeterminate_expression(a, op->type, &expr)) {
            return expr;
        }

        const Call *c;
        if (is_one(a)) {
            return make_zero(a.type());
        } else if (is_zero(a)) {
            return make_one(a.type());
        } else if (const Not *n = a.as<Not>()) {
            // Double negatives cancel
            return n->a;
        } else if (const LE *n = a.as<LE>()) {
            return LT::make(n->b, n->a);
        } else if (const GE *n = a.as<GE>()) {
            return LT::make(n->a, n->b);
        } else if (const LT *n = a.as<LT>()) {
            return LE::make(n->b, n->a);
        } else if (const GT *n = a.as<GT>()) {
            return LE::make(n->a, n->b);
        } else if (const NE *n = a.as<NE>()) {
            return EQ::make(n->a, n->b);
        } else if (const EQ *n = a.as<EQ>()) {
            return NE::make(n->a, n->b);
        } else if (const Broadcast *n = a.as<Broadcast>()) {
            return mutate(Broadcast::make(!n->value, n->lanes));
        } else if ((c = a.as<Call>()) != nullptr && c->is_intrinsic(Call::likely)) {
            // !likely(e) -> likely(!e)
            return likely(mutate(Not::make(c->args[0])));
        } else if (a.same_as(op->a)) {
            return op;
        } else {
            return Not::make(a);
        }
    }

    Expr visit(const Select *op) override {
        Expr condition = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        Expr expr;
        if (propagate_indeterminate_expression(condition, true_value, false_value, op->type, &expr)) {
            return expr;
        }

        const Call *ct = true_value.as<Call>();
        const Call *cf = false_value.as<Call>();
        const Select *sel_t = true_value.as<Select>();
        const Select *sel_f = false_value.as<Select>();
        const Add *add_t = true_value.as<Add>();
        const Add *add_f = false_value.as<Add>();
        const Sub *sub_t = true_value.as<Sub>();
        const Sub *sub_f = false_value.as<Sub>();
        const Mul *mul_t = true_value.as<Mul>();
        const Mul *mul_f = false_value.as<Mul>();

        if (is_zero(condition)) {
            return false_value;
        } else if (is_one(condition)) {
            return true_value;
        } else if (equal(true_value, false_value)) {
            return true_value;
        } else if (true_value.type().is_bool() &&
                   is_one(true_value) &&
                   is_zero(false_value)) {
            if (true_value.type().is_vector() && condition.type().is_scalar()) {
                return Broadcast::make(condition, true_value.type().lanes());
            } else {
                return condition;
            }
        } else if (true_value.type().is_bool() &&
                   is_zero(true_value) &&
                   is_one(false_value)) {
            if (true_value.type().is_vector() && condition.type().is_scalar()) {
                return Broadcast::make(mutate(!condition), true_value.type().lanes());
            } else {
                return mutate(!condition);
            }
        } else if (const Broadcast *b = condition.as<Broadcast>()) {
            // Select of broadcast -> scalar select
            return mutate(Select::make(b->value, true_value, false_value));
        } else if (const NE *ne = condition.as<NE>()) {
            // Normalize select(a != b, c, d) to select(a == b, d, c)
            return mutate(Select::make(ne->a == ne->b, false_value, true_value));
        } else if (const LE *le = condition.as<LE>()) {
            // Normalize select(a <= b, c, d) to select(b < a, d, c)
            return mutate(Select::make(le->b < le->a, false_value, true_value));
        } else if (ct && ct->is_intrinsic(Call::likely) &&
                   equal(ct->args[0], false_value)) {
            // select(cond, likely(a), a) -> likely(a)
            return true_value;
        } else if (cf &&
                   cf->is_intrinsic(Call::likely) &&
                   equal(cf->args[0], true_value)) {
            // select(cond, a, likely(a)) -> likely(a)
            return false_value;
        } else if (sel_t &&
                   equal(sel_t->true_value, false_value)) {
            // select(a, select(b, c, d), c) -> select(a && !b, d, c)
            return mutate(Select::make(condition && !sel_t->condition, sel_t->false_value, false_value));
        } else if (sel_t &&
                   equal(sel_t->false_value, false_value)) {
            // select(a, select(b, c, d), d) -> select(a && b, c, d)
            return mutate(Select::make(condition && sel_t->condition, sel_t->true_value, false_value));
        } else if (sel_f &&
                   equal(sel_f->false_value, true_value)) {
            // select(a, d, select(b, c, d)) -> select(a || !b, d, c)
            return mutate(Select::make(condition || !sel_f->condition, true_value, sel_f->true_value));
        } else if (sel_f &&
                   equal(sel_f->true_value, true_value)) {
            // select(a, d, select(b, d, c)) -> select(a || b, d, c)
            return mutate(Select::make(condition || sel_f->condition, true_value, sel_f->false_value));
        } else if (sel_t &&
                   equal(sel_t->condition, condition)) {
            // select(a, select(a, b, c), d) -> select(a, b, d)
            return mutate(Select::make(condition, sel_t->true_value, false_value));
        } else if (sel_f &&
                   equal(sel_f->condition, condition)) {
            // select(a, b, select(a, c, d)) -> select(a, b, d)
            return mutate(Select::make(condition, true_value, sel_f->false_value));
        } else if (add_t &&
                   add_f &&
                   equal(add_t->a, add_f->a)) {
            // select(c, a+b, a+d) -> a + select(x, b, d)
            return mutate(add_t->a + Select::make(condition, add_t->b, add_f->b));
        } else if (add_t &&
                   add_f &&
                   equal(add_t->a, add_f->b)) {
            // select(c, a+b, d+a) -> a + select(x, b, d)
            return mutate(add_t->a + Select::make(condition, add_t->b, add_f->a));
        } else if (add_t &&
                   add_f &&
                   equal(add_t->b, add_f->a)) {
            // select(c, b+a, a+d) -> a + select(x, b, d)
            return mutate(add_t->b + Select::make(condition, add_t->a, add_f->b));
        } else if (add_t &&
                   add_f &&
                   equal(add_t->b, add_f->b)) {
            // select(c, b+a, d+a) -> select(x, b, d) + a
            return mutate(Select::make(condition, add_t->a, add_f->a) + add_t->b);
        } else if (sub_t &&
                   sub_f &&
                   equal(sub_t->a, sub_f->a)) {
            // select(c, a-b, a-d) -> a - select(x, b, d)
            return mutate(sub_t->a - Select::make(condition, sub_t->b, sub_f->b));
        } else if (sub_t &&
                   sub_f &&
                   equal(sub_t->b, sub_f->b)) {
            // select(c, b-a, d-a) -> select(x, b, d) - a
            return mutate(Select::make(condition, sub_t->a, sub_f->a) - sub_t->b);\
        } else if (add_t &&
                   sub_f &&
                   equal(add_t->a, sub_f->a)) {
            // select(c, a+b, a-d) -> a + select(x, b, 0-d)
            return mutate(add_t->a + Select::make(condition, add_t->b, make_zero(sub_f->b.type()) - sub_f->b));
        } else if (add_t &&
                   sub_f &&
                   equal(add_t->b, sub_f->a)) {
            // select(c, b+a, a-d) -> a + select(x, b, 0-d)
            return mutate(add_t->b + Select::make(condition, add_t->a, make_zero(sub_f->b.type()) - sub_f->b));
        } else if (sub_t &&
                   add_f &&
                   equal(sub_t->a, add_f->a)) {
            // select(c, a-b, a+d) -> a + select(x, 0-b, d)
            return mutate(sub_t->a + Select::make(condition, make_zero(sub_t->b.type()) - sub_t->b, add_f->b));
        } else if (sub_t &&
                   add_f &&
                   equal(sub_t->a, add_f->b)) {
            // select(c, a-b, d+a) -> a + select(x, 0-b, d)
            return mutate(sub_t->a + Select::make(condition, make_zero(sub_t->b.type()) - sub_t->b, add_f->a));
        } else if (mul_t &&
                   mul_f &&
                   equal(mul_t->a, mul_f->a)) {
            // select(c, a*b, a*d) -> a * select(x, b, d)
            return mutate(mul_t->a * Select::make(condition, mul_t->b, mul_f->b));
        } else if (mul_t &&
                   mul_f &&
                   equal(mul_t->a, mul_f->b)) {
            // select(c, a*b, d*a) -> a * select(x, b, d)
            return mutate(mul_t->a * Select::make(condition, mul_t->b, mul_f->a));
        } else if (mul_t &&
                   mul_f &&
                   equal(mul_t->b, mul_f->a)) {
            // select(c, b*a, a*d) -> a * select(x, b, d)
            return mutate(mul_t->b * Select::make(condition, mul_t->a, mul_f->b));
        } else if (mul_t &&
                   mul_f &&
                   equal(mul_t->b, mul_f->b)) {
            // select(c, b*a, d*a) -> select(x, b, d) * a
            return mutate(Select::make(condition, mul_t->a, mul_f->a) * mul_t->b);
        } else if (condition.same_as(op->condition) &&
                   true_value.same_as(op->true_value) &&
                   false_value.same_as(op->false_value)) {
            return op;
        } else {
            return Select::make(condition, true_value, false_value);
        }
    }

    Expr visit(const Ramp *op) override {
        Expr base = mutate(op->base);
        Expr stride = mutate(op->stride);

        if (is_zero(stride)) {
            return Broadcast::make(base, op->lanes);
        } else if (base.same_as(op->base) &&
                   stride.same_as(op->stride)) {
            return op;
        } else {
            return Ramp::make(base, stride, op->lanes);
        }
    }

    Stmt visit(const IfThenElse *op) override {
        Expr condition = mutate(op->condition);

        // If (true) ...
        if (is_one(condition)) {
            return mutate(op->then_case);
        }

        // If (false) ...
        if (is_zero(condition)) {
            Stmt stmt = mutate(op->else_case);
            if (!stmt.defined()) {
                // Emit a noop
                stmt = Evaluate::make(0);
            }
            return stmt;
        }

        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        // If both sides are no-ops, bail out.
        if (is_no_op(then_case) && is_no_op(else_case)) {
            return then_case;
        }

        // Remember the statements before substitution.
        Stmt then_nosubs = then_case;
        Stmt else_nosubs = else_case;

        // Mine the condition for useful constraints to apply (eg var == value && bool_param).
        vector<Expr> stack;
        stack.push_back(condition);
        bool and_chain = false, or_chain = false;
        while (!stack.empty()) {
            Expr next = stack.back();
            stack.pop_back();

            if (!or_chain) {
                then_case = substitute(next, const_true(), then_case);
            }
            if (!and_chain) {
                else_case = substitute(next, const_false(), else_case);
            }

            if (const And *a = next.as<And>()) {
                if (!or_chain) {
                    stack.push_back(a->b);
                    stack.push_back(a->a);
                    and_chain = true;
                }
            } else if (const Or *o = next.as<Or>()) {
                if (!and_chain) {
                    stack.push_back(o->b);
                    stack.push_back(o->a);
                    or_chain = true;
                }
            } else {
                const EQ *eq = next.as<EQ>();
                const NE *ne = next.as<NE>();
                const Variable *var = eq ? eq->a.as<Variable>() : next.as<Variable>();

                if (eq && var) {
                    if (!or_chain) {
                        then_case = substitute(var->name, eq->b, then_case);
                    }
                    if (!and_chain && eq->b.type().is_bool()) {
                        else_case = substitute(var->name, !eq->b, else_case);
                    }
                } else if (var) {
                    if (!or_chain) {
                        then_case = substitute(var->name, const_true(), then_case);
                    }
                    if (!and_chain) {
                        else_case = substitute(var->name, const_false(), else_case);
                    }
                } else if (eq && is_const(eq->b) && !or_chain) {
                    // some_expr = const
                    then_case = substitute(eq->a, eq->b, then_case);
                } else if (ne && is_const(ne->b) && !and_chain) {
                    // some_expr != const
                    else_case = substitute(ne->a, ne->b, else_case);
                }
            }
        }

        // If substitutions have been made, simplify again.
        if (!then_case.same_as(then_nosubs)) {
            then_case = mutate(then_case);
        }
        if (!else_case.same_as(else_nosubs)) {
            else_case = mutate(else_case);
        }

        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(condition, then_case, else_case);
        }
    }

    Expr visit(const Load *op) override {
        found_buffer_reference(op->name);

        Expr predicate = mutate(op->predicate);
        Expr index = mutate(op->index);

        const Broadcast *b_index = index.as<Broadcast>();
        const Broadcast *b_pred = predicate.as<Broadcast>();
        if (is_zero(predicate)) {
            // Predicate is always false
            return undef(op->type);
        } else if (b_index && b_pred) {
            // Load of a broadcast should be broadcast of the load
            Expr load = Load::make(op->type.element_of(), op->name, b_index->value, op->image, op->param, b_pred->value);
            return Broadcast::make(load, b_index->lanes);
        } else if (predicate.same_as(op->predicate) && index.same_as(op->index)) {
            return op;
        } else {
            return Load::make(op->type, op->name, index, op->image, op->param, predicate);
        }
    }

    Expr visit(const Call *op) override {
        // Calls implicitly depend on host, dev, mins, and strides of the buffer referenced
        if (op->call_type == Call::Image || op->call_type == Call::Halide) {
            found_buffer_reference(op->name, op->args.size());
        }

        if (op->is_intrinsic(Call::shift_left) ||
            op->is_intrinsic(Call::shift_right)) {
            Expr a = mutate(op->args[0]), b = mutate(op->args[1]);
            Expr expr;
            if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
                return expr;
            }

            int64_t ib = 0;
            if (const_int(b, &ib) || const_uint(b, (uint64_t *)(&ib))) {
                Type t = op->type;

                bool shift_left = op->is_intrinsic(Call::shift_left);
                if (t.is_int() && ib < 0) {
                    shift_left = !shift_left;
                    ib = -ib;
                }

                if (ib >= 0 && ib < std::min(t.bits(), 64) - 1) {
                    ib = 1LL << ib;
                    b = make_const(t, ib);

                    if (shift_left) {
                        return mutate(Mul::make(a, b));
                    } else {
                        return mutate(Div::make(a, b));
                    }
                }
            }

            if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
                return op;
            } else if (op->is_intrinsic(Call::shift_left)) {
                return a << b;
            } else {
                return a >> b;
            }
        } else if (op->is_intrinsic(Call::bitwise_and)) {
            Expr a = mutate(op->args[0]), b = mutate(op->args[1]);
            Expr expr;
            if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
                return expr;
            }

            int64_t ia, ib = 0;
            uint64_t ua, ub = 0;
            int bits;

            if (const_int(a, &ia) &&
                const_int(b, &ib)) {
                return make_const(op->type, ia & ib);
            } else if (const_uint(a, &ua) &&
                       const_uint(b, &ub)) {
                return make_const(op->type, ua & ub) ;
            } else if (const_int(b, &ib) &&
                !b.type().is_max(ib) &&
                is_const_power_of_two_integer(make_const(a.type(), ib + 1), &bits)) {
                return Mod::make(a, make_const(a.type(), ib + 1));
            } else if (const_uint(b, &ub) &&
                       b.type().is_max(ub)) {
                return a;
            } else if (const_uint(b, &ub) &&
                       is_const_power_of_two_integer(make_const(a.type(), ub + 1), &bits)) {
                return Mod::make(a, make_const(a.type(), ub + 1));
            } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
                return op;
            } else {
                return a & b;
            }
        } else if (op->is_intrinsic(Call::bitwise_or)) {
            Expr a = mutate(op->args[0]), b = mutate(op->args[1]);
            Expr expr;
            if (propagate_indeterminate_expression(a, b, op->type, &expr)) {
                return expr;
            }
            int64_t ia, ib;
            uint64_t ua, ub;
            if (const_int(a, &ia) &&
                const_int(b, &ib)) {
                return make_const(op->type, ia | ib);
            } else if (const_uint(a, &ua) &&
                       const_uint(b, &ub)) {
                return make_const(op->type, ua | ub);
            } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
                return op;
            } else {
                return a | b;
            }
        } else if (op->is_intrinsic(Call::bitwise_not)) {
            Expr a = mutate(op->args[0]);
            Expr expr;
            if (propagate_indeterminate_expression(a, op->type, &expr)) {
                return expr;
            }
            int64_t ia;
            uint64_t ua;
            if (const_int(a, &ia)) {
                return make_const(op->type, ~ia);
            } else if (const_uint(a, &ua)) {
                return make_const(op->type, ~ua);
            } else if (a.same_as(op->args[0])) {
                return op;
            } else {
                return ~a;
            }
        } else if (op->is_intrinsic(Call::reinterpret)) {
            Expr a = mutate(op->args[0]);
            Expr expr;
            if (propagate_indeterminate_expression(a, op->type, &expr)) {
                return expr;
            }
            int64_t ia;
            uint64_t ua;
            bool vector = op->type.is_vector() || a.type().is_vector();
            if (op->type == a.type()) {
                return a;
            } else if (const_int(a, &ia) && op->type.is_uint() && !vector) {
                // int -> uint
                return make_const(op->type, (uint64_t)ia);
            } else if (const_uint(a, &ua) && op->type.is_int() && !vector) {
                // uint -> int
                return make_const(op->type, (int64_t)ua);
            } else if (a.same_as(op->args[0])) {
                return op;
            } else {
                return reinterpret(op->type, a);
            }
        } else if (op->is_intrinsic(Call::abs)) {
            // Constant evaluate abs(x).
            Expr a = mutate(op->args[0]);
            Expr expr;
            if (propagate_indeterminate_expression(a, op->type, &expr)) {
                return expr;
            }
            Type ta = a.type();
            int64_t ia = 0;
            double fa = 0;
            if (ta.is_int() && const_int(a, &ia)) {
                if (ia < 0 && !(Int(64).is_min(ia))) {
                    ia = -ia;
                }
                return make_const(op->type, ia);
            } else if (ta.is_uint()) {
                // abs(uint) is a no-op.
                return a;
            } else if (const_float(a, &fa)) {
                if (fa < 0) {
                    fa = -fa;
                }
                return make_const(a.type(), fa);
            } else if (a.same_as(op->args[0])) {
                return op;
            } else {
                return abs(a);
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "is_nan_f32") {
            Expr arg = mutate(op->args[0]);
            double f = 0.0;
            if (const_float(arg, &f)) {
                return std::isnan(f);
            } else if (arg.same_as(op->args[0])) {
                return op;
            } else {
                return Call::make(op->type, op->name, {arg}, op->call_type);
            }
        } else if (op->is_intrinsic(Call::stringify)) {
            // Eagerly concat constant arguments to a stringify.
            bool changed = false;
            vector<Expr> new_args;
            const StringImm *last = nullptr;
            for (size_t i = 0; i < op->args.size(); i++) {
                Expr arg = mutate(op->args[i]);
                if (!arg.same_as(op->args[i])) {
                    changed = true;
                }
                const StringImm *string_imm = arg.as<StringImm>();
                const IntImm    *int_imm    = arg.as<IntImm>();
                const FloatImm  *float_imm  = arg.as<FloatImm>();
                // We use snprintf here rather than stringstreams,
                // because the runtime's float printing is guaranteed
                // to match snprintf.
                char buf[64]; // Large enough to hold the biggest float literal.
                if (last && string_imm) {
                    new_args.back() = last->value + string_imm->value;
                    changed = true;
                } else if (int_imm) {
                    snprintf(buf, sizeof(buf), "%lld", (long long)int_imm->value);
                    if (last) {
                        new_args.back() = last->value + buf;
                    } else {
                        new_args.push_back(string(buf));
                    }
                    changed = true;
                } else if (last && float_imm) {
                    snprintf(buf, sizeof(buf), "%f", float_imm->value);
                    if (last) {
                        new_args.back() = last->value + buf;
                    } else {
                        new_args.push_back(string(buf));
                    }
                    changed = true;
                } else {
                    new_args.push_back(arg);
                }
                last = new_args.back().as<StringImm>();
            }

            if (new_args.size() == 1 && new_args[0].as<StringImm>()) {
                // stringify of a string constant is just the string constant
                return new_args[0];
            } else if (changed) {
                return Call::make(op->type, op->name, new_args, op->call_type);
            } else {
                return op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "sqrt_f32") {
            Expr arg = mutate(op->args[0]);
            Expr expr;
            if (propagate_indeterminate_expression(arg, op->type, &expr)) {
                return expr;
            }
            if (const double *f = as_const_float(arg)) {
                return FloatImm::make(arg.type(), std::sqrt(*f));
            } else if (!arg.same_as(op->args[0])) {
                return Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                return op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "log_f32") {
            Expr arg = mutate(op->args[0]);
            Expr expr;
            if (propagate_indeterminate_expression(arg, op->type, &expr)) {
                return expr;
            }
            if (const double *f = as_const_float(arg)) {
                return FloatImm::make(arg.type(), std::log(*f));
            } else if (!arg.same_as(op->args[0])) {
                return Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                return op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "exp_f32") {
            Expr arg = mutate(op->args[0]);
            Expr expr;
            if (propagate_indeterminate_expression(arg, op->type, &expr)) {
                return expr;
            }
            if (const double *f = as_const_float(arg)) {
                return FloatImm::make(arg.type(), std::exp(*f));
            } else if (!arg.same_as(op->args[0])) {
                return Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                return op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "pow_f32") {
            Expr arg0 = mutate(op->args[0]);
            Expr arg1 = mutate(op->args[1]);
            Expr expr;
            if (propagate_indeterminate_expression(arg0, arg1, op->type, &expr)) {
                return expr;
            }
            const double *f0 = as_const_float(arg0);
            const double *f1 = as_const_float(arg1);
            if (f0 && f1) {
                return FloatImm::make(arg0.type(), std::pow(*f0, *f1));
            } else if (!arg0.same_as(op->args[0]) || !arg1.same_as(op->args[1])) {
                return Call::make(op->type, op->name, {arg0, arg1}, op->call_type);
            } else {
                return op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   (op->name == "floor_f32" || op->name == "ceil_f32" ||
                    op->name == "round_f32" || op->name == "trunc_f32")) {
            internal_assert(op->args.size() == 1);
            Expr arg = mutate(op->args[0]);
            Expr expr;
            if (propagate_indeterminate_expression(arg, op->type, &expr)) {
                return expr;
            }
            const Call *call = arg.as<Call>();
            if (const double *f = as_const_float(arg)) {
                if (op->name == "floor_f32") {
                    return FloatImm::make(arg.type(), std::floor(*f));
                } else if (op->name == "ceil_f32") {
                    return FloatImm::make(arg.type(), std::ceil(*f));
                } else if (op->name == "round_f32") {
                    return FloatImm::make(arg.type(), std::nearbyint(*f));
                } else if (op->name == "trunc_f32") {
                    return FloatImm::make(arg.type(), (*f < 0 ? std::ceil(*f) : std::floor(*f)));
                } else {
                    return op;
                }
            } else if (call && call->call_type == Call::PureExtern &&
                       (call->name == "floor_f32" || call->name == "ceil_f32" ||
                        call->name == "round_f32" || call->name == "trunc_f32")) {
                // For any combination of these integer-valued functions, we can
                // discard the outer function. For example, floor(ceil(x)) == ceil(x).
                return call;
            } else if (!arg.same_as(op->args[0])) {
                return Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                return op;
            }
        } else if (op->is_intrinsic(Call::prefetch)) {
            // Collapse the prefetched region into lower dimension whenever is possible.
            // TODO(psuriana): Deal with negative strides and overlaps.

            internal_assert(op->args.size() % 2 == 0); // Format: {base, offset, extent0, min0, ...}

            vector<Expr> args(op->args);
            bool changed = false;
            for (size_t i = 0; i < op->args.size(); ++i) {
                args[i] = mutate(op->args[i]);
                if (!args[i].same_as(op->args[i])) {
                    changed = true;
                }
            }

            // The {extent, stride} args in the prefetch call are sorted
            // based on the storage dimension in ascending order (i.e. innermost
            // first and outermost last), so, it is enough to check for the upper
            // triangular pairs to see if any contiguous addresses exist.
            for (size_t i = 2; i < args.size(); i += 2) {
                Expr extent_0 = args[i];
                Expr stride_0 = args[i + 1];
                for (size_t j = i + 2; j < args.size(); j += 2) {
                    Expr extent_1 = args[j];
                    Expr stride_1 = args[j + 1];

                    if (can_prove(extent_0 * stride_0 == stride_1)) {
                        Expr new_extent = mutate(extent_0 * extent_1);
                        Expr new_stride = stride_0;
                        args.erase(args.begin() + j, args.begin() + j + 2);
                        args[i] = new_extent;
                        args[i + 1] = new_stride;
                        i -= 2;
                        break;
                    }
                }
            }
            internal_assert(args.size() <= op->args.size());

            if (changed || (args.size() != op->args.size())) {
                return Call::make(op->type, Call::prefetch, args, Call::Intrinsic);
            } else {
                return op;
            }
        } else if (op->is_intrinsic(Call::require)) {
            Expr cond = mutate(op->args[0]);
            // likely(const-bool) is deliberately not reduced
            // by the simplify(), but for our purposes here, we want
            // to ignore the likely() wrapper. (Note that this is
            // equivalent to calling can_prove() without needing to
            // create a new Simplifier instance.)
            if (const Call *c = cond.as<Call>()) {
                if (c->is_intrinsic(Call::likely)) {
                    cond = c->args[0];
                }
            }
            if (is_one(cond)) {
                return mutate(op->args[1]);
            } else {
                if (is_zero(cond)) {
                    // (We could simplify this to avoid evaluating the provably-false
                    // expression, but since this is a degenerate condition, don't bother.)
                    user_warning << "This pipeline is guaranteed to fail a require() expression at runtime: \n"
                                 << Expr(op) << "\n";
                }
                return IRMutator2::visit(op);
            }
        } else {
            return IRMutator2::visit(op);
        }
    }

    Expr visit(const Shuffle *op) override {
        if (op->is_extract_element() &&
            (op->vectors[0].as<Ramp>() ||
             op->vectors[0].as<Broadcast>())) {
            // Extracting a single lane of a ramp or broadcast
            if (const Ramp *r = op->vectors[0].as<Ramp>()) {
                return mutate(r->base + op->indices[0]*r->stride);
            } else if (const Broadcast *b = op->vectors[0].as<Broadcast>()) {
                return mutate(b->value);
            } else {
                internal_error << "Unreachable";
                return Expr();
            }
        }

        // Mutate the vectors
        vector<Expr> new_vectors;
        bool changed = false;
        for (Expr vector : op->vectors) {
            Expr new_vector = mutate(vector);
            if (!vector.same_as(new_vector)) {
                changed = true;
            }
            new_vectors.push_back(new_vector);
        }

        // Try to convert a load with shuffled indices into a
        // shuffle of a dense load.
        if (const Load *first_load = new_vectors[0].as<Load>()) {
            vector<Expr> load_predicates;
            vector<Expr> load_indices;
            bool unpredicated = true;
            for (Expr e : new_vectors) {
                const Load *load = e.as<Load>();
                if (load && load->name == first_load->name) {
                    load_predicates.push_back(load->predicate);
                    load_indices.push_back(load->index);
                    unpredicated = unpredicated && is_one(load->predicate);
                } else {
                    break;
                }
            }

            if (load_indices.size() == new_vectors.size()) {
                Type t = load_indices[0].type().with_lanes(op->indices.size());
                Expr shuffled_index = Shuffle::make(load_indices, op->indices);
                shuffled_index = mutate(shuffled_index);
                if (shuffled_index.as<Ramp>()) {
                    Expr shuffled_predicate;
                    if (unpredicated) {
                        shuffled_predicate = const_true(t.lanes());
                    } else {
                        shuffled_predicate = Shuffle::make(load_predicates, op->indices);
                        shuffled_predicate = mutate(shuffled_predicate);
                    }
                    t = first_load->type;
                    t = t.with_lanes(op->indices.size());
                    return Load::make(t, first_load->name, shuffled_index, first_load->image,
                                      first_load->param, shuffled_predicate);
                }
            }
        }

        // Try to collapse a shuffle of broadcasts into a single
        // broadcast. Note that it doesn't matter what the indices
        // are.
        const Broadcast *b1 = new_vectors[0].as<Broadcast>();
        if (b1) {
            bool can_collapse = true;
            for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
                if (const Broadcast *b2 = new_vectors[i].as<Broadcast>()) {
                    Expr check = mutate(b1->value - b2->value);
                    can_collapse &= is_zero(check);
                } else {
                    can_collapse = false;
                }
            }
            if (can_collapse) {
                if (op->indices.size() == 1) {
                    return b1->value;
                } else {
                    return Broadcast::make(b1->value, op->indices.size());
                }
            }
        }

        if (op->is_interleave()) {
            int terms = (int)new_vectors.size();

            // Try to collapse an interleave of ramps into a single ramp.
            const Ramp *r = new_vectors[0].as<Ramp>();
            if (r) {
                bool can_collapse = true;
                for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
                    // If we collapse these terms into a single ramp,
                    // the new stride is going to be the old stride
                    // divided by the number of terms, so the
                    // difference between two adjacent terms in the
                    // interleave needs to be a broadcast of the new
                    // stride.
                    Expr diff = mutate(new_vectors[i] - new_vectors[i-1]);
                    const Broadcast *b = diff.as<Broadcast>();
                    if (b) {
                        Expr check = mutate(b->value * terms - r->stride);
                        can_collapse &= is_zero(check);
                    } else {
                        can_collapse = false;
                    }
                }
                if (can_collapse) {
                    return Ramp::make(r->base, mutate(r->stride / terms), r->lanes * terms);
                }
            }

            // Try to collapse an interleave of slices of vectors from
            // the same vector into a single vector.
            if (const Shuffle *first_shuffle = new_vectors[0].as<Shuffle>()) {
                if (first_shuffle->is_slice()) {
                    bool can_collapse = true;
                    for (size_t i = 0; i < new_vectors.size() && can_collapse; i++) {
                        const Shuffle *i_shuffle = new_vectors[i].as<Shuffle>();

                        // Check that the current shuffle is a slice...
                        if (!i_shuffle || !i_shuffle->is_slice()) {
                            can_collapse = false;
                            break;
                        }

                        // ... and that it is a slice in the right place...
                        if (i_shuffle->slice_begin() != (int)i || i_shuffle->slice_stride() != terms) {
                            can_collapse = false;
                            break;
                        }

                        if (i > 0) {
                            // ... and that the vectors being sliced are the same.
                            if (first_shuffle->vectors.size() != i_shuffle->vectors.size()) {
                                can_collapse = false;
                                break;
                            }

                            for (size_t j = 0; j < first_shuffle->vectors.size() && can_collapse; j++) {
                                if (!equal(first_shuffle->vectors[j], i_shuffle->vectors[j])) {
                                    can_collapse = false;
                                }
                            }
                        }
                    }

                    if (can_collapse) {
                        return Shuffle::make_concat(first_shuffle->vectors);
                    }
                }
            }
        } else if (op->is_concat()) {
            // Try to collapse a concat of ramps into a single ramp.
            const Ramp *r = new_vectors[0].as<Ramp>();
            if (r) {
                bool can_collapse = true;
                for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
                    Expr diff;
                    if (new_vectors[i].type().lanes() == new_vectors[i-1].type().lanes()) {
                        diff = mutate(new_vectors[i] - new_vectors[i-1]);
                    }

                    const Broadcast *b = diff.as<Broadcast>();
                    if (b) {
                        Expr check = mutate(b->value - r->stride * new_vectors[i-1].type().lanes());
                        can_collapse &= is_zero(check);
                    } else {
                        can_collapse = false;
                    }
                }
                if (can_collapse) {
                    return Ramp::make(r->base, r->stride, op->indices.size());
                }
            }

            // Try to collapse a concat of scalars into a ramp.
            if (new_vectors[0].type().is_scalar() && new_vectors[1].type().is_scalar()) {
                bool can_collapse = true;
                Expr stride = mutate(new_vectors[1] - new_vectors[0]);
                for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
                    if (!new_vectors[i].type().is_scalar()) {
                        can_collapse = false;
                        break;
                    }

                    Expr check = mutate(new_vectors[i] - new_vectors[i - 1] - stride);
                    if (!is_zero(check)) {
                        can_collapse = false;
                    }
                }

                if (can_collapse) {
                    return Ramp::make(new_vectors[0], stride, op->indices.size());
                }
            }
        }

        if (!changed) {
            return op;
        } else {
            return Shuffle::make(new_vectors, op->indices);
        }
    }

    template <typename T>
    Expr hoist_slice_vector(Expr e) {
        const T *op = e.as<T>();
        internal_assert(op);

        const Shuffle *shuffle_a = op->a.template as<Shuffle>();
        const Shuffle *shuffle_b = op->b.template as<Shuffle>();

        internal_assert(shuffle_a && shuffle_b &&
                        shuffle_a->is_slice() &&
                        shuffle_b->is_slice());

        if (shuffle_a->indices != shuffle_b->indices) {
            return e;
        }

        const std::vector<Expr> &slices_a = shuffle_a->vectors;
        const std::vector<Expr> &slices_b = shuffle_b->vectors;
        if (slices_a.size() != slices_b.size()) {
            return e;
        }

        for (size_t i = 0; i < slices_a.size(); i++) {
            if (slices_a[i].type() != slices_b[i].type()) {
                return e;
            }
        }

        vector<Expr> new_slices;
        for (size_t i = 0; i < slices_a.size(); i++) {
            new_slices.push_back(T::make(slices_a[i], slices_b[i]));
        }

        return Shuffle::make(new_slices, shuffle_a->indices);
    }

    template<typename T, typename Body>
    Body simplify_let(const T *op) {
        internal_assert(!var_info.contains(op->name))
            << "Simplify only works on code where every name is unique. Repeated name: " << op->name << "\n";

        // If the value is trivial, make a note of it in the scope so
        // we can subs it in later
        Expr value = mutate(op->value);
        Body body = op->body;

        // Iteratively peel off certain operations from the let value and push them inside.
        Expr new_value = value;
        string new_name = op->name + ".s";
        Expr new_var = Variable::make(new_value.type(), new_name);
        Expr replacement = new_var;

        debug(4) << "simplify let " << op->name << " = " << value << " in ... " << op->name << " ...\n";

        while (1) {
            const Variable *var = new_value.as<Variable>();
            const Add *add = new_value.as<Add>();
            const Sub *sub = new_value.as<Sub>();
            const Mul *mul = new_value.as<Mul>();
            const Div *div = new_value.as<Div>();
            const Mod *mod = new_value.as<Mod>();
            const Min *min = new_value.as<Min>();
            const Max *max = new_value.as<Max>();
            const Ramp *ramp = new_value.as<Ramp>();
            const Cast *cast = new_value.as<Cast>();
            const Broadcast *broadcast = new_value.as<Broadcast>();
            const Shuffle *shuffle = new_value.as<Shuffle>();
            const Variable *var_b = nullptr;
            const Variable *var_a = nullptr;
            if (add) {
                var_b = add->b.as<Variable>();
            } else if (sub) {
                var_b = sub->b.as<Variable>();
            } else if (mul) {
                var_b = mul->b.as<Variable>();
            } else if (shuffle && shuffle->is_concat() && shuffle->vectors.size() == 2) {
                var_a = shuffle->vectors[0].as<Variable>();
                var_b = shuffle->vectors[1].as<Variable>();
            }

            if (is_const(new_value)) {
                replacement = substitute(new_name, new_value, replacement);
                new_value = Expr();
                break;
            } else if (var) {
                replacement = substitute(new_name, var, replacement);
                new_value = Expr();
                break;
            } else if (add && (is_const(add->b) || var_b)) {
                replacement = substitute(new_name, Add::make(new_var, add->b), replacement);
                new_value = add->a;
            } else if (mul && (is_const(mul->b) || var_b)) {
                replacement = substitute(new_name, Mul::make(new_var, mul->b), replacement);
                new_value = mul->a;
            } else if (div && is_const(div->b)) {
                replacement = substitute(new_name, Div::make(new_var, div->b), replacement);
                new_value = div->a;
            } else if (sub && (is_const(sub->b) || var_b)) {
                replacement = substitute(new_name, Sub::make(new_var, sub->b), replacement);
                new_value = sub->a;
            } else if (mod && is_const(mod->b)) {
                replacement = substitute(new_name, Mod::make(new_var, mod->b), replacement);
                new_value = mod->a;
            } else if (min && is_const(min->b)) {
                replacement = substitute(new_name, Min::make(new_var, min->b), replacement);
                new_value = min->a;
            } else if (max && is_const(max->b)) {
                replacement = substitute(new_name, Max::make(new_var, max->b), replacement);
                new_value = max->a;
            } else if (ramp && is_const(ramp->stride)) {
                new_value = ramp->base;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, Ramp::make(new_var, ramp->stride, ramp->lanes), replacement);
            } else if (broadcast) {
                new_value = broadcast->value;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, Broadcast::make(new_var, broadcast->lanes), replacement);
            } else if (cast && cast->type.bits() > cast->value.type().bits()) {
                // Widening casts get pushed inwards, narrowing casts
                // stay outside. This keeps the temporaries small, and
                // helps with peephole optimizations in codegen that
                // skip the widening entirely.
                new_value = cast->value;
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, Cast::make(cast->type, new_var), replacement);
            } else if (shuffle && shuffle->is_slice()) {
                // Replacing new_value below might free the shuffle
                // indices vector, so save them now.
                std::vector<int> slice_indices = shuffle->indices;
                new_value = Shuffle::make_concat(shuffle->vectors);
                new_var = Variable::make(new_value.type(), new_name);
                replacement = substitute(new_name, Shuffle::make({new_var}, slice_indices), replacement);
            } else if (shuffle && shuffle->is_concat() &&
                       ((var_a && !var_b) || (!var_a && var_b))) {
                new_var = Variable::make(var_a ? shuffle->vectors[1].type() : shuffle->vectors[0].type(), new_name);
                Expr op_a = var_a ? shuffle->vectors[0] : new_var;
                Expr op_b = var_a ? new_var : shuffle->vectors[1];
                replacement = substitute(new_name, Shuffle::make_concat({op_a, op_b}), replacement);
                new_value = var_a ? shuffle->vectors[1] : shuffle->vectors[0];
            } else {
                break;
            }
        }

        if (new_value.same_as(value)) {
            // Nothing to substitute
            new_value = Expr();
            replacement = Expr();
        } else {
            debug(4) << "new let " << new_name << " = " << new_value << " in ... " << replacement << " ...\n";
        }

        VarInfo info;
        info.old_uses = 0;
        info.new_uses = 0;
        info.replacement = replacement;

        var_info.push(op->name, info);

        // Before we enter the body, track the alignment info
        bool new_value_alignment_tracked = false, new_value_bounds_tracked = false;
        if (new_value.defined() && no_overflow_scalar_int(new_value.type())) {
            ModulusRemainder mod_rem = modulus_remainder(new_value, alignment_info);
            if (mod_rem.modulus > 1) {
                alignment_info.push(new_name, mod_rem);
                new_value_alignment_tracked = true;
            }
            int64_t val_min, val_max;
            if (const_int_bounds(new_value, &val_min, &val_max)) {
                bounds_info.push(new_name, { val_min, val_max });
                new_value_bounds_tracked = true;
            }
        }
        bool value_alignment_tracked = false, value_bounds_tracked = false;;
        if (no_overflow_scalar_int(value.type())) {
            ModulusRemainder mod_rem = modulus_remainder(value, alignment_info);
            if (mod_rem.modulus > 1) {
                alignment_info.push(op->name, mod_rem);
                value_alignment_tracked = true;
            }
            int64_t val_min, val_max;
            if (const_int_bounds(value, &val_min, &val_max)) {
                bounds_info.push(op->name, { val_min, val_max });
                value_bounds_tracked = true;
            }
        }

        body = mutate(body);

        if (value_alignment_tracked) {
            alignment_info.pop(op->name);
        }
        if (value_bounds_tracked) {
            bounds_info.pop(op->name);
        }
        if (new_value_alignment_tracked) {
            alignment_info.pop(new_name);
        }
        if (new_value_bounds_tracked) {
            bounds_info.pop(new_name);
        }

        info = var_info.get(op->name);
        var_info.pop(op->name);

        Body result = body;

        if (new_value.defined() && info.new_uses > 0) {
            // The new name/value may be used
            result = T::make(new_name, new_value, result);
        }

        if (info.old_uses > 0) {
            // The old name is still in use. We'd better keep it as well.
            result = T::make(op->name, value, result);
        }

        // Don't needlessly make a new Let/LetStmt node.  (Here's a
        // piece of template syntax I've never needed before).
        const T *new_op = result.template as<T>();
        if (new_op &&
            new_op->name == op->name &&
            new_op->body.same_as(op->body) &&
            new_op->value.same_as(op->value)) {
            return op;
        }

        return result;

    }


    Expr visit(const Let *op) override {
        if (simplify_lets) {
            return simplify_let<Let, Expr>(op);
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        if (simplify_lets) {
            return simplify_let<LetStmt, Stmt>(op);
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const AssertStmt *op) override {
        Stmt stmt = IRMutator2::visit(op);

        const AssertStmt *a = stmt.as<AssertStmt>();
        if (a && is_zero(a->condition)) {
            // Usually, assert(const-false) should generate a warning;
            // in at least one case (specialize_fail()), we want to suppress
            // the warning, because the assertion is generated internally
            // by Halide and is expected to always fail.
            const Call *call = a->message.as<Call>();
            const bool const_false_conditions_expected =
                call && call->name == "halide_error_specialize_fail";
            if (!const_false_conditions_expected) {
                user_warning << "This pipeline is guaranteed to fail an assertion at runtime: \n"
                             << stmt << "\n";
            }
        } else if (a && is_one(a->condition)) {
            stmt = Evaluate::make(0);
        }
        return stmt;
    }


    Stmt visit(const For *op) override {
        Expr new_min = mutate(op->min);
        Expr new_extent = mutate(op->extent);

        int64_t new_min_int, new_extent_int;
        bool bounds_tracked = false;
        if (const_int(new_min, &new_min_int) &&
            const_int(new_extent, &new_extent_int)) {
            bounds_tracked = true;
            int64_t new_max_int = new_min_int + new_extent_int - 1;
            bounds_info.push(op->name, { new_min_int, new_max_int });
        }

        Stmt new_body = mutate(op->body);

        if (bounds_tracked) {
            bounds_info.pop(op->name);
        }

        if (is_no_op(new_body)) {
            return new_body;
        } else if (op->min.same_as(new_min) &&
            op->extent.same_as(new_extent) &&
            op->body.same_as(new_body)) {
            return op;
        } else {
            return For::make(op->name, new_min, new_extent, op->for_type, op->device_api, new_body);
        }
    }

    Stmt visit(const Provide *op) override {
        found_buffer_reference(op->name, op->args.size());
        return IRMutator2::visit(op);
    }

    Stmt visit(const Store *op) override {
        found_buffer_reference(op->name);

        Expr predicate = mutate(op->predicate);
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);

        const Load *load = value.as<Load>();
        const Broadcast *scalar_pred = predicate.as<Broadcast>();

        if (is_zero(predicate)) {
            // Predicate is always false
            return Evaluate::make(0);
        } else if (scalar_pred && !is_one(scalar_pred->value)) {
            return IfThenElse::make(scalar_pred->value,
                                    Store::make(op->name, value, index, op->param, const_true(value.type().lanes())));
        } else if (is_undef(value) || (load && load->name == op->name && equal(load->index, index))) {
            // foo[x] = foo[x] or foo[x] = undef is a no-op
            return Evaluate::make(0);
        } else if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
            return op;
        } else {
            return Store::make(op->name, value, index, op->param, predicate);
        }
    }

    Stmt visit(const Allocate *op) override {
        std::vector<Expr> new_extents;
        bool all_extents_unmodified = true;
        for (size_t i = 0; i < op->extents.size(); i++) {
            new_extents.push_back(mutate(op->extents[i]));
            all_extents_unmodified &= new_extents[i].same_as(op->extents[i]);
        }
        Stmt body = mutate(op->body);
        Expr condition = mutate(op->condition);
        Expr new_expr;
        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
        }
        const IfThenElse *body_if = body.as<IfThenElse>();
        if (body_if &&
            op->condition.defined() &&
            equal(op->condition, body_if->condition)) {
            // We can move the allocation into the if body case. The
            // else case must not use it.
            Stmt stmt = Allocate::make(op->name, op->type, op->memory_type,
                                       new_extents, condition, body_if->then_case,
                                       new_expr, op->free_function);
            return IfThenElse::make(body_if->condition, stmt, body_if->else_case);
        } else if (all_extents_unmodified &&
                   body.same_as(op->body) &&
                   condition.same_as(op->condition) &&
                   new_expr.same_as(op->new_expr)) {
            return op;
        } else {
            return Allocate::make(op->name, op->type, op->memory_type,
                                  new_extents, condition, body,
                                  new_expr, op->free_function);
        }
    }

    Stmt visit(const Evaluate *op) override {
        Expr value = mutate(op->value);

        // Rewrite Lets inside an evaluate as LetStmts outside the Evaluate.
        vector<pair<string, Expr>> lets;
        while (const Let *let = value.as<Let>()) {
            lets.push_back({let->name, let->value});
            value = let->body;
        }

        if (value.same_as(op->value)) {
            internal_assert(lets.empty());
            return op;
        } else {
            // Rewrap the lets outside the evaluate node
            Stmt stmt = Evaluate::make(value);
            for (size_t i = lets.size(); i > 0; i--) {
                stmt = LetStmt::make(lets[i-1].first, lets[i-1].second, stmt);
            }
            return stmt;
        }
    }

    Stmt visit(const ProducerConsumer *op) override {
        Stmt body = mutate(op->body);

        if (is_no_op(body)) {
            return Evaluate::make(0);
        } else if (body.same_as(op->body)) {
            return op;
        } else {
            return ProducerConsumer::make(op->name, op->is_producer, body);
        }
    }

    Stmt visit(const Block *op) override {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);

        // Check if both halves start with a let statement.
        const LetStmt *let_first = first.as<LetStmt>();
        const LetStmt *let_rest = rest.as<LetStmt>();
        const IfThenElse *if_first = first.as<IfThenElse>();
        const IfThenElse *if_rest = rest.as<IfThenElse>();

        if (is_no_op(first) &&
            is_no_op(rest)) {
            return Evaluate::make(0);
        } else if (is_no_op(first)) {
            return rest;
        } else if (is_no_op(rest)) {
            return first;
        } else if (let_first &&
                   let_rest &&
                   equal(let_first->value, let_rest->value) &&
                   is_pure(let_first->value)) {

            // Do both first and rest start with the same let statement (occurs when unrolling).
            Stmt new_block = mutate(Block::make(let_first->body, let_rest->body));

            // We need to make a new name since we're pulling it out to a
            // different scope.
            string var_name = unique_name('t');
            Expr new_var = Variable::make(let_first->value.type(), var_name);
            new_block = substitute(let_first->name, new_var, new_block);
            new_block = substitute(let_rest->name, new_var, new_block);

            return LetStmt::make(var_name, let_first->value, new_block);
        } else if (if_first &&
                   if_rest &&
                   equal(if_first->condition, if_rest->condition) &&
                   is_pure(if_first->condition)) {
            // Two ifs with matching conditions
            Stmt then_case = mutate(Block::make(if_first->then_case, if_rest->then_case));
            Stmt else_case;
            if (if_first->else_case.defined() && if_rest->else_case.defined()) {
                else_case = mutate(Block::make(if_first->else_case, if_rest->else_case));
            } else if (if_first->else_case.defined()) {
                // We already simplified the body of the ifs.
                else_case = if_first->else_case;
            } else {
                else_case = if_rest->else_case;
            }
            return IfThenElse::make(if_first->condition, then_case, else_case);
        } else if (if_first &&
                   if_rest &&
                   !if_rest->else_case.defined() &&
                   is_pure(if_first->condition) &&
                   is_pure(if_rest->condition) &&
                   is_one(mutate((if_first->condition && if_rest->condition) == if_rest->condition))) {
            // Two ifs where the second condition is tighter than
            // the first condition.  The second if can be nested
            // inside the first one, because if it's true the
            // first one must also be true.
            Stmt then_case = mutate(Block::make(if_first->then_case, if_rest));
            Stmt else_case = mutate(if_first->else_case);
            return IfThenElse::make(if_first->condition, then_case, else_case);
        } else if (op->first.same_as(first) &&
                   op->rest.same_as(rest)) {
            return op;
        } else {
            return Block::make(first, rest);
        }
    }
};

Expr simplify(Expr e, bool simplify_lets,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(simplify_lets, &bounds, &alignment).mutate(e);
}

Stmt simplify(Stmt s, bool simplify_lets,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(simplify_lets, &bounds, &alignment).mutate(s);
}

class SimplifyExprs : public IRMutator2 {
public:
    using IRMutator2::mutate;
    Expr mutate(const Expr &e) override {
        return simplify(e);
    }
};

Stmt simplify_exprs(Stmt s) {
    return SimplifyExprs().mutate(s);
}

bool can_prove(Expr e) {
    internal_assert(e.type().is_bool())
        << "Argument to can_prove is not a boolean Expr: " << e << "\n";
    e = simplify(e);
    // likely(const-bool) is deliberately left unsimplified, because
    // things like max(likely(1), x) are meaningful, but we do want to
    // have can_prove(likely(1)) return true.
    if (const Call *c = e.as<Call>()) {
        if (c->is_intrinsic(Call::likely)) {
            e = c->args[0];
        }
    }
    return is_one(e);
}

namespace {

void check(const Expr &a, const Expr &b) {
    //debug(0) << "Checking that " << a << " -> " << b << "\n";
    Expr simpler = simplify(a);
    if (!equal(simpler, b)) {
        internal_error
            << "\nSimplification failure:\n"
            << "Input: " << a << '\n'
            << "Output: " << simpler << '\n'
            << "Expected output: " << b << '\n';
    }
}

void check(const Stmt &a, const Stmt &b) {
    //debug(0) << "Checking that " << a << " -> " << b << "\n";
    Stmt simpler = simplify(a);
    if (!equal(simpler, b)) {
        internal_error
            << "\nSimplification failure:\n"
            << "Input: " << a << '\n'
            << "Output: " << simpler << '\n'
            << "Expected output: " << b << '\n';
    }
}

void check_in_bounds(const Expr &a, const Expr &b, const Scope<Interval> &bi) {
    //debug(0) << "Checking that " << a << " -> " << b << "\n";
    Expr simpler = simplify(a, true, bi);
    if (!equal(simpler, b)) {
        internal_error
            << "\nSimplification failure:\n"
            << "Input: " << a << '\n'
            << "Output: " << simpler << '\n'
            << "Expected output: " << b << '\n';
    }
}

// Helper functions to use in the tests below
Expr interleave_vectors(const vector<Expr> &e) {
    return Shuffle::make_interleave(e);
}

Expr concat_vectors(const vector<Expr> &e) {
    return Shuffle::make_concat(e);
}

Expr slice(const Expr &e, int begin, int stride, int w) {
    return Shuffle::make_slice(e, begin, stride, w);
}

Expr ramp(const Expr &base, const Expr &stride, int w) {
    return Ramp::make(base, stride, w);
}

Expr broadcast(const Expr &base, int w) {
    return Broadcast::make(base, w);
}

void check_casts() {
    Expr x = Var("x");

    check(cast(Int(32), cast(Int(32), x)), x);
    check(cast(Float(32), 3), 3.0f);
    check(cast(Int(32), 5.0f), 5);

    check(cast(Int(32), cast(Int(8), 3)), 3);
    check(cast(Int(32), cast(Int(8), 1232)), -48);

    // Check redundant casts
    check(cast(Float(32), cast(Float(64), x)), cast(Float(32), x));
    check(cast(Int(16), cast(Int(32), x)), cast(Int(16), x));
    check(cast(Int(16), cast(UInt(32), x)), cast(Int(16), x));
    check(cast(UInt(16), cast(Int(32), x)), cast(UInt(16), x));
    check(cast(UInt(16), cast(UInt(32), x)), cast(UInt(16), x));

    // Check evaluation of constant expressions involving casts
    check(cast(UInt(16), 53) + cast(UInt(16), 87), make_const(UInt(16), 140));
    check(cast(Int(8), 127) + cast(Int(8), 1), make_const(Int(8), -128));
    check(cast(UInt(16), -1) - cast(UInt(16), 1), make_const(UInt(16), 65534));
    check(cast(Int(16), 4) * cast(Int(16), -5), make_const(Int(16), -20));
    check(cast(Int(16), 16) / cast(Int(16), 4), make_const(Int(16), 4));
    check(cast(Int(16), 23) % cast(Int(16), 5), make_const(Int(16), 3));
    check(min(cast(Int(16), 30000), cast(Int(16), -123)), make_const(Int(16), -123));
    check(max(cast(Int(16), 30000), cast(Int(16), 65000)), make_const(Int(16), 30000));
    check(cast(UInt(16), -1) == cast(UInt(16), 65535), const_true());
    check(cast(UInt(16), 65) == cast(UInt(16), 66), const_false());
    check(cast(UInt(16), -1) < cast(UInt(16), 65535), const_false());
    check(cast(UInt(16), 65) < cast(UInt(16), 66), const_true());
    check(cast(UInt(16), 123.4f), make_const(UInt(16), 123));
    check(cast(Float(32), cast(UInt(16), 123456.0f)), 57920.0f);
    // Specific checks for 32 bit unsigned expressions - ensure simplifications are actually unsigned.
    // 4000000000 (4 billion) is less than 2^32 but more than 2^31.  As an int, it is negative.
    check(cast(UInt(32), (int) 4000000000UL) + cast(UInt(32), 5), make_const(UInt(32), (int) 4000000005UL));
    check(cast(UInt(32), (int) 4000000000UL) - cast(UInt(32), 5), make_const(UInt(32), (int) 3999999995UL));
    check(cast(UInt(32), (int) 4000000000UL) / cast(UInt(32), 5), make_const(UInt(32), 800000000));
    check(cast(UInt(32), 800000000) * cast(UInt(32), 5), make_const(UInt(32), (int) 4000000000UL));
    check(cast(UInt(32), (int) 4000000023UL) % cast(UInt(32), 100), make_const(UInt(32), 23));
    check(min(cast(UInt(32), (int) 4000000023UL) , cast(UInt(32), 1000)), make_const(UInt(32), (int) 1000));
    check(max(cast(UInt(32), (int) 4000000023UL) , cast(UInt(32), 1000)), make_const(UInt(32), (int) 4000000023UL));
    check(cast(UInt(32), (int) 4000000023UL) < cast(UInt(32), 1000), const_false());
    check(cast(UInt(32), (int) 4000000023UL) == cast(UInt(32), 1000), const_false());

    check(cast(Float(64), 0.5f), Expr(0.5));
    check((x - cast(Float(64), 0.5f)) * (x - cast(Float(64), 0.5f)),
          (x + Expr(-0.5)) * (x + Expr(-0.5)));

    check(cast(Int(64, 3), ramp(5.5f, 2.0f, 3)),
          cast(Int(64, 3), ramp(5.5f, 2.0f, 3)));
    check(cast(Int(64, 3), ramp(x, 2, 3)),
          ramp(cast(Int(64), x), cast(Int(64), 2), 3));

    // Check cancellations can occur through casts
    check(cast(Int(64), x + 1) - cast(Int(64), x), cast(Int(64), 1));
    check(cast(Int(64), 1 + x) - cast(Int(64), x), cast(Int(64), 1));
    // But only when overflow is undefined for the type
    check(cast(UInt(8), x + 1) - cast(UInt(8), x),
          cast(UInt(8), x + 1) - cast(UInt(8), x));
}

void check_algebra() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w"), v = Var("v");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();

    check(3 + x, x + 3);
    check(x + 0, x);
    check(0 + x, x);
    check(Expr(ramp(x, 2, 3)) + Expr(ramp(y, 4, 3)), ramp(x+y, 6, 3));
    check(Expr(broadcast(4.0f, 5)) + Expr(ramp(3.25f, 4.5f, 5)), ramp(7.25f, 4.5f, 5));
    check(Expr(ramp(3.25f, 4.5f, 5)) + Expr(broadcast(4.0f, 5)), ramp(7.25f, 4.5f, 5));
    check(Expr(broadcast(3, 3)) + Expr(broadcast(1, 3)), broadcast(4, 3));
    check((x + 3) + 4, x + 7);
    check(4 + (3 + x), x + 7);
    check((x + 3) + y, (x + y) + 3);
    check(y + (x + 3), (y + x) + 3);
    check((3 - x) + x, 3);
    check(x + (3 - x), 3);
    check(x*y + x*z, x*(y+z));
    check(x*y + z*x, x*(y+z));
    check(y*x + x*z, x*(y+z));
    check(y*x + z*x, x*(y+z));

    check(x - 0, x);
    check((x/y) - (x/y), 0);
    check(x - 2, x + (-2));
    check(Expr(ramp(x, 2, 3)) - Expr(ramp(y, 4, 3)), ramp(x-y, -2, 3));
    check(Expr(broadcast(4.0f, 5)) - Expr(ramp(3.25f, 4.5f, 5)), ramp(0.75f, -4.5f, 5));
    check(Expr(ramp(3.25f, 4.5f, 5)) - Expr(broadcast(4.0f, 5)), ramp(-0.75f, 4.5f, 5));
    check(Expr(broadcast(3, 3)) - Expr(broadcast(1, 3)), broadcast(2, 3));
    check((x + y) - x, y);
    check((x + y) - y, x);
    check(x - (x + y), 0 - y);
    check(x - (y + x), 0 - y);
    check((x + 3) - 2, x + 1);
    check((x + 3) - y, (x - y) + 3);
    check((x - 3) - y, (x - y) + (-3));
    check(x - (y - 2), (x - y) + 2);
    check(3 - (y - 2), 5 - y);
    check(x - (0 - y), x + y);
    check(x + (0 - y), x - y);
    check((0 - x) + y, y - x);
    check(x*y - x*z, x*(y-z));
    check(x*y - z*x, x*(y-z));
    check(y*x - x*z, x*(y-z));
    check(y*x - z*x, x*(y-z));
    check(x - y*-2, x + y*2);
    check(x + y*-2, x - y*2);
    check(x*-2 + y, y - x*2);
    check(xf - yf*-2.0f, xf + y*2.0f);
    check(xf + yf*-2.0f, xf - y*2.0f);
    check(xf*-2.0f + yf, yf - x*2.0f);

    check((x * 8) - (y * 4), (x * 2 - y) * 4);
    check((x * 4) - (y * 8), (x - y * 2) * 4);

    check((x * 2) % 6, (x % 3) * 2);

    check(x - (x/8)*8, x % 8);
    check((x/8)*8 - x, -(x % 8));
    check((x/8)*8 < x + y, 0 < x%8 + y);
    check((x/8)*8 < x - y, y < x%8);
    check((x/8)*8 < x, 0 < x%8);
    check(((x+3)/8)*8 < x + y, 3 < (x+3)%8 + y);
    check(((x+3)/8)*8 < x - y, y < (x+3)%8 + (-3));
    check(((x+3)/8)*8 < x, 3 < (x+3)%8);

    check(x*0, 0);
    check(0*x, 0);
    check(x*1, x);
    check(1*x, x);
    check(Expr(2.0f)*4.0f, 8.0f);
    check(Expr(2)*4, 8);
    check((3*x)*4, x*12);
    check(4*(3+x), x*4 + 12);
    check(Expr(broadcast(4.0f, 5)) * Expr(ramp(3.0f, 4.0f, 5)), ramp(12.0f, 16.0f, 5));
    check(Expr(ramp(3.0f, 4.0f, 5)) * Expr(broadcast(2.0f, 5)), ramp(6.0f, 8.0f, 5));
    check(Expr(broadcast(3, 3)) * Expr(broadcast(2, 3)), broadcast(6, 3));

    check(x*y + x, x*(y + 1));
    check(x*y - x, x*(y + -1));
    check(x + x*y, x*(y + 1));
    check(x - x*y, x*(1 - y));
    check(x*y + y, (x + 1)*y);
    check(x*y - y, (x + -1)*y);
    check(y + x*y, (x + 1)*y);
    check(y - x*y, (1 - x)*y);

    check(0/x, 0);
    check(x/1, x);
    check(x/x, 1);
    check((-1)/x, select(x < 0, 1, -1));
    check(Expr(7)/3, 2);
    check(Expr(6.0f)/2.0f, 3.0f);
    check((x / 3) / 4, x / 12);
    check((x*4)/2, x*2);
    check((x*2)/4, x/2);
    check((x*4 + y)/2, x*2 + y/2);
    check((y + x*4)/2, y/2 + x*2);
    check((x*4 - y)/2, x*2 + (0 - y)/2);
    check((y - x*4)/2, y/2 - x*2);
    check((x + 3)/2 + 7, (x + 17)/2);
    check((x/2 + 3)/5, (x + 6)/10);
    check((x + 8)/2, x/2 + 4);
    check((x - y)*-2, (y - x)*2);
    check((xf - yf)*-2.0f, (yf - xf)*2.0f);

    // Pull terms that are a multiple of the divisor out of a ternary expression
    check(((x*4 + y) + z) / 2, x*2 + (y + z)/2);
    check(((x*4 - y) + z) / 2, x*2 + (z - y)/2);
    check(((x*4 + y) - z) / 2, x*2 + (y - z)/2);
    check(((x*4 - y) - z) / 2, x*2 + (0 - y - z)/2);
    check((x + (y*4 + z)) / 2, y*2 + (x + z)/2);
    check((x + (y*4 - z)) / 2, y*2 + (x - z)/2);
    check((x - (y*4 + z)) / 2, (x - z)/2 - y*2);
    check((x - (y*4 - z)) / 2, (x + z)/2 - y*2);

    // Pull out the gcd of the numerator and divisor
    check((x * 12 + 5) / 9, (x * 4 + 1) / 3);
    check((x * 12 + 19) / 9, (x * 4) / 3 + 2);

    // Cancellations in non-const integer divisions
    check((x*y)/x, y);
    check((y*x)/x, y);
    check((x*y + z)/x, y + z/x);
    check((y*x + z)/x, y + z/x);
    check((z + x*y)/x, z/x + y);
    check((z + y*x)/x, z/x + y);
    check((x*y - z)/x, y + (-z)/x);
    check((y*x - z)/x, y + (-z)/x);
    check((z - x*y)/x, z/x - y);
    check((z - y*x)/x, z/x - y);

    check((x + y)/x, y/x + 1);
    check((y + x)/x, y/x + 1);
    check((x - y)/x, (-y)/x + 1);
    check((y - x)/x, y/x + (-1));

    check(((x + y) + z)/x, (y + z)/x + 1);
    check(((y + x) + z)/x, (y + z)/x + 1);
    check((y + (x + z))/x, (y + z)/x + 1);
    check((y + (z + x))/x, (y + z)/x + 1);

    check(xf / 4.0f, xf * 0.25f);

    // Some quaternary rules with cancellations
    check((x + y) - (z + y), x - z);
    check((x + y) - (y + z), x - z);
    check((y + x) - (z + y), x - z);
    check((y + x) - (y + z), x - z);

    check((x - y) - (z - y), x - z);
    check((y - z) - (y - x), x - z);

    check(((x + y) + z) - x, y + z);
    check(((x + y) + z) - y, x + z);
    check((x + (y + z)) - y, x + z);
    check((x + (y + z)) - z, x + y);

    check((x*8) % 4, 0);
    check((x*8 + y) % 4, y % 4);
    check((y + 8) % 4, y % 4);
    check((y + x*8) % 4, y % 4);
    check((y*16 + 13) % 2, 1);
    check((x*y) % 1, 0);

    // Check an optimization important for fusing dimensions
    check((x/3)*3 + x%3, x);
    check(x%3 + (x/3)*3, x);

    check(((x/3)*3 + y) + x%3, x + y);
    check((x%3 + y) + (x/3)*3, x + y);

    check((y + x%3) + (x/3)*3, y + x);
    check((y + (x/3*3)) + x%3, y + x);

    // Almost-cancellations through integer divisions. These rules all
    // deduplicate x and wrap it in a modulo operator, neutering it
    // for the purposes of bounds inference. Patterns below look
    // confusing, but were brute-force tested.
    check((x + 17)/3 - (x + 7)/3, ((x+1)%3 + 10)/3);
    check((x + 17)/3 - (x + y)/3, (19 - y - (x+2)%3)/3);
    check((x + y )/3 - (x + 7)/3, ((x+1)%3 + y + -7)/3);
    check( x      /3 - (x + y)/3, (2 - y - x % 3)/3);
    check((x + y )/3 -  x     /3, (x%3 + y)/3);
    check( x      /3 - (x + 7)/3, (-5 - x%3)/3);
    check((x + 17)/3 -  x     /3, (x%3 + 17)/3);
    check((x + 17)/3 - (x - y)/3, (y - (x+2)%3 + 19)/3);
    check((x - y )/3 - (x + 7)/3, ((x+1)%3 - y + (-7))/3);
    check( x      /3 - (x - y)/3, (y - x%3 + 2)/3);
    check((x - y )/3 -  x     /3, (x%3 - y)/3);

    // Check some specific expressions involving div and mod
    check(Expr(23) / 4, Expr(5));
    check(Expr(-23) / 4, Expr(-6));
    check(Expr(-23) / -4, Expr(6));
    check(Expr(23) / -4, Expr(-5));
    check(Expr(-2000000000) / 1000000001, Expr(-2));
    check(Expr(23) % 4, Expr(3));
    check(Expr(-23) % 4, Expr(1));
    check(Expr(-23) % -4, Expr(1));
    check(Expr(23) % -4, Expr(3));
    check(Expr(-2000000000) % 1000000001, Expr(2));

    check(Expr(3) + Expr(8), 11);
    check(Expr(3.25f) + Expr(7.75f), 11.0f);

    check(Expr(7) % 2, 1);
    check(Expr(7.25f) % 2.0f, 1.25f);
    check(Expr(-7.25f) % 2.0f, 0.75f);
    check(Expr(-7.25f) % -2.0f, -1.25f);
    check(Expr(7.25f) % -2.0f, -0.75f);

    check(2*x + (2*x + y)/5, (x*12 + y)/5);
    check(x + (x - y)/4, (x*5 - y)/4);
    check((x + z) + (y + (x + z))/3, ((x + z)*4 + y)/3);
    check(x + ((y + w) - x)/2, (x + (y + w))/2);
    check((x + y)/3 + x, (x*4 + y)/3);
    check((x - y)/4 + x, (x*5 - y)/4);
    check((y + x)/3 + x, (y + x*4)/3);
    check((y - x)/3 + x, (y + x*2)/3);
    check(1 + (1 + y)/2, (y + 3)/2);
    check((y + 1)/2 + 1, (y + 3)/2);
    check((0 - y)/5 + 1, (0 - y)/5 + 1);

    check(x - (x + y)/3, (x*2 - y + 2)/3);
    check((w + x) - ((w + x) - y*z)/3, ((w + x)*2 + y*z + 2)/3);
    check(x - (y + x)/2, (x - y + 1)/2);
    check(x - (y - x)/6, (x*7 - y + 5)/6);
    check(x - (x + y)/-3, x - (x + y)/-3);
    check((w + x) - ((w + x) - y*z)/-3, (w + x) - ((w + x) - y*z)/-3);
    check(x - (y + x)/-2, x - (y + x)/-2);
    check(x - (y - x)/-6, x - (y - x)/-6);
    check((x + y)/3 - x, (y - x*2)/3);
    check((x*y - w)/4 - x*y, (x*y*(-3) - w)/4);
    check((y + x)/5 - x, (y - x*4)/5);
    check((y - x)/6 - x, (y - x*7)/6);
    check(1 - (1 + y)/2 - 1, (0 - y)/2);
    check(1 - (-y + 1)/2 - 1, y/2);
    check(1 - (0 - y)/5, (y + 9)/5);
}

void check_vectors() {
    Expr x = Var("x"), y = Var("y"), z = Var("z");

    check(Expr(broadcast(y, 4)) / Expr(broadcast(x, 4)),
          Expr(broadcast(y/x, 4)));
    check(Expr(ramp(x, 4, 4)) / 2, ramp(x/2, 2, 4));
    check(Expr(ramp(x, -4, 7)) / 2, ramp(x/2, -2, 7));
    check(Expr(ramp(x, 4, 5)) / -2, ramp(x/-2, -2, 5));
    check(Expr(ramp(x, -8, 5)) / -2, ramp(x/-2, 4, 5));

    check(Expr(ramp(4*x, 1, 4)) / 4, broadcast(x, 4));
    check(Expr(ramp(x*4, 1, 3)) / 4, broadcast(x, 3));
    check(Expr(ramp(x*8, 2, 4)) / 8, broadcast(x, 4));
    check(Expr(ramp(x*8, 3, 3)) / 8, broadcast(x, 3));
    check(Expr(ramp(0, 1, 8)) % 16, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(8, 1, 8)) % 16, Expr(ramp(8, 1, 8)));
    check(Expr(ramp(9, 1, 8)) % 16, Expr(ramp(9, 1, 8)) % 16);
    check(Expr(ramp(16, 1, 8)) % 16, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(0, 1, 8)) % 8, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(x*8+17, 1, 4)) % 8, Expr(ramp(1, 1, 4)));
    check(Expr(ramp(x*8+17, 1, 8)) % 8, Expr(ramp(1, 1, 8) % 8));


    check(Expr(broadcast(x, 4)) % Expr(broadcast(y, 4)),
          Expr(broadcast(x % y, 4)));
    check(Expr(ramp(x, 2, 4)) % (broadcast(2, 4)),
          broadcast(x % 2, 4));
    check(Expr(ramp(2*x+1, 4, 4)) % (broadcast(2, 4)),
          broadcast(1, 4));

    check(max(broadcast(24, 2), broadcast(x, 2) % ramp(-8, -33, 2)),
          max(broadcast(x, 2) % ramp(-8, -33, 2), broadcast(24, 2)));
    check(max(broadcast(41, 2), broadcast(x, 2) % ramp(-8, -33, 2)),
          broadcast(41, 2));

    check(ramp(0, 1, 4) == broadcast(2, 4),
          ramp(-2, 1, 4) == broadcast(0, 4));

    {
        Expr test = select(ramp(const_true(), const_true(), 2),
                           ramp(const_false(), const_true(), 2),
                           broadcast(const_false(), 2)) ==
                    broadcast(const_false(), 2);
        Expr expected = !(ramp(const_true(), const_true(), 2)) ||
                        (ramp(const_false(), const_true(), 2) == broadcast(const_false(), 2));
        check(test, expected);
    }

    {
        Expr test = select(ramp(const_true(), const_true(), 2),
                           broadcast(const_true(), 2),
                           ramp(const_false(), const_true(), 2)) ==
                    broadcast(const_false(), 2);
        Expr expected = (!ramp(const_true(), const_true(), 2)) &&
                        (ramp(const_false(), const_true(), 2) == broadcast(const_false(), 2));
        check(test, expected);
    }
}

void check_bounds() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w");

    check(min(Expr(7), 3), 3);
    check(min(Expr(4.25f), 1.25f), 1.25f);
    check(min(broadcast(x, 4), broadcast(y, 4)),
          broadcast(min(x, y), 4));
    check(min(x, x+3), x);
    check(min(x+4, x), x);
    check(min(x-1, x+2), x+(-1));
    check(min(7, min(x, 3)), min(x, 3));
    check(min(min(x, y), x), min(x, y));
    check(min(min(x, y), y), min(x, y));
    check(min(x, min(x, y)), min(x, y));
    check(min(y, min(x, y)), min(x, y));

    check(max(Expr(7), 3), 7);
    check(max(Expr(4.25f), 1.25f), 4.25f);
    check(max(broadcast(x, 4), broadcast(y, 4)),
          broadcast(max(x, y), 4));
    check(max(x, x+3), x+3);
    check(max(x+4, x), x+4);
    check(max(x-1, x+2), x+2);
    check(max(7, max(x, 3)), max(x, 7));
    check(max(max(x, y), x), max(x, y));
    check(max(max(x, y), y), max(x, y));
    check(max(x, max(x, y)), max(x, y));
    check(max(y, max(x, y)), max(x, y));

    // Check that simplifier can recognise instances where the extremes of the
    // datatype appear as constants in comparisons, Min and Max expressions.
    // The result of min/max with extreme is known to be either the extreme or
    // the other expression.  The result of < or > comparison is known to be true or false.
    check(x <= Int(32).max(), const_true());
    check(cast(Int(16), x) >= Int(16).min(), const_true());
    check(x < Int(32).min(), const_false());
    check(min(cast(UInt(16), x), cast(UInt(16), 65535)), cast(UInt(16), x));
    check(min(x, Int(32).max()), x);
    check(min(Int(32).min(), x), Int(32).min());
    check(max(cast(Int(8), x), cast(Int(8), -128)), cast(Int(8), x));
    check(max(x, Int(32).min()), x);
    check(max(x, Int(32).max()), Int(32).max());
    // Check that non-extremes do not lead to incorrect simplification
    check(max(cast(Int(8), x), cast(Int(8), -127)), max(cast(Int(8), x), make_const(Int(8), -127)));

    // Some quaternary rules with cancellations
    check((x + y) - (z + y), x - z);
    check((x + y) - (y + z), x - z);
    check((y + x) - (z + y), x - z);
    check((y + x) - (y + z), x - z);

    check((x - y) - (z - y), x - z);
    check((y - z) - (y - x), x - z);

    check((x + 3) / 4 - (x + 2) / 4, ((x + 2) % 4 + 1)/4);

    check(x - min(x + y, z), max(-y, x-z));
    check(x - min(y + x, z), max(-y, x-z));
    check(x - min(z, x + y), max(-y, x-z));
    check(x - min(z, y + x), max(-y, x-z));

    check(min(x + y, z) - x, min(y, z-x));
    check(min(y + x, z) - x, min(y, z-x));
    check(min(z, x + y) - x, min(y, z-x));
    check(min(z, y + x) - x, min(y, z-x));

    check(min(x + y, z + y), min(x, z) + y);
    check(min(y + x, z + y), min(x, z) + y);
    check(min(x + y, y + z), min(x, z) + y);
    check(min(y + x, y + z), min(x, z) + y);

    check(min(x, y) - min(y, x), 0);
    check(max(x, y) - max(y, x), 0);

    check(min(123 - x, 1 - x), 1 - x);
    check(max(123 - x, 1 - x), 123 - x);

    check(min(x*43, y*43), min(x, y)*43);
    check(max(x*43, y*43), max(x, y)*43);
    check(min(x*-43, y*-43), max(x, y)*-43);
    check(max(x*-43, y*-43), min(x, y)*-43);

    check(min(min(x, 4), y), min(min(x, y), 4));
    check(max(max(x, 4), y), max(max(x, y), 4));

    check(min(x*8, 24), min(x, 3)*8);
    check(max(x*8, 24), max(x, 3)*8);
    check(min(x*-8, 24), max(x, -3)*-8);
    check(max(x*-8, 24), min(x, -3)*-8);

    check(min(clamp(x, -10, 14), clamp(y, -10, 14)), clamp(min(x, y), -10, 14));

    check(min(x/4, y/4), min(x, y)/4);
    check(max(x/4, y/4), max(x, y)/4);

    check(min(x/(-4), y/(-4)), max(x, y)/(-4));
    check(max(x/(-4), y/(-4)), min(x, y)/(-4));

    // Min and max of clamped expressions
    check(min(clamp(x+1, y, z), clamp(x-1, y, z)), clamp(x+(-1), y, z));
    check(max(clamp(x+1, y, z), clamp(x-1, y, z)), clamp(x+1, y, z));

    // Additions that cancel a term inside a min or max
    check(x + min(y - x, z), min(y, z + x));
    check(x + max(y - x, z), max(y, z + x));
    check(min(y + (-2), z) + 2, min(y, z + 2));
    check(max(y + (-2), z) + 2, max(y, z + 2));

    check(x + min(y - x, z), min(y, z + x));
    check(x + max(y - x, z), max(y, z + x));
    check(min(y + (-2), z) + 2, min(y, z + 2));
    check(max(y + (-2), z) + 2, max(y, z + 2));

    // Min/Max distributive law
    check(max(max(x, y), max(x, z)), max(max(y, z), x));
    check(min(max(x, y), max(x, z)), max(min(y, z), x));
    check(min(min(x, y), min(x, z)), min(min(y, z), x));
    check(max(min(x, y), min(x, z)), min(max(y, z), x));

    // Mins of expressions and rounded up versions of them
    check(min(((x+7)/8)*8, x), x);
    check(min(x, ((x+7)/8)*8), x);

    check(min(((x+7)/8)*8, max(x, 8)), max(x, 8));
    check(min(max(x, 8), ((x+7)/8)*8), max(x, 8));

    check(min(x, likely(x)), likely(x));
    check(min(likely(x), x), likely(x));
    check(max(x, likely(x)), likely(x));
    check(max(likely(x), x), likely(x));
    check(select(x > y, likely(x), x), likely(x));
    check(select(x > y, x, likely(x)), likely(x));

    check(min(x + 1, y) - min(x, y - 1), 1);
    check(max(x + 1, y) - max(x, y - 1), 1);
    check(min(x + 1, y) - min(y - 1, x), 1);
    check(max(x + 1, y) - max(y - 1, x), 1);

    // min and max on constant ramp v broadcast
    check(max(ramp(0, 1, 8), 0), ramp(0, 1, 8));
    check(min(ramp(0, 1, 8), 7), ramp(0, 1, 8));
    check(max(ramp(0, 1, 8), 7), broadcast(7, 8));
    check(min(ramp(0, 1, 8), 0), broadcast(0, 8));
    check(min(ramp(0, 1, 8), 4), min(ramp(0, 1, 8), 4));

    check(max(ramp(7, -1, 8), 0), ramp(7, -1, 8));
    check(min(ramp(7, -1, 8), 7), ramp(7, -1, 8));
    check(max(ramp(7, -1, 8), 7), broadcast(7, 8));
    check(min(ramp(7, -1, 8), 0), broadcast(0, 8));
    check(min(ramp(7, -1, 8), 4), min(ramp(7, -1, 8), 4));

    check(max(0, ramp(0, 1, 8)), ramp(0, 1, 8));
    check(min(7, ramp(0, 1, 8)), ramp(0, 1, 8));

    check(min(8 - x, 2), 8 - max(x, 6));
    check(max(3, 77 - x), 77 - min(x, 74));
    check(min(max(8-x, 0), 8), 8 - max(min(x, 8), 0));

    check(x - min(x, 2), max(x + -2, 0));
    check(x - max(x, 2), min(x + -2, 0));
    check(min(x, 2) - x, 2 - max(x, 2));
    check(max(x, 2) - x, 2 - min(x, 2));
    check(x - min(2, x), max(x + -2, 0));
    check(x - max(2, x), min(x + -2, 0));
    check(min(2, x) - x, 2 - max(x, 2));
    check(max(2, x) - x, 2 - min(x, 2));

    check(max(min(x, y), x), x);
    check(max(min(x, y), y), y);
    check(min(max(x, y), x), x);
    check(min(max(x, y), y), y);
    check(max(min(x, y), x) + y, x + y);

    check(max(min(max(x, y), z), y), max(min(x, z), y));
    check(max(min(z, max(x, y)), y), max(min(x, z), y));
    check(max(y, min(max(x, y), z)), max(min(x, z), y));
    check(max(y, min(z, max(x, y))), max(min(x, z), y));

    check(max(min(max(y, x), z), y), max(min(x, z), y));
    check(max(min(z, max(y, x)), y), max(min(x, z), y));
    check(max(y, min(max(y, x), z)), max(min(x, z), y));
    check(max(y, min(z, max(y, x))), max(min(x, z), y));

    check(min(max(min(x, y), z), y), min(max(x, z), y));
    check(min(max(z, min(x, y)), y), min(max(x, z), y));
    check(min(y, max(min(x, y), z)), min(max(x, z), y));
    check(min(y, max(z, min(x, y))), min(max(x, z), y));

    check(min(max(min(y, x), z), y), min(max(x, z), y));
    check(min(max(z, min(y, x)), y), min(max(x, z), y));
    check(min(y, max(min(y, x), z)), min(max(x, z), y));
    check(min(y, max(z, min(y, x))), min(max(x, z), y));

    {
        Expr one = broadcast(cast(Int(16), 1), 64);
        Expr three = broadcast(cast(Int(16), 3), 64);
        Expr four = broadcast(cast(Int(16), 4), 64);
        Expr five = broadcast(cast(Int(16), 5), 64);
        Expr v1 = Variable::make(Int(16).with_lanes(64), "x");
        Expr v2 = Variable::make(Int(16).with_lanes(64), "y");

        // Bound: [-4, 4]
        std::vector<Expr> clamped = {
            max(min(v1, four), -four),
            max(-four, min(v1, four)),
            min(max(v1, -four), four),
            min(four, max(v1, -four)),
            clamp(v1, -four, four)
        };

        for (size_t i = 0; i < clamped.size(); ++i) {
            // min(v, 4) where v=[-4, 4] -> v
            check(min(clamped[i], four), simplify(clamped[i]));
            // min(v, 5) where v=[-4, 4] -> v
            check(min(clamped[i], five), simplify(clamped[i]));
            // min(v, 3) where v=[-4, 4] -> min(v, 3)
            check(min(clamped[i], three), simplify(min(clamped[i], three)));
            // min(v, -5) where v=[-4, 4] -> -5
            check(min(clamped[i], -five), simplify(-five));
        }

        for (size_t i = 0; i < clamped.size(); ++i) {
            // max(v, 4) where v=[-4, 4] -> 4
            check(max(clamped[i], four), simplify(four));
            // max(v, 5) where v=[-4, 4] -> 5
            check(max(clamped[i], five), simplify(five));
            // max(v, 3) where v=[-4, 4] -> max(v, 3)
            check(max(clamped[i], three), simplify(max(clamped[i], three)));
            // max(v, -5) where v=[-4, 4] -> v
            check(max(clamped[i], -five), simplify(clamped[i]));
        }

        for (size_t i = 0; i < clamped.size(); ++i) {
            // max(min(v, 5), -5) where v=[-4, 4] -> v
            check(max(min(clamped[i], five), -five), simplify(clamped[i]));
            // max(min(v, 5), 5) where v=[-4, 4] -> 5
            check(max(min(clamped[i], five), five), simplify(five));

            // max(min(v, -5), -5) where v=[-4, 4] -> -5
            check(max(min(clamped[i], -five), -five), simplify(-five));
            // max(min(v, -5), 5) where v=[-4, 4] -> 5
            check(max(min(clamped[i], -five), five), simplify(five));
            // max(min(v, -5), 3) where v=[-4, 4] -> 3
            check(max(min(clamped[2], -five), three), simplify(three));
        }

        // max(min(v, 5), 3) where v=[-4, 4] -> max(v, 3)
        check(max(min(clamped[2], five), three), simplify(max(clamped[2], three)));

        // max(min(v, 5), 3) where v=[-4, 4] -> max(v, 3) -> v=[3, 4]
        // There is simplification rule that will simplify max(max(min(x, 4), -4), 3)
        // further into max(min(x, 4), 3)
        check(max(min(clamped[0], five), three), simplify(max(min(v1, four), three)));

        for (size_t i = 0; i < clamped.size(); ++i) {
            // min(v + 1, 4) where v=[-4, 4] -> min(v + 1, 4)
            check(min(clamped[i] + one, four), simplify(min(clamped[i] + one, four)));
            // min(v + 1, 5) where v=[-4, 4] -> v + 1
            check(min(clamped[i] + one, five), simplify(clamped[i] + one));
            // min(v + 1, -4) where v=[-4, 4] -> -4
            check(min(clamped[i] + one, -four), simplify(-four));
            // max(min(v + 1, 4), -4) where v=[-4, 4] -> min(v + 1, 4)
            check(max(min(clamped[i] + one, four), -four), simplify(min(clamped[i] + one, four)));
        }
        for (size_t i = 0; i < clamped.size(); ++i) {
            // max(v + 1, 4) where v=[-4, 4] -> max(v + 1, 4)
            check(max(clamped[i] + one, four), simplify(max(clamped[i] + one, four)));
            // max(v + 1, 5) where v=[-4, 4] -> 5
            check(max(clamped[i] + one, five), simplify(five));
            // max(v + 1, -4) where v=[-4, 4] -> -v + 1
            check(max(clamped[i] + one, -four), simplify(clamped[i] + one));
            // min(max(v + 1, -4), 4) where v=[-4, 4] -> min(v + 1, 4)
            check(min(max(clamped[i] + one, -four), four), simplify(min(clamped[i] + one, four)));
        }

        Expr t1 = clamp(v1, one, four);
        Expr t2 = clamp(v1, -five, -four);
        check(min(max(min(v2, t1), t2), five), simplify(max(min(t1, v2), t2)));
    }

    {
        Expr xv = Variable::make(Int(16).with_lanes(64), "x");
        Expr yv = Variable::make(Int(16).with_lanes(64), "y");
        Expr zv = Variable::make(Int(16).with_lanes(64), "z");

        // min(min(x, broadcast(y, n)), broadcast(z, n))) -> min(x, broadcast(min(y, z), n))
        check(min(min(xv, broadcast(y, 64)), broadcast(z, 64)), min(xv, broadcast(min(y, z), 64)));
        // min(min(broadcast(x, n), y), broadcast(z, n))) -> min(y, broadcast(min(x, z), n))
        check(min(min(broadcast(x, 64), yv), broadcast(z, 64)), min(yv, broadcast(min(x, z), 64)));
        // min(broadcast(x, n), min(y, broadcast(z, n)))) -> min(y, broadcast(min(x, z), n))
        check(min(broadcast(x, 64), min(yv, broadcast(z, 64))), min(yv, broadcast(min(z, x), 64)));
        // min(broadcast(x, n), min(broadcast(y, n), z))) -> min(z, broadcast(min(x, y), n))
        check(min(broadcast(x, 64), min(broadcast(y, 64), zv)), min(zv, broadcast(min(y, x), 64)));

        // max(max(x, broadcast(y, n)), broadcast(z, n))) -> max(x, broadcast(max(y, z), n))
        check(max(max(xv, broadcast(y, 64)), broadcast(z, 64)), max(xv, broadcast(max(y, z), 64)));
        // max(max(broadcast(x, n), y), broadcast(z, n))) -> max(y, broadcast(max(x, z), n))
        check(max(max(broadcast(x, 64), yv), broadcast(z, 64)), max(yv, broadcast(max(x, z), 64)));
        // max(broadcast(x, n), max(y, broadcast(z, n)))) -> max(y, broadcast(max(x, z), n))
        check(max(broadcast(x, 64), max(yv, broadcast(z, 64))), max(yv, broadcast(max(z, x), 64)));
        // max(broadcast(x, n), max(broadcast(y, n), z))) -> max(z, broadcast(max(x, y), n))
        check(max(broadcast(x, 64), max(broadcast(y, 64), zv)), max(zv, broadcast(max(y, x), 64)));
    }

    // Pull out common addition term inside min/max
    check(min((x + y) + z, x + w), min(y + z, w) + x);
    check(min((y + x) + z, x + w), min(y + z, w) + x);
    check(min(x + y, (x + z) + w), min(y, z + w) + x);
    check(min(x + y, (z + x) + w), min(y, z + w) + x);
    check(min(x + (y + z), y + w), min(x + z, w) + y);
    check(min(x + (z + y), y + w), min(x + z, w) + y);
    check(min(x + y, z + (x + w)), min(y, z + w) + x);
    check(min(x + y, z + (w + x)), min(y, z + w) + x);
    check(min(x + y/2 + 13, x + (0 - y)/2), min(y/2 + 13, (0 - y)/2) + x);

    check(max((x + y) + z, x + w), max(y + z, w) + x);
    check(max((y + x) + z, x + w), max(y + z, w) + x);
    check(max(x + y, (x + z) + w), max(y, z + w) + x);
    check(max(x + y, (z + x) + w), max(y, z + w) + x);
    check(max(x + (y + z), y + w), max(x + z, w) + y);
    check(max(x + (z + y), y + w), max(x + z, w) + y);
    check(max(x + y, z + (x + w)), max(y, z + w) + x);
    check(max(x + y, z + (w + x)), max(y, z + w) + x);
}

void check_boolean() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();
    Expr b1 = Variable::make(Bool(), "b1");
    Expr b2 = Variable::make(Bool(), "b2");

    check(x == x, t);
    check(x == (x+1), f);
    check(x-2 == y+3, (x-y) == 5);
    check(x+y == y+z, x == z);
    check(y+x == y+z, x == z);
    check(x+y == z+y, x == z);
    check(y+x == z+y, x == z);
    check((y+x)*17 == (z+y)*17, x == z);
    check(x*0 == y*0, t);
    check(x == x+y, y == 0);
    check(x+y == x, y == 0);
    check(100 - x == 99 - y, (y-x) == -1);

    check(x < x, f);
    check(x < (x+1), t);
    check(x-2 < y+3, x < y+5);
    check(x+y < y+z, x < z);
    check(y+x < y+z, x < z);
    check(x+y < z+y, x < z);
    check(y+x < z+y, x < z);
    check((y+x)*17 < (z+y)*17, x < z);
    check(x*0 < y*0, f);
    check(x < x+y, 0 < y);
    check(x+y < x, y < 0);

    check(select(x < 3, 2, 2), 2);
    check(select(x < (x+1), 9, 2), 9);
    check(select(x > (x+1), 9, 2), 2);
    // Selects of comparisons should always become selects of LT or selects of EQ
    check(select(x != 5, 2, 3), select(x == 5, 3, 2));
    check(select(x >= 5, 2, 3), select(x < 5, 3, 2));
    check(select(x <= 5, 2, 3), select(5 < x, 3, 2));
    check(select(x > 5, 2, 3), select(5 < x, 2, 3));

    check(select(x > 5, 2, 3) + select(x > 5, 6, 2), select(5 < x, 8, 5));
    check(select(x > 5, 8, 3) - select(x > 5, 6, 2), select(5 < x, 2, 1));

    check(select(x < 5, select(x < 5, 0, 1), 2), select(x < 5, 0, 2));
    check(select(x < 5, 0, select(x < 5, 1, 2)), select(x < 5, 0, 2));

    check((1 - xf)*6 < 3, 0.5f < xf);

    check(!f, t);
    check(!t, f);
    check(!(x < y), y <= x);
    check(!(x > y), x <= y);
    check(!(x >= y), x < y);
    check(!(x <= y), y < x);
    check(!(x == y), x != y);
    check(!(x != y), x == y);
    check(!(!(x == 0)), x == 0);
    check(!Expr(broadcast(x > y, 4)),
          broadcast(x <= y, 4));

    check(b1 || !b1, t);
    check(!b1 || b1, t);
    check(b1 && !b1, f);
    check(!b1 && b1, f);
    check(b1 && b1, b1);
    check(b1 || b1, b1);
    check(broadcast(b1, 4) || broadcast(!b1, 4), broadcast(t, 4));
    check(broadcast(!b1, 4) || broadcast(b1, 4), broadcast(t, 4));
    check(broadcast(b1, 4) && broadcast(!b1, 4), broadcast(f, 4));
    check(broadcast(!b1, 4) && broadcast(b1, 4), broadcast(f, 4));
    check(broadcast(b1, 4) && broadcast(b1, 4), broadcast(b1, 4));
    check(broadcast(b1, 4) || broadcast(b1, 4), broadcast(b1, 4));

    check((x == 1) && (x != 2), (x == 1));
    check((x != 1) && (x == 2), (x == 2));
    check((x == 1) && (x != 1), f);
    check((x != 1) && (x == 1), f);

    check((x == 1) || (x != 2), (x != 2));
    check((x != 1) || (x == 2), (x != 1));
    check((x == 1) || (x != 1), t);
    check((x != 1) || (x == 1), t);

    check(x < 20 || x > 19, t);
    check(x > 19 || x < 20, t);
    check(x < 20 || x > 20, x < 20 || 20 < x);
    check(x > 20 || x < 20, 20 < x || x < 20);
    check(x < 20 && x > 19, f);
    check(x > 19 && x < 20, f);
    check(x < 20 && x > 18, x < 20 && 18 < x);
    check(x > 18 && x < 20, 18 < x && x < 20);

    check(x <= 20 || x > 19, t);
    check(x > 19 || x <= 20, t);
    check(x <= 18 || x > 20, x <= 18 || 20 < x);
    check(x > 20 || x <= 18, 20 < x || x <= 18);
    check(x <= 18 && x > 19, f);
    check(x > 19 && x <= 18, f);
    check(x <= 20 && x > 19, x <= 20 && 19 < x);
    check(x > 19 && x <= 20, 19 < x && x <= 20);

    check(x < 20 || x >= 19, t);
    check(x >= 19 || x < 20, t);
    check(x < 18 || x >= 20, x < 18 || 20 <= x);
    check(x >= 20 || x < 18, 20 <= x || x < 18);
    check(x < 18 && x >= 19, f);
    check(x >= 19 && x < 18, f);
    check(x < 20 && x >= 19, x < 20 && 19 <= x);
    check(x >= 19 && x < 20, 19 <= x && x < 20);

    check(x <= 20 || x >= 21, t);
    check(x >= 21 || x <= 20, t);
    check(x <= 18 || x >= 20, x <= 18 || 20 <= x);
    check(x >= 20 || x <= 18, 20 <= x || x <= 18);
    check(x <= 18 && x >= 19, f);
    check(x >= 19 && x <= 18, f);
    check(x <= 20 && x >= 20, x <= 20 && 20 <= x);
    check(x >= 20 && x <= 20, 20 <= x && x <= 20);

    // check for substitution patterns
    check((b1 == t) && (b1 && b2), (b1 == t) && b2);
    check((b1 && b2) && (b1 == t), b2 && (b1 == t));

    {
        Expr i = Variable::make(Int(32), "i");
        check((i!=2 && (i!=4 && (i!=8 && i!=16))) || (i==16), (i!=2 && (i!=4 && (i!=8))));
        check((i==16) || (i!=2 && (i!=4 && (i!=8 && i!=16))), (i!=2 && (i!=4 && (i!=8))));
    }

    check(t && (x < 0), x < 0);
    check(f && (x < 0), f);
    check(t || (x < 0), t);
    check(f || (x < 0), x < 0);

    check(x == y || y != x, t);
    check(x == y || x != y, t);
    check(x == y && x != y, f);
    check(x == y && y != x, f);
    check(x < y || x >= y, t);
    check(x <= y || x > y, t);
    check(x < y && x >= y, f);
    check(x <= y && x > y, f);

    check(x <= max(x, y), t);
    check(x <  min(x, y), f);
    check(min(x, y) <= x, t);
    check(max(x, y) <  x, f);
    check(max(x, y) <= y, x <= y);
    check(min(x, y) >= y, y <= x);

    check((1 < y) && (2 < y), 2 < y);

    check(x*5 < 4, x < 1);
    check(x*5 < 5, x < 1);
    check(x*5 < 6, x < 2);
    check(x*5 <= 4, x <= 0);
    check(x*5 <= 5, x <= 1);
    check(x*5 <= 6, x <= 1);
    check(x*5 > 4, 0 < x);
    check(x*5 > 5, 1 < x);
    check(x*5 > 6, 1 < x);
    check(x*5 >= 4, 1 <= x);
    check(x*5 >= 5, 1 <= x);
    check(x*5 >= 6, 2 <= x);

    check(x/4 < 3, x < 12);
    check(3 < x/4, 15 < x);

    check(4 - x <= 0, 4 <= x);

    check((x/8)*8 < x - 8, f);
    check((x/8)*8 < x - 9, f);
    check((x/8)*8 < x - 7, f);
    check((x/8)*8 < x - 6, 6 < x % 8);
    check(ramp(x*4, 1, 4) < broadcast(y*4, 4), broadcast(x < y, 4));
    check(ramp(x*8, 1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 1, 1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 4, 1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 8, 1, 4) < broadcast(y*8, 4), broadcast(x < y + (-1), 4));
    check(ramp(x*8 + 5, 1, 4) < broadcast(y*8, 4), ramp(x*8 + 5, 1, 4) < broadcast(y*8, 4));
    check(ramp(x*8 - 1, 1, 4) < broadcast(y*8, 4), ramp(x*8 + (-1), 1, 4) < broadcast(y*8, 4));
    check(ramp(x*8, 1, 4) < broadcast(y*4, 4), broadcast(x*2 < y, 4));
    check(ramp(x*8, 2, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 1, 2, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 2, 2, 4) < broadcast(y*8, 4), ramp(x*8 + 2, 2, 4) < broadcast(y*8, 4));
    check(ramp(x*8, 3, 4) < broadcast(y*8, 4), ramp(x*8, 3, 4) < broadcast(y*8, 4));
    check(select(ramp((x/16)*16, 1, 8) < broadcast((y/8)*8, 8), broadcast(1, 8), broadcast(3, 8)),
          select((x/16)*2 < y/8, broadcast(1, 8), broadcast(3, 8)));

    check(ramp(x*8, -1, 4) < broadcast(y*8, 4), ramp(x*8, -1, 4) < broadcast(y*8, 4));
    check(ramp(x*8 + 1, -1, 4) < broadcast(y*8, 4), ramp(x*8 + 1, -1, 4) < broadcast(y*8, 4));
    check(ramp(x*8 + 4, -1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 + 8, -1, 4) < broadcast(y*8, 4), ramp(x*8 + 8, -1, 4) < broadcast(y*8, 4));
    check(ramp(x*8 + 5, -1, 4) < broadcast(y*8, 4), broadcast(x < y, 4));
    check(ramp(x*8 - 1, -1, 4) < broadcast(y*8, 4), broadcast(x < y + 1, 4));

    // Check anded conditions apply to the then case only
    check(IfThenElse::make(x == 4 && y == 5,
                           Evaluate::make(z + x + y),
                           Evaluate::make(z + x - y)),
          IfThenElse::make(x == 4 && y == 5,
                           Evaluate::make(z + 9),
                           Evaluate::make(z + x - y)));

    // Check ored conditions apply to the else case only
    check(IfThenElse::make(b1 || b2,
                           Evaluate::make(select(b1, x+3, y+4) + select(b2, x+5, y+7)),
                           Evaluate::make(select(b1, x+3, y+8) - select(b2, x+5, y+7))),
          IfThenElse::make(b1 || b2,
                           Evaluate::make(select(b1, x+3, y+4) + select(b2, x+5, y+7)),
                           Evaluate::make(1)));

    // Check single conditions apply to both cases of an ifthenelse
    check(IfThenElse::make(b1,
                           Evaluate::make(select(b1, x, y)),
                           Evaluate::make(select(b1, z, w))),
          IfThenElse::make(b1,
                           Evaluate::make(x),
                           Evaluate::make(w)));

    check(IfThenElse::make(x < y,
                           IfThenElse::make(x < y, Evaluate::make(y), Evaluate::make(x)),
                           Evaluate::make(x)),
          IfThenElse::make(x < y,
                           Evaluate::make(y),
                           Evaluate::make(x)));

    check(Block::make(IfThenElse::make(x < y, Evaluate::make(x+1), Evaluate::make(x+2)),
                      IfThenElse::make(x < y, Evaluate::make(x+3), Evaluate::make(x+4))),
          IfThenElse::make(x < y,
                           Block::make(Evaluate::make(x+1), Evaluate::make(x+3)),
                           Block::make(Evaluate::make(x+2), Evaluate::make(x+4))));

    check(Block::make(IfThenElse::make(x < y, Evaluate::make(x+1)),
                      IfThenElse::make(x < y, Evaluate::make(x+2))),
          IfThenElse::make(x < y, Block::make(Evaluate::make(x+1), Evaluate::make(x+2))));

    check(Block::make(IfThenElse::make(x < y, Evaluate::make(x+1), Evaluate::make(x+2)),
                      IfThenElse::make(x < y, Evaluate::make(x+3))),
          IfThenElse::make(x < y,
                           Block::make(Evaluate::make(x+1), Evaluate::make(x+3)),
                           Evaluate::make(x+2)));

    check(Block::make(IfThenElse::make(x < y, Evaluate::make(x+1)),
                      IfThenElse::make(x < y, Evaluate::make(x+2), Evaluate::make(x+3))),
          IfThenElse::make(x < y,
                           Block::make(Evaluate::make(x+1), Evaluate::make(x+2)),
                           Evaluate::make(x+3)));

    // Check conditions involving entire exprs
    Expr foo = x + 3*y;
    Expr foo_simple = x + y*3;
    check(IfThenElse::make(foo == 17,
                           Evaluate::make(x+foo+1),
                           Evaluate::make(x+foo+2)),
          IfThenElse::make(foo_simple == 17,
                           Evaluate::make(x+18),
                           Evaluate::make(x+foo_simple+2)));

    check(IfThenElse::make(foo != 17,
                           Evaluate::make(x+foo+1),
                           Evaluate::make(x+foo+2)),
          IfThenElse::make(foo_simple != 17,
                           Evaluate::make(x+foo_simple+1),
                           Evaluate::make(x+19)));

    // The construct
    //     if (var == expr) then a else b;
    // was being simplified incorrectly, but *only* if var was of type Bool.
    Stmt then_clause = AssertStmt::make(b2, Expr(22));
    Stmt else_clause = AssertStmt::make(b2, Expr(33));
    check(IfThenElse::make(b1 == b2, then_clause, else_clause),
          IfThenElse::make(b1 == b2, then_clause, else_clause));

    // Simplifications of selects
    check(select(x == 3, 5, 7) + 7, select(x == 3, 12, 14));
    check(select(x == 3, 5, 7) - 7, select(x == 3, -2, 0));
    check(select(x == 3, 5, y) - y, select(x == 3, 5 - y, 0));
    check(select(x == 3, y, 5) - y, select(x == 3, 0, 5 - y));
    check(y - select(x == 3, 5, y), select(x == 3, y + (-5), 0));
    check(y - select(x == 3, y, 5), select(x == 3, 0, y + (-5)));

    check(select(x == 3, 5, 7) == 7, x != 3);
    check(select(x == 3, z, y) == z, (x == 3) || (y == z));

    check(select(x == 3, 4, 2) == 0, const_false());
    check(select(x == 3, y, 2) == 4, (x == 3) && (y == 4));
    check(select(x == 3, 2, y) == 4, (x != 3) && (y == 4));

    check(min(select(x == 2, y*3, 8), select(x == 2, y+8, y*7)),
          select(x == 2, min(y*3, y+8), min(y*7, 8)));

    check(max(select(x == 2, y*3, 8), select(x == 2, y+8, y*7)),
          select(x == 2, max(y*3, y+8), max(y*7, 8)));

    check(select(x == 2, x+1, x+5), x + select(x == 2, 1, 5));
    check(select(x == 2, x+y, x+z), x + select(x == 2, y, z));
    check(select(x == 2, y+x, x+z), x + select(x == 2, y, z));
    check(select(x == 2, y+x, z+x), select(x == 2, y, z) + x);
    check(select(x == 2, x+y, z+x), x + select(x == 2, y, z));
    check(select(x == 2, x*2, x*5), x * select(x == 2, 2, 5));
    check(select(x == 2, x*y, x*z), x * select(x == 2, y, z));
    check(select(x == 2, y*x, x*z), x * select(x == 2, y, z));
    check(select(x == 2, y*x, z*x), select(x == 2, y, z) * x);
    check(select(x == 2, x*y, z*x), x * select(x == 2, y, z));
    check(select(x == 2, x-y, x-z), x - select(x == 2, y, z));
    check(select(x == 2, y-x, z-x), select(x == 2, y, z) - x);
    check(select(x == 2, x+y, x-z), x + select(x == 2, y, 0-z));
    check(select(x == 2, y+x, x-z), x + select(x == 2, y, 0-z));
    check(select(x == 2, x-z, x+y), x + select(x == 2, 0-z, y));
    check(select(x == 2, x-z, y+x), x + select(x == 2, 0-z, y));


    {

        Expr b[12];
        for (int i = 0; i < 12; i++) {
            b[i] = Variable::make(Bool(), unique_name('b'));
        }

        // Some rules that collapse selects
        check(select(b[0], x, select(b[1], x, y)),
              select(b[0] || b[1], x, y));
        check(select(b[0], x, select(b[1], y, x)),
              select(b[0] || !b[1], x, y));
        check(select(b[0], select(b[1], x, y), x),
              select(b[0] && !b[1], y, x));
        check(select(b[0], select(b[1], y, x), x),
              select(b[0] && b[1], y, x));

        // Ternary boolean expressions in two variables
        check(b[0] || (b[0] && b[1]), b[0]);
        check((b[0] && b[1]) || b[0], b[0]);
        check(b[0] && (b[0] || b[1]), b[0]);
        check((b[0] || b[1]) && b[0], b[0]);
        check(b[0] && (b[0] && b[1]), b[0] && b[1]);
        check((b[0] && b[1]) && b[0], b[1] && b[0]);
        check(b[0] || (b[0] || b[1]), b[0] || b[1]);
        check((b[0] || b[1]) || b[0], b[1] || b[0]);

        // A nasty unsimplified boolean Expr seen in the wild
        Expr nasty = ((((((((((((((((((((((((((((((((((((((((((((b[0] && b[1]) || (b[2] && b[1])) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[6]) || (b[2] && b[6]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[3]) || (b[2] && b[3]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[7]) || (b[2] && b[7]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[4]) || (b[2] && b[4]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[8]) || (b[2] && b[8]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[5]) || (b[2] && b[5]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[10]) || (b[2] && b[10]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[9]) || (b[2] && b[9]))) || b[0]) || b[2]);
        check(nasty, b[0] || b[2]);
    }

    {
        // verify that likely(const-bool) is *not* simplified.
        check(likely(t), likely(t));
        check(likely(f), likely(f));

        // verify that !likely(e) -> likely(!e)
        check(!likely(t), likely(f));
        check(!likely(f), likely(t));
        check(!likely(x == 2), likely(x != 2));

        // can_prove(likely(const-true)) = true
        // can_prove(!likely(const-false)) = true
        internal_assert(can_prove(likely(t)));
        internal_assert(can_prove(!likely(f)));

        // unprovable cases
        internal_assert(!can_prove(likely(f)));
        internal_assert(!can_prove(!likely(t)));
        internal_assert(!can_prove(!likely(x == 2)));
    }
}

void check_math() {
    Var x = Var("x");

    check(sqrt(4.0f), 2.0f);
    check(log(0.5f + 0.5f), 0.0f);
    check(exp(log(2.0f)), 2.0f);
    check(pow(4.0f, 0.5f), 2.0f);
    check(round(1000.0f*pow(exp(1.0f), log(10.0f))), 10000.0f);

    check(floor(0.98f), 0.0f);
    check(ceil(0.98f), 1.0f);
    check(round(0.6f), 1.0f);
    check(round(-0.5f), 0.0f);
    check(trunc(-1.6f), -1.0f);
    check(floor(round(x)), round(x));
    check(ceil(ceil(x)), ceil(x));
}

void check_overflow() {
    Expr overflowing[] = {
        make_const(Int(32), 0x7fffffff) + 1,
        make_const(Int(32), 0x7ffffff0) + 16,
        (make_const(Int(32), 0x7fffffff) +
         make_const(Int(32), 0x7fffffff)),
        make_const(Int(32), 0x08000000) * 16,
        (make_const(Int(32), 0x00ffffff) *
         make_const(Int(32), 0x00ffffff)),
        make_const(Int(32), 0x80000000) - 1,
        0 - make_const(Int(32), 0x80000000),
        make_const(Int(64), (int64_t)0x7fffffffffffffffLL) + 1,
        make_const(Int(64), (int64_t)0x7ffffffffffffff0LL) + 16,
        (make_const(Int(64), (int64_t)0x7fffffffffffffffLL) +
         make_const(Int(64), (int64_t)0x7fffffffffffffffLL)),
        make_const(Int(64), (int64_t)0x0800000000000000LL) * 16,
        (make_const(Int(64), (int64_t)0x00ffffffffffffffLL) *
         make_const(Int(64), (int64_t)0x00ffffffffffffffLL)),
        make_const(Int(64), (int64_t)0x8000000000000000LL) - 1,
        0 - make_const(Int(64), (int64_t)0x8000000000000000LL),
    };
    Expr not_overflowing[] = {
        make_const(Int(32), 0x7ffffffe) + 1,
        make_const(Int(32), 0x7fffffef) + 16,
        make_const(Int(32), 0x07ffffff) * 2,
        (make_const(Int(32), 0x0000ffff) *
         make_const(Int(32), 0x00008000)),
        make_const(Int(32), 0x80000001) - 1,
        0 - make_const(Int(32), 0x7fffffff),
        make_const(Int(64), (int64_t)0x7ffffffffffffffeLL) + 1,
        make_const(Int(64), (int64_t)0x7fffffffffffffefLL) + 16,
        make_const(Int(64), (int64_t)0x07ffffffffffffffLL) * 16,
        (make_const(Int(64), (int64_t)0x00000000ffffffffLL) *
         make_const(Int(64), (int64_t)0x0000000080000000LL)),
        make_const(Int(64), (int64_t)0x8000000000000001LL) - 1,
        0 - make_const(Int(64), (int64_t)0x7fffffffffffffffLL),
    };

    for (Expr e : overflowing) {
        internal_assert(!is_const(simplify(e)))
            << "Overflowing expression should not have simplified: " << e << "\n";
    }
    for (Expr e : not_overflowing) {
        internal_assert(is_const(simplify(e)))
            << "Non-everflowing expression should have simplified: " << e << "\n";
    }
}

void check_ind_expr(Expr e, bool expect_error) {
    Expr e2 = simplify(e);
    const Call *call = e2.as<Call>();
    bool is_error = call && call->is_intrinsic(Call::indeterminate_expression);
    if (expect_error && !is_error)
        internal_error << "Expression should be indeterminate: " << e << " but saw: " << e2 << "\n";
    else if (!expect_error && is_error)
        internal_error << "Expression should not be indeterminate: " << e << " but saw: " << e2 << "\n";
}

void check_indeterminate_ops(Expr e, bool e_is_zero, bool e_is_indeterminate) {
    Expr b = cast<bool>(e);
    Expr t = const_true(), f = const_false();
    Expr one = cast(e.type(), 1);
    Expr zero = cast(e.type(), 0);

    check_ind_expr(e, e_is_indeterminate);
    check_ind_expr(e + e, e_is_indeterminate);
    check_ind_expr(e - e, e_is_indeterminate);
    check_ind_expr(e * e, e_is_indeterminate);
    check_ind_expr(e / e, e_is_zero || e_is_indeterminate);
    check_ind_expr((1 / e) / e, e_is_zero || e_is_indeterminate);
    // Expr::operator% asserts if denom is constant zero.
    if (!is_zero(e)) {
        check_ind_expr(e % e, e_is_zero || e_is_indeterminate);
        check_ind_expr((1 / e) % e, e_is_zero || e_is_indeterminate);
    }
    check_ind_expr(min(e, one), e_is_indeterminate);
    check_ind_expr(max(e, one), e_is_indeterminate);
    check_ind_expr(e == one, e_is_indeterminate);
    check_ind_expr(one == e, e_is_indeterminate);
    check_ind_expr(e < one, e_is_indeterminate);
    check_ind_expr(one < e, e_is_indeterminate);
    check_ind_expr(!(e == one), e_is_indeterminate);
    check_ind_expr(!(one == e), e_is_indeterminate);
    check_ind_expr(!(e < one), e_is_indeterminate);
    check_ind_expr(!(one < e), e_is_indeterminate);
    check_ind_expr(b && t, e_is_indeterminate);
    check_ind_expr(t && b, e_is_indeterminate);
    check_ind_expr(b || t, e_is_indeterminate);
    check_ind_expr(t || b, e_is_indeterminate);
    check_ind_expr(!b, e_is_indeterminate);
    check_ind_expr(select(b, one, zero), e_is_indeterminate);
    check_ind_expr(select(t, e, zero), e_is_indeterminate);
    check_ind_expr(select(f, zero, e), e_is_indeterminate);
    check_ind_expr(e << one, e_is_indeterminate);
    check_ind_expr(e >> one, e_is_indeterminate);
    // Avoid warnings for things like (1 << 2147483647)
    if (e_is_indeterminate) {
        check_ind_expr(one << e, e_is_indeterminate);
        check_ind_expr(one >> e, e_is_indeterminate);
    }
    check_ind_expr(one & e, e_is_indeterminate);
    check_ind_expr(e & one, e_is_indeterminate);
    check_ind_expr(one | e, e_is_indeterminate);
    check_ind_expr(e | one, e_is_indeterminate);
    if (!e.type().is_uint()) {
        // Avoid warnings
        check_ind_expr(abs(e), e_is_indeterminate);
    }
    check_ind_expr(log(e), e_is_indeterminate);
    check_ind_expr(sqrt(e), e_is_indeterminate);
    check_ind_expr(exp(e), e_is_indeterminate);
    check_ind_expr(pow(e, one), e_is_indeterminate);
    // pow(x, y) explodes for huge integer y (Issue #1441)
    if (e_is_indeterminate) {
        check_ind_expr(pow(one, e), e_is_indeterminate);
    }
    check_ind_expr(floor(e), e_is_indeterminate);
    check_ind_expr(ceil(e), e_is_indeterminate);
    check_ind_expr(round(e), e_is_indeterminate);
    check_ind_expr(trunc(e), e_is_indeterminate);
}

void check_indeterminate() {
    const int32_t values[] = {
        int32_t(0x80000000),
        -2147483647,
        -2,
        -1,
        0,
        1,
        2,
        2147483647,
    };

    for (int32_t i1 : values) {
        // reality-check for never-indeterminate values.
        check_indeterminate_ops(Expr(i1), !i1, false);
        for (int32_t i2 : values) {
            {
                Expr e1(i1), e2(i2);
                Expr r = (e1 / e2);
                bool r_is_zero = !i1 || (i2 != 0 && !div_imp((int64_t)i1, (int64_t)i2));  // avoid trap for -2147483648/-1
                bool r_is_ind = !i2;
                check_indeterminate_ops(r, r_is_zero, r_is_ind);

                // Expr::operator% asserts if denom is constant zero.
                if (!is_zero(e2)) {
                    Expr m = (e1 % e2);
                    bool m_is_zero = !i1 || (i2 != 0 && !mod_imp((int64_t)i1, (int64_t)i2));  // avoid trap for -2147483648/-1
                    bool m_is_ind = !i2;
                    check_indeterminate_ops(m, m_is_zero, m_is_ind);
                }
            }
            {
                uint32_t u1 = (uint32_t)i1;
                uint32_t u2 = (uint32_t)i2;
                Expr e1(u1), e2(u2);
                Expr r = (e1 / e2);
                bool r_is_zero = !u1 || (u2 != 0 && !div_imp(u1, u2));
                bool r_is_ind = !u2;
                check_indeterminate_ops(r, r_is_zero, r_is_ind);

                // Expr::operator% asserts if denom is constant zero.
                if (!is_zero(e2)) {
                    Expr m = (e1 % e2);
                    bool m_is_zero = !u1 || (u2 != 0 && !mod_imp(u1, u2));
                    bool m_is_ind = !u2;
                    check_indeterminate_ops(m, m_is_zero, m_is_ind);
                }
            }
        }
    }
}

}  // namespace

void simplify_test() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w"), v = Var("v");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();

    check_indeterminate();
    check_casts();
    check_algebra();
    check_vectors();
    check_bounds();
    check_math();
    check_boolean();
    check_overflow();

    // Check bitshift operations
    check(cast(Int(16), x) << 10, cast(Int(16), x) * 1024);
    check(cast(Int(16), x) >> 10, cast(Int(16), x) / 1024);
    check(cast(Int(16), x) << -10, cast(Int(16), x) / 1024);
    // Correctly triggers a warning:
    //check(cast(Int(16), x) << 20, cast(Int(16), x) << 20);

    // Check bitwise_and. (Added as result of a bug.)
    // TODO: more coverage of bitwise_and and bitwise_or.
    check(cast(UInt(32), x) & Expr((uint32_t)0xaaaaaaaa),
          cast(UInt(32), x) & Expr((uint32_t)0xaaaaaaaa));

    // Check constant-folding of bitwise ops (and indirectly, reinterpret)
    check(Let::make(x.as<Variable>()->name, 5, ((~x) & 3) | 16), (~5 & 3) | 16);
    check(Let::make(x.as<Variable>()->name, 5, ((~cast<uint8_t>(x)) & 3) | 16), make_const(UInt(8), (~5 & 3) | 16));

    // Check that chains of widening casts don't lose the distinction
    // between zero-extending and sign-extending.
    check(cast(UInt(64), cast(UInt(32), cast(Int(8), -1))),
          UIntImm::make(UInt(64), 0xffffffffULL));

    v = Variable::make(Int(32, 4), "v");
    // Check constants get pushed inwards
    check(Let::make("x", 3, x+4), 7);

    // Check ramps in lets get pushed inwards
    check(Let::make("v", ramp(x*2+7, 3, 4), v + Expr(broadcast(2, 4))),
          ramp(x*2+9, 3, 4));

    // Check broadcasts in lets get pushed inwards
    check(Let::make("v", broadcast(x, 4), v + Expr(broadcast(2, 4))),
          broadcast(x+2, 4));

    // Check that dead lets get stripped
    check(Let::make("x", 3*y*y*y, 4), 4);
    check(Let::make("x", 0, 0), 0);

    // Check that lets inside an evaluate node get lifted
    check(Evaluate::make(Let::make("x", Call::make(Int(32), "dummy", {3, x, 4}, Call::Extern), Let::make("y", 10, x + y + 2))),
          LetStmt::make("x", Call::make(Int(32), "dummy", {3, x, 4}, Call::Extern), Evaluate::make(x + 12)));

    // Test case with most negative 32-bit number, as constant to check that it is not negated.
    check(((x * (int32_t)0x80000000) + (y + z * (int32_t)0x80000000)),
          ((x * (int32_t)0x80000000) + (y + z * (int32_t)0x80000000)));

    // Check that constant args to a stringify get combined
    check(Call::make(type_of<const char *>(), Call::stringify, {3, string(" "), 4}, Call::Intrinsic),
          string("3 4"));

    check(Call::make(type_of<const char *>(), Call::stringify, {3, x, 4, string(", "), 3.4f}, Call::Intrinsic),
          Call::make(type_of<const char *>(), Call::stringify, {string("3"), x, string("4, 3.400000")}, Call::Intrinsic));

    {
        // Check that contiguous prefetch call get collapsed
        Expr base = Variable::make(Handle(), "buf");
        check(Call::make(Int(32), Call::prefetch, {base, x, 4, 1, 64, 4, min(x + y, 128), 256}, Call::Intrinsic),
              Call::make(Int(32), Call::prefetch, {base, x, min(x + y, 128) * 256, 1}, Call::Intrinsic));
    }

    // Check min(x, y)*max(x, y) gets simplified into x*y
    check(min(x, y)*max(x, y), x*y);
    check(min(x, y)*max(y, x), x*y);
    check(max(x, y)*min(x, y), x*y);
    check(max(y, x)*min(x, y), x*y);

    // Check min(x, y) + max(x, y) gets simplified into x + y
    check(min(x, y) + max(x, y), x + y);
    check(min(x, y) + max(y, x), x + y);
    check(max(x, y) + min(x, y), x + y);
    check(max(y, x) + min(x, y), x + y);

    // Check max(min(x, y), max(x, y)) gets simplified into max(x, y)
    check(max(min(x, y), max(x, y)), max(x, y));
    check(max(min(x, y), max(y, x)), max(x, y));
    check(max(max(x, y), min(x, y)), max(x, y));
    check(max(max(y, x), min(x, y)), max(x, y));

    // Check min(max(x, y), min(x, y)) gets simplified into min(x, y)
    check(min(max(x, y), min(x, y)), min(x, y));
    check(min(max(x, y), min(y, x)), min(x, y));
    check(min(min(x, y), max(x, y)), min(x, y));
    check(min(min(y, x), max(x, y)), min(x, y));

    // Check if we can simplify away comparison on vector types considering bounds.
    Scope<Interval> bounds_info;
    bounds_info.push("x", Interval(0,4));
    check_in_bounds(ramp(x,  1, 4) < broadcast( 0, 4), const_false(4), bounds_info);
    check_in_bounds(ramp(x,  1, 4) < broadcast( 8, 4), const_true(4),  bounds_info);
    check_in_bounds(ramp(x, -1, 4) < broadcast(-4, 4), const_false(4), bounds_info);
    check_in_bounds(ramp(x, -1, 4) < broadcast( 5, 4), const_true(4),  bounds_info);
    check_in_bounds(min(ramp(x,  1, 4), broadcast( 0, 4)), broadcast(0, 4),  bounds_info);
    check_in_bounds(min(ramp(x,  1, 4), broadcast( 8, 4)), ramp(x, 1, 4),    bounds_info);
    check_in_bounds(min(ramp(x, -1, 4), broadcast(-4, 4)), broadcast(-4, 4), bounds_info);
    check_in_bounds(min(ramp(x, -1, 4), broadcast( 5, 4)), ramp(x, -1, 4),   bounds_info);
    check_in_bounds(max(ramp(x,  1, 4), broadcast( 0, 4)), ramp(x, 1, 4),    bounds_info);
    check_in_bounds(max(ramp(x,  1, 4), broadcast( 8, 4)), broadcast(8, 4),  bounds_info);
    check_in_bounds(max(ramp(x, -1, 4), broadcast(-4, 4)), ramp(x, -1, 4),   bounds_info);
    check_in_bounds(max(ramp(x, -1, 4), broadcast( 5, 4)), broadcast(5, 4),  bounds_info);

    // Collapse some vector interleaves
    check(interleave_vectors({ramp(x, 2, 4), ramp(x+1, 2, 4)}), ramp(x, 1, 8));
    check(interleave_vectors({ramp(x, 4, 4), ramp(x+2, 4, 4)}), ramp(x, 2, 8));
    check(interleave_vectors({ramp(x-y, 2*y, 4), ramp(x, 2*y, 4)}), ramp(x-y, y, 8));
    check(interleave_vectors({ramp(x, 3, 4), ramp(x+1, 3, 4), ramp(x+2, 3, 4)}), ramp(x, 1, 12));
    {
        Expr vec = ramp(x, 1, 16);
        check(interleave_vectors({slice(vec, 0, 2, 8), slice(vec, 1, 2, 8)}), vec);
        check(interleave_vectors({slice(vec, 0, 4, 4), slice(vec, 1, 4, 4), slice(vec, 2, 4, 4), slice(vec, 3, 4, 4)}), vec);
    }

    // Collapse some vector concats
    check(concat_vectors({ramp(x, 2, 4), ramp(x+8, 2, 4)}), ramp(x, 2, 8));
    check(concat_vectors({ramp(x, 3, 2), ramp(x+6, 3, 2), ramp(x+12, 3, 2)}), ramp(x, 3, 6));

    // Now some ones that can't work
    {
        Expr e = interleave_vectors({ramp(x, 2, 4), ramp(x, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(x+2, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 3, 4), ramp(x+1, 3, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(y+1, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(x+1, 3, 4)});
        check(e, e);

        e = concat_vectors({ramp(x, 1, 4), ramp(x+4, 2, 4)});
        check(e, e);
        e = concat_vectors({ramp(x, 1, 4), ramp(x+8, 1, 4)});
        check(e, e);
        e = concat_vectors({ramp(x, 1, 4), ramp(y+4, 1, 4)});
        check(e, e);
    }

    // Now check that an interleave of some collapsible loads collapses into a single dense load
    {
        Expr load1 = Load::make(Float(32, 4), "buf", ramp(x, 2, 4), Buffer<>(), Parameter(), const_true(4));
        Expr load2 = Load::make(Float(32, 4), "buf", ramp(x+1, 2, 4), Buffer<>(), Parameter(), const_true(4));
        Expr load12 = Load::make(Float(32, 8), "buf", ramp(x, 1, 8), Buffer<>(), Parameter(), const_true(8));
        check(interleave_vectors({load1, load2}), load12);

        // They don't collapse in the other order
        Expr e = interleave_vectors({load2, load1});
        check(e, e);

        // Or if the buffers are different
        Expr load3 = Load::make(Float(32, 4), "buf2", ramp(x+1, 2, 4), Buffer<>(), Parameter(), const_true(4));
        e = interleave_vectors({load1, load3});
        check(e, e);
    }

    // Check that concatenated loads of adjacent scalars collapse into a vector load.
    {
        int lanes = 4;
        std::vector<Expr> loads;
        for (int i = 0; i < lanes; i++) {
            loads.push_back(Load::make(Float(32), "buf", x+i, Buffer<>(), Parameter(), const_true()));
        }

        check(concat_vectors(loads), Load::make(Float(32, lanes), "buf", ramp(x, 1, lanes), Buffer<>(), Parameter(), const_true(lanes)));
    }

    // This expression doesn't simplify, but it did cause exponential
    // slowdown at one stage.
    {
        Expr e = x;
        for (int i = 0; i < 100; i++) {
            e = max(e, 1)/2;
        }
        check(e, e);
    }

    // These expressions are used to cause infinite recursion.
    check(Broadcast::make(-16, 2) < (ramp(Cast::make(UInt(16), 7), Cast::make(UInt(16), 11), 2) - Broadcast::make(1, 2)),
          Broadcast::make(-16, 2) < (ramp(make_const(UInt(16), 7), make_const(UInt(16), 11), 2) - Broadcast::make(1, 2)));
    check((ramp(-71, 39, 2)/Cast::make(Int(32).with_lanes(2), ramp(Expr((uint16_t)1), Expr((uint16_t)1), 2))) >= Broadcast::make(23, 2),
          (Cast::make(Int(32).with_lanes(2), ramp(Expr((uint16_t)1), Expr((uint16_t)1), 2)) * Broadcast::make(23, 2)) <= ramp(-71, 39, 2));

    {
        Expr pred = ramp(x*y + x*z, 2, 8) > 2;
        Expr index = ramp(x + y, 1, 8);
        Expr value = Load::make(index.type(), "f", index, Buffer<>(), Parameter(), const_true(index.type().lanes()));
        Stmt stmt = Store::make("f", value, index, Parameter(), pred);
        check(stmt, Evaluate::make(0));
    }

    {
        // Verify that integer types passed to min() and max() are coerced to match
        // Exprs, rather than being promoted to int first. (TODO: This doesn't really
        // belong in the test for Simplify, but IROperator has no test unit of its own.)
        Expr one = cast<uint16_t>(1);
        const int two = 2;  // note that type is int, not uint16_t
        Expr r1, r2, r3;

        r1 = min(one, two);
        internal_assert(r1.type() == halide_type_of<uint16_t>());
        r2 = min(one, two, one);
        internal_assert(r2.type() == halide_type_of<uint16_t>());
        // Explicitly passing 'two' as an Expr, rather than an int, will defeat this logic.
        r3 = min(one, Expr(two), one);
        internal_assert(r3.type() == halide_type_of<int>());

        r1 = max(one, two);
        internal_assert(r1.type() == halide_type_of<uint16_t>());
        r2 = max(one, two, one);
        internal_assert(r2.type() == halide_type_of<uint16_t>());
        // Explicitly passing 'two' as an Expr, rather than an int, will defeat this logic.
        r3 = max(one, Expr(two), one);
        internal_assert(r3.type() == halide_type_of<int>());
    }

    {
        Expr x = Variable::make(UInt(32), "x");
        Expr y = Variable::make(UInt(32), "y");
        // This is used to get simplified into broadcast(x - y, 2) which is
        // incorrect when there is overflow.
        Expr e = simplify(max(ramp(x, y, 2), broadcast(x, 2)) - max(broadcast(y, 2), ramp(y, y, 2)));
        Expr expected = max(ramp(x, y, 2), broadcast(x, 2)) - max(ramp(y, y, 2), broadcast(y, 2));
        check(e, expected);
    }

    check(min(x, 63) - min(x, 3), clamp(x, 3, 63) + (-3));
    check(min(x, 3) - min(x, 63), 3 - clamp(x, 3, 63));
    check(min(63, x) - min(x, 3), clamp(x, 3, 63) + (-3));
    check(min(x, 3) - min(63, x), 3 - clamp(x, 3, 63));

    // This used to throw the simplifier into a loop
    simplify((min((min(((x*64) + y), (z + -63)) + 31), min((((x*64) + y) + 63), z)) -
              min((min((((x*64) + y) + 63), z) + -31), (min(((x*64) + y), (z + -63)) + 32))));

    check(min(x * 4 + 63, y) - min(x * 4, y - 3), clamp(y - x * 4 + (-63), -60, 0) + 63);
    check(min(x * 4, y - 3) - min(x * 4 + 63, y), -3 - clamp(y - x * 4 + (-3), 0, 60));
    check(min(y, x * 4 + 63) - min(x * 4, y - 3), 63 - clamp(x * 4 - y + 63, 0, 60));
    check(min(x * 4, y - 3) - min(y, x * 4 + 63), -3 - clamp(y - x * 4 + (-3), 0, 60));

    check(max(x, 63) - max(x, 3), 63 - clamp(x, 3, 63));
    check(max(x, 3) - max(x, 63), clamp(x, 3, 63) + (-63));
    check(max(63, x) - max(3, x), 63 - clamp(x, 3, 63));
    check(max(3, x) - max(x, 63), clamp(x, 3, 63) + (-63));

    check(max(x * 4 + 63, y) - max(x * 4, y - 3), 3 - clamp(y - x * 4 + (-63), -60, 0));
    check(max(x * 4, y - 3) - max(x * 4 + 63, y), clamp(y - x * 4 + (-3), 0, 60) + (-63));
    check(max(x * 4 + 63, y) - max(y - 3, x * 4), 3 - clamp(y - x * 4 + (-63), -60, 0));
    check(max(y - 3, x * 4) - max(y, x * 4 + 63), -63 - clamp(x * 4 - y + 3, -60, 0));

    // Check that provably-true require() expressions are simplified away
    {
        Expr result(42);

        check(require(Expr(1) > Expr(0), result, "error"), result);
        check(require(x == x, result, "error"), result);
    }

    std::cout << "Simplify test passed" << std::endl;
}
}
}
