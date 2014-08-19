#include "Matrix.h"

namespace Halide {

bool is_int(Expr i) {
    return i.type().is_int() || i.type().is_uint());
}

bool is_positive_int(Expr i) {
    return Internal::is_positive_const(i) && is_int(i);
}

MatrixRef::MatrixRef(Matrix M, Expr i, Expr j) : mat(M), row(i), col(j) {
    internal_assert(row.defined() && is_int(row));
    internal_assert(col.defined() && is_int(col));
}

void MatrixRef::operator=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) = x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = expr;
    }
}

void MatrixRef::operator+=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) += x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] + expr;
    }
}

void MatrixRef::operator-=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) -= x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] - expr;
    }
}

void MatrixRef::operator*=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) *= x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] * expr;
    }
}

void MatrixRef::operator/=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) -= x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] - expr;
    }
}

void MatrixRef::operator=(const FuncRefVar &e) {
    internal_assert(e.size() == 1);
    (*this) = Expr(e);
}

void MatrixRef::operator=(const FuncRefExpr &e) {
    internal_assert(e.size() == 1);
    (*this) = Expr(e);
}

MatrixRef::operator Expr() const {
    if (mat.is_large) {
        return mat.func(row, col);
    } else {
        const int i = mat.small_offset(row, col);
        return mat.coeffs[i];
    }
}

int Matrix::small_offset(Expr row, Expr col) {
    if (!is_large) {
        internal_assert(is_positive_int(i));
        internal_assert(is_positive_int(j));
        internal_assert(is_positive_int(nrows));
        internal_assert(is_positive_int(ncols));

        const int i = *as_const_int(row);
        const int j = *as_const_int(col);
        const int m = *as_const_int(nrows);
        
        return i + j * m;
    }

    return -1;
}

Matrix::Matrix() : is_large(false), nrows(0), ncols(0) {}

Matrix::Matrix(Expr m, Expr n, Type t) : is_large(true), nrows(m), ncols(n) {
    internal_assert(nrows.defined() && is_int(nrows));
    internal_assert(ncols.defined() && is_int(ncols));

    if (is_positive_int(nrows) && is_positive_int(ncols)) {
        const int nr = *Internal::as_const_int(nrows);
        const int nc = *Internal::as_const_int(ncols);

        if (nr < 4 && nc < 4) {
            is_large = false;
            coeffs.resize(nr * nc, Halide::undef(t));
            return;
        }
    }

    x = Var("x");
    y = Var("y");

    func(x, y) = Halide::undef(t);
    func.bound(x, 0, nrows)
                .bound(y, 0, ncols);
}

Matrix::Matrix(Expr m, Expr n, const std::vector<Expr>& c) : is_large(false), nrows(m), ncols(n) {
    internal_assert(is_positive_int(nrows));
    internal_assert(is_positive_int(ncols));

    const int nr = *Internal::as_const_int(nrows);
    const int nc = *Internal::as_const_int(ncols);

    internal_assert(nr < 4 && nc < 4);
    internal_assert(nr * nc == c.size());
    Type t = c[0].type();
    coeffs.resize(nr * nc, Halide::undef(t));
    for (int i = 0; i < c.size(); ++i) {
        internal_assert(c[i].type() = t);
        coeffs[i] = c[i];
    }
}

Matrix::Matrix(Expr m, Expr n, Func f) : is_large(true), nrows(m), ncols(n) {
    internal_assert(is_int(nrows));
    internal_assert(is_int(ncols));
    internal_assert(f.outputs() == 1);

    if (f.dimensions() == 1) {
        internal_assert(is_one(ncols) || is_one(nrows));

        if (is_one(ncols)) {
            if (is_positive_int(nrows)) {
                const int nr = *Internal::as_const_int(nrows);

                if (nr < 4) {
                    is_large = false;
                    coeffs.resize(nr);
                    
                    for (int i = 0; i < nr; ++i) {
                        coeffs[i] = f(i);
                    }
                }
                
                return;
            }
            
            x = f.args()[0];
            y = Var("y");
            func(x, y) = Halide::undef(f.output_types()[0]);
            func(x, 0) = f(x);
            func.bound(x, 0, nrows)
                .bound(y, 0, 1);
        } else {  // is_one(nrows)
            if (is_positive_int(ncols)) {
                const int nc = *Internal::as_const_int(ncols);

                if (nc < 4) {
                    is_large = false;
                    coeffs.resize(nc);
                    
                    for (int i = 0; i < nc; ++i) {
                        coeffs[i] = f(i);
                    }
                }
                
                return;
            }
            
            x = Var("y");
            y = f.args()[0];
            func(x, y) = Halide::undef(f.output_types()[0]);
            func(0, y) = f(y);
            func.bound(x, 0, 1)
                .bound(y, 0, ncols);
        }
    } else {
        internal_assert(f.dimensions() == 2);
    
        if (is_positive_int(nrows) && is_positive_int(ncols)) {
            const int nr = *Internal::as_const_int(nrows);
            const int nc = *Internal::as_const_int(ncols);

            if (0 < nr && 0 < nc && nr*nc < 16) {
                is_large = false;
                coeffs.resize(nr*nc);

                for (int j = 0; j < nc; ++j) {
                    for (int i = 0; i < nr; ++i) {
                        const int idx = small_offset(i, j);
                        coeffs[idx] = f(i, j);
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

Expr Matrix::num_rows() const {
    return nrows;
}

Expr Matrix::num_cols() const {
    return ncols;
}

Matrix Matrix::row(Expr i) const {
    if (is_positive_int(ncols)) {
        const int n = *Internal::as_const_int(ncols);
        if (n < 4) {
            std::vector<Expr> row_coeffs(n);
            for (int j = 0; j < n; ++j) {
                row_coeffs[j] = (*this)(i, j);
                return Matrix(1, ncols, row_coeffs);
            }
        }
    }
    
    Func row_func("matrix_row");
    row_func(y) = func(i, y);
    return Matrix(1, ncols, row_func);
}

Matrix Matrix::col(Expr j) const {
    if (is_positive_int(nrows)) {
        const int m = *Internal::as_const_int(nrows);
        if (m < 4) {
            std::vector<Expr> col_coeffs(n);
            for (int i = 0; i < m; ++i) {
                col_coeffs[i] = (*this)(i, j);
                return Matrix(nrows, 1, col_coeffs);
            }
        }
    }
    
    Func col_func("matrix_col");
    col_func(x) = func(x, j);
    return Matrix(nrows, 1, col_func);
}

Matrix Matrix::block(Expr min_i, Expr max_i, Expr min_j, Expr max_j) const {
}

Matrix Matrix::transpose() const {
}

MatrixRef Matrix::operator[] (Expr i) {
}

MatrixRef Matrix::operator() (Expr i, Expr j) {
}

}
