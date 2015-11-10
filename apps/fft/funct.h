#ifndef FUNCT_H
#define FUNCT_H

#include <string>
#include <vector>

#include <Halide.h>

template <typename T>
class FuncRefVarT : public T {
    Halide::FuncRefVar untyped;

public:
    typedef Halide::Stage Stage;
    typedef Halide::Tuple Tuple;

    FuncRefVarT(const Halide::FuncRefVar& untyped)
        : T(untyped.function().has_pure_definition() ? T(Tuple(untyped)) : T()),
            untyped(untyped) {}

    Stage operator=(T x) { return untyped = x; }
    Stage operator+=(T x) { return untyped = T(Tuple(untyped)) + x; }
    Stage operator-=(T x) { return untyped = T(Tuple(untyped)) - x; }
    Stage operator*=(T x) { return untyped = T(Tuple(untyped)) * x; }
    Stage operator/=(T x) { return untyped = T(Tuple(untyped)) / x; }
};

template <typename T>
class FuncRefExprT : public T {
    Halide::FuncRefExpr untyped;

public:
    typedef Halide::Stage Stage;
    typedef Halide::Tuple Tuple;

    FuncRefExprT(const Halide::FuncRefExpr& untyped)
        : T(Tuple(untyped)), untyped(untyped) {}

    Stage operator=(T x) { return untyped = x; }
    Stage operator+=(T x) { return untyped = T(Tuple(untyped)) + x; }
    Stage operator-=(T x) { return untyped = T(Tuple(untyped)) - x;}
    Stage operator*=(T x) { return untyped = T(Tuple(untyped)) * x; }
    Stage operator/=(T x) { return untyped = T(Tuple(untyped)) / x; }
};

template <typename T>
class FuncT : public Halide::Func {
public:
    typedef Halide::Var Var;
    typedef Halide::Expr Expr;
    typedef Halide::Func Func;

    explicit FuncT(const std::string &name) : Func(name) {}
    FuncT() {}
    explicit FuncT(Expr e) : Func(e) {}
    explicit FuncT(Func f) : Func(f) {}
    explicit FuncT(Halide::Internal::Function f) : Func(f) {}

    FuncRefVarT<T> operator()() const { return Func::operator()(); }
    FuncRefVarT<T> operator()(Var x) const { return Func::operator()(x); }
    FuncRefVarT<T> operator()(Var x, Var y) const { return Func::operator()(x, y); }
    FuncRefVarT<T> operator()(Var x, Var y, Var z) const { return Func::operator()(x, y, z); }
    FuncRefVarT<T> operator()(Var x, Var y, Var z, Var w) const { return Func::operator()(x, y, z, w); }
    FuncRefVarT<T> operator()(Var x, Var y, Var z, Var w, Var u) const { return Func::operator()(x, y, z, w, u); }
    FuncRefVarT<T> operator()(Var x, Var y, Var z, Var w, Var u, Var v) const { return Func::operator()(x, y, z, w, u, v); }
    FuncRefVarT<T> operator()(std::vector<Var> vars) const { return Func::operator()(vars); }

    FuncRefExprT<T> operator()(Expr x) const { return Func::operator()(x); }
    FuncRefExprT<T> operator()(Expr x, Expr y) const { return Func::operator()(x, y); }
    FuncRefExprT<T> operator()(Expr x, Expr y, Expr z) const { return Func::operator()(x, y, z); }
    FuncRefExprT<T> operator()(Expr x, Expr y, Expr z, Expr w) const { return Func::operator()(x, y, z, w); }
    FuncRefExprT<T> operator()(Expr x, Expr y, Expr z, Expr w, Expr u) const { return Func::operator()(x, y, z, w, u); }
    FuncRefExprT<T> operator()(Expr x, Expr y, Expr z, Expr w, Expr u, Expr v) const { return Func::operator()(x, y, z, w, u, v); }
    FuncRefExprT<T> operator()(std::vector<Expr> vars) const { return Func::operator()(vars); }
};

// Forward operator overload invocations on FuncRefVarT/FuncRefExprT to
// the type the user intended (T).

// TODO(dsharlet): This is obscene. Find a better way... but it is unlikely
// there is one.
template <typename T>
T operator-(FuncRefVarT<T> x) { return -static_cast<T>(x); }
template <typename T>
T operator~(FuncRefVarT<T> x) { return ~static_cast<T>(x); }

template <typename T>
T operator+(FuncRefVarT<T> a, T b) { return static_cast<T>(a) + b; }
template <typename T>
T operator-(FuncRefVarT<T> a, T b) { return static_cast<T>(a) - b; }
template <typename T>
T operator*(FuncRefVarT<T> a, T b) { return static_cast<T>(a) * b; }
template <typename T>
T operator/(FuncRefVarT<T> a, T b) { return static_cast<T>(a) / b; }
template <typename T>
T operator%(FuncRefVarT<T> a, T b) { return static_cast<T>(a) % b; }
template <typename T>
T operator+(T a, FuncRefVarT<T> b) { return a + static_cast<T>(b); }
template <typename T>
T operator-(T a, FuncRefVarT<T> b) { return a - static_cast<T>(b); }
template <typename T>
T operator*(T a, FuncRefVarT<T> b) { return a * static_cast<T>(b); }
template <typename T>
T operator/(T a, FuncRefVarT<T> b) { return a / static_cast<T>(b); }
template <typename T>
T operator%(T a, FuncRefVarT<T> b) { return a % static_cast<T>(b); }

template <typename T>
Halide::Expr operator==(FuncRefVarT<T> a, T b) { return static_cast<T>(a) == b; }
template <typename T>
Halide::Expr operator!=(FuncRefVarT<T> a, T b) { return static_cast<T>(a) != b; }
template <typename T>
Halide::Expr operator<=(FuncRefVarT<T> a, T b) { return static_cast<T>(a) <= b; }
template <typename T>
Halide::Expr operator>=(FuncRefVarT<T> a, T b) { return static_cast<T>(a) >= b; }
template <typename T>
Halide::Expr operator<(FuncRefVarT<T> a, T b) { return static_cast<T>(a) < b; }
template <typename T>
Halide::Expr operator>(FuncRefVarT<T> a, T b) { return static_cast<T>(a) > b; }
template <typename T>
Halide::Expr operator==(T a, FuncRefVarT<T> b) { return a == static_cast<T>(b); }
template <typename T>
Halide::Expr operator!=(T a, FuncRefVarT<T> b) { return a != static_cast<T>(b); }
template <typename T>
Halide::Expr operator<=(T a, FuncRefVarT<T> b) { return a <= static_cast<T>(b); }
template <typename T>
Halide::Expr operator>=(T a, FuncRefVarT<T> b) { return a >= static_cast<T>(b); }
template <typename T>
Halide::Expr operator<(T a, FuncRefVarT<T> b) { return a < static_cast<T>(b); }
template <typename T>
Halide::Expr operator>(T a, FuncRefVarT<T> b) { return a > static_cast<T>(b); }

template <typename T>
T operator-(FuncRefExprT<T> x) { return -static_cast<T>(x); }
template <typename T>
T operator~(FuncRefExprT<T> x) { return ~static_cast<T>(x); }

template <typename T>
T operator+(FuncRefExprT<T> a, T b) { return static_cast<T>(a) + b; }
template <typename T>
T operator-(FuncRefExprT<T> a, T b) { return static_cast<T>(a) - b; }
template <typename T>
T operator*(FuncRefExprT<T> a, T b) { return static_cast<T>(a) * b; }
template <typename T>
T operator/(FuncRefExprT<T> a, T b) { return static_cast<T>(a) / b; }
template <typename T>
T operator%(FuncRefExprT<T> a, T b) { return static_cast<T>(a) % b; }
template <typename T>
T operator+(T a, FuncRefExprT<T> b) { return a + static_cast<T>(b); }
template <typename T>
T operator-(T a, FuncRefExprT<T> b) { return a - static_cast<T>(b); }
template <typename T>
T operator*(T a, FuncRefExprT<T> b) { return a * static_cast<T>(b); }
template <typename T>
T operator/(T a, FuncRefExprT<T> b) { return a / static_cast<T>(b); }
template <typename T>
T operator%(T a, FuncRefExprT<T> b) { return a % static_cast<T>(b); }

template <typename T>
Halide::Expr operator==(FuncRefExprT<T> a, T b) { return static_cast<T>(a) == b; }
template <typename T>
Halide::Expr operator!=(FuncRefExprT<T> a, T b) { return static_cast<T>(a) != b; }
template <typename T>
Halide::Expr operator<=(FuncRefExprT<T> a, T b) { return static_cast<T>(a) <= b; }
template <typename T>
Halide::Expr operator>=(FuncRefExprT<T> a, T b) { return static_cast<T>(a) >= b; }
template <typename T>
Halide::Expr operator<(FuncRefExprT<T> a, T b) { return static_cast<T>(a) < b; }
template <typename T>
Halide::Expr operator>(FuncRefExprT<T> a, T b) { return static_cast<T>(a) > b; }
template <typename T>
Halide::Expr operator==(T a, FuncRefExprT<T> b) { return a == static_cast<T>(b); }
template <typename T>
Halide::Expr operator!=(T a, FuncRefExprT<T> b) { return a != static_cast<T>(b); }
template <typename T>
Halide::Expr operator<=(T a, FuncRefExprT<T> b) { return a <= static_cast<T>(b); }
template <typename T>
Halide::Expr operator>=(T a, FuncRefExprT<T> b) { return a >= static_cast<T>(b); }
template <typename T>
Halide::Expr operator<(T a, FuncRefExprT<T> b) { return a < static_cast<T>(b); }
template <typename T>
Halide::Expr operator>(T a, FuncRefExprT<T> b) { return a > static_cast<T>(b); }

#endif
