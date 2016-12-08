#include <string.h>
#include <iostream>
#include "halide_blas.h"

#define assert_no_error(func)                                       \
  if (func != 0) {                                                  \
    std::cerr << "ERROR! Halide kernel returned non-zero value.\n"; \
  }                                                                 \

namespace {

void init_scalar_buffer(const float *x, halide_nd_buffer_t<1> *buff) {
    buff->host = (uint8_t*)const_cast<float*>(x);
    buff->dim[0] = halide_dimension_t(0, 1, 1);
    buff->type = halide_type_of<float>();
}

void init_scalar_buffer(const double *x, halide_nd_buffer_t<1> *buff) {
    buff->host = (uint8_t*)const_cast<double*>(x);
    buff->dim[0] = halide_dimension_t(0, 1, 1);
    buff->type = halide_type_of<double>();
}

void init_vector_buffer(const int N, const float *x, const int incx, halide_nd_buffer_t<1> *buff) {
    buff->host = (uint8_t*)const_cast<float*>(x);
    buff->dim[0] = halide_dimension_t(0, N, incx);
    buff->type = halide_type_of<float>();
}

void init_vector_buffer(const int N, const double *x, const int incx, halide_nd_buffer_t<1> *buff) {
    buff->host = (uint8_t*)const_cast<double*>(x);
    buff->dim[0] = halide_dimension_t(0, N, incx);
    buff->type = halide_type_of<double>();
}

void init_matrix_buffer(const int M, const int N, const float *A, const int lda, halide_nd_buffer_t<2> *buff) {
    buff->host = (uint8_t*)const_cast<float*>(A);
    buff->dim[0] = halide_dimension_t(0, M, 1);
    buff->dim[1] = halide_dimension_t(0, N, lda);
    buff->type = halide_type_of<float>();
}

void init_matrix_buffer(const int M, const int N, const double *A, const int lda, halide_nd_buffer_t<2> *buff) {
    buff->host = (uint8_t*)const_cast<double*>(A);
    buff->dim[0] = halide_dimension_t(0, M, 1);
    buff->dim[1] = halide_dimension_t(0, N, lda);
    buff->type = halide_type_of<double>();
}

}

#ifdef __cplusplus
extern "C" {
#endif

//////////
// copy //
//////////

void hblas_scopy(const int N, const float *x, const int incx,
                 float *y, const int incy) {
    halide_nd_buffer_t<1> buff_x, buff_y;
    init_vector_buffer(N, x, incx, &buff_x);
    init_vector_buffer(N, y, incy, &buff_y);
    assert_no_error(halide_scopy(&buff_x, &buff_y));
}

void hblas_dcopy(const int N, const double *x, const int incx,
                 double *y, const int incy) {
    halide_nd_buffer_t<1> buff_x, buff_y;
    init_vector_buffer(N, x, incx, &buff_x);
    init_vector_buffer(N, y, incy, &buff_y);
    assert_no_error(halide_dcopy(&buff_x, &buff_y));
}

//////////
// scal //
//////////

void hblas_sscal(const int N, const float a, float *x, const int incx) {
    halide_nd_buffer_t<1> buff_x;
    init_vector_buffer(N, x, incx, &buff_x);
    assert_no_error(halide_sscal(a, &buff_x));
}

void hblas_dscal(const int N, const double a, double *x, const int incx) {
    halide_nd_buffer_t<1> buff_x;
    init_vector_buffer(N, x, incx, &buff_x);
    assert_no_error(halide_dscal(a, &buff_x));
}

//////////
// axpy //
//////////

void hblas_saxpy(const int N, const float a, const float *x, const int incx,
                 float *y, const int incy) {
    halide_nd_buffer_t<1> buff_x, buff_y;
    init_vector_buffer(N, x, incx, &buff_x);
    init_vector_buffer(N, y, incy, &buff_y);
    assert_no_error(halide_saxpy(a, &buff_x, &buff_y));
}

void hblas_daxpy(const int N, const double a, const double *x, const int incx,
                 double *y, const int incy) {
    halide_nd_buffer_t<1> buff_x, buff_y;
    init_vector_buffer(N, x, incx, &buff_x);
    init_vector_buffer(N, y, incy, &buff_y);
    assert_no_error(halide_daxpy(a, &buff_x, &buff_y));
}

//////////
// dot  //
//////////

float hblas_sdot(const int N, const float *x, const int incx,
                 const float *y, const int incy) {
    float result;
    halide_nd_buffer_t<1> buff_x, buff_y, buff_dot;
    init_vector_buffer(N, x, incx, &buff_x);
    init_vector_buffer(N, y, incy, &buff_y);
    init_scalar_buffer(&result, &buff_dot);
    assert_no_error(halide_sdot(&buff_x, &buff_y, &buff_dot));
    return result;
}

double hblas_ddot(const int N, const double *x, const int incx,
                  const double *y, const int incy) {
    double result;
    halide_nd_buffer_t<1> buff_x, buff_y, buff_dot;
    init_vector_buffer(N, x, incx, &buff_x);
    init_vector_buffer(N, y, incy, &buff_y);
    init_scalar_buffer(&result, &buff_dot);
    assert_no_error(halide_ddot(&buff_x, &buff_y, &buff_dot));
    return result;
}

//////////
// nrm2 //
//////////

float hblas_snrm2(const int N, const float *x, const int incx) {
    float result;
    halide_nd_buffer_t<1> buff_x, buff_nrm;
    init_vector_buffer(N, x, incx, &buff_x);
    init_scalar_buffer(&result, &buff_nrm);
    assert_no_error(halide_sdot(&buff_x, &buff_x, &buff_nrm));
    return std::sqrt(result);
}

double hblas_dnrm2(const int N, const double *x, const int incx) {
    double result;
    halide_nd_buffer_t<1> buff_x, buff_nrm;
    init_vector_buffer(N, x, incx, &buff_x);
    init_scalar_buffer(&result, &buff_nrm);
    assert_no_error(halide_ddot(&buff_x, &buff_x, &buff_nrm));
    return std::sqrt(result);
}

//////////
// asum //
//////////

float hblas_sasum(const int N, const float *x, const int incx) {
    float result;
    halide_nd_buffer_t<1> buff_x, buff_sum;
    init_vector_buffer(N, x, incx, &buff_x);
    init_scalar_buffer(&result, &buff_sum);
    assert_no_error(halide_sasum(&buff_x, &buff_sum));
    return result;
}

double hblas_dasum(const int N, const double *x, const int incx) {
    double result;
    halide_nd_buffer_t<1> buff_x, buff_sum;
    init_vector_buffer(N, x, incx, &buff_x);
    init_scalar_buffer(&result, &buff_sum);
    assert_no_error(halide_dasum(&buff_x, &buff_sum));
    return result;
}

//////////
// gemv //
//////////

void hblas_sgemv(const enum HBLAS_ORDER Order, const enum HBLAS_TRANSPOSE trans,
                 const int M, const int N, const float a, const float *A, const int lda,
                 const float *x, const int incx, const float b, float *y, const int incy) {
    bool t = false;
    switch (trans) {
    case HblasNoTrans:
        t = false; break;
    case HblasConjTrans:
    case HblasTrans:
        t = true; break;
    };

    halide_nd_buffer_t<2> buff_A;
    halide_nd_buffer_t<1> buff_x, buff_y;
    init_matrix_buffer(M, N, A, lda, &buff_A);
    if (t) {
        init_vector_buffer(M, x, incx, &buff_x);
        init_vector_buffer(N, y, incy, &buff_y);
    } else {
        init_vector_buffer(N, x, incx, &buff_x);
        init_vector_buffer(M, y, incy, &buff_y);
    }

    assert_no_error(halide_sgemv(t, a, &buff_A, &buff_x, b, &buff_y));
}

void hblas_dgemv(const enum HBLAS_ORDER Order, const enum HBLAS_TRANSPOSE trans,
                 const int M, const int N, const double a, const double *A, const int lda,
                 const double *x, const int incx, const double b, double *y, const int incy) {
    bool t = false;
    switch (trans) {
    case HblasNoTrans:
        t = false; break;
    case HblasConjTrans:
    case HblasTrans:
        t = true; break;
    };

    halide_nd_buffer_t<2> buff_A;
    halide_nd_buffer_t<1> buff_x, buff_y;
    init_matrix_buffer(M, N, A, lda, &buff_A);
    if (t) {
        init_vector_buffer(M, x, incx, &buff_x);
        init_vector_buffer(N, y, incy, &buff_y);
    } else {
        init_vector_buffer(N, x, incx, &buff_x);
        init_vector_buffer(M, y, incy, &buff_y);
    }

    assert_no_error(halide_dgemv(t, a, &buff_A, &buff_x, b, &buff_y));
}

//////////
// ger  //
//////////

void hblas_sger(const enum HBLAS_ORDER order, const int M, const int N,
                const float alpha, const float *x, const int incx,
                const float *y, const int incy, float *A, const int lda)
{
    halide_nd_buffer_t<2> buff_A;
    halide_nd_buffer_t<1> buff_x, buff_y;
    init_vector_buffer(M, x, incx, &buff_x);
    init_vector_buffer(N, y, incy, &buff_y);
    init_matrix_buffer(M, N, A, lda, &buff_A);

    assert_no_error(halide_sger(alpha, &buff_x, &buff_y, &buff_A));
}

void hblas_dger(const enum HBLAS_ORDER order, const int M, const int N,
                const double alpha, const double *x, const int incx,
                const double *y, const int incy, double *A, const int lda)
{
    halide_nd_buffer_t<2> buff_A;
    halide_nd_buffer_t<1> buff_x, buff_y;
    init_vector_buffer(M, x, incx, &buff_x);
    init_vector_buffer(N, y, incy, &buff_y);
    init_matrix_buffer(M, N, A, lda, &buff_A);

    assert_no_error(halide_dger(alpha, &buff_x, &buff_y, &buff_A));
}

//////////
// gemm //
//////////

void hblas_sgemm(const enum HBLAS_ORDER Order, const enum HBLAS_TRANSPOSE TransA,
                 const enum HBLAS_TRANSPOSE TransB, const int M, const int N,
                 const int K, const float alpha, const float *A,
                 const int lda, const float *B, const int ldb,
                 const float beta, float *C, const int ldc) {
    bool tA = false, tB = false;
    switch (TransA) {
    case HblasNoTrans:
        tA = false; break;
    case HblasConjTrans:
    case HblasTrans:
        tA = true; break;
    };

    switch (TransB) {
    case HblasNoTrans:
        tB = false; break;
    case HblasConjTrans:
    case HblasTrans:
        tB = true; break;
    };

    halide_nd_buffer_t<2> buff_A, buff_B, buff_C;
    if (!tA) {
        init_matrix_buffer(M, K, A, lda, &buff_A);
    } else {
        init_matrix_buffer(K, M, A, lda, &buff_A);
    }

    if (!tB) {
        init_matrix_buffer(K, N, B, ldb, &buff_B);
    } else {
        init_matrix_buffer(N, K, B, ldb, &buff_B);
    }

    init_matrix_buffer(M, N, C, ldc, &buff_C);

    assert_no_error(halide_sgemm(tA, tB, alpha, &buff_A, &buff_B, beta, &buff_C));
}

void hblas_dgemm(const enum HBLAS_ORDER Order, const enum HBLAS_TRANSPOSE TransA,
                 const enum HBLAS_TRANSPOSE TransB, const int M, const int N,
                 const int K, const double alpha, const double *A,
                 const int lda, const double *B, const int ldb,
                 const double beta, double *C, const int ldc) {
    bool tA = false, tB = false;
    switch (TransA) {
    case HblasNoTrans:
        tA = false; break;
    case HblasConjTrans:
    case HblasTrans:
        tA = true; break;
    };

    switch (TransB) {
    case HblasNoTrans:
        tB = false; break;
    case HblasConjTrans:
    case HblasTrans:
        tB = true; break;
    };

    halide_nd_buffer_t<2> buff_A, buff_B, buff_C;
    if (!tA) {
        init_matrix_buffer(M, K, A, lda, &buff_A);
    } else {
        init_matrix_buffer(K, M, A, lda, &buff_A);
    }

    if (!tB) {
        init_matrix_buffer(K, N, B, ldb, &buff_B);
    } else {
        init_matrix_buffer(N, K, B, ldb, &buff_B);
    }

    init_matrix_buffer(M, N, C, ldc, &buff_C);

    assert_no_error(halide_dgemm(tA, tB, alpha, &buff_A, &buff_B, beta, &buff_C));
}


#ifdef __cplusplus
}
#endif
