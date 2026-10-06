#include "harness.h"

#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace gq {

bool verbose = false;
std::string self_exe;

namespace {

std::mutex log_mu;
std::vector<std::string> log_lines;
std::string log_partial;

void log_cb(ggml_log_level level, const char *text, void *) {
    std::lock_guard<std::mutex> lock(log_mu);
    if (verbose || (level >= GGML_LOG_LEVEL_WARN && level != GGML_LOG_LEVEL_CONT)) {
        fputs(text, stderr);
    }
    log_partial += text;
    for (size_t p; (p = log_partial.find('\n')) != std::string::npos;) {
        log_lines.push_back(log_partial.substr(0, p));
        log_partial.erase(0, p + 1);
    }
}

// Text after `key` up to the next `stop`, from the first captured line containing `key`.
std::string grep_log(const std::vector<std::string> &lines, const std::string &key, char stop = '\n') {
    for (const auto &l : lines) {
        if (auto p = l.find(key); p != std::string::npos) {
            std::string s = l.substr(p + key.size());
            return s.substr(0, s.find(stop));
        }
    }
    return "";
}

std::string lower(std::string s) {
    for (char &c : s)
        c = std::tolower(c);
    return s;
}

struct VecDot : Kernel {
    ggml_vec_dot_t f;
    Inputs in;
    size_t wbytes;
    float out = 0;
    void run(int reps) override {
        const uint8_t *w = in.w->data(), *a = in.a->data();
        for (int r = 0, c = 0; r < reps; r++, c = c + 1 == in.copies ? 0 : c + 1) {
            f(in.s.K, &out, 0, w + c * wbytes, 0, a, 0, 1);
        }
    }
    void read(float *o) override {
        *o = out;
    }
};

// out = W x A for every weight copy, G nodes per graph, so that per-graph
// overhead (large on Metal) is amortized as in a real model's layer stack.
struct GraphKernel : Kernel {
    ggml_backend_t be = nullptr;
    ggml_threadpool *tp = nullptr;
    ggml_context *wctx = nullptr, *ctx = nullptr;
    ggml_backend_buffer_t wbuf = nullptr, buf = nullptr;
    ggml_cgraph *gf = nullptr;
    ggml_tensor *last = nullptr;
    int64_t N = 0, M = 0;
    ~GraphKernel() override {
        if (buf) ggml_backend_buffer_free(buf);
        if (wbuf) ggml_backend_buffer_free(wbuf);
        if (ctx) ggml_free(ctx);
        if (wctx) ggml_free(wctx);
        if (be) ggml_backend_free(be);
        if (tp) ggml_threadpool_free(tp);
    }
    void run(int reps) override {
        if (tp) ggml_threadpool_resume(tp);
        for (int r = 0; r < reps; r++) {
            if (ggml_backend_graph_compute(be, gf) != GGML_STATUS_SUCCESS) {
                fprintf(stderr, "graph compute failed\n");
                abort();
            }
        }
        // Stop the pool's spin-waiting from stealing cycles from other providers.
        if (tp) ggml_threadpool_pause(tp);
    }
    void read(float *o) override {
        ggml_backend_tensor_get(last, o, 0, N * M * sizeof(float));
    }
};

constexpr int kNodes = 16;

// buft: where W lives (nullptr: the device default). Returns nullptr if unsupported.
std::unique_ptr<GraphKernel> make_graph(ggml_backend_dev_t dev, ggml_backend_buffer_type_t buft, const Inputs &in) {
    auto k = std::make_unique<GraphKernel>();
    const auto &s = in.s;
    k->N = s.N;
    k->M = s.M;
    int G = k->batch = std::max(kNodes, in.copies);
    ggml_init_params p{ggml_tensor_overhead() * (in.copies + G + 4) + ggml_graph_overhead_custom(2 * G + 4, false),
                       nullptr, true};
    k->wctx = ggml_init(p);
    k->ctx = ggml_init(p);
    std::vector<ggml_tensor *> W;
    for (int c = 0; c < in.copies; c++)
        W.push_back(ggml_new_tensor_2d(k->wctx, in.wt, s.K, s.N));
    ggml_tensor *A = ggml_new_tensor_2d(k->ctx, in.at, s.K, s.M);
    k->wbuf = ggml_backend_alloc_ctx_tensors_from_buft(k->wctx, buft ? buft : ggml_backend_dev_buffer_type(dev));
    if (!k->wbuf) return nullptr;
    ggml_backend_buffer_set_usage(k->wbuf, GGML_BACKEND_BUFFER_USAGE_WEIGHTS);
    k->gf = ggml_new_graph_custom(k->ctx, 2 * G + 4, false);
    for (int g = 0; g < G; g++) {
        k->last = ggml_mul_mat(k->ctx, W[g % in.copies], A);
        ggml_build_forward_expand(k->gf, k->last);
    }
    // Must precede tensor_set: extra buffer types repack in set_tensor and
    // abort on types they do not support.
    if (!ggml_backend_dev_supports_op(dev, k->last)) return nullptr;
    k->be = ggml_backend_dev_init(dev, nullptr);
    k->buf = ggml_backend_alloc_ctx_tensors(k->ctx, k->be);
    size_t wbytes = ggml_nbytes(W[0]);
    for (int c = 0; c < in.copies; c++)
        ggml_backend_tensor_set(W[c], in.w->data() + c * wbytes, 0, wbytes);
    ggml_backend_tensor_set(A, in.a->data(), 0, ggml_nbytes(A));
    if (ggml_backend_is_cpu(k->be)) {
        auto tpp = ggml_threadpool_params_default(in.threads);
        k->tp = ggml_threadpool_new(&tpp);
        ggml_backend_cpu_set_n_threads(k->be, in.threads);
        ggml_backend_cpu_set_threadpool(k->be, k->tp);
        ggml_threadpool_pause(k->tp);
    } else if (auto set = (ggml_backend_set_n_threads_t)ggml_backend_reg_get_proc_address(
                   ggml_backend_dev_backend_reg(dev), "ggml_backend_set_n_threads")) {
        set(k->be, in.threads);
    }
    return k;
}

ggml_type dot_type(ggml_type t) {
    return ggml_get_type_traits_cpu(t)->vec_dot_type;
}

// The CPU vec_dot loop (also used by the repack kernels): integer or
// f32 products per block, f32 accumulation; f16 weights accumulate in f16
// lanes (32 of them, then 3 f16 tree levels) when the CPU has fp16 arithmetic.
Precision cpu_precision(const Inputs &in) {
    Precision p;
    ggml_type vdt = dot_type(in.wt);
    if (in.at != vdt && vdt != GGML_TYPE_F32) p.act_quant = vdt;
    if (in.wt == GGML_TYPE_F16 && ggml_cpu_has_fp16_va()) {
        p.acc_bits = 11;
        p.chain = in.s.K / 32 + 3;
    }
    return p;
}

// Empty if the case crashed or failed: Metal's supports_op ignores src1's
// type, but e.g. mul_mv has no q4_0 x f16 kernel.
std::string metal_path(const Inputs &in) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "exec 2>/dev/null; '%s' --probe-metal %s %s %lld %lld %lld", self_exe.c_str(),
             ggml_type_name(in.wt), ggml_type_name(in.at), (long long)in.s.K, (long long)in.s.N, (long long)in.s.M);
    FILE *f = popen(cmd, "r");
    if (!f) return "";
    std::string out;
    char line[512];
    while (fgets(line, sizeof(line), f))
        out += line;
    if (pclose(f) != 0) return "";
    while (!out.empty() && isspace(out.back()))
        out.pop_back();
    return "metal:" + (out.empty() ? std::string("?") : out);
}

void add_device(ggml_backend_dev_t dev) {
    std::string dname = ggml_backend_dev_name(dev);
    auto type = ggml_backend_dev_type(dev);
    bool cpu = type == GGML_BACKEND_DEVICE_TYPE_CPU;
    bool gpu = type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU;
    std::string name = cpu ? "ggml-cpu" : gpu ? "ggml-metal" :
                                                "ggml-" + lower(dname);
    if (gpu && dname.rfind("MTL", 0) != 0) name = "ggml-" + lower(dname);
    providers().push_back({name, GQ_mul_mat, true, [dev, cpu, gpu](const Inputs &in) -> std::unique_ptr<Kernel> {
                               std::string mpath = gpu ? metal_path(in) : "";
                               if (gpu && mpath.empty()) return nullptr;
                               auto k = make_graph(dev, nullptr, in);
                               if (!k) return nullptr;
                               if (cpu) {
                                   k->prec = cpu_precision(in);
                                   bool kai_f16 = in.wt == GGML_TYPE_F16 && in.at == GGML_TYPE_F32 && in.s.M > 1 && ggml_cpu_has_sme();
                                   k->path = kai_f16 ? "kleidiai:f16:SME" : std::string("vec_dot:") + ggml_type_name(dot_type(in.wt));
                               } else if (gpu) {
                                   // mul_mm stages both operands in half; mul_mv may not: assume the worse.
                                   k->prec.operand_bits = 11;
                                   k->path = mpath;
                               } else {
                                   k->path = "blas:sgemm(dequant)";
                               }
                               return k;
                           }});
    if (!cpu) return;
    auto get = (ggml_backend_dev_get_extra_bufts_t)ggml_backend_reg_get_proc_address(
        ggml_backend_dev_backend_reg(dev), "ggml_backend_dev_get_extra_bufts");
    for (auto *b = get ? get(dev) : nullptr; b && *b; b++) {
        ggml_backend_buffer_type_t buft = *b;
        std::string bname = lower(ggml_backend_buft_name(buft));
        if (bname.rfind("cpu_", 0) == 0) bname = bname.substr(4);
        providers().push_back({"ggml-cpu-" + bname, GQ_mul_mat, true, [dev, buft, bname](const Inputs &in) -> std::unique_ptr<Kernel> {
                                   take_log();
                                   auto k = make_graph(dev, buft, in);
                                   if (!k) return nullptr;
                                   auto log = take_log();
                                   k->prec = cpu_precision(in);
                                   std::string kind = in.s.M == 1 ? "gemv" : "gemm";
                                   if (bname == "kleidiai") {
                                       // KleidiAI re-quantizes: q4_0 activations to 8-bit blocks of
                                       // 32 (q8_0-like); q8_0 weights AND activations per row.
                                       bool q8 = in.wt == GGML_TYPE_Q8_0;
                                       k->prec.act_quant = GGML_TYPE_Q8_0;
                                       k->prec.act_block = q8 ? in.s.K : 32;
                                       k->prec.w_block = q8 ? in.s.K : 0;
                                       k->path = std::string("kleidiai:") + (q8 ? "qai8dx_qsi8cx" : "qsi8d32_qsi4c32") + ":" + kind;
                                   } else {
                                       std::string layout = grep_log(log, "repack tensor ");
                                       if (auto p = layout.find(" with "); p != std::string::npos) layout = layout.substr(p + 6);
                                       k->path = bname + ":" + (layout.empty() ? "?" : layout) + ":" + kind;
                                   }
                                   return k;
                               }});
    }
}

}  // namespace

std::vector<std::string> take_log() {
    std::lock_guard<std::mutex> lock(log_mu);
    return std::exchange(log_lines, {});
}

std::unique_ptr<Kernel> vec_dot_kernel(ggml_vec_dot_t f, const Inputs &in) {
    auto k = std::make_unique<VecDot>();
    k->f = f;
    k->in = in;
    k->wbytes = ggml_row_size(in.wt, in.s.K);
    return k;
}

void register_ggml_providers() {
    ggml_log_set(log_cb, nullptr);
    ggml_cpu_init();
    providers().push_back({"ggml-vec_dot", GQ_vec_dot, true, [](const Inputs &in) -> std::unique_ptr<Kernel> {
                               const auto *t = ggml_get_type_traits_cpu(in.wt);
                               if (!t->vec_dot || in.at != t->vec_dot_type) return nullptr;
                               auto k = vec_dot_kernel(t->vec_dot, in);
                               k->prec = cpu_precision(in);
                               k->path = std::string("vec_dot:") + ggml_type_name(in.at);
                               return k;
                           }});
    for (size_t i = 0; i < ggml_backend_dev_count(); i++)
        add_device(ggml_backend_dev_get(i));
}

// Child process: run one case on Metal and print the mul_m* pipelines it
// compiled. Metal compiles (and logs) a pipeline once per process, so a fresh
// process is the robust way to attribute pipelines to a case.
int probe_metal(int argc, char **argv) {
    if (argc < 5) return 1;
    auto type = [](const char *n) {
        for (int t = 0; t < GGML_TYPE_COUNT; t++) {
            if (ggml_get_type_traits((ggml_type)t)->type_name && !strcmp(ggml_get_type_traits((ggml_type)t)->type_name, n)) return (ggml_type)t;
        }
        return GGML_TYPE_COUNT;
    };
    ggml_log_set(log_cb, nullptr);
    Inputs in{type(argv[0]), type(argv[1]), {"", "", atoll(argv[2]), atoll(argv[3]), atoll(argv[4])}, 1, nullptr, 1, nullptr};
    std::vector<uint8_t> w(ggml_row_size(in.wt, in.s.K) * in.s.N), a(ggml_row_size(in.at, in.s.K) * in.s.M);
    in.w = &w;
    in.a = &a;
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(dev) != GGML_BACKEND_DEVICE_TYPE_GPU && ggml_backend_dev_type(dev) != GGML_BACKEND_DEVICE_TYPE_IGPU) continue;
        auto k = make_graph(dev, nullptr, in);
        if (!k) return 1;
        k->run(1);
        std::string names;
        for (const auto &l : take_log()) {
            std::string n = grep_log({l}, "name = '", '\'');
            if (n.rfind("kernel_mul_m", 0) == 0) names += (names.empty() ? "" : ",") + n;
        }
        printf("%s\n", names.c_str());
        return 0;
    }
    return 1;
}

}  // namespace gq
