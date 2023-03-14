#include "Halide.h"
#include <math.h>
#include <stdio.h>

using namespace Halide;

// NB: You must compile with -rdynamic for llvm to be able to find the
// appropriate symbols

// Many things that are difficult to do with Halide can be hacked in
// using reductions that call extern C functions. In general this is a
// bad way to do things, because you've tied yourself to C, which
// means no GPU.  Additionally, if your reduction has pure dimensions,
// you need to take care to make your extern functions
// thread-safe.

// Here we use an extern call to print an ascii-art Mandelbrot set.
int call_count = 0;
extern "C" HALIDE_EXPORT_SYMBOL int draw_pixel(int x, int y, int val) {
    call_count++;
    static int last_y = 0;
    if (y != last_y) {
        printf("\n");
        last_y = y;
    }

    const char *code = " .:-~*={}&%#@";

    if (val >= static_cast<int>(strlen(code))) {
        val = static_cast<int>(strlen(code)) - 1;
    }
    printf("%c", code[val]);
    return 0;
}
HalideExtern_3(int, draw_pixel, int, int, int);

// Make a complex number type using Tuples
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
    Var x, y;

    Func mandelbrot;
    // Use a different scale on x and y because terminal characters
    // are not square. Arbitrarily chosen to fit the set nicely.
    Complex initial(x / 20.0f, y / 8.0f);
    Var z;
    mandelbrot(x, y, z) = Complex(0.0f, 0.0f);
    RDom t(1, 40);
    Complex current = mandelbrot(x, y, t - 1);
    mandelbrot(x, y, t) = current * current + initial;

    // How many iterations until something escapes a circle of radius 2?
    Func count;
    Tuple escape = argmin(magnitude(mandelbrot(x, y, t)) < 4);
    // If it never escapes, use the value 0
    count(x, y) = select(escape[1], 0, escape[0]);

    RDom r(-45, 71, -10, 21);
    Func render;
    render() += draw_pixel(r.x, r.y, count(r.x, r.y));

    mandelbrot.compute_at(render, r.x);

    render.realize();

    printf("\n");

    // Check draw_pixel was called the right number of times.
    if (call_count != 71 * 21) {
        printf("Something went wrong\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
