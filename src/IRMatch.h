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

// An alternative template-metaprogramming approach to expression matching.
namespace IRMatcher {

struct Wild {
    const Expr *val = nullptr;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        if (val) {
            // early-out
            if ((*val)->node_type != e->node_type) return false;
            if (val->same_as(e)) return true;
            return equal(*val, e);
        }
        val = &e;
        return true;
    }
    operator Expr() {
        return *val;
    }
    HALIDE_ALWAYS_INLINE void reset() {
        val = nullptr;
    }
};

template<typename... Args>
struct Intrin {
    Call::ConstString intrin;
    std::tuple<Args...> args;
    const Call *val = nullptr;

    template<int i,
             typename = typename std::enable_if<(i + 1 < sizeof...(Args))>::type>
    HALIDE_ALWAYS_INLINE bool match_args(int, const Call *c) {
        return std::get<i>(args).match(c->args[i]) && match_args<i + 1>(0, c);
    }

    template<int i>
    HALIDE_ALWAYS_INLINE bool match_args(double, const Call *c) {
        return true;
    }

    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        const Call *c = (const Call *)(e.get());
        if (c->node_type == Call::_node_type) {
            if (c->is_intrinsic(intrin) && match_args<0>(0, c)) {
                val = c;
                return true;
            }
        }
        return false;
    }

    Intrin(Call::ConstString intrin, Args... args) : intrin(intrin), args(args...) {}
    operator Expr() {
        return val;
    }
    HALIDE_ALWAYS_INLINE void reset() {
        val = nullptr;
    }
};

struct ConstInt {
    int64_t i;
    const Expr *val = nullptr;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        if (val) {
            return is_const(e, i);
        } else if (const int64_t *ival = as_const_int(e)) {
            val = &e;
            i = *ival;
            return true;
        } else {
            return false;
        }
    }
    operator Expr() {
        return *val;
    }
    HALIDE_ALWAYS_INLINE void reset() {
        val = nullptr;
    }
};

struct Zero {
    Type type;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        if (is_zero(e)) {
            type = e.type();
            return true;
        } else {
            return false;
        }
    }
    operator Expr() {
        return make_zero(type);
    }
    HALIDE_ALWAYS_INLINE void reset() {}
};

struct One {
    Type type;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        if (is_one(e)) {
            type = e.type();
            return true;
        } else {
            return false;
        }
    }
    operator Expr() {
        return make_one(type);
    }
    HALIDE_ALWAYS_INLINE void reset() {}
};

template<typename Op, typename A, typename B>
struct BinOp {
    A a;
    B b;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        const Op *op = (const Op *)(e.get());
        return (op->node_type == Op::_node_type &&
                a.match(op->a) &&
                b.match(op->b));
    }
    operator Expr() {
        return Op::make(a, b);
    }
    HALIDE_ALWAYS_INLINE void reset() {
        a.reset();
        b.reset();
    }
};

template<typename Op, typename A>
struct UnaryOp {
    A a;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        const Op *op = (const Op *)(e.get());
        return (op->node_type == Op::_node_type &&
                a.match(op->a));
    }
    operator Expr() {
        return Op::make(a);
    }
    HALIDE_ALWAYS_INLINE void reset() {
        a.reset();
    }
};

template<typename C, typename T, typename F>
struct SelectOp {
    C c;
    T t;
    F f;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        const Select *op = (const Select *)(e.get());
        return (op->node_type == Select::_node_type &&
                c.match(op->condition) &&
                t.match(op->true_value) &&
                f.match(op->false_value));
    }
    template<typename C2, typename T2, typename F2>
    HALIDE_ALWAYS_INLINE bool match(const SelectOp<C2, T2, F2> &instance) {
        return c.match(instance.c) && t.match(instance.t) && f.match(instance.f);
    }
    operator Expr() {
        return Select::make(c, t, f);
    }
    HALIDE_ALWAYS_INLINE void reset() {
        c.reset();
        t.reset();
        f.reset();
    }
};

template<typename A>
struct BroadcastOp {
    A a;
    int lanes;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        const Broadcast *op = (const Broadcast *)(e.get());
        if (op->node_type == Broadcast::_node_type &&
            a.match(op->value)) {
            lanes = e.type().lanes();
            return true;
        } else {
            return false;
        }
    }
    operator Expr() {
        return Broadcast::make(a, lanes);
    }
    HALIDE_ALWAYS_INLINE void reset() {
        a.reset();
    }
};

template<typename A, typename B>
struct RampOp {
    A a;
    B b;
    int lanes;
    HALIDE_ALWAYS_INLINE bool match(const Expr &e) {
        const Ramp *op = (const Ramp *)(e.get());
        if (op->node_type == Ramp::_node_type &&
            a.match(op->base) &&
            b.match(op->stride)) {
            lanes = e.type().lanes();
            return true;
        } else {
            return false;
        }
    }
    operator Expr() {
        return Ramp::make(a, b, lanes);
    }
    HALIDE_ALWAYS_INLINE void reset() {
        a.reset();
        b.reset();
    }
};

template<typename A, typename B>
BinOp<Add, A, B> operator+(A &&a, B &&b) {
    return BinOp<Add, A, B>{a, b};
}

template<typename A, typename B>
BinOp<Sub, A, B> operator-(A &&a, B &&b) {
    return BinOp<Sub, A, B>{a, b};
}

template<typename A, typename B>
BinOp<Mul, A, B> operator*(A &&a, B &&b) {
    return BinOp<Mul, A, B>{a, b};
}

template<typename A, typename B>
BinOp<Div, A, B> operator/(A &&a, B &&b) {
    return BinOp<Div, A, B>{a, b};
}

template<typename A, typename B>
BinOp<Mod, A, B> operator%(A &&a, B &&b) {
    return BinOp<Mod, A, B>{a, b};
}

template<typename A, typename B>
BinOp<Min, A, B> min(A &&a, B &&b) {
    return BinOp<Min, A, B>{a, b};
}

template<typename A, typename B>
BinOp<Max, A, B> max(A &&a, B &&b) {
    return BinOp<Max, A, B>{a, b};
}

template<typename A, typename B>
BinOp<LT, A, B> operator<(A &&a, B &&b) {
    return BinOp<LT, A, B>{a, b};
}

template<typename A, typename B>
BinOp<GT, A, B> operator>(A &&a, B &&b) {
    return BinOp<GT, A, B>{a, b};
}

template<typename A, typename B>
BinOp<LE, A, B> operator<=(A &&a, B &&b) {
    return BinOp<LE, A, B>{a, b};
}

template<typename A, typename B>
BinOp<GE, A, B> operator>=(A &&a, B &&b) {
    return BinOp<GE, A, B>{a, b};
}

template<typename A, typename B>
BinOp<EQ, A, B> operator==(A &&a, B &&b) {
    return BinOp<EQ, A, B>{a, b};
}

template<typename A, typename B>
BinOp<NE, A, B> operator!=(A &&a, B &&b) {
    return BinOp<NE, A, B>{a, b};
}

template<typename A>
UnaryOp<Not, A> operator!(A &&a) {
    return UnaryOp<Not, A>{a};
}

template<typename C, typename T, typename F>
SelectOp<C, T, F> select(C &&c, T &&t, F &&f) {
    return SelectOp<C, T, F>{c, t, f};
}

template<typename A>
BroadcastOp<A> broadcast(A &&a) { // matches any number of lanes
    return BroadcastOp<A>{a};
}

template<typename A, typename B>
RampOp<A, B> ramp(A &&a, B &&b) { // matches any number of lanes
    return RampOp<A, B>{a, b};
}

template<typename... Args>
Intrin<Args...> intrin(Call::ConstString name, Args&&... args) {
    return Intrin<Args...>(name, args...);
}

template<typename Instance, typename Pattern>
HALIDE_ALWAYS_INLINE bool match(Instance &&instance, Pattern &&pattern) {
    pattern.reset();
    return pattern.match(instance);
}



}

}
}

#endif
