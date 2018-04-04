#ifndef HALIDE_IR_MATCH_H
#define HALIDE_IR_MATCH_H

/** \file
 * Defines a method to match a fragment of IR against a pattern containing wildcards
 */

#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"

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

constexpr int max_wild = 4;

/** To save stack space, the matcher objects are largely stateless and
 * immutable. This state object is built up during matching and then
 * consumed when constructing a replacement Expr.
 */
struct MatcherState {
    const BaseExprNode *bindings[max_wild] {nullptr, nullptr, nullptr, nullptr};

    HALIDE_ALWAYS_INLINE
    void set_binding(int i, const BaseExprNode &n) {
        bindings[i] = &n;
    }

    HALIDE_ALWAYS_INLINE
    bool is_bound(int i) const {
        return bindings[i] != nullptr;
    }

    HALIDE_ALWAYS_INLINE
    const BaseExprNode *get_binding(int i) const {
        return bindings[i];
    }

    halide_scalar_value_t bound_const[max_wild];

    // values of the lanes field with special meaning.
    static constexpr uint16_t signed_integer_overflow = 0x8000;
    static constexpr uint16_t indeterminate_expression = 0x4000;
    static constexpr uint16_t special_values_mask = 0xc000;

    halide_type_t bound_const_type[max_wild];
    uint32_t const_bound_mask = 0;

    HALIDE_ALWAYS_INLINE
    void set_bound_const(int i, int64_t s, halide_type_t t) {
        bound_const[i].u.i64 = s;
        bound_const_type[i] = t;
        const_bound_mask |= (1 << i);
    }

    HALIDE_ALWAYS_INLINE
    void set_bound_const(int i, uint64_t u, halide_type_t t) {
        bound_const[i].u.u64 = u;
        bound_const_type[i] = t;
        const_bound_mask |= (1 << i);
    }

    HALIDE_ALWAYS_INLINE
    void set_bound_const(int i, double f, halide_type_t t) {
        bound_const[i].u.f64 = f;
        bound_const_type[i] = t;
        const_bound_mask |= (1 << i);
    }

    HALIDE_ALWAYS_INLINE
    void get_bound_const(int i, halide_scalar_value_t &val, halide_type_t &type) const {
        val = bound_const[i];
        type = bound_const_type[i];
    }

    HALIDE_ALWAYS_INLINE
    bool is_bound_const(int i) const {
        return (const_bound_mask & (1 << i)) != 0;
    }

};

template<typename T,
         typename = typename std::remove_reference<T>::type::pattern_tag>
struct enable_if_pattern {
    struct type {};
};

template<typename Pattern,
         typename = typename enable_if_pattern<Pattern>::type>
HALIDE_ALWAYS_INLINE
Expr to_expr(Pattern &&p, const MatcherState &state) {
    return p.make(state);
}

HALIDE_ALWAYS_INLINE
Expr to_expr(const BaseExprNode &e, const MatcherState &state) {
    return Expr(&e);
}

inline NO_INLINE
Expr to_special_expr(halide_type_t ty) {
    uint16_t flags = ty.lanes & MatcherState::special_values_mask;
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
Expr to_expr(halide_scalar_value_t val, halide_type_t ty, const MatcherState &state) {
    halide_type_t scalar_type = ty;
    if (scalar_type.lanes & MatcherState::special_values_mask) {
        return to_special_expr(scalar_type);
    }

    int lanes = scalar_type.lanes;
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

HALIDE_ALWAYS_INLINE
std::ostream &operator<<(std::ostream &s, const BaseExprNode &n) {
    s << Expr(&n);
    return s;
}

bool equal_helper(const BaseExprNode &a, const BaseExprNode &b);

// A fast version of expression equality that assumes a well-typed non-null expression tree.
HALIDE_ALWAYS_INLINE
bool equal(const BaseExprNode &a, const BaseExprNode &b) {
    // Early out
    return (&a == &b) ||
        ((a.type == b.type) &&
         (a.node_type == b.node_type) &&
         equal_helper(a, b));
}

template<int i>
struct WildConstInt {
    struct pattern_tag {};
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e;
        if (op->node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        if (op->node_type != IRNodeType::IntImm) {
            return false;
        }
        int64_t value = ((const IntImm *)op)->value;
        if (state.is_bound_const(i)) {
            halide_scalar_value_t val;
            halide_type_t type;
            state.get_bound_const(i, val, type);
            return op->type == type && value == val.u.i64;
        }
        state.set_bound_const(i, value, e.type);
        return true;
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return to_expr(val, type, state);
    }

    HALIDE_ALWAYS_INLINE void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, const MatcherState &state) const {
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
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e;
        if (op->node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        if (op->node_type != IRNodeType::UIntImm) {
            return false;
        }
        uint64_t value = ((const UIntImm *)op)->value;
        if (state.is_bound_const(i)) {
            halide_scalar_value_t val;
            halide_type_t type;
            state.get_bound_const(i, val, type);
            return op->type == type && value == val.u.u64;
        }
        state.set_bound_const(i, value, e.type);
        return true;
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return to_expr(val, type, state);
    }

    HALIDE_ALWAYS_INLINE void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, const MatcherState &state) const {
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
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
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
        if (state.is_bound_const(i)) {
            halide_scalar_value_t val;
            halide_type_t type;
            state.get_bound_const(i, val, type);
            return op->type == type && value == val.u.f64;
        }
        state.set_bound_const(i, value, ty);
        return true;
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return to_expr(val, type, state);
    }

    HALIDE_ALWAYS_INLINE void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, const MatcherState &state) const {
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
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        static_assert(i >= 0 && i < max_wild, "Wild with out-of-range index");
        const BaseExprNode *op = &e;
        if (op->node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        switch (op->node_type) {
        case IRNodeType::IntImm:
            return WildConstInt<i>().match(e, state);
        case IRNodeType::UIntImm:
            return WildConstUInt<i>().match(e, state);
        case IRNodeType::FloatImm:
            return WildConstFloat<i>().match(e, state);
        default:
            return false;
        }
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        halide_scalar_value_t val;
        halide_type_t type;
        state.get_bound_const(i, val, type);
        return to_expr(val, type, state);
    }

    HALIDE_ALWAYS_INLINE void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, const MatcherState &state) const {
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
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        if (state.is_bound(i)) {
            return equal(*state.get_binding(i), e);
        }
        state.set_binding(i, e);
        return true;
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        return state.get_binding(i);
    }
};

template<int i>
std::ostream &operator<<(std::ostream &s, const Wild<i> &op) {
    s << "_" << i;
    return s;
}

// Matches a specific constant or broadcast of that constant. The
// constant must be representable as an int.
template<int i>
struct Const {
    struct pattern_tag {};
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        const BaseExprNode *op = &e;
        if (e.node_type == IRNodeType::Broadcast) {
            op = ((const Broadcast *)op)->value.get();
        }
        switch (op->node_type) {
        case IRNodeType::IntImm:
            return ((const IntImm *)op)->value == i;
        case IRNodeType::UIntImm:
            return ((const UIntImm *)op)->value == i;
        case IRNodeType::FloatImm:
            return ((const FloatImm *)op)->value == i;
        default:
            return false;
        }
    }

    HALIDE_ALWAYS_INLINE bool match(const Const<i> &, MatcherState &state) const {
        return true;
    }
};

template<int i>
std::ostream &operator<<(std::ostream &s, const Const<i> &op) {
    s << i;
    return s;
}

template<typename Op>
int64_t constant_fold_bin_op(halide_type_t &, int64_t, int64_t);

template<typename Op>
uint64_t constant_fold_bin_op(halide_type_t &, uint64_t, uint64_t);

template<typename Op>
double constant_fold_bin_op(halide_type_t &, double, double);

template<typename Op>
uint64_t constant_fold_cmp_op(int64_t, int64_t);

template<typename Op>
uint64_t constant_fold_cmp_op(uint64_t, uint64_t);

template<typename Op>
uint64_t constant_fold_cmp_op(double, double);


// Matches one of the binary operators
template<typename Op, typename A, typename B>
struct BinOp {
    struct pattern_tag {};
    A a;
    B b;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        if (e.node_type != Op::_node_type) {
            return false;
        }
        const Op &op = (const Op &)e;
        return a.match(*op.a.get(), state) && b.match(*op.b.get(), state);
    }

    template<typename Op2, typename A2, typename B2>
    HALIDE_ALWAYS_INLINE bool match(const BinOp<Op2, A2, B2> &op, MatcherState &state) const {
        return std::is_same<Op, Op2>::value && a.match(op.a, state) && b.match(op.b, state);
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        Expr ea = to_expr(a, state), eb = to_expr(b, state);
        match_types(ea, eb);
        return Op::make(std::move(ea), std::move(eb));
    }

    template<typename A1 = A, typename B1 = B>
    HALIDE_ALWAYS_INLINE void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, const MatcherState &state) const {
        halide_scalar_value_t val_a, val_b;
        halide_type_t type_a, type_b;
        a.make_folded_const(val_a, type_a, state);
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
};

// Matches one of the comparison operators
template<typename Op, typename A, typename B>
struct CmpOp {
    struct pattern_tag {};
    A a;
    B b;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        if (e.node_type != Op::_node_type) {
            return false;
        }
        const Op &op = (const Op &)e;
        return a.match(*op.a.get(), state) && b.match(*op.b.get(), state);
    }

    template<typename Op2, typename A2, typename B2>
    HALIDE_ALWAYS_INLINE bool match(const CmpOp<Op2, A2, B2> &op, MatcherState &state) const {
        return std::is_same<Op, Op2>::value && a.match(op.a, state) && b.match(op.b, state);
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        Expr ea = to_expr(a, state), eb = to_expr(b, state);
        match_types(ea, eb);
        return Op::make(std::move(ea), std::move(eb));
    }

    template<typename A1 = A,
             typename B1 = B>
    HALIDE_ALWAYS_INLINE void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, const MatcherState &state) const {
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

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<Add, A, B> operator+(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Add>(halide_type_t &t, int64_t a, int64_t b) {
    t.lanes |= ((t.bits >= 32) && add_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    uint64_t ones = (uint64_t)(-1);
    return (a + b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Add>(halide_type_t &t, uint64_t a, uint64_t b) {
    uint64_t ones = (uint64_t)(-1);
    return (a + b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Add>(halide_type_t &t, double a, double b) {
    return a + b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<Sub, A, B> operator-(A a, B b) {
    return {a, b};
}

HALIDE_ALWAYS_INLINE
BinOp<Sub, const BaseExprNode &, const BaseExprNode &> sub(const Expr &a, const Expr &b) {
    return {*a.get(), *b.get()};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Sub>(halide_type_t &t, int64_t a, int64_t b) {
    t.lanes |= ((t.bits >= 32) && sub_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    uint64_t ones = (uint64_t)(-1);
    return (a - b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Sub>(halide_type_t &t, uint64_t a, uint64_t b) {
    uint64_t ones = (uint64_t)(-1);
    return (a - b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Sub>(halide_type_t &t, double a, double b) {
    return a - b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<Mul, A, B> operator*(A a, B b) {
    return {a, b};
}

HALIDE_ALWAYS_INLINE
BinOp<Mul, const BaseExprNode &, const BaseExprNode &> mul(const Expr &a, const Expr &b) {
    return {*a.get(), *b.get()};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Mul>(halide_type_t &t, int64_t a, int64_t b) {
    t.lanes |= ((t.bits >= 32) && mul_would_overflow(t.bits, a, b)) ? MatcherState::signed_integer_overflow : 0;
    uint64_t ones = (uint64_t)(-1);
    return (a * b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Mul>(halide_type_t &t, uint64_t a, uint64_t b) {
    uint64_t ones = (uint64_t)(-1);
    return (a * b) & (ones >> (64 - t.bits));
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Mul>(halide_type_t &t, double a, double b) {
    return a * b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<Div, A, B> operator/(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Div>(halide_type_t &t, int64_t a, int64_t b) {
    if (b == 0) {
        t.lanes |= MatcherState::indeterminate_expression;
        return 0;
    } else {
        return div_imp(a, b);
    }
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Div>(halide_type_t &t, uint64_t a, uint64_t b) {
    if (b == 0) {
        t.lanes |= MatcherState::indeterminate_expression;
        return 0;
    } else {
        return a / b;
    }
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Div>(halide_type_t &t, double a, double b) {
    return a / b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<Mod, A, B> operator%(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Mod>(halide_type_t &t, int64_t a, int64_t b) {
    if (b == 0) {
        t.lanes |= MatcherState::indeterminate_expression;
        return 0;
    } else {
        return mod_imp(a, b);
    }
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Mod>(halide_type_t &t, uint64_t a, uint64_t b) {
    if (b == 0) {
        t.lanes |= MatcherState::indeterminate_expression;
        return 0;
    } else {
        return a % b;
    }
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Mod>(halide_type_t &t, double a, double b) {
    return mod_imp(a, b);
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<Min, A, B> min(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Min>(halide_type_t &t, int64_t a, int64_t b) {
    return std::min(a, b);
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Min>(halide_type_t &t, uint64_t a, uint64_t b) {
    return std::min(a, b);
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Min>(halide_type_t &t, double a, double b) {
    return std::min(a, b);
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<Max, A, B> max(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
int64_t constant_fold_bin_op<Max>(halide_type_t &t, int64_t a, int64_t b) {
    return std::max(a, b);
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Max>(halide_type_t &t, uint64_t a, uint64_t b) {
    return std::max(a, b);
}

template<>
HALIDE_ALWAYS_INLINE
double constant_fold_bin_op<Max>(halide_type_t &t, double a, double b) {
    return std::max(a, b);
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
CmpOp<LT, A, B> operator<(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LT>(int64_t a, int64_t b) {
    return a < b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LT>(uint64_t a, uint64_t b) {
    return a < b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LT>(double a, double b) {
    return a < b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
CmpOp<GT, A, B> operator>(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GT>(int64_t a, int64_t b) {
    return a > b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GT>(uint64_t a, uint64_t b) {
    return a > b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GT>(double a, double b) {
    return a > b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
CmpOp<LE, A, B> operator<=(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LE>(int64_t a, int64_t b) {
    return a <= b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LE>(uint64_t a, uint64_t b) {
    return a <= b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<LE>(double a, double b) {
    return a <= b;
}


template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
CmpOp<GE, A, B> operator>=(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GE>(int64_t a, int64_t b) {
    return a >= b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GE>(uint64_t a, uint64_t b) {
    return a >= b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<GE>(double a, double b) {
    return a >= b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
CmpOp<EQ, A, B> operator==(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<EQ>(int64_t a, int64_t b) {
    return a == b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<EQ>(uint64_t a, uint64_t b) {
    return a == b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<EQ>(double a, double b) {
    return a == b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
CmpOp<NE, A, B> operator!=(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<NE>(int64_t a, int64_t b) {
    return a != b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<NE>(uint64_t a, uint64_t b) {
    return a != b;
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_cmp_op<NE>(double a, double b) {
    return a != b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<Or, A, B> operator||(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<Or>(halide_type_t &t, uint64_t a, uint64_t b) {
    return a | b;
}

template<typename A,
         typename B,
         typename = typename enable_if_pattern<A>::type,
         typename = typename enable_if_pattern<B>::type>
HALIDE_ALWAYS_INLINE
BinOp<And, A, B> operator&&(A a, B b) {
    return {a, b};
}

template<>
HALIDE_ALWAYS_INLINE
uint64_t constant_fold_bin_op<And>(halide_type_t &t, uint64_t a, uint64_t b) {
    return a & b;
}

template<typename... Args>
struct Intrin {
    struct pattern_tag {};
    Call::ConstString intrin;
    std::tuple<Args...> args;

    template<int i,
             typename = typename std::enable_if<(i < sizeof...(Args))>::type>
    HALIDE_ALWAYS_INLINE bool match_args(int, const Call &c, MatcherState &state) const {
        return std::get<i>(args).match(*c.args[i].get(), state) && match_args<i + 1>(0, c, state);
    }

    template<int i>
    HALIDE_ALWAYS_INLINE bool match_args(double, const Call &c, MatcherState &state) const {
        return true;
    }

    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        if (e.node_type != IRNodeType::Call) {
            return false;
        }
        const Call &c = (const Call &)e;
        return (c.is_intrinsic(intrin) && match_args<0>(0, c, state));
    }

    template<int i,
             typename = typename std::enable_if<(i < sizeof...(Args))>::type>
    HALIDE_ALWAYS_INLINE void print_args(int, std::ostream &s) const {
        s << std::get<i>(args);
        if (i + 1 < sizeof...(Args)) {
            s << ", ";
        }
        print_args<i+1>(0, s);
    }

    template<int i>
    HALIDE_ALWAYS_INLINE void print_args(double, std::ostream &s) const {
    }

    HALIDE_ALWAYS_INLINE void print_args(std::ostream &s) const {
        print_args<0>(0, s);
    }

    Intrin(Call::ConstString intrin, Args... args) : intrin(intrin), args(args...) {}
};

template<typename... Args>
inline std::ostream &operator<<(std::ostream &s, const Intrin<Args...> &op) {
    s << op.intrin << "(";
    op.print_args(s);
    s << ")";
    return s;
}

template<typename... Args>
HALIDE_ALWAYS_INLINE
Intrin<Args...> intrin(Call::ConstString name, Args&&... args) {
    return Intrin<Args...>(name, std::forward<Args>(args)...);
}

template<typename Op, typename A>
struct UnaryOp {
    struct pattern_tag {};
    A a;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        const Op &op = (const Op &)e;
        return (op.node_type == Op::_node_type &&
                a.match(*op.a.get(), state));
    }

    template<typename Op2, typename A2>
    HALIDE_ALWAYS_INLINE bool match(const UnaryOp<Op2, A2> &op, MatcherState &state) const {
        return std::is_same<Op, Op2>::value && a.match(op.a, state);
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        return Op::make(to_expr(a, state));
    }
};

template<typename A>
HALIDE_ALWAYS_INLINE
UnaryOp<Not, A> operator!(A &&a) {
    return UnaryOp<Not, A>{std::forward<A>(a)};
}

template<typename A>
inline std::ostream &operator<<(std::ostream &s, const UnaryOp<Not, A> &op) {
    s << "!(" << op.a << ")";
    return s;
}

template<typename C, typename T, typename F>
struct SelectOp {
    struct pattern_tag {};
    C c;
    T t;
    F f;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        const Select &op = (const Select &)e;
        return (e.node_type == Select::_node_type &&
                c.match(*op.condition.get(), state) &&
                t.match(*op.true_value.get(), state) &&
                f.match(*op.false_value.get(), state));
    }
    template<typename C2, typename T2, typename F2>
    HALIDE_ALWAYS_INLINE bool match(const SelectOp<C2, T2, F2> &instance, MatcherState &state) const {
        return (c.match(instance.c, state) &&
                t.match(instance.t, state) &&
                f.match(instance.f, state));
    }
    template<typename Pattern,
             typename = typename enable_if_pattern<Pattern>::type>
    HALIDE_ALWAYS_INLINE bool match(const Pattern &p, MatcherState &state) const {
        return false;
    }
    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        return Select::make(to_expr(c, state), to_expr(t, state), to_expr(f, state));
    }
};

template<typename C, typename T, typename F>
std::ostream &operator<<(std::ostream &s, const SelectOp<C, T, F> &op) {
    s << "select(" << op.c << ", " << op.t << ", " << op.f << ")";
    return s;
}

template<typename C,
         typename T,
         typename F,
         typename = typename enable_if_pattern<C>::type,
         typename = typename enable_if_pattern<T>::type,
         typename = typename enable_if_pattern<F>::type>
HALIDE_ALWAYS_INLINE
SelectOp<C, T, F> select(C c, T t, F f) {
    return {c, t, f};
}

HALIDE_ALWAYS_INLINE
SelectOp<const BaseExprNode &, const BaseExprNode &, const BaseExprNode &> select(const Expr &c, const Expr &t, const Expr &f) {
    return {*c.get(), *t.get(), *f.get()};
}

template<typename A>
struct BroadcastOp {
    struct pattern_tag {};
    A a;
    int lanes;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        if (e.node_type == Broadcast::_node_type) {
            const Broadcast &op = (const Broadcast &)e;
            if ((lanes == -1 || lanes == op.lanes) &&
                a.match(*op.value.get(), state)) {
                return true;
            }
        }
        return false;
    }

    template<typename A2>
    HALIDE_ALWAYS_INLINE bool match(const BroadcastOp<A2> &op, MatcherState &state) const {
        return a.match(op.a, state) && (lanes == op.lanes || lanes == -1 || op.lanes == -1);
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        return Broadcast::make(to_expr(a, state), lanes);
    }

};

template<typename A>
inline std::ostream &operator<<(std::ostream &s, const BroadcastOp<A> &op) {
    s << "broadcast(" << op.a << ")";
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
BroadcastOp<A> broadcast(A &&a, int lanes = -1) { // -1 => matches any number of lanes
    return BroadcastOp<A>{std::forward<A>(a), lanes};
}

template<typename A, typename B>
struct RampOp {
    struct pattern_tag {};
    A a;
    B b;
    int lanes;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        const Ramp &op = (const Ramp &)e;
        if (op.node_type == Ramp::_node_type &&
            a.match(*op.base.get(), state) &&
            b.match(*op.stride.get(), state)) {
            return true;
        } else {
            return false;
        }
    }
    template<typename A2, typename B2>
    HALIDE_ALWAYS_INLINE bool match(const RampOp<A2, B2> &op, MatcherState &state) const {
        return (a.match(op.a, state) &&
                b.match(op.b, state));
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        return Ramp::make(to_expr(a, state), to_expr(b, state), lanes);
    }
};

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const RampOp<A, B> &op) {
    s << "ramp(" << op.a << ", " << op.b << ")";
    return s;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
RampOp<A, B> ramp(A &&a, B &&b, int lanes = -1) { // -1 => matches any number of lanes
    return RampOp<A, B>{std::forward<A>(a), std::forward<B>(b), lanes};
}

template<typename A>
struct NegateOp {
    struct pattern_tag {};
    A a;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        const Sub &op = (const Sub &)e;
        return (op.node_type == Sub::_node_type &&
                a.match(*op.b.get(), state) &&
                is_zero(op.a));
    }
    HALIDE_ALWAYS_INLINE bool match(NegateOp<A> &&p, MatcherState &state) const {
        return a.match(p.a, state);
    }
    template<typename Pattern,
             typename = typename enable_if_pattern<Pattern>::type>
    HALIDE_ALWAYS_INLINE bool match(Pattern &&p, MatcherState &state) const {
        return false;
    }
    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        Expr ea = to_expr(a, state);
        Expr z = make_zero(ea.type());
        return Sub::make(std::move(z), std::move(ea));
    }
    template<typename A1 = A>
    HALIDE_ALWAYS_INLINE void make_folded_const(halide_scalar_value_t &val, halide_type_t &ty, const MatcherState &state) const {
        a.make_folded_const(val, ty, state);
        switch (ty.code) {
        case halide_type_int:
            if (ty.bits >= 32 && val.u.i64 && !(val.u.i64 << (65 - ty.bits))) {
                // Trying to negate the most negative signed int for a no-overflow type.
                ty.lanes |= MatcherState::signed_integer_overflow;
            } else {
                val.u.i64 = -val.u.i64;
            }
            break;
        case halide_type_uint:
            val.u.u64 = -val.u.u64; // Let it overflow
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

template<typename A,
         typename = typename enable_if_pattern<A>::type>
HALIDE_ALWAYS_INLINE
NegateOp<A> operator-(A a) {
    return NegateOp<A>{a};
}

template<typename A>
struct CastOp {
    struct pattern_tag {};
    Type type;
    A a;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        const Cast &op = (const Cast &)e;
        return (op.node_type == Cast::_node_type &&
                a.match(*op.value.get(), state));
    }
    template<typename A2>
    HALIDE_ALWAYS_INLINE bool match(const CastOp<A2> &op, MatcherState &state) const {
        return a.match(op.a, state);
    }

    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        return cast(type, to_expr(a, state));
    }
};

template<typename A>
std::ostream &operator<<(std::ostream &s, const CastOp<A> &op) {
    s << "cast(" << op.type << ", " << op.a << ")";
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
CastOp<A> cast(Type t, A &&a) {
    return CastOp<A>{t, std::forward<A>(a)};
}

template<typename A>
struct Fold {
    struct pattern_tag {};
    A a;
    HALIDE_ALWAYS_INLINE Expr make(const MatcherState &state) const {
        halide_scalar_value_t c {{0}};
        halide_type_t ty;
        a.make_folded_const(c, ty, state);
        return to_expr(c, ty, state);
    }
};

template<typename A>
HALIDE_ALWAYS_INLINE
Fold<A> fold(A a) {
    return {a};
}

// Statically verify properties of each rewrite rule
template<typename Before, typename After>
HALIDE_ALWAYS_INLINE
void validate_rule() {
    // TODO
}

HALIDE_ALWAYS_INLINE
bool evaluate_predicate(bool x, MatcherState &) {
    return x;
}

template<typename Pattern,
         typename = typename enable_if_pattern<Pattern>::type>
HALIDE_ALWAYS_INLINE
bool evaluate_predicate(Pattern &&p, MatcherState &state) {
    halide_scalar_value_t c;
    halide_type_t ty;
    p.make_folded_const(c, ty, state);
    return c.u.u64 != 0;
}

template<typename Before, typename After, typename Predicate>
struct Rule {
    Before before;
    After after;
    Predicate pred;

    template<typename Instance>
    HALIDE_ALWAYS_INLINE
    bool apply(Instance &&in, Expr &result) {
        MatcherState state;
        // debug(0) << in << " vs " << before << "\n";
        if (!before.match(std::forward<Instance>(in), state)) {
            // debug(0) << "No match\n";
            return false;
        } else if (!evaluate_predicate(pred, state)) {
            return false;
        } else {
            // debug(0) << "Match\n";
            result = to_expr(after, state);
            return true;
        }
    }
};

template<typename Before,
         typename After,
         typename = typename enable_if_pattern<After>::type,
         typename = typename enable_if_pattern<Before>::type>
HALIDE_ALWAYS_INLINE
Rule<Before, After, bool> rule(Before &&before, After &&after, bool pred = true) {
    return {before, after, pred};
}

template<typename Before,
         typename After,
         typename Predicate,
         typename = typename enable_if_pattern<After>::type,
         typename = typename enable_if_pattern<Before>::type,
         typename = typename enable_if_pattern<Predicate>::type>
HALIDE_ALWAYS_INLINE
Rule<Before, After, Predicate> rule(Before &&before, After &&after, Predicate &&pred) {
    return {before, after, pred};
}

template<typename Before,
         typename = typename enable_if_pattern<Before>::type>
HALIDE_ALWAYS_INLINE
Rule<Before, const BaseExprNode &, bool> rule(Before &&before, const Expr &after, bool pred = true) {
    return {before, *after.get(), pred};
}

template<typename Before,
         typename Predicate,
         typename = typename enable_if_pattern<Before>::type,
         typename = typename enable_if_pattern<Predicate>::type>
HALIDE_ALWAYS_INLINE
Rule<Before, const BaseExprNode &, Predicate> rule(Before &&before, const Expr &after, Predicate &&pred) {
    return {before, *after.get(), pred};
}

template<typename Before,
         typename = typename enable_if_pattern<Before>::type>
HALIDE_ALWAYS_INLINE
Rule<Before, const BaseExprNode &, bool> rule(Before &&before, Expr &after, bool pred = true) {
    return {before, *after.get(), pred};
}

template<typename Before,
         typename Predicate,
         typename = typename enable_if_pattern<Before>::type,
         typename = typename enable_if_pattern<Predicate>::type>
HALIDE_ALWAYS_INLINE
Rule<Before, const BaseExprNode &, Predicate> rule(Before &&before, Expr &after, Predicate &&pred) {
    return {before, *after.get(), pred};
}

template<typename Instance>
HALIDE_ALWAYS_INLINE
bool rewrite(Instance &&, Expr &) {
    return false;
}

template<typename Instance, typename Rule, typename... Rules>
HALIDE_ALWAYS_INLINE
bool rewrite(Instance &&in, Expr &result, Rule &&rule, Rules&&... rules) {
    if (rule.apply(in, result)) {
        return true;
    } else {
        return rewrite(std::forward<Instance>(in), result,
                       std::forward<Rules>(rules)...);
    }
}

}

}
}

#endif
