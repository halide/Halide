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
#include <thread>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread/qos.h>
#include <sys/sysctl.h>
#include <unistd.h>
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
    int ops = GQ_vec_dot | GQ_mul_mat | GQ_codec;
    std::vector<std::string> types, acts{"dot", "f16", "f32"}, filters;
    std::string shapes = "smoke";
    int threads = 0, rounds = 15;
    double min_ms = 20;
    bool check = false, json = false, cold = false, pretouch = true;
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
    "  --op vec_dot|mul_mat|codec  (default: all)\n"
    "  --types q4_0,q8_0,...    weight types (default: all GGML can mul_mat)\n"
    "  --acts dot,f16,f32       activation types; dot = the weight's vec_dot_type\n"
    "  --shapes PRESET          smoke|gemv|full|<model>-gemv|<model>-full|KxNxM|<n> (default smoke)\n"
    "  --providers a,b          only providers whose name contains one of these\n"
    "  --threads N              (default: performance cores)\n"
    "  --rounds R --min-ms T    timing rounds; min ms per sample (15, 20)\n"
    "  --check                  correctness only, exit 1 on any failure\n"
    "  --cold                   cycle >= 512 MiB of weight copies (cache-cold)\n"
    "  --no-keep-awake          let Halide's idle threads sleep during samples\n"
    "  --no-pretouch            don't fault in every page of the loaded images' __TEXT and\n"
    "                           __DATA_CONST at startup, nor the 16 KB past each input\n"
    "                           buffer (see README: layout sensitivity)\n"
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

// GGML's quantizer on rows split across threads (rows are independent: the
// same bytes as one call; the iq* searches otherwise dominate the bench's run).
void quantize_rows(ggml_type t, const float *src, uint8_t *dst, int64_t rows, int64_t K) {
    const float *w = importance(t, K);
    ggml_quantize_init(t);
    int64_t n = std::min<int64_t>(rows, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> ts;
    for (int64_t i = 0; i < n; i++) {
        int64_t r0 = rows * i / n, r1 = rows * (i + 1) / n;
        ts.emplace_back([=] { ggml_quantize_chunk(t, src + r0 * K, dst + r0 * ggml_row_size(t, K), 0, r1 - r0, K, w); });
    }
    for (std::thread &th : ts)
        th.join();
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

// An input buffer followed by >= 16 KB of resident (written) slack. macOS
// malloc page-aligns large blocks, so e.g. a K=4096 f32 act row ends exactly at
// a 16 KB page edge, and the page after it is often mapped but never touched.
// Streaming a buffer to within ~2 KB of such a page makes the hardware
// prefetcher walk the missing translation on every call (failures aren't
// cached), halving the IPC of long-bodied kernels that read the buffer. Real
// inputs live in reused compute buffers with resident neighbours.
std::vector<uint8_t> input_buffer(size_t n, bool slack) {
    constexpr size_t kSlack = 16 << 10;
    std::vector<uint8_t> v;
    if (slack) {
        v.reserve(n + kSlack);
        v.resize(n + kSlack);
    }
    v.resize(n);
    return v;
}

#ifdef __APPLE__
// Read one byte of every page of an image's __TEXT and __DATA_CONST, so that all
// of their pages have valid translations. A kernel that randomly indexes a
// constant table (q5_0/q5_1 qh tables, iq grids) near a page boundary whose
// neighbouring page was never touched otherwise pays a failed hardware page walk
// per row, which slows DRAM-streaming kernels by up to ~25% depending on link
// layout (README, "Benchmark methodology").
void pretouch_image(const struct mach_header *mh, intptr_t slide) {
    if (mh->magic != MH_MAGIC_64) return;
    const auto *h = (const mach_header_64 *)mh;
    const uint8_t *p = (const uint8_t *)(h + 1);
    const uint64_t page = getpagesize();
    for (uint32_t i = 0; i < h->ncmds; i++, p += ((const load_command *)p)->cmdsize) {
        const auto *s = (const segment_command_64 *)p;
        if (s->cmd != LC_SEGMENT_64 || (strcmp(s->segname, "__TEXT") && strcmp(s->segname, "__DATA_CONST"))) continue;
        for (uint64_t a = s->vmaddr; a < s->vmaddr + s->vmsize; a += page)
            (void)*(volatile const uint8_t *)(a + slide);
    }
}
#endif

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
                                              v == "codec"       ? GQ_codec :
                                                                   GQ_vec_dot | GQ_mul_mat | GQ_codec;
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
        } else if (a == "--no-keep-awake") {
            halide_keep_awake = false;
        } else if (a == "--no-pretouch") {
            o.pretouch = false;
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
#ifdef __APPLE__
    // Also called for images loaded later (e.g. by the Metal backend).
    if (o.pretouch) _dyld_register_func_for_add_image(pretouch_image);
#endif

    register_ggml_providers();
    register_halide_providers();

    std::vector<ggml_type> wtypes;
    if (o.types.empty()) {
        for (int t = 0; t < GGML_TYPE_COUNT; t++) {
            if (weight_type_ok((ggml_type)t)) wtypes.push_back((ggml_type)t);
        }
    } else {
        for (const auto &n : o.types) {
            // activation-only types (q8_1): their codecs only
            ggml_type t = type_named(n);
            // KleidiAI's formats, which GGML has no type for: their codecs only
            if (t == GGML_TYPE_COUNT && o.ops == GQ_codec && rowwise_format(parse_format(n))) continue;
            if (t == GGML_TYPE_COUNT || (!weight_type_ok(t) && o.ops != GQ_codec)) {
                fprintf(stderr, "unsupported weight type %s\n", n.c_str());
                return 2;
            }
            if (weight_type_ok(t)) wtypes.push_back(t);
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
                // Weights: N(0, 1), quantized by GGML (the synthetic importance
                // matrix for types that require one: importance).
                size_t wrow = ggml_row_size(wt, s.K), wbytes = wrow * s.N;
                int copies = o.cold ? (int)std::max<int64_t>(1, ((int64_t)512 << 20) / wbytes + 1) : 1;
                std::vector<float> wf(s.N * s.K);
                for (float &x : wf)
                    x = normal(rng);
                std::vector<uint8_t> w = input_buffer(wbytes * copies, o.pretouch);
                quantize_rows(wt, wf.data(), w.data(), s.N, s.K);
                for (int c = 1; c < copies; c++)
                    memcpy(&w[c * wbytes], w.data(), wbytes);
                std::vector<float> af(s.M * s.K);
                for (float &x : af)
                    x = normal(rng);

                for (ggml_type at : ats) {
                    if (s.K % ggml_blck_size(at)) continue;
                    std::vector<uint8_t> a = input_buffer(ggml_row_size(at, s.K) * s.M, o.pretouch);
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
    if (o.ops & GQ_codec) {
        std::vector<int64_t> Ks;
        for (const Shape &s : shapes(o.shapes, GQ_vec_dot)) {
            if (std::find(Ks.begin(), Ks.end(), s.K) == Ks.end()) Ks.push_back(s.K);
        }
        for (const CodecResult &r : run_codecs(Ks, o.types, o.check, o.rounds, o.min_ms)) {
            if (!o.filters.empty() && std::none_of(o.filters.begin(), o.filters.end(), [&](const std::string &f) { return r.provider.find(f) != std::string::npos; })) continue;
            bool ok = r.detail.empty();
            cases++;
            failures += !ok;
            if (!ok) fprintf(stderr, "codec %s %s %s K=%lld: %s\n", r.type.c_str(), r.dir.c_str(), r.provider.c_str(), (long long)r.K, r.detail.c_str());
            // Same columns as above; atype is the direction, gops is Gvalues/s.
            std::vector<std::pair<std::string, std::string>> kv = {
                {"op", "codec"}, {"wtype", r.type}, {"atype", r.dir}, {"model", ""}, {"layer", ""}, {"K", std::to_string(r.K)}, {"N", std::to_string(r.rows)}, {"M", "1"}, {"threads", "1"}, {"cold", "0"}, {"provider", r.provider}, {"path", r.path}, {"err_ratio", ok ? "0" : "inf"}, {"ok", ok ? "1" : "0"}};
            if (r.timed) {
                double bytes = r.rows * (4.0 * r.K + ggml_row_size(type_named(parse_format(r.type).type), r.K));
                kv.insert(kv.end(), {{"ns", fmt(r.t.median, "%.1f")}, {"ns_lo", fmt(r.t.lo, "%.1f")}, {"ns_hi", fmt(r.t.hi, "%.1f")}, {"ci_pct", fmt(100 * (r.t.hi - r.t.lo) / r.t.median, "%.2f")}, {"samples", std::to_string(r.t.samples)}, {"reps", std::to_string(r.t.reps)}, {"gbps", fmt(bytes / r.t.median, "%.2f")}, {"gops", fmt(r.rows * r.K / r.t.median, "%.2f")}});
            }
            out.row(kv);
        }
    }
    fprintf(stderr, "%d results, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
