// This is a wrapper around Func, handling Complex values by adding an extra dimension of size 2.
// There is a similar wrapper class in the FFT example, that one stores Complex numbers as tuples.

#ifndef _HALIDE_COMPLEXFUNC_H
#define _HALIDE_COMPLEXFUNC_H

#include <Halide.h>
#include <vector>

namespace Halide {
namespace Tools {

class ComplexExpr;
/*
 * ComplexFunc wraps a Func in a way that intercepts index expressions and generates ComplexExprs for them.
 * A 2d ComplexFunc of size [i,j] will have an underlying (inner) 3d Func of size [2,i,j].
 * The complex axis is passed in explicitly, it should be consistent across all ComplexFuncs that participate in
 * complex mathematical expressions.
 */
class ComplexFunc {
public:
    Halide::Func inner;
    Halide::Var element;

    ComplexFunc(Halide::Var &element, std::string name = "");
    ComplexFunc(Halide::Var &element, Halide::Func &inner);
    ComplexExpr operator()(std::vector<Halide::Expr>);
    ComplexExpr operator()();
    ComplexExpr operator()(Halide::Expr idx1);
    ComplexExpr operator()(Halide::Expr idx1, Halide::Expr idx2);
    ComplexExpr operator()(Halide::Expr idx1, Halide::Expr idx2, Halide::Expr idx3);
};

/*
 * ComplexExpr represents a Complex value.  It uses operator overloading to
 * implement mathematical operations.  These act on either the complex number
 * as a whole or the real/imaginary elements separately, depending on the
 * operation.
 *
 * Some ComplexExprs represent a position in a ComplexFunc.  This allows it to
 * act as an lvalue and be assigned to.  All ComplexExprs can act as rvalues,
 * except for the ones representing ComplexFuncs which have never been assigned
 * to yet.
 */
class ComplexExpr {
public:
    Halide::Var element;
    Halide::Expr real;                   // index contains an explicit 0
    Halide::Expr imag;                   // index contains an explicit 1
    Halide::Expr pair;                   // this is a mux expression
    const ComplexFunc *func;             // Func that writes are passed through to
    std::vector<Halide::Expr> pair_idx;  // saved index for writes

    bool can_read;
    bool can_write;

    inline ComplexExpr(const ComplexFunc *func, const std::vector<Halide::Expr> &idx);               // lvalue constructor
    inline ComplexExpr(const Halide::Var &element, const Halide::Expr &v1, const Halide::Expr &v2);  // rvalue constructor

    // write ops
    ComplexExpr &operator=(ComplexExpr rvalue);
    ComplexExpr operator+=(ComplexExpr b);
    ComplexExpr &operator-=(const ComplexExpr &b);
    ComplexExpr &operator*=(const ComplexExpr &b);
    ComplexExpr &operator/=(const ComplexExpr &b);
    ComplexExpr &operator+=(const Halide::Expr &b);
    ComplexExpr &operator-=(const Halide::Expr &b);
    ComplexExpr &operator*=(const Halide::Expr &b);
    ComplexExpr &operator/=(const Halide::Expr &b);
};

/*
 * Create a ComplexExpr that represents an element of a ComplexFunc.  This
 * ComplexExpr can be assigned to as an lvalue.  If the underlying Func
 * is defined (by having been assigned to previously), this ComplexExpr
 * can also be used as an rvalue, or as an element in a larger mathematical
 * expression.
 */
inline ComplexExpr::ComplexExpr(const ComplexFunc *func, const std::vector<Halide::Expr> &idx)
    : func(func) {
    element = func->element;
    std::vector<Halide::Expr> real_idx({Halide::Expr(0)});
    std::vector<Halide::Expr> imag_idx({Halide::Expr(1)});
    pair_idx.reserve(idx.size() + 1);
    pair_idx.push_back(element);
    real_idx.reserve(idx.size() + 1);
    imag_idx.reserve(idx.size() + 1);
    copy(idx.begin(), idx.end(), back_inserter(real_idx));
    copy(idx.begin(), idx.end(), back_inserter(imag_idx));
    copy(idx.begin(), idx.end(), back_inserter(pair_idx));
    can_write = true;
    can_read = func->inner.defined();
    if (can_read) {
        real = func->inner(real_idx);
        imag = func->inner(imag_idx);
        pair = func->inner(pair_idx);
    }
}

/*
 * Create a ComplexExpr representing a read-only value.  This ComplexExpr has
 * no ComplexFunc, hence it cannot be assigned to.  It can be used as an
 * rvalue, or as an element in a larger mathematical expression.
 */
inline ComplexExpr::ComplexExpr(const Halide::Var &element, const Halide::Expr &v1, const Halide::Expr &v2) {
    real = v1;
    imag = v2;
    can_read = true;
    can_write = false;
    this->element = element;
    pair = Halide::mux(element, {v1, v2});
}

// negation
inline ComplexExpr operator-(const ComplexExpr &a) {
    if (a.can_read == false)
        throw;
    return ComplexExpr(a.element, -a.real, -a.imag);
}

// addition
inline ComplexExpr operator+(const ComplexExpr &a, const ComplexExpr &b) {
    if (a.can_read == false || b.can_read == false)
        throw;
    return ComplexExpr(a.element, a.pair + b.real, a.pair + b.imag);
}
inline ComplexExpr operator+(const ComplexExpr &a, const Halide::Expr &b) {
    if (a.can_read == false)
        throw;
    return ComplexExpr(a.element, a.real + b, a.imag);
}
inline ComplexExpr operator+(const Halide::Expr &b, const ComplexExpr &a) {
    return a + b;
}

// subtraction
inline ComplexExpr operator-(const ComplexExpr &a, const ComplexExpr &b) {
    if (a.can_read == false || b.can_read == false)
        throw;
    return ComplexExpr(a.element, a.pair - b.real, a.pair - b.imag);
}
inline ComplexExpr operator-(const ComplexExpr &a, const Halide::Expr &b) {
    if (a.can_read == false)
        throw;
    return ComplexExpr(a.element, a.real - b, a.imag);
}
inline ComplexExpr operator-(const Halide::Expr &b, const ComplexExpr &a) {
    return -a + b;
}

// multiplication
inline ComplexExpr operator*(const ComplexExpr &a, const ComplexExpr &b) {
    if (a.can_read == false || b.can_read == false)
        throw;
    return ComplexExpr(a.element, a.real * b.real - a.imag * b.imag, a.real * b.imag + a.imag * b.real);
}
inline ComplexExpr operator*(const ComplexExpr &a, const Halide::Expr &b) {
    if (a.can_read == false)
        throw;
    return ComplexExpr(a.element, a.real * b, a.imag * b);
}
inline ComplexExpr operator*(const Halide::Expr &b, const ComplexExpr &a) {
    return a * b;
}

// conjugation
inline ComplexExpr conj(const ComplexExpr &z) {
    return ComplexExpr(z.element, z.real, -z.imag);
}

// division
inline ComplexExpr operator/(const ComplexExpr &a, const ComplexExpr &b) {
    if (a.can_read == false || b.can_read == false)
        throw;
    ComplexExpr conjugate = conj(b);
    ComplexExpr numerator = a * conjugate;
    ComplexExpr denominator = b * conjugate;
    return ComplexExpr(a.element, numerator.real / denominator.real, numerator.imag / denominator.real);
}
inline ComplexExpr operator/(const ComplexExpr &a, const Halide::Expr &b) {
    if (a.can_read == false)
        throw;
    return ComplexExpr(a.element, a.real / b, a.imag / b);
}
inline ComplexExpr operator/(const Halide::Expr &b, const ComplexExpr &a) {
    ComplexExpr numerator = b * conj(a);
    ComplexExpr denominator = a * conj(a);
    return ComplexExpr(a.element, numerator.real / denominator.real, numerator.imag / denominator.real);
}

// exponential
inline ComplexExpr exp(const ComplexExpr &z) {
    return ComplexExpr(z.element, Halide::exp(z.real) * Halide::cos(z.imag), Halide::exp(z.real) * Halide::sin(z.imag));
}
inline ComplexExpr expj(const Halide::Var &element, const Halide::Expr &x) {
    return ComplexExpr(element, Halide::cos(x), Halide::sin(x));
}

// assignment
ComplexExpr &ComplexExpr::operator=(ComplexExpr rvalue) {
    if (rvalue.can_read == false)
        throw;
    Halide::FuncRef funcref = func->inner(pair_idx);
    funcref = rvalue.pair;
    pair = rvalue.pair;
    real = rvalue.real;
    imag = rvalue.imag;
    can_read = true;
    return *this;
}

// updates
ComplexExpr ComplexExpr::operator+=(ComplexExpr b) {
    if (can_read == false || b.can_read == false)
        throw;
    ComplexExpr rvalue = *this + b;
    Halide::FuncRef funcref = func->inner(pair_idx);
    funcref = rvalue.pair;
    return *this;
}
ComplexExpr &ComplexExpr::operator+=(const Halide::Expr &b) {
    if (can_read == false)
        throw;
    ComplexExpr rvalue = *this + b;
    Halide::FuncRef funcref = func->inner(pair_idx);
    funcref = rvalue.pair;
    return *this;
}
ComplexExpr &ComplexExpr::operator-=(const ComplexExpr &b) {
    if (can_read == false || b.can_read == false)
        throw;
    ComplexExpr rvalue = *this - b;
    Halide::FuncRef funcref = func->inner(pair_idx);
    funcref = rvalue.pair;
    return *this;
}
ComplexExpr &ComplexExpr::operator-=(const Halide::Expr &b) {
    if (can_read == false)
        throw;
    ComplexExpr rvalue = *this - b;
    Halide::FuncRef funcref = func->inner(pair_idx);
    funcref = rvalue.pair;
    return *this;
}
ComplexExpr &ComplexExpr::operator*=(const ComplexExpr &b) {
    if (can_read == false || b.can_read == false)
        throw;
    Halide::FuncRef funcref = func->inner(pair_idx);
    ComplexExpr rvalue = *this * b;
    funcref = rvalue.pair;
    return *this;
}
ComplexExpr &ComplexExpr::operator*=(const Halide::Expr &b) {
    if (can_read == false)
        throw;
    Halide::FuncRef funcref = func->inner(pair_idx);
    funcref *= select(func->element, b, Halide::Expr(1.0));
    return *this;
}
ComplexExpr &ComplexExpr::operator/=(const ComplexExpr &b) {
    if (can_read == false || b.can_read == false)
        throw;
    Halide::FuncRef funcref = func->inner(pair_idx);
    funcref /= b.pair;
    return *this;
}
ComplexExpr &ComplexExpr::operator/=(const Halide::Expr &b) {
    if (can_read == false)
        throw;
    Halide::FuncRef funcref = func->inner(pair_idx);
    funcref /= select(func->element, b, Halide::Expr(1.0));
    return *this;
}

// other helper functions

// stringification
inline std::ostream &operator<<(std::ostream &os, const ComplexExpr &a) {
    os << "<ComplexExpr " << a.real << ", " << a.imag << ">";
    return os;
}

// absolute value
inline Halide::Expr abs(ComplexExpr a) {
    return sqrt(a.real * a.real + a.imag * a.imag);
}

// summation
inline ComplexExpr sum(const ComplexExpr &z, const std::string &s = "sum") {
    return ComplexExpr(z.element,
                       Halide::sum(z.real, s + "_real"),
                       Halide::sum(z.imag, s + "_imag"));
}
// selection
inline ComplexExpr select(const Halide::Var &element, Halide::Expr c, ComplexExpr t, ComplexExpr f) {
    return ComplexExpr(element,
                       Halide::select(c, t.real, f.real),
                       Halide::select(c, t.imag, f.imag));
}

inline ComplexExpr select(const Halide::Var &element,
                          Halide::Expr c1, ComplexExpr t1,
                          Halide::Expr c2, ComplexExpr t2,
                          ComplexExpr f) {
    return ComplexExpr(element,
                       Halide::select(c1, t1.real, c2, t2.real, f.real),
                       Halide::select(c1, t1.imag, c2, t2.imag, f.imag));
}

// ComplexFunc methods

ComplexFunc::ComplexFunc(Halide::Var &element, std::string name)
    : element(element) {
    if (name == "") {
        inner = Halide::Func();
    } else {
        inner = Halide::Func(name);
    }
}

ComplexFunc::ComplexFunc(Halide::Var &element, Halide::Func &inner)
    : inner(inner), element(element) {
}

ComplexExpr ComplexFunc::operator()(std::vector<Halide::Expr> idx) {
    return ComplexExpr(this, idx);
}

ComplexExpr ComplexFunc::operator()() {
    std::vector<Halide::Expr> idx({});
    return (*this)(idx);
}
ComplexExpr ComplexFunc::operator()(Halide::Expr idx1) {
    std::vector<Halide::Expr> idx({idx1});
    return (*this)(idx);
}
ComplexExpr ComplexFunc::operator()(Halide::Expr idx1, Halide::Expr idx2) {
    std::vector<Halide::Expr> idx({idx1, idx2});
    return (*this)(idx);
}
ComplexExpr ComplexFunc::operator()(Halide::Expr idx1, Halide::Expr idx2, Halide::Expr idx3) {
    std::vector<Halide::Expr> idx({idx1, idx2, idx3});
    return (*this)(idx);
}

}  // namespace Tools
}  // namespace Halide

#endif /* _HALIDE_COMPLEXFUNC_H */
