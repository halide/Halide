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

/** To save stack space, the matcher objects are largely stateless and
 * immutable. This state object is built up during matching and then
 * consumed when constructing a replacement Expr.
 */
struct MatcherState {
    const BaseExprNode *bindings[4] {nullptr, nullptr, nullptr, nullptr};

    HALIDE_ALWAYS_INLINE
    void set_binding(int i, const BaseExprNode &n) {
        bindings[i] = &n;
    }

    HALIDE_ALWAYS_INLINE
    bool is_bound(int i) const {
        return bindings[i];
    }

    HALIDE_ALWAYS_INLINE
    const BaseExprNode *get_binding(int i) const {
        return bindings[i];
    }
};

template<typename Pattern>
HALIDE_ALWAYS_INLINE
Expr to_expr(Pattern &&p, MatcherState &state) {
    return p.make(state);
}

HALIDE_ALWAYS_INLINE
Expr to_expr(const BaseExprNode &e, MatcherState &state) {
    return Expr(&e);
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

// Matches and binds to any Expr
template<int i>
struct Wild {
    struct IRMatcherPattern {};
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        if (state.is_bound(i)) {
            // early-out
            const BaseExprNode *val = state.get_binding(i);
            if (val->node_type != e.node_type) return false;
            if (val == &e) return true;
            return equal(*val, e);
        }
        state.set_binding(i, e);
        return true;
    }

    HALIDE_ALWAYS_INLINE bool match(const Wild<i> &, MatcherState &state) const {
        return true;
    }

    template<typename Pattern>
    HALIDE_ALWAYS_INLINE bool match(const Pattern &op, MatcherState &state) const {
        return false;
    }

    HALIDE_ALWAYS_INLINE Expr make(MatcherState &state) const {
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
    struct IRMatcherPattern {};
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

    template<typename Pattern>
    HALIDE_ALWAYS_INLINE bool match(const Pattern &op, MatcherState &state) const {
        return false;
    }
};

template<int i>
std::ostream &operator<<(std::ostream &s, const Const<i> &op) {
    s << i;
    return s;
}

// Matches one of the binary operators
template<typename Op, typename A, typename B>
struct BinOp {
    struct IRMatcherPattern {};
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
        return std::is_same<Op, Op2>::value && a.match(op.a) && b.match(op.b);
    }

    template<typename Pattern>
    HALIDE_ALWAYS_INLINE bool match(const Pattern &op, MatcherState &state) const {
        return false;
    }

    HALIDE_ALWAYS_INLINE Expr make(MatcherState &state) const {
        Expr ea = to_expr(a, state), eb = to_expr(b, state);
        if (ea.type() != eb.type()) {
            match_types(ea, eb);
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
std::ostream &operator<<(std::ostream &s, const BinOp<LE, A, B> &op) {
    s << "(" << op.a << " <= " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<LT, A, B> &op) {
    s << "(" << op.a << " < " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<GE, A, B> &op) {
    s << "(" << op.a << " >= " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<GT, A, B> &op) {
    s << "(" << op.a << " > " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<EQ, A, B> &op) {
    s << "(" << op.a << " == " << op.b << ")";
    return s;
}

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const BinOp<NE, A, B> &op) {
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
BinOp<Add, A, B> operator+(A &&a, B &&b) {
    return BinOp<Add, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<Sub, A, B> operator-(A &&a, B &&b) {
    return BinOp<Sub, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<Mul, A, B> operator*(A &&a, B &&b) {
    return BinOp<Mul, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<Div, A, B> operator/(A &&a, B &&b) {
    return BinOp<Div, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<Mod, A, B> operator%(A &&a, B &&b) {
    return BinOp<Mod, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<Min, A, B> min(A &&a, B &&b) {
    return BinOp<Min, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<Max, A, B> max(A &&a, B &&b) {
    return BinOp<Max, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<LT, A, B> operator<(A &&a, B &&b) {
    return BinOp<LT, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<GT, A, B> operator>(A &&a, B &&b) {
    return BinOp<GT, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<LE, A, B> operator<=(A &&a, B &&b) {
    return BinOp<LE, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<GE, A, B> operator>=(A &&a, B &&b) {
    return BinOp<GE, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<EQ, A, B> operator==(A &&a, B &&b) {
    return BinOp<EQ, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<NE, A, B> operator!=(A &&a, B &&b) {
    return BinOp<NE, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<Or, A, B> operator||(A &&a, B &&b) {
    return BinOp<Or, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
BinOp<And, A, B> operator&&(A &&a, B &&b) {
    return BinOp<And, A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename... Args>
struct Intrin {
    Call::ConstString intrin;
    std::tuple<Args...> args;

    template<int i,
             typename = typename std::enable_if<(i + 1 < sizeof...(Args))>::type>
    HALIDE_ALWAYS_INLINE bool match_args(int, const Call &c, MatcherState &state) const {
        return std::get<i>(args).match(c.args[i], state) && match_args<i + 1>(0, c, state);
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

    Intrin(Call::ConstString intrin, Args... args) : intrin(intrin), args(args...) {}
};

template<typename... Args>
HALIDE_ALWAYS_INLINE
Intrin<Args...> intrin(Call::ConstString name, Args&&... args) {
    return Intrin<Args...>(name, std::forward<Args>(args)...);
}

template<typename Op, typename A>
struct UnaryOp {
    A a;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        const Op &op = (const Op &)e;
        return (op.node_type == Op::_node_type &&
                a.match(*op.a.get(), state));
    }

    template<typename Op2, typename A2>
    HALIDE_ALWAYS_INLINE bool match(const UnaryOp<Op2, A2> &op, MatcherState &state) const {
        return std::is_same<Op, Op2>::value && a.match(op.a);
    }

    template<typename Pattern>
    HALIDE_ALWAYS_INLINE bool match(const Pattern &op, MatcherState &state) const {
        return false;
    }

    HALIDE_ALWAYS_INLINE Expr make(MatcherState &state) const {
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
    template<typename Pattern>
    HALIDE_ALWAYS_INLINE bool match(const Pattern &p, MatcherState &state) const {
        return false;
    }
    HALIDE_ALWAYS_INLINE Expr make(MatcherState &state) const {
        return Select::make(to_expr(c, state), to_expr(t, state), to_expr(f, state));
    }
};

template<typename C, typename T, typename F>
std::ostream &operator<<(std::ostream &s, const SelectOp<C, T, F> &op) {
    s << "select(" << op.c << ", " << op.t << ", " << op.f << ")";
    return s;
}

template<typename C, typename T, typename F>
HALIDE_ALWAYS_INLINE
SelectOp<C, T, F> select(C &&c, T &&t, F &&f) {
    return SelectOp<C, T, F>{std::forward<C>(c), std::forward<T>(t), std::forward<F>(f)};
}

template<typename A>
struct BroadcastOp {
    struct IRMatcherPattern {};
    A a;
    HALIDE_ALWAYS_INLINE bool match(const BaseExprNode &e, MatcherState &state) const {
        if (e.node_type == Broadcast::_node_type) {
            const Broadcast &op = (const Broadcast &)e;
            if (a.match(*op.value.get(), state)) {
                return true;
            }
        }
        return false;
    }
};

template<typename A>
inline std::ostream &operator<<(std::ostream &s, const BroadcastOp<A> &op) {
    s << "broadcast(" << op.a << ")";
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
BroadcastOp<A> broadcast(A &&a) { // matches any number of lanes
    return BroadcastOp<A>{std::forward<A>(a)};
}

template<typename A, typename B>
struct RampOp {
    A a;
    B b;
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
        return (a.match(op.a) &&
                b.match(op.b));
    }
};

template<typename A, typename B>
std::ostream &operator<<(std::ostream &s, const RampOp<A, B> &op) {
    s << "ramp(" << op.a << ", " << op.b << ")";
    return s;
}

template<typename A, typename B>
HALIDE_ALWAYS_INLINE
RampOp<A, B> ramp(A &&a, B &&b) { // matches any number of lanes
    return RampOp<A, B>{std::forward<A>(a), std::forward<B>(b)};
}

template<typename A>
struct NegateOp {
    struct IRMatcherPattern {};
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
    template<typename Pattern>
    HALIDE_ALWAYS_INLINE bool match(Pattern &&p, MatcherState &state) const {
        return false;
    }
    HALIDE_ALWAYS_INLINE Expr make(MatcherState &state) const {
        Expr ea = to_expr(a, state);
        Expr z = make_zero(ea.type());
        return Sub::make(std::move(z), std::move(ea));
    }
};

template<typename A>
std::ostream &operator<<(std::ostream &s, const NegateOp<A> &op) {
    s << "-" << op.a;
    return s;
}

template<typename A>
HALIDE_ALWAYS_INLINE
NegateOp<A> operator-(A &&a) {
    return NegateOp<A>{std::forward<A>(a)};
}

template<typename A>
struct CastOp {
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
    template<typename Pattern>
    HALIDE_ALWAYS_INLINE bool match(Pattern &&p, MatcherState &state) const {
        return false;
    }
    HALIDE_ALWAYS_INLINE Expr make(MatcherState &state) const {
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

// Statically verify properties of each rewrite rule
template<typename Before, typename After>
void validate_rule() {
    // TODO
}

template<typename Instance, typename Before, typename After>
HALIDE_ALWAYS_INLINE
bool apply_rule_inner(Instance &&in, Expr &result, Before &&before, After &&after) {
    MatcherState state;
    if (!before.match(std::forward<Instance>(in), state)) {
        return false;
    } else {
        result = to_expr(std::forward<After>(after), state);
        return true;
    }
}

template<typename Pattern>
HALIDE_ALWAYS_INLINE
Pattern unwrap_expr(Pattern &&p) {
    return std::forward<Pattern>(p);
}

HALIDE_ALWAYS_INLINE
const BaseExprNode &unwrap_expr(const Expr &e) {
    return *e.get();
}

HALIDE_ALWAYS_INLINE
const BaseExprNode &unwrap_expr(Expr &e) {
    return *e.get();
}

template<typename Instance, typename Before, typename After>
HALIDE_ALWAYS_INLINE
bool apply_rule(Instance &&in, Expr &result, Before &&before, After &&after) {
    validate_rule<Before, After>();
    return apply_rule_inner(unwrap_expr(std::forward<Instance>(in)),
                            result,
                            std::forward<Before>(before),
                            unwrap_expr(std::forward<After>(after)));
}

template<typename Instance>
HALIDE_ALWAYS_INLINE
bool rewrite(Instance &&, Expr &) {
    return false;
}

template<typename Instance, typename Before, typename After, typename... Rules>
HALIDE_ALWAYS_INLINE
bool rewrite(Instance &&in, Expr &result, Before &&before, After &&after, Rules&&... rules) {
    if (apply_rule(in, result, std::forward<Before>(before), std::forward<After>(after))) {
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
