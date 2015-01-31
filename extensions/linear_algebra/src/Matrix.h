#ifndef HALIDE_MATRIX_H
#define HALIDE_MATRIX_H

#ifdef WITH_EIGEN
# include <Eigen/Eigen>
#endif

#include "IR.h"
#include "Func.h"
#include "Function.h"
#include "IntrusivePtr.h"
#include "Schedule.h"
#include "Tuple.h"

namespace Halide {
namespace LinearAlgebra {

class Matrix;
class MatrixRef;
class Partition;
class PartitionContents;

std::vector<std::string> matrix_args(Matrix &M);
std::string matrix_name(Matrix *M, std::string name, std::string alt_name = "");

/** A fragment of front-end syntax of the form A(i, j), where i, and j
 * are Exprs. It could be the left hand side of a reduction
 * definition, or it could be a call to a matrix. We don't know
 * until we see how this object gets used.
 */
class MatrixRef {
    Matrix& mat;
    Expr row;
    Expr col;
public:
    MatrixRef(LinearAlgebra::Matrix& M, Expr i, Expr j);

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

/**
 * A Partition defines a decomposition of a Matrix into a hierarchy of
 * blocks that is used for scheduling matrix computations. A Matrix
 * can be defined in several update steps and each one can have it's
 * own partitioning.
 *
 * The Partition class automatically manages the creation of
 * specializations in the Matrix schedule. If an (m x n) partition is
 * requested on a matrix whose size is smaller than (m x n) the matrix
 * computations will not be tiled.
 */
class Partition {
    Internal::IntrusivePtr<PartitionContents>  contents;

    friend class Matrix;
public:
    // Create a root partition.
    Partition(Internal::Schedule schedule, const std::string &name, Expr m, Expr n);

    // Create a partition that splits an existing partition into blocks of size m x n.
    Partition(Partition p, Expr m, Expr n);

    // Create a partition from known contents.
    Partition(Internal::IntrusivePtr<PartitionContents> c);

    // Returns the level of this partition in the hierarchy.
    int level();

    // Returns the depth of the partition hierarchy that this partition belongs to.
    int depth();

    // Get a reference to Partition object representing a particular level of the hierachy
    // this partition belongs to.
    Partition get_level(int n);

    // Get a reference to the root Partition of the hierachy this partition belongs to.
    Partition get_root();

    // Get a reference to the leaf Partition of the hierachy this partition belongs to.
    Partition get_leaf();

    const std::string &name() const;

    Stage schedule();

    Partition parent();
    Partition child();

    bool is_root() const;

    Expr num_rows() const;
    Expr num_cols() const;

    Var row_var() const;
    Var col_var() const;

    void rename_row(Var v);
    void rename_col(Var v);

    Partition partition(Expr n);
    Partition partition(Expr m, Expr n);

    Partition &vectorize();
    Partition &unroll_rows();
    Partition &unroll_cols();
    Partition &parallel_rows();
    Partition &parallel_cols();
};

class Matrix {
    /* For small matrices we store the coefficient Expr's directly. */
    std::vector<Expr> coeffs;

    /* For large matrices (m > 4 || n > 4) we express a matrix via a Function. */
    Internal::Function func;

    /* Variables for accessing the function as a matrix. */
    Var ij[2];

    /* A flag indicating if we should use the function representation or
     * the coefficient representation. */
    bool is_large;

    /* Number of rows in the matrix. */
    Expr nrows;

    /* Number of columns in the matrix. */
    Expr ncols;

    /* Vector of partitions that have been applied to this matrix. We
     * store a partition per update step of the matrix definition. */
    std::vector<Partition> partitions;

    /* Level of partitioning at which to vectorize operations. */
    int vec_level;

    /* Vectors storing the loop types for the rows and columns of each level of the partition. */
    std::vector<Internal::For::ForType> row_loop_types;
    std::vector<Internal::For::ForType> col_loop_types;

    /* Returns the offset into the expression vector for small matrices. */
    int small_offset(Expr row, Expr col) const;

    void init(Expr num_rows, Expr num_cols);
    void define(Expr value);
    void define_update(Expr row, Expr col, Expr value);

    // Stage root_schedule(int update=-1);

    friend class MatrixRef;
    friend class Partition;

    friend Matrix operator+(Matrix, Matrix);
    friend Matrix operator-(Matrix, Matrix);
    friend Matrix operator*(Matrix, Matrix);
    friend Matrix operator*(Expr, Matrix);
    friend Matrix operator*(Matrix, Expr);
    friend Matrix operator/(Matrix, Expr);
public:
    EXPORT Matrix(std::string name = "");
    EXPORT Matrix(Expr m, Expr n, Type t, std::string name = "");
    EXPORT Matrix(Expr m, Expr n, Func f, std::string name = "");
    EXPORT Matrix(Expr m, Expr n, Tuple c, std::string name = "");
    EXPORT Matrix(Expr m, Expr n, std::vector<Expr> c, std::string name = "");
    EXPORT Matrix(ImageParam img, std::string name = "");

#ifdef WITH_EIGEN
    template<class M>
    EXPORT Matrix(const Eigen::MatrixBase<M>& mat, std::string name = "");
#endif

    EXPORT bool is_large_matrix() const {return is_large;}
    EXPORT std::string name() const {return func.name();}

    EXPORT Var row_var() const;
    EXPORT Var col_var() const;

    EXPORT Type type() const;
    // EXPORT Buffer realize();

    EXPORT Expr num_rows() const;
    EXPORT Expr num_cols() const;

    EXPORT bool const_num_rows(int &m);
    EXPORT bool const_num_cols(int &n);
    EXPORT bool const_size(int &m, int &n);

    EXPORT Matrix &compute_at_rows(Partition p);
    EXPORT Matrix &compute_at_columns(Partition p);

    EXPORT Matrix &partition(Expr size);
    EXPORT Matrix &partition(Expr row_size, Expr col_size);
    EXPORT Matrix &vectorize(int level = -1);
    EXPORT Matrix &unroll_rows(int level = -1);
    EXPORT Matrix &unroll_cols(int level = -1);
    EXPORT Matrix &parallel_rows(int level = -1);
    EXPORT Matrix &parallel_cols(int level = -1);

    EXPORT Partition &get_partition(int update = 0);
    EXPORT const Partition &get_partition(int update = 0) const;

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

    EXPORT Realization realize(const Target &target = get_jit_target_from_environment());

    operator Tuple();
    operator Func();

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
}  // namespace LinearAlgebra

namespace Internal {
template<class M>
Expr buildMatrixDef(const Eigen::MatrixBase<M>& mat,
                    const Var x, const Var y,
                    const int i=0, const int j=0) {
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


namespace LinearAlgebra {

template<class M>
Matrix::Matrix(const Eigen::MatrixBase<M>& mat, std::string name)
        : func(matrix_name(this, name)) {
    const int m = mat.rows();
    const int n = mat.cols();

    init(m, n);

    if (is_large) {
        Matrix &A = *this;
        A(row_var(), col_var()) = Internal::buildMatrixDef(mat, row_var(), col_var());
    } else {
        coeffs.resize(m * n);
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < m; ++i) {
                const int idx = small_offset(i, j);
                coeffs[idx] = mat(i, j);
            }
        }
    }
}
#endif

}  // namespace LinearAlgebra
}  // namespace Halide

#endif
