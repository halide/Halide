#include "harness.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef __APPLE__
#define ACCELERATE_NEW_LAPACK
#include <Accelerate/Accelerate.h>
#else
#include <cblas.h>
#endif

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
        } else {
            to_float(t, p + rows[i] * rs, &out[i * K], K);
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

// Per-element bound e on |x - deq(q(x))| for re-quantization of a row x to
// type t (8-bit absmax blocks of `block`, or f16/bf16): d = amax / 127 covers
// any rounding mode; (1 + 2^-7) covers a bf16- or f16-rounded d. With
// `nearest`, d / 2, plus 2^-4 d for an f16-rounded d on codes up to 127 (127
// * 2^-11) and the f32 roundings on the way. f16/bf16 round to nearest: half
// an ulp, relative (2^-11, 2^-8) down to the subnormals' absolute half ulp
// (2^-25, 2^-134); x past the largest finite value overflows (INFINITY).
void requant_err(const float *x, int64_t K, ggml_type t, int64_t block, double *e, bool nearest = false) {
    if (t == GGML_TYPE_F16 || t == GGML_TYPE_BF16) {
        bool h = t == GGML_TYPE_F16;
        for (int64_t k = 0; k < K; k++) {
            double a = std::abs(x[k]);
            e[k] = h && a >= 65520 ? INFINITY : std::max(a * std::ldexp(1.0, h ? -11 : -8), std::ldexp(1.0, h ? -25 : -134));
        }
        return;
    }
    for (int64_t i = 0; i < K; i += block) {
        double mx = 0;
        for (int64_t j = i; j < std::min(K, i + block); j++)
            mx = std::max(mx, (double)std::abs(x[j]));
        std::fill(&e[i], &e[std::min(K, i + block)], mx / 127 * (nearest ? 0.5 + std::ldexp(1.0, -4) : 1 + std::ldexp(1.0, -7)));
    }
}

// out[m * N + n] = (add ? out[m * N + n] : 0) + sum_k l[n * K + k] * r[m * K + k]
// for the M x K matrix r and an N x K matrix l that `left` writes one row of
// at a time: BLAS's dgemm on <= 32 MiB of l's rows at once.
template<typename Left>
void product(int64_t N, int64_t M, int64_t K, Left left, const std::vector<double> &r, std::vector<double> &out, bool add = false) {
    out.resize(M * N);
    int64_t rows = std::clamp<int64_t>((4 << 20) / K, 1, N);
    std::vector<double> l(rows * K);
    for (int64_t n0 = 0; n0 < N; n0 += rows) {
        int64_t n1 = std::min(N, n0 + rows);
        for (int64_t n = n0; n < n1; n++)
            left(n, &l[(n - n0) * K]);
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans, (int)M, (int)(n1 - n0), (int)K, 1.0, r.data(), (int)K, l.data(), (int)K, add ? 1.0 : 0.0, &out[n0], (int)N);
    }
}

// The re-quantization model of p: {act_quant, its block, w_block, w_quant,
// act_sum and act_nearest}, 0 for what p does not use.
std::array<int64_t, 5> requant_key(const Precision &p) {
    bool aq = p.act_quant != GGML_TYPE_COUNT, wq = !p.w_block && p.w_quant != GGML_TYPE_COUNT;
    return {aq ? p.act_quant + 1 : 0, aq ? (p.act_block ? p.act_block : ggml_blck_size(p.act_quant)) : 0, p.w_block, wq ? p.w_quant + 1 : 0, aq ? p.act_sum + 2 * p.act_nearest : 0};
}

// max |w| over each of a weight row's blocks of wb values.
void block_max(const float *w, int64_t K, int64_t wb, double *row) {
    for (int64_t k0 = 0; k0 < K; k0 += wb) {
        float mx = 0;
        for (int64_t k = k0; k < k0 + wb; k++)
            mx = std::max(mx, std::abs(w[k]));
        std::fill(row + k0, row + k0 + wb, (double)mx);
    }
}

// The sums over k, for each of ref's outputs, of check's e = cw da + dw |A|
// and of e^2, as products: da is of an act row, and dw and cw (|W| + dw, plus
// max_block|W| with p.act_sum) of a weight row. Empty (e = 0) if p
// re-quantizes nothing.
Reference::Requant requant_sums(const Reference &ref, const Precision &p) {
    int64_t K = ref.K, N = ref.ns.size(), M = ref.ms.size();
    Reference::Requant r;
    bool a_requant = p.act_quant != GGML_TYPE_COUNT, w_requant = p.w_block || p.w_quant != GGML_TYPE_COUNT;
    auto dw = [&](int64_t n, double *row) {
        if (p.w_block) {
            requant_err(&ref.w[n * K], K, GGML_TYPE_Q8_0, p.w_block, row);
        } else if (w_requant) {
            requant_err(&ref.w[n * K], K, p.w_quant, ggml_blck_size(p.w_quant), row);
        } else {
            std::fill(row, row + K, 0.0);
        }
    };
    std::vector<double> wmax(K, 0.0);
    auto cw = [&](int64_t n, double *row) {
        dw(n, row);
        if (p.act_sum) block_max(&ref.w[n * K], K, ref.w_block, wmax.data());
        for (int64_t k = 0; k < K; k++)
            row[k] += std::abs(ref.w[n * K + k]) + wmax[k];
    };
    std::vector<double> da, abs_a(M * K);
    for (int64_t i = 0; i < M * K; i++)
        abs_a[i] = std::abs(ref.a[i]);
    if (a_requant) {
        da.resize(M * K);
        for (int64_t m = 0; m < M; m++) {
            requant_err(&ref.a0[m * K], K, p.act_quant, p.act_block ? p.act_block : ggml_blck_size(p.act_quant), &da[m * K], p.act_nearest);
            // da bounds |a0 - A'|; the oracle used A = in.at(a0), so add |A - a0|.
            for (int64_t k = 0; k < K; k++)
                da[m * K + k] += std::abs((double)ref.a[m * K + k] - ref.a0[m * K + k]);
        }
    }
    auto square = [](std::vector<double> v) {
        for (double &x : v)
            x *= x;
        return v;
    };
    auto squared = [&](auto f) {
        return [&, f](int64_t n, double *row) {
            f(n, row);
            for (int64_t k = 0; k < K; k++)
                row[k] *= row[k];
        };
    };
    if (a_requant) {
        product(N, M, K, cw, da, r.sum);
        product(N, M, K, squared(cw), square(da), r.sum_sq);
    }
    if (w_requant) {
        product(N, M, K, dw, abs_a, r.sum, a_requant);
        product(N, M, K, squared(dw), square(abs_a), r.sum_sq, a_requant);
    }
    if (a_requant && w_requant) {
        // e^2's cross term, 2 (cw dw) (da |A|)
        for (int64_t i = 0; i < M * K; i++)
            abs_a[i] *= 2 * da[i];
        std::vector<double> d(K);
        auto cw_dw = [&](int64_t n, double *row) {
            cw(n, row);
            dw(n, d.data());
            for (int64_t k = 0; k < K; k++)
                row[k] *= d[k];
        };
        product(N, M, K, cw_dw, abs_a, r.sum_sq, true);
    }
    return r;
}

// Whether each of ref's outputs in got is, bit for bit, GGML's CPU vec_dot of
// its weight row and its act row, the acts converted to the weight's
// vec_dot_type first (as GGML's mul_mat does). Threaded over the weight rows:
// GGML's vec_dot and from_float are functions of their arguments alone (its
// own mul_mat calls them from every thread of its pool).
bool is_ggml_vec_dot(const Inputs &in, const Reference &ref, const float *got) {
    const auto *t = ggml_get_type_traits_cpu(in.wt);
    int64_t K = ref.K, N = ref.ns.size(), M = ref.ms.size();
    size_t wrow = ggml_row_size(in.wt, K), arow = ggml_row_size(in.at, K);
    bool convert = in.at != t->vec_dot_type;
    if (convert && in.at != GGML_TYPE_F32) abort();
    std::atomic<bool> same{true};
    auto rows = [&](int64_t n0, int64_t n1) {
        std::vector<uint8_t> converted(convert ? ggml_row_size(t->vec_dot_type, K) : 0);
        for (int64_t m = 0; m < M; m++) {
            const uint8_t *a = in.a->data() + ref.ms[m] * arow;
            if (convert) {
                ggml_get_type_traits_cpu(t->vec_dot_type)->from_float((const float *)a, converted.data(), K);
                a = converted.data();
            }
            for (int64_t n = n0; n < n1; n++) {
                float v = got[ref.ms[m] * in.s.N + ref.ns[n]], g;
                t->vec_dot((int)K, &g, 0, in.w->data() + ref.ns[n] * wrow, 0, a, 0, 1);
                if (std::memcmp(&v, &g, sizeof(v))) {
                    fprintf(stderr, "not GGML's vec_dot: K=%lld n=%lld m=%lld: %a, GGML %a\n", (long long)K, (long long)ref.ns[n], (long long)ref.ms[m], v, g);
                    same = false;
                }
            }
        }
    };
    int64_t threads = std::min<int64_t>(N, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> ts;
    for (int64_t i = 1; i < threads; i++)
        ts.emplace_back(rows, N * i / threads, N * (i + 1) / threads);
    rows(0, N / threads);
    for (std::thread &th : ts)
        th.join();
    return same;
}

}  // namespace

void to_float(ggml_type t, const uint8_t *p, float *out, int64_t K) {
    if (t == GGML_TYPE_Q8_1 || t == GGML_TYPE_Q8_K) {
        decode_q8(t, p, out, K);
    } else {
        ggml_get_type_traits(t)->to_float(p, out, K);
    }
}

Reference reference(const Inputs &in, const std::vector<float> &a_f32, bool all) {
    int64_t K = in.s.K;
    Reference r;
    r.K = K;
    r.ns = sample(in.s.N, all ? in.s.N : 64);
    r.ms = sample(in.s.M, all ? in.s.M : 4);
    r.w = decode(in.wt, in.w->data(), r.ns, K);
    r.a = decode(in.at, in.a->data(), r.ms, K);
    for (int64_t m : r.ms)
        r.a0.insert(r.a0.end(), &a_f32[m * K], &a_f32[(m + 1) * K]);
    r.w_block = ggml_is_quantized(in.wt) ? ggml_blck_size(in.wt) : 1;
    int64_t N = r.ns.size(), M = r.ms.size();
    const float *w = r.w.data();
    std::vector<double> a(r.a.begin(), r.a.end());
    product(N, M, K, [&](int64_t n, double *row) { std::copy_n(&w[n * K], K, row); }, a, r.out);
    for (double &x : a)
        x = std::abs(x);
    auto abs_w = [&](int64_t n, double *row) {
        for (int64_t k = 0; k < K; k++)
            row[k] = std::abs(w[n * K + k]);
    };
    product(N, M, K, abs_w, a, r.sum_wa);
    product(N, M, K, [&](int64_t n, double *row) { block_max(&w[n * K], K, r.w_block, row); }, a, r.sum_wmax_a);
    for (double &x : a)
        x *= x;
    auto w2 = [&](int64_t n, double *row) {
        for (int64_t k = 0; k < K; k++)
            row[k] = (double)w[n * K + k] * w[n * K + k];
    };
    product(N, M, K, w2, a, r.sum_w2a2);
    return r;
}

// Tolerance (every term is a bound, one of them of a stated probability; none
// is tuned). With W, A the decoded inputs, W', A' what the provider actually
// multiplies (after any internal re-quantization, |W - W'| <= dw,
// |A - A'| <= da elementwise), each term of W' A' is within
//   e = cw da + dw |A|, cw = |W| + dw,
// of W A's. With p.act_sum the provider splits W' into an offset per block,
// times the block's sum of A, and the rest, times A': only the rest meets
// da, and cw gains the offset's bound, max_block|W|. With
//   E = sum_k e,
//   S = sum_k |W| |A| + E + 2 sum_k max_block|W| |A|
// (the last term covers decoding formats whose to_float subtracts a min or
// offset, so the provider's partial terms may exceed |W A|):
//   |got - sum_k W A| <= E + g S, with
//   g = gamma_{chain+4}(u_acc) + gamma_{levels+1}(2^-24) + 2 u_op + u_op^2,
// i.e. chain accumulations plus <= 4 ops per term (decode, scales) in the
// accumulator precision, an f32 tree over K/chain partials, and operand rounding.
// E's worst case has every term's error on one side. The activations are the
// harness's own draws: their signs are independent and even, whatever their
// magnitudes and W. Where A' (and in.at's A) is odd in each activation, as
// GGML's activation types are (scales of a block's largest magnitude, codes
// rounded alike either side of 0) and as !p.act_asymmetric declares, an
// activation's sign flips its term's error and no other's: the errors' sum is
// sum_k s_k x_k, |x_k| <= e, of independent even signs s_k, and by Hoeffding's
// inequality exceeds lambda sqrt(sum_k e^2) with probability <= 2 exp(-lambda^2
// / 2). lambda = sqrt(2 ln(2 outputs / p)) makes that p = 1e-9 over all of a
// check's outputs, and that bound takes E's place where it is the lesser.
// The reference is BLAS's f64 sum of the K terms (each exact in f64), added
// in an order of its choosing, with or without FMA:
//   |ref - sum_k W A| <= gamma_K(2^-53) sum_k |W| |A|,
// and the tolerance on |got - ref| is the two bounds' sum. The sums over k
// are matrix products: those of the inputs alone are the reference's, and E's
// its re-quantization model's (requant_sums), computed once for the providers
// that share the model.
// With p.ggml_dot, also bit-exact: GGML's CPU vec_dot (else INFINITY).
//
// tol_scale: a tolerance is worth what it excludes. sqrt(sum_k (W A)^2) is
// what an output comes to with its terms' signs at random; a tolerance of
// that much or more (gamma_n(u) is INFINITY from n u = 1) accepts outputs
// that owe nothing to the reference.
Check check(const Inputs &in, Reference &ref, const Precision &p, const float *got) {
    int64_t K = ref.K, chain = p.chain > 0 ? std::min(p.chain, K) : K;
    double levels = std::ceil(std::log2((double)K / chain));
    double uo = p.operand_bits ? std::ldexp(1.0, -p.operand_bits) : 0;
    double g = gamma(chain + 4, p.acc_bits) + gamma(levels + 1, 24) + 2 * uo + uo * uo;
    auto cached = ref.requant.find(requant_key(p));
    if (cached == ref.requant.end()) cached = ref.requant.emplace(requant_key(p), requant_sums(ref, p)).first;
    const Reference::Requant &requant = cached->second;
    static const ggml_type odd_types[] = {GGML_TYPE_F32, GGML_TYPE_F16, GGML_TYPE_BF16, GGML_TYPE_Q8_0, GGML_TYPE_Q8_1, GGML_TYPE_Q8_K};
    bool odd = !p.act_asymmetric && std::find(std::begin(odd_types), std::end(odd_types), in.at) != std::end(odd_types);
    double lambda = std::sqrt(2 * std::log(2 * (double)(ref.ms.size() * ref.ns.size()) / 1e-9));
    Check c{0, 0};
    for (size_t m = 0; m < ref.ms.size(); m++) {
        for (size_t n = 0; n < ref.ns.size(); n++) {
            size_t i = m * ref.ns.size() + n;
            double sum_wa = ref.sum_wa[i], sum_wmax_a = ref.sum_wmax_a[i], E = requant.sum.empty() ? 0 : requant.sum[i];
            double S = sum_wa + E + 2 * sum_wmax_a;
            if (odd && !requant.sum.empty()) E = std::min(E, lambda * std::sqrt(requant.sum_sq[i]));
            // The oracle decodes q8_1 as d * qs and never reads s. Kernels
            // may read s for the sum of the decoded block, which dots with
            // the weight's block min. GGML's s = f16(d * sum(qs)) (d
            // unrounded; the harness's activations are GGML's) is within 2
            // f16 roundings of it, ours (f16(f16(d) * sum(qs))) within 1,
            // and p.act_sum's (f16 of the f32 sum of up to 32 values)
            // within 2 of that sum: either way <= gamma(2, 11) of a term <=
            // wmax sum |a|.
            if (in.at == GGML_TYPE_Q8_1 || p.act_quant == GGML_TYPE_Q8_1) E += gamma(2, 11) * sum_wmax_a;
            double tol = E + g * S + gamma(K, 53) * sum_wa;
            double err = std::abs((double)got[ref.ms[m] * in.s.N + ref.ns[n]] - ref.out[i]);
            double ratio = tol > 0 ? err / tol : (err == 0 ? 0 : INFINITY);
            c.err_ratio = std::max(c.err_ratio, std::isnan(ratio) ? INFINITY : ratio);
            double scale = std::sqrt(ref.sum_w2a2[i]), tol_scale = tol > 0 ? tol / scale : 0;
            c.tol_scale = std::max(c.tol_scale, std::isnan(tol_scale) ? INFINITY : tol_scale);
        }
    }
    if (p.ggml_dot) {
        if (is_ggml_vec_dot(in, ref, got)) {
            c.tol_scale = 0;
        } else {
            c.err_ratio = INFINITY;
        }
    }
    return c;
}

}  // namespace gq
