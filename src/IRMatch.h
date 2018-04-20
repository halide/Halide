#ifndef HALIDE_IR_MATCH_H
#define HALIDE_IR_MATCH_H

/** \file
 * Defines a method to match a fragment of IR against a pattern containing wildcards
 */

#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"
#include "ModulusRemainder.h"

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

    HALIDE_ALWAYS_INLINE
    void reset() noexcept {
        // TODO: delete me
    }
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
    static std::atomic<int> counter;
    if (flags & MatcherState::indeterminate_expression) {
        return Call::make(ty, Call::indeterminate_expression, {counter++}, Call::Intrinsic);
    } else if (flags & MatcherState::signed_integer_overflow) {
        return Call::make(ty, Call::signed_integer_overflow, {counter++}, Call::Intrinsic);
    }
    // unreachable
    return Expr();
}

HALIDE_ALWAYS_INLINE
Expr make_const_expr(halide_scalar_value_t val, halide_type_t ty, MatcherState & __restrict__ state) {
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

    const BaseExprNode &expr;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        return equal(expr, e.expr);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return &expr;
    }
};

inline std::ostream &operator<<(std::ostream &s, SpecificExpr e) {
    s << Expr(&e.expr);
    return s;
}

template<int i>
struct WildConstInt {
    struct pattern_tag {};

    constexpr static uint32_t binds = 1 << i;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e.expr;
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
        state.set_bound_const(i, value, e.expr.type);
        return true;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return make_const_expr(val, type, state);
    }

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const {
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
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e.expr;
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
        state.set_bound_const(i, value, e.expr.type);
        return true;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return make_const_expr(val, type, state);
    }

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
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
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        halide_type_t ty = e.expr.type;
        const BaseExprNode *op = &e.expr;
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
    Expr make(MatcherState & __restrict__ state) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return make_const_expr(val, type, state);
    }

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
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
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e.expr;
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
    Expr make(MatcherState & __restrict__ state) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return make_const_expr(val, type, state);
    }

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
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
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        if (bound & binds) {
            return equal(*state.get_binding(i), e.expr);
        }
        state.set_binding(i, e.expr);
        return true;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return state.get_binding(i);
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
    int64_t val;
    halide_type_t type;

    constexpr static uint32_t binds = 0;

    HALIDE_ALWAYS_INLINE
    Const(int64_t v) : val(v) {}

    HALIDE_ALWAYS_INLINE
    Const(int64_t v, halide_type_t t) : val(v), type(t) {}

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        const BaseExprNode *op = &e.expr;
        if (e.expr.node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        switch (op->node_type) {
        case IRNodeType::IntImm:
            return ((const IntImm *)op)->value == (int64_t)val;
        case IRNodeType::UIntImm:
            return ((const UIntImm *)op)->value == (uint64_t)val;
        case IRNodeType::FloatImm:
            return ((const FloatImm *)op)->value == (double)val;
        default:
            return false;
        }
    }

    HALIDE_ALWAYS_INLINE
    bool match(const Const &b, MatcherState & __restrict__ state) const noexcept {
        return val == b.val;
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return make_const(type, val);
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
    return Const(x);
}
HALIDE_ALWAYS_INLINE
const SpecificExpr pattern_arg(const Expr &e) {
    return {*e.get()};
}

inline std::ostream &operator<<(std::ostream &s, const Const &op) {
    s << op.val;
    return s;
}

template<typename Op>
int64_t constant_fold_bin_op(halide_type_t &, int64_t, int64_t) noexcept;

template<typename Op>
uint64_t constant_fold_bin_op(halide_type_t &, uint64_t, uint64_t) noexcept;

template<typename Op>
double constant_fold_bin_op(halide_type_t &, double, double) noexcept;

template<typename Op, typename A, typename B>
struct BinOpFolder {
    HALIDE_ALWAYS_INLINE
    static void make_folded_const(const A &a, const B &b, halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) noexcept {
        halide_scalar_value_t val_a, val_b;
        halide_type_t type_a, type_b;
        a.make_folded_const(val_a, type_a, state);
        if ((std::is_same<Op, And>::value && val_a.u.u64 == 0) ||
            (std::is_same<Op, Or>::value && val_a.u.u64 == 1)) {
            // Short circuit
            ty = type_a;
            val = val_a;
            return;
        }
        b.make_folded_const(val_b, type_b, state);
        // The types are known to match except possibly for overflow flags in the lanes field
        ty = type_a;
        ty.lanes |= type_b.lanes;
        switch (type_a.code) {
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
    static Expr make(const A &a, const B &b, MatcherState & __restrict__ state) noexcept {
        Expr ea = a.make(state), eb = b.make(state);
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

template<typename Op, typename A>
struct BinOpFolder<Op, A, Const> {
    HALIDE_ALWAYS_INLINE
    static void make_folded_const(const A &a, Const b, halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) noexcept {
        a.make_folded_const(val, ty, state);
        switch (ty.code) {
        case halide_type_int:
            val.u.i64 = constant_fold_bin_op<Op>(ty, val.u.i64, (int64_t)b.val);
            break;
        case halide_type_uint:
            val.u.u64 = constant_fold_bin_op<Op>(ty, val.u.u64, (uint64_t)b.val);
            break;
        case halide_type_float:
            val.u.f64 = constant_fold_bin_op<Op>(ty, val.u.f64, (double)b.val);
            break;
        default:
            // unreachable
            ;
        }
    }

    HALIDE_ALWAYS_INLINE
    static Expr make(const A &a, Const b, MatcherState & __restrict__ state) {
        Expr ea = a.make(state);
        Expr eb = make_const(ea.type(), b.val);
        return Op::make(std::move(ea), std::move(eb));
    }
};

template<typename Op, typename B>
struct BinOpFolder<Op, Const, B> {
    HALIDE_ALWAYS_INLINE
    static void make_folded_const(Const a, const B &b, halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) noexcept {
        b.make_folded_const(val, ty, state);
        switch (ty.code) {
        case halide_type_int:
            val.u.i64 = constant_fold_bin_op<Op>(ty, (int64_t)a.val, val.u.i64);
            break;
        case halide_type_uint:
            val.u.u64 = constant_fold_bin_op<Op>(ty, (uint64_t)a.val, val.u.u64);
            break;
        case halide_type_float:
            val.u.f64 = constant_fold_bin_op<Op>(ty, (double)a.val, val.u.f64);
            break;
        default:
            // unreachable
            ;
        }
    }

    HALIDE_ALWAYS_INLINE
    static Expr make(Const a, const B &b, MatcherState & __restrict__ state) {
        Expr eb = b.make(state);
        Expr ea = make_const(eb.type(), a.val);
        return Op::make(std::move(ea), std::move(eb));
    }
};

// Matches one of the binary operators
template<typename Op, typename A, typename B>
struct BinOp {
    struct pattern_tag {};
    A a;
    B b;

    constexpr static uint32_t binds = bindings<A>::mask | bindings<B>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        if (e.expr.node_type != Op::_node_type) {
            return false;
        }
        const Op &op = (const Op &)e.expr;
        return (a.template match<bound>(SpecificExpr{*op.a.get()}, state) &&
                b.template match<bound | bindings<A>::mask>(SpecificExpr{*op.b.get()}, state));
    }

    template<uint32_t bound, typename Op2, typename A2, typename B2>
    HALIDE_ALWAYS_INLINE
    bool match(const BinOp<Op2, A2, B2> &op, MatcherState & __restrict__ state) const noexcept {
        return (std::is_same<Op, Op2>::value &&
                a.template match<bound>(op.a, state) &&
                b.template match<bound | bindings<A>::mask>(op.b, state));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return BinOpFolder<Op, A, B>::make(a, b, state);
    }


    template<typename A1 = A, typename B1 = B>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const {
        BinOpFolder<Op, A1, B1>::make_folded_const(a, b, val, ty, state);
    }
};

template<typename Op>
uint64_t constant_fold_cmp_op(int64_t, int64_t) noexcept;

template<typename Op>
uint64_t constant_fold_cmp_op(uint64_t, uint64_t) noexcept;

template<typename Op>
uint64_t constant_fold_cmp_op(double, double) noexcept;

template<typename Op, typename A, typename B>
struct CmpOpFolder {
    HALIDE_ALWAYS_INLINE
    static void make_folded_const(const A &a, const B &b, halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) noexcept {
        halide_scalar_value_t val_a, val_b;
        halide_type_t type_a, type_b;
        a.make_folded_const(val_a, type_a, state);
        b.make_folded_const(val_b, type_b, state);
        ty.code = halide_type_uint;
        ty.bits = 1;
        ty.lanes = type_a.lanes | type_b.lanes;
        switch (type_a.code) {
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
    }

    HALIDE_ALWAYS_INLINE
    static Expr make(const A &a, const B &b, MatcherState & __restrict__ state) {
        Expr ea = a.make(state), eb = b.make(state);
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

template<typename Op, typename B>
struct CmpOpFolder<Op, Const, B> {
    HALIDE_ALWAYS_INLINE
    static void make_folded_const(Const a, const B &b, halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) noexcept {
        b.make_folded_const(val, ty, state);
        switch (ty.code) {
        case halide_type_int:
            val.u.u64 = constant_fold_cmp_op<Op>((int64_t)a.val, val.u.i64);
            break;
        case halide_type_uint:
            val.u.u64 = constant_fold_cmp_op<Op>((uint64_t)a.val, val.u.u64);
            break;
        case halide_type_float:
            val.u.u64 = constant_fold_cmp_op<Op>((double)a.val, val.u.f64);
            break;
        default:
            // unreachable
            ;
        }
        ty.bits = 1;
        ty.code = halide_type_uint;
    }

    HALIDE_ALWAYS_INLINE
    static Expr make(Const a, const B &b, MatcherState & __restrict__ state) {
        Expr eb = b.make(state);
        Expr ea = make_const(eb.type(), a.val);
        return Op::make(std::move(ea), std::move(eb));
    }
};

template<typename Op, typename A>
struct CmpOpFolder<Op, A, Const> {
    HALIDE_ALWAYS_INLINE
    static void make_folded_const(const A &a, Const b, halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) noexcept {
        a.make_folded_const(val, ty, state);
        switch (ty.code) {
        case halide_type_int:
            val.u.u64 = constant_fold_cmp_op<Op>(val.u.i64, (int64_t)b.val);
            break;
        case halide_type_uint:
            val.u.u64 = constant_fold_cmp_op<Op>(val.u.u64, (uint64_t)b.val);
            break;
        case halide_type_float:
            val.u.u64 = constant_fold_cmp_op<Op>(val.u.f64, (double)b.val);
            break;
        default:
            // unreachable
            ;
        }
        ty.bits = 1;
        ty.code = halide_type_uint;
    }

    HALIDE_ALWAYS_INLINE
    static Expr make(const A &a, Const b, MatcherState & __restrict__ state) {
        Expr ea = a.make(state);
        Expr eb = make_const(ea.type(), b.val);
        return Op::make(std::move(ea), std::move(eb));
    }
};

// Matches one of the comparison operators
template<typename Op, typename A, typename B>
struct CmpOp {
    struct pattern_tag {};
    A a;
    B b;

    constexpr static uint32_t binds = bindings<A>::mask | bindings<B>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        if (e.expr.node_type != Op::_node_type) {
            return false;
        }
        const Op &op = (const Op &)e.expr;
        return (a.template match<bound>(SpecificExpr{*op.a.get()}, state) &&
                b.template match<bound | bindings<A>::mask>(SpecificExpr{*op.b.get()}, state));
    }

    template<uint32_t bound, typename Op2, typename A2, typename B2>
    HALIDE_ALWAYS_INLINE
    bool match(const CmpOp<Op2, A2, B2> &op, MatcherState & __restrict__ state) const noexcept {
        return (std::is_same<Op, Op2>::value &&
                a.template match<bound>(op.a, state) &&
                b.template match<bound | bindings<A>::mask>(op.b, state));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return CmpOpFolder<Op, A, B>::make(a, b, state);
    }

    template<typename A1 = A,
             typename B1 = B>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
        CmpOpFolder<Op, A, B>::make_folded_const(a, b, val, ty, state);
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
    return ((a + b) << dead_bits) >> dead_bits;
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
    int dead_bits = 64 - t.bits;
    // Drop the high bits then sign-extend them back
    return ((a - b) << dead_bits) >> dead_bits;
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
    return ((a * b) << dead_bits) >> dead_bits;
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
    return 0;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Or>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return a | b;
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Or>(halide_type_t &t, double a, double b) noexcept {
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
    return 0;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<And>(halide_type_t &t, uint64_t a, uint64_t b) noexcept {
    return a & b;
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<And>(halide_type_t &t, double a, double b) noexcept {
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
    bool match_args(int, const Call &c, MatcherState & __restrict__ state) const noexcept {
        using T = decltype(std::get<i>(args));
        return (std::get<i>(args).template match<bound>(SpecificExpr{*c.args[i].get()}, state) &&
                match_args<i + 1, bound | bindings<T>::mask>(0, c, state));
    }

    template<int i, uint32_t binds>
    HALIDE_ALWAYS_INLINE
    bool match_args(double, const Call &c, MatcherState & __restrict__ state) const noexcept {
        return true;
    }

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        if (e.expr.node_type != IRNodeType::Call) {
            return false;
        }
        const Call &c = (const Call &)e.expr;
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
Intrin<Args...> intrin(Call::ConstString name, Args&&... args) noexcept {
    return Intrin<Args...>(name, std::forward<Args>(args)...);
}

struct IndeterminateOp {
    struct pattern_tag {};

    halide_type_t type;

    static constexpr uint32_t binds = 0;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        if (e.expr.node_type != IRNodeType::Call) {
            return false;
        }
        const Call &c = (const Call &)e.expr;
        return c.is_intrinsic(Call::indeterminate_expression);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        halide_type_t ty = type;
        ty.lanes |= MatcherState::indeterminate_expression;
        return make_const_special_expr(ty);
    }
};

HALIDE_ALWAYS_INLINE
IndeterminateOp indet(halide_type_t type) {
    return {type};
}

HALIDE_ALWAYS_INLINE
IndeterminateOp indet() {
    return IndeterminateOp();
}

inline std::ostream &operator<<(std::ostream &s, const IndeterminateOp &op) {
    s << "indeterminate_expression()";
    return s;
}


struct OverflowOp {
    struct pattern_tag {};

    halide_type_t type;

    static constexpr uint32_t binds = 0;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        if (e.expr.node_type != IRNodeType::Call) {
            return false;
        }
        const Call &c = (const Call &)e.expr;
        return c.is_intrinsic(Call::signed_integer_overflow);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        halide_type_t ty = type;
        ty.lanes |= MatcherState::signed_integer_overflow;
        return make_const_special_expr(ty);
    }
};

HALIDE_ALWAYS_INLINE
OverflowOp overflow(halide_type_t type) {
    return {type};
}

HALIDE_ALWAYS_INLINE
OverflowOp overflow() {
    return OverflowOp();
}

inline std::ostream &operator<<(std::ostream &s, const OverflowOp &op) {
    s << "signed_integer_overflow()";
    return s;
}

template<typename A>
struct NotOp {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        const Not &op = (const Not &)e.expr;
        return (e.expr.node_type == IRNodeType::Not &&
                a.template match<bound>(SpecificExpr{*op.a.get()}, state));
    }

    template<uint32_t bound, typename A2>
    HALIDE_ALWAYS_INLINE
    bool match(const NotOp<A2> &op, MatcherState & __restrict__ state) const noexcept {
        return a.template match<bound>(op.a, state);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return Not::make(a.make(state));
    }

    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
        a.make_folded_const(val, ty, state);
        val.u.u64 = (val.u.u64 == 0) ? 1 : 0;
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
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        const Select &op = (const Select &)e.expr;
        return (e.expr.node_type == Select::_node_type &&
                c.template match<bound>(SpecificExpr{*op.condition.get()}, state) &&
                t.template match<bound | bindings<C>::mask>(SpecificExpr{*op.true_value.get()}, state) &&
                f.template match<bound | bindings<C>::mask | bindings<T>::mask>(SpecificExpr{*op.false_value.get()}, state));
    }
    template<uint32_t bound, typename C2, typename T2, typename F2>
    HALIDE_ALWAYS_INLINE
    bool match(const SelectOp<C2, T2, F2> &instance, MatcherState & __restrict__ state) const noexcept {
        return (c.template match<bound>(instance.c, state) &&
                t.template match<bound | bindings<C>::mask>(instance.t, state) &&
                f.template match<bound | bindings<C>::mask | bindings<T>::mask>(instance.f, state));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return Select::make(c.make(state), t.make(state), f.make(state));
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

template<typename A>
struct BroadcastOp {
    struct pattern_tag {};
    A a;
    int lanes;

    constexpr static uint32_t binds = bindings<A>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        if (e.expr.node_type == Broadcast::_node_type) {
            const Broadcast &op = (const Broadcast &)e.expr;
            if ((lanes == -1 || lanes == op.lanes) &&
                a.template match<bound>(SpecificExpr{*op.value.get()}, state)) {
                return true;
            }
        }
        return false;
    }

    template<uint32_t bound, typename A2>
    HALIDE_ALWAYS_INLINE
    bool match(const BroadcastOp<A2> &op, MatcherState & __restrict__ state) const noexcept {
        return (a.template match<bound>(op.a, state) &&
                (lanes == op.lanes || lanes == -1 || op.lanes == -1));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return Broadcast::make(a.make(state), lanes);
    }

};

template<typename A>
inline std::ostream &operator<<(std::ostream &s, const BroadcastOp<A> &op) {
    s << "broadcast(" << op.a << ")";
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
auto broadcast(A a, int lanes = -1) noexcept -> BroadcastOp<decltype(pattern_arg(a))> {
    return {pattern_arg(a), lanes};
}

template<typename A, typename B>
struct RampOp {
    struct pattern_tag {};
    A a;
    B b;
    int lanes;

    constexpr static uint32_t binds = bindings<A>::mask | bindings<B>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        const Ramp &op = (const Ramp &)e.expr;
        if (op.node_type == Ramp::_node_type &&
            (lanes == op.type.lanes() || lanes == -1) &&
            a.template match<bound>(SpecificExpr{*op.base.get()}, state) &&
            b.template match<bound | bindings<A>::mask>(SpecificExpr{*op.stride.get()}, state)) {
            return true;
        } else {
            return false;
        }
    }

    template<uint32_t bound, typename A2, typename B2>
    HALIDE_ALWAYS_INLINE
    bool match(const RampOp<A2, B2> &op, MatcherState & __restrict__ state) const noexcept {
        return ((lanes == op.lanes || lanes == -1 || op.lanes == -1) &&
                a.template match<bound>(op.a, state) &&
                b.template match<bound | bindings<A>::mask>(op.b, state));
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return Ramp::make(a.make(state), b.make(state), lanes);
    }
};

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const RampOp<A, B> &op) {
    s << "ramp(" << op.a << ", " << op.b << ")";
    return s;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto ramp(A a, B b, int lanes = -1) noexcept -> RampOp<decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b), lanes};
}

template<typename A>
struct NegateOp {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        const Sub &op = (const Sub &)e.expr;
        return (op.node_type == Sub::_node_type &&
                a.template match<bound>(SpecificExpr{*op.b.get()}, state) &&
                is_zero(op.a));
    }

    template<uint32_t bound, typename A2>
    HALIDE_ALWAYS_INLINE
    bool match(NegateOp<A2> &&p, MatcherState & __restrict__ state) const noexcept {
        return a.template match<bound>(p.a, state);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        Expr ea = a.make(state);
        Expr z = make_zero(ea.type());
        return Sub::make(std::move(z), std::move(ea));
    }

    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
        a.make_folded_const(val, ty, state);
        int dead_bits = 64 - ty.bits;
        switch (ty.code) {
        case halide_type_int:
            if (ty.bits >= 32 && val.u.i64 && !(val.u.i64 << (65 - ty.bits))) {
                // Trying to negate the most negative signed int for a no-overflow type.
                ty.lanes |= MatcherState::signed_integer_overflow;
            } else {
                // Negate, drop the high bits, and then sign-extend them back
                val.u.i64 = ((-val.u.i64) << dead_bits) >> dead_bits;
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
struct IsConstOp {
    struct pattern_tag {};

    constexpr static uint32_t binds = bindings<A>::mask;

    A a;
    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
        Expr e = a.make(state);
        ty.code = halide_type_uint;
        ty.bits = 64;
        ty.lanes = 1;
        val.u.u64 = is_const(e) ? 1 : 0;
    }
};

template<typename A>
HALIDE_ALWAYS_INLINE
auto is_const(A a) noexcept -> IsConstOp<decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<typename A>
std::ostream &operator<<(std::ostream &s, const IsConstOp<A> &op) {
    s << "is_const(" << op.a << ")";
    return s;
}

template<typename A>
struct CastOp {
    struct pattern_tag {};
    Type type;
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    template<uint32_t bound>
    HALIDE_ALWAYS_INLINE
    bool match(SpecificExpr e, MatcherState & __restrict__ state) const noexcept {
        const Cast &op = (const Cast &)e.expr;
        return (op.node_type == Cast::_node_type &&
                a.template match<bound>(SpecificExpr{*op.value.get()}, state));
    }
    template<uint32_t bound, typename A2>
    HALIDE_ALWAYS_INLINE
    bool match(const CastOp<A2> &op, MatcherState & __restrict__ state) const noexcept {
        return a.template match<bound>(op.a, state);
    }

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const {
        return cast(type, a.make(state));
    }
};

template<typename A>
std::ostream &operator<<(std::ostream &s, const CastOp<A> &op) {
    s << "cast(" << op.type << ", " << op.a << ")";
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
auto cast(halide_type_t t, A a) noexcept -> CastOp<decltype(pattern_arg(a))> {
    return {t, pattern_arg(a)};
}

template<typename A>
struct FoldOp {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask;

    HALIDE_ALWAYS_INLINE
    Expr make(MatcherState & __restrict__ state) const noexcept {
        halide_scalar_value_t c;
        halide_type_t ty;
        a.make_folded_const(c, ty, state);
        return make_const_expr(c, ty, state);
    }
};

template<typename A>
HALIDE_ALWAYS_INLINE
auto fold(A a) noexcept -> FoldOp<decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<typename A>
std::ostream &operator<<(std::ostream &s, const FoldOp<A> &op) {
    s << "fold(" << op.a << ")";
    return s;
}

template<typename A, typename Prover>
struct CanProveOp {
    struct pattern_tag {};
    A a;
    Prover *prover;  // An existing simplifying mutator

    constexpr static uint32_t binds = bindings<A>::mask;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const {
        Expr condition = a.make(state);
        // debug(0) << "Attempting to prove " << a << " = " << condition << "\n";
        condition = prover->mutate(condition, nullptr);
        val.u.u64 = is_one(condition);
        ty.code = halide_type_uint;
        ty.bits = 1;
        ty.lanes = condition.type().lanes();
    };
};

template<typename A, typename Prover>
HALIDE_ALWAYS_INLINE
auto can_prove(A a, Prover *p) noexcept -> CanProveOp<decltype(pattern_arg(a)), Prover> {
    return {pattern_arg(a), p};
}

template<typename A, typename Prover>
std::ostream &operator<<(std::ostream &s, const CanProveOp<A, Prover> &op) {
    s << "can_prove(" << op.a << ")";
    return s;
}

template<typename A, typename B>
struct GCDOp {
    struct pattern_tag {};
    A a;
    B b;

    constexpr static uint32_t binds = bindings<A>::mask | bindings<B>::mask;

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
        halide_scalar_value_t val_a, val_b;
        halide_type_t type_a, type_b;
        a.make_folded_const(val_a, type_a, state);
        b.make_folded_const(val_b, type_b, state);
        ty = type_a;
        ty.lanes |= type_b.lanes;
        internal_assert(ty.code == halide_type_int && ty.bits >= 32);
        val.u.i64 = Halide::Internal::gcd(val_a.u.i64, val_b.u.i64);
    };
};

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
auto gcd(A a, B b) noexcept -> GCDOp<decltype(pattern_arg(a)), decltype(pattern_arg(b))> {
    return {pattern_arg(a), pattern_arg(b)};
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const GCDOp<A, B> &op) {
    s << "gcd(" << op.a << ", " << op.b << ")";
    return s;
}

template<int i, typename A>
struct BindOp {
    struct pattern_tag {};
    A a;

    constexpr static uint32_t binds = bindings<A>::mask | (1 << i);

    HALIDE_ALWAYS_INLINE
    void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, MatcherState & __restrict__ state) const noexcept {
        a.make_folded_const(val, ty, state);
        state.set_bound_const(i, val, ty);
        // The bind node evaluates to true
        val.u.u64 = 1;
        ty.code = halide_type_uint;
        ty.bits = 1;
        ty.lanes = 1;
    };
};

template<int i, typename A>
HALIDE_ALWAYS_INLINE
auto bind(WildConst<i> c, A a) noexcept -> BindOp<i, decltype(pattern_arg(a))> {
    return {pattern_arg(a)};
}

template<int i, typename A>
std::ostream &operator<<(std::ostream &s, const BindOp<i, A> &op) {
    s << "bind(_" << i << " = " << op.a << ")";
    return s;
}

// Statically verify properties of each rewrite rule
template<typename Before, typename After>
HALIDE_ALWAYS_INLINE
void validate_rule() noexcept {
    // TODO
}

HALIDE_ALWAYS_INLINE
bool evaluate_predicate(bool x, MatcherState & __restrict__ ) noexcept {
    return x;
}

template<typename Pattern,
         typename = typename enable_if_pattern<Pattern>::type>
HALIDE_ALWAYS_INLINE
bool evaluate_predicate(Pattern &&p, MatcherState & __restrict__ state) {
    halide_scalar_value_t c;
    halide_type_t ty;
    p.make_folded_const(c, ty, state);
    return (c.u.u64 != 0) && ((ty.lanes & MatcherState::special_values_mask) == 0);
}

#define HALIDE_DEBUG_MATCHED_RULES 0

template<typename Instance>
struct Rewriter {
    Instance instance;
    Expr result;
    MatcherState state;

    HALIDE_ALWAYS_INLINE
    Rewriter(Instance &&instance) : instance(std::forward<Instance>(instance)) {}

    template<typename Before,
             typename After,
             typename = typename enable_if_pattern<Before>::type,
             typename = typename enable_if_pattern<After>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before &&before, After &&after) {
        state.reset();
        if (before.template match<0>(instance, state)) {
            result = after.make(state);
            if (HALIDE_DEBUG_MATCHED_RULES) debug(0) << instance << " -> " << result << " via " << before << " -> " << after << "\n";
            return true;
        } else {
            return false;
        }
    }

    template<typename Before,
             typename = typename enable_if_pattern<Before>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before &&before, const Expr &after) noexcept {
        state.reset();
        if (before.template match<0>(instance, state)) {
            result = after;
            if (HALIDE_DEBUG_MATCHED_RULES) debug(0) << instance << " -> " << result << " via " << before << " -> " << after << "\n";
            return true;
        } else {
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
    bool operator()(Before &&before, After &&after, Predicate &&pred) {
        state.reset();
        if (before.template match<0>(instance, state) &&
            evaluate_predicate(std::forward<Predicate>(pred), state)) {
            result = after.make(state);
            if (HALIDE_DEBUG_MATCHED_RULES) debug(0) << instance << " -> " << result << " via " << before << " -> " << after << " when " << pred << "\n";
            return true;
        } else {
            return false;
        }
    }

    template<typename Before,
             typename Predicate,
             typename = typename enable_if_pattern<Before>::type,
             typename = typename enable_if_pattern<Predicate>::type>
    HALIDE_ALWAYS_INLINE
    bool operator()(Before &&before, const Expr &after, Predicate &&pred) {
        state.reset();
        if (before.template match<0>(instance, state) &&
            evaluate_predicate(std::forward<Predicate>(pred), state)) {
            result = after;
            if (HALIDE_DEBUG_MATCHED_RULES) debug(0) << instance << " -> " << result << " via " << before << " -> " << after << " when " << pred << "\n";
            return true;
        } else {
            return false;
        }
    }
};

template<typename Instance>
HALIDE_ALWAYS_INLINE
Rewriter<Instance> rewriter(Instance &&instance) noexcept {
    return Rewriter<Instance>(std::forward<Instance>(instance));
}
}

}
}

#endif
