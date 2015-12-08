#ifndef COMPLEX_H
#define COMPLEX_H

#include <string>

#include "funct.h"

// Complex number expression in Halide. This maps complex number Tuples
// to a type we can use for function overloading (especially operator
// overloading).
struct ComplexExpr {
    Halide::Expr x, y;

    explicit ComplexExpr(Halide::Tuple z) : x(z[0]), y(z[1]) {}

    // A default constructed complex number is zero.
    ComplexExpr() : x(0.0f), y(0.0f) {}
    ComplexExpr(float x, float y) : x(x), y(y) {}
    // This constructor will implicitly convert a real number (either
    // Halide::Expr or constant float) to a complex number with an
    // imaginary part of zero.
    ComplexExpr(Halide::Expr x, Halide::Expr y = 0.0f) : x(x), y(y) {}

    Halide::Expr re() { return x; }
    Halide::Expr im() { return y; }

    operator Halide::Tuple() const { return Halide::Tuple(x, y); }

    ComplexExpr &operator+=(ComplexExpr r) {
        x += r.x;
        y += r.y;
        return *this;
    }
};

// Define a typed Func for complex numbers.
typedef FuncT<ComplexExpr> ComplexFunc;

// Function style real/imaginary part of a complex number (and real
// numbers too).
inline Halide::Expr re(ComplexExpr z) { return z.re(); }
inline Halide::Expr im(ComplexExpr z) { return z.im(); }
inline Halide::Expr re(Halide::Expr x) { return x; }
inline Halide::Expr im(Halide::Expr x) { return 0.0f; }

inline ComplexExpr conj(ComplexExpr z) {
    return ComplexExpr(re(z), -im(z));
}

// Unary negation.
inline ComplexExpr operator-(ComplexExpr z) {
    return ComplexExpr(-re(z), -im(z));
}
// Complex arithmetic.
inline ComplexExpr operator+(ComplexExpr a, ComplexExpr b) {
    return ComplexExpr(re(a) + re(b), im(a) + im(b));
}
inline ComplexExpr operator+(ComplexExpr a, Halide::Expr b) {
    return ComplexExpr(re(a) + b, im(a));
}
inline ComplexExpr operator+(Halide::Expr a, ComplexExpr b) {
    return ComplexExpr(a + re(b), im(b));
}
inline ComplexExpr operator-(ComplexExpr a, ComplexExpr b) {
    return ComplexExpr(re(a) - re(b), im(a) - im(b));
}
inline ComplexExpr operator-(ComplexExpr a, Halide::Expr b) {
    return ComplexExpr(re(a) - b, im(a));
}
inline ComplexExpr operator-(Halide::Expr a, ComplexExpr b) {
    return ComplexExpr(a - re(b), -im(b));
}
inline ComplexExpr operator*(ComplexExpr a, ComplexExpr b) {
    return ComplexExpr(re(a) * re(b) - im(a) * im(b),
                       re(a) * im(b) + im(a) * re(b));
}
inline ComplexExpr operator*(ComplexExpr a, Halide::Expr b) {
    return ComplexExpr(re(a) * b, im(a) * b);
}
inline ComplexExpr operator*(Halide::Expr a, ComplexExpr b) {
    return ComplexExpr(a * re(b), a * im(b));
}
inline ComplexExpr operator/(ComplexExpr a, Halide::Expr b) {
    return ComplexExpr(re(a) / b, im(a) / b);
}

// Compute exp(j*x)
inline ComplexExpr expj(Halide::Expr x) {
    return ComplexExpr(Halide::cos(x), Halide::sin(x));
}

// Some helpers for doing basic Halide operations with complex numbers.
inline ComplexExpr sum(ComplexExpr z, const std::string &s = "sum") {
    return ComplexExpr(Halide::sum(re(z), s + "_re"),
                       Halide::sum(im(z), s + "_im"));
}
inline ComplexExpr select(Halide::Expr c, ComplexExpr t, ComplexExpr f) {
    return ComplexExpr(Halide::select(c, re(t), re(f)),
                       Halide::select(c, im(t), im(f)));
}
inline ComplexExpr select(Halide::Expr c1, ComplexExpr t1,
                                                    Halide::Expr c2, ComplexExpr t2,
                                                    ComplexExpr f) {
    return ComplexExpr(Halide::select(c1, re(t1), c2, re(t2), re(f)),
                       Halide::select(c1, im(t1), c2, im(t2), im(f)));
}
template <typename T>
inline ComplexExpr cast(ComplexExpr z) {
    return ComplexExpr(Halide::cast<T>(re(z)), Halide::cast<T>(im(z)));
}
inline ComplexExpr cast(Halide::Type type, ComplexExpr z) {
    return ComplexExpr(Halide::cast(type, re(z)), Halide::cast(type, im(z)));
}
inline ComplexExpr likely(ComplexExpr z) {
    return ComplexExpr(Halide::likely(re(z)), Halide::likely(im(z)));
}

#endif
