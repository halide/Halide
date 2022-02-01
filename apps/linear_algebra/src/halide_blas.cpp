#include "halide_blas.h"
#include "HalideBuffer.h"
#include <iostream>
#include <string.h>

using Halide::Runtime::Buffer;

#define assert_no_error(func)                                               \
    do {                                                                    \
        if (func != 0) {                                                    \
            std::cerr << "ERROR! Halide kernel returned non-zero value.\n"; \
        }                                                                   \
    } while (0)

namespace {

template<typename T>
Buffer<T, 0> init_scalar_buffer(T *x) {
    return Buffer<T, 0>::make_scalar(x);
}

template<typename T>
Buffer<T, 1> init_vector_buffer(const int N, T *x, const int incx) {
    halide_dimension_t shape = {0, N, incx};
    return Buffer<T, 1>(x, 1, &shape);
}

template<typename T>
Buffer<T, 2> init_matrix_buffer(const int M, const int N, T *A, const int lda) {
    halide_dimension_t shape[] = {{0, M, 1}, {0, N, lda}};
    return Buffer<T, 2>(A, 2, shape);
}

}  // namespace

#ifdef __cplusplus
extern "C" {
#endif

//////////
// copy //
//////////

void hblas_scopy(const int N, const float *x, const int incx,
                 float *y, const int incy) {
    auto buff_x = init_vector_buffer(N, const_cast<float *>(x), incx);
    auto buff_y = init_vector_buffer(N, y, incy);
    assert_no_error(halide_scopy(buff_x, buff_y));
}

void hblas_dcopy(const int N, const double *x, const int incx,
                 double *y, const int incy) {
    auto buff_x = init_vector_buffer(N, const_cast<double *>(x), incx);
    auto buff_y = init_vector_buffer(N, y, incy);
    assert_no_error(halide_dcopy(buff_x, buff_y));
}

//////////
// scal //
//////////

void hblas_sscal(const int N, const float a, float *x, const int incx) {
    auto buff_x = init_vector_buffer(N, x, incx);
    assert_no_error(halide_sscal(a, buff_x));
}

void hblas_dscal(const int N, const double a, double *x, const int incx) {
    auto buff_x = init_vector_buffer(N, x, incx);
    assert_no_error(halide_dscal(a, buff_x));
}

//////////
// axpy //
//////////

void hblas_saxpy(const int N, const float a, const float *x, const int incx,
                 float *y, const int incy) {
    auto buff_x = init_vector_buffer(N, const_cast<float *>(x), incx);
    auto buff_y = init_vector_buffer(N, y, incy);
    assert_no_error(halide_saxpy(a, buff_x, buff_y));
}

void hblas_daxpy(const int N, const double a, const double *x, const int incx,
                 double *y, const int incy) {
    auto buff_x = init_vector_buffer(N, const_cast<double *>(x), incx);
    auto buff_y = init_vector_buffer(N, y, incy);
    assert_no_error(halide_daxpy(a, buff_x, buff_y));
}

//////////
// dot  //
//////////

float hblas_sdot(const int N, const float *x, const int incx,
                 const float *y, const int incy) {
    float result;
    auto buff_x = init_vector_buffer(N, const_cast<float *>(x), incx);
    auto buff_y = init_vector_buffer(N, const_cast<float *>(y), incy);
    auto buff_dot = init_scalar_buffer(&result);
    assert_no_error(halide_sdot(buff_x, buff_y, buff_dot));
    return result;
}

double hblas_ddot(const int N, const double *x, const int incx,
                  const double *y, const int incy) {
    double result;
    auto buff_x = init_vector_buffer(N, const_cast<double *>(x), incx);
    auto buff_y = init_vector_buffer(N, const_cast<double *>(y), incy);
    auto buff_dot = init_scalar_buffer(&result);
    assert_no_error(halide_ddot(buff_x, buff_y, buff_dot));
    return result;
}

//////////
// nrm2 //
//////////

float hblas_snrm2(const int N, const float *x, const int incx) {
    float result;
    auto buff_x = init_vector_buffer(N, const_cast<float *>(x), incx);
    auto buff_nrm = init_scalar_buffer(&result);
    assert_no_error(halide_sdot(buff_x, buff_x, buff_nrm));
    return std::sqrt(result);
}

double hblas_dnrm2(const int N, const double *x, const int incx) {
    double result;
    auto buff_x = init_vector_buffer(N, const_cast<double *>(x), incx);
    auto buff_nrm = init_scalar_buffer(&result);
    assert_no_error(halide_ddot(buff_x, buff_x, buff_nrm));
    return std::sqrt(result);
}

//////////
// asum //
//////////

float hblas_sasum(const int N, const float *x, const int incx) {
    float result;
    auto buff_x = init_vector_buffer(N, const_cast<float *>(x), incx);
    auto buff_sum = init_scalar_buffer(&result);
    assert_no_error(halide_sasum(buff_x, buff_sum));
    return result;
}

double hblas_dasum(const int N, const double *x, const int incx) {
    double result;
    auto buff_x = init_vector_buffer(N, const_cast<double *>(x), incx);
    auto buff_sum = init_scalar_buffer(&result);
    assert_no_error(halide_dasum(buff_x, buff_sum));
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
        t = false;
        break;
    case HblasConjTrans:
    case HblasTrans:
        t = true;
        break;
    };

    auto buff_A = init_matrix_buffer(M, N, const_cast<float *>(A), lda);
    auto buff_x = init_vector_buffer(t ? M : N, const_cast<float *>(x), incx);
    auto buff_y = init_vector_buffer(t ? N : M, y, incy);

    assert_no_error(halide_sgemv(t, a, buff_A, buff_x, b, buff_y));
}

void hblas_dgemv(const enum HBLAS_ORDER Order, const enum HBLAS_TRANSPOSE trans,
                 const int M, const int N, const double a, const double *A, const int lda,
                 const double *x, const int incx, const double b, double *y, const int incy) {
    bool t = false;
    switch (trans) {
    case HblasNoTrans:
        t = false;
        break;
    case HblasConjTrans:
    case HblasTrans:
        t = true;
        break;
    };

    auto buff_A = init_matrix_buffer(M, N, const_cast<double *>(A), lda);
    auto buff_x = init_vector_buffer(t ? M : N, const_cast<double *>(x), incx);
    auto buff_y = init_vector_buffer(t ? N : M, y, incy);

    assert_no_error(halide_dgemv(t, a, buff_A, buff_x, b, buff_y));
}

//////////
// ger  //
//////////

void hblas_sger(const enum HBLAS_ORDER order, const int M, const int N,
                const float alpha, const float *x, const int incx,
                const float *y, const int incy, float *A, const int lda) {
    auto buff_x = init_vector_buffer(M, const_cast<float *>(x), incx);
    auto buff_y = init_vector_buffer(N, const_cast<float *>(y), incy);
    auto buff_A = init_matrix_buffer(M, N, A, lda);

    assert_no_error(halide_sger(alpha, buff_x, buff_y, buff_A));
}

void hblas_dger(const enum HBLAS_ORDER order, const int M, const int N,
                const double alpha, const double *x, const int incx,
                const double *y, const int incy, double *A, const int lda) {
    auto buff_x = init_vector_buffer(M, const_cast<double *>(x), incx);
    auto buff_y = init_vector_buffer(N, const_cast<double *>(y), incy);
    auto buff_A = init_matrix_buffer(M, N, A, lda);

    assert_no_error(halide_dger(alpha, buff_x, buff_y, buff_A));
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
        tA = false;
        break;
    case HblasConjTrans:
    case HblasTrans:
        tA = true;
        break;
    };

    switch (TransB) {
    case HblasNoTrans:
        tB = false;
        break;
    case HblasConjTrans:
    case HblasTrans:
        tB = true;
        break;
    };

    auto buff_A = init_matrix_buffer(tA ? K : M, tA ? M : K, const_cast<float *>(A), lda);
    auto buff_B = init_matrix_buffer(tB ? N : K, tB ? K : N, const_cast<float *>(B), ldb);
    auto buff_C = init_matrix_buffer(M, N, C, ldc);

    assert_no_error(halide_sgemm(tA, tB, alpha, buff_A, buff_B, beta, buff_C));
}

void hblas_dgemm(const enum HBLAS_ORDER Order, const enum HBLAS_TRANSPOSE TransA,
                 const enum HBLAS_TRANSPOSE TransB, const int M, const int N,
                 const int K, const double alpha, const double *A,
                 const int lda, const double *B, const int ldb,
                 const double beta, double *C, const int ldc) {
    bool tA = false, tB = false;
    switch (TransA) {
    case HblasNoTrans:
        tA = false;
        break;
    case HblasConjTrans:
    case HblasTrans:
        tA = true;
        break;
    };

    switch (TransB) {
    case HblasNoTrans:
        tB = false;
        break;
    case HblasConjTrans:
    case HblasTrans:
        tB = true;
        break;
    };

    auto buff_A = init_matrix_buffer(tA ? K : M, tA ? M : K, const_cast<double *>(A), lda);
    auto buff_B = init_matrix_buffer(tB ? N : K, tB ? K : N, const_cast<double *>(B), ldb);
    auto buff_C = init_matrix_buffer(M, N, C, ldc);

    assert_no_error(halide_dgemm(tA, tB, alpha, buff_A, buff_B, beta, buff_C));
}

#ifdef __cplusplus
}
#endif
