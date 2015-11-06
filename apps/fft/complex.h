#ifndef COMPLEX_H
#define COMPLEX_H

#include <string>

#include "funct.h"

// Complex number expression in Halide. This maps complex number Tuples
// to a type we can use for function overloading (especially operator
// overloading).
struct ComplexExpr {
 public:
  Halide::Expr x, y;

  explicit ComplexExpr(Halide::Tuple z) : x(z[0]), y(z[1]) {}

  // A default constructed complex number is zero.
  ComplexExpr() : x(0.0f), y(0.0f) {}
  ComplexExpr(float x, float y) : x(x), y(y) {}
  // This constructor will implicitly convert a real number (either
  // Halide::Expr or constant float) to a complex number with an
  // imaginary part of zero.
  ComplexExpr(Halide::Expr x, Halide::Expr y = 0.0f) : x(x), y(y) {}

  Halide::Expr Re() { return x; }
  Halide::Expr Im() { return y; }

  operator Halide::Tuple() const { return Halide::Tuple(x, y); }

  ComplexExpr &operator+=(ComplexExpr r) {
    x += r.x;
    y += r.y;
    return *this;
  }
};

// Define a typed Func for complex numbers.
typedef FuncT<ComplexExpr> ComplexFunc;
typedef FuncRefExprT<ComplexExpr> ComplexFuncRefExpr;

// Function style real/imaginary part of a complex number (and real
// numbers too).
inline Halide::Expr Re(ComplexExpr z) { return z.Re(); }
inline Halide::Expr Im(ComplexExpr z) { return z.Im(); }
inline Halide::Expr Re(Halide::Expr x) { return x; }
inline Halide::Expr Im(Halide::Expr x) { return 0.0f; }

inline ComplexExpr Conjugate(ComplexExpr z) {
  return ComplexExpr(Re(z), -Im(z));
}

// Unary negation.
inline ComplexExpr operator-(ComplexExpr z) {
  return ComplexExpr(-Re(z), -Im(z));
}
// Complex arithmetic.
inline ComplexExpr operator+(ComplexExpr a, ComplexExpr b) {
  return ComplexExpr(Re(a) + Re(b), Im(a) + Im(b));
}
inline ComplexExpr operator+(ComplexExpr a, Halide::Expr b) {
  return ComplexExpr(Re(a) + b, Im(a));
}
inline ComplexExpr operator+(Halide::Expr a, ComplexExpr b) {
  return ComplexExpr(a + Re(b), Im(b));
}
inline ComplexExpr operator-(ComplexExpr a, ComplexExpr b) {
  return ComplexExpr(Re(a) - Re(b), Im(a) - Im(b));
}
inline ComplexExpr operator-(ComplexExpr a, Halide::Expr b) {
  return ComplexExpr(Re(a) - b, Im(a));
}
inline ComplexExpr operator-(Halide::Expr a, ComplexExpr b) {
  return ComplexExpr(a - Re(b), -Im(b));
}
inline ComplexExpr operator*(ComplexExpr a, ComplexExpr b) {
  return ComplexExpr(Re(a) * Re(b) - Im(a) * Im(b),
                     Re(a) * Im(b) + Im(a) * Re(b));
}
inline ComplexExpr operator*(ComplexExpr a, Halide::Expr b) {
  return ComplexExpr(Re(a) * b, Im(a) * b);
}
inline ComplexExpr operator*(Halide::Expr a, ComplexExpr b) {
  return ComplexExpr(a * Re(b), a * Im(b));
}
inline ComplexExpr operator/(ComplexExpr a, Halide::Expr b) {
  return ComplexExpr(Re(a) / b, Im(a) / b);
}

// Compute exp(j*x)
inline ComplexExpr Expj(Halide::Expr x) {
  return ComplexExpr(Halide::cos(x), Halide::sin(x));
}

// Compute |z|^2.
inline Halide::Expr AbsSq(ComplexExpr z) {
  return Re(z) * Re(z) + Im(z) * Im(z);
}

// Some helpers for doing basic Halide operations with complex numbers.
inline ComplexExpr sum(ComplexExpr z, const std::string &s = "sum") {
  return ComplexExpr(Halide::sum(Re(z), s + "_re"),
                     Halide::sum(Im(z), s + "_im"));
}
inline ComplexExpr select(Halide::Expr c, ComplexExpr t, ComplexExpr f) {
  return ComplexExpr(Halide::select(c, Re(t), Re(f)),
                     Halide::select(c, Im(t), Im(f)));
}
inline ComplexExpr select(Halide::Expr c1, ComplexExpr t1,
                          Halide::Expr c2, ComplexExpr t2,
                          ComplexExpr f) {
  return ComplexExpr(Halide::select(c1, Re(t1), c2, Re(t2), Re(f)),
                     Halide::select(c1, Im(t1), c2, Im(t2), Im(f)));
}
template <typename T>
inline ComplexExpr Cast(ComplexExpr z) {
  return ComplexExpr(Halide::cast<T>(Re(z)), Halide::cast<T>(Im(z)));
}
inline ComplexExpr Cast(Halide::Type type, ComplexExpr z) {
  return ComplexExpr(Halide::cast(type, Re(z)), Halide::cast(type, Im(z)));
}
inline ComplexExpr likely(ComplexExpr z) {
  return ComplexExpr(Halide::likely(Re(z)), Halide::likely(Im(z)));
}

#endif
