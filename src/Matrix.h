#ifndef HALIDE_MATRIX_H
#define HALIDE_MATRIX_H

#ifdef WITH_EIGEN
# include <Eigen/Eigen>
#endif

#include "IR.h"
#include "Func.h"
#include "Function.h"
#include "Tuple.h"

namespace Halide {

class Matrix;

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
    Stage stage;
    Partition *prev;
    Partition *next;

    // Block variables, indexing the blocks at this level of the partition.
    Var  bi, bj;

    // The number of rows and columns per block in this partition.
    Expr par_rows, par_cols;

    // The minimum number of rows and columns we must have in the
    // matrix in order to use this partition.
    Expr min_rows, min_cols;

    // The number of rows and columns in the matrix that we are partitioning.
    Expr mat_rows, mat_cols;

    // A flag to indicate that the schedule for this partition is a specialization
    // of the root schedule.
    bool is_special;

    Partition(Partition *p, Expr m, Expr n);
public:
    Partition(Stage s, Expr m, Expr n);

    // Returns the level of this partition in the hierarchy.
    int level() const;

    // Returns the depth of the partition hierarchy that this partition belongs to.
    int depth() const;

    // Get a reference to Partition object representing a particular level of the hierachy 
    // this partition belongs to.
    Partition &get_level(int n);

    // Get a reference to the root Partition of the hierachy this partition belongs to.
    Partition &get_root() {return get_level(0);}

    // Get a reference to the leaf Partition of the hierachy this partition belongs to.
    Partition &get_leaf() {return get_level(depth()-1);}

    void rename_row(Var v);
    void rename_col(Var v);

    const std::string &name() const {return stage.name();}
    Partition split(Expr m, Expr n) {return Partition(&get_leaf(), m, n);}

    Stage schedule() {return stage;}
    Partition *parent() {return prev;}
    Partition *child() {return next;}
    bool is_root() const {return prev == NULL;}
    bool is_specialization() const {return is_special;}

    Expr num_rows() const {return par_rows;}
    Expr num_cols() const {return par_cols;}

    Var row_var() const {return bi;}
    Var col_var() const {return bj;}
};

class Matrix {
    /* For small matrices we store the coefficient Expr's directly. */
    std::vector<Expr> coeffs;

    /* For large matrices (m > 4 || n > 4) we simply wrap a Func. */
    Func func;

    /* We store a schedule here, which we can apply to the Func as
     * soon as we receive a compute_* call. */
    /* Internal::Schedule schedule; */

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

    /* Level of partitioning at which to parallelize operations. */
    int par_level;

    /* Returns the offset into the expression vector for small matrices. */
    int small_offset(Expr row, Expr col) const;

    void init(Expr num_row, Expr num_col);

    Stage root_schedule(int update=-1);

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
    EXPORT Matrix(const Eigen::MatrixBase<M>& mat);
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

    EXPORT Matrix &compute_at_rows(Matrix &other, int level = 0);
    EXPORT Matrix &compute_at_columns(Matrix &other, int level = 0);

    EXPORT Matrix &partition(Expr size);
    EXPORT Matrix &partition(Expr row_size, Expr col_size);
    EXPORT Matrix &vectorize(int level = -1);
    EXPORT Matrix &parallelize(int level = -1);

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

template<class M>
Matrix::Matrix(const Eigen::MatrixBase<M>& mat) {
    const int m = mat.rows();
    const int n = mat.cols();

    init(m, n);

    if (is_large) {
        Var i = row_var();
        Var j = col_var();
        func(i, j) = Internal::buildMatrixDef(mat, i, j);
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

}

#endif
