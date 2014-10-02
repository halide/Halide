#ifndef HALIDE_FUNC_T_H
#define HALIDE_FUNC_T_H

/** \file
 *
 * Defines FuncT<T>, A Func that returns T instead of Expr/Tuple.
 */

#include "Func.h"

namespace Halide {

/** A typed version of FuncRefVar. */
template <typename T>
class FuncRefVarT : public T {
    FuncRefVar untyped;

public:
    FuncRefVarT(FuncRefVar untyped) : T(static_cast<Tuple>(untyped)), untyped(untyped) {}

    /* See FuncRefExpr::operator =. Note that unlike basic Funcs,
     * the update definitions do not implicitly define a base case. */
    // @{
    Stage operator=(T x) { return untyped = x; }
    Stage operator+=(T x) { return untyped = untyped + x; }
    Stage operator-=(T x) { return untyped = untyped - x;}
    Stage operator*=(T x) { return untyped = untyped * x; }
    Stage operator/=(T x) { return untyped = untyped / x; }
    // @}
};

/** A typed version of FuncRefExpr. T should be implicitly convertible
 * to/from Tuple. */
template <typename T>
class FuncRefExprT : public T {
    FuncRefExpr untyped;

public:
    FuncRefExprT(FuncRefExpr untyped) : T(static_cast<Tuple>(untyped)), untyped(untyped) {}

    /* See FuncRefExpr::operator =. Note that unlike basic Funcs,
     * the update definitions do not implicitly define a base case. */
    // @{
    Stage operator=(T x) { return untyped = x; }
    Stage operator+=(T x) { return untyped = untyped + x; }
    Stage operator-=(T x) { return untyped = untyped - x;}
    Stage operator*=(T x) { return untyped = untyped * x; }
    Stage operator/=(T x) { return untyped = untyped / x; }
    // @}
};

/** A Func that returns a type T. T should be implicitly convertible
 * to/from Tuple. */
template <typename T>
class FuncT : public Func {
public:
    /** See Func::Func. */
    // @{
    explicit FuncT(const std::string &name) : Func(name) {}
    FuncT() {}
    explicit FuncT(Expr e) : Func(e) {}
    explicit FuncT(Internal::Function f) : Func(f) {}

    /** See Func::operator(). */
    // @{
    FuncRefVarT<T> operator()() const { return Func::operator()(); }
    FuncRefVarT<T> operator()(Var x) const{ return Func::operator()(x); }
    FuncRefVarT<T> operator()(Var x, Var y) const { return Func::operator()(x, y); }
    FuncRefVarT<T> operator()(Var x, Var y, Var z) const { return Func::operator()(x, y, z); }
    FuncRefVarT<T> operator()(Var x, Var y, Var z, Var w) const { return Func::operator()(x, y, z, w); }
    FuncRefVarT<T> operator()(Var x, Var y, Var z, Var w, Var u) const{ return Func::operator()(x, y, z, w, u); }
    FuncRefVarT<T> operator()(Var x, Var y, Var z, Var w, Var u, Var v) const { return Func::operator()(x, y, z, w, u, v); }
    FuncRefVarT<T> operator()(std::vector<Var> vars) const { return Func::operator()(vars); }
    // @}

    /** See Func::operator(). */
    // @{
    FuncRefExprT<T> operator()(Expr x) const{ return Func::operator()(x); }
    FuncRefExprT<T> operator()(Expr x, Expr y) const { return Func::operator()(x, y); }
    FuncRefExprT<T> operator()(Expr x, Expr y, Expr z) const { return Func::operator()(x, y, z); }
    FuncRefExprT<T> operator()(Expr x, Expr y, Expr z, Expr w) const { return Func::operator()(x, y, z, w); }
    FuncRefExprT<T> operator()(Expr x, Expr y, Expr z, Expr w, Expr u) const{ return Func::operator()(x, y, z, w, u); }
    FuncRefExprT<T> operator()(Expr x, Expr y, Expr z, Expr w, Expr u, Expr v) const { return Func::operator()(x, y, z, w, u, v); }
    FuncRefExprT<T> operator()(std::vector<Expr> vars) const { return Func::operator()(vars); }
    // @}
};

// Forward operator overload invocations on FuncRefVarT/FuncRefExprT to
// the type the user intended (T).

// TODO: This is obscene. Find a better way... but it is unlikely
// there is one.
 template <typename T>
T operator - (FuncRefVarT<T> x) { return -static_cast<T>(x); }
template <typename T>
T operator ~ (FuncRefVarT<T> x) { return ~static_cast<T>(x); }

template <typename T>
T operator + (FuncRefVarT<T> a, T b) { return static_cast<T>(a) + b; }
template <typename T>
T operator - (FuncRefVarT<T> a, T b) { return static_cast<T>(a) - b; }
template <typename T>
T operator * (FuncRefVarT<T> a, T b) { return static_cast<T>(a) * b; }
template <typename T>
T operator / (FuncRefVarT<T> a, T b) { return static_cast<T>(a) / b; }
template <typename T>
T operator % (FuncRefVarT<T> a, T b) { return static_cast<T>(a) % b; }
template <typename T>
T operator + (T a, FuncRefVarT<T> b) { return a + static_cast<T>(b); }
template <typename T>
T operator - (T a, FuncRefVarT<T> b) { return a - static_cast<T>(b); }
template <typename T>
T operator * (T a, FuncRefVarT<T> b) { return a * static_cast<T>(b); }
template <typename T>
T operator / (T a, FuncRefVarT<T> b) { return a / static_cast<T>(b); }
template <typename T>
T operator % (T a, FuncRefVarT<T> b) { return a % static_cast<T>(b); }

template <typename T>
Expr operator == (FuncRefVarT<T> a, T b) { return static_cast<T>(a) == b; }
template <typename T>
Expr operator != (FuncRefVarT<T> a, T b) { return static_cast<T>(a) != b; }
template <typename T>
Expr operator <= (FuncRefVarT<T> a, T b) { return static_cast<T>(a) <= b; }
template <typename T>
Expr operator >= (FuncRefVarT<T> a, T b) { return static_cast<T>(a) >= b; }
template <typename T>
Expr operator < (FuncRefVarT<T> a, T b) { return static_cast<T>(a) < b; }
template <typename T>
Expr operator > (FuncRefVarT<T> a, T b) { return static_cast<T>(a) > b; }
template <typename T>
Expr operator == (T a, FuncRefVarT<T> b) { return a == static_cast<T>(b); }
template <typename T>
Expr operator != (T a, FuncRefVarT<T> b) { return a != static_cast<T>(b); }
template <typename T>
Expr operator <= (T a, FuncRefVarT<T> b) { return a <= static_cast<T>(b); }
template <typename T>
Expr operator >= (T a, FuncRefVarT<T> b) { return a >= static_cast<T>(b); }
template <typename T>
Expr operator < (T a, FuncRefVarT<T> b) { return a < static_cast<T>(b); }
template <typename T>
Expr operator > (T a, FuncRefVarT<T> b) { return a > static_cast<T>(b); }

template <typename T>
T operator - (FuncRefExprT<T> x) { return -static_cast<T>(x); }
template <typename T>
T operator ~ (FuncRefExprT<T> x) { return ~static_cast<T>(x); }

template <typename T>
T operator + (FuncRefExprT<T> a, T b) { return static_cast<T>(a) + b; }
template <typename T>
T operator - (FuncRefExprT<T> a, T b) { return static_cast<T>(a) - b; }
template <typename T>
T operator * (FuncRefExprT<T> a, T b) { return static_cast<T>(a) * b; }
template <typename T>
T operator / (FuncRefExprT<T> a, T b) { return static_cast<T>(a) / b; }
template <typename T>
T operator % (FuncRefExprT<T> a, T b) { return static_cast<T>(a) % b; }
template <typename T>
T operator + (T a, FuncRefExprT<T> b) { return a + static_cast<T>(b); }
template <typename T>
T operator - (T a, FuncRefExprT<T> b) { return a - static_cast<T>(b); }
template <typename T>
T operator * (T a, FuncRefExprT<T> b) { return a * static_cast<T>(b); }
template <typename T>
T operator / (T a, FuncRefExprT<T> b) { return a / static_cast<T>(b); }
template <typename T>
T operator % (T a, FuncRefExprT<T> b) { return a % static_cast<T>(b); }

template <typename T>
Expr operator == (FuncRefExprT<T> a, T b) { return static_cast<T>(a) == b; }
template <typename T>
Expr operator != (FuncRefExprT<T> a, T b) { return static_cast<T>(a) != b; }
template <typename T>
Expr operator <= (FuncRefExprT<T> a, T b) { return static_cast<T>(a) <= b; }
template <typename T>
Expr operator >= (FuncRefExprT<T> a, T b) { return static_cast<T>(a) >= b; }
template <typename T>
Expr operator < (FuncRefExprT<T> a, T b) { return static_cast<T>(a) < b; }
template <typename T>
Expr operator > (FuncRefExprT<T> a, T b) { return static_cast<T>(a) > b; }
template <typename T>
Expr operator == (T a, FuncRefExprT<T> b) { return a == static_cast<T>(b); }
template <typename T>
Expr operator != (T a, FuncRefExprT<T> b) { return a != static_cast<T>(b); }
template <typename T>
Expr operator <= (T a, FuncRefExprT<T> b) { return a <= static_cast<T>(b); }
template <typename T>
Expr operator >= (T a, FuncRefExprT<T> b) { return a >= static_cast<T>(b); }
template <typename T>
Expr operator < (T a, FuncRefExprT<T> b) { return a < static_cast<T>(b); }
template <typename T>
Expr operator > (T a, FuncRefExprT<T> b) { return a > static_cast<T>(b); }

}

#endif
