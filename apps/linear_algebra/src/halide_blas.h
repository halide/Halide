#ifndef HALIDE_BLAS_H
#define HALIDE_BLAS_H

#include <cmath>

#include "halide_scopy_impl.h"
#include "halide_dcopy_impl.h"
#include "halide_sscal_impl.h"
#include "halide_dscal_impl.h"
#include "halide_saxpy_impl.h"
#include "halide_daxpy_impl.h"
#include "halide_sdot.h"
#include "halide_ddot.h"
#include "halide_sasum.h"
#include "halide_dasum.h"
#include "halide_sgemv_notrans.h"
#include "halide_dgemv_notrans.h"
#include "halide_sgemv_trans.h"
#include "halide_dgemv_trans.h"

inline int halide_scopy(buffer_t *x, buffer_t *y) {
  return halide_scopy_impl(0, x, nullptr, y);
}

inline int halide_dcopy(buffer_t *x, buffer_t *y) {
  return halide_dcopy_impl(0, x, nullptr, y);
}

inline int halide_sscal(float a, buffer_t *x) {
  return halide_sscal_impl(a, x, nullptr, x);
}

inline int halide_dscal(double a, buffer_t *x) {
  return halide_dscal_impl(a, x, nullptr, x);
}

inline int halide_saxpy(float a, buffer_t *x, buffer_t *y) {
  return halide_saxpy_impl(a, x, y, y);
}

inline int halide_daxpy(double a, buffer_t *x, buffer_t *y) {
  return halide_daxpy_impl(a, x, y, y);
}

inline int halide_sgemv(bool trans, float a, buffer_t *A, buffer_t *x, float b, buffer_t *y) {
  if (trans) {
    return halide_sgemv_trans(a, A, x, b, y, y);
  } else {
    return halide_sgemv_notrans(a, A, x, b, y, y);
  }
}

inline int halide_dgemv(bool trans, double a, buffer_t *A, buffer_t *x, double b, buffer_t *y) {
  if (trans) {
    return halide_dgemv_trans(a, A, x, b, y, y);
  } else {
    return halide_dgemv_notrans(a, A, x, b, y, y);
  }
}

enum HBLAS_ORDER {HblasRowMajor=101, HblasColMajor=102};
enum HBLAS_TRANSPOSE {HblasNoTrans=111, HblasTrans=112, HblasConjTrans=113};
enum HBLAS_UPLO {HblasUpper=121, HblasLower=122};
enum HBLAS_DIAG {HblasNonUnit=131, HblasUnit=132};
enum HBLAS_SIDE {HblasLeft=141, HblasRight=142};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ===========================================================================
 * Prototypes for level 1 BLAS functions (complex are recast as routines)
 * ===========================================================================
 */
// float  hblas_sdsdot(const int N, const float alpha, const float *X,
//                     const int incX, const float *Y, const int incY);
// double hblas_dsdot(const int N, const float *X, const int incX, const float *Y,
//                    const int incY);
float  hblas_sdot(const int N, const float  *X, const int incX,
                  const float  *Y, const int incY);
double hblas_ddot(const int N, const double *X, const int incX,
                  const double *Y, const int incY);


/*
 * Functions having prefixes S D SC DZ
 */
float  hblas_snrm2(const int N, const float *X, const int incX);
float  hblas_sasum(const int N, const float *X, const int incX);

double hblas_dnrm2(const int N, const double *X, const int incX);
double hblas_dasum(const int N, const double *X, const int incX);


/*
 * Functions having standard 4 prefixes (S D C Z)
 */
// HBLAS_INDEX hblas_isamax(const int N, const float  *X, const int incX);
// HBLAS_INDEX hblas_idamax(const int N, const double *X, const int incX);
// HBLAS_INDEX hblas_icamax(const int N, const void   *X, const int incX);
// HBLAS_INDEX hblas_izamax(const int N, const void   *X, const int incX);

/*
 * ===========================================================================
 * Prototypes for level 1 BLAS routines
 * ===========================================================================
 */

/*
 * Routines with standard 4 prefixes (s, d, c, z)
 */
// void hblas_sswap(const int N, float *X, const int incX,
//                  float *Y, const int incY);
void hblas_scopy(const int N, const float *X, const int incX,
                 float *Y, const int incY);
void hblas_saxpy(const int N, const float alpha, const float *X,
                 const int incX, float *Y, const int incY);

// void hblas_dswap(const int N, double *X, const int incX,
//                  double *Y, const int incY);
void hblas_dcopy(const int N, const double *X, const int incX,
                 double *Y, const int incY);
void hblas_daxpy(const int N, const double alpha, const double *X,
                 const int incX, double *Y, const int incY);


/*
 * Routines with S and D prefix only
 */
// void hblas_srotg(float *a, float *b, float *c, float *s);
// void hblas_srotmg(float *d1, float *d2, float *b1, const float b2, float *P);
// void hblas_srot(const int N, float *X, const int incX,
//                 float *Y, const int incY, const float c, const float s);
// void hblas_srotm(const int N, float *X, const int incX,
//                 float *Y, const int incY, const float *P);

// void hblas_drotg(double *a, double *b, double *c, double *s);
// void hblas_drotmg(double *d1, double *d2, double *b1, const double b2, double *P);
// void hblas_drot(const int N, double *X, const int incX,
//                 double *Y, const int incY, const double c, const double  s);
// void hblas_drotm(const int N, double *X, const int incX,
//                 double *Y, const int incY, const double *P);


/*
 * Routines with S D C Z CS and ZD prefixes
 */
void hblas_sscal(const int N, const float alpha, float *X, const int incX);
void hblas_dscal(const int N, const double alpha, double *X, const int incX);

/*
 * ===========================================================================
 * Prototypes for level 2 BLAS
 * ===========================================================================
 */

/*
 * Routines with standard 4 prefixes (S, D, C, Z)
 */
void hblas_sgemv(/*const enum HBLAS_ORDER order,*/
                 const enum HBLAS_TRANSPOSE TransA, const int M, const int N,
                 const float alpha, const float *A, const int lda,
                 const float *X, const int incX, const float beta,
                 float *Y, const int incY);

void hblas_dgemv(/*const enum HBLAS_ORDER order,*/
                 const enum HBLAS_TRANSPOSE TransA, const int M, const int N,
                 const double alpha, const double *A, const int lda,
                 const double *X, const int incX, const double beta,
                 double *Y, const int incY);

#ifdef __cplusplus
}
#endif

#endif  // HALIDE_BLAS_H
