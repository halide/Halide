// The extern stage of the trusted encoders (schemes/grid.h: ExternQuantize):
// GGML's quantizer of a type, on the blocks of values Halide asks for. Only
// the codec's quantize pipelines call it; the kernels (vec_dot, mul_mat) read
// weights GGML quantized.
//
// The synthetic importance matrix: GGML's iq2_xxs, iq2_xs and iq1_s quantizers
// require one (llama-quantize computes it from activations on a calibration
// text); the harness's is deterministic, column k's weight 1 + h(k) / 64, h(k)
// the top byte of k * 2654435761 (Knuth's multiplicative hash): in [1, 5),
// varied across columns so the weighted searches differ from unweighted ones.
#include "harness.h"

#include <cstring>
#include <map>
#include <mutex>

namespace gq {

const float *importance(ggml_type t, int64_t K) {
    if (!ggml_quantize_requires_imatrix(t)) return nullptr;
    static std::mutex m;
    static std::map<int64_t, std::vector<float>> ws;  // per K: never moved
    std::lock_guard<std::mutex> lock(m);
    std::vector<float> &w = ws[K];
    for (int64_t k = (int64_t)w.size(); k < K; k++) {
        w.push_back(1.0f + (float)((uint32_t)((uint64_t)k * 2654435761u) >> 24) / 64.0f);
    }
    return w.data();
}

}  // namespace gq

namespace {

ggml_type named(const char *n) {
    for (int t = 0; t < GGML_TYPE_COUNT; t++) {
        const char *tn = ggml_get_type_traits((ggml_type)t)->type_name;
        if (tn && !strcmp(n, tn)) return (ggml_type)t;
    }
    return GGML_TYPE_COUNT;
}

}  // namespace

// x: values (k, rest...), dense along k; out: blocks (block, rest...) of
// GGML's records of `type` (any element type of its size), dense along block.
extern "C" int ggml_trusted_quantize(const char *type, halide_buffer_t *x, halide_buffer_t *out) {
    ggml_type t = named(type);
    if (t == GGML_TYPE_COUNT) return -1;
    int64_t bk = ggml_blck_size(t);
    if (x->is_bounds_query()) {
        x->dim[0].min = out->dim[0].min * bk;
        x->dim[0].extent = out->dim[0].extent * bk;
        for (int d = 1; d < x->dimensions; d++) {
            x->dim[d].min = out->dim[d].min;
            x->dim[d].extent = out->dim[d].extent;
        }
        return 0;
    }
    if (out->type.bytes() != ggml_type_size(t) || x->dim[0].stride != 1 || out->dim[0].stride != 1) return -1;
    int64_t nb = out->dim[0].extent, b0 = out->dim[0].min, rows = 1;
    for (int d = 1; d < out->dimensions; d++)
        rows *= out->dim[d].extent;
    const float *w = gq::importance(t, bk * (b0 + nb));
    for (int64_t r = 0; r < rows; r++) {
        int64_t xo = 0, oo = 0, rest = r;
        for (int d = 1; d < out->dimensions; d++) {
            int64_t i = rest % out->dim[d].extent;
            rest /= out->dim[d].extent;
            xo += (out->dim[d].min + i - x->dim[d].min) * x->dim[d].stride;
            oo += i * out->dim[d].stride;
        }
        const float *src = (const float *)x->host + xo + (b0 * bk - x->dim[0].min);
        uint8_t *dst = out->host + oo * out->type.bytes();
        ggml_quantize_chunk(t, src, dst, 0, 1, bk * nb, w ? w + b0 * bk : nullptr);
    }
    return 0;
}
