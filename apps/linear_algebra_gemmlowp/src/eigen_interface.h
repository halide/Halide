#ifndef EIGEN_INTERFACE_H
#define EIGEN_INTERFACE_H

#include <Eigen/Eigen>

// Gemmlowp interface for Eigen

namespace eigen {

typedef Eigen::Matrix<uint8_t, Eigen::Dynamic, 1> EigenVector;
typedef Eigen::Matrix<uint8_t, Eigen::Dynamic, Eigen::Dynamic> EigenMatrix;
typedef Eigen::Matrix<int32_t, Eigen::Dynamic, Eigen::Dynamic> EigenMatrix32i;

namespace {

#define assert_no_error(func)                                       \
  if (func != 0) {                                                  \
    std::cerr << "ERROR! Eigen returned non-zero value.\n";         \
  }                                                                 \

EigenMatrix convert_to_eigen_matrix(const int M, const int N, const uint8_t *A, const int lda) {
    EigenMatrix matrix(M, N);
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < M; ++i) {
            matrix(i, j) = A[i + lda*j];
        }
    }
    return matrix;
}

template<typename MatrixType>
inline void add_scalar(MatrixType& m, int scalar) {
    for (int j = 0; j < m.cols(); ++j) {
        for (int i = 0; i < m.rows(); ++i) {
            m(i, j) += scalar;
        }
    }
}

} // namespace anonymous

inline int eigen_igemm(bool transpose_A, bool transpose_B, bool transpose_C,
                       const EigenMatrix &A, int32_t a_offset, const EigenMatrix &B,
                       int32_t b_offset, EigenMatrix &C, int32_t c_offset,
                       int32_t c_mult_int, int32_t c_shift) {
    EigenMatrix32i A_int, B_int;
    if (transpose_A) {
        A_int = A.transpose().cast<int32_t>();
    } else {
        A_int = A.cast<int32_t>();
    }
    if (transpose_B) {
        B_int = B.transpose().cast<int32_t>();
    } else {
        B_int = B.cast<int32_t>();
    }

    add_scalar(A_int, a_offset);
    add_scalar(B_int, b_offset);

    EigenMatrix32i C_int = (A_int * B_int);
    add_scalar(C_int, c_offset);
    C_int *= c_mult_int;

    int32_t rounding_term = (c_shift < 1) ? 0 : (1 << (c_shift - 1));
    for (int j = 0; j < C_int.cols(); ++j) {
        for (int i = 0; i < C_int.rows(); ++i) {
            C_int(i, j) = (C_int(i, j) + rounding_term) >> c_shift;
            if (C_int(i, j) > 255) {
                C_int(i, j) = 255;
            }
            if (C_int(i, j) < 0) {
                C_int(i, j) = 0;
            }
        }
    }

    if (transpose_C) {
        C = C_int.transpose().cast<uint8_t>();
    } else {
        C = C_int.cast<uint8_t>();
    }

    return 0;
}


#ifdef __cplusplus
extern "C" {
#endif

/*
 * ===========================================================================
 * Gemmlowp interface for Eigen
 * ===========================================================================
 */

void eigen_igemm(bool transpose_A, bool transpose_B, bool transpose_C,
                 int M, int N, int K, const uint8_t *A,
                 int32_t a_offset, int lda, const uint8_t *B,
                 int32_t b_offset, int ldb, uint8_t *C,
                 int32_t c_offset, int32_t c_mult_int,
                 int32_t c_shift, int ldc) {
    EigenMatrix matrix_A, matrix_B, matrix_C;
    if (!transpose_A) {
        matrix_A = convert_to_eigen_matrix(M, K, A, lda);
    } else {
        matrix_A = convert_to_eigen_matrix(K, M, A, lda);
    }

    if (!transpose_B) {
        matrix_B = convert_to_eigen_matrix(K, N, B, ldb);
    } else {
        matrix_B = convert_to_eigen_matrix(N, K, B, ldb);
    }

    if (!transpose_C) {
        matrix_C = convert_to_eigen_matrix(M, N, C, ldc);
    } else {
        matrix_C = convert_to_eigen_matrix(N, M, C, ldc);
    }

    assert_no_error(eigen_igemm(transpose_A, transpose_B, transpose_C, matrix_A,
                                a_offset, matrix_B, b_offset, matrix_C, c_offset,
                                c_mult_int, c_shift));

    memcpy(C, matrix_C.data(), matrix_C.size() * sizeof(uint8_t));
}


#ifdef __cplusplus
}
#endif

} // namespace eigen

#endif  // EIGEN_INTERFACE_H
