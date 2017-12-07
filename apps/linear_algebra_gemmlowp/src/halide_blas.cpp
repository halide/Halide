#include <string.h>
#include <iostream>
#include "halide_blas.h"

#define assert_no_error(func)                                       \
  if (func != 0) {                                                  \
    std::cerr << "ERROR! Halide kernel returned non-zero value.\n"; \
  }                                                                 \

namespace {

void init_matrix_buffer(const int M, const int N, const uint8_t *A, const int lda, buffer_t *buff) {
    memset((void*)buff, 0, sizeof(buffer_t));
    buff->host = (uint8_t*)const_cast<uint8_t*>(A);
    buff->extent[0] = M;
    buff->extent[1] = N;
    buff->stride[0] = 1;
    buff->stride[1] = lda;
    buff->elem_size = sizeof(uint8_t);
}

}

#ifdef __cplusplus
extern "C" {
#endif

//////////
// gemm //
//////////

void hblas_igemm(bool transpose_A, bool transpose_B, bool transpose_C,
                 int M, int N, int K, const uint8_t *A,
                 int32_t a_offset, int lda, const uint8_t *B,
                 int32_t b_offset, int ldb, uint8_t *C,
                 int32_t c_offset, int32_t c_mult_int,
                 int32_t c_shift, int ldc) {
    buffer_t buff_A, buff_B, buff_C;
    if (!transpose_A) {
        init_matrix_buffer(M, K, A, lda, &buff_A);
    } else {
        init_matrix_buffer(K, M, A, lda, &buff_A);
    }

    if (!transpose_B) {
        init_matrix_buffer(K, N, B, ldb, &buff_B);
    } else {
        init_matrix_buffer(N, K, B, ldb, &buff_B);
    }

    if (!transpose_C) {
        init_matrix_buffer(M, N, C, ldc, &buff_C);
    } else {
        init_matrix_buffer(N, M, C, ldc, &buff_C);
    }

    assert_no_error(halide_igemm(transpose_A, transpose_B, transpose_C, &buff_A,
                                 a_offset, &buff_B, b_offset, &buff_C, c_offset,
                                 c_mult_int, c_shift));
}


#ifdef __cplusplus
}
#endif
