#ifndef HALIDE_IR_MATCH_H
#define HALIDE_IR_MATCH_H

/** \file
 * Defines a method to match a fragment of IR against a pattern containing wildcards
 */

#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"
#include "ModulusRemainder.h"

#include <random>
#include <set>

namespace Halide {
namespace Internal {

/** Does the first expression have the same structure as the second?
 * Variables in the first expression with the name * are interpreted
 * as wildcards, and their matching equivalent in the second
 * expression is placed in the vector give as the third argument.
 * Wildcards require the types to match. For the type bits and width,
 * a 0 indicates "match anything". So an Int(8, 0) will match 8-bit
 * integer vectors of any width (including scalars), and a UInt(0, 0)
 * will match any unsigned integer type.
 *
 * For example:
 \code
 Expr x = Variable::make(Int(32), "*");
 match(x + x, 3 + (2*k), result)
 \endcode
 * should return true, and set result[0] to 3 and
 * result[1] to 2*k.
 */
bool expr_match(Expr pattern, Expr expr, std::vector<Expr> &result);

/** Does the first expression have the same structure as the second?
 * Variables are matched consistently. The first time a variable is
 * matched, it assumes the value of the matching part of the second
 * expression. Subsequent matches must be equal to the first match.
 *
 * For example:
 \code
 Var x("x"), y("y");
 match(x*(x + y), a*(a + b), result)
 \endcode
 * should return true, and set result["x"] = a, and result["y"] = b.
 */
bool expr_match(Expr pattern, Expr expr, std::map<std::string, Expr> &result);

void expr_match_test();

/** An alternative template-metaprogramming approach to expression
 * matching. Potentially more efficient. We lift the expression
 * pattern into a type, and then use force-inlined functions to
 * generate efficient matching and reconstruction code for any
 * pattern. Pattern elements are either one of the classes in the
 * namespace IRMatcher, or are non-null Exprs (represented as
 * BaseExprNode &).
 *
 * Pattern elements that are fully specified by their pattern can be
 * built into an expression using the ::make method. Some patterns,
 * such as a broadcast that matches any number of lanes, don't have
 * enough information to recreate an Expr.
 */
namespace IRMatcher {

constexpr int max_wild = 6;

/** To save stack space, the matcher objects are largely stateless and
 * immutable. This state object is built up during matching and then
 * consumed when constructing a replacement Expr.
 */
struct MatcherState {
    const BaseExprNode *bindings[max_wild];
    halide_scalar_value_t bound_const[max_wild];

    // values of the lanes field with special meaning.
    static constexpr uint16_t signed_integer_overflow = 0x8000;
    static constexpr uint16_t indeterminate_expression = 0x4000;
    static constexpr uint16_t special_values_mask = 0xc000;

    halide_type_t bound_const_type[max_wild];

    HALIDE_ALWAYS_INLINE
    void set_binding(int i, const BaseExprNode &n) noexcept {
        bindings[i] = &n;
    }

    HALIDE_ALWAYS_INLINE
    const BaseExprNode *get_binding(int i) const noexcept {
        return bindings[i];
    }

    HALIDE_ALWAYS_INLINE
    void set_bound_const(int i, int64_t s, halide_type_t t) noexcept {
        bound_const[i].u.i64 = s;
        bound_const_type[i] = t;
    }

    HALIDE_ALWAYS_INLINE
    void set_bound_const(int i, uint64_t u, halide_type_t t) noexcept {
        bound_const[i].u.u64 = u;
        bound_const_type[i] = t;
    }

    HALIDE_ALWAYS_INLINE
    void set_bound_const(int i, double f, halide_type_t t) noexcept {
        bound_const[i].u.f64 = f;
        bound_const_type[i] = t;
    }

    HALIDE_ALWAYS_INLINE
    void set_bound_const(int i, halide_scalar_value_t val, halide_type_t t) noexcept {
        bound_const[i] = val;
        bound_const_type[i] = t;
    }

    HALIDE_ALWAYS_INLINE
    void get_bound_const(int i, halide_scalar_value_t &val, halide_type_t &type) const noexcept {
        val = bound_const[i];
        type = bound_const_type[i];
    }

    HALIDE_ALWAYS_INLINE
    MatcherState() noexcept {}
};

template<typename T,
         typename = typename std::remove_reference<T>::type::pattern_tag>
struct enable_if_pattern {
    struct type {};
};

template<typename T>
struct bindings {
    constexpr static uint32_t mask = std::remove_reference<T>::type::binds;
};

inline HALIDE_NEVER_INLINE
Expr make_const_special_expr(halide_type_t ty) {
    const uint16_t flags = ty.lanes & MatcherState::special_values_mask;
    ty.lanes &= ~MatcherState::special_values_mask;
    if (flags & MatcherState::indeterminate_expression) {
        return make_indeterminate_expression(ty);
    } else if (flags & MatcherState::signed_integer_overflow) {
        return make_signed_integer_overflow(ty);
    }
    // unreachable
    return Expr();
}

HALIDE_ALWAYS_INLINE
Expr make_const_expr(halide_scalar_value_t val, halide_type_t ty) {
    halide_type_t scalar_type = ty;
    if (scalar_type.lanes & MatcherState::special_values_mask) {
        return make_const_special_expr(scalar_type);
    }

    const int lanes = scalar_type.lanes;
    scalar_type.lanes = 1;

    Expr e;
    switch (scalar_type.code) {
    case halide_type_int:
        e = IntImm::make(scalar_type, val.u.i64);
        break;
    case halide_type_uint:
        e = UIntImm::make(scalar_type, val.u.u64);
        break;
    case halide_type_float:
        e = FloatImm::make(scalar_type, val.u.f64);
        break;
    default:
        // Unreachable
        return Expr();
    }
    if (lanes > 1) {
        e = Broadcast::make(e, lanes);
    }
    return e;
}

bool equal_helper(const BaseExprNode &a, const BaseExprNode &b) noexcept;

// A fast version of expression equality that assumes a well-typed non-null expression tree.
HALIDE_ALWAYS_INLINE
bool equal(const BaseExprNode &a, const BaseExprNode &b) noexcept {
    // Early out
    return (&a == &b) ||
        ((a.type == b.type) &&
         (a.node_type == b.node_type) &&
         equal_helper(a, b));
}

// A pattern that matches a specific expression
struct SpecificExpr {
    struct pattern_tag {};

    constexpr static uint32_t binds = 0;

    Expr expr;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        return equal(*expr.get(), e);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        return expr;
    }

    constexpr static bool foldable = false;
};

inline std::ostream &operator<<(std::ostream &s, SpecificExpr e) {
    s << e.expr;
    return s;
}

template<int i>
struct WildConstInt {
    struct pattern_tag {};

    constexpr static uint32_t binds = 1 << i;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e;
        if (op->node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        if (op->node_type != IRNodeType::IntImm) {
            return false;
        }
        int64_t value = ((const IntImm *)op)->value;
        if (bound & binds) {
            halide_scalar_value_t val;
            halide_type_t type;
            state.get_bound_const(i, val, type);
            return op->type == type && value == val.u.i64;
        }
        state.set_bound_const(i, value, e.type);
        return true;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return make_const_expr(val, type);
    }

    constexpr static bool foldable = true;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const {
        state.get_bound_const(i, val, ty);
    }
};

template<int i>
std::ostream &operator<<(std::ostream &s, const WildConstInt<i> &c) {
    s << "ci" << i;
    return s;
}

template<int i>
struct WildConstUInt {
    struct pattern_tag {};

    constexpr static uint32_t binds = 1 << i;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e;
        if (op->node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        if (op->node_type != IRNodeType::UIntImm) {
            return false;
        }
        uint64_t value = ((const UIntImm *)op)->value;
        if (bound & binds) {
            halide_scalar_value_t val;
            halide_type_t type;
            state.get_bound_const(i, val, type);
            return op->type == type && value == val.u.u64;
        }
        state.set_bound_const(i, value, e.type);
        return true;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return make_const_expr(val, type);
    }

    constexpr static bool foldable = true;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        state.get_bound_const(i, val, ty);
    }
};

template<int i>
std::ostream &operator<<(std::ostream &s, const WildConstUInt<i> &c) {
    s << "cu" << i;
    return s;
}

template<int i>
struct WildConstFloat {
    struct pattern_tag {};

    constexpr static uint32_t binds = 1 << i;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        halide_type_t ty = e.type;
        const BaseExprNode *op = &e;
        if (op->node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        if (op->node_type != IRNodeType::FloatImm) {
            return false;
        }
        double value = ((const FloatImm *)op)->value;
        if (bound & binds) {
            halide_scalar_value_t val;
            halide_type_t type;
            state.get_bound_const(i, val, type);
            return op->type == type && value == val.u.f64;
        }
        state.set_bound_const(i, value, ty);
        return true;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return make_const_expr(val, type);
    }

    constexpr static bool foldable = true;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        state.get_bound_const(i, val, ty);
    }
};

template<int i>
std::ostream &operator<<(std::ostream &s, const WildConstFloat<i> &c) {
    s << "cf" << i;
    return s;
}

// Matches and binds to any constant Expr. Does not support constant-folding.
template<int i>
struct WildConst {
    struct pattern_tag {};

    constexpr static uint32_t binds = 1 << i;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e;
        if (op->node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        switch (op->node_type) {
        case IRNodeType::IntImm:
            return WildConstInt<i>().template match<bound>(e, state);
        case IRNodeType::UIntImm:
            return WildConstUInt<i>().template match<bound>(e, state);
        case IRNodeType::FloatImm:
            return WildConstFloat<i>().template match<bound>(e, state);
        default:
            return false;
        }
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return make_const_expr(val, type);
    }

    constexpr static bool foldable = true;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        state.get_bound_const(i, val, ty);
    }
};

template<int i>
std::ostream &operator<<(std::ostream &s, const WildConst<i> &c) {
    s << "c" << i;
    return s;
}

// Matches and binds to any Expr
template<int i>
struct Wild {
    struct pattern_tag {};

    constexpr static uint32_t binds = 1 << (i + 16);

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (bound & binds) {
            return equal(*state.get_binding(i), e);
        }
        state.set_binding(i, e);
        return true;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        return state.get_binding(i);
    }

    constexpr static bool foldable = true;
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        auto e = state.get_binding(i);
        ty = e->type;
        switch(e->node_type) {
        case IRNodeType::UIntImm:
            val.u.u64 = ((const UIntImm *)e)->value;
            return;
        case IRNodeType::IntImm:
            val.u.i64 = ((const IntImm *)e)->value;
            return;
        case IRNodeType::FloatImm:
            val.u.f64 = ((const FloatImm *)e)->value;
            return;
        default:
            // The function is noexcept, so silent failure. You
            // shouldn't be calling this if you haven't already
            // checked it's going to be a constant (e.g. with
            // is_const, or because you manually bound a constant Expr
            // to the state).
            val.u.u64 = 0;
        }
    }
};

template<int i>
std::ostream &operator<<(std::ostream &s, const Wild<i> &op) {
    s << "_" << i;
    return s;
}

// Matches a specific constant or broadcast of that constant. The
// constant must be representable as an int64_t.
struct Const {
    struct pattern_tag {};
    int64_t v;

    constexpr static uint32_t binds = 0;

    HALIDE_ALWAYS_INLINE
    Const(int64_t v) : v(v) {}

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        const BaseExprNode *op = &e;
        if (e.node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        switch (op->node_type) {
        case IRNodeType::IntImm:
            return ((const IntImm *)op)->value == (int64_t)v;
        case IRNodeType::UIntImm:
            return ((const UIntImm *)op)->value == (uint64_t)v;
        case IRNodeType::FloatImm:
            return ((const FloatImm *)op)->value == (double)v;
        default:
            return false;
        }
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const Const &b, MatcherState &state) const noexcept {
        return v == b.v;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        return make_const(type_hint, v);
    }

    constexpr static bool foldable = true;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        // Assume type is already correct
        switch (ty.code) {
        case halide_type_int:
            val.u.i64 = v;
            break;
        case halide_type_uint:
            val.u.u64 = (uint64_t)v;
            break;
        case halide_type_float:
            val.u.f64 = (double)v;
            break;
        default:
            // Unreachable
            ;
        }
    }
};

// Convert a provided pattern, expr, or constant int into the internal
// representation we use in the matcher trees.
template<typename T,
         typename = typename std::remove_reference<T>::type::pattern_tag>
HALIDE_ALWAYS_INLINE
T pattern_arg(T t) {
    return t;
}
HALIDE_ALWAYS_INLINE
Const pattern_arg(int64_t x) {
    return {x};
}
HALIDE_ALWAYS_INLINE
const SpecificExpr pattern_arg(const Expr &e) {
    return {e};
}

inline std::ostream &operator<<(std::ostream &s, const Const &op) {
    s << op.v;
    return s;
}

template<typename Op>
int64_t constant_fold_bin_op(halide_type_t &, int64_t, int64_t) noexcept;

template<typename Op>
uint64_t constant_fold_bin_op(halide_type_t &, uint64_t, uint64_t) noexcept;

template<typename Op>
double constant_fold_bin_op(halide_type_t &, double, double) noexcept;

// Matches one of the binary operators
template<typename Op, typename A, typename B>
struct BinOp {
    struct pattern_tag {};
    A a;
    B b;

    constexpr static uint32_t binds = bindings<A>::mask | bindings<B>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != Op::_node_type) {
            return false;
        }
        const Op &op = (const Op &)e;
        return (a.template match<bound>(*op.a.get(), state) &&
                b.template match<bound | bindings<A>::mask>(*op.b.get(), state));
    }

    template<uint32_t bound, typename Op2, typename A2, typename B2>
    HALIDE_ALWAYS_INLINE
    bool match(const BinOp<Op2, A2, B2> &op, MatcherState &state) const noexcept {
        return (std::is_same<Op, Op2>::value &&
                a.template match<bound>(op.a, state) &&
                b.template match<bound | bindings<A>::mask>(op.b, state));
    }

    constexpr static bool foldable = A::foldable && B::foldable;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        halide_scalar_value_t val_a, val_b;
        if (std::is_same<A, Const>::value) {
            b.make_folded_const(val_b, ty, state);
            if ((std::is_same<Op, And>::value && val_b.u.u64 == 0) ||
                (std::is_same<Op, Or>::value && val_b.u.u64 == 1)) {
                // Short circuit
                val = val_b;
                return;
            }
            const uint16_t l = ty.lanes;
            a.make_folded_const(val_a, ty, state);
            ty.lanes |= l;  // Make sure the overflow bits are sticky
        } else {
            a.make_folded_const(val_a, ty, state);
            if ((std::is_same<Op, And>::value && val_a.u.u64 == 0) ||
                (std::is_same<Op, Or>::value && val_a.u.u64 == 1)) {
                // Short circuit
                val = val_a;
                return;
            }
            const uint16_t l = ty.lanes;
            b.make_folded_const(val_b, ty, state);
            ty.lanes |= l;
        }
        switch (ty.code) {
        case halide_type_int:
            val.u.i64 = constant_fold_bin_op<Op>(ty, val_a.u.i64, val_b.u.i64);
            break;
        case halide_type_uint:
            val.u.u64 = constant_fold_bin_op<Op>(ty, val_a.u.u64, val_b.u.u64);
            break;
        case halide_type_float:
            val.u.f64 = constant_fold_bin_op<Op>(ty, val_a.u.f64, val_b.u.f64);
            break;
        default:
            // unreachable
            ;
        }
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const noexcept {
        Expr ea, eb;
        if (std::is_same<A, Const>::value) {
            eb = b.make(state, type_hint);
            ea = a.make(state, eb.type());
        } else {
            ea = a.make(state, type_hint);
            eb = b.make(state, ea.type());
        }
        // We sometimes mix vectors and scalars in the rewrite rules,
        // so insert a broadcast if necessary.
        if (ea.type().is_vector() && !eb.type().is_vector()) {
            eb = Broadcast::make(eb, ea.type().lanes());
        }
        if (eb.type().is_vector() && !ea.type().is_vector()) {
            ea = Broadcast::make(ea, eb.type().lanes());
        }
        return Op::make(std::move(ea), std::move(eb));
    }
};

template<typename Op>
uint64_t constant_fold_cmp_op(int64_t, int64_t) noexcept;

template<typename Op>
uint64_t constant_fold_cmp_op(uint64_t, uint64_t) noexcept;

template<typename Op>
uint64_t constant_fold_cmp_op(double, double) noexcept;

// Matches one of the comparison operators
template<typename Op, typename A, typename B>
struct CmpOp {
    struct pattern_tag {};
    A a;
    B b;

    constexpr static uint32_t binds = bindings<A>::mask | bindings<B>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != Op::_node_type) {
            return false;
        }
        const Op &op = (const Op &)e;
        return (a.template match<bound>(*op.a.get(), state) &&
                b.template match<bound | bindings<A>::mask>(*op.b.get(), state));
    }

    template<uint32_t bound, typename Op2, typename A2, typename B2>
    HALIDE_ALWAYS_INLINE
    bool match(const CmpOp<Op2, A2, B2> &op, MatcherState &state) const noexcept {
        return (std::is_same<Op, Op2>::value &&
                a.template match<bound>(op.a, state) &&
                b.template match<bound | bindings<A>::mask>(op.b, state));
    }

    constexpr static bool foldable = A::foldable && B::foldable;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        halide_scalar_value_t val_a, val_b;
        // If one side is an untyped const, evaluate the other side first to get a type hint.
        if (std::is_same<A, Const>::value) {
            b.make_folded_const(val_b, ty, state);
            const uint16_t l = ty.lanes;
            a.make_folded_const(val_a, ty, state);
            ty.lanes |= l;
        } else {
            a.make_folded_const(val_a, ty, state);
            const uint16_t l = ty.lanes;
            b.make_folded_const(val_b, ty, state);
            ty.lanes |= l;
        }
        switch (ty.code) {
        case halide_type_int:
            val.u.u64 = constant_fold_cmp_op<Op>(val_a.u.i64, val_b.u.i64);
            break;
        case halide_type_uint:
            val.u.u64 = constant_fold_cmp_op<Op>(val_a.u.u64, val_b.u.u64);
            break;
        case halide_type_float:
            val.u.u64 = constant_fold_cmp_op<Op>(val_a.u.f64, val_b.u.f64);
            break;
        default:
            // unreachable
            ;
        }
        ty.code = halide_type_uint;
        ty.bits = 1;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        // If one side is an untyped const, evaluate the other side first to get a type hint.
        Expr ea, eb;
        if (std::is_same<A, Const>::value) {
            eb = b.make(state, {});
            ea = a.make(state, eb.type());
        } else {
            ea = a.make(state, {});
            eb = b.make(state, ea.type());
        }
        // We sometimes mix vectors and scalars in the rewrite rules,
        // so insert a broadcast if necessary.
        if (ea.type().is_vector() && !eb.type().is_vector()) {
            eb = Broadcast::make(eb, ea.type().lanes());
        }
        if (eb.type().is_vector() && !ea.type().is_vector()) {
            ea = Broadcast::make(ea, eb.type().lanes());
        }
        return Op::make(std::move(ea), std::move(eb));
    }
};

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<Add, A, B> &op) {
    s << "(" << op.a << " + " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<Sub, A, B> &op) {
    s << "(" << op.a << " - " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<Mul, A, B> &op) {
    s << "(" << op.a << " * " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<Div, A, B> &op) {
    s << "(" << op.a << " / " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<And, A, B> &op) {
    s << "(" << op.a << " && " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<Or, A, B> &op) {
    s << "(" << op.a << " || " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<Min, A, B> &op) {
    s << "min(" << op.a << ", " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<Max, A, B> &op) {
    s << "max(" << op.a << ", " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const CmpOp<LE, A, B> &op) {
    s << "(" << op.a << " <= " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const CmpOp<LT, A, B> &op) {
    s << "(" << op.a << " < " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const CmpOp<GE, A, B> &op) {
    s << "(" << op.a << " >= " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const CmpOp<GT, A, B> &op) {
    s << "(" << op.a << " > " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const CmpOp<EQ, A, B> &op) {
    s << "(" << op.a << " == " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const CmpOp<NE, A, B> &op) {
    s << "(" << op.a << " != " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<Mod, A, B> &op) {
    s << "(" << op.a << " % " << op.b << ")";
    return s;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator+(A a, B b) noexcept -> BinOp<Add, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto add(A a, B b) -> decltype(IRMatcher::operator+(a, b)) {return IRMatcher::operator+(a, b);}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Add>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    t.lanes |= ((t.bits >= 32) && add_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    int dead_bits = 64 - t.bits;
    // Drop the high bits then sign-extend them back
    return int64_t((uint64_t(a) + uint64_t(b)) << dead_bits) >> dead_bits;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Add>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    uint64_t ones = (uint64_t)(-1);
    return (a + b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Add>(halide_type_t &t, double a, double b) noexcept {
    return a + b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator-(A a, B b) noexcept -> BinOp<Sub, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto sub(A a, B b) -> decltype(IRMatcher::operator-(a, b)) {return IRMatcher::operator-(a, b);}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Sub>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    t.lanes |= ((t.bits >= 32) && sub_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    // Drop the high bits then sign-extend them back
    int dead_bits = 64 - t.bits;
    return int64_t((uint64_t(a) - uint64_t(b)) << dead_bits) >> dead_bits;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Sub>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    uint64_t ones = (uint64_t)(-1);
    return (a - b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Sub>(halide_type_t &t, double a, double b) noexcept {
    return a - b;
}


template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator*(A a, B b) noexcept -> BinOp<Mul, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto mul(A a, B b) -> decltype(IRMatcher::operator*(a, b)) {return IRMatcher::operator*(a, b);}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Mul>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    t.lanes |= ((t.bits >= 32) && mul_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    int dead_bits = 64 - t.bits;
    // Drop the high bits then sign-extend them back
    return int64_t((uint64_t(a) * uint64_t(b)) << dead_bits) >> dead_bits;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Mul>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    uint64_t ones = (uint64_t)(-1);
    return (a * b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Mul>(halide_type_t &t, double a, double b) noexcept {
    return a * b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator/(A a, B b) noexcept -> BinOp<Div, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto div(A a, B b) -> decltype(IRMatcher::operator/(a, b)) {return IRMatcher::operator/(a, b);}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Div>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    if (b == 0) {
        t.lanes |= MatcherState::indeterminate_expression;
        return 0;
    } else {
        return div_imp(a, b);
    }
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Div>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    if (b == 0) {
        t.lanes |= MatcherState::indeterminate_expression;
        return 0;
    } else {
        return a / b;
    }
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Div>(halide_type_t &t, double a, double b) noexcept {
    return a / b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator%(A a, B b) noexcept -> BinOp<Mod, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto mod(A a, B b) -> decltype(IRMatcher::operator%(a, b)) {return IRMatcher::operator%(a, b);}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Mod>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    if (b == 0) {
        t.lanes |= MatcherState::indeterminate_expression;
        return 0;
    } else {
        return mod_imp(a, b);
    }
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Mod>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    if (b == 0) {
        t.lanes |= MatcherState::indeterminate_expression;
        return 0;
    } else {
        return a % b;
    }
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Mod>(halide_type_t &t, double a, double b) noexcept {
    return mod_imp(a, b);
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto min(A a, B b) noexcept -> BinOp<Min, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Min>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return std::min(a, b);
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Min>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return std::min(a, b);
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Min>(halide_type_t &t, double a, double b) noexcept {
    return std::min(a, b);
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto max(A a, B b) noexcept -> BinOp<Max, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Max>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return std::max(a, b);
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Max>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return std::max(a, b);
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Max>(halide_type_t &t, double a, double b) noexcept {
    return std::max(a, b);
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator<(A a, B b) noexcept -> CmpOp<LT, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto lt(A a, B b) -> decltype(IRMatcher::operator<(a, b)) {return IRMatcher::operator<(a, b);}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LT>(int64_t a, int64_t b) noexcept {
    return a < b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LT>(uint64_t a, uint64_t b) noexcept {
    return a < b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LT>(double a, double b) noexcept {
    return a < b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator>(A a, B b) noexcept -> CmpOp<GT, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto gt(A a, B b) -> decltype(IRMatcher::operator>(a, b)) {return IRMatcher::operator>(a, b);}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GT>(int64_t a, int64_t b) noexcept {
    return a > b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GT>(uint64_t a, uint64_t b) noexcept {
    return a > b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GT>(double a, double b) noexcept {
    return a > b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator<=(A a, B b) noexcept -> CmpOp<LE, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto le(A a, B b) -> decltype(IRMatcher::operator<=(a, b)) {return IRMatcher::operator<=(a, b);}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LE>(int64_t a, int64_t b) noexcept {
    return a <= b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LE>(uint64_t a, uint64_t b) noexcept {
    return a <= b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LE>(double a, double b) noexcept {
    return a <= b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator>=(A a, B b) noexcept -> CmpOp<GE, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto ge(A a, B b) -> decltype(IRMatcher::operator>=(a, b)) {return IRMatcher::operator>=(a, b);}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GE>(int64_t a, int64_t b) noexcept {
    return a >= b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GE>(uint64_t a, uint64_t b) noexcept {
    return a >= b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GE>(double a, double b) noexcept {
    return a >= b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator==(A a, B b) noexcept -> CmpOp<EQ, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto eq(A a, B b) -> decltype(IRMatcher::operator==(a, b)) {return IRMatcher::operator==(a, b);}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<EQ>(int64_t a, int64_t b) noexcept {
    return a == b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<EQ>(uint64_t a, uint64_t b) noexcept {
    return a == b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<EQ>(double a, double b) noexcept {
    return a == b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator!=(A a, B b) noexcept -> CmpOp<NE, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto ne(A a, B b) -> decltype(IRMatcher::operator!=(a, b)) {return IRMatcher::operator!=(a, b);}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<NE>(int64_t a, int64_t b) noexcept {
    return a != b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<NE>(uint64_t a, uint64_t b) noexcept {
    return a != b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<NE>(double a, double b) noexcept {
    return a != b;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator||(A a, B b) noexcept -> BinOp<Or, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto or_op(A a, B b) -> decltype(IRMatcher::operator||(a, b)) {return IRMatcher::operator||(a, b);}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Or>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return (a | b) & 1;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Or>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return (a | b) & 1;
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Or>(halide_type_t &t, double a, double b) noexcept {
    t.lanes |= MatcherState::indeterminate_expression;
    return 0;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto operator&&(A a, B b) noexcept -> BinOp<And, decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto and_op(A a, B b) -> decltype(IRMatcher::operator&&(a, b)) {return IRMatcher::operator&&(a, b);}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<And>(halide_type_t &t, int64_t a, int64_t b) noexcept {
    return a & b & 1;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<And>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return a & b & 1;
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<And>(halide_type_t &t, double a, double b) noexcept {
    t.lanes |= MatcherState::indeterminate_expression;
    return 0;
}

constexpr inline uint32_t bitwise_or_reduce() {
    return 0;
}

template<typename... Args>
constexpr uint32_t bitwise_or_reduce(uint32_t first, Args... rest) {
    return first | bitwise_or_reduce(rest...);
}

template<typename... Args>
struct Intrin {
    struct pattern_tag {};
    Call::ConstString intrin;
    std::tuple<Args...> args;

    static constexpr uint32_t binds = bitwise_or_reduce((bindings<Args>::mask)...);

    template<int i,
             uint32_t bound,
             typename = typename std::enable_if<(i < sizeof...(Args))>::type>
    HALIDE_ALWAYS_INLINE
    bool match_args(int, const Call &c, MatcherState &state) const noexcept {
        using T = decltype(std::get<i>(args));
        return (std::get<i>(args).template match<bound>(*c.args[i].get(), state) &&
                match_args<i + 1, bound | bindings<T>::mask>(0, c, state));
    }

    template<int i, uint32_t binds>
    HALIDE_ALWAYS_INLINE
    bool match_args(double, const Call &c, MatcherState &state) const noexcept {
        return true;
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != IRNodeType::Call) {
            return false;
        }
        const Call &c = (const Call &)e;
        return (c.is_intrinsic(intrin) && match_args<0, bound>(0, c, state));
    }

    template<int i,
             typename = typename std::enable_if<(i < sizeof...(Args))>::type>
    HALIDE_ALWAYS_INLINE
    void print_args(int, std::ostream &s) const {
        s << std::get<i>(args);
        if (i + 1 < sizeof...(Args)) {
            s << ", ";
        }
        print_args<i+1>(0, s);
    }

    template<int i>
    HALIDE_ALWAYS_INLINE
    void print_args(double, std::ostream &s) const {
    }

    HALIDE_ALWAYS_INLINE
    void print_args(std::ostream &s) const {
        print_args<0>(0, s);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        if (intrin == Call::likely) {
            return likely(std::get<0>(args).make(state, type_hint));
        } else if (intrin == Call::likely_if_innermost) {
            return likely_if_innermost(std::get<0>(args).make(state, type_hint));
        }
        internal_error << "Unhandled intrinsic in IRMatcher: " << intrin;
        return Expr();
    }

    constexpr static bool foldable = false;

    HALIDE_ALWAYS_INLINE
    Intrin(Call::ConstString intrin, Args... args) noexcept : intrin(intrin), args(args...) {}
};

template<typename... Args>
std::ostream &operator<<(std::ostream &s, const Intrin<Args...> &op) {
    s << op.intrin << "(";
    op.print_args(s);
    s << ")";
    return s;
}

template<typename... Args>
HALIDE_ALWAYS_INLINE
auto intrin(Call::ConstString name, Args... args) noexcept -> Intrin<decltype(pattern_arg(args))...> {
    return {name, pattern_arg(args)...};
}

template<typename A>
struct NotOp {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != IRNodeType::Not) {
            return false;
        }
        const Not &op = (const Not &)e;
        return (a.template match<bound>(*op.a.get(), state));
    }

    template<uint32_t bound, typename A2>
    HALIDE_ALWAYS_INLINE
    bool match(const NotOp<A2> &op, MatcherState &state) const noexcept {
        return a.template match<bound>(op.a, state);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        return Not::make(a.make(state, type_hint));
    }

    constexpr static bool foldable = A::foldable;

    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        a.make_folded_const(val, ty, state);
        val.u.u64 = ~val.u.u64;
        val.u.u64 &= 1;
        ty.lanes |= ((int)ty.code == (int)halide_type_float) ? MatcherState::indeterminate_expression : 0;
    }
};

template<typename A>
HALIDE_ALWAYS_INLINE
auto operator!(A a) noexcept -> NotOp<decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<typename A>
HALIDE_ALWAYS_INLINE
auto not_op(A a) -> decltype(IRMatcher::operator!(a)) {return IRMatcher::operator!(a);}

template<typename A>
inline std::ostream &operator<<(std::ostream &s, const NotOp<A> &op) {
    s << "!(" << op.a << ")";
    return s;
}

template<typename C, typename T, typename F>
struct SelectOp {
    struct pattern_tag {};
    C c;
    T t;
    F f;

    constexpr static uint32_t binds = bindings<C>::mask | bindings<T>::mask | bindings<F>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != Select::_node_type) {
            return false;
        }
        const Select &op = (const Select &)e;
        return (c.template match<bound>(*op.condition.get(), state) &&
                t.template match<bound | bindings<C>::mask>(*op.true_value.get(), state) &&
                f.template match<bound | bindings<C>::mask | bindings<T>::mask>(*op.false_value.get(), state));
    }
    template<uint32_t bound, typename C2, typename T2, typename F2>
    HALIDE_ALWAYS_INLINE
    bool match(const SelectOp<C2, T2, F2> &instance, MatcherState &state) const noexcept {
        return (c.template match<bound>(instance.c, state) &&
                t.template match<bound | bindings<C>::mask>(instance.t, state) &&
                f.template match<bound | bindings<C>::mask | bindings<T>::mask>(instance.f, state));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        return Select::make(c.make(state, {}), t.make(state, type_hint), f.make(state, type_hint));
    }

    constexpr static bool foldable = C::foldable && T::foldable && F::foldable;

    template<typename C1 = C>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        halide_scalar_value_t c_val, t_val, f_val;
        halide_type_t c_ty;
        c.make_folded_const(c_val, c_ty, state);
        if ((c_val.u.u64 & 1) == 1) {
            t.make_folded_const(val, ty, state);
        } else {
            f.make_folded_const(val, ty, state);
        }
        ty.lanes |= c_ty.lanes & MatcherState::special_values_mask;
    }
};

template<typename C, typename T, typename F>
std::ostream &operator<<(std::ostream &s, const SelectOp<C, T, F> &op) {
    s << "select(" << op.c << ", " << op.t << ", " << op.f << ")";
    return s;
}

template<typename C, typename T, typename F>
HALIDE_ALWAYS_INLINE
auto select(C c, T t, F f) noexcept -> SelectOp<decltype(pattern_arg(c)), decltype(pattern_arg(t)), decltype(pattern_arg(f))> {
    return {pattern_arg(c), pattern_arg(t), pattern_arg(f)};
}

template<typename A, bool known_lanes>
struct BroadcastOp {
    struct pattern_tag {};
    A a;
    int lanes;

    constexpr static uint32_t binds = bindings<A>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type == Broadcast::_node_type) {
            const Broadcast &op = (const Broadcast &)e;
            if ((!known_lanes || lanes == op.lanes) &&
                a.template match<bound>(*op.value.get(), state)) {
                return true;
            }
        }
        return false;
    }

    template<uint32_t bound, typename A2, bool known_lanes_2>
    HALIDE_ALWAYS_INLINE
    bool match(const BroadcastOp<A2, known_lanes_2> &op, MatcherState &state) const noexcept {
        return (a.template match<bound>(op.a, state) &&
                (lanes == op.lanes || !known_lanes || !known_lanes_2));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        const int l = known_lanes ? lanes : type_hint.lanes;
        type_hint.lanes = 1;
        return Broadcast::make(a.make(state, type_hint), l);
    }

    constexpr static bool foldable = false;

    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        uint16_t l = known_lanes ? lanes : ty.lanes;
        a.make_folded_const(val, ty, state);
        ty.lanes = l | (ty.lanes & MatcherState::special_values_mask);
    }
};

template<typename A>
inline std::ostream &operator<<(std::ostream &s, const BroadcastOp<A, true> &op) {
    s << "broadcast(" << op.a << ", " << op.lanes << ")";
    return s;
}

template<typename A>
inline std::ostream &operator<<(std::ostream &s, const BroadcastOp<A, false> &op) {
    s << "broadcast(" << op.a << ")";
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
auto broadcast(A a, int lanes) noexcept -> BroadcastOp<decltype(pattern_arg(a)), true> {
    return {pattern_arg(a), lanes};
}

template<typename A>
HALIDE_ALWAYS_INLINE
auto broadcast(A a) noexcept -> BroadcastOp<decltype(pattern_arg(a)), false> {
    return {pattern_arg(a), 0};
}

template<typename A, typename B, bool known_lanes>
struct RampOp {
    struct pattern_tag {};
    A a;
    B b;
    int lanes;

    constexpr static uint32_t binds = bindings<A>::mask | bindings<B>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != Ramp::_node_type) {
            return false;
        }
        const Ramp &op = (const Ramp &)e;
        if ((lanes == op.type.lanes() || !known_lanes) &&
            a.template match<bound>(*op.base.get(), state) &&
            b.template match<bound | bindings<A>::mask>(*op.stride.get(), state)) {
            return true;
        } else {
            return false;
        }
    }

    template<uint32_t bound, typename A2, typename B2, bool known_lanes_2>
    HALIDE_ALWAYS_INLINE
    bool match(const RampOp<A2, B2, known_lanes_2> &op, MatcherState &state) const noexcept {
        return ((lanes == op.lanes || !known_lanes || !known_lanes_2) &&
                a.template match<bound>(op.a, state) &&
                b.template match<bound | bindings<A>::mask>(op.b, state));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        const int l = known_lanes ? lanes : type_hint.lanes;
        type_hint.lanes = 1;
        Expr ea, eb;
        if (std::is_same<A, Const>::value) {
            eb = b.make(state, type_hint);
            ea = a.make(state, eb.type());
        } else {
            ea = a.make(state, type_hint);
            eb = b.make(state, ea.type());
        }
        return Ramp::make(ea, eb, l);
    }

    constexpr static bool foldable = false;
};

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const RampOp<A, B, true> &op) {
    s << "ramp(" << op.a << ", " << op.b << ", " << op.lanes << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const RampOp<A, B, false> &op) {
    s << "ramp(" << op.a << ", " << op.b << ")";
    return s;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto ramp(A a, B b, int lanes) noexcept -> RampOp<decltype(pattern_arg(a)), decltype(pattern_arg(b)), true> {
    return {pattern_arg(a), pattern_arg(b), lanes};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto ramp(A a, B b) noexcept -> RampOp<decltype(pattern_arg(a)), decltype(pattern_arg(b)), false> {
    return {pattern_arg(a), pattern_arg(b), 0};
}

template<typename A>
struct NegateOp {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != Sub::_node_type) {
            return false;
        }
        const Sub &op = (const Sub &)e;
        return (a.template match<bound>(*op.b.get(), state) &&
                is_zero(op.a));
    }

    template<uint32_t bound, typename A2>
    HALIDE_ALWAYS_INLINE
    bool match(NegateOp<A2> &&p, MatcherState &state) const noexcept {
        return a.template match<bound>(p.a, state);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        Expr ea = a.make(state, type_hint);
        Expr z = make_zero(ea.type());
        return Sub::make(std::move(z), std::move(ea));
    }

    constexpr static bool foldable = A::foldable;

    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        a.make_folded_const(val, ty, state);
        int dead_bits = 64 - ty.bits;
        switch (ty.code) {
        case halide_type_int:
            if (ty.bits >= 32 && val.u.u64 && (val.u.u64 << (65 - ty.bits)) == 0) {
                // Trying to negate the most negative signed int for a no-overflow type.
                ty.lanes |= MatcherState::signed_integer_overflow;
            } else {
                // Negate, drop the high bits, and then sign-extend them back
                val.u.i64 = int64_t(uint64_t(-val.u.i64) << dead_bits) >> dead_bits;
            }
            break;
        case halide_type_uint:
            val.u.u64 = ((-val.u.u64) << dead_bits) >> dead_bits;
            break;
        case halide_type_float:
            val.u.f64 = -val.u.f64;
            break;
        default:
            // unreachable
            ;
        }
    }
};

template<typename A>
std::ostream &operator<<(std::ostream &s, const NegateOp<A> &op) {
    s << "-" << op.a;
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
auto operator-(A a) noexcept -> NegateOp<decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<typename A>
HALIDE_ALWAYS_INLINE
auto negate(A a) -> decltype(IRMatcher::operator-(a)) {return IRMatcher::operator-(a);}

template<typename A>
struct CastOp {
    struct pattern_tag {};
    Type t;
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != Cast::_node_type) {
            return false;
        }
        const Cast &op = (const Cast &)e;
        return (e.type == t &&
                a.template match<bound>(*op.value.get(), state));
    }
    template<uint32_t bound, typename A2>
    HALIDE_ALWAYS_INLINE
    bool match(const CastOp<A2> &op, MatcherState &state) const noexcept {
        return t == op.t && a.template match<bound>(op.a, state);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        return cast(t, a.make(state, {}));
    }

    constexpr static bool foldable = false; // TODO
};

template<typename A>
std::ostream &operator<<(std::ostream &s, const CastOp<A> &op) {
    s << "cast(" << op.t << ", " << op.a << ")";
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
auto cast(halide_type_t t, A a) noexcept -> CastOp<decltype(pattern_arg(a))> {
    return {t, pattern_arg(a)};
}

template<typename A>
struct Fold {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const noexcept {
        halide_scalar_value_t c;
        halide_type_t ty = type_hint;
        a.make_folded_const(c, ty, state);
        return make_const_expr(c, ty);
    }

    constexpr static bool foldable = A::foldable;

    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        a.make_folded_const(val, ty, state);
    }
};

template<typename A>
HALIDE_ALWAYS_INLINE
auto fold(A a) noexcept -> Fold<decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<typename A>
std::ostream &operator<<(std::ostream &s, const Fold<A> &op) {
    s << "fold(" << op.a << ")";
    return s;
}

template<typename A>
struct Overflows {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    constexpr static bool foldable = A::foldable;

    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        a.make_folded_const(val, ty, state);
        ty.code = halide_type_uint;
        ty.bits = 64;
        val.u.u64 = (ty.lanes & MatcherState::special_values_mask) != 0;
        ty.lanes = 1;
    }
};

template<typename A>
HALIDE_ALWAYS_INLINE
auto overflows(A a) noexcept -> Overflows<decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<typename A>
std::ostream &operator<<(std::ostream &s, const Overflows<A> &op) {
    s << "overflows(" << op.a << ")";
    return s;
}

struct Indeterminate {
    struct pattern_tag {};

    constexpr static uint32_t binds = 0;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != Call::_node_type) {
            return false;
        }
        const Call &op = (const Call &)e;
        return (op.is_intrinsic(Call::indeterminate_expression));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        type_hint.lanes |= MatcherState::indeterminate_expression;
        return make_const_special_expr(type_hint);
    }

    constexpr static bool foldable = true;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        val.u.u64 = 0;
        ty.lanes |= MatcherState::indeterminate_expression;
    }
};

inline std::ostream &operator<<(std::ostream &s, const Indeterminate &op) {
    s << "indeterminate()";
    return s;
}

struct Overflow {
    struct pattern_tag {};

    constexpr static uint32_t binds = 0;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const SpecificExpr &e, MatcherState &state) const noexcept {
        return match<bound>(*e.expr.get(), state);
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(const BaseExprNode &e, MatcherState &state) const noexcept {
        if (e.node_type != Call::_node_type) {
            return false;
        }
        const Call &op = (const Call &)e;
        return (op.is_intrinsic(Call::signed_integer_overflow));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState &state, halide_type_t type_hint) const {
        type_hint.lanes |= MatcherState::signed_integer_overflow;
        return make_const_special_expr(type_hint);
    }

    constexpr static bool foldable = true;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        val.u.u64 = 0;
        ty.lanes |= MatcherState::signed_integer_overflow;
    }
};

inline std::ostream &operator<<(std::ostream &s, const Overflow &op) {
    s << "overflow()";
    return s;
}

template<typename A>
struct IsConst {
    struct pattern_tag {};

    constexpr static uint32_t binds = bindings<A>::mask;

    A a;

    constexpr static bool foldable = true;

    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const noexcept {
        Expr e = a.make(state, {});
        ty.code = halide_type_uint;
        ty.bits = 64;
        ty.lanes = 1;
        val.u.u64 = is_const(e) ? 1 : 0;
    }
};

template<typename A>
HALIDE_ALWAYS_INLINE
auto is_const(A a) noexcept -> IsConst<decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<typename A>
std::ostream &operator<<(std::ostream &s, const IsConst<A> &op) {
    s << "is_const(" << op.a << ")";
    return s;
}

template<typename A, typename Prover>
struct CanProve {
    struct pattern_tag {};
    A a;
    Prover *prover;  // An existing simplifying mutator

    constexpr static uint32_t binds = bindings<A>::mask;

    constexpr static bool foldable = true;

    HALIDE_NEVER_INLINE // Includes a raw call to an inlined make method, so don't inline.
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const {
        Expr condition = a.make(state, {});
        condition = prover->mutate(condition, nullptr);
        val.u.u64 = is_one(condition);
        ty.code = halide_type_uint;
        ty.bits = 1;
        ty.lanes = condition.type().lanes();
    };
};

template<typename A, typename Prover>
HALIDE_ALWAYS_INLINE
auto can_prove(A a, Prover *p) noexcept -> CanProve<decltype(pattern_arg(a)), Prover> {
    return {pattern_arg(a), p};
}

template<typename A, typename Prover>
std::ostream &operator<<(std::ostream &s, const CanProve<A, Prover> &op) {
    s << "can_prove(" << op.a << ")";
    return s;
}

template<typename A>
struct IsFloat {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    constexpr static bool foldable = true;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState &state) const {
        // a is almost certainly a very simple pattern (e.g. a wild), so just inline the make method.
        Type t = a.make(state, {}).type();
        val.u.u64 = t.is_float();
        ty.code = halide_type_uint;
        ty.bits = 1;
        ty.lanes = t.lanes();
    };
};

template<typename A>
HALIDE_ALWAYS_INLINE
auto is_float(A a) noexcept -> IsFloat<decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<typename A>
std::ostream &operator<<(std::ostream &s, const IsFloat<A> &op) {
    s << "is_float(" << op.a << ")";
    return s;
}

// Verify properties of each rewrite rule. Currently just fuzz tests them.
template<typename Before,
         typename After,
         typename Predicate,
         typename = typename std::enable_if<std::remove_reference<Before>::type::foldable &&
                                            std::remove_reference<After>::type::foldable>::type>
HALIDE_NEVER_INLINE
void fuzz_test_rule(Before &&before, After &&after, Predicate &&pred,
                   halide_type_t wildcard_type, halide_type_t output_type) noexcept {

    // We only validate the rules in the scalar case
    wildcard_type.lanes = output_type.lanes = 1;

    // Track which types this rule has been tested for before
    static std::set<uint32_t> tested;

    if (!tested.insert(reinterpret_bits<uint32_t>(wildcard_type)).second) return;

    // Print it in a form where it can be piped into a python/z3 validator
    debug(0) << "validate('" << before << "', '" << after << "', '" << pred << "', " << Type(wildcard_type) << ", " << Type(output_type) << ")\n";

    // Substitute some random constants into the before and after
    // expressions and see if the rule holds true. This should catch
    // silly errors, but not necessarily corner cases.
    static std::mt19937_64 rng(0);
    MatcherState state;

    Expr exprs[max_wild];

    for (int trials = 0; trials < 100; trials++) {
        // We want to test small constants more frequently than
        // large ones, otherwise we'll just get coverage of
        // overflow rules.
        int shift = (int)(rng() & (wildcard_type.bits - 1));

        for (int i = 0; i < max_wild; i++) {
            // Bind all the exprs and constants
            switch (wildcard_type.code) {
            case halide_type_uint:
                {
                    // Normalize to the type's range by adding zero
                    uint64_t val = constant_fold_bin_op<Add>(wildcard_type, (uint64_t)rng() >> shift, 0);
                    state.set_bound_const(i, val, wildcard_type);
                    val = constant_fold_bin_op<Add>(wildcard_type, (uint64_t)rng() >> shift, 0);
                    exprs[i] = make_const(wildcard_type, val);
                    state.set_binding(i, *exprs[i].get());
                }
                break;
            case halide_type_int:
                {
                    int64_t val = constant_fold_bin_op<Add>(wildcard_type, (int64_t)rng() >> shift, 0);
                    state.set_bound_const(i, val, wildcard_type);
                    val = constant_fold_bin_op<Add>(wildcard_type, (int64_t)rng() >> shift, 0);
                    exprs[i] = make_const(wildcard_type, val);
                }
                break;
            case halide_type_float:
                {
                    // Use a very narrow range of precise floats, so
                    // that none of the rules a human is likely to
                    // write have instabilities.
                    double val = ((int64_t)(rng() & 15) - 8) / 2.0;
                    state.set_bound_const(i, val, wildcard_type);
                    val = ((int64_t)(rng() & 15) - 8) / 2.0;
                    exprs[i] = make_const(wildcard_type, val);
                }
                break;
            default:
                return; // Don't care about handles
            }
            state.set_binding(i, *exprs[i].get());
        }

        halide_scalar_value_t val_pred, val_before, val_after;
        halide_type_t type = output_type;
        if (!evaluate_predicate(pred, state)) continue;
        before.make_folded_const(val_before, type, state);
        uint16_t lanes = type.lanes;
        after.make_folded_const(val_after, type, state);
        lanes |= type.lanes;

        if (lanes & MatcherState::special_values_mask) continue;

        bool ok = true;
        switch (output_type.code) {
        case halide_type_uint:
            // Compare normalized representations
            ok &= (constant_fold_bin_op<Add>(output_type, val_before.u.u64, 0) ==
                   constant_fold_bin_op<Add>(output_type, val_after.u.u64, 0));
            break;
        case halide_type_int:
            ok &= (constant_fold_bin_op<Add>(output_type, val_before.u.i64, 0) ==
                   constant_fold_bin_op<Add>(output_type, val_after.u.i64, 0));
            break;
        case halide_type_float:
            {
                double error = std::abs(val_before.u.f64 - val_after.u.f64);
                // We accept an equal bit pattern (e.g. inf vs inf),
                // a small floating point difference, or turning a nan into not-a-nan.
                ok &= (error < 0.01 ||
                       val_before.u.u64 == val_after.u.u64 ||
                       std::isnan(val_before.u.f64));
                break;
            }
        default:
            return;
        }

        if (!ok) {
            debug(0) << "Fails with values:\n";
            for (int i = 0; i < max_wild; i++) {
                halide_scalar_value_t val;
                state.get_bound_const(i, val, wildcard_type);
                debug(0) << " c" << i << ": " << make_const_expr(val, wildcard_type) << "\n";
            }
            for (int i = 0; i < max_wild; i++) {
                debug(0) << " _" << i << ": " << Expr(state.get_binding(i)) << "\n";
            }
            debug(0) << " Before: " << make_const_expr(val_before, output_type) << "\n";
            debug(0) << " After:  " << make_const_expr(val_after, output_type) << "\n";
            debug(0) << val_before.u.u64 << " " << val_after.u.u64 << "\n";
            internal_error;
        }
    }
}

template<typename Before,
         typename After,
         typename Predicate,
         typename = typename std::enable_if<!(std::remove_reference<Before>::type::foldable &&
                                              std::remove_reference<After>::type::foldable)>::type>
HALIDE_ALWAYS_INLINE
void fuzz_test_rule(Before &&before, After &&after, Predicate &&pred,
                   halide_type_t, halide_type_t, int dummy = 0) noexcept {
    // We can't verify rewrite rules that can't be constant-folded.
}

HALIDE_ALWAYS_INLINE
bool evaluate_predicate(bool x, MatcherState &) noexcept {
    return x;
}

template<typename Pattern,
         typename = typename enable_if_pattern<Pattern>::type>
HALIDE_ALWAYS_INLINE
bool evaluate_predicate(Pattern p, MatcherState &state) {
    halide_scalar_value_t c;
    halide_type_t ty = halide_type_of<bool>();
    p.make_folded_const(c, ty, state);
    // Overflow counts as a failed predicate
    return (c.u.u64 != 0) && ((ty.lanes & MatcherState::special_values_mask) == 0);
}

// #defines for testing

// Print all successful or failed matches
#define HALIDE_DEBUG_MATCHED_RULES 0
#define HALIDE_DEBUG_UNMATCHED_RULES 0

// Set to true if you want to fuzz test every rewrite passed to
// operator() to ensure the input and the output have the same value
// for lots of random values of the wildcards. Run
// correctness_simplify with this on.
#define HALIDE_FUZZ_TEST_RULES 0

template<typename Instance>
struct Rewriter {
    Instance instance;
    Expr result;
    MatcherState state;
    halide_type_t output_type, wildcard_type;
    bool validate;

    HALIDE_ALWAYS_INLINE
    Rewriter(Instance &&instance, halide_type_t ot, halide_type_t wt) :
        instance(std::forward<Instance>(instance)), output_type(ot), wildcard_type(wt) {}

    template<typename After>
    HALIDE_NEVER_INLINE
    void build_replacement(After after) {
        result = after.make(state, output_type);
    }

    template<typename Before,
             typename After,
             typename = typename enable_if_pattern<Before>::type,
             typename = typename enable_if_pattern<After>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before before, After after) {
        static_assert((Before::binds & After::binds) == After::binds, "Rule result uses unbound values");
        #if HALIDE_FUZZ_TEST_RULES
        fuzz_test_rule(before, after, true, wildcard_type, output_type);
        #endif
        if (before.template match<0>(instance, state)) {
            build_replacement(after);
            #if HALIDE_DEBUG_MATCHED_RULES
            debug(0) << instance << " -> " << result << " via " << before << " -> " << after << "\n";
            #endif
            return true;
        } else {
            #if HALIDE_DEBUG_UNMATCHED_RULES
            debug(0) << instance << " does not match " << before << "\n";
            #endif
            return false;
        }
    }

    template<typename Before,
             typename = typename enable_if_pattern<Before>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before before, const Expr &after) noexcept {
        if (before.template match<0>(instance, state)) {
            result = after;
            #if HALIDE_DEBUG_MATCHED_RULES
            debug(0) << instance << " -> " << result << " via " << before << " -> " << after << "\n";
            #endif
            return true;
        } else {
            #if HALIDE_DEBUG_UNMATCHED_RULES
            debug(0) << instance << " does not match " << before << "\n";
            #endif
            return false;
        }
    }

    template<typename Before,
             typename = typename enable_if_pattern<Before>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before before, int64_t after) noexcept {
        #if HALIDE_FUZZ_TEST_RULES
        fuzz_test_rule(before, Const(after), true, wildcard_type, output_type);
        #endif
        if (before.template match<0>(instance, state)) {
            result = make_const(output_type, after);
            #if HALIDE_DEBUG_MATCHED_RULES
            debug(0) << instance << " -> " << result << " via " << before << " -> " << after << "\n";
            #endif
            return true;
        } else {
            #if HALIDE_DEBUG_UNMATCHED_RULES
            debug(0) << instance << " does not match " << before << "\n";
            #endif
            return false;
        }
    }

    template<typename Before,
             typename After,
             typename Predicate,
             typename = typename enable_if_pattern<Before>::type,
             typename = typename enable_if_pattern<After>::type,
             typename = typename enable_if_pattern<Predicate>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before before, After after, Predicate pred) {
        static_assert(Predicate::foldable, "Predicates must consist only of operations that can constant-fold");
        static_assert((Before::binds & After::binds) == After::binds, "Rule result uses unbound values");
        static_assert((Before::binds & Predicate::binds) == Predicate::binds, "Rule predicate uses unbound values");
        #if HALIDE_FUZZ_TEST_RULES
        fuzz_test_rule(before, after, pred, wildcard_type, output_type);
        #endif
        if (before.template match<0>(instance, state) &&
            evaluate_predicate(pred, state)) {
            build_replacement(after);
            #if HALIDE_DEBUG_MATCHED_RULES
            debug(0) << instance << " -> " << result << " via " << before << " -> " << after << " when " << pred << "\n";
            #endif
            return true;
        } else {
            #if HALIDE_DEBUG_UNMATCHED_RULES
            debug(0) << instance << " does not match " << before << "\n";
            #endif
            return false;
        }
    }

    template<typename Before,
             typename Predicate,
             typename = typename enable_if_pattern<Before>::type,
             typename = typename enable_if_pattern<Predicate>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before before, const Expr &after, Predicate pred) {
        static_assert(Predicate::foldable, "Predicates must consist only of operations that can constant-fold");
        if (before.template match<0>(instance, state) &&
            evaluate_predicate(pred, state)) {
            result = after;
            #if HALIDE_DEBUG_MATCHED_RULES
            debug(0) << instance << " -> " << result << " via " << before << " -> " << after << " when " << pred << "\n";
            #endif
            return true;
        } else {
            #if HALIDE_DEBUG_UNMATCHED_RULES
            debug(0) << instance << " does not match " << before << "\n";
            #endif
            return false;
        }
    }

    template<typename Before,
             typename Predicate,
             typename = typename enable_if_pattern<Before>::type,
             typename = typename enable_if_pattern<Predicate>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before before, int64_t after, Predicate pred) {
        static_assert(Predicate::foldable, "Predicates must consist only of operations that can constant-fold");
        #if HALIDE_FUZZ_TEST_RULES
        fuzz_test_rule(before, Const(after), pred, wildcard_type, output_type);
        #endif
        if (before.template match<0>(instance, state) &&
            evaluate_predicate(pred, state)) {
            result = make_const(output_type, after);
            #if HALIDE_DEBUG_MATCHED_RULES
            debug(0) << instance << " -> " << result << " via " << before << " -> " << after << " when " << pred << "\n";
            #endif
            return true;
        } else {
            #if HALIDE_DEBUG_UNMATCHED_RULES
            debug(0) << instance << " does not match " << before << "\n";
            #endif
            return false;
        }
    }
};

/** Construct a rewriter for the given instance, which may be a pattern
 * with concrete expressions as leaves, or just an expression. The
 * second optional argument (wildcard_type) is a hint as to what the
 * type of the wildcards is likely to be. If omitted it uses the same
 * type as the expression itself.  They are not required to be this
 * type, but the rule will only be tested for wildcards of that type
 * when testing is enabled.
 *
 * The rewriter can be used to check to see if the instance is one of
 * some number of patterns and if so rewrite it into another form,
 * using its operator() method. See Simplify.cpp for a bunch of
 * example usage.
 */
// @{
template<typename Instance,
         typename = typename enable_if_pattern<Instance>::type>
HALIDE_ALWAYS_INLINE
auto rewriter(Instance instance, halide_type_t output_type, halide_type_t wildcard_type) noexcept -> Rewriter<decltype(pattern_arg(instance))> {
    return {pattern_arg(instance), output_type, wildcard_type};
}

template<typename Instance,
         typename = typename enable_if_pattern<Instance>::type>
HALIDE_ALWAYS_INLINE
auto rewriter(Instance instance, halide_type_t output_type) noexcept -> Rewriter<decltype(pattern_arg(instance))> {
    return {pattern_arg(instance), output_type, output_type};
}

HALIDE_ALWAYS_INLINE
auto rewriter(const Expr &e, halide_type_t wildcard_type) noexcept -> Rewriter<decltype(pattern_arg(e))> {
    return {pattern_arg(e), e.type(), wildcard_type};
}


HALIDE_ALWAYS_INLINE
auto rewriter(const Expr &e) noexcept -> Rewriter<decltype(pattern_arg(e))> {
    return {pattern_arg(e), e.type(), e.type()};
}
// @}

}

}  // namespace Internal
}  // namespace Halide

#endif
