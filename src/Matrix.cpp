#include "Matrix.h"

namespace Halide {

Matrix::Matrix() : is_large(false), nrows(0), ncols(0) {}

Matrix::Matrix(Expr m, Expr n, Type t) : is_large(true), nrows(m), ncols(n) {
    internal_assert(nrows.type().is_int() || nrows.type().is_uint());
    internal_assert(ncols.type().is_int() || ncols.type().is_uint());

    if (Internal::is_const(nrows) && Internal::is_const(ncols)) {
        const int* nr = Internal::as_const_int(nrows);
        const int* nc = Internal::as_const_int(ncols);

        if (0 < *nr && 0 < *nc && (*nr) * (*nc) < 16) {
            is_large = false;
            coeffs.resize((*nr) * (*nc), Halide::undef(t));
            return;
        }
    }

    x = Var("x");
    y = Var("y");

    func(x, y) = Halide::undef(t);
    func.bound(x, 0, nrows)
        .bound(y, 0, ncols);
}

Matrix::Matrix(Expr m, Expr n, Func f) : is_large(true), nrows(m), ncols(n) {
    internal_assert(nrows.type().is_int() || nrows.type().is_uint());
    internal_assert(ncols.type().is_int() || ncols.type().is_uint());
    internal_assert(f.dimensions() == 2);

    if (Internal::is_const(nrows) && Internal::is_const(ncols)) {
        const int nr = *Internal::as_const_int(nrows);
        const int nc = *Internal::as_const_int(ncols);

        if (0 < nr && 0 < nc && nr*nc < 16) {
            is_large = false;
            coeffs.resize(nr*nc);

            for (int j = 0; j < nc; ++j) {
                for (int i = 0; i < nr; ++i) {
                    coeffs[i + j*nr] = f(i, j);
                }
            }

            return;
        }
    }

    x = f.args()[0];
    y = f.args()[1];
    func = f;
    func.bound(x, 0, nrows)
        .bound(y, 0, ncols);
}

}
