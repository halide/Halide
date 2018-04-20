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
#include "IRMatch.h"

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

// Returns true iff t is an integral type where overflow is undefined
HALIDE_ALWAYS_INLINE
bool no_overflow_int(Type t) {
    return t.is_int() && t.bits() >= 32;
}

HALIDE_ALWAYS_INLINE
bool no_overflow_scalar_int(Type t) {
    return t.is_scalar() && no_overflow_int(t);
}

// Returns true iff t does not have a well defined overflow behavior.
HALIDE_ALWAYS_INLINE
bool no_overflow(Type t) {
    return t.is_float() || no_overflow_int(t);
}

#if LOG_EXPR_MUTATIONS || LOG_STMT_MUTATIONS
static int debug_indent = 0;
#endif

}

class Simplify : public VariadicVisitor<Simplify, Expr, Stmt> {
public:
    Simplify(bool r, const Scope<Interval> *bi, const Scope<ModulusRemainder> *ai) :
        remove_dead_lets(r), no_float_simplify(false) {
        alignment_info.set_containing_scope(ai);

        // Only respect the constant bounds from the containing scope.
        for (Scope<Interval>::const_iterator iter = bi->cbegin(); iter != bi->cend(); ++iter) {
            ConstBounds bounds;
            if (const int64_t *i_min = as_const_int(iter.value().min)) {
                bounds.min_defined = true;
                bounds.min = *i_min;
            }
            if (const int64_t *i_max = as_const_int(iter.value().max)) {
                bounds.max_defined = true;
                bounds.max = *i_max;
            }

            if (bounds.min_defined || bounds.max_defined) {
                bounds_info.push(iter.name(), bounds);
            }
        }

    }

    // Track constant integer bounds when they exist
    struct ConstBounds {
        int64_t min = 0, max = 0;
        bool min_defined = false, max_defined = false;
    };

#if LOG_EXPR_MUTATIONS
    Expr mutate(const Expr &e, ConstBounds *b) {
        const std::string spaces(debug_indent, ' ');
        debug(1) << spaces << "Simplifying Expr: " << e << "\n";
        debug_indent++;
        Expr new_e = VariadicVisitor<Simplify, Expr, Stmt>::dispatch(e, b);
        debug_indent--;
        if (!new_e.same_as(e)) {
            debug(1)
                << spaces << "Before: " << e << "\n"
                << spaces << "After:  " << new_e << "\n";
        }
        internal_assert(e.type() == new_e.type());
        return new_e;
    }

#else
    HALIDE_ALWAYS_INLINE
    Expr mutate(const Expr &e, ConstBounds *b) {
        return VariadicVisitor<Simplify, Expr, Stmt>::dispatch(e, b);
    }
#endif

#if LOG_STMT_MUTATIONS
    Stmt mutate(const Stmt &s) {
        const std::string spaces(debug_indent, ' ');
        debug(1) << spaces << "Simplifying Stmt: " << s << "\n";
        debug_indent++;
        Stmt new_s = VariadicVisitor<Simplify, Expr, Stmt>::dispatch(s);
        debug_indent--;
        if (!new_s.same_as(s)) {
            debug(1)
                << spaces << "Before: " << s << "\n"
                << spaces << "After:  " << new_s << "\n";
        }
        return new_s;
    }
#else
    Stmt mutate(const Stmt &s) {
        return VariadicVisitor<Simplify, Expr, Stmt>::dispatch(s);
    }
#endif

    bool remove_dead_lets;
    bool no_float_simplify;

    HALIDE_ALWAYS_INLINE
    bool may_simplify(const Type &t) {
        return !no_float_simplify || !t.is_float();
    }

    struct VarInfo {
        Expr replacement;
        int old_uses, new_uses;
    };

    Scope<VarInfo> var_info;
    Scope<ConstBounds> bounds_info;
    Scope<ModulusRemainder> alignment_info;

    // Symbols used by rewrite rules
    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
    IRMatcher::Wild<3> w;
    IRMatcher::Wild<4> u;
    IRMatcher::WildConst<0> c0;
    IRMatcher::WildConst<1> c1;
    IRMatcher::WildConst<2> c2;
    IRMatcher::WildConst<3> c3;

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

    Expr visit(const IntImm *op, ConstBounds *bounds) {
        if (bounds && no_overflow_int(op->type)) {
            bounds->min_defined = bounds->max_defined = true;
            bounds->min = bounds->max = op->value;
        }
        return op;
    }

    Expr visit(const UIntImm *op, ConstBounds *bounds) {
        return op;
    }

    Expr visit(const FloatImm *op, ConstBounds *bounds) {
        return op;
    }

    Expr visit(const StringImm *op, ConstBounds *bounds) {
        return op;
    }

    Expr visit(const Broadcast *op, ConstBounds *bounds) {
        Expr value = mutate(op->value, bounds);
        if (value.same_as(op->value)) {
            return op;
        } else {
            return Broadcast::make(value, op->type.lanes());
        }
    }

    Expr visit(const Cast *op, ConstBounds *bounds) {
        // We don't try to reason about bounds through casts for now
        Expr value = mutate(op->value, nullptr);

        if (may_simplify(op->type) && may_simplify(op->value.type())) {
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
                return mutate(Cast::make(op->type, cast->value), bounds);
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
                return mutate(Cast::make(op->type, cast->value), bounds);
            } else if (broadcast_value) {
                // cast(broadcast(x)) -> broadcast(cast(x))
                return mutate(Broadcast::make(Cast::make(op->type.element_of(), broadcast_value->value), broadcast_value->lanes), bounds);
            } else if (ramp_value &&
                       op->type.element_of() == Int(64) &&
                       op->value.type().element_of() == Int(32)) {
                // cast(ramp(a, b, w)) -> ramp(cast(a), cast(b), w)
                return mutate(Ramp::make(Cast::make(op->type.element_of(), ramp_value->base),
                                         Cast::make(op->type.element_of(), ramp_value->stride),
                                         ramp_value->lanes), bounds);
            } else if (add &&
                       op->type == Int(64) &&
                       op->value.type() == Int(32) &&
                       is_const(add->b)) {
                // In the interest of moving constants outwards so they
                // can cancel, pull the addition outside of the cast.
                return mutate(Cast::make(op->type, add->a) + add->b, bounds);
            }
        }

        if (value.same_as(op->value)) {
            return op;
        } else {
            return Cast::make(op->type, value);
        }
    }

    Expr visit(const Variable *op, ConstBounds *bounds) {
        if (bounds_info.contains(op->name)) {
            const ConstBounds &b = bounds_info.get(op->name);
            if (bounds) {
                *bounds = b;
            }
            if (b.min_defined && b.max_defined && b.min == b.max) {
                return make_const(op->type, b.min);
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

    Expr visit(const Add *op, ConstBounds *bounds) {
        ConstBounds a_bounds, b_bounds;
        Expr a = mutate(op->a, &a_bounds);
        Expr b = mutate(op->b, &b_bounds);

        if (bounds && no_overflow_int(op->type)) {
            bounds->min_defined = a_bounds.min_defined && b_bounds.min_defined;
            bounds->max_defined = a_bounds.max_defined && b_bounds.max_defined;
            bounds->min = a_bounds.min + b_bounds.min;
            bounds->max = a_bounds.max + b_bounds.max;
        }

        if (may_simplify(op->type)) {

            // Order commutative operations by node type
            if (a.node_type() < b.node_type()) {
                std::swap(a, b);
                std::swap(a_bounds, b_bounds);
            }

            auto indet = IRMatcher::indet(op->type);
            auto overflow = IRMatcher::overflow(op->type);
            auto rewrite = IRMatcher::rewriter(IRMatcher::add(a, b));
            const int lanes = op->type.lanes();

            if (rewrite(x + indet, b) ||
                rewrite(indet + x, a) ||
                rewrite(x + overflow, b) ||
                rewrite(overflow + x, a) ||
                rewrite(c0 + c1, fold(c0 + c1)) ||
                rewrite(x + 0, a) ||
                rewrite(0 + x, b)) {
                return rewrite.result;
            }

            if (rewrite(x + x, x * 2) ||
                rewrite(ramp(x, y) + ramp(z, w), ramp(x + z, y + w, lanes)) ||
                rewrite(ramp(x, y) + broadcast(z), ramp(x + z, y, lanes)) ||
                rewrite(broadcast(x) + broadcast(y), broadcast(x + y, lanes)) ||
                rewrite(select(x, y, z) + select(x, w, u), select(x, y + w, z + u)) ||
                rewrite(select(x, c0, c1) + c2, select(x, fold(c0 + c2), fold(c1 + c2))) ||
                rewrite(select(x, y, c1) + c2, select(x, y + c2, fold(c1 + c2))) ||
                rewrite(select(x, c0, y) + c2, select(x, fold(c0 + c2), y + c2)) ||
                rewrite((x + c0) + c1, x + fold(c0 + c1)) ||
                rewrite((x + c0) + y, (x + y) + c0) ||
                rewrite(x + (y + c0), (x + y) + c0) ||
                rewrite((c0 - x) + c1, fold(c0 + c1) - x) ||
                rewrite((c0 - x) + y, (y - x) + c0) ||
                rewrite((x - y) + y, x) ||
                rewrite(x + (y - x), y) ||
                rewrite(x + (c0 - y), (x - y) + c0) ||
                rewrite((x - y) + (y - z), x - z) ||
                rewrite((x - y) + (z - x), z - y) ||
                rewrite(x + y*c0, x - y*(-c0), c0 < 0 && -c0 > 0) ||
                rewrite(x*c0 + y, y - x*(-c0), c0 < 0 && -c0 > 0 && !is_const(y)) ||
                rewrite(x*y + z*y, (x + z)*y) ||
                rewrite(x*y + y*z, (x + z)*y) ||
                rewrite(y*x + z*y, y*(x + z)) ||
                rewrite(y*x + y*z, y*(x + z)) ||
                rewrite(x*c0 + y*c1, (x + y*fold(c1/c0)) * c0, c1 % c0 == 0) ||
                rewrite(x*c0 + y*c1, (x*fold(c0/c1) + y) * c1, c0 % c1 == 0) ||
                (no_overflow(op->type) &&
                 (rewrite(x + x*y, x * (y + 1)) ||
                  rewrite(x + y*x, (y + 1) * x) ||
                  rewrite(x*y + x, x * (y + 1)) ||
                  rewrite(y*x + x, (y + 1) * x, !is_const(x)) ||
                  rewrite((x + c0)/c1 + c2, (x + fold(c0 + c1*c2))/c1) ||
                  rewrite((x + (y + c0)/c1) + c2, x + (y + (c0 + c1*c2))/c1) ||
                  rewrite(((y + c0)/c1 + x) + c2, x + (y + (c0 + c1*c2))/c1) ||
                  rewrite((c0 - x)/c1 + c2, (fold(c0 + c1*c2) - x)/c1, c0 != 0) ||
                  rewrite(x + (x + y)/c0, (fold(c0 + 1)*x + y)/c0) ||
                  rewrite(x + (y + x)/c0, (fold(c0 + 1)*x + y)/c0) ||
                  rewrite(x + (y - x)/c0, (fold(c0 - 1)*x + y)/c0) ||
                  rewrite(x + (x - y)/c0, (fold(c0 + 1)*x - y)/c0) ||
                  rewrite((x - y)/c0 + x, (fold(c0 + 1)*x - y)/c0) ||
                  rewrite((y - x)/c0 + x, (y + fold(c0 - 1)*x)/c0) ||
                  rewrite((x + y)/c0 + x, (fold(c0 + 1)*x + y)/c0) ||
                  rewrite((y + x)/c0 + x, (y + fold(c0 + 1)*x)/c0) ||
                  rewrite(min(x, y - z) + z, min(x + z, y)) ||
                  rewrite(min(y - z, x) + z, min(y, x + z)) ||
                  rewrite(min(x, y + c0) + c1, min(x + c1, y), c0 + c1 == 0) ||
                  rewrite(min(y + c0, x) + c1, min(y, x + c1), c0 + c1 == 0) ||
                  rewrite(z + min(x, y - z), min(z + x, y)) ||
                  rewrite(z + min(y - z, x), min(y, z + x)) ||
                  rewrite(z + max(x, y - z), max(z + x, y)) ||
                  rewrite(z + max(y - z, x), max(y, z + x)) ||
                  rewrite(max(x, y - z) + z, max(x + z, y)) ||
                  rewrite(max(y - z, x) + z, max(y, x + z)) ||
                  rewrite(max(x, y + c0) + c1, max(x + c1, y), c0 + c1 == 0) ||
                  rewrite(max(y + c0, x) + c1, max(y, x + c1), c0 + c1 == 0) ||
                  rewrite(max(x, y) + min(x, y), x + y) ||
                  rewrite(max(x, y) + min(y, x), x + y))) ||
                (no_overflow_int(op->type) &&
                 (rewrite((x/y)*y + x%y, x) ||
                  rewrite((z + x/y)*y + x%y, z*y + x) ||
                  rewrite((x/y + z)*y + x%y, x + z*y) ||
                  rewrite(y%c0 + (z + x*c0), z + (x*c0 + y%c0)) ||
                  rewrite(y%c0 + (x*c0 + z), z + (x*c0 + y%c0)) ||
                  rewrite(y*c0 + (z + x%c0), z + (y*c0 + x%c0)) ||
                  rewrite(y*c0 + (x%c0 + z), z + (y*c0 + x%c0)) ||
                  rewrite(x/2 + x%2, (x + 1) / 2)))) {
                return mutate(std::move(rewrite.result), bounds);
            }

            const Shuffle *shuffle_a = a.as<Shuffle>();
            const Shuffle *shuffle_b = b.as<Shuffle>();
            if (shuffle_a && shuffle_b &&
                shuffle_a->is_slice() &&
                shuffle_b->is_slice()) {
                if (a.same_as(op->a) && b.same_as(op->b)) {
                    return hoist_slice_vector<Add>(op);
                } else {
                    return hoist_slice_vector<Add>(Add::make(a, b));
                }
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Add::make(a, b);
        }
    }

    Expr visit(const Sub *op, ConstBounds *bounds) {
        ConstBounds a_bounds, b_bounds;
        Expr a = mutate(op->a, &a_bounds);
        Expr b = mutate(op->b, &b_bounds);

        if (bounds && no_overflow_int(op->type)) {
            bounds->min_defined = a_bounds.min_defined && b_bounds.max_defined;
            bounds->max_defined = a_bounds.max_defined && b_bounds.min_defined;
            bounds->min = a_bounds.min - b_bounds.max;
            bounds->max = a_bounds.max - b_bounds.min;
        }

        if (may_simplify(op->type)) {

            auto rewrite = IRMatcher::rewriter(IRMatcher::sub(a, b));
            auto overflow = IRMatcher::overflow(op->type);
            auto indet = IRMatcher::indet(op->type);
            auto zero = IRMatcher::Const(0, op->type);
            const int lanes = op->type.lanes();

            if (rewrite(x - 0, x) ||
                rewrite(overflow - x, overflow) ||
                rewrite(x - overflow, overflow) ||
                rewrite(indet - x, indet) ||
                rewrite(x - indet, indet) ||
                rewrite(x - x, zero) ||
                rewrite(c0 - c1, fold(c0 - c1))) {
                return rewrite.result;
            }

            if ((!op->type.is_uint() && rewrite(x - c0, x + fold(-c0))) ||
                rewrite(ramp(x, y) - ramp(z, w), ramp(x - z, y - w, lanes)) ||
                rewrite(ramp(x, y) - broadcast(z), ramp(x - z, y, lanes)) ||
                rewrite(broadcast(x) - ramp(z, w), ramp(x - z, -w, lanes)) ||
                rewrite(broadcast(x) - broadcast(y), broadcast(x - y, lanes)) ||
                rewrite(select(x, y, z) - select(x, w, u), select(x, y - w, z - u)) ||
                rewrite(select(x, y, z) - y, select(x, zero, z - y)) ||
                rewrite(select(x, y, z) - z, select(x, y - z, zero)) ||
                rewrite(y - select(x, y, z), select(x, zero, y - z)) ||
                rewrite(z - select(x, y, z), select(x, z - y, zero)) ||
                rewrite((x + y) - x, y) ||
                rewrite((x + y) - y, x) ||
                rewrite(x - (x + y), -y) ||
                rewrite(y - (x + y), -x) ||
                rewrite((x - y) - x, -y) ||
                rewrite((x + c0) - c1, x + fold(c0 - c1)) ||
                rewrite((x + c0) - (c1 - y), (x + y) + fold(c0 - c1)) ||
                rewrite((x + c0) - (y + c1), (x - y) + fold(c0 - c1)) ||
                rewrite((x + c0) - y, (x - y) + c0) ||
                rewrite((c0 - x) - (c1 - y), (y - x) + fold(c0 - c1)) ||
                rewrite((c0 - x) - (y + c1), fold(c0 - c1) - (x + y)) ||
                rewrite(x - (y - z), x + (z - y)) ||
                rewrite(x - y*c0, x + y*fold(-c0), c0 < 0 && -c0 > 0) ||
                rewrite(x - (y + c0), (x - y) - c0) ||
                rewrite((c0 - x) - c1, fold(c0 - c1) - x) ||
                rewrite(x*y - z*y, (x - z)*y) ||
                rewrite(x*y - y*z, (x - z)*y) ||
                rewrite(y*x - z*y, y*(x - z)) ||
                rewrite(y*x - y*z, y*(x - z)) ||
                rewrite((x + y) - (x + z), y - z) ||
                rewrite((x + y) - (z + x), y - z) ||
                rewrite((y + x) - (x + z), y - z) ||
                rewrite((y + x) - (z + x), y - z) ||
                rewrite(((x + y) + z) - x, y + z) ||
                rewrite(((y + x) + z) - x, y + z) ||
                rewrite((z + (x + y)) - x, z + y) ||
                rewrite((z + (y + x)) - x, z + y) ||
                (no_overflow(op->type) &&
                 (rewrite(max(x, y) - x, max(zero, y - x)) ||
                  rewrite(min(x, y) - x, min(zero, y - x)) ||
                  rewrite(max(x, y) - y, max(x - y, zero)) ||
                  rewrite(min(x, y) - y, min(x - y, zero)) ||
                  rewrite(x - max(x, y), min(zero, x - y), !is_const(x)) ||
                  rewrite(x - min(x, y), max(zero, x - y), !is_const(x)) ||
                  rewrite(y - max(x, y), min(y - x, zero), !is_const(y)) ||
                  rewrite(y - min(x, y), max(y - x, zero), !is_const(y)) ||
                  rewrite(x*y - x, x*(y - 1)) ||
                  rewrite(x*y - y, (x - 1)*y) ||
                  rewrite(x - x*y, x*(1 - y)) ||
                  rewrite(x - y*x, (1 - y)*x) ||
                  rewrite(c0 - (c1 - x)/c2, (fold(c0*c2 - c1 + c2 - 1) + x)/c2, c2 > 0) ||
                  rewrite(c0 - (x + c1)/c2, (fold(c0*c2 - c1 + c2 - 1) - x)/c2, c2 > 0) ||
                  rewrite(x - (x + y)/c0, (x*fold(c0 - 1) - y + fold(c0 - 1))/c0, c0 > 0) ||
                  rewrite(x - (x - y)/c0, (x*fold(c0 - 1) + y + fold(c0 - 1))/c0, c0 > 0) ||
                  rewrite(x - (y + x)/c0, (x*fold(c0 - 1) - y + fold(c0 - 1))/c0, c0 > 0) ||
                  rewrite(x - (y - x)/c0, (x*fold(c0 + 1) - y + fold(c0 - 1))/c0, c0 > 0) ||
                  rewrite((x + y)/c0 - x, (x*fold(1 - c0) + y)/c0) ||
                  rewrite((y + x)/c0 - x, (y + x*fold(1 - c0))/c0) ||
                  rewrite((x - y)/c0 - x, (x*fold(1 - c0) - y)/c0) ||
                  rewrite((y - x)/c0 - x, (y - x*fold(1 + c0))/c0) ||
                  rewrite(x - min(x + y, z), max(-y, x - z)) ||
                  rewrite(x - min(y + x, z), max(-y, x - z)) ||
                  rewrite(x - min(z, x + y), max(x - z, -y)) ||
                  rewrite(x - min(z, y + x), max(x - z, -y)) ||
                  rewrite(min(x + y, z) - x, min(y, z - x)) ||
                  rewrite(min(y + x, z) - x, min(y, z - x)) ||
                  rewrite(min(z, x + y) - x, min(z - x, y)) ||
                  rewrite(min(z, y + x) - x, min(z - x, y)) ||
                  rewrite(min(x, y) - min(y, x), zero) ||
                  rewrite(min(x, y) - min(z, w), y - w, can_prove(x - y == z - w, this)) ||
                  rewrite(min(x, y) - min(w, z), y - w, can_prove(x - y == z - w, this)) ||

                  rewrite(x - max(x + y, z), min(-y, x - z)) ||
                  rewrite(x - max(y + x, z), min(-y, x - z)) ||
                  rewrite(x - max(z, x + y), min(x - z, -y)) ||
                  rewrite(x - max(z, y + x), min(x - z, -y)) ||
                  rewrite(max(x + y, z) - x, max(y, z - x)) ||
                  rewrite(max(y + x, z) - x, max(y, z - x)) ||
                  rewrite(max(z, x + y) - x, max(z - x, y)) ||
                  rewrite(max(z, y + x) - x, max(z - x, y)) ||
                  rewrite(max(x, y) - max(y, x), zero) ||
                  rewrite(max(x, y) - max(z, w), y - w, can_prove(x - y == z - w, this)) ||
                  rewrite(max(x, y) - max(w, z), y - w, can_prove(x - y == z - w, this)) ||

                  // When you have min(x, y) - min(z, w) and no further
                  // information, there are four possible ways for the
                  // mins to resolve. However if you can prove that the
                  // decisions are correlated (i.e. x < y implies z < w or
                  // vice versa), then there are simplifications to be
                  // made that tame x. Whether or not these
                  // simplifications are profitable depends on what terms
                  // end up being constant.

                  // If x < y implies z < w:
                  //   min(x, y) - min(z, w)
                  // = min(x - min(z, w), y - min(z, w))   using the distributive properties of min/max
                  // = min(x - z, y - min(z, w))           using the implication
                  // This duplicates z, so it's good if x - z causes some cancellation (e.g. they are equal)

                  // If, on the other hand, z < w implies x < y:
                  //   min(x, y) - min(z, w)
                  // = max(min(x, y) - z, min(x, y) - w)   using the distributive properties of min/max
                  // = max(x - z, min(x, y) - w)           using the implication
                  // Again, this is profitable when x - z causes some cancellation

                  // What follows are special cases of this general
                  // transformation where it is easy to see that x - z
                  // cancels and that there is an implication in one
                  // direction or the other.

                  // Then the actual rules. We consider only cases where x and z differ by a constant.
                  rewrite(min(x, y) - min(x, w), min(0, y - min(x, w)), can_prove(y <= w, this)) ||
                  rewrite(min(x, y) - min(x, w), max(0, min(x, y) - w), can_prove(y >= w, this)) ||
                  rewrite(min(x + c0, y) - min(x, w), min(c0, y - min(x, w)), can_prove(y <= w + c0, this)) ||
                  rewrite(min(x + c0, y) - min(x, w), max(c0, min(x + c0, y) - w), can_prove(y >= w + c0, this)) ||
                  rewrite(min(x, y) - min(x + c1, w), min(fold(-c1), y - min(x + c1, w)), can_prove(y + c1 <= w, this)) ||
                  rewrite(min(x, y) - min(x + c1, w), max(fold(-c1), min(x, y) - w), can_prove(y + c1 >= w, this)) ||
                  rewrite(min(x + c0, y) - min(x + c1, w), min(fold(c0 - c1), y - min(x + c1, w)), can_prove(y + c1 <= w + c0, this)) ||
                  rewrite(min(x + c0, y) - min(x + c1, w), max(fold(c0 - c1), min(x + c0, y) - w), can_prove(y + c1 >= w + c0, this)) ||

                  rewrite(min(y, x) - min(w, x), min(0, y - min(x, w)), can_prove(y <= w, this)) ||
                  rewrite(min(y, x) - min(w, x), max(0, min(x, y) - w), can_prove(y >= w, this)) ||
                  rewrite(min(y, x + c0) - min(w, x), min(c0, y - min(x, w)), can_prove(y <= w + c0, this)) ||
                  rewrite(min(y, x + c0) - min(w, x), max(c0, min(x + c0, y) - w), can_prove(y >= w + c0, this)) ||
                  rewrite(min(y, x) - min(w, x + c1), min(fold(-c1), y - min(x + c1, w)), can_prove(y + c1 <= w, this)) ||
                  rewrite(min(y, x) - min(w, x + c1), max(fold(-c1), min(x, y) - w), can_prove(y + c1 >= w, this)) ||
                  rewrite(min(y, x + c0) - min(w, x + c1), min(fold(c0 - c1), y - min(x + c1, w)), can_prove(y + c1 <= w + c0, this)) ||
                  rewrite(min(y, x + c0) - min(w, x + c1), max(fold(c0 - c1), min(x + c0, y) - w), can_prove(y + c1 >= w + c0, this)) ||

                  rewrite(min(x, y) - min(w, x), min(0, y - min(x, w)), can_prove(y <= w, this)) ||
                  rewrite(min(x, y) - min(w, x), max(0, min(x, y) - w), can_prove(y >= w, this)) ||
                  rewrite(min(x + c0, y) - min(w, x), min(c0, y - min(x, w)), can_prove(y <= w + c0, this)) ||
                  rewrite(min(x + c0, y) - min(w, x), max(c0, min(x + c0, y) - w), can_prove(y >= w + c0, this)) ||
                  rewrite(min(x, y) - min(w, x + c1), min(fold(-c1), y - min(x + c1, w)), can_prove(y + c1 <= w, this)) ||
                  rewrite(min(x, y) - min(w, x + c1), max(fold(-c1), min(x, y) - w), can_prove(y + c1 >= w, this)) ||
                  rewrite(min(x + c0, y) - min(w, x + c1), min(fold(c0 - c1), y - min(x + c1, w)), can_prove(y + c1 <= w + c0, this)) ||
                  rewrite(min(x + c0, y) - min(w, x + c1), max(fold(c0 - c1), min(x + c0, y) - w), can_prove(y + c1 >= w + c0, this)) ||

                  rewrite(min(y, x) - min(x, w), min(0, y - min(x, w)), can_prove(y <= w, this)) ||
                  rewrite(min(y, x) - min(x, w), max(0, min(x, y) - w), can_prove(y >= w, this)) ||
                  rewrite(min(y, x + c0) - min(x, w), min(c0, y - min(x, w)), can_prove(y <= w + c0, this)) ||
                  rewrite(min(y, x + c0) - min(x, w), max(c0, min(x + c0, y) - w), can_prove(y >= w + c0, this)) ||
                  rewrite(min(y, x) - min(x + c1, w), min(fold(-c1), y - min(x + c1, w)), can_prove(y + c1 <= w, this)) ||
                  rewrite(min(y, x) - min(x + c1, w), max(fold(-c1), min(x, y) - w), can_prove(y + c1 >= w, this)) ||
                  rewrite(min(y, x + c0) - min(x + c1, w), min(fold(c0 - c1), y - min(x + c1, w)), can_prove(y + c1 <= w + c0, this)) ||
                  rewrite(min(y, x + c0) - min(x + c1, w), max(fold(c0 - c1), min(x + c0, y) - w), can_prove(y + c1 >= w + c0, this)) ||

                  // The equivalent rules for max are what you'd
                  // expect. Just swap < and > and min and max (apply the
                  // isomorphism x -> -x).
                  rewrite(max(x, y) - max(x, w), max(0, y - max(x, w)), can_prove(y >= w, this)) ||
                  rewrite(max(x, y) - max(x, w), min(0, max(x, y) - w), can_prove(y <= w, this)) ||
                  rewrite(max(x + c0, y) - max(x, w), max(c0, y - max(x, w)), can_prove(y >= w + c0, this)) ||
                  rewrite(max(x + c0, y) - max(x, w), min(c0, max(x + c0, y) - w), can_prove(y <= w + c0, this)) ||
                  rewrite(max(x, y) - max(x + c1, w), max(fold(-c1), y - max(x + c1, w)), can_prove(y + c1 >= w, this)) ||
                  rewrite(max(x, y) - max(x + c1, w), min(fold(-c1), max(x, y) - w), can_prove(y + c1 <= w, this)) ||
                  rewrite(max(x + c0, y) - max(x + c1, w), max(fold(c0 - c1), y - max(x + c1, w)), can_prove(y + c1 >= w + c0, this)) ||
                  rewrite(max(x + c0, y) - max(x + c1, w), min(fold(c0 - c1), max(x + c0, y) - w), can_prove(y + c1 <= w + c0, this)) ||

                  rewrite(max(y, x) - max(w, x), max(0, y - max(x, w)), can_prove(y >= w, this)) ||
                  rewrite(max(y, x) - max(w, x), min(0, max(x, y) - w), can_prove(y <= w, this)) ||
                  rewrite(max(y, x + c0) - max(w, x), max(c0, y - max(x, w)), can_prove(y >= w + c0, this)) ||
                  rewrite(max(y, x + c0) - max(w, x), min(c0, max(x + c0, y) - w), can_prove(y <= w + c0, this)) ||
                  rewrite(max(y, x) - max(w, x + c1), max(fold(-c1), y - max(x + c1, w)), can_prove(y + c1 >= w, this)) ||
                  rewrite(max(y, x) - max(w, x + c1), min(fold(-c1), max(x, y) - w), can_prove(y + c1 <= w, this)) ||
                  rewrite(max(y, x + c0) - max(w, x + c1), max(fold(c0 - c1), y - max(x + c1, w)), can_prove(y + c1 >= w + c0, this)) ||
                  rewrite(max(y, x + c0) - max(w, x + c1), min(fold(c0 - c1), max(x + c0, y) - w), can_prove(y + c1 <= w + c0, this)) ||

                  rewrite(max(x, y) - max(w, x), max(0, y - max(x, w)), can_prove(y >= w, this)) ||
                  rewrite(max(x, y) - max(w, x), min(0, max(x, y) - w), can_prove(y <= w, this)) ||
                  rewrite(max(x + c0, y) - max(w, x), max(c0, y - max(x, w)), can_prove(y >= w + c0, this)) ||
                  rewrite(max(x + c0, y) - max(w, x), min(c0, max(x + c0, y) - w), can_prove(y <= w + c0, this)) ||
                  rewrite(max(x, y) - max(w, x + c1), max(fold(-c1), y - max(x + c1, w)), can_prove(y + c1 >= w, this)) ||
                  rewrite(max(x, y) - max(w, x + c1), min(fold(-c1), max(x, y) - w), can_prove(y + c1 <= w, this)) ||
                  rewrite(max(x + c0, y) - max(w, x + c1), max(fold(c0 - c1), y - max(x + c1, w)), can_prove(y + c1 >= w + c0, this)) ||
                  rewrite(max(x + c0, y) - max(w, x + c1), min(fold(c0 - c1), max(x + c0, y) - w), can_prove(y + c1 <= w + c0, this)) ||

                  rewrite(max(y, x) - max(x, w), max(0, y - max(x, w)), can_prove(y >= w, this)) ||
                  rewrite(max(y, x) - max(x, w), min(0, max(x, y) - w), can_prove(y <= w, this)) ||
                  rewrite(max(y, x + c0) - max(x, w), max(c0, y - max(x, w)), can_prove(y >= w + c0, this)) ||
                  rewrite(max(y, x + c0) - max(x, w), min(c0, max(x + c0, y) - w), can_prove(y <= w + c0, this)) ||
                  rewrite(max(y, x) - max(x + c1, w), max(fold(-c1), y - max(x + c1, w)), can_prove(y + c1 >= w, this)) ||
                  rewrite(max(y, x) - max(x + c1, w), min(fold(-c1), max(x, y) - w), can_prove(y + c1 <= w, this)) ||
                  rewrite(max(y, x + c0) - max(x + c1, w), max(fold(c0 - c1), y - max(x + c1, w)), can_prove(y + c1 >= w + c0, this)) ||
                  rewrite(max(y, x + c0) - max(x + c1, w), min(fold(c0 - c1), max(x + c0, y) - w), can_prove(y + c1 <= w + c0, this)))) ||

                (no_overflow_int(op->type) &&
                 (rewrite((x/c0)*c0 - x, -(x % c0), c0 > 0) ||
                  rewrite(x - (x/c0)*c0, x % c0, c0 > 0) ||
                  rewrite(((x + c0)/c1)*c1 - x, x % c1, c1 > 0 && c0 + 1 == c1) ||
                  rewrite(x - ((x + c0)/c1)*c1, -(x % c1), c1 > 0 && c0 + 1 == c1) ||
                  rewrite(x * c0 - y * c1, (x * fold(c0 / c1) - y) * c1, c0 % c1 == 0) ||
                  rewrite(x * c0 - y * c1, (x - y * fold(c1 / c0)) * c0, c1 % c0 == 0) ||
                  // Various forms of (x +/- a)/c - (x +/- b)/c. We can
                  // *almost* cancel the x.  The right thing to do depends
                  // on which of a or b is a constant, and we also need to
                  // catch the cases where that constant is zero.
                  rewrite((x + y)/c0 - (x + c1)/c0, (((x + fold(c1 % c0)) % c0) + (y - c1))/c0, c0 > 0) ||
                  rewrite((x + c1)/c0 - (x + y)/c0, ((fold(c0 + c1 - 1) - y) - ((x + fold(c1 % c0)) % c0))/c0, c0 > 0) ||
                  rewrite((x - y)/c0 - (x + c1)/c0, (((x + fold(c1 % c0)) % c0) - y - c1)/c0, c0 > 0) ||
                  rewrite((x + c1)/c0 - (x - y)/c0, ((y + fold(c0 + c1 - 1)) - ((x + fold(c1 % c0)) % c0))/c0, c0 > 0) ||
                  rewrite(x/c0 - (x + y)/c0, ((fold(c0 - 1) - y) - (x % c0))/c0, c0 > 0) ||
                  rewrite((x + y)/c0 - x/c0, ((x % c0) + y)/c0, c0 > 0) ||
                  rewrite(x/c0 - (x - y)/c0, ((y + fold(c0 - 1)) - (x % c0))/c0, c0 > 0) ||
                  rewrite((x - y)/c0 - x/c0, ((x % c0) - y)/c0, c0 > 0)))) {
                return mutate(std::move(rewrite.result), bounds);
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Sub::make(a, b);
        }
    }

    Expr visit(const Mul *op, ConstBounds *bounds) {
        ConstBounds a_bounds, b_bounds;
        Expr a = mutate(op->a, &a_bounds);
        Expr b = mutate(op->b, &b_bounds);

        if (bounds && no_overflow_int(op->type)) {
            bool a_positive = a_bounds.min_defined && a_bounds.min > 0;
            bool b_positive = b_bounds.min_defined && b_bounds.min > 0;
            bool a_bounded = a_bounds.min_defined && a_bounds.max_defined;
            bool b_bounded = b_bounds.min_defined && b_bounds.max_defined;

            if (a_bounded && b_bounded) {
                bounds->min_defined = bounds->max_defined = true;
                int64_t v1 = a_bounds.min * b_bounds.min;
                int64_t v2 = a_bounds.min * b_bounds.max;
                int64_t v3 = a_bounds.max * b_bounds.min;
                int64_t v4 = a_bounds.max * b_bounds.max;
                bounds->min = std::min(std::min(v1, v2), std::min(v3, v4));
                bounds->max = std::max(std::max(v1, v2), std::max(v3, v4));
            } else if ((a_bounds.max_defined && b_bounded && b_positive) ||
                       (b_bounds.max_defined && a_bounded && a_positive)) {
                bounds->max_defined = true;
                bounds->max = a_bounds.max * b_bounds.max;
            } else if ((a_bounds.min_defined && b_bounded && b_positive) ||
                       (b_bounds.min_defined && a_bounded && a_positive)) {
                bounds->min_defined = true;
                bounds->min = a_bounds.min * b_bounds.min;
            }
        }

        if (may_simplify(op->type)) {

            // Order commutative operations by node type
            if (a.node_type() < b.node_type()) {
                std::swap(a, b);
                std::swap(a_bounds, b_bounds);
            }


            auto indet = IRMatcher::indet(op->type);
            auto overflow = IRMatcher::overflow(op->type);

            auto rewrite = IRMatcher::rewriter(IRMatcher::mul(a, b));
            if (rewrite(c0 * c1, fold(c0 * c1)) ||
                rewrite(0 * x, a) ||
                rewrite(1 * x, b) ||
                rewrite(x * 0, b) ||
                rewrite(x * 1, a) ||
                rewrite(overflow * x, a) ||
                rewrite(x * overflow, b) ||
                rewrite(indet * x, a) ||
                rewrite(x * indet, b)) {
                return rewrite.result;
            }

            if (rewrite((x + c0) * c1, x * c1 + fold(c0 * c1)) ||
                rewrite((x - y) * c0, (y - x) * fold(-c0), c0 < 0 && -c0 > 0) || // If negating c0 causes overflow or UB, the predicate will be treated as false.
                rewrite((x * c0) * c1, x * fold(c0 * c1)) ||
                rewrite((x * c0) * y, (x * y) * c0) ||
                rewrite(x * (y * c0), (x * y) * c0) ||
                rewrite(max(x, y) * min(x, y), x * y) ||
                rewrite(max(x, y) * min(y, x), y * x) ||
                rewrite(broadcast(x) * broadcast(y), broadcast(x * y, op->type.lanes())) ||
                rewrite(ramp(x, y) * broadcast(z), ramp(x * z, y * z, op->type.lanes()))) {
                return mutate(std::move(rewrite.result), bounds);
            }

            const Shuffle *shuffle_a = a.as<Shuffle>();
            const Shuffle *shuffle_b = b.as<Shuffle>();
            if (shuffle_a && shuffle_b &&
                shuffle_a->is_slice() &&
                shuffle_b->is_slice()) {
                if (a.same_as(op->a) && b.same_as(op->b)) {
                    return hoist_slice_vector<Mul>(op);
                } else {
                    return hoist_slice_vector<Mul>(Mul::make(a, b));
                }
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Mul::make(a, b);
        }
    }

    Expr visit(const Div *op, ConstBounds *bounds) {
        ConstBounds a_bounds, b_bounds;
        Expr a = mutate(op->a, &a_bounds);
        Expr b = mutate(op->b, &b_bounds);

        if (bounds && no_overflow_int(op->type)) {
            bounds->min_defined = bounds->max_defined =
                (a_bounds.min_defined && b_bounds.min_defined &&
                 a_bounds.max_defined && b_bounds.max_defined &&
                 (b_bounds.min > 0 || b_bounds.max < 0));
            if (bounds->min_defined) {
                int64_t v1 = div_imp(a_bounds.min, b_bounds.min);
                int64_t v2 = div_imp(a_bounds.min, b_bounds.max);
                int64_t v3 = div_imp(a_bounds.max, b_bounds.min);
                int64_t v4 = div_imp(a_bounds.max, b_bounds.max);
                bounds->min = std::min(std::min(v1, v2), std::min(v3, v4));
                bounds->max = std::max(std::max(v1, v2), std::max(v3, v4));

                // Bounded numerator divided by constantish
                // denominator can sometimes collapse things to a
                // constant at this point.
                if (bounds->min == bounds->max) {
                    return make_const(op->type, bounds->min);
                }
            }
        }

        if (may_simplify(op->type)) {

            auto indet = IRMatcher::indet(op->type);
            auto overflow = IRMatcher::overflow(op->type);
            int lanes = op->type.lanes();

            auto rewrite = IRMatcher::rewriter(IRMatcher::div(a, b));

            if (rewrite(indet / x, a) ||
                rewrite(x / indet, b) ||
                rewrite(overflow / x, a) ||
                rewrite(x / overflow, b) ||
                (!op->type.is_float() && rewrite(x / 0, indet)) ||
                rewrite(x / 1, a) ||
                rewrite(0 / x, a) ||
                rewrite(x / x, IRMatcher::Const(1, op->type)) ||
                rewrite(c0 / c1, fold(c0 / c1))) {
                return rewrite.result;
            }

            if (rewrite(broadcast(x) / broadcast(y), broadcast(x / y, lanes)) ||
                (no_overflow(op->type) &&
                 (// Fold repeated division
                  rewrite((x / c0) / c2, x / fold(c0 * c2),                          c0 > 0 && c2 > 0) ||
                  rewrite((x / c0 + c1) / c2, (x + fold(c1 * c0)) / fold(c0 * c2),   c0 > 0 && c2 > 0) ||
                  rewrite((x * c0) / c1, x / fold(c1 / c0),                          c1 % c0 == 0 && c1 > 0) ||
                  // Pull out terms that are a multiple of the denominator
                  rewrite((x * c0) / c1, x * fold(c0 / c1),                          c0 % c1 == 0 && c1 > 0) ||

                  rewrite((x * c0 + y) / c1, y / c1 + x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||
                  rewrite((x * c0 - y) / c1, (-y) / c1 + x * fold(c0 / c1),          c0 % c1 == 0 && c1 > 0) ||
                  rewrite((y + x * c0) / c1, y / c1 + x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||
                  rewrite((y - x * c0) / c1, y / c1 - x * fold(c0 / c1),             c0 % c1 == 0 && c1 > 0) ||

                  rewrite(((x * c0 + y) + z) / c1, (y + z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite(((x * c0 - y) + z) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite(((x * c0 + y) - z) / c1, (y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite(((x * c0 - y) - z) / c1, x * fold(c0 / c1) - (y + z) / c1, c0 % c1 == 0 && c1 > 0) ||

                  rewrite(((y + x * c0) + z) / c1, (y + z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite(((y + x * c0) - z) / c1, (y - z) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite(((y - x * c0) - z) / c1, (y - z) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite(((y - x * c0) + z) / c1, (y + z) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

                  rewrite((z + (x * c0 + y)) / c1, (z + y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite((z + (x * c0 - y)) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite((z - (x * c0 - y)) / c1, (z + y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite((z - (x * c0 + y)) / c1, (z - y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

                  rewrite((z + (y + x * c0)) / c1, (z + y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite((z - (y + x * c0)) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite((z + (y - x * c0)) / c1, (z + y) / c1 - x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||
                  rewrite((z - (y - x * c0)) / c1, (z - y) / c1 + x * fold(c0 / c1), c0 % c1 == 0 && c1 > 0) ||

                  rewrite((x + c0) / c1, x / c1 + fold(c0 / c1),                     c0 % c1 == 0) ||
                  rewrite((x + y)/x, y/x + 1) ||
                  rewrite((y + x)/x, y/x + 1) ||
                  rewrite((x - y)/x, (-y)/x + 1) ||
                  rewrite((y - x)/x, y/x - 1) ||
                  rewrite(((x + y) + z)/x, (y + z)/x + 1) ||
                  rewrite(((y + x) + z)/x, (y + z)/x + 1) ||
                  rewrite((z + (x + y))/x, (z + y)/x + 1) ||
                  rewrite((z + (y + x))/x, (z + y)/x + 1) ||
                  rewrite((x*y)/x, y) ||
                  rewrite((y*x)/x, y) ||
                  rewrite((x*y + z)/x, y + z/x) ||
                  rewrite((y*x + z)/x, y + z/x) ||
                  rewrite((z + x*y)/x, z/x + y) ||
                  rewrite((z + y*x)/x, z/x + y) ||
                  rewrite((x*y - z)/x, y + (-z)/x) ||
                  rewrite((y*x - z)/x, y + (-z)/x) ||
                  rewrite((z - x*y)/x, z/x - y) ||
                  rewrite((z - y*x)/x, z/x - y) ||
                  (op->type.is_float() && rewrite(x/c0, x * fold(1/c0))))) ||
                (no_overflow_int(op->type) &&
                 (rewrite(ramp(x, c0) / broadcast(c1), ramp(x / c1, fold(c0 / c1), lanes), c0 % c1 == 0) ||
                  rewrite(ramp(x, c0) / broadcast(c1), broadcast(x / c1, lanes),
                          // First and last lanes are the same when...
                          can_prove((x % c1 + c0 * (lanes - 1)) / c1 == 0, this)))) ||
                (no_overflow_scalar_int(op->type) &&
                 (rewrite(x / -1, -x) ||
                  rewrite(c0 / y, select(y < 0, fold(-c0), c0), c0 == -1) ||
                  // In expressions of the form (x*a + b)/c, we can divide all the constants by gcd(a, c)
                  // E.g. (y*12 + 5)/9 = (y*4 + 2)/3
                  rewrite((x * c0 + c1) / c2,
                          (x * fold(c0 / c3) + fold(c1 / c3)) / fold(c2 / c3),
                          c2 > 0 && bind(c3, gcd(c0, c2)) && c3 > 1) ||
                  // A very specific pattern that comes up in bounds in upsampling code.
                  rewrite((x % 2 + c0) / 2, x % 2 + fold(c0 / 2), c0 % 2 == 1)))) {
                return mutate(std::move(rewrite.result), bounds);
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Div::make(a, b);
        }
    }

    Expr visit(const Mod *op, ConstBounds *bounds) {
        ConstBounds a_bounds, b_bounds;
        Expr a = mutate(op->a, &a_bounds);
        Expr b = mutate(op->b, &b_bounds);

        // Just use the bounds of the RHS
        if (bounds && no_overflow_int(op->type)) {
            bounds->min_defined = bounds->max_defined =
                (b_bounds.min_defined && b_bounds.max_defined &&
                 (b_bounds.min > 0 || b_bounds.max < 0));
            bounds->min = 0;
            bounds->max = std::max(std::abs(b_bounds.min), std::abs(b_bounds.max)) - 1;
        }

        if (may_simplify(op->type)) {

            auto indet = IRMatcher::indet(op->type);
            auto overflow = IRMatcher::overflow(op->type);
            int lanes = op->type.lanes();

            auto rewrite = IRMatcher::rewriter(IRMatcher::mod(a, b));

            if (rewrite(c0 % c1, fold(c0 % c1)) ||
                rewrite(0 % x, a) ||
                rewrite(x % c0, a, c0 > 0 && a_bounds.min_defined && a_bounds.max_defined && a_bounds.min >= 0 && a_bounds.max < c0) ||
                (!op->type.is_float() &&
                 (rewrite(x % indet, b) ||
                  rewrite(indet % x, a) ||
                  rewrite(x % overflow, b) ||
                  rewrite(overflow % x, a) ||
                  rewrite(x % 0, indet) ||
                  rewrite(x % 1, IRMatcher::Const(0, op->type))))) {
                return rewrite.result;
            }

            if (rewrite(broadcast(x) % broadcast(y), broadcast(x % y, lanes)) ||
                (no_overflow_int(op->type) &&
                 (rewrite((x * c0) % c1, (x * fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0)) ||
                  rewrite((x + c0) % c1, (x + fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0)) ||
                  rewrite((x * c0) % c1, (x % fold(c1/c0)) * c0, c1 % c0 == 0) ||
                  rewrite((x * c0 + y) % c1, y % c1, c0 % c1 == 0) ||
                  rewrite((y + x * c0) % c1, y % c1, c0 % c1 == 0) ||
                  rewrite(ramp(x, c0) % broadcast(c1), broadcast(x, lanes) % c1, c0 % c1 == 0) ||
                  rewrite(ramp(x, c0) % broadcast(c1), ramp(x % c1, c0, lanes),
                          // First and last lanes are the same when...
                          can_prove((x % c1 + c0 * (lanes - 1)) / c1 == 0, this)) ||
                  rewrite(ramp(x * c0, c2) % broadcast(c1), (ramp(x * fold(c0 % c1), fold(c2 % c1), lanes) % c1), c1 > 0 && (c0 >= c1 || c0 < 0)) ||
                  rewrite(ramp(x + c0, c2) % broadcast(c1), (ramp(x + fold(c0 % c1), fold(c2 % c1), lanes) % c1), c1 > 0 && (c0 >= c1 || c0 < 0)) ||
                  rewrite(ramp(x * c0 + y, c2) % broadcast(c1), ramp(y, fold(c2 % c1), lanes) % c1, c0 % c1 == 0) ||
                  rewrite(ramp(y + x * c0, c2) % broadcast(c1), ramp(y, fold(c2 % c1), lanes) % c1, c0 % c1 == 0)))) {
                return mutate(std::move(rewrite.result), bounds);
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Mod::make(a, b);
        }
    }

    Expr visit(const Min *op, ConstBounds *bounds) {
        ConstBounds a_bounds, b_bounds;
        Expr a = mutate(op->a, &a_bounds);
        Expr b = mutate(op->b, &b_bounds);

        if (bounds) {
            bounds->min_defined = a_bounds.min_defined && b_bounds.min_defined;
            bounds->max_defined = a_bounds.max_defined || b_bounds.max_defined;
            bounds->min = std::min(a_bounds.min, b_bounds.min);
            if (a_bounds.max_defined && b_bounds.max_defined) {
                bounds->max = std::min(a_bounds.max, b_bounds.max);
            } else if (a_bounds.max_defined) {
                bounds->max = a_bounds.max;
            } else {
                bounds->max = b_bounds.max;
            }
        }

        // Early out when the bounds tells us one side or the other is smaller
        if (a_bounds.max_defined && b_bounds.min_defined && a_bounds.max <= b_bounds.min) {
            return a;
        }
        if (b_bounds.max_defined && a_bounds.min_defined && b_bounds.max <= a_bounds.min) {
            return b;
        }

        if (may_simplify(op->type)) {

            // Order commutative operations by node type
            if (a.node_type() < b.node_type()) {
                std::swap(a, b);
                std::swap(a_bounds, b_bounds);
            }

            auto indet = IRMatcher::indet(op->type);
            auto overflow = IRMatcher::overflow(op->type);
            int lanes = op->type.lanes();
            auto rewrite = IRMatcher::rewriter(IRMatcher::min(a, b));

            if (rewrite(min(x, x), a) ||
                rewrite(min(indet, x), a) ||
                rewrite(min(x, indet), b) ||
                rewrite(min(overflow, x), a) ||
                rewrite(min(x, overflow), b) ||
                rewrite(min(c0, c1), fold(min(c0, c1))) ||

                // Cases where one side dominates:
                rewrite(min(x, op->type.min()), b) ||
                rewrite(min(x, op->type.max()), a) ||
                rewrite(min((x/c0)*c0, x), a, c0 > 0) ||
                rewrite(min(x, (x/c0)*c0), b, c0 > 0) ||
                rewrite(min(min(x, y), x), a) ||
                rewrite(min(min(x, y), y), a) ||
                rewrite(min(min(min(x, y), z), x), a) ||
                rewrite(min(min(min(x, y), z), y), a) ||
                rewrite(min(min(min(min(x, y), z), w), x), a) ||
                rewrite(min(min(min(min(x, y), z), w), y), a) ||
                rewrite(min(min(min(min(min(x, y), z), w), u), x), a) ||
                rewrite(min(min(min(min(min(x, y), z), w), u), y), a) ||
                rewrite(min(x, max(x, y)), a) ||
                rewrite(min(x, max(y, x)), a) ||
                rewrite(min(max(x, y), min(x, y)), b) ||
                rewrite(min(max(x, y), min(y, x)), b) ||
                rewrite(min(max(x, y), x), b) ||
                rewrite(min(max(y, x), x), b) ||
                rewrite(min(max(x, c0), c1), b, c1 <= c0) ||

                rewrite(min(intrin(Call::likely, x), x), a) ||
                rewrite(min(x, intrin(Call::likely, x)), b) ||
                rewrite(min(intrin(Call::likely_if_innermost, x), x), a) ||
                rewrite(min(x, intrin(Call::likely_if_innermost, x)), b) ||

                (no_overflow(op->type) &&
                 (rewrite(min(ramp(x, y), broadcast(z)), a, can_prove(x + y * (lanes - 1) <= z && x <= z, this)) ||
                  rewrite(min(ramp(x, y), broadcast(z)), b, can_prove(x + y * (lanes - 1) >= z && x >= z, this)) ||
                  // Compare x to a stair-step function in x
                  rewrite(min(((x + c0)/c1)*c1 + c2, x), b, c1 > 0 && c0 + c2 >= c1 - 1) ||
                  rewrite(min(x, ((x + c0)/c1)*c1 + c2), a, c1 > 0 && c0 + c2 >= c1 - 1) ||
                  rewrite(min(((x + c0)/c1)*c1 + c2, x), a, c1 > 0 && c0 + c2 <= 0) ||
                  rewrite(min(x, ((x + c0)/c1)*c1 + c2), b, c1 > 0 && c0 + c2 <= 0) ||
                  // Special cases where c0 or c2 is zero
                  rewrite(min((x/c1)*c1 + c2, x), b, c1 > 0 && c2 >= c1 - 1) ||
                  rewrite(min(x, (x/c1)*c1 + c2), a, c1 > 0 && c2 >= c1 - 1) ||
                  rewrite(min(((x + c0)/c1)*c1, x), b, c1 > 0 && c0 >= c1 - 1) ||
                  rewrite(min(x, ((x + c0)/c1)*c1), a, c1 > 0 && c0 >= c1 - 1) ||
                  rewrite(min((x/c1)*c1 + c2, x), a, c1 > 0 && c2 <= 0) ||
                  rewrite(min(x, (x/c1)*c1 + c2), b, c1 > 0 && c2 <= 0) ||
                  rewrite(min(((x + c0)/c1)*c1, x), a, c1 > 0 && c0 <= 0) ||
                  rewrite(min(x, ((x + c0)/c1)*c1), b, c1 > 0 && c0 <= 0)))) {
                return rewrite.result;
            }

            if (rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1)))) ||
                rewrite(min(min(x, c0), y), min(min(x, y), c0)) ||
                rewrite(min(min(x, y), min(x, z)), min(min(y, z), x)) ||
                rewrite(min(min(y, x), min(x, z)), min(min(y, z), x)) ||
                rewrite(min(min(x, y), min(z, x)), min(min(y, z), x)) ||
                rewrite(min(min(y, x), min(z, x)), min(min(y, z), x)) ||
                rewrite(min(min(x, y), min(z, w)), min(min(min(x, y), z), w)) ||
                rewrite(min(broadcast(x), broadcast(y)), broadcast(min(x, y), lanes)) ||
                rewrite(min(broadcast(x), ramp(y, z)), min(b, a)) ||
                rewrite(min(min(x, broadcast(y)), broadcast(z)), min(x, broadcast(min(y, z), lanes))) ||
                rewrite(min(max(x, y), max(x, z)), max(x, min(y, z))) ||
                rewrite(min(max(x, y), max(z, x)), max(x, min(y, z))) ||
                rewrite(min(max(y, x), max(x, z)), max(min(y, z), x)) ||
                rewrite(min(max(y, x), max(z, x)), max(min(y, z), x)) ||
                rewrite(min(max(min(x, y), z), y), min(max(x, z), y)) ||
                rewrite(min(max(min(y, x), z), y), min(y, max(x, z))) ||
                rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1)))) ||

                // Canonicalize a clamp
                rewrite(min(max(x, c0), c1), max(min(x, c1), c0), c0 <= c1) ||

                (no_overflow(op->type) &&
                 (rewrite(min(x + c0, c1), min(x, fold(c1 - c0)) + c0) ||

                  rewrite(min(x + y, x + z), x + min(y, z)) ||
                  rewrite(min(x + y, z + x), x + min(y, z)) ||
                  rewrite(min(y + x, x + z), min(y, z) + x) ||
                  rewrite(min(y + x, z + x), min(y, z) + x) ||
                  rewrite(min(x, x + z), x + min(z, 0)) ||
                  rewrite(min(x, z + x), x + min(z, 0)) ||
                  rewrite(min(y + x, x), min(y, 0) + x) ||
                  rewrite(min(x + y, x), x + min(y, 0)) ||

                  rewrite(min(min(x + y, z), x + w), min(x + min(y, w), z)) ||
                  rewrite(min(min(z, x + y), x + w), min(x + min(y, w), z)) ||
                  rewrite(min(min(x + y, z), w + x), min(x + min(y, w), z)) ||
                  rewrite(min(min(z, x + y), w + w), min(x + min(y, w), z)) ||

                  rewrite(min(min(y + x, z), x + w), min(min(y, w) + x, z)) ||
                  rewrite(min(min(z, y + x), x + w), min(min(y, w) + x, z)) ||
                  rewrite(min(min(y + x, z), w + x), min(min(y, w) + x, z)) ||
                  rewrite(min(min(z, y + x), w + w), min(min(y, w) + x, z)) ||

                  rewrite(min((x + w) + y, x + z), x + min(w + y, z)) ||
                  rewrite(min((w + x) + y, x + z), min(w + y, z) + x) ||
                  rewrite(min((x + w) + y, z + x), x + min(w + y, z)) ||
                  rewrite(min((w + x) + y, z + x), min(w + y, z) + x) ||
                  rewrite(min((x + w) + y, x), x + min(w + y, 0)) ||
                  rewrite(min((w + x) + y, x), x + min(w + y, 0)) ||
                  rewrite(min(x + y, (w + x) + z), x + min(w + z, y)) ||
                  rewrite(min(x + y, (x + w) + z), x + min(w + z, y)) ||
                  rewrite(min(y + x, (w + x) + z), min(w + z, y) + x) ||
                  rewrite(min(y + x, (x + w) + z), min(w + z, y) + x) ||
                  rewrite(min(x, (w + x) + z), x + min(w + z, 0)) ||
                  rewrite(min(x, (x + w) + z), x + min(w + z, 0)) ||

                  rewrite(min(y - x, z - x), min(y, z) - x) ||
                  rewrite(min(x - y, x - z), x - max(y, z)) ||

                  rewrite(min(x * c0, c1), min(x, fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
                  rewrite(min(x * c0, c1), max(x, fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||

                  rewrite(min(x * c0, y * c1), min(x, y * fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
                  rewrite(min(x * c0, y * c1), max(x, y * fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||
                  rewrite(min(x * c0, y * c1), min(x * fold(c0 / c1), y) * c0, c1 > 0 && c0 % c1 == 0) ||
                  rewrite(min(x * c0, y * c1), max(x * fold(c0 / c1), y) * c0, c1 < 0 && c0 % c1 == 0) ||
                  rewrite(min(x * c0, y * c0 + c1), min(x, y + fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
                  rewrite(min(x * c0, y * c0 + c1), max(x, y + fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||

                  rewrite(min(x / c0, y / c0), min(x, y) / c0, c0 > 0) ||
                  rewrite(min(x / c0, y / c0), max(x, y) / c0, c0 < 0) ||
                  rewrite(min(x / c0, y / c0 + c1), min(x, y + fold(c1 * c0)) / c0, c0 > 0) ||
                  rewrite(min(x / c0, y / c0 + c1), max(x, y + fold(c1 * c0)) / c0, c0 < 0) ||

                  rewrite(min(select(x, y, z), select(x, w, u)), select(x, min(y, w), min(z, u))) ||

                  rewrite(min(c0 - x, c1), c0 - max(x, fold(c0 - c1)))))) {

                return mutate(std::move(rewrite.result), bounds);
            }

        }

        const Shuffle *shuffle_a = a.as<Shuffle>();
        const Shuffle *shuffle_b = b.as<Shuffle>();
        if (shuffle_a && shuffle_b &&
            shuffle_a->is_slice() &&
            shuffle_b->is_slice()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return hoist_slice_vector<Min>(op);
            } else {
                return hoist_slice_vector<Min>(min(a, b));
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Min::make(a, b);
        }
    }

    Expr visit(const Max *op, ConstBounds *bounds) {
        ConstBounds a_bounds, b_bounds;
        Expr a = mutate(op->a, &a_bounds);
        Expr b = mutate(op->b, &b_bounds);

        if (bounds) {
            bounds->min_defined = a_bounds.min_defined || b_bounds.min_defined;
            bounds->max_defined = a_bounds.max_defined && b_bounds.max_defined;
            bounds->max = std::max(a_bounds.max, b_bounds.max);
            if (a_bounds.min_defined && b_bounds.min_defined) {
                bounds->min = std::max(a_bounds.min, b_bounds.min);
            } else if (a_bounds.min_defined) {
                bounds->min = a_bounds.min;
            } else {
                bounds->min = b_bounds.min;
            }
        }

        // Early out when the bounds tells us one side or the other is smaller
        if (a_bounds.max_defined && b_bounds.min_defined && a_bounds.max <= b_bounds.min) {
            return b;
        }
        if (b_bounds.max_defined && a_bounds.min_defined && b_bounds.max <= a_bounds.min) {
            return a;
        }

        if (may_simplify(op->type)) {

            // Order commutative operations by node type
            if (a.node_type() < b.node_type()) {
                std::swap(a, b);
                std::swap(a_bounds, b_bounds);
            }

            auto indet = IRMatcher::indet(op->type);
            auto overflow = IRMatcher::overflow(op->type);
            int lanes = op->type.lanes();
            auto rewrite = IRMatcher::rewriter(IRMatcher::max(a, b));

            if (rewrite(max(x, x), a) ||
                rewrite(max(indet, x), a) ||
                rewrite(max(x, indet), b) ||
                rewrite(max(overflow, x), a) ||
                rewrite(max(x, overflow), b) ||
                rewrite(max(c0, c1), fold(max(c0, c1))) ||

                // Cases where one side dominates:
                rewrite(max(x, op->type.max()), b) ||
                rewrite(max(x, op->type.min()), a) ||
                rewrite(max((x/c0)*c0, x), b, c0 > 0) ||
                rewrite(max(x, (x/c0)*c0), a, c0 > 0) ||
                rewrite(max(max(x, y), x), a) ||
                rewrite(max(max(x, y), y), a) ||
                rewrite(max(max(max(x, y), z), x), a) ||
                rewrite(max(max(max(x, y), z), y), a) ||
                rewrite(max(max(max(max(x, y), z), w), x), a) ||
                rewrite(max(max(max(max(x, y), z), w), y), a) ||
                rewrite(max(max(max(max(max(x, y), z), w), u), x), a) ||
                rewrite(max(max(max(max(max(x, y), z), w), u), y), a) ||
                rewrite(max(x, min(x, y)), a) ||
                rewrite(max(x, min(y, x)), a) ||
                rewrite(max(max(x, y), min(x, y)), a) ||
                rewrite(max(max(x, y), min(y, x)), a) ||
                rewrite(max(min(x, y), x), b) ||
                rewrite(max(min(y, x), x), b) ||
                rewrite(max(min(x, c0), c1), b, c1 >= c0) ||

                rewrite(max(intrin(Call::likely, x), x), a) ||
                rewrite(max(x, intrin(Call::likely, x)), b) ||
                rewrite(max(intrin(Call::likely_if_innermost, x), x), a) ||
                rewrite(max(x, intrin(Call::likely_if_innermost, x)), b) ||

                (no_overflow(op->type) &&
                 (rewrite(max(ramp(x, y), broadcast(z)), a, can_prove(x + y * (lanes - 1) >= z && x >= z, this)) ||
                  rewrite(max(ramp(x, y), broadcast(z)), b, can_prove(x + y * (lanes - 1) <= z && x <= z, this)) ||
                  // Compare x to a stair-step function in x
                  rewrite(max(((x + c0)/c1)*c1 + c2, x), a, c1 > 0 && c0 + c2 >= c1 - 1) ||
                  rewrite(max(x, ((x + c0)/c1)*c1 + c2), b, c1 > 0 && c0 + c2 >= c1 - 1) ||
                  rewrite(max(((x + c0)/c1)*c1 + c2, x), b, c1 > 0 && c0 + c2 <= 0) ||
                  rewrite(max(x, ((x + c0)/c1)*c1 + c2), a, c1 > 0 && c0 + c2 <= 0) ||
                  // Special cases where c0 or c2 is zero
                  rewrite(max((x/c1)*c1 + c2, x), a, c1 > 0 && c2 >= c1 - 1) ||
                  rewrite(max(x, (x/c1)*c1 + c2), b, c1 > 0 && c2 >= c1 - 1) ||
                  rewrite(max(((x + c0)/c1)*c1, x), a, c1 > 0 && c0 >= c1 - 1) ||
                  rewrite(max(x, ((x + c0)/c1)*c1), b, c1 > 0 && c0 >= c1 - 1) ||
                  rewrite(max((x/c1)*c1 + c2, x), b, c1 > 0 && c2 <= 0) ||
                  rewrite(max(x, (x/c1)*c1 + c2), a, c1 > 0 && c2 <= 0) ||
                  rewrite(max(((x + c0)/c1)*c1, x), b, c1 > 0 && c0 <= 0) ||
                  rewrite(max(x, ((x + c0)/c1)*c1), a, c1 > 0 && c0 <= 0)))) {
                return rewrite.result;
            }

            if (rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1)))) ||
                rewrite(max(max(x, c0), y), max(max(x, y), c0)) ||
                rewrite(max(max(x, y), max(x, z)), max(max(y, z), x)) ||
                rewrite(max(max(y, x), max(x, z)), max(max(y, z), x)) ||
                rewrite(max(max(x, y), max(z, x)), max(max(y, z), x)) ||
                rewrite(max(max(y, x), max(z, x)), max(max(y, z), x)) ||
                rewrite(max(max(x, y), max(z, w)), max(max(max(x, y), z), w)) ||
                rewrite(max(broadcast(x), broadcast(y)), broadcast(max(x, y), lanes)) ||
                rewrite(max(broadcast(x), ramp(y, z)), max(b, a)) ||
                rewrite(max(max(x, broadcast(y)), broadcast(z)), max(x, broadcast(max(y, z), lanes))) ||
                rewrite(max(min(x, y), min(x, z)), min(x, max(y, z))) ||
                rewrite(max(min(x, y), min(z, x)), min(x, max(y, z))) ||
                rewrite(max(min(y, x), min(x, z)), min(max(y, z), x)) ||
                rewrite(max(min(y, x), min(z, x)), min(max(y, z), x)) ||
                rewrite(max(min(max(x, y), z), y), max(min(x, z), y)) ||
                rewrite(max(min(max(y, x), z), y), max(y, min(x, z))) ||
                rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1)))) ||

                (no_overflow(op->type) &&
                 (rewrite(max(x + c0, c1), max(x, fold(c1 - c0)) + c0) ||

                  rewrite(max(x + y, x + z), x + max(y, z)) ||
                  rewrite(max(x + y, z + x), x + max(y, z)) ||
                  rewrite(max(y + x, x + z), max(y, z) + x) ||
                  rewrite(max(y + x, z + x), max(y, z) + x) ||
                  rewrite(max(x, x + z), x + max(z, 0)) ||
                  rewrite(max(x, z + x), x + max(z, 0)) ||
                  rewrite(max(y + x, x), max(y, 0) + x) ||
                  rewrite(max(x + y, x), x + max(y, 0)) ||

                  rewrite(max(max(x + y, z), x + w), max(x + max(y, w), z)) ||
                  rewrite(max(max(z, x + y), x + w), max(x + max(y, w), z)) ||
                  rewrite(max(max(x + y, z), w + x), max(x + max(y, w), z)) ||
                  rewrite(max(max(z, x + y), w + w), max(x + max(y, w), z)) ||

                  rewrite(max(max(y + x, z), x + w), max(max(y, w) + x, z)) ||
                  rewrite(max(max(z, y + x), x + w), max(max(y, w) + x, z)) ||
                  rewrite(max(max(y + x, z), w + x), max(max(y, w) + x, z)) ||
                  rewrite(max(max(z, y + x), w + w), max(max(y, w) + x, z)) ||

                  rewrite(max((x + w) + y, x + z), x + max(w + y, z)) ||
                  rewrite(max((w + x) + y, x + z), max(w + y, z) + x) ||
                  rewrite(max((x + w) + y, z + x), x + max(w + y, z)) ||
                  rewrite(max((w + x) + y, z + x), max(w + y, z) + x) ||
                  rewrite(max((x + w) + y, x), x + max(w + y, 0)) ||
                  rewrite(max((w + x) + y, x), x + max(w + y, 0)) ||
                  rewrite(max(x + y, (w + x) + z), x + max(w + z, y)) ||
                  rewrite(max(x + y, (x + w) + z), x + max(w + z, y)) ||
                  rewrite(max(y + x, (w + x) + z), max(w + z, y) + x) ||
                  rewrite(max(y + x, (x + w) + z), max(w + z, y) + x) ||
                  rewrite(max(x, (w + x) + z), x + max(w + z, 0)) ||
                  rewrite(max(x, (x + w) + z), x + max(w + z, 0)) ||

                  rewrite(max(y - x, z - x), max(y, z) - x) ||
                  rewrite(max(x - y, x - z), x - min(y, z)) ||

                  rewrite(max(x * c0, c1), max(x, fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
                  rewrite(max(x * c0, c1), min(x, fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||

                  rewrite(max(x * c0, y * c1), max(x, y * fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
                  rewrite(max(x * c0, y * c1), min(x, y * fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||
                  rewrite(max(x * c0, y * c1), max(x * fold(c0 / c1), y) * c0, c1 > 0 && c0 % c1 == 0) ||
                  rewrite(max(x * c0, y * c1), min(x * fold(c0 / c1), y) * c0, c1 < 0 && c0 % c1 == 0) ||
                  rewrite(max(x * c0, y * c0 + c1), max(x, y + fold(c1 / c0)) * c0, c0 > 0 && c1 % c0 == 0) ||
                  rewrite(max(x * c0, y * c0 + c1), min(x, y + fold(c1 / c0)) * c0, c0 < 0 && c1 % c0 == 0) ||

                  rewrite(max(x / c0, y / c0), max(x, y) / c0, c0 > 0) ||
                  rewrite(max(x / c0, y / c0), min(x, y) / c0, c0 < 0) ||
                  rewrite(max(x / c0, y / c0 + c1), max(x, y + fold(c1 * c0)) / c0, c0 > 0) ||
                  rewrite(max(x / c0, y / c0 + c1), min(x, y + fold(c1 * c0)) / c0, c0 < 0) ||

                  rewrite(max(select(x, y, z), select(x, w, u)), select(x, max(y, w), max(z, u))) ||

                  rewrite(max(c0 - x, c1), c0 - min(x, fold(c0 - c1)))))) {

                return mutate(std::move(rewrite.result), bounds);
            }
        }

        const Shuffle *shuffle_a = a.as<Shuffle>();
        const Shuffle *shuffle_b = b.as<Shuffle>();
        if (shuffle_a && shuffle_b &&
            shuffle_a->is_slice() &&
            shuffle_b->is_slice()) {
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return hoist_slice_vector<Max>(op);
            } else {
                return hoist_slice_vector<Max>(min(a, b));
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Max::make(a, b);
        }
    }

    Expr visit(const EQ *op, ConstBounds *bounds) {

        if (!may_simplify(op->a.type())) {
            Expr a = mutate(op->a, nullptr);
            Expr b = mutate(op->b, nullptr);
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return op;
            } else {
                return EQ::make(a, b);
            }
        }

        ConstBounds delta_bounds;
        Expr delta = mutate(op->a - op->b, &delta_bounds);

        if (delta_bounds.min_defined && delta_bounds.min > 0) {
            return const_false(op->type.lanes());
        }

        if (delta_bounds.max_defined && delta_bounds.max < 0) {
            return const_false(op->type.lanes());
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
        }

        if (broadcast) {
            // Push broadcasts outwards
            return Broadcast::make(mutate(broadcast->value ==
                                          make_zero(broadcast->value.type()), bounds),
                                   broadcast->lanes);
        } else if (add && is_const(add->b)) {
            // x + const = 0 -> x = -const
            return (add->a == mutate(make_zero(delta.type()) - add->b, nullptr));
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
            return mutate(mul->a == zero || mul->b == zero, bounds);
        } else if (sel && is_zero(sel->true_value)) {
            // select(c, 0, f) == 0 -> c || (f == 0)
            return mutate(sel->condition || (sel->false_value == zero), bounds);
        } else if (sel &&
                   (is_positive_const(sel->true_value) || is_negative_const(sel->true_value))) {
            // select(c, 4, f) == 0 -> !c && (f == 0)
            return mutate((!sel->condition) && (sel->false_value == zero), bounds);
        } else if (sel && is_zero(sel->false_value)) {
            // select(c, t, 0) == 0 -> !c || (t == 0)
            return mutate((!sel->condition) || (sel->true_value == zero), bounds);
        } else if (sel &&
                   (is_positive_const(sel->false_value) || is_negative_const(sel->false_value))) {
            // select(c, t, 4) == 0 -> c && (t == 0)
            return mutate((sel->condition) && (sel->true_value == zero), bounds);
        } else {
            return (delta == make_zero(delta.type()));
        }
    }

    Expr visit(const NE *op, ConstBounds *bounds) {
        if (!may_simplify(op->a.type())) {
            Expr a = mutate(op->a, nullptr);
            Expr b = mutate(op->b, nullptr);
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return op;
            } else {
                return NE::make(a, b);
            }
        }

        return mutate(Not::make(op->a == op->b), bounds);
    }

    Expr visit(const LT *op, ConstBounds *bounds) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);

        if (may_simplify(op->a.type())) {

            ConstBounds delta_bounds;
            Expr delta = mutate(a - b, &delta_bounds);

            if (delta_bounds.min_defined && delta_bounds.min >= 0) {
                return const_false(op->type.lanes());
            }
            if (delta_bounds.max_defined && delta_bounds.max < 0) {
                return const_true(op->type.lanes());
            }

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
            double fa, fb;

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
            } else if (const_float(a, &fa) &&
                       const_float(b, &fb)) {
                return make_bool(fa < fb, op->type.lanes());
            } else if (const_int(a, &ia) &&
                       a.type().is_max(ia)) {
                // Comparing maximum of type < expression of type.  This can never be true.
                return const_false(op->type.lanes());
            } else if (const_int(b, &ib) &&
                       b.type().is_min(ib)) {
                // Comparing expression of type < minimum of type.  This can never be true.
                return const_false(op->type.lanes());
            } else if (broadcast_a &&
                       broadcast_b) {
                // Push broadcasts outwards
                return mutate(Broadcast::make(broadcast_a->value < broadcast_b->value, broadcast_a->lanes), bounds);
            }

            if (no_overflow(delta.type())) {
                if (ramp_a &&
                    ramp_b &&
                    equal(ramp_a->stride, ramp_b->stride)) {
                    // Ramps with matching stride
                    Expr bases_lt = (ramp_a->base < ramp_b->base);
                    return mutate(Broadcast::make(bases_lt, ramp_a->lanes), bounds);
                } else if (add_a &&
                           add_b &&
                           equal(add_a->a, add_b->a)) {
                    // Subtract a term from both sides
                    return mutate(add_a->b < add_b->b, bounds);
                } else if (add_a &&
                           add_b &&
                           equal(add_a->a, add_b->b)) {
                    return mutate(add_a->b < add_b->a, bounds);
                } else if (add_a &&
                           add_b &&
                           equal(add_a->b, add_b->a)) {
                    return mutate(add_a->a < add_b->b, bounds);
                } else if (add_a &&
                           add_b &&
                           equal(add_a->b, add_b->b)) {
                    return mutate(add_a->a < add_b->a, bounds);
                } else if (sub_a &&
                           sub_b &&
                           equal(sub_a->a, sub_b->a)) {
                    // Add a term to both sides and negate.
                    return mutate(sub_b->b < sub_a->b, bounds);
                } else if (sub_a &&
                           sub_b &&
                           equal(sub_a->b, sub_b->b)) {
                    return mutate(sub_a->a < sub_b->a, bounds);
                } else if (add_a) {
                    // Rearrange so that all adds and subs are on the rhs to cut down on further cases
                    return mutate(add_a->a < (b - add_a->b), bounds);
                } else if (sub_a) {
                    return mutate(sub_a->a < (b + sub_a->b), bounds);
                } else if (add_b &&
                           equal(add_b->a, a)) {
                    // Subtract a term from both sides
                    return mutate(make_zero(add_b->b.type()) < add_b->b, bounds);
                } else if (add_b &&
                           equal(add_b->b, a)) {
                    return mutate(make_zero(add_b->a.type()) < add_b->a, bounds);
                } else if (add_b &&
                           is_simple_const(a) &&
                           is_simple_const(add_b->b)) {
                    // a < x + b -> (a - b) < x
                    return mutate((a - add_b->b) < add_b->a, bounds);
                } else if (sub_b &&
                           equal(sub_b->a, a)) {
                    // Subtract a term from both sides
                    return mutate(sub_b->b < make_zero(sub_b->b.type()), bounds);
                } else if (sub_b &&
                           is_const(a) &&
                           is_const(sub_b->a) &&
                           !is_const(sub_b->b)) {
                    // (c1 < c2 - x) -> (x < c2 - c1)
                    return mutate(sub_b->b < (sub_b->a - a), bounds);
                } else if (mul_a &&
                           mul_b &&
                           is_positive_const(mul_a->b) &&
                           is_positive_const(mul_b->b) &&
                           equal(mul_a->b, mul_b->b)) {
                    // Divide both sides by a constant
                    return mutate(mul_a->a < mul_b->a, bounds);
                } else if (mul_a &&
                           is_positive_const(mul_a->b) &&
                           is_const(b)) {
                    if (mul_a->type.is_int()) {
                        // (a * c1 < c2) <=> (a < (c2 - 1) / c1 + 1)
                        return mutate(mul_a->a < (((b - 1) / mul_a->b) + 1), bounds);
                    } else {
                        // (a * c1 < c2) <=> (a < c2 / c1)
                        return mutate(mul_a->a < (b / mul_a->b), bounds);
                    }
                } else if (mul_b &&
                           is_positive_const(mul_b->b) &&
                           is_simple_const(mul_b->b) &&
                           is_simple_const(a)) {
                    // (c1 < b * c2) <=> ((c1 / c2) < b)
                    return mutate((a / mul_b->b) < mul_b->a, bounds);
                } else if (a.type().is_int() &&
                           div_a &&
                           is_positive_const(div_a->b) &&
                           is_const(b)) {
                    // a / c1 < c2 <=> a < c1*c2
                    return mutate(div_a->a < (div_a->b * b), bounds);
                } else if (a.type().is_int() &&
                           div_b &&
                           is_positive_const(div_b->b) &&
                           is_const(a)) {
                    // c1 < b / c2 <=> (c1+1)*c2-1 < b
                    Expr one = make_one(a.type());
                    return mutate((a + one)*div_b->b - one < div_b->a, bounds);
                } else if (min_a) {
                    // (min(a, b) < c) <=> (a < c || b < c)
                    // See if that would simplify usefully:
                    Expr lt_a = mutate(min_a->a < b, nullptr);
                    Expr lt_b = mutate(min_a->b < b, nullptr);
                    if (is_const(lt_a) || is_const(lt_b)) {
                        return mutate(lt_a || lt_b, bounds);
                    } else if (a.same_as(op->a) && b.same_as(op->b)) {
                        return op;
                    } else {
                        return LT::make(a, b);
                    }
                } else if (max_a) {
                    // (max(a, b) < c) <=> (a < c && b < c)
                    Expr lt_a = mutate(max_a->a < b, nullptr);
                    Expr lt_b = mutate(max_a->b < b, nullptr);
                    if (is_const(lt_a) || is_const(lt_b)) {
                        return mutate(lt_a && lt_b, bounds);
                    } else if (a.same_as(op->a) && b.same_as(op->b)) {
                        return op;
                    } else {
                        return LT::make(a, b);
                    }
                } else if (min_b) {
                    // (a < min(b, c)) <=> (a < b && a < c)
                    Expr lt_a = mutate(a < min_b->a, nullptr);
                    Expr lt_b = mutate(a < min_b->b, nullptr);
                    if (is_const(lt_a) || is_const(lt_b)) {
                        return mutate(lt_a && lt_b, bounds);
                    } else if (a.same_as(op->a) && b.same_as(op->b)) {
                        return op;
                    } else {
                        return LT::make(a, b);
                    }
                } else if (max_b) {
                    // (a < max(b, c)) <=> (a < b || a < c)
                    Expr lt_a = mutate(a < max_b->a, nullptr);
                    Expr lt_b = mutate(a < max_b->b, nullptr);
                    if (is_const(lt_a) || is_const(lt_b)) {
                        return mutate(lt_a || lt_b, bounds);
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
                    return mutate(0 < b % make_const(a.type(), ia), bounds);
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
                    return mutate(0 < add_b->a % div_a_a->b + add_b->b, bounds);
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
                    return mutate(sub_b->b < sub_b->a % div_a_a->b, bounds);
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
                    return mutate(add_a_a_a->b < div_a_a->a % div_a_a->b, bounds);
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
                    return mutate(add_a_a_a->b < div_a_a->a % div_a_a->b + add_b->b, bounds);
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
                    return mutate(sub_b->b < div_a_a->a % div_a_a->b + make_const(a.type(), -ic), bounds);
                } else if (delta_ramp &&
                           is_positive_const(delta_ramp->stride) &&
                           is_one(mutate(delta_ramp->base + delta_ramp->stride*(delta_ramp->lanes - 1) < 0, nullptr))) {
                    return const_true(delta_ramp->lanes);
                } else if (delta_ramp &&
                           is_positive_const(delta_ramp->stride) &&
                           is_one(mutate(delta_ramp->base >= 0, nullptr))) {
                    return const_false(delta_ramp->lanes);
                } else if (delta_ramp &&
                           is_negative_const(delta_ramp->stride) &&
                           is_one(mutate(delta_ramp->base < 0, nullptr))) {
                    return const_true(delta_ramp->lanes);
                } else if (delta_ramp &&
                           is_negative_const(delta_ramp->stride) &&
                           is_one(mutate(delta_ramp->base + delta_ramp->stride*(delta_ramp->lanes - 1) >= 0, nullptr))) {
                    return const_false(delta_ramp->lanes);
                } else if (delta_ramp && mod_rem.modulus > 0 &&
                           const_int(delta_ramp->stride, &ia) &&
                           0 <= ia * (delta_ramp->lanes - 1) + mod_rem.remainder &&
                           ia * (delta_ramp->lanes - 1) + mod_rem.remainder < mod_rem.modulus) {
                    // ramp(x, a, b) < 0 -> broadcast(x < 0, b)
                    return Broadcast::make(mutate(LT::make(delta_ramp->base / mod_rem.modulus, 0), bounds), delta_ramp->lanes);
                }
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return LT::make(a, b);
        }
    }

    Expr visit(const LE *op, ConstBounds *bounds) {
        if (!may_simplify(op->a.type())) {
            Expr a = mutate(op->a, nullptr);
            Expr b = mutate(op->b, nullptr);
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return op;
            } else {
                return LE::make(a, b);
            }
        }

        return mutate(!(op->b < op->a), bounds);
    }

    Expr visit(const GT *op, ConstBounds *bounds) {
        if (!may_simplify(op->a.type())) {
            Expr a = mutate(op->a, nullptr);
            Expr b = mutate(op->b, nullptr);
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return op;
            } else {
                return GT::make(a, b);
            }
        }

        return mutate(op->b < op->a, bounds);
    }

    Expr visit(const GE *op, ConstBounds *bounds) {
        if (!may_simplify(op->a.type())) {
            Expr a = mutate(op->a, nullptr);
            Expr b = mutate(op->b, nullptr);
            if (a.same_as(op->a) && b.same_as(op->b)) {
                return op;
            } else {
                return GE::make(a, b);
            }
        }

        return mutate(!(op->a < op->b), bounds);
    }

    Expr visit(const And *op, ConstBounds *bounds) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);

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
            return mutate(le_a->a <= min(le_a->b, le_b->b), bounds);
        } else if (le_a &&
                   le_b &&
                   equal(le_a->b, le_b->b)) {
            // (foo <= x && bar <= x) -> max(foo, bar) <= x
            return mutate(max(le_a->a, le_b->a) <= le_a->b, bounds);
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->a, lt_b->a)) {
            // (x < foo && x < bar) -> x < min(foo, bar)
            return mutate(lt_a->a < min(lt_a->b, lt_b->b), bounds);
        } else if (lt_a &&
                   lt_b &&
                   equal(lt_a->b, lt_b->b)) {
            // (foo < x && bar < x) -> max(foo, bar) < x
            return mutate(max(lt_a->a, lt_b->a) < lt_a->b, bounds);
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
            return mutate(And::make(a, NE::make(eq_a->b, neq_b->b)), bounds);
        } else if (neq_a &&
                   eq_b &&
                   equal(neq_a->a, eq_b->a) &&
                   is_simple_const(neq_a->b) &&
                   is_simple_const(eq_b->b)) {
            // (a != k1) && (a == k2) -> (a == k2) && (k1 != k2)
            // (second term always folds away)
            return mutate(And::make(b, NE::make(neq_a->b, eq_b->b)), bounds);
        } else if (eq_a &&
                   eq_a->a.as<Variable>() &&
                   is_simple_const(eq_a->b) &&
                   expr_uses_var(b, eq_a->a.as<Variable>()->name)) {
            // (somevar == k) && b -> (somevar == k) && substitute(somevar, k, b)
            return mutate(And::make(a, substitute(eq_a->a.as<Variable>(), eq_a->b, b)), bounds);
        } else if (eq_b &&
                   eq_b->a.as<Variable>() &&
                   is_simple_const(eq_b->b) &&
                   expr_uses_var(a, eq_b->a.as<Variable>()->name)) {
            // a && (somevar == k) -> substitute(somevar, k1, a) && (somevar == k)
            return mutate(And::make(substitute(eq_b->a.as<Variable>(), eq_b->b, a), b), bounds);
        } else if (broadcast_a &&
                   broadcast_b &&
                   broadcast_a->lanes == broadcast_b->lanes) {
            // x8(a) && x8(b) -> x8(a && b)
            return Broadcast::make(mutate(And::make(broadcast_a->value, broadcast_b->value), bounds), broadcast_a->lanes);
        } else if (var_a && expr_uses_var(b, var_a->name)) {
            return mutate(a && substitute(var_a->name, make_one(a.type()), b), bounds);
        } else if (var_b && expr_uses_var(a, var_b->name)) {
            return mutate(substitute(var_b->name, make_one(b.type()), a) && b, bounds);
        } else if (a.same_as(op->a) &&
                   b.same_as(op->b)) {
            return op;
        } else {
            return And::make(a, b);
        }
    }

    Expr visit(const Or *op, ConstBounds *bounds) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);

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
            return Broadcast::make(mutate(Or::make(broadcast_a->value, broadcast_b->value), bounds), broadcast_a->lanes);
        } else if (eq_a &&
                   neq_b &&
                   equal(eq_a->a, neq_b->a) &&
                   is_simple_const(eq_a->b) &&
                   is_simple_const(neq_b->b)) {
            // (a == k1) || (a != k2) -> (a != k2) || (k1 == k2)
            // (second term always folds away)
            return mutate(Or::make(b, EQ::make(eq_a->b, neq_b->b)), bounds);
        } else if (neq_a &&
                   eq_b &&
                   equal(neq_a->a, eq_b->a) &&
                   is_simple_const(neq_a->b) &&
                   is_simple_const(eq_b->b)) {
            // (a != k1) || (a == k2) -> (a != k1) || (k1 == k2)
            // (second term always folds away)
            return mutate(Or::make(a, EQ::make(neq_a->b, eq_b->b)), bounds);
        } else if (var_a && expr_uses_var(b, var_a->name)) {
            return mutate(a || substitute(var_a->name, make_zero(a.type()), b), bounds);
        } else if (var_b && expr_uses_var(a, var_b->name)) {
            return mutate(substitute(var_b->name, make_zero(b.type()), a) || b, bounds);
        } else if (is_var_simple_const_comparison(b, &name_c) &&
                   and_a &&
                   ((is_var_simple_const_comparison(and_a->a, &name_a) && name_a == name_c) ||
                    (is_var_simple_const_comparison(and_a->b, &name_b) && name_b == name_c))) {
            // (a && b) || (c) -> (a || c) && (b || c)
            // iff c and at least one of a or b is of the form
            //     (var == const) or (var != const)
            // (and the vars are the same)
            return mutate(And::make(Or::make(and_a->a, b), Or::make(and_a->b, b)), bounds);
        } else if (is_var_simple_const_comparison(a, &name_c) &&
                   and_b &&
                   ((is_var_simple_const_comparison(and_b->a, &name_a) && name_a == name_c) ||
                    (is_var_simple_const_comparison(and_b->b, &name_b) && name_b == name_c))) {
            // (c) || (a && b) -> (a || c) && (b || c)
            // iff c and at least one of a or b is of the form
            //     (var == const) or (var != const)
            // (and the vars are the same)
            return mutate(And::make(Or::make(and_b->a, a), Or::make(and_b->b, a)), bounds);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Or::make(a, b);
        }
    }

    Expr visit(const Not *op, ConstBounds *bounds) {
        Expr a = mutate(op->a, nullptr);

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
            return mutate(Broadcast::make(!n->value, n->lanes), bounds);
        } else if ((c = a.as<Call>()) != nullptr && c->is_intrinsic(Call::likely)) {
            // !likely(e) -> likely(!e)
            return likely(mutate(Not::make(c->args[0]), bounds));
        } else if (a.same_as(op->a)) {
            return op;
        } else {
            return Not::make(a);
        }
    }

    Expr visit(const Select *op, ConstBounds *bounds) {

        ConstBounds t_bounds, f_bounds;
        Expr condition = mutate(op->condition, nullptr);
        Expr true_value = mutate(op->true_value, &t_bounds);
        Expr false_value = mutate(op->false_value, &f_bounds);

        if (bounds) {
            bounds->min_defined = t_bounds.min_defined && f_bounds.min_defined;
            bounds->max_defined = t_bounds.max_defined && f_bounds.max_defined;
            bounds->min = std::min(t_bounds.min, f_bounds.min);
            bounds->max = std::max(t_bounds.max, f_bounds.max);
        }

        if (may_simplify(op->type)) {
            auto indet = IRMatcher::indet(op->type);
            auto rewrite = IRMatcher::rewriter(IRMatcher::select(condition, true_value, false_value));

            if (rewrite(select(indet, x, y), indet) ||
                rewrite(select(x, indet, y), indet) ||
                rewrite(select(x, y, indet), indet) ||
                rewrite(select(1, x, y), x) ||
                rewrite(select(0, x, y), y) ||
                rewrite(select(x, y, y), y) ||
                rewrite(select(x, intrin(Call::likely, y), y), true_value) ||
                rewrite(select(x, y, intrin(Call::likely, y)), false_value)) {
                return rewrite.result;
            }

            if (rewrite(select(broadcast(x), y, z), select(x, y, z)) ||
                rewrite(select(x != y, z, w), select(x == y, w, z)) ||
                rewrite(select(x <= y, z, w), select(y < x, w, z)) ||
                rewrite(select(x, select(y, z, w), z), select(x && !y, w, z)) ||
                rewrite(select(x, select(y, z, w), w), select(x && y, z, w)) ||
                rewrite(select(x, y, select(z, y, w)), select(x || z, y, w)) ||
                rewrite(select(x, y, select(z, w, y)), select(x || !z, y, w)) ||
                rewrite(select(x, select(x, y, z), w), select(x, y, w)) ||
                rewrite(select(x, y, select(x, z, w)), select(x, y, w)) ||
                rewrite(select(x, y + z, y + w), y + select(x, z, w)) ||
                rewrite(select(x, y + z, w + y), y + select(x, z, w)) ||
                rewrite(select(x, z + y, y + w), y + select(x, z, w)) ||
                rewrite(select(x, z + y, w + y), select(x, z, w) + y) ||
                rewrite(select(x, y - z, y - w), y - select(x, z, w)) ||
                rewrite(select(x, y - z, y + w), y + select(x, -z, w)) ||
                rewrite(select(x, y + z, y - w), y + select(x, z, -w)) ||
                rewrite(select(x, y - z, w + y), y + select(x, -z, w)) ||
                rewrite(select(x, z + y, y - w), y + select(x, z, -w)) ||
                rewrite(select(x, z - y, w - y), select(x, z, w) - y) ||
                rewrite(select(x, y * z, y * w), y * select(x, z, w)) ||
                rewrite(select(x, y * z, w * y), y * select(x, z, w)) ||
                rewrite(select(x, z * y, y * w), y * select(x, z, w)) ||
                rewrite(select(x, z * y, w * y), select(x, z, w) * y) ||
                (op->type.is_bool() &&
                 (rewrite(select(x, 1, 0), cast(op->type, x)) ||
                  rewrite(select(x, 0, 1), cast(op->type, !x))))) {
                return mutate(std::move(rewrite.result), bounds);
            }
        }

        if (condition.same_as(op->condition) &&
            true_value.same_as(op->true_value) &&
            false_value.same_as(op->false_value)) {
            return op;
        } else {
            return Select::make(std::move(condition), std::move(true_value), std::move(false_value));
        }

    }

    Expr visit(const Ramp *op, ConstBounds *bounds) {
        ConstBounds base_bounds, stride_bounds;
        Expr base = mutate(op->base, &base_bounds);
        Expr stride = mutate(op->stride, &stride_bounds);
        const int lanes = op->type.lanes();

        if (bounds && no_overflow_int(op->type)) {
            bounds->min_defined = base_bounds.min_defined && stride_bounds.min_defined;
            bounds->max_defined = base_bounds.max_defined && stride_bounds.max_defined;
            bounds->min = std::min(base_bounds.min, base_bounds.min + (lanes - 1) * stride_bounds.min);
            bounds->max = std::max(base_bounds.max, base_bounds.max + (lanes - 1) * stride_bounds.max);
        }

        // A somewhat torturous way to check if the stride is zero,
        // but it helps to have as many rules as possible written as
        // formal rewrites, so that they can be formally verified,
        // etc.
        auto rewrite = IRMatcher::rewriter(IRMatcher::ramp(base, stride));
        if (rewrite(ramp(x, 0), broadcast(x, lanes))) {
            return rewrite.result;
        }

        if (base.same_as(op->base) &&
            stride.same_as(op->stride)) {
            return op;
        } else {
            return Ramp::make(base, stride, op->lanes);
        }
    }

    Stmt visit(const IfThenElse *op) {
        Expr condition = mutate(op->condition, nullptr);

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

    Expr visit(const Load *op, ConstBounds *bounds) {
        found_buffer_reference(op->name);

        Expr predicate = mutate(op->predicate, nullptr);
        Expr index = mutate(op->index, nullptr);

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

    Expr visit(const Call *op, ConstBounds *bounds) {
        // Calls implicitly depend on host, dev, mins, and strides of the buffer referenced
        if (op->call_type == Call::Image || op->call_type == Call::Halide) {
            found_buffer_reference(op->name, op->args.size());
        }

        if (op->is_intrinsic(Call::strict_float)) {
            ScopedValue<bool> save_no_float_simplify(no_float_simplify, true);
            Expr arg = mutate(op->args[0], nullptr);
            if (arg.same_as(op->args[0])) {
                return op;
            } else {
                return strict_float(arg);
            }
        } else if (op->is_intrinsic(Call::shift_left) ||
                   op->is_intrinsic(Call::shift_right)) {
            Expr a = mutate(op->args[0], nullptr);
            Expr b = mutate(op->args[1], nullptr);

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
                        return mutate(Mul::make(a, b), bounds);
                    } else {
                        return mutate(Div::make(a, b), bounds);
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
            Expr a = mutate(op->args[0], nullptr);
            Expr b = mutate(op->args[1], nullptr);

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
            Expr a = mutate(op->args[0], nullptr);
            Expr b = mutate(op->args[1], nullptr);

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
            Expr a = mutate(op->args[0], nullptr);

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
            Expr a = mutate(op->args[0], nullptr);

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
            ConstBounds a_bounds;
            Expr a = mutate(op->args[0], &a_bounds);

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
            } else if (a_bounds.min_defined && a_bounds.min >= 0) {
                return a;
            } else if (a_bounds.max_defined && a_bounds.max <= 0) {
                return -a;
            } else if (a.same_as(op->args[0])) {
                return op;
            } else {
                return abs(a);
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "is_nan_f32") {
            Expr arg = mutate(op->args[0], nullptr);
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
                Expr arg = mutate(op->args[i], nullptr);
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
            Expr arg = mutate(op->args[0], nullptr);

            if (const double *f = as_const_float(arg)) {
                return FloatImm::make(arg.type(), std::sqrt(*f));
            } else if (!arg.same_as(op->args[0])) {
                return Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                return op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "log_f32") {
            Expr arg = mutate(op->args[0], nullptr);

            if (const double *f = as_const_float(arg)) {
                return FloatImm::make(arg.type(), std::log(*f));
            } else if (!arg.same_as(op->args[0])) {
                return Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                return op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "exp_f32") {
            Expr arg = mutate(op->args[0], nullptr);

            if (const double *f = as_const_float(arg)) {
                return FloatImm::make(arg.type(), std::exp(*f));
            } else if (!arg.same_as(op->args[0])) {
                return Call::make(op->type, op->name, {arg}, op->call_type);
            } else {
                return op;
            }
        } else if (op->call_type == Call::PureExtern &&
                   op->name == "pow_f32") {
            Expr arg0 = mutate(op->args[0], nullptr);
            Expr arg1 = mutate(op->args[1], nullptr);

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
            Expr arg = mutate(op->args[0], nullptr);

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
                args[i] = mutate(op->args[i], nullptr);
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
                        Expr new_extent = mutate(extent_0 * extent_1, nullptr);
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
            Expr cond = mutate(op->args[0], nullptr);
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
            Expr arg = mutate(op->args[1], bounds);

            if (is_one(cond)) {
                return arg;
            }

            if (is_zero(cond)) {
                // (We could simplify this to avoid evaluating the provably-false
                // expression, but since this is a degenerate condition, don't bother.)
                user_warning << "This pipeline is guaranteed to fail a require() expression at runtime: \n"
                             << Expr(op) << "\n";
            }

            if (cond.same_as(op->args[0]) && arg.same_as(op->args[1])) {
                return op;
            } else {
                return require(cond, arg);
            }
        } else {
            vector<Expr> new_args(op->args.size());
            bool changed = false;

            // Mutate the args
            for (size_t i = 0; i < op->args.size(); i++) {
                const Expr &old_arg = op->args[i];
                Expr new_arg = mutate(old_arg, nullptr);
                if (!new_arg.same_as(old_arg)) changed = true;
                new_args[i] = std::move(new_arg);
            }

            if (!changed) {
                return op;
            } else {
                return Call::make(op->type, op->name, new_args, op->call_type,
                                  op->func, op->value_index, op->image, op->param);
            }
        }
    }

    Expr visit(const Shuffle *op, ConstBounds *bounds) {
        if (op->is_extract_element() &&
            (op->vectors[0].as<Ramp>() ||
             op->vectors[0].as<Broadcast>())) {
            // Extracting a single lane of a ramp or broadcast
            if (const Ramp *r = op->vectors[0].as<Ramp>()) {
                return mutate(r->base + op->indices[0]*r->stride, bounds);
            } else if (const Broadcast *b = op->vectors[0].as<Broadcast>()) {
                return mutate(b->value, bounds);
            } else {
                internal_error << "Unreachable";
                return Expr();
            }
        }

        // Mutate the vectors
        vector<Expr> new_vectors;
        bool changed = false;
        for (Expr vector : op->vectors) {
            ConstBounds v_bounds;
            Expr new_vector = mutate(vector, &v_bounds);
            if (!vector.same_as(new_vector)) {
                changed = true;
            }
            if (bounds) {
                if (new_vectors.empty()) {
                    *bounds = v_bounds;
                } else {
                    bounds->min_defined &= v_bounds.min_defined;
                    bounds->max_defined &= v_bounds.max_defined;
                    bounds->min = std::min(bounds->min, v_bounds.min);
                    bounds->max = std::max(bounds->max, v_bounds.max);
                }
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
                shuffled_index = mutate(shuffled_index, nullptr);
                if (shuffled_index.as<Ramp>()) {
                    Expr shuffled_predicate;
                    if (unpredicated) {
                        shuffled_predicate = const_true(t.lanes());
                    } else {
                        shuffled_predicate = Shuffle::make(load_predicates, op->indices);
                        shuffled_predicate = mutate(shuffled_predicate, nullptr);
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
                    Expr check = mutate(b1->value - b2->value, nullptr);
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
                    Expr diff = mutate(new_vectors[i] - new_vectors[i-1], nullptr);
                    const Broadcast *b = diff.as<Broadcast>();
                    if (b) {
                        Expr check = mutate(b->value * terms - r->stride, nullptr);
                        can_collapse &= is_zero(check);
                    } else {
                        can_collapse = false;
                    }
                }
                if (can_collapse) {
                    return mutate(Ramp::make(r->base, r->stride / terms, r->lanes * terms), bounds);
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
                        diff = mutate(new_vectors[i] - new_vectors[i-1], nullptr);
                    }

                    const Broadcast *b = diff.as<Broadcast>();
                    if (b) {
                        Expr check = mutate(b->value - r->stride * new_vectors[i-1].type().lanes(), nullptr);
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
                Expr stride = mutate(new_vectors[1] - new_vectors[0], nullptr);
                for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
                    if (!new_vectors[i].type().is_scalar()) {
                        can_collapse = false;
                        break;
                    }

                    Expr check = mutate(new_vectors[i] - new_vectors[i - 1] - stride, nullptr);
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

    Stmt mutate_let_body(Stmt s, ConstBounds *) {return mutate(s);}
    Expr mutate_let_body(Expr e, ConstBounds *bounds) {return mutate(e, bounds);}

    template<typename T, typename Body>
    Body simplify_let(const T *op, ConstBounds *bounds) {
        internal_assert(!var_info.contains(op->name))
            << "Simplify only works on code where every name is unique. Repeated name: " << op->name << "\n";

        // If the value is trivial, make a note of it in the scope so
        // we can subs it in later
        ConstBounds value_bounds;
        Expr value = mutate(op->value, &value_bounds);
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
                var_a = add->a.as<Variable>();
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
            } else if (add && var_a) {
                replacement = substitute(new_name, Add::make(add->a, new_var), replacement);
                new_value = add->b;
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
            ConstBounds new_value_bounds;
            new_value = mutate(new_value, &new_value_bounds);
            if (new_value_bounds.min_defined || new_value_bounds.max_defined) {
                bounds_info.push(new_name, new_value_bounds);
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
            if (value_bounds.min_defined || value_bounds.max_defined) {
                bounds_info.push(op->name, value_bounds);
                value_bounds_tracked = true;
            }
        }

        body = mutate_let_body(body, bounds);

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

        if (info.old_uses > 0 || !remove_dead_lets) {
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

    Expr visit(const Let *op, ConstBounds *bounds) {
        return simplify_let<Let, Expr>(op, bounds);
    }

    Stmt visit(const LetStmt *op) {
        return simplify_let<LetStmt, Stmt>(op, nullptr);
    }

    Stmt visit(const AssertStmt *op) {
        Expr cond = mutate(op->condition, nullptr);
        Expr message = mutate(op->message, nullptr);

        if (is_zero(cond)) {
            // Usually, assert(const-false) should generate a warning;
            // in at least one case (specialize_fail()), we want to suppress
            // the warning, because the assertion is generated internally
            // by Halide and is expected to always fail.
            const Call *call = message.as<Call>();
            const bool const_false_conditions_expected =
                call && call->name == "halide_error_specialize_fail";
            if (!const_false_conditions_expected) {
                user_warning << "This pipeline is guaranteed to fail an assertion at runtime with error: \n"
                             << message << "\n";
            }
        } else if (is_one(cond)) {
            return Evaluate::make(0);
        }

        if (cond.same_as(op->condition) && message.same_as(op->message)) {
            return op;
        } else {
            return AssertStmt::make(cond, message);
        }
    }

    Stmt visit(const For *op) {
        ConstBounds min_bounds, extent_bounds;
        Expr new_min = mutate(op->min, &min_bounds);
        Expr new_extent = mutate(op->extent, &extent_bounds);

        bool bounds_tracked = false;
        if (min_bounds.min_defined || (min_bounds.max_defined && extent_bounds.max_defined)) {
            min_bounds.max += extent_bounds.max - 1;
            min_bounds.max_defined &= extent_bounds.max_defined;
            bounds_tracked = true;
            bounds_info.push(op->name, min_bounds);
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

    Stmt visit(const Provide *op) {
        found_buffer_reference(op->name, op->args.size());

        vector<Expr> new_args(op->args.size());
        vector<Expr> new_values(op->values.size());
        bool changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            const Expr &old_arg = op->args[i];
            Expr new_arg = mutate(old_arg, nullptr);
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
        }

        for (size_t i = 0; i < op->values.size(); i++) {
            const Expr &old_value = op->values[i];
            Expr new_value = mutate(old_value, nullptr);
            if (!new_value.same_as(old_value)) changed = true;
            new_values[i] = new_value;
        }

        if (!changed) {
            return op;
        } else {
            return Provide::make(op->name, new_values, new_args);
        }
    }

    Stmt visit(const Store *op) {
        found_buffer_reference(op->name);

        Expr predicate = mutate(op->predicate, nullptr);
        Expr value = mutate(op->value, nullptr);
        Expr index = mutate(op->index, nullptr);

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

    Stmt visit(const Allocate *op) {
        std::vector<Expr> new_extents;
        bool all_extents_unmodified = true;
        for (size_t i = 0; i < op->extents.size(); i++) {
            new_extents.push_back(mutate(op->extents[i], nullptr));
            all_extents_unmodified &= new_extents[i].same_as(op->extents[i]);
        }
        Stmt body = mutate(op->body);
        Expr condition = mutate(op->condition, nullptr);
        Expr new_expr;
        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr, nullptr);
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

    Stmt visit(const Evaluate *op) {
        Expr value = mutate(op->value, nullptr);

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

    Stmt visit(const ProducerConsumer *op) {
        Stmt body = mutate(op->body);

        if (is_no_op(body)) {
            return Evaluate::make(0);
        } else if (body.same_as(op->body)) {
            return op;
        } else {
            return ProducerConsumer::make(op->name, op->is_producer, body);
        }
    }

    Stmt visit(const Block *op) {
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
                   is_one(mutate((if_first->condition && if_rest->condition) == if_rest->condition, nullptr))) {
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

    Stmt visit(const Realize *op) {
        Region new_bounds;
        bool bounds_changed;

        // Mutate the bounds
        std::tie(new_bounds, bounds_changed) = mutate_region(this, op->bounds, nullptr);

        Stmt body = mutate(op->body);
        Expr condition = mutate(op->condition, nullptr);
        if (!bounds_changed &&
            body.same_as(op->body) &&
            condition.same_as(op->condition)) {
            return op;
        }
        return Realize::make(op->name, op->types, op->memory_type, new_bounds,
                             std::move(condition), std::move(body));
    }

    Stmt visit(const Prefetch *op) {
        Region new_bounds;
        bool bounds_changed;

        // Mutate the bounds
        std::tie(new_bounds, bounds_changed) = mutate_region(this, op->bounds, nullptr);

        if (!bounds_changed) {
            return op;
        }
        return Prefetch::make(op->name, op->types, new_bounds, op->param);
    }

    Stmt visit(const Free *op) {
        return op;
    }

};

Expr simplify(Expr e, bool remove_dead_lets,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(remove_dead_lets, &bounds, &alignment).mutate(e, nullptr);
}

Stmt simplify(Stmt s, bool remove_dead_lets,
              const Scope<Interval> &bounds,
              const Scope<ModulusRemainder> &alignment) {
    return Simplify(remove_dead_lets, &bounds, &alignment).mutate(s);
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

}
}
