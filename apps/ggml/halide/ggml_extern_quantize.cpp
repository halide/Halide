// Extern-stage scaffolding for K-quant quantize kernels.
//
// GGML's reference quantizer for the K-quant super-block formats (Q2_K,
// Q3_K, Q4_K, Q5_K, Q6_K) isn't a closed-form scale computation like every
// other type in this directory -- it runs an iterative, per-sub-block
// error-minimizing search over ~19 candidate scale factors (see
// src/ggml-quants.c: make_qx_quants / make_qkx1_quants / make_qkx2_quants /
// make_q3_quants). Porting that search to Halide is deferred; per the
// project's current phase, this sets up the Halide extern-stage plumbing
// now (a Func whose realization is computed by an external C function) and
// simply calls out to GGML's own public from_float_ref for the actual
// computation. Dequantize (a pure unpacking operation, no search) is
// implemented natively in Halide for these types -- see qX_k_generators.cpp.
//
// This is the one file in halide/ that depends on GGML's public API --
// every generator's *body* stays GGML-independent, but this scaffold
// deliberately borrows GGML's own reference computation for now, to be
// replaced with a from-scratch Halide search later.
//
// Extern-stage ABI: a plain C function taking one halide_buffer_t* per
// Func argument/output, returning 0 on success. Halide calls it twice per
// realization: once in "bounds query" mode (host pointers null, dimensions
// need to be filled in based on the output's already-concrete request) and
// once for real (host pointers valid, actually compute the data). See
// test/correctness/extern_bounds_inference.cpp for the reference pattern.

#include <HalideRuntime.h>

#include <ggml.h>

namespace {

int quantize_via_ggml_reference(ggml_type type, halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    if (x_buf->is_bounds_query()) {
        // out_buf already carries the concrete requested region (its dim[1]
        // extent is the number of blocks); the input row needed is exactly
        // that many blocks' worth of elements.
        const int64_t nb = out_buf->dim[1].extent;
        x_buf->dim[0].min = 0;
        x_buf->dim[0].extent = static_cast<int32_t>(nb * ggml_blck_size(type));
        return 0;
    }

    const float *x = reinterpret_cast<const float *>(x_buf->host);
    void *y = reinterpret_cast<void *>(out_buf->host);
    const int64_t k = x_buf->dim[0].extent;

    ggml_get_type_traits(type)->from_float_ref(x, y, k);
    return 0;
}

}  // namespace

extern "C" int q2_k_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_Q2_K, x_buf, out_buf);
}

extern "C" int q3_k_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_Q3_K, x_buf, out_buf);
}

extern "C" int q4_k_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_Q4_K, x_buf, out_buf);
}

extern "C" int q5_k_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_Q5_K, x_buf, out_buf);
}

extern "C" int q6_k_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_Q6_K, x_buf, out_buf);
}

// The remaining types below use this same scaffolding for different
// reasons than the K-quants: MXFP4/NVFP4 derive their scale via a
// transcendental (log2) or rounding-sensitive fixed-point float format not
// guaranteed to be bit-reproducible from scratch; IQ4_NL/IQ4_XS run a
// per-block/sub-block nearest-codeword search with scale refinement; TQ1_0/
// TQ2_0's byte-packing, while closed-form, is fiddly to unroll in Halide's
// functional style. All are deferred the same way, for now.

extern "C" int mxfp4_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_MXFP4, x_buf, out_buf);
}

extern "C" int nvfp4_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_NVFP4, x_buf, out_buf);
}

extern "C" int iq4_nl_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_IQ4_NL, x_buf, out_buf);
}

extern "C" int iq4_xs_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_IQ4_XS, x_buf, out_buf);
}

extern "C" int tq1_0_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_TQ1_0, x_buf, out_buf);
}

extern "C" int tq2_0_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_TQ2_0, x_buf, out_buf);
}

extern "C" int iq3_xxs_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_IQ3_XXS, x_buf, out_buf);
}

extern "C" int iq3_s_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_IQ3_S, x_buf, out_buf);
}

extern "C" int iq2_s_quantize_via_ggml(halide_buffer_t *x_buf, halide_buffer_t *out_buf) {
    return quantize_via_ggml_reference(GGML_TYPE_IQ2_S, x_buf, out_buf);
}
