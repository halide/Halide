#include "Halide.h"

using namespace Halide;

namespace {

class Complex {
    Tuple t;

public:
    Complex(Expr real, Expr imag)
        : t(real, imag) {
    }
    Complex(Tuple tup)
        : t(tup) {
    }
    Complex(FuncRef f)
        : t(Tuple(f)) {
    }
    Expr real() const {
        return t[0];
    }
    Expr imag() const {
        return t[1];
    }

    operator Tuple() const {
        return t;
    }
};

// Define the usual complex arithmetic
Complex operator+(const Complex &a, const Complex &b) {
    return Complex(a.real() + b.real(), a.imag() + b.imag());
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

class Mandelbrot : public Generator<Mandelbrot> {
public:
    Input<float> x_min{"x_min"};
    Input<float> x_max{"x_max"};
    Input<float> y_min{"y_min"};
    Input<float> y_max{"y_max"};
    Input<float> c_real{"c_real"};
    Input<float> c_imag{"c_imag"};
    Input<int> iters{"iters"};
    Input<int> w{"w"};
    Input<int> h{"h"};
    Output<Buffer<int32_t, 2>> count{"count"};

    void generate() {
        assert(get_target().has_feature(Target::Profile));

        Var x, y, z;

        Complex initial(lerp(x_min, x_max, cast<float>(x) / w),
                        lerp(y_min, y_max, cast<float>(y) / h));
        Complex c(c_real, c_imag);

        Func mandelbrot;
        mandelbrot(x, y, z) = initial;
        RDom t(1, iters);
        Complex current = mandelbrot(x, y, t - 1);
        mandelbrot(x, y, t) = current * current + c;

        // How many iterations until something escapes a circle of radius 2?
        Tuple escape = argmin(magnitude(mandelbrot(x, y, t)) < 4);

        // If it never escapes, use the value 0
        count(x, y) = select(escape[1], 0, escape[0]);

        Var xi, yi, xo, yo;
        mandelbrot.compute_at(count, xo);

        count.tile(x, y, xo, yo, xi, yi, 8, 8).parallel(yo).vectorize(xi, 4).unroll(xi).unroll(yi, 2);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Mandelbrot, memory_profiler_mandelbrot)
