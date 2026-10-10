// The codec op: every format's Halide quantize/dequantize against GGML's
// reference quantizer and to_float, bitwise, on N(0, 1) rows whose first
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

}  // namespace

std::vector<CodecResult> run_codecs(const std::vector<int64_t> &Ks, const std::vector<std::string> &filters, bool check, int rounds, double min_ms) {
    std::vector<CodecResult> results;
    std::mt19937 rng(7);
    std::normal_distribution<float> normal;
    for (const CodecRow &row : codec_rows()) {
        // Layouts of several rows per record: 2 records' worth of rows, laid
        // out by GGML's repack.
        FormatSpec fs = parse_format(row.type);
        if (!filters.empty() && std::find(filters.begin(), filters.end(), fs.type) == filters.end() && std::find(filters.begin(), filters.end(), row.type) == filters.end()) continue;
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
            ggml_quantize_chunk(t, x.data(), ref.data(), 0, R, K, nullptr);
            ggml_get_type_traits(t)->to_float(ref.data(), ref_y.data(), K * R);
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
                        es.push_back({"ggml", lambda("quantize_chunk", [&] { ggml_quantize_chunk(t, x.data(), q.data(), 0, R, K, nullptr); }), ""});
                        if (auto from = ggml_get_type_traits_cpu(t)->from_float) {
                            es.push_back({"ggml", lambda("from_float", [&, from] { from(x.data(), q.data(), K * R); }), ""});
                        }
                    } else {
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
            ggml_get_type_traits(t)->to_float(aos.data(), ref_y.data(), K * R);
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
