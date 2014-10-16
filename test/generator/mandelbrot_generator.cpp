#include "Halide.h"

using namespace Halide;

namespace {

class Complex {
    Tuple t;

public:
    Complex(Expr real, Expr imag) : t(real, imag) {}
    Complex(Tuple tup) : t(tup) {}
    Complex(FuncRefExpr f) : t(Tuple(f)) {}
    Complex(FuncRefVar f) : t(Tuple(f)) {}
    Expr real() const { return t[0]; }
    Expr imag() const { return t[1]; }

    operator Tuple() const { return t; }
};

// Define the usual complex arithmetic
Complex operator+(const Complex &a, const Complex &b) {
    return Complex(a.real() + b.real(), a.imag() + b.imag());
}

Complex operator-(const Complex &a, const Complex &b) {
    return Complex(a.real() - b.real(), a.imag() - b.imag());
}

Complex operator*(const Complex &a, const Complex &b) {
    return Complex(a.real() * b.real() - a.imag() * b.imag(),
                   a.real() * b.imag() + a.imag() * b.real());
}

Complex conjugate(const Complex &a) { return Complex(a.real(), -a.imag()); }

Expr magnitude(Complex a) { return (a * conjugate(a)).real(); }

class Mandelbrot : public Generator<Mandelbrot> {
public:
    Param<float> x_min{"x_min"};
    Param<float> x_max{"x_max"};
    Param<float> y_min{"y_min"};
    Param<float> y_max{"y_max"};
    Param<float> c_real{"c_real"};
    Param<float> c_imag{"c_imag"};
    Param<int> iters{"iters"};
    Param<int> w{"w"};
    Param<int> h{"h"};

    static std::string name() {
        return "mandelbrot";
    }

    Func build() override {
        Func mandelbrot;
        Var x, y, z;

        Complex initial(lerp(x_min, x_max, cast<float>(x) / w),
                        lerp(y_min, y_max, cast<float>(y) / h));
        Complex c(c_real, c_imag);

        mandelbrot(x, y, z) = initial;
        RDom t(1, iters);
        Complex current = mandelbrot(x, y, t - 1);
        mandelbrot(x, y, t) = current * current + c;

        // How many iterations until something escapes a circle of radius 2?
        Func count;
        Tuple escape = argmin(magnitude(mandelbrot(x, y, t)) < 4);

        // If it never escapes, use the value 0
        count(x, y) = select(escape[1], 0, escape[0]);

        Var xi, yi, xo, yo;
        mandelbrot.compute_at(count, xo);

        count.tile(x, y, xo, yo, xi, yi, 8, 8).parallel(yo).vectorize(xi, 4).unroll(xi).unroll(yi,
                                                                                               2);

        return count;
    }
};

RegisterGenerator<Mandelbrot> register_my_gen;

}  // namespace
