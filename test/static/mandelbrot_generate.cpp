#include <Halide.h>
#include <stdio.h>

using namespace Halide;

// Make a complex number type using Tuples
class Complex {
    Tuple t;
public:
    Complex(Expr real, Expr imag) : t(real, imag) {}
    Complex(Tuple tup) : t(tup) {}
    Complex(FuncRefExpr f) : t(Tuple(f)) {}
    Complex(FuncRefVar f) : t(Tuple(f)) {}
    Expr real() const {return t[0];}
    Expr imag() const {return t[1];}

    operator Tuple() const {return t;}
};

// Define the usual complex arithmetic
Complex operator+(const Complex &a, const Complex &b) {
    return Complex(a.real() + b.real(),
                   a.imag() + b.imag());
}

Complex operator-(const Complex &a, const Complex &b) {
    return Complex(a.real() - b.real(),
                   a.imag() - b.imag());
}

Complex operator*(const Complex &a, const Complex &b) {
    return Complex(a.real() * b.real() - a.imag() * b.imag(),
                   a.real() * b.imag() + a.imag() * b.real());
}

Complex conjugate(const Complex &a) {
    return Complex(a.real(), -a.imag());
}

Expr magnitude(Complex a) {
    return (a * conjugate(a)).real();
}


int main(int argc, char **argv) {
    Func mandelbrot;
    Var x, y;

    Param<float> x_min, x_max, y_min, y_max, c_real, c_imag;
    Param<int> w, h, iters;
    Complex initial(lerp(x_min, x_max, cast<float>(x)/w),
                    lerp(y_min, y_max, cast<float>(y)/h));
    Complex c(c_real, c_imag);

    Var z;
    mandelbrot(x, y, z) = initial;
    RDom t(1, iters);
    Complex current = mandelbrot(x, y, t-1);
    mandelbrot(x, y, t) = current*current + c;

    // How many iterations until something escapes a circle of radius 2?
    Func count;
    Tuple escape = argmin(magnitude(mandelbrot(x, y, t)) < 4);

    // If it never escapes, use the value 0
    count(x, y) = select(escape[1], 0, escape[0]);

    Var xi, yi, xo, yo;
    count.tile(x, y, xo, yo, xi, yi, 8, 8);
    count.parallel(yo).vectorize(xi, 4).unroll(xi).unroll(yi, 2);
    mandelbrot.compute_at(count, xo);

    Argument args[] = {x_min, x_max, y_min, y_max, c_real, c_imag, iters, w, h};

    count.compile_to_file("mandelbrot", std::vector<Argument>(args, args + 9));

    return 0;
}
