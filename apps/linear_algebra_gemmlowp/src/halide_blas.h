#ifndef HALIDE_BLAS_H
#define HALIDE_BLAS_H

#include <cmath>

#include "HalideRuntime.h"
#include "halide_igemm_notrans.h"
#include "halide_igemm_transA.h"
#include "halide_igemm_transB.h"
#include "halide_igemm_transAB.h"
#include "halide_igemm_transC.h"
#include "halide_igemm_transAC.h"
#include "halide_igemm_transBC.h"
#include "halide_igemm_transABC.h"

inline int halide_igemm(bool transA, bool transB, bool transC, buffer_t *A,
                        int32_t a_offset, buffer_t *B, int32_t b_offset, buffer_t *C,
                        int32_t c_offset, int32_t c_mult_int, int32_t c_shift) {
    if (transA && transB && transC) {
        return halide_igemm_transABC(A, a_offset, B, b_offset, C, c_offset, c_mult_int, c_shift, C);
    } else if (transA && transB) {
        return halide_igemm_transAB(A, a_offset, B, b_offset, C, c_offset, c_mult_int, c_shift, C);
    } else if (transA && transC) {
        return halide_igemm_transAC(A, a_offset, B, b_offset, C, c_offset, c_mult_int, c_shift, C);
    } else if (transB && transC) {
        return halide_igemm_transBC(A, a_offset, B, b_offset, C, c_offset, c_mult_int, c_shift, C);
    } else if (transA) {
        return halide_igemm_transA(A, a_offset, B, b_offset, C, c_offset, c_mult_int, c_shift, C);
    } else if (transB) {
        return halide_igemm_transB(A, a_offset, B, b_offset, C, c_offset, c_mult_int, c_shift, C);
    } else if (transC) {
        return halide_igemm_transC(A, a_offset, B, b_offset, C, c_offset, c_mult_int, c_shift, C);
    } else {
        return halide_igemm_notrans(A, a_offset, B, b_offset, C, c_offset, c_mult_int, c_shift, C);
    }
    return -1;
}

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ===========================================================================
 * Prototypes for level 3 BLAS
 * ===========================================================================
 */

void hblas_igemm(bool transpose_A, bool transpose_B, bool transpose_C,
                 int M, int N, int K, const uint8_t *A,
                 int32_t a_offset, int lda, const uint8_t *B,
                 int32_t b_offset, int ldb, uint8_t *C,
                 int32_t c_offset, int32_t c_mult_int,
                 int32_t c_shift, int ldc);


#ifdef __cplusplus
}
#endif

#endif  // HALIDE_BLAS_H
