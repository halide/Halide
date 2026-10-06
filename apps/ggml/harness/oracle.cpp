#include "harness.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gq {
namespace {

// GGML has no to_float for its activation-only types; decode them from their
// block layouts in ggml-common.h: q8_1 {f16 d, f16 s, i8 qs[32]},
// q8_K {f32 d, i8 qs[256], i16 bsums[16]}.
void decode_q8(ggml_type t, const uint8_t *p, float *out, int64_t K) {
    bool k = t == GGML_TYPE_Q8_K;
    int64_t bs = k ? 256 : 32, hdr = 4, size = hdr + bs + (k ? 32 : 0);
    if ((int64_t)ggml_type_size(t) != size) abort();
    for (int64_t b = 0; b < K / bs; b++, p += size) {
        float d;
        if (k) {
            std::memcpy(&d, p, 4);
        } else {
            ggml_fp16_t h;
            std::memcpy(&h, p, 2);
            d = ggml_fp16_to_fp32(h);
        }
        for (int64_t j = 0; j < bs; j++)
            out[b * bs + j] = d * (int8_t)p[hdr + j];
    }
}

std::vector<float> decode(ggml_type t, const uint8_t *p, const std::vector<int64_t> &rows, int64_t K) {
    std::vector<float> out(rows.size() * K);
    size_t rs = ggml_row_size(t, K);
    for (size_t i = 0; i < rows.size(); i++) {
        if (t == GGML_TYPE_F32) {
            std::memcpy(&out[i * K], p + rows[i] * rs, K * sizeof(float));
        } else if (t == GGML_TYPE_Q8_1 || t == GGML_TYPE_Q8_K) {
            decode_q8(t, p + rows[i] * rs, &out[i * K], K);
        } else {
            ggml_get_type_traits(t)->to_float(p + rows[i] * rs, &out[i * K], K);
        }
    }
    return out;
}

// All of 0..n-1 if n <= max, else max evenly spread indices including both ends.
std::vector<int64_t> sample(int64_t n, int64_t max) {
    std::vector<int64_t> v;
    for (int64_t i = 0; i < std::min(n, max); i++)
        v.push_back(n <= max ? i : i * (n - 1) / (max - 1));
    return v;
}

// gamma_n = n u / (1 - n u): the standard bound on n chained roundings.
double gamma(double n, int bits) {
    double nu = n * std::ldexp(1.0, -bits);
    return nu < 1 ? nu / (1 - nu) : INFINITY;
}

// Per-element bound on |x - deq(q(x))| for re-quantization of a row x to type
// t (8-bit absmax blocks of `block`, or f16/bf16): d = amax / 127 covers any
// rounding mode; (1 + 2^-7) covers a bf16- or f16-rounded d.
std::vector<double> requant_err(const float *x, int64_t K, ggml_type t, int64_t block) {
    std::vector<double> e(K);
    if (t == GGML_TYPE_F16 || t == GGML_TYPE_BF16) {
        for (int64_t k = 0; k < K; k++)
            e[k] = std::abs(x[k]) * std::ldexp(1.0, t == GGML_TYPE_F16 ? -11 : -8);
        return e;
    }
    for (int64_t i = 0; i < K; i += block) {
        double mx = 0;
        for (int64_t j = i; j < std::min(K, i + block); j++)
            mx = std::max(mx, (double)std::abs(x[j]));
        std::fill(&e[i], &e[std::min(K, i + block)], mx / 127 * (1 + std::ldexp(1.0, -7)));
    }
    return e;
}

}  // namespace

Reference reference(const Inputs &in, const std::vector<float> &a_f32) {
    int64_t K = in.s.K;
    Reference r;
    r.K = K;
    r.ns = sample(in.s.N, 64);
    r.ms = sample(in.s.M, 4);
    r.w = decode(in.wt, in.w->data(), r.ns, K);
    r.a = decode(in.at, in.a->data(), r.ms, K);
    for (int64_t m : r.ms)
        r.a0.insert(r.a0.end(), &a_f32[m * K], &a_f32[(m + 1) * K]);
    for (size_t m = 0; m < r.ms.size(); m++) {
        for (size_t n = 0; n < r.ns.size(); n++) {
            double s = 0;
            for (int64_t k = 0; k < K; k++)
                s += (double)r.w[n * K + k] * r.a[m * K + k];
            r.out.push_back(s);
        }
    }
    return r;
}

// Tolerance (every term is a bound; none is tuned). With W, A the decoded
// inputs, W', A' what the provider actually multiplies (after any internal
// re-quantization, |W - W'| <= dw, |A - A'| <= da elementwise), and
//   S = sum_k (|W| + dw) (|A| + da) + 2 max_block|W| |A|
// (the second term covers decoding formats whose to_float subtracts a min or
// offset, so the provider's partial terms may exceed |W A|):
//   |got - ref| <= sum_k (|W| da + |A| dw + dw da) + g S, with
//   g = gamma_{chain+4}(u_acc) + gamma_{levels+1}(2^-24) + 2 u_op + u_op^2,
// i.e. chain accumulations plus <= 4 ops per term (decode, scales) in the
// accumulator precision, an f32 tree over K/chain partials, and operand rounding.
double error_ratio(const Inputs &in, const Reference &ref, const Precision &p, const float *got) {
    int64_t K = ref.K, chain = p.chain > 0 ? std::min(p.chain, K) : K;
    double levels = std::ceil(std::log2((double)K / chain));
    double uo = p.operand_bits ? std::ldexp(1.0, -p.operand_bits) : 0;
    double g = gamma(chain + 4, p.acc_bits) + gamma(levels + 1, 24) + 2 * uo + uo * uo;
    int64_t wb = ggml_is_quantized(in.wt) ? ggml_blck_size(in.wt) : 1;
    double worst = 0;
    for (size_t m = 0; m < ref.ms.size(); m++) {
        const float *a = &ref.a[m * K];
        std::vector<double> da(K, 0.0);
        if (p.act_quant != GGML_TYPE_COUNT) {
            da = requant_err(&ref.a0[m * K], K, p.act_quant, p.act_block ? p.act_block : ggml_blck_size(p.act_quant));
            // da bounds |a0 - A'|; the oracle used A = in.at(a0), so add |A - a0|.
            for (int64_t k = 0; k < K; k++)
                da[k] += std::abs((double)a[k] - ref.a0[m * K + k]);
        }
        for (size_t n = 0; n < ref.ns.size(); n++) {
            const float *w = &ref.w[n * K];
            std::vector<double> dw(K, 0.0);
            if (p.w_block) dw = requant_err(w, K, GGML_TYPE_Q8_0, p.w_block);
            double S = 0, E = 0, sum_wmax_a = 0;
            for (int64_t k0 = 0; k0 < K; k0 += wb) {
                double wmax = 0;
                for (int64_t k = k0; k < k0 + wb; k++)
                    wmax = std::max(wmax, (double)std::abs(w[k]));
                for (int64_t k = k0; k < k0 + wb; k++) {
                    double aw = std::abs(w[k]), aa = std::abs(a[k]);
                    S += (aw + dw[k]) * (aa + da[k]) + 2 * wmax * aa;
                    E += aw * da[k] + aa * dw[k] + dw[k] * da[k];
                    sum_wmax_a += wmax * aa;
                }
            }
            // q8_1 carries s = f16(d * sum(qs)) (d unrounded), which dots with
            // the weight's block min: 2 f16 roundings of a term <= wmax sum |a|.
            if (in.at == GGML_TYPE_Q8_1 || p.act_quant == GGML_TYPE_Q8_1) E += gamma(2, 11) * sum_wmax_a;
            double tol = E + g * S;
            size_t i = m * ref.ns.size() + n;
            double err = std::abs((double)got[ref.ms[m] * in.s.N + ref.ns[n]] - ref.out[i]);
            double ratio = tol > 0 ? err / tol : (err == 0 ? 0 : INFINITY);
            worst = std::max(worst, std::isnan(ratio) ? INFINITY : ratio);
        }
    }
    return worst;
}

}  // namespace gq
