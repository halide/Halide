#include "InlineReductions.h"
#include "Matrix.h"
#include "Simplify.h"

namespace Halide {

bool is_int(Expr i) {
    return i.type().is_int() || i.type().is_uint();
}

bool is_size_const(Expr i) {
    bool valid = is_const(i) && is_int(i);
    if (valid) {
        const int n = *as_const_int(i);
        valid = n >= 0;
    }

    return valid;
}

MatrixRef::MatrixRef(Matrix& M, Expr i, Expr j) : mat(M), row(i), col(j) {
    internal_assert(row.defined() && is_int(row));
    internal_assert(col.defined() && is_int(col));
}

void MatrixRef::operator=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) = x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = x;
    }
}

void MatrixRef::operator+=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) += x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] + x;
    }
}

void MatrixRef::operator-=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) -= x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] - x;
    }
}

void MatrixRef::operator*=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) *= x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] * x;
    }
}

void MatrixRef::operator/=(Expr x) {
    if (mat.is_large) {
        mat.func(row, col) -= x;
    } else {
        const int i = mat.small_offset(row, col);
        mat.coeffs[i] = mat.coeffs[i] - x;
    }
}

void MatrixRef::operator=(const MatrixRef &e) {
    (*this) = Expr(e);
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

int Matrix::small_offset(Expr row, Expr col) const {
    if (!is_large) {
        internal_assert(is_size_const(row));
        internal_assert(is_size_const(col));
        internal_assert(is_size_const(nrows));
        internal_assert(is_size_const(ncols));

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

    if (is_size_const(nrows) && is_size_const(ncols)) {
        const int nr = *Internal::as_const_int(nrows);
        const int nc = *Internal::as_const_int(ncols);

        if (nr <= 4 && nc <= 4) {
            is_large = false;
            coeffs.resize(nr * nc, Halide::undef(t));
            return;
        }
    }

    x = Var("x");
    y = Var("y");

    func(x, y) = Halide::undef(t);
}

Matrix::Matrix(Expr m, Expr n, const std::vector<Expr>& c) : is_large(false), nrows(m), ncols(n) {
    internal_assert(is_size_const(nrows));
    internal_assert(is_size_const(ncols));

    const int nr = *Internal::as_const_int(nrows);
    const int nc = *Internal::as_const_int(ncols);

    internal_assert(nr <= 4 && nc <= 4);
    internal_assert((size_t)(nr * nc) == c.size());
    Type t = c[0].type();
    coeffs.resize(nr * nc, Halide::undef(t));
    for (size_t i = 0; i < c.size(); ++i) {
        internal_assert(c[i].type() == t);
        coeffs[i] = c[i];
    }
}

Matrix::Matrix(ImageParam img) : is_large(true) {
    if (img.dimensions() == 1) {
        nrows = img.width();
        ncols = 1;

        if (is_size_const(nrows)) {
            const int nr = *Internal::as_const_int(nrows);

            if (nr <= 4) {
                is_large = false;
                coeffs.resize(nr);

                for (int i = 0; i < nr; ++i) {
                    coeffs[i] = img(i);
                }

                return;
            }
        }

        x = Var("x");
        y = Var("y");
        func(x, y) = img(x);
    } else {
        internal_assert(img.dimensions() == 2);

        nrows = img.width();
        ncols = img.height();

        if (is_size_const(nrows) && is_size_const(ncols)) {
            const int nr = *Internal::as_const_int(nrows);
            const int nc = *Internal::as_const_int(ncols);

            if (nr <= 4 && nc <= 4) {
                is_large = false;
                coeffs.resize(nr*nc);

                for (int j = 0; j < nc; ++j) {
                    for (int i = 0; i < nr; ++i) {
                        const int idx = small_offset(i, j);
                        coeffs[idx] = img(i, j);
                    }
                }

                return;
            }
        }

        x = Var("x");
        y = Var("y");
        func(x, y) = img(x, y);
    }
}

Matrix::Matrix(Expr m, Expr n, Func f) : is_large(true), nrows(m), ncols(n) {
    internal_assert(is_int(nrows));
    internal_assert(is_int(ncols));
    internal_assert(f.outputs() == 1);

    if (f.dimensions() == 1) {
        internal_assert(is_one(ncols) || is_one(nrows));

        if (is_one(ncols)) {
            if (is_size_const(nrows)) {
                const int nr = *Internal::as_const_int(nrows);

                if (nr <= 4) {
                    is_large = false;
                    coeffs.resize(nr);

                    for (int i = 0; i < nr; ++i) {
                        coeffs[i] = f(i);
                    }

                    return;
                }
            }

            x = f.args()[0];
            y = Var("y");
            func(x, y) = f(x);
        } else {  // is_one(nrows)
            if (is_size_const(ncols)) {
                const int nc = *Internal::as_const_int(ncols);

                if (nc <= 4) {
                    is_large = false;
                    coeffs.resize(nc);

                    for (int i = 0; i < nc; ++i) {
                        coeffs[i] = f(i);
                    }

                    return;
                }
            }

            x = Var("y");
            y = f.args()[0];
            func(x, y) = f(y);
        }
    } else {
        internal_assert(f.dimensions() == 2);

        if (is_size_const(nrows) && is_size_const(ncols)) {
            const int nr = *Internal::as_const_int(nrows);
            const int nc = *Internal::as_const_int(ncols);

            if (nr <= 4 && nc <= 4) {
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
    }
}

Type Matrix::type() const {
    if (is_large) {
        return func.output_types()[0];
    } else {
        return coeffs[0].type();
    }
}

Func Matrix::function() {
    if (!is_large && !func.defined()) {
        const int nr = *Internal::as_const_int(nrows);
        const int nc = *Internal::as_const_int(ncols);

        func(x, y) = Halide::undef(type());

        for (int j = 0; j < nc; ++j ) {
            for (int i = 0; i < nr; ++i ) {
                const int idx = small_offset(i, j);
                func(i, j) = coeffs[idx];
            }
        }

        // func.bound(x, 0, nrows)
        //     .bound(y, 0, ncols)
        //     .unroll(x)
        //     .unroll(y);
    }

    return func;
}

Buffer Matrix::realize() {
  internal_assert(is_size_const(nrows));
  internal_assert(is_size_const(ncols));

  const int nr = *Internal::as_const_int(nrows);
  const int nc = *Internal::as_const_int(ncols);

  Func f = function();
  f.bound(x, 0, nrows).bound(y, 0, ncols);

  return f.realize(nr, nc);
}

Expr Matrix::num_rows() const {
    return nrows;
}

Expr Matrix::num_cols() const {
    return ncols;
}

Matrix Matrix::row(Expr i) {
    if (is_size_const(ncols)) {
        const int n = *Internal::as_const_int(ncols);
        if (n <= 4) {
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

Matrix Matrix::col(Expr j) {
    if (is_size_const(nrows)) {
        const int m = *Internal::as_const_int(nrows);
        if (m <= 4) {
            std::vector<Expr> col_coeffs(m);
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

Matrix Matrix::block(Expr min_i, Expr max_i, Expr min_j, Expr max_j) {
    Expr block_nrows = simplify(max_i - min_i + 1);
    Expr block_ncols = simplify(max_j - min_j + 1);

    if (is_size_const(block_nrows) && is_size_const(block_ncols)) {
        const int m = *Internal::as_const_int(block_nrows);
        const int n = *Internal::as_const_int(block_ncols);

        if (m <= 4 && n <= 4) {
            std::vector<Expr> block_coeffs(m * n);
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < m; ++i) {
                    const int idx = i + j * m;
                    block_coeffs[idx] = (*this)(i, j);
                    return Matrix(m, n, block_coeffs);
                }
            }
        }
    }

    Func block_func("matrix_block");
    block_func(x, y) = Halide::select(x <= block_nrows && y <= block_ncols,
                                      func(x - min_i, y - min_j),
                                      Halide::undef(type()));
    return Matrix(block_nrows, block_ncols, block_func);
}

Matrix Matrix::transpose() {
    if (is_large) {
        Func mat_trans("matrix_trans");
        mat_trans(x, y) = func(y, x);
        return Matrix(ncols, nrows, mat_trans);
    } else {
        const int m = *as_const_int(nrows);
        const int n = *as_const_int(ncols);

        std::vector<Expr> coeff_trans(m * n);
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < m; ++i) {
                const int idx = small_offset(i, j);
                const int idx_t = small_offset(j, i);
                coeff_trans[idx_t] = coeffs[idx];
            }
        }
        return Matrix(ncols, nrows, coeff_trans);
    }
}

Expr Matrix::cofactor(int i, int j) {
    internal_assert(!is_large) << "matrix cofactors are only available for small matrices.\n";

    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    internal_assert(m == n) << "matrix cofactors are only defined for square matrices.\n";

    Matrix &A = *this;
    Matrix  B(n-1, n-1, A.type());
    Expr sign = (i + j) % 2 == 0? 1 : -1;

    for (int k = 0; k < n-1; ++k) {
        const int k_off = k < j? 0: 1;
        for (int l = 0; l < n-1; ++l) {
            const int l_off = l < i? 0: 1;
            B(l,k) = A(l + l_off, k + k_off);
        }
    }

    return sign * B.determinant();
}

Expr Matrix::determinant() {
    internal_assert(!is_large) << "matrix determinant is only available for small matrices.\n";

    // Assert nrows == ncols!!!
    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    internal_assert(m == n) << "matrix determinant is only defined for square matrices.\n";

    Matrix &A = *this;

    Expr det = cast(type(), 0);
    if (n == 1) {
        det = A(0,0);
    } else if (n == 2) {
        det = A(0,0)*A(1,1) - A(0,1)*A(1,0);
    } else if (n == 3) {
        det = A(0,0)*(A(1,1)*A(2,2) - A(1,2)*A(2,1))
                - A(0,1)*(A(1,0)*A(2,2) - A(1,2)*A(2,0))
                + A(0,2)*(A(1,0)*A(2,1) - A(1,1)*A(2,0));
    } else { /*if (n == 4)*/
        for (int j = 0; j < n; ++j) {
            det += A(0,j) * A.cofactor(0, j);
        }
    }
    return det;
}

Matrix Matrix::inverse() {
    internal_assert(!is_large) << "matrix inverse is only available for small matrices.\n";

    // Assert nrows == ncols!!!
    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    internal_assert(m == n) << "matrix inverse is only defined for square matrices.\n";

    Matrix &A = *this;
    Expr det = A.determinant();

    Matrix inv(n, n, type());
    if (n == 1) {
        inv(0,0) = 1 / A(0,0);
    } else if (n == 2) {
        inv(0,0) =  A(1,1) / det;  inv(0,1) = -A(0,1) / det;
        inv(1,0) = -A(1,0) / det;  inv(1,1) =  A(0,0) / det;
    } else { /*if (n == 3 || n == 4)*/
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                inv(i, j) = cofactor(j, i) / det;
            }
        }
    }
    return inv;
}

MatrixRef Matrix::operator[] (Expr i) {
    internal_assert(is_one(nrows) || is_one(ncols));

    if (is_one(nrows)) {
        return MatrixRef(*this, 0, i);
    } else /*if (is_one(ncols))*/ {
        return MatrixRef(*this, i, 0);
    }
}

MatrixRef Matrix::operator() (Expr i, Expr j) {
    return MatrixRef(*this, i, j);
}

Matrix identity_matrix(Type t, Expr size) {
    if (is_positive_const(size)) {
        const int n = *as_const_int(size);

        if (n <= 4) {
            std::vector<Expr> ident(n * n);
            for (int j = 0; j < n; ++j) {
                for (int i =0; i < n; ++i) {
                    const int idx = i + j * n;
                    ident[idx] = i == j? Halide::cast(t, 1):
                                         Halide::cast(t, 0);
                }
            }

            return Matrix(size, size, ident);
        }
    }

    Func ident("identity_matrix");
    Var x("x"), y("y");
    ident(x, y) = Halide::select(x == y, Halide::cast(t, 1),
                                         Halide::cast(t, 0));
    return Matrix(size, size, ident);
}

Matrix operator+(Matrix a, Matrix b) {
    // internal_assert(a.num_rows() == b.num_rows());
    // internal_assert(a.num_cols() == b.num_cols());

    if (a.is_large) {
        Var x("x"), y("y");

        Func sum("matrix_sum");
        sum(x, y) = a.func(x, y) + b.func(x, y);
        return Matrix(a.nrows, a.ncols, sum);
    } else {
        std::vector<Expr> sum(a.coeffs);
        for (size_t i = 0; i < sum.size(); ++i) {
            sum[i] += b.coeffs[i];
        }

        return Matrix(a.nrows, a.ncols, sum);
    }
}

Matrix operator-(Matrix a, Matrix b) {
    // internal_assert(a.num_rows() == b.num_rows());
    // internal_assert(a.num_cols() == b.num_cols());

    if (a.is_large) {
        Var x("x"), y("y");

        Func diff("matrix_diff");
        diff(x, y) = a.func(x, y) - b.func(x, y);
        return Matrix(a.nrows, a.ncols, diff);
    } else {
        std::vector<Expr> diff(a.coeffs);
        for (size_t i = 0; i < diff.size(); ++i) {
            diff[i] -= b.coeffs[i];
        }

        return Matrix(a.nrows, a.ncols, diff);
    }
}

Matrix operator*(Expr a, Matrix b) {
    if (b.is_large) {
        Var x("x"), y("y");

        Func scale("matrix_scale");
        scale(x, y) = a * b.func(x, y);
        return Matrix(b.nrows, b.ncols, scale);
    } else {
        std::vector<Expr> scale(b.coeffs);
        for (size_t i = 0; i < scale.size(); ++i) {
            scale[i] *= a;
        }

        return Matrix(b.nrows, b.ncols, scale);
    }
}

Matrix operator*(Matrix b, Expr a) {
    if (b.is_large) {
        Var x("x"), y("y");

        Func scale("matrix_scale");
        scale(x, y) = a * b.func(x, y);
        return Matrix(b.nrows, b.ncols, scale);
    } else {
        std::vector<Expr> scale(b.coeffs);
        for (size_t i = 0; i < scale.size(); ++i) {
            scale[i] *= a;
        }

        return Matrix(b.nrows, b.ncols, scale);
    }
}

Matrix operator/(Matrix b, Expr a) {
    if (b.is_large) {
        Var x("x"), y("y");

        Func scale("matrix_scale");
        scale(x, y) = b.func(x, y) / a;
        return Matrix(b.nrows, b.ncols, scale);
    } else {
        std::vector<Expr> scale(b.coeffs);
        for (size_t i = 0; i < scale.size(); ++i) {
            scale[i] /= a;
        }

        return Matrix(b.nrows, b.ncols, scale);
    }
}

Matrix operator*(Matrix a, Matrix b) {
    // internal_assert(a.num_cols() == b.num_rows());

    Expr prod_nrows = a.num_rows();
    Expr prod_ncols = b.num_cols();

    if (is_positive_const(prod_nrows) && is_positive_const(prod_ncols)) {
        const int m = *as_const_int(prod_nrows);
        const int n = *as_const_int(prod_ncols);

        if (m <= 4 && n <= 4) {
            // Product will be a small matrix.
            std::vector<Expr> prod(m * n);

            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < m; ++i) {
                    const int idx = i + j * m;
                    if (a.is_large) {
                        RDom k(0, a.num_rows(), "k");
                        prod[idx] = sum(a.func(i, k) * b.func(k, j));
                    } else {
                        const int p = *as_const_int(a.ncols);
                        prod[idx] = cast(a.type(), 0);
                        for (int k = 0; k < p; ++k) {
                            prod[idx] += a(i, k) * b(k, j);
                        }
                    }
                }
            }

            return Matrix(prod_nrows, prod_ncols, prod);
        }
    }

    Var x("x"), y("y");
    Var tx("tx"), ty("ty");
    Var ttx("ttx"), tty("tty");

    Func prod("matrix_prod");
    Func Bt("Bt"), A("A");
    Bt(x, y) = b.func(y, x);
    A(x, y)  = a.func(x, y);

    const Expr sum_size  = a.ncols;
    const int  vec_size  = 8;
    const int  tile_size = 16;

    Func dot("row_dot");
    RDom sum_vecs(0, sum_size/vec_size);
    Var z("z");
    dot(z, x, y) += A(sum_vecs*vec_size + z, x) * Bt(sum_vecs*vec_size + z, y);

    RDom sum_lanes(0, vec_size);
    prod(x, y) = sum(dot(sum_lanes, x, y));

    prod.bound(x, 0, (a.nrows/tile_size)*tile_size).bound(y, 0, (b.ncols/tile_size)*tile_size)
        .tile(x, y, tx, ty, x, y, tile_size, tile_size)
        .tile(x, y, ttx, tty, x, y, vec_size, vec_size)
        .parallel(ty);

    dot.compute_at(prod, ttx).vectorize(z);
    dot.update()
            .reorder(z, x, y, sum_vecs).vectorize(z)
            .unroll(x).unroll(y);

    // Compute B transpose per-core as needed in 16x16 tiles.
    // a.func.compute_at(dot, sum_vecs).bound(x, 0, a.nrows).bound(y, 0, a.ncols);
    Bt.compute_at(prod, ty).bound(x, 0, (b.ncols/tile_size)*tile_size).bound(y, 0, (b.nrows/tile_size)*tile_size)
            .tile(x, y, tx, ty, x, y, tile_size, tile_size);

    // B.compute_at(prod, ty)
    //         .tile(x, y, tx, ty, x, y, tile_size, tile_size);

    prod.output_buffer().set_min(0,0).set_min(1,0);

    return Matrix(prod_nrows, prod_ncols, prod);
}


}
