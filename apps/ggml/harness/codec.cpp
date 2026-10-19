// The codec op: every format's Halide quantize/dequantize against GGML's
// reference quantizer and to_float (for q8_1, which has none: d * qs), bitwise
// (q8_1's s against f16(f16(d) * sum(qs)) of GGML's d and qs: see
// consistent_q8_1_sums), on N(0, 1) rows whose first
// blocks are adversarial (signed zeros, ties, constants, tiny and huge
// values). inf/NaN inputs are undefined behavior in GGML's reference and are
// not tested. Also checked: the same with the scaled codes kernels may pick
// (same bytes, same values), and dequantize on crafted blocks spanning every
// code byte and finite fp16 scale. Layouts GGML lacks (SoA) are checked
// against GGML's bytes laid out by the harness (relayout). Timed against the
// same GGML functions (and from_float, GGML's fast path for activations,
// where it differs from the reference).
#include "harness.h"

#include "HalideRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace gq {
namespace {

using Ports = std::vector<std::unique_ptr<HBuf>>;

// quantize(x, ports...) or dequantize(ports..., y).
int call(Argv f, bool quant, const HBuf &values, const Ports &ports) {
    std::vector<void *> args;
    for (const auto &p : ports)
        args.push_back(&p->b);
    args.insert(quant ? args.begin() : args.end(), (void *)&values.b);
    return f(args.data());
}

struct Lambda : Kernel {
    std::function<void()> f;
    void run(int reps) override {
        for (int r = 0; r < reps; r++)
            f();
    }
    void read(float *) override {
    }
};

std::unique_ptr<Kernel> lambda(std::string path, std::function<void()> f) {
    auto k = std::make_unique<Lambda>();
    k->path = std::move(path);
    k->f = std::move(f);
    return k;
}

void adversarial(float *x, int64_t K) {
    for (int j = 0; j < 32 && K >= 32 * 8; j++) {
        x[j] = j == 0 ? -0.0f : 0.0f;
        x[32 + j] = (j % 2 ? -1 : 1) * 0.25f * (j % 8);
        x[64 + j] = 100;
        x[96 + j] = 1e-20f * (j - 7);
        x[128 + j] = 1e-37f * (j - 3);
        x[160 + j] = 1e6f * (j - 9);
        x[192 + j] = j == 5 ? -3.0f : 3.0f;
        x[224 + j] = 7e4f;
    }
}

std::string first_diff(const void *a, const void *b, size_t n, size_t block_bytes) {
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) {
            char s[96];
            snprintf(s, sizeof(s), "byte %zu (block %zu): %02x vs ggml %02x", i, i / block_bytes, p[i], q[i]);
            return s;
        }
    }
    return "";
}

// ggml_quantize_chunk lacks GGML's activation-only types (q8_1).
bool chunked(ggml_type t) {
    return t != GGML_TYPE_Q8_1;
}

// GGML's q8_1 stores s = f16(d * sum(qs)) from the unrounded scale d, which in
// about a quarter of blocks is 1 ulp off the sum of the decoded block. Our
// scheme derives s from the decoded scale: s = f16(f16(d) * sum(qs)). Rewrite
// GGML's blocks (d and qs are checked bitwise as they are) to that, returning
// how many blocks differed.
int64_t consistent_q8_1_sums(uint8_t *blocks, int64_t nb) {
    const size_t bytes = ggml_type_size(GGML_TYPE_Q8_1);
    int64_t differs = 0;
    for (int64_t b = 0; b < nb; b++) {
        uint8_t *p = blocks + b * bytes;
        ggml_fp16_t d, s;
        memcpy(&d, p, sizeof(d));
        memcpy(&s, p + sizeof(d), sizeof(s));
        int32_t sum = 0;
        for (int j = 0; j < ggml_blck_size(GGML_TYPE_Q8_1); j++)
            sum += (int8_t)p[2 * sizeof(d) + j];
        ggml_fp16_t ours = ggml_fp32_to_fp16(ggml_fp16_to_fp32(d) * (float)sum);
        differs += ours != s;
        memcpy(p + sizeof(d), &ours, sizeof(ours));
    }
    return differs;
}

// GGML's reference quantizer of R rows of K values.
void quantize_ref(ggml_type t, const float *x, uint8_t *dst, int64_t R, int64_t K) {
    if (chunked(t)) {
        ggml_quantize_chunk(t, x, dst, 0, R, K, nullptr);
    } else {
        ggml_get_type_traits(t)->from_float_ref(x, dst, R * K);
    }
}

#ifdef GQ_KLEIDIAI
// KleidiAI v1.24.0 (libkleidiai.a, linked with GGML's CPU backend; its
// headers are not installed).
extern "C" {
struct kai_rhs_pack_qsi8cx_params {
    int32_t lhs_zero_point;
    float scale_multiplier;
};
size_t kai_get_lhs_packed_size_lhs_quant_pack_qai8dxp_f32(size_t m, size_t k, size_t mr, size_t kr, size_t sr);
void kai_run_lhs_quant_pack_qai8dxp_f32(size_t m, size_t k, size_t mr, size_t kr, size_t sr, size_t m_idx_start,
                                        const float *lhs, size_t lhs_stride, void *lhs_packed);
size_t kai_get_rhs_packed_size_rhs_pack_nxk_qsi8cxp_qsi8cx_neon(size_t n, size_t k, size_t nr, size_t kr, size_t sr);
void kai_run_rhs_pack_nxk_qsi8cxp_qsi8cx_neon(size_t num_groups, size_t n, size_t k, size_t nr, size_t kr, size_t sr,
                                              const int8_t *rhs, const float *bias, const float *scale, void *rhs_packed,
                                              size_t extra_bytes, const kai_rhs_pack_qsi8cx_params *params);
}
#endif

// GGML's q8_0 -> qsi8cx requantization of a row of K values (d * qs of its
// q8_0 blocks), as in ggml-cpu/kleidiai/kleidiai.cpp (static there):
// returns the scale.
float ggml_requant_qsi8cx(const float *v, int64_t K, int8_t *q) {
    float max_abs = 0.0f;
    for (int64_t k = 0; k < K; k++)
        max_abs = std::max(max_abs, std::fabs(v[k]));
    float scale = max_abs > 0.0f ? max_abs / 127.0f : 0.0f;
    const float inv_scale = scale > 0.0f ? 1.0f / scale : 0.0f;
    for (int64_t k = 0; k < K; k++) {
        int32_t c = scale > 0.0f ? static_cast<int32_t>(std::lround(v[k] * inv_scale)) : 0;
        q[k] = (int8_t)std::clamp(c, -127, 127);
    }
    return scale;
}

// Row r's code k in a rowwise format's codes [32 * rows, K / 32, R / rows]
// (rows > 1: interleaved in chunk-byte pieces).
int8_t code_at(const FormatSpec &f, const HBuf &codes, int64_t r, int64_t k, int64_t K) {
    const int8_t *p = (const int8_t *)codes.b.host;
    if (f.rows == 1) return p[r * K + k];
    int64_t j = k % 32, rec = (r / f.rows) * (K / 32) + k / 32;
    return p[rec * 32 * f.rows + (j / f.chunk) * f.rows * f.chunk + (r % f.rows) * f.chunk + j % f.chunk];
}

template<typename T>
T field(const HBuf &b, int64_t r) {
    T v;
    memcpy(&v, b.b.host + r * sizeof(T), sizeof(T));
    return v;
}

std::string mismatch(const std::string &what, int64_t r, double got, double want) {
    char s[160];
    snprintf(s, sizeof(s), "row %lld %s: %.9g vs %.9g", (long long)r, what.c_str(), got, want);
    return s;
}

// KleidiAI's formats (qsi8cx, qai8dx), which GGML has no type for: R rows
// (N(0, 1), the first with `adversarial` blocks, then zero, constant, ties,
// all-negative, skewed, tiny and huge rows). qsi8cx, on the rows and on
// their q8_0 decoding (what it re-quantizes in GGML): codes and scales
// against GGML's loop (ggml_requant_qsi8cx), the codes' bytes (layout) and
// sums against KleidiAI's packing of them (kai_run_rhs_pack_nxk_qsi8cxp_
// qsi8cx_neon, its reduction sums with lhs_zero_point 1, as GGML calls it).
// qai8dx: codes, offset and scale against kai_run_lhs_quant_pack_qai8dxp_f32
// (mr = 1, kr = 8, sr = 1: the 1x8 gemv kernel's packing). Dequantize: the
// formats' decode, (code [+ offset]) * scale, of the quantized ports.
void rowwise_codecs(const CodecRow &row, const std::vector<int64_t> &Ks, bool check, int rounds, double min_ms,
                    std::vector<CodecResult> &results) {
    FormatSpec fs = parse_format(row.type);
    bool act = fs.type == "qai8dx";
    const halide_filter_metadata_t *md = row.metadata();
    int np = md->num_arguments - 1;
    const int64_t R = 8;
    std::mt19937 rng(11);
    std::normal_distribution<float> normal;
    for (int64_t K : Ks) {
        if (K % 32) continue;
        std::vector<float> x(K * R), y(K * R);
        for (int64_t r = 0; r < R; r++) {
            float *v = &x[r * K];
            for (int64_t k = 0; k < K; k++) {
                float n = normal(rng);
                v[k] = r == 0 ? n : r == 1 ? (k % 3 ? 0.0f : -0.0f) :
                                r == 2     ? 100.0f :
                                r == 3     ? (float)(k % 255) - 127.0f + 0.5f :
                                r == 4     ? -std::fabs(n) - 0.5f :
                                r == 5     ? 10.0f * std::fabs(n) :
                                r == 6     ? 1e-30f * n :
                                             1e37f * n;
            }
            if (r == 0) adversarial(v, K);
            if (r == 3) v[0] = -127.5f, v[1] = 127.5f;  // s = 1: x s at every tie
            if (r == 5) v[0] = -0.1f;
        }
        std::vector<std::pair<std::string, std::vector<float>>> inputs{{"N(0,1)", x}};
        if (!act) {
            // GGML's q8_0 of the rows, decoded: what GGML re-quantizes.
            // (Rows scaled to q8_0's range: its d = max |x| / 127 is fp16.)
            std::vector<float> x8 = x;
            for (int64_t r = 0; r < R; r++) {
                float mx = 0, lim = 6e6f;
                for (int64_t k = 0; k < K; k++)
                    mx = std::max(mx, std::fabs(x8[r * K + k]));
                for (int64_t k = 0; mx > lim && k < K; k++)
                    x8[r * K + k] *= lim / mx;
            }
            std::vector<uint8_t> q8(ggml_row_size(GGML_TYPE_Q8_0, K) * R);
            ggml_quantize_chunk(GGML_TYPE_Q8_0, x8.data(), q8.data(), 0, R, K, nullptr);
            inputs.push_back({"q8_0", std::vector<float>(K * R)});
            to_float(GGML_TYPE_Q8_0, q8.data(), inputs.back().second.data(), K * R);
        }
        for (auto &in : inputs) {
            const std::string &input = in.first;
            std::vector<float> &xs = in.second;
            HBuf bx(xs.data(), halide_type_t(halide_type_float, 32), {K, R}), by(y.data(), halide_type_t(halide_type_float, 32), {K, R});
            // The reference: codes [R, K], and the per-row fields.
            std::vector<int8_t> rc(K * R);
            std::vector<float> rscale(R);
            std::vector<int32_t> roffset(R), rsum(R);
            std::vector<uint8_t> packed;
            std::string ref_path;
            if (act) {
#ifdef GQ_KLEIDIAI
                packed.resize(kai_get_lhs_packed_size_lhs_quant_pack_qai8dxp_f32(R, K, 1, 8, 1));
                kai_run_lhs_quant_pack_qai8dxp_f32(R, K, 1, 8, 1, 0, xs.data(), K * sizeof(float), packed.data());
                for (int64_t r = 0; r < R; r++) {
                    const uint8_t *p = packed.data() + r * (K + 8);
                    memcpy(&rc[r * K], p, K);
                    memcpy(&roffset[r], p + K, 4);
                    memcpy(&rscale[r], p + K + 4, 4);
                }
                ref_path = "vs kai_run_lhs_quant_pack_qai8dxp_f32";
#else
                fprintf(stderr, "codec %s: no KleidiAI to check against\n", row.type.c_str());
                return;
#endif
            } else {
                for (int64_t r = 0; r < R; r++) {
                    rscale[r] = ggml_requant_qsi8cx(&xs[r * K], K, &rc[r * K]);
                    for (int64_t k = 0; k < K; k++)
                        rsum[r] += rc[r * K + k];
                }
                ref_path = "vs ggml requant";
#ifdef GQ_KLEIDIAI
                int nr = fs.rows, kr = fs.rows > 1 ? fs.chunk : 8;
                packed.resize(kai_get_rhs_packed_size_rhs_pack_nxk_qsi8cxp_qsi8cx_neon(R, K, nr, kr, 1));
                kai_rhs_pack_qsi8cx_params params{1, 1.0f};
                kai_run_rhs_pack_nxk_qsi8cxp_qsi8cx_neon(1, R, K, nr, kr, 1, rc.data(), nullptr, rscale.data(), packed.data(), 0, &params);
                ref_path += " + kai_run_rhs_pack_nxk_qsi8cxp_qsi8cx_neon";
#endif
            }
            std::vector<std::vector<uint8_t>> mem;
            auto bq = rowwise_ports(md, 1, np, fs, K, R, mem);
            // ports: codes, then qsi8cx: scale, sum; qai8dx: offset, scale
            auto compare = [&]() -> std::string {
                for (int64_t r = 0; r < R; r++) {
                    for (int64_t k = 0; k < K; k++) {
                        int8_t c = code_at(fs, *bq[0], r, k, K);
                        if (c != rc[r * K + k]) return mismatch("code " + std::to_string(k), r, c, rc[r * K + k]);
                    }
                    float sc = field<float>(*bq[act ? 2 : 1], r);
                    if (memcmp(&sc, &rscale[r], 4)) return mismatch("scale", r, sc, rscale[r]);
                    int32_t o = field<int32_t>(*bq[act ? 1 : 2], r), want = act ? roffset[r] : rsum[r];
                    if (o != want) return mismatch(act ? "offset" : "sum", r, o, want);
                }
                if (!act && !packed.empty()) {
                    // KleidiAI's packing: per nr rows, their codes (K), then
                    // nr sums, scales and biases.
                    int64_t nr = fs.rows, stride = packed.size() / (R / nr);
                    for (int64_t g = 0; g < R / nr; g++) {
                        const uint8_t *p = packed.data() + g * stride;
                        for (int64_t i = 0; i < nr * K; i++) {
                            int64_t r = g * nr, k = i;
                            if (nr > 1) {
                                int64_t piece = i / fs.chunk;
                                r += piece % nr, k = (piece / nr) * fs.chunk + i % fs.chunk;
                            } else {
                                r += i / K, k = i % K;
                            }
                            int8_t c = code_at(fs, *bq[0], r, k, K);
                            if (c != (int8_t)p[i]) return mismatch("code " + std::to_string(k) + " vs kai packed", r, c, (int8_t)p[i]);
                        }
                        for (int64_t i = 0; i < nr; i++) {
                            int32_t ks;
                            memcpy(&ks, p + nr * K + 4 * i, 4);
                            int32_t o = field<int32_t>(*bq[2], g * nr + i);
                            if (o != ks) return mismatch("sum vs kai packed", g * nr + i, o, ks);
                        }
                    }
                }
                return "";
            };
            auto decoded = [&]() -> std::string {
                for (int64_t r = 0; r < R; r++) {
                    for (int64_t k = 0; k < K; k++) {
                        float c = code_at(fs, *bq[0], r, k, K), want;
                        want = act ? (c + (float)field<int32_t>(*bq[1], r)) * field<float>(*bq[2], r) : c * field<float>(*bq[1], r);
                        if (memcmp(&y[r * K + k], &want, 4)) return mismatch("value " + std::to_string(k), r, y[r * K + k], want);
                    }
                }
                return "";
            };
            for (const char *dir : {"quantize", "dequantize"}) {
                bool quant = dir[0] == 'q';
                struct Entry {
                    std::string provider;
                    std::unique_ptr<Kernel> k;
                    std::string detail;
                };
                std::vector<Entry> es;
                for (int bench : {0, 1, 2}) {  // checked, bench, scaled (checked; the same: rowwise codes are not scaled)
                    if (!check && bench != 1) continue;
                    Argv f = bench == 2 ? row.scaled[!quant] : quant ? row.quantize[bench] :
                                                                       row.dequantize[bench];
                    std::string name = "halide-" + row.type + (bench == 1 ? "" : bench ? ":scaled" :
                                                                                         ":checked");
                    std::string detail;
                    if (quant) {
                        for (auto &m : mem)
                            std::fill(m.begin(), m.end(), 0x5a);
                        int err = call(f, true, bx, bq);
                        detail = err ? "halide error " + std::to_string(err) : compare();
                        if (!detail.empty()) detail = input + " " + ref_path + ": " + detail;
                    } else {
                        call(row.quantize[0], true, bx, bq);
                        std::fill(y.begin(), y.end(), NAN);
                        int err = call(f, false, by, bq);
                        detail = err ? "halide error " + std::to_string(err) : decoded();
                        if (!detail.empty()) detail = input + ": " + detail;
                    }
                    es.push_back({name, lambda("halide", quant ? std::function<void()>([=, &bx, &bq] { call(f, true, bx, bq); }) : [=, &by, &bq] { call(f, false, by, bq); }), detail});
                }
#ifdef GQ_KLEIDIAI
                if (!check && quant && act) {
                    es.push_back({"kleidiai", lambda("lhs_quant_pack_qai8dxp_f32", [&] { kai_run_lhs_quant_pack_qai8dxp_f32(R, K, 1, 8, 1, 0, xs.data(), K * sizeof(float), packed.data()); }), ""});
                }
#endif
                std::vector<Timing> ts;
                if (!check) {
                    std::vector<Kernel *> ks;
                    for (auto &e : es)
                        ks.push_back(e.k.get());
                    ts = time_interleaved(ks, rounds, min_ms);
                }
                for (size_t i = 0; i < es.size(); i++) {
                    results.push_back({row.type, dir, es[i].provider, es[i].k->path, K, es[i].detail, !check, check ? Timing{} : ts[i], R});
                }
            }
            if (!check) break;  // timed once, on the N(0, 1) rows
        }
    }
}

}  // namespace

std::vector<CodecResult> run_codecs(const std::vector<int64_t> &Ks, const std::vector<std::string> &filters, bool check, int rounds, double min_ms) {
    std::vector<CodecResult> results;
    std::mt19937 rng(7);
    std::normal_distribution<float> normal;
    for (const CodecRow &row : codec_rows()) {
        // Layouts of several rows per record: 2 records' worth of rows, laid
        // out by GGML's repack.
        FormatSpec fs = parse_format(row.type);
        auto wanted = [&](const std::string &n) { return std::find(filters.begin(), filters.end(), n) != filters.end(); };
        // KleidiAI's formats also under q8_0 (GGML's KleidiAI q8_0 path)
        if (!filters.empty() && !wanted(fs.type) && !wanted(row.type) && !(rowwise_format(fs) && wanted("q8_0"))) continue;
        if (rowwise_format(fs)) {
            rowwise_codecs(row, Ks, check, rounds, min_ms, results);
            continue;
        }
        ggml_type t = GGML_TYPE_COUNT;
        for (int i = 0; i < GGML_TYPE_COUNT; i++) {
            const char *n = ggml_get_type_traits((ggml_type)i)->type_name;
            if (n && fs.type == n) t = (ggml_type)i;
        }
        const halide_filter_metadata_t *md = row.metadata();
        int np = md->num_arguments - 1;
        int64_t R = fs.rows > 1 ? 2 * fs.rows : 1, record_bytes = ggml_type_size(t) * fs.rows;
        for (int64_t K : Ks) {
            if (K % ggml_blck_size(t)) continue;
            int64_t nb = K / ggml_blck_size(t), records = nb * R / fs.rows;
            size_t bytes = ggml_row_size(t, K) * R;
            std::vector<float> x(K * R), y(K * R), ref_y(K * R);
            for (float &v : x)
                v = normal(rng);
            adversarial(x.data(), K);
            std::vector<uint8_t> q(bytes), ref(bytes);
            quantize_ref(t, x.data(), ref.data(), R, K);
            if (t == GGML_TYPE_Q8_1 && check) {
                int64_t n = nb * R, differs = consistent_q8_1_sums(ref.data(), n);
                fprintf(stderr, "q8_1 K=%lld: GGML's s differs from f16(f16(d) * sum(qs)) in %lld of %lld blocks (%.1f%%)\n",
                        (long long)K, (long long)differs, (long long)n, 100.0 * differs / n);
            }
            to_float(t, ref.data(), ref_y.data(), K * R);
            if (fs.rows > 1 || fs.soa) {
                std::vector<uint8_t> aos = ref, live;
                if (!relayout(fs, t, aos.data(), ref.data(), R, K)) {
                    results.push_back({row.type, "repack", "ggml", "transcription", K, "no GGML repack to this layout", false, {}, R});
                    continue;
                }
                std::string name = fs.rows > 1 ? ggml_repack_live(t, aos.data(), live, R, K) : "";
                if (check && name == fs.type + "_" + std::to_string(fs.rows) + "x" + std::to_string(fs.chunk)) {
                    // The transcription against GGML itself, where it picks this layout.
                    results.push_back({row.type, "repack", "ggml-repack", name, K, first_diff(ref.data(), live.data(), bytes, bytes / records), false, {}, R});
                }
            }
            std::vector<int64_t> xe{K}, qe{records};
            if (R > 1) xe.push_back(R), qe = {nb, R / fs.rows};
            HBuf bx(x.data(), halide_type_t(halide_type_float, 32), xe), by(y.data(), halide_type_t(halide_type_float, 32), xe);
            Ports bq = port_buffers(md, 1, np, q.data(), qe, record_bytes), bref = port_buffers(md, 1, np, ref.data(), qe, record_bytes);
            for (const char *dir : {"quantize", "dequantize"}) {
                bool quant = dir[0] == 'q';
                struct Entry {
                    std::string provider;
                    std::unique_ptr<Kernel> k;
                    std::string detail;
                };
                std::vector<Entry> es;
                for (int bench : {0, 1, 2}) {  // checked, bench, scaled (checked)
                    if (!check && bench != 1) continue;
                    Argv f = bench == 2 ? row.scaled[!quant] : quant ? row.quantize[bench] :
                                                                       row.dequantize[bench];
                    std::string name = "halide-" + row.type + (bench == 1 ? "" : bench ? ":scaled" :
                                                                                         ":checked");
                    memset(q.data(), 0, bytes);
                    std::fill(y.begin(), y.end(), NAN);
                    int err = quant ? call(f, true, bx, bq) : call(f, false, by, bref);
                    std::string detail = err   ? "halide error " + std::to_string(err) :
                                         quant ? first_diff(q.data(), ref.data(), bytes, bytes / records) :
                                                 first_diff(y.data(), ref_y.data(), K * R * sizeof(float), ggml_blck_size(t) * sizeof(float));
                    es.push_back({name, lambda("halide", quant ? std::function<void()>([=, &bx, &bq] { call(f, true, bx, bq); }) : [=, &bref, &by] { call(f, false, by, bref); }), detail});
                }
                if (!check) {
                    if (quant) {
                        es.push_back({"ggml", lambda(chunked(t) ? "quantize_chunk" : "from_float_ref", [&] { quantize_ref(t, x.data(), q.data(), R, K); }), ""});
                        if (auto from = ggml_get_type_traits_cpu(t)->from_float) {
                            es.push_back({"ggml", lambda("from_float", [&, from] { from(x.data(), q.data(), K * R); }), ""});
                        }
                    } else if (ggml_get_type_traits(t)->to_float) {
                        es.push_back({"ggml", lambda("to_float", [&] { ggml_get_type_traits(t)->to_float(ref.data(), y.data(), K * R); }), ""});
                    }
                }
                std::vector<Timing> ts;
                if (!check) {
                    std::vector<Kernel *> ks;
                    for (auto &e : es)
                        ks.push_back(e.k.get());
                    ts = time_interleaved(ks, rounds, min_ms);
                }
                for (size_t i = 0; i < es.size(); i++) {
                    results.push_back({row.type, dir, es[i].provider, es[i].k->path, K, es[i].detail, !check, check ? Timing{} : ts[i], R});
                }
            }
        }
        if (check) {
            // Crafted blocks, as GGML's (an fp16 scale, then the code bytes):
            // block i's scale is a finite fp16 (zeros, subnormals, both signs,
            // up to 65504); its code bytes count up, so every 16 blocks span
            // every byte value.
            int64_t K = 4096 * ggml_blck_size(t), nb = K / ggml_blck_size(t), bs = ggml_type_size(t);
            std::vector<uint8_t> aos(bs * nb * R), q(aos.size());
            for (int64_t i = 0; i < nb * R; i++) {
                uint16_t h = (uint16_t)((i * 31) % 0x7c00 | (i & 1) << 15);
                memcpy(&aos[i * bs], &h, 2);
                for (int64_t j = 2; j < bs; j++)
                    aos[i * bs + j] = (uint8_t)((i * (bs - 2) + j - 2) & 255);
            }
            std::vector<float> y(K * R), ref_y(K * R);
            to_float(t, aos.data(), ref_y.data(), K * R);
            if (!relayout(fs, t, aos.data(), q.data(), R, K)) continue;
            std::vector<int64_t> ye{K}, qe{nb};
            if (R > 1) ye.push_back(R), qe = {nb, R / fs.rows};
            Ports bq = port_buffers(md, 1, np, q.data(), qe, record_bytes);
            HBuf by(y.data(), halide_type_t(halide_type_float, 32), ye);
            for (auto [f, name] : {std::pair{row.dequantize[0], ":checked"}, {row.scaled[1], ":scaled"}}) {
                std::fill(y.begin(), y.end(), NAN);
                int err = call(f, false, by, bq);
                std::string detail = err ? "halide error " + std::to_string(err) : first_diff(y.data(), ref_y.data(), K * R * sizeof(float), ggml_blck_size(t) * sizeof(float));
                results.push_back({row.type, "dequantize", "halide-" + row.type + name, "crafted", K, detail, false, {}, R});
            }
        }
    }
    return results;
}

}  // namespace gq
