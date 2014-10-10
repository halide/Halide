#include "InlineReductions.h"
#include "IREquality.h"
#include "Matrix.h"
#include "Schedule.h"
#include "Simplify.h"
#include "Tuple.h"
#include "Util.h"

namespace Halide {

static const int small_matrix_limit = 4;

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

MatrixRef::MatrixRef(Matrix& M, Expr i, Expr j) : mat(M), rowcol(2) {
    internal_assert(i.defined() && is_int(i));
    internal_assert(j.defined() && is_int(j));
    rowcol[0] = i;
    rowcol[1] = j;
}

Expr MatrixRef::row() const {
    return rowcol[0];
}

Expr MatrixRef::col() const {
    return rowcol[1];
}

void MatrixRef::operator=(Expr x) {
    if (mat.is_large) {
        FuncRefExpr(mat.func, rowcol) = x;
    } else {
        const int i = mat.small_offset(row(), col());
        mat.coeffs[i] = x;
    }
}

void MatrixRef::operator+=(Expr x) {
    if (mat.is_large) {
        FuncRefExpr(mat.func, rowcol) += x;
    } else {
        const int i = mat.small_offset(row(), col());
        mat.coeffs[i] = mat.coeffs[i] + x;
    }
}

void MatrixRef::operator-=(Expr x) {
    if (mat.is_large) {
        FuncRefExpr(mat.func, rowcol) -= x;
    } else {
        const int i = mat.small_offset(row(), col());
        mat.coeffs[i] = mat.coeffs[i] - x;
    }
}

void MatrixRef::operator*=(Expr x) {
    if (mat.is_large) {
        FuncRefExpr(mat.func, rowcol) *= x;
    } else {
        const int i = mat.small_offset(row(), col());
        mat.coeffs[i] = mat.coeffs[i] * x;
    }
}

void MatrixRef::operator/=(Expr x) {
    if (mat.is_large) {
        FuncRefExpr(mat.func, rowcol) /= x;
    } else {
        const int i = mat.small_offset(row(), col());
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
        return FuncRefExpr(mat.func, rowcol);
    } else {
        const int i = mat.small_offset(row(), col());
        return mat.coeffs[i];
    }
}

namespace {

std::vector<Expr> vector_of(Expr v) {
    return std::vector<Expr>(1, v);
}

std::string strip(std::string name) {
    int pos = name.find('$');
    return name.substr(0, pos);
}

std::string matrix_name(Matrix *M, std::string name, std::string alt_name = "") {
    static std::string default_name = Internal::make_entity_name(M, "Halide::Matrix", 'M');

    if (name.empty()) {
        if (alt_name.empty()) {
            return default_name;
        } else {
            return Internal::unique_name(strip(alt_name));
        }
    } else {
        return Internal::unique_name(strip(name));
    }
}

}

std::vector<std::string> Matrix::args() const {
    std::vector<std::string> a(2);
    a[0] = ij[0].name();
    a[1] = ij[1].name();
    return a;
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

void Matrix::init(Expr num_row = 0, Expr num_col = 0) {
    ij = std::vector<Var>(2);
    ij[0] = Var("i");
    ij[1] = Var("j");

    nrows = num_row;
    ncols = num_col;

    internal_assert(nrows.defined() && is_int(nrows));
    internal_assert(ncols.defined() && is_int(ncols));

    is_large = true;

    int m, n;
    if(const_size(m, n)) {
        if (m <= small_matrix_limit &&
            n <= small_matrix_limit ) {
            is_large = false;
        }
    }

    Internal::Bound row_bound = {ij[0].name(), 0, nrows};
    Internal::Bound col_bound = {ij[1].name(), 0, ncols};
    schedule.bounds().push_back(row_bound);
    schedule.bounds().push_back(col_bound);
}

bool Matrix::const_num_rows(int &m) {
    if (is_size_const(nrows)) {
        m = *Internal::as_const_int(nrows);
        return true;
    } else {
        return false;
    }
}

bool Matrix::const_num_cols(int &n) {
    if (is_size_const(ncols)) {
        n = *Internal::as_const_int(ncols);
        return true;
    } else {
        return false;
    }
}

bool Matrix::const_size(int &m, int &n) {
    bool is_const = const_num_rows(m);
    is_const = is_const && const_num_cols(n);
    return is_const;
}

Matrix::Matrix(std::string name)
        : func(matrix_name(this, name)) {
    init(0, 0);
}

Matrix::Matrix(Expr num_row, Expr num_col, Type t, std::string name)
        : func(matrix_name(this, name)) {
    init(num_row, num_col);

    if (!is_large) {
        int m, n;
        const_size(m, n);
        coeffs.resize(m * n, Halide::undef(t));
    }
}

Matrix::Matrix(Expr num_row, Expr num_col, Tuple c, std::string name)
        : func(matrix_name(this, name)) {
    init(num_row, num_col);
    internal_assert(!is_large);

    int m, n;
    const_size(m, n);
    internal_assert((size_t)(m * n) == c.size());

    Type t = c[0].type();
    coeffs.resize(m * n);
    for (size_t i = 0; i < c.size(); ++i) {
        internal_assert(c[i].type() == t);
        coeffs[i] = c[i];
    }
}


Matrix::Matrix(Expr num_row, Expr num_col, std::vector<Expr> c, std::string name)
        : func(matrix_name(this, name)) {
    init(num_row, num_col);
    internal_assert(!is_large);

    int m, n;
    const_size(m, n);
    internal_assert((size_t)(m * n) == c.size());

    Type t = c[0].type();
    coeffs.resize(m * n);
    for (size_t i = 0; i < c.size(); ++i) {
        internal_assert(c[i].type() == t);
        coeffs[i] = c[i];
    }
}

Matrix::Matrix(ImageParam img, std::string name)
        : func(matrix_name(this, name, img.name())) {
    if (img.dimensions() == 1) {
        init(img.width(), 1);

        if (is_large) {
            func.define(args(), vector_of(img(ij[0])));
        } else {
            int m, n;
            const_size(m, n);
            coeffs.resize(m);  // n == 1
            for (int i = 0; i < m; ++i) {
                coeffs[i] = img(i);
            }
        }
    } else {
        internal_assert(img.dimensions() == 2);

        init(img.width(), img.height());

        if (is_large) {
            func.define(args(), vector_of(img(ij[0], ij[1])));
        } else {
            int m, n;
            const_size(m, n);
            coeffs.resize(m * n);

            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < m; ++i) {
                    const int idx = small_offset(i, j);
                    coeffs[idx] = img(i, j);
                }
            }
        }
    }
}

Matrix::Matrix(Expr num_row, Expr num_col, Func f, std::string name)
        : func(matrix_name(this, name, f.name())) {
    internal_assert(f.outputs() == 1);

    init(num_row, num_col);

    if (f.dimensions() == 1) {
        internal_assert(is_one(ncols) || is_one(nrows));

        if (!is_large) {
            int m, n;
            const_size(m, n);
            coeffs.resize(m * n);
            for (int i = 0; i < m * n; ++i) {
                coeffs[i] = f(i);
            }
        } else if (is_one(ncols)) {
            func.define(args(), vector_of(f(row_var())));
        } else {// is_one(nrows)
            func.define(args(), vector_of(f(col_var())));
        }
    } else {
        internal_assert(f.dimensions() == 2);

        if (is_large) {
            func.define(args(), vector_of(f(row_var(), col_var())));
        } else {
            int m, n;
            const_size(m, n);
            coeffs.resize(m * n);
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < m; ++i) {
                    int idx = small_offset(i, j);
                    coeffs[idx] = f(i, j);
                }
            }
        }
    }
}

Type Matrix::type() const {
    if (is_large) {
        return func.output_types()[0];
    } else {
        return coeffs[0].type();
    }
}

Matrix::operator Tuple() {
    internal_assert(!is_large);
    return Tuple(coeffs);
}

Matrix::operator Func() {
    if (!is_large && !func.has_pure_definition()) {
        int m, n;
        const_size(m, n);

        Expr mat = Halide::undef(type());
        for (int j = 0; j < n; ++j ) {
            for (int i = 0; i < n; ++i ) {
                const int idx = small_offset(i, j);
                mat = select(row_var() == i && col_var() == j,
                             coeffs[idx], mat);
            }
        }

        func.define(args(), vector_of(mat));
    }

    func.schedule() = schedule;
    return Func(func);
}

Matrix &Matrix::compute_at_rows(Matrix &other) {
    Internal::LoopLevel loop_level(other.name(), row_var().name());
    schedule.compute_level() = loop_level;
    if (schedule.store_level().is_inline()) {
        schedule.store_level() = loop_level;
    }

    if (is_large) {
        func.schedule() = schedule;
    }

    return *this;
}

Matrix &Matrix::compute_at_columns(Matrix &other) {
    Internal::LoopLevel loop_level(other.name(), col_var().name());
    schedule.compute_level() = loop_level;
    if (schedule.store_level().is_inline()) {
        schedule.store_level() = loop_level;
    }

    if (is_large) {
        func.schedule() = schedule;
    }

    return *this;
}

// Matrix &Matrix::compute_at_blocks(Matrix &other, Expr block_rows, Expr block_cols) {
//     LoopLevel loop_level(other.name(), col_var().name());
//     schedule.compute_level() = loop_level;
//     if (schedule.store_level().is_inline()) {
//         schedule.store_level() = loop_level;
//     }

//     if (is_large) {
//         func.schedule() = schedule;
//     }
// }

// Buffer Matrix::realize() {
//   internal_assert(is_size_const(nrows));
//   internal_assert(is_size_const(ncols));

//   const int nr = *Internal::as_const_int(nrows);
//   const int nc = *Internal::as_const_int(ncols);

//   Func f = *this;
//   f.bound(x, 0, nrows).bound(y, 0, ncols);

//   return f.realize(nr, nc);
// }

Expr Matrix::num_rows() const {
    return nrows;
}

Expr Matrix::num_cols() const {
    return ncols;
}

Matrix Matrix::row(Expr i) {
    std::string result_name = strip(this->name()) + "_row";

    int n;
    if (const_num_cols(n)) {
        if (n <= 4) {
            Matrix &A = *this;
            std::vector<Expr> row_coeffs(n);
            for (int j = 0; j < n; ++j) {
                row_coeffs[j] = static_cast<Expr>(A(i, j));
            }

            return Matrix(1, ncols, row_coeffs, result_name);
        }
    }

    Func row_func;
    row_func(col_var()) = FuncRefVar(func, ij);
    return Matrix(1, ncols, row_func, result_name);
}

Matrix Matrix::col(Expr j) {
    std::string result_name = strip(this->name()) + "_col";

    int m;
    if (const_num_rows(m)) {
        if (m <= 4) {
            Matrix &A = *this;
            std::vector<Expr> col_coeffs(m);
            for (int i = 0; i < m; ++i) {
                col_coeffs[i] = static_cast<Expr>(A(i, j));
            }

            return Matrix(nrows, 1, col_coeffs, result_name);
        }
    }

    Func col_func;
    col_func(row_var()) = FuncRefVar(func, ij);
    return Matrix(nrows, 1, col_func, result_name);
}

Matrix Matrix::block(Expr min_i, Expr max_i, Expr min_j, Expr max_j) {
    std::string result_name = strip(this->name()) + "_block";

    Expr block_nrows = simplify(max_i - min_i + 1);
    Expr block_ncols = simplify(max_j - min_j + 1);
    Matrix &A = *this;

    if (!is_large) {
        int m, n;
        const_size(m, n);

        std::vector<Expr> block_coeffs(m * n);
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < m; ++i) {
                const int idx = i + j * m;
                block_coeffs[idx] = static_cast<Expr>(A(i, j));
            }

            return Matrix(m, n, block_coeffs, result_name);
        }
    }

    Func block_func;
    block_func(row_var(), col_var()) =
            Halide::select(row_var() <= block_nrows && col_var() <= block_ncols,
                           A(row_var() - min_i, col_var() - min_j),
                           Halide::undef(type()));
    return Matrix(block_nrows, block_ncols, block_func, result_name);
}

Matrix Matrix::transpose() {
    std::string result_name = strip(this->name()) + "_t";

    if (is_large) {
        Func mat_trans;
        mat_trans(col_var(), row_var()) = FuncRefVar(func, ij);
        return Matrix(ncols, nrows, mat_trans, result_name);
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
        return Matrix(ncols, nrows, coeff_trans, result_name);
    }
}

Expr Matrix::cofactor(int i, int j) {
    user_assert(!is_large)
            << "matrix cofactors are only available for small matrices.\n";

    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    user_assert(m == n)
            << "matrix cofactors are only defined for square matrices.\n";

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
    user_assert(!is_large)
            << "matrix determinant is only available for small matrices.\n";

    // Assert nrows == ncols!!!
    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    user_assert(m == n)
            << "matrix determinant is only defined for square matrices.\n";

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
    user_assert(!is_large)
            << "matrix inverse is only available for small matrices.\n";

    // Assert nrows == ncols!!!
    const int m = *as_const_int(nrows);
    const int n = *as_const_int(ncols);
    user_assert(m == n)
            << "matrix inverse is only defined for square matrices.\n";

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
    user_assert(is_one(nrows) || is_one(ncols))
            << "operator[] only defined for 1-dimensional matrices.\n";

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
            Tuple ident(std::vector<Expr>(n*n));
            for (int j = 0; j < n; ++j) {
                for (int i =0; i < n; ++i) {
                    const int idx = i + j * n;
                    ident[idx] = i == j? Halide::cast(t, 1):
                            Halide::cast(t, 0);
                }
            }

            return Matrix(size, size, ident, "I");
        }
    }

    Func ident;
    Var i("i"), j("j");
    ident(i, j) = Halide::select(i == j, Halide::cast(t, 1),
                                         Halide::cast(t, 0));
    return Matrix(size, size, ident, "I");
}

Matrix operator+(Matrix A, Matrix B) {
    user_assert(equal(A.num_rows(), B.num_rows()) ||
                equal(A.num_cols(), B.num_cols())) <<
            "Attempting to add matrices of different sizes.";

    std::string result_name = strip(A.name()) + "_plus_" + strip(B.name());

    if (A.is_large_matrix()) {
        Var i = A.row_var();
        Var j = A.col_var();

        Func sum;
        sum(i, j) = A(i, j) + B(i, j);
        return Matrix(A.num_rows(), B.num_cols(), sum, result_name);
    } else {
        Tuple sum = A;
        Tuple B_coeffs = B;
        for (size_t k = 0; k < sum.size(); ++k) {
            sum[k] += B_coeffs[k];
        }

        return Matrix(A.num_rows(), A.num_cols(), sum, result_name);
    }
}

Matrix operator-(Matrix A, Matrix B) {
    user_assert(equal(A.num_rows(), B.num_rows()) ||
                equal(A.num_cols(), B.num_cols())) <<
            "Attempting to subtract matrices of different sizes.";

    std::string result_name = strip(A.name()) + "_minus_" + strip(B.name());

    if (A.is_large_matrix()) {
        Var i = A.row_var();
        Var j = A.col_var();

        Func diff;
        diff(i, j) = A(i, j) - B(i, j);
        return Matrix(A.num_rows(), B.num_cols(), diff, result_name);
    } else {
        Tuple diff = A;
        Tuple B_coeffs = B;
        for (size_t k = 0; k < diff.size(); ++k) {
            diff[k] -= B_coeffs[k];
        }

        return Matrix(A.num_rows(), A.num_cols(), diff, result_name);
    }
}

Matrix operator*(Expr a, Matrix B) {
    std::string result_name = strip(B.name()) + "_scaled";

    if (B.is_large_matrix()) {
        Var i = B.row_var();
        Var j = B.col_var();

        Func scale;
        scale(i, j) = a * B(i, j);
        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    } else {
        Tuple scale = B;
        for (size_t k = 0; k < scale.size(); ++k) {
            scale[k] = a * scale[k];
        }

        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    }
}

Matrix operator*(Matrix B, Expr a) {
    std::string result_name = strip(B.name()) + "_scaled";

    if (B.is_large_matrix()) {
        Var i = B.row_var();
        Var j = B.col_var();

        Func scale;
        scale(i, j) = B(i, j) * a;
        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    } else {
        Tuple scale = B;
        for (size_t k = 0; k < scale.size(); ++k) {
            scale[k] *= a;
        }

        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    }
}

Matrix operator/(Matrix B, Expr a) {
    std::string result_name = strip(B.name()) + "_scaled";

    if (B.is_large_matrix()) {
        Var i = B.row_var();
        Var j = B.col_var();

        Func scale;
        scale(i, j) = B(i, j) / a;
        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    } else {
        Tuple scale = B;
        for (size_t k = 0; k < scale.size(); ++k) {
            scale[k] /= a;
        }

        return Matrix(B.num_rows(), B.num_cols(), scale, result_name);
    }
}

Matrix operator*(Matrix A, Matrix B) {
    // user_assert(equal(A.num_cols(), B.num_rows()))
    //         << "Attempting to multiply matrices of mis-matched dimensions: "
    //         << A.num_rows() << "x" << A.num_cols() << " and "
    //         << B.num_rows() << "x" << B.num_cols() << ".\n";

    Expr prod_nrows = A.num_rows();
    Expr prod_ncols = B.num_cols();

    std::string result_name = strip(A.name()) + "_times_" + strip(B.name());

    if (is_positive_const(prod_nrows) && is_positive_const(prod_ncols)) {
        const int m = *as_const_int(prod_nrows);
        const int n = *as_const_int(prod_ncols);

        if (m <= 4 && n <= 4) {
            // Product will be a small matrix.
            Tuple prod(std::vector<Expr> (m*n));

            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < m; ++i) {
                    const int idx = i + j * m;
                    if (A.is_large_matrix()) {
                        RDom k(0, A.num_rows(), "k");
                        prod[idx] = sum(A(i, k) * B(k, j));
                    } else {
                        const int p = *as_const_int(A.num_cols());
                        prod[idx] = cast(A.type(), 0);
                        for (int k = 0; k < p; ++k) {
                            prod[idx] += A(i, k) * A(k, j);
                        }
                    }
                }
            }

            return Matrix(prod_nrows, prod_ncols, prod, result_name);
        }
    }

    Var i = A.row_var();
    Var j = A.col_var();

    RDom k(0, A.num_cols(), "k");
    Func prod;
    prod(i, j) = sum(A(i, k) * B(k, j));

    // const int  vec_size  = 8;
    // const int  tile_size = 16;

    return Matrix(prod_nrows, prod_ncols, prod, result_name);

#if 0
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
#endif
}


}
