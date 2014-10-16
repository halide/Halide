#include "InlineReductions.h"
#include "IREquality.h"
#include "Matrix.h"
#include "Schedule.h"
#include "Simplify.h"
#include "Tuple.h"
#include "Util.h"

namespace Halide {

static const int small_matrix_limit = 4;


namespace {

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

std::vector<Expr> vector_of(Expr v) {
    return std::vector<Expr>(1, v);
}

std::string strip(std::string name) {
    int pos = name.find('$');
    return name.substr(0, pos);
}

std::string block_var_name(std::string base_name, int level) {
    std::ostringstream sout;
    sout << "b" << level << "_" << base_name;
    return sout.str();
}

std::string matrix_name(Matrix *M, std::string name, std::string alt_name = "") {
    static std::string default_name = Internal::make_entity_name(M, "Halide::Matrix", 'M');

    if (name.empty()) {
        if (alt_name.empty()) {
            return default_name;
        } else {
            return strip(alt_name);
        }
    } else {
        return strip(name);
    }
}

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
        mat.func(row, col) /= x;
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

Partition::Partition(Matrix &mat) :
    matrix(mat), nrows(1), ncols(1), level(0)
{
    bi = Var("i");
    bj = Var("j");

    specialized = false;
}

Partition::Partition(Matrix &mat, int up, int lvl, Expr num_rows, Expr num_cols) :
    matrix(mat), nrows(num_rows), ncols(num_cols), update(up), level(lvl)
{
    internal_assert(level > 0);

    bi = Var(block_var_name("i", level));
    bj = Var(block_var_name("j", level));

    std::cout << "Partitioning matrix " << matrix.name() << " at level " << level
              << " into " << nrows << " x " << ncols << " blocks. "
              << "Partion vars: (" << bi.name() << ", " << bj.name() << ")\n";

    int m, n;
    bool const_size_matrix = matrix.const_size(m, n);

    for (int i = level-1; i >= 0; --i) {
        Partition &p = matrix.get_partition(i);

        num_rows = simplify(num_rows * p.nrows);
        num_cols = simplify(num_cols * p.ncols);
    }

    std::cout << "\tPartion blocks total size = " << num_rows << " x " << num_cols << "\n";

    if (const_size_matrix && is_size_const(num_rows) && is_size_const(num_cols)) {
        specialized = false;
    } else {
        condition = matrix.num_rows() >= num_rows && matrix.num_cols() >= num_cols;
        specialized = true;
        std::cout << "\tPartion schedule is specialized on the condition " << condition << "\n";
    }
    
    Var pi = parent().row_var();
    Var pj = parent().col_var();

    std::cout << "\tPartion tiled as: " << matrix.name() << "("
              << pi.name() << ", " << pj.name() << ", "
              << bi.name() << ", " << bj.name() << ", "
              << pi.name() << ", " << pj.name() << ")\n";
    schedule().tile(pi, pj, bi, bj, pi, pj, nrows, ncols);
}

Stage Partition::schedule() {
    if (specialized) {
        return parent().schedule().specialize(condition);
    } else {
        return matrix.root_schedule(update;);
    }
}

Partition &Partition::parent() {
    internal_assert(level > 0);
    return matrix.get_partition(level-1);
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
    partitions.push_back(Partition(*this));

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

    vec_level = -1;
    par_level = -1;
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

Stage Matrix::root_schedule(int update) {
    internal_assert(func.defined());

    if (update < 0) {
        return static_cast<Stage>(func);
    } else {
        return func.update(update);
    }
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
            Var i = row_var();
            Var j = col_var();
            func(i, j) = img(i);
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
            Var i = row_var();
            Var j = col_var();
            func(i, j) = img(i, j);
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
            Var i = row_var();
            Var j = col_var();
            func(i, j) = f(i);
        } else {// is_one(nrows)
            Var i = row_var();
            Var j = col_var();
            func(i, j) = f(j);
        }
    } else {
        internal_assert(f.dimensions() == 2);

        if (is_large) {
            Var i = row_var();
            Var j = col_var();
            func(i, j) = f(i, j);
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

Var Matrix::row_var() const {
    return root_partition().row_var();
}

Var Matrix::col_var() const {
    return root_partition().col_var();
}

Type Matrix::type() const {
    if (is_large) {
        return func.output_types()[0];
    } else {
        return coeffs[0].type();
    }
}

Expr Matrix::num_rows() const {
    return nrows;
}

Expr Matrix::num_cols() const {
    return ncols;
}

Matrix::operator Tuple() {
    internal_assert(!is_large);
    return Tuple(coeffs);
}

Matrix::operator Func() {
    if (!is_large && !func.defined()) {
        int m, n;
        const_size(m, n);

        Expr mat = undef(type());
        for (int j = 0; j < n; ++j ) {
            for (int i = 0; i < n; ++i ) {
                const int idx = small_offset(i, j);
                mat = select(row_var() == i && col_var() == j,
                             coeffs[idx], mat);
            }
        }

        func(row_var(), col_var()) = mat;
    }

    return func;
}

Matrix &Matrix::compute_at_rows(Matrix &other, int level) {
    if (is_large) {
        internal_assert(0 <= level && level < other.num_partitions());

        Partition &p = other.get_partition(level);

        // Inject the compute_at variable into all other branches of the
        // specialization tree via renames.
        if (p.is_specialization()) {
            bool finish = false;
            for (int i = level-1; i >= 0 && !finish; --i) {
                Partition &pi = other.get_partition(i);
                std::cout << "Renaming row var in partition " << i
                          << ": " << pi.row_var() << " --> " << p.row_var() << "\n";
                pi.schedule().rename(pi.row_var(), p.row_var());
                finish = !pi.is_specialization();
            }
        }
        
        func.compute_at(static_cast<Func>(other), p.row_var());
    }

    return *this;
}

Matrix &Matrix::compute_at_columns(Matrix &other, int level) {
    if (is_large) {
        internal_assert(0 <= level && level < other.num_partitions());

        Partition &p = other.get_partition(level);

        // Inject the compute_at variable into all other branches of the
        // specialization tree via renames.
        if (p.is_specialization()) {
            bool finish = false;
            for (int i = level-1; i >= 0 && !finish; --i) {
                Partition &pi = other.get_partition(i);
                std::cout << "Renaming col var in partition " << i
                          << ": " << pi.col_var() << " --> " << p.col_var() << "\n";
                pi.schedule().rename(pi.col_var(), p.col_var());
                finish = !pi.is_specialization();
            }
        }

        func.compute_at(static_cast<Func>(other), p.col_var());
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

Matrix &Matrix::partition(Expr size) {
    return partition(size, size);
}

Matrix &Matrix::partition(Expr row_size, Expr col_size) {
    int level = partitions.size();
    partitions.push_back(Partition(*this, level, row_size, col_size));

    if (level == vec_level) {
        const int *vec_size = as_const_int(row_size);
        user_assert(vec_size) <<
                "Attemtping to vectorize matrix computations on a partition that doesn't "
                "have a constant number of rows. Partition at level " << level << " on "
                "matrix " << name() << " does not have constant height.\n";

        Partition &p = partitions.back();
        p.schedule().vectorize(p.row_var());
    }

    return *this;
}

Matrix &Matrix::vectorize(int level) {
    user_assert(vec_level == -1) <<
            "The vectorize schedule on matrices may only be applied at one level "
            "you have already called vectorize on matrix " << name() << " at "
            "level " << vec_level << ".\n";

    if (level < 0) {
        vec_level = partitions.size()-1;
    } else {
        vec_level = level;
    }

    if (vec_level < partitions.size()) {
        Partition &p = partitions[vec_level];

        const int *vec_size = as_const_int(p.num_rows());
        user_assert(vec_size) <<
                "Attemtping to vectorize matrix computations on a partition that doesn't "
                "have a constant number of rows. Partition at level " << level << " on "
                "matrix " << name() << " does not have constant height.\n";

        p.schedule().vectorize(p.row_var());
    }

    return *this;
}

Matrix &Matrix::parallelize(int level) {
    user_assert(par_level == -1) <<
            "The parallel schedule on matrices may only be applied at one level "
            "you have already called parallelize on matrix " << name() << " at "
            "level " << par_level << ".\n";


    if (level < 0) {
        par_level = partitions.size()-1;
    } else {
        par_level = level;
    }

    if (par_level < partitions.size()) {
        Partition &p = partitions[par_level];
        p.schedule().parallel(p.col_var());
    }

    return *this;
}

int Matrix::num_partitions() {
    return partitions.size();
}

Partition &Matrix::root_partition() {
    return partitions[0];
}

Partition &Matrix::get_partition(int level) {
    internal_assert(0 <= level && level < partitions.size());
    return partitions[level];
}

const Partition &Matrix::root_partition() const {
    return partitions[0];
}

const Partition &Matrix::get_partition(int level) const {
    internal_assert(0 <= level && level < partitions.size());
    return partitions[level];
}

Matrix Matrix::block(Expr min_i, Expr max_i, Expr min_j, Expr max_j) {
    Matrix &A = *this;

    std::string result_name = strip(this->name()) + "_block";

    Expr block_nrows = simplify(max_i - min_i + 1);
    Expr block_ncols = simplify(max_j - min_j + 1);

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

    Matrix block(block_nrows, block_ncols, A.type(), result_name);
    block(row_var(), col_var()) =
            Halide::select(row_var() <= block_nrows && col_var() <= block_ncols,
                           A(row_var() - min_i, col_var() - min_j),
                           Halide::undef(type()));
    return block;
}

Matrix Matrix::row(Expr i) {
    Matrix &A = *this;

    std::string result_name = strip(this->name()) + "_block";

    if (!is_large) {
        int m, n;
        const_size(m, n);

        std::vector<Expr> row_coeffs(m * n);
        for (int j = 0; j < n; ++j) {
            row_coeffs[j] = static_cast<Expr>(A(i, j));
        }

        return Matrix(m, n, row_coeffs, result_name);
    }

    Matrix row(1, ncols, A.type(), result_name);
    row(row_var(), col_var()) = A(0, col_var());
    return row;
}

Matrix Matrix::col(Expr j) {
    Matrix &A = *this;

    std::string result_name = strip(this->name()) + "_col";

    int m;
    if (const_num_rows(m)) {
        if (m <= 4) {
            std::vector<Expr> col_coeffs(m);
            for (int i = 0; i < m; ++i) {
                col_coeffs[i] = static_cast<Expr>(A(i, j));
            }

            return Matrix(nrows, 1, col_coeffs, result_name);
        }
    }

    Matrix col(nrows, 1, A.type(), result_name);
    col(row_var(), col_var()) = A(row_var(), 0);
    return col;
}

Matrix Matrix::transpose() {
    std::string result_name = strip(this->name()) + "_t";

    if (is_large) {
        Matrix &A = *this;
        Matrix A_t(ncols, nrows, A.type(), result_name);
        A_t(row_var(), col_var()) = A(col_var(), row_var());
        return A_t;
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
                    ident[idx] = i == j? cast(t, 1): cast(t, 0);
                }
            }

            return Matrix(size, size, ident, "I");
        }
    }

    Matrix I(size, size, t, "I");
    Var i = I.row_var();
    Var j = I.col_var();
    I(i, j) = select(i == j, cast(t, 1), cast(t, 0));
    return I;
}

Matrix operator+(Matrix A, Matrix B) {
    user_assert(equal(A.num_rows(), B.num_rows()) ||
                equal(A.num_cols(), B.num_cols())) <<
            "Attempting to add matrices of different sizes.";

    std::string result_name = strip(A.name()) + "_plus_" + strip(B.name());

    if (A.is_large_matrix()) {
        Matrix sum(A.num_rows(), A.num_cols(), A.type(), result_name);
        Var i = sum.row_var();
        Var j = sum.col_var();
        sum(i, j) = A(i, j) + B(i, j);
        return sum;
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
        Matrix diff(A.num_rows(), A.num_cols(), A.type(), result_name);
        Var i = diff.row_var();
        Var j = diff.col_var();
        diff(i, j) = A(i, j) - B(i, j);
        return diff;
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
        Matrix scale(B.num_rows(), B.num_cols(), B.type(), result_name);
        Var i = scale.row_var();
        Var j = scale.col_var();
        scale(i, j) = a * B(i, j);
        return scale;
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
        Matrix scale(B.num_rows(), B.num_cols(), B.type(), result_name);
        Var i = scale.row_var();
        Var j = scale.col_var();
        scale(i, j) = B(i, j) * a;
        return scale;
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
        Matrix scale(B.num_rows(), B.num_cols(), B.type(), result_name);
        Var i = scale.row_var();
        Var j = scale.col_var();
        scale(i, j) = B(i, j) / a;
        return scale;
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

    Matrix prod(A.num_rows(), B.num_cols(), A.type(), result_name);
    Var i = prod.row_var();
    Var j = prod.col_var();

    RDom k(0, A.num_cols(), "k");
    prod(i, j) += A(i, k) * B(k, j);

    const int vec_size  = 16 / A.type().bytes();
    const int tile_size = 4;
    prod.partition(vec_size).vectorize()
        .partition(tile_size)
        .partition(2).parallelize();

    A.compute_at_rows(prod, 2);
    B.compute_at_rows(prod, 2);

    // Var b1_i("b1_i"), b1_j("b1_j");
    // Var b2_i("b2_i"), b2_j("b2_j");
    // Var b3_i("b3_i"), b3_j("b3_j");

    // Func f = prod.func;
    // f.update(0)
    //     .specialize(prod_nrows >= vec_size && prod_ncols >= vec_size)
    //     .tile(i, j, b1_i, b1_j, i, j, vec_size, vec_size)
    //     .vectorize(i);

    // f.update(0)
    //     .specialize(prod_nrows >= vec_size && prod_ncols >= vec_size)
    //     .specialize(prod_nrows >= vec_size*tile_size && prod_ncols >= vec_size*tile_size)
    //     .tile(b1_i, b1_j, b2_i, b2_j, b1_i, b1_j, tile_size, tile_size);

    // f.update(0)
    //     .specialize(prod_nrows >= vec_size && prod_ncols >= vec_size)
    //     .specialize(prod_nrows >= vec_size*tile_size && prod_ncols >= vec_size*tile_size)
    //     .specialize(prod_nrows >= 2*vec_size*tile_size && prod_ncols >= 2*vec_size*tile_size)
    //     .tile(b2_i, b2_j, b3_i, b3_j, b2_i, b2_j, 2, 2)
    //     .parallel(b3_j);

    // f.update(0)
    //     .specialize(prod_nrows >= vec_size && prod_ncols >= vec_size)
    //     .rename(b1_i, b2_i)
    //     .rename(b1_j, b2_j);

    // f.update(0)
    //     .rename(i, b2_i)
    //     .rename(j, b2_j);

    // A.func.compute_at(f, b2_i);
    // B.func.compute_at(f, b2_i);

    return prod;

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
