#ifndef HALIDE_MATRIX_H
#define HALIDE_MATRIX_H

#ifdef WITH_EIGEN
# include <Eigen/Eigen>
#endif

#include "IR.h"
#include "Func.h"

namespace Halide {

class Matrix;

/** A fragment of front-end syntax of the form f(x, y, z), where x, y,
 * z are Exprs. If could be the left hand side of a reduction
 * definition, or it could be a call to a function. We don't know
 * until we see how this object gets used.
 */
class MatrixRef {
    Matrix& mat;
    Expr row;
    Expr col;
  public:
    MatrixRef(Matrix& M, Expr i, Expr j);

    /** Use this as the left-hand-side of a reduction definition (see
     * \ref RDom). The function must already have a pure definition.
     */
    EXPORT void operator=(Expr);

    /** Define this function as a sum reduction over the negative of
     * the given expression. The expression should refer to some RDom
     * to sum over. If the function does not already have a pure
     * definition, this sets it to zero.
     */
    EXPORT void operator+=(Expr);

    /** Define this function as a sum reduction over the given
     * expression. The expression should refer to some RDom to sum
     * over. If the function does not already have a pure definition,
     * this sets it to zero.
     */
    EXPORT void operator-=(Expr);

    /** Define this function as a product reduction. The expression
     * should refer to some RDom to take the product over. If the
     * function does not already have a pure definition, this sets it
     * to 1.
     */
    EXPORT void operator*=(Expr);

    /** Define this function as the product reduction over the inverse
     * of the expression. The expression should refer to some RDom to
     * take the product over. If the function does not already have a
     * pure definition, this sets it to 1.
     */
    EXPORT void operator/=(Expr);

    /* Override the usual assignment operator, so that
     * f(x, y) = g(x, y) defines f.
     */
    // @{
    EXPORT void operator=(const MatrixRef &);
    EXPORT void operator=(const FuncRefVar &);
    EXPORT void operator=(const FuncRefExpr &);
    // @}

    /** Use this as a call to the function, and not the left-hand-side
     * of a definition. Only works for single-output Funcs. */
    EXPORT operator Expr() const;
};

class Matrix {
    /* For small matrices we store the coefficient Expr's directly. */
    std::vector<Expr> coeffs;

    /* For large matrices (m > 4 || n > 4) we simply wrap a Func. */
    Func func;

    /* Variables for accessing the function as a matrix. */
    Var x, y;

    /* A flag indicating if we should use the function representation or
     * the coefficient representation. */
    bool is_large;

    /* Number of rows in the matrix. */
    Expr nrows;

    /* Number of columns in the matrix. */
    Expr ncols;

    /* Returns the offset into the expression vector for small matrices. */
    int small_offset(Expr row, Expr col) const;

    friend class MatrixRef;

    friend Matrix operator+(Matrix, Matrix);
    friend Matrix operator-(Matrix, Matrix);
    friend Matrix operator*(Matrix, Matrix);
    friend Matrix operator*(Expr, Matrix);
    friend Matrix operator*(Matrix, Expr);
    friend Matrix operator/(Matrix, Expr);
  public:
    EXPORT Matrix();
    EXPORT Matrix(Expr m, Expr n, Type t);
    EXPORT Matrix(Expr m, Expr n, Func f);
    EXPORT Matrix(Expr m, Expr n, const std::vector<Expr>& c);
    EXPORT Matrix(ImageParam img);

#ifdef WITH_EIGEN
    template<class M>
    EXPORT Matrix(const Eigen::MatrixBase<M>& mat);
#endif

    EXPORT Type type() const;
    EXPORT Func function();
    EXPORT Buffer realize();

    EXPORT Expr num_rows() const;
    EXPORT Expr num_cols() const;

    EXPORT Matrix row(Expr i);
    EXPORT Matrix col(Expr j);
    EXPORT Matrix block(Expr min_i, Expr max_i, Expr min_j, Expr max_j);

    EXPORT Matrix transpose();

    /*
      These operations are only available for small nxn matrices, i.e.
      n = 2, 3, or 4.
     */
    // @{
    EXPORT Expr cofactor(int i, int j);
    EXPORT Expr determinant();
    EXPORT Matrix inverse();
    // @}

    EXPORT MatrixRef operator[] (Expr i);
    EXPORT MatrixRef operator() (Expr i, Expr j);
};

EXPORT Matrix identity_matrix(Type, Expr);

template<typename T>
EXPORT Matrix identity_matrix(Expr size) {
    return identity_matrix(type_of<T>(), size);
}

EXPORT Matrix operator+(Matrix, Matrix);
EXPORT Matrix operator-(Matrix, Matrix);
EXPORT Matrix operator*(Matrix, Matrix);
EXPORT Matrix operator*(Expr, Matrix);
EXPORT Matrix operator*(Matrix, Expr);
EXPORT Matrix operator/(Matrix, Expr);

#ifdef WITH_EIGEN
namespace Internal {
template<class M>
Expr buildMatrixDef(const Eigen::MatrixBase<M>& mat, const Var x, const Var y, const int i=0, const int j=0) {
    if (i == mat.rows()-1 && j == mat.cols()-1) {
        return mat(i,j);
    } else {
        const int next_i = i < mat.rows()-1? i+1 : 0;
        const int next_j = next_i == 0? j+1 : j;

        return select(x == i && y == j, mat(i,j),
                      buildMatrixDef(mat, x, y, next_i, next_j));
    }
}

}  // namespace Internal

template<class M>
Matrix::Matrix(const Eigen::MatrixBase<M>& mat) {
    const int m = mat.rows();
    const int n = mat.cols();

    nrows = Expr(m);
    ncols = Expr(n);

    if (m <= 4 && n <= 4) {
        is_large = false;
        coeffs.resize(m * n);
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < m; ++i) {
                const int idx = small_offset(i, j);
                coeffs[idx] = mat(i, j);
            }
        }
    } else {
        is_large = true;

        x = Var("x");
        y = Var("y");

        func(x, y) = Internal::buildMatrixDef(mat, x, y);
        // func.bound(x, 0, nrows)
        //     .bound(y, 0, ncols);
    }
}
#endif

}

#endif
