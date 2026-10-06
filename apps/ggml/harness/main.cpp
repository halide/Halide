// ggml-quant-bench: compare GGML-format quantized kernels (GGML's own CPU,
// CPU extra-buffer, Metal and BLAS paths, plus registered Halide kernels)
// against an f64 oracle and each other. See ../README.md.
#include "harness.h"

#include "ggml-cpu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <sstream>

#ifdef __APPLE__
#include <pthread/qos.h>
#include <sys/sysctl.h>
#endif

namespace gq {

std::vector<Provider> &providers() {
    static std::vector<Provider> p;
    return p;
}

}  // namespace gq

using namespace gq;

namespace {

struct Options {
    int ops = GQ_vec_dot | GQ_mul_mat;
    std::vector<std::string> types, acts{"dot", "f16", "f32"}, filters;
    std::string shapes = "smoke";
    int threads = 0, rounds = 15;
    double min_ms = 20;
    bool check = false, json = false, cold = false;
};

std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    for (std::string t; std::getline(ss, t, ',');)
        v.push_back(t);
    return v;
}

const char *usage =
    "usage: ggml-quant-bench [options]\n"
    "  --op vec_dot|mul_mat     (default: both)\n"
    "  --types q4_0,q8_0,...    weight types (default: all GGML can mul_mat)\n"
    "  --acts dot,f16,f32       activation types; dot = the weight's vec_dot_type\n"
    "  --shapes PRESET          smoke|gemv|full|<model>-gemv|<model>-full|KxNxM|<n> (default smoke)\n"
    "  --providers a,b          only providers whose name contains one of these\n"
    "  --threads N              (default: performance cores)\n"
    "  --rounds R --min-ms T    timing rounds; min ms per sample (15, 20)\n"
    "  --check                  correctness only, exit 1 on any failure\n"
    "  --cold                   cycle >= 512 MiB of weight copies (cache-cold)\n"
    "  --json                   JSON lines instead of CSV\n"
    "  --verbose                echo GGML's log\n";

int perf_cores() {
#ifdef __APPLE__
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.perflevel0.physicalcpu", &n, &len, nullptr, 0) == 0 && n > 0) return n;
#endif
    return 4;
}

bool weight_type_ok(ggml_type t) {
    const auto *tt = ggml_get_type_traits(t);
    // Removed types have blck_size 0; q8_1/q8_K etc. are activation-only (no vec_dot).
    return tt->blck_size > 0 && (tt->to_float || t == GGML_TYPE_F32) && ggml_get_type_traits_cpu(t)->vec_dot &&
           (ggml_is_quantized(t) || t == GGML_TYPE_F16 || t == GGML_TYPE_BF16 || t == GGML_TYPE_F32);
}

ggml_type type_named(const std::string &n) {
    for (int t = 0; t < GGML_TYPE_COUNT; t++) {
        const char *tn = ggml_get_type_traits((ggml_type)t)->type_name;
        if (tn && n == tn) return (ggml_type)t;
    }
    return GGML_TYPE_COUNT;
}

void convert_rows(ggml_type t, const float *src, uint8_t *dst, int64_t rows, int64_t K) {
    size_t rs = ggml_row_size(t, K);
    auto from = ggml_get_type_traits_cpu(t)->from_float;
    for (int64_t r = 0; r < rows; r++) {
        if (t == GGML_TYPE_F32) {
            memcpy(dst + r * rs, src + r * K, rs);
        } else if (from) {
            from(src + r * K, dst + r * rs, K);
        } else {
            ggml_quantize_chunk(t, src + r * K, dst + r * rs, 0, 1, K, nullptr);
        }
    }
}

struct Emitter {
    bool json;
    bool header = false;
    void row(const std::vector<std::pair<std::string, std::string>> &kv) {
        if (json) {
            printf("{");
            for (size_t i = 0; i < kv.size(); i++) {
                const char *v = kv[i].second.c_str();
                char *end = nullptr;
                strtod(v, &end);
                bool num = *v && !*end && std::isfinite(strtod(v, nullptr));
                printf("%s\"%s\": %s%s%s", i ? ", " : "", kv[i].first.c_str(), num ? "" : "\"", kv[i].second.c_str(), num ? "" : "\"");
            }
            printf("}\n");
        } else {
            if (!header) {
                for (size_t i = 0; i < kv.size(); i++)
                    printf("%s%s", i ? "," : "", kv[i].first.c_str());
                printf("\n");
                header = true;
            }
            for (size_t i = 0; i < kv.size(); i++)
                printf("%s%s", i ? "," : "", kv[i].second.c_str());
            printf("\n");
        }
        fflush(stdout);
    }
};

std::string fmt(double x, const char *f = "%.4g") {
    char b[64];
    snprintf(b, sizeof(b), f, x);
    return b;
}

}  // namespace

int main(int argc, char **argv) {
    self_exe = argv[0];
    if (argc > 1 && !strcmp(argv[1], "--probe-metal")) return probe_metal(argc - 2, argv + 2);
#ifdef __APPLE__
    // Prefer performance cores for the timing thread and the pools it spawns.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    // G independent mul_mats per graph must run back to back, as dependent layers would.
    setenv("GGML_METAL_CONCURRENCY_DISABLE", "1", 1);

    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s needs a value\n%s", a.c_str(), usage);
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--op") {
            std::string v = next();
            o.ops = v == "vec_dot" ? GQ_vec_dot : v == "mul_mat" ? GQ_mul_mat :
                                                                   GQ_vec_dot | GQ_mul_mat;
        } else if (a == "--types") {
            o.types = split(next());
        } else if (a == "--acts") {
            o.acts = split(next());
        } else if (a == "--shapes") {
            o.shapes = next();
        } else if (a == "--providers") {
            o.filters = split(next());
        } else if (a == "--threads") {
            o.threads = std::stoi(next());
        } else if (a == "--rounds") {
            o.rounds = std::stoi(next());
        } else if (a == "--min-ms") {
            o.min_ms = std::stod(next());
        } else if (a == "--check") {
            o.check = true;
        } else if (a == "--cold") {
            o.cold = true;
        } else if (a == "--json") {
            o.json = true;
        } else if (a == "--verbose") {
            verbose = true;
        } else {
            fprintf(stderr, "%s", usage);
            return a == "--help" ? 0 : 2;
        }
    }
    if (!o.threads) o.threads = perf_cores();

    register_ggml_providers();
    register_halide_providers();

    std::vector<ggml_type> wtypes;
    if (o.types.empty()) {
        for (int t = 0; t < GGML_TYPE_COUNT; t++) {
            if (weight_type_ok((ggml_type)t)) wtypes.push_back((ggml_type)t);
        }
    } else {
        for (const auto &n : o.types) {
            ggml_type t = type_named(n);
            if (t == GGML_TYPE_COUNT || !weight_type_ok(t)) {
                fprintf(stderr, "unsupported weight type %s\n", n.c_str());
                return 2;
            }
            wtypes.push_back(t);
        }
    }

    Emitter out{o.json};
    std::mt19937 rng(42);
    std::normal_distribution<float> normal;
    int failures = 0, cases = 0;
    for (int op : {GQ_vec_dot, GQ_mul_mat}) {
        if (!(o.ops & op)) continue;
        for (const Shape &s : shapes(o.shapes, op)) {
            for (ggml_type wt : wtypes) {
                if (s.K % ggml_blck_size(wt)) continue;
                std::vector<ggml_type> ats;
                for (const auto &an : o.acts) {
                    ggml_type at = an == "dot" ? ggml_get_type_traits_cpu(wt)->vec_dot_type : type_named(an);
                    if (at != GGML_TYPE_COUNT && std::find(ats.begin(), ats.end(), at) == ats.end()) ats.push_back(at);
                }
                // Weights: N(0, 1), quantized by GGML (an all-ones importance
                // matrix for types that require one).
                size_t wrow = ggml_row_size(wt, s.K), wbytes = wrow * s.N;
                int copies = o.cold ? (int)std::max<int64_t>(1, ((int64_t)512 << 20) / wbytes + 1) : 1;
                std::vector<float> wf(s.N * s.K);
                for (float &x : wf)
                    x = normal(rng);
                std::vector<float> ones(s.K, 1.0f);
                std::vector<uint8_t> w(wbytes * copies);
                ggml_quantize_chunk(wt, wf.data(), w.data(), 0, s.N, s.K, ggml_quantize_requires_imatrix(wt) ? ones.data() : nullptr);
                for (int c = 1; c < copies; c++)
                    memcpy(&w[c * wbytes], w.data(), wbytes);
                std::vector<float> af(s.M * s.K);
                for (float &x : af)
                    x = normal(rng);

                for (ggml_type at : ats) {
                    if (s.K % ggml_blck_size(at)) continue;
                    std::vector<uint8_t> a(ggml_row_size(at, s.K) * s.M);
                    convert_rows(at, af.data(), a.data(), s.M, s.K);
                    Inputs in{wt, at, s, op == GQ_vec_dot ? 1 : o.threads, &w, copies, &a};
                    Reference ref = reference(in, af);

                    struct Entry {
                        const Provider *p;
                        std::unique_ptr<Kernel> k;
                        double err;
                    };
                    std::vector<Entry> es;
                    for (const Provider &p : providers()) {
                        if (!(p.ops & op)) continue;
                        if (!o.check && !p.timed) continue;
                        if (!o.filters.empty() && std::none_of(o.filters.begin(), o.filters.end(), [&](const std::string &f) { return p.name.find(f) != std::string::npos; })) continue;
                        auto k = p.make(in);
                        if (!k) continue;
                        k->run(1);
                        std::vector<float> got(s.N * s.M);
                        k->read(got.data());
                        double err = error_ratio(in, ref, k->prec, got.data());
                        es.push_back({&p, std::move(k), err});
                    }
                    if (es.empty()) continue;
                    std::vector<Timing> ts;
                    if (!o.check) {
                        std::vector<Kernel *> ks;
                        for (auto &e : es)
                            ks.push_back(e.k.get());
                        ts = time_interleaved(ks, o.rounds, o.min_ms);
                    }
                    for (size_t i = 0; i < es.size(); i++) {
                        auto &e = es[i];
                        bool ok = e.err <= 1;
                        cases++;
                        failures += !ok;
                        std::vector<std::pair<std::string, std::string>> kv = {
                            {"op", op == GQ_vec_dot ? "vec_dot" : "mul_mat"},
                            {"wtype", ggml_type_name(wt)},
                            {"atype", ggml_type_name(at)},
                            {"model", s.model},
                            {"layer", s.layer},
                            {"K", std::to_string(s.K)},
                            {"N", std::to_string(s.N)},
                            {"M", std::to_string(s.M)},
                            {"threads", std::to_string(in.threads)},
                            {"cold", o.cold ? "1" : "0"},
                            {"provider", e.p->name},
                            {"path", e.k->path},
                            {"err_ratio", fmt(e.err)},
                            {"ok", ok ? "1" : "0"},
                        };
                        if (!o.check) {
                            const Timing &t = ts[i];
                            double bytes = (double)wrow * s.N + ggml_row_size(at, s.K) * s.M + 4.0 * s.N * s.M;
                            kv.insert(kv.end(), {
                                                    {"ns", fmt(t.median, "%.1f")},
                                                    {"ns_lo", fmt(t.lo, "%.1f")},
                                                    {"ns_hi", fmt(t.hi, "%.1f")},
                                                    {"ci_pct", fmt(100 * (t.hi - t.lo) / t.median, "%.2f")},
                                                    {"samples", std::to_string(t.samples)},
                                                    {"reps", std::to_string(t.reps)},
                                                    {"gbps", fmt(bytes / t.median, "%.2f")},
                                                    {"gops", fmt(2.0 * s.K * s.N * s.M / t.median, "%.2f")},
                                                });
                        }
                        out.row(kv);
                    }
                }
            }
        }
    }
    fprintf(stderr, "%d results, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
