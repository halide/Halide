#include "Halide.h"
#include <stdio.h>

using namespace Halide;

Expr sum3x3(Func f, Var x, Var y) {
    return f(x-1, y-1) + f(x-1, y) + f(x-1, y+1) +
           f(x, y-1)   + f(x, y)   + f(x, y+1) +
           f(x+1, y-1) + f(x+1, y) + f(x+1, y+1);
}

int main(int argc, char **argv) {

    ImageParam in(Float(32), 3);

    Func in_bounded = BoundaryConditions::repeat_edge(in);

    Var x, y, c;

    Func gray("gray");
    gray(x, y) = 0.299f * in_bounded(x, y, 0) + 0.587f * in_bounded(x, y, 1)
                 + 0.114f * in_bounded(x, y, 2);


    Func Iy("Iy");
    Iy(x, y) = gray(x-1, y-1)*(-1.0f/12) + gray(x-1, y+1)*(1.0f/12) +
               gray(x, y-1)*(-2.0f/12) + gray(x, y+1)*(2.0f/12) +
               gray(x+1, y-1)*(-1.0f/12) + gray(x+1, y+1)*(1.0f/12);


    Func Ix("Ix");
    Ix(x, y) = gray(x-1, y-1)*(-1.0f/12) + gray(x+1, y-1)*(1.0f/12) +
               gray(x-1, y)*(-2.0f/12) + gray(x+1, y)*(2.0f/12) +
               gray(x-1, y+1)*(-1.0f/12) + gray(x+1, y+1)*(1.0f/12);


    Func Ixx("Ixx");
    Ixx(x, y) = Ix(x, y) * Ix(x, y);

    Func Iyy("Iyy");
    Iyy(x, y) = Iy(x, y) * Iy(x, y);

    Func Ixy("Ixy");
    Ixy(x, y) = Ix(x, y) * Iy(x, y);

    Func Sxx("Sxx");
    Sxx(x, y) = sum3x3(Ixx, x, y);

    Func Syy("Syy");
    Syy(x, y) = sum3x3(Iyy, x, y);


    Func Sxy("Sxy");
    Sxy(x, y) = sum3x3(Ixy, x, y);

    Func det("det");
    det(x, y) = Sxx(x, y) * Syy(x, y) - Sxy(x, y) * Sxy(x, y);

    Func trace("trace");
    trace(x, y) = Sxx(x, y) + Syy(x, y);

    Func harris("harris");
    harris(x, y) = det(x, y) - 0.04f * trace(x, y) * trace(x, y);

    Target target = get_target_from_environment();
    Var yi, xi;
    harris.split(x, x, xi, 128).split(y, y, yi, 128).
        reorder(xi, yi, x, y).vectorize(xi, 8).parallel(y);
    Ix.compute_at(harris, x).vectorize(x, 8);
    Iy.compute_at(harris, x).vectorize(x, 8);
    Sxx.compute_at(harris, x).vectorize(x, 8);
    Syy.compute_at(harris, x).vectorize(x, 8);
    Sxy.compute_at(harris, x).vectorize(x, 8);

    harris.compile_to_file("harris", {in}, target);

    return 0;
}
