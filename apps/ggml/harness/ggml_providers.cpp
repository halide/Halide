#include "harness.h"

#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

#ifdef GQ_CUPTI
#include <cupti.h>
#endif

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

// The GPU kernels a case runs, from a child process (--probe-gpu), comma
// separated; nullopt if the case crashed or failed: the GPU backends'
// supports_op accept cases they abort on (Metal's ignores src1's type, mul_mv
// has no q4_0 x f16 kernel; CUDA's batched cuBLAS has no q4_0 x f16
// conversion).
std::optional<std::string> gpu_probe(const Inputs &in) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "exec 2>/dev/null; '%s' --probe-gpu %s %s %lld %lld %lld", self_exe.c_str(),
             ggml_type_name(in.wt), ggml_type_name(in.at), (long long)in.s.K, (long long)in.s.N, (long long)in.s.M);
    FILE *f = popen(cmd, "r");
    if (!f) return std::nullopt;
    std::string out;
    char line[512];
    while (fgets(line, sizeof(line), f))
        out += line;
    if (pclose(f) != 0) return std::nullopt;
    while (!out.empty() && isspace(out.back()))
        out.pop_back();
    return out;
}

#ifdef GQ_CUPTI
// The kernels a CUDA graph launches, by their (mangled) names, from CUPTI's
// launch callbacks (NVIDIA's public profiling API; the CUDA counterpart of
// Metal's logged pipeline names).
std::vector<std::string> cuda_launches;

void CUPTIAPI cupti_cb(void *, CUpti_CallbackDomain, CUpti_CallbackId, const void *data) {
    const auto *cb = (const CUpti_CallbackData *)data;
    if (cb->callbackSite == CUPTI_API_ENTER && cb->functionName && strstr(cb->functionName, "Launch") && cb->symbolName) {
        cuda_launches.emplace_back(cb->symbolName);
    }
}

std::vector<std::string> cuda_kernels(Kernel &k) {
    CUpti_SubscriberHandle sub = nullptr;
    if (cuptiSubscribe(&sub, (CUpti_CallbackFunc)cupti_cb, nullptr) != CUPTI_SUCCESS) return {};
    cuda_launches.clear();
    cuptiEnableDomain(1, sub, CUPTI_CB_DOMAIN_RUNTIME_API);
    cuptiEnableDomain(1, sub, CUPTI_CB_DOMAIN_DRIVER_API);
    k.run(1);
    cuptiUnsubscribe(sub);
    return std::exchange(cuda_launches, {});
}
#else
std::vector<std::string> cuda_kernels(Kernel &k) {
    k.run(1);
    return {};
}
#endif

// GGML's CUDA mul_mat path (ggml_cuda_mul_mat), from the kernels it launched
// (`ks`, gpu_probe()), and what it does to the operands:
//   mmvq (mul_mat_vec_q), mmq (mul_mat_q): f32 acts quantized to q8_1 (dp4a/int8 mma)
//   mmvf (mul_mat_vec_f): f16 weights times acts rounded to f16, in half2
//     per-thread partial sums (>= 32 threads, 2 lanes each), then f32
//   mmf (mul_mat_f): tensor-core mma on f16/bf16 (f32: tf32) operands, f32 accumulators
//   cublas: weights and acts converted to f16 (bf16 for bf16), cuBLAS gemm;
//     on f16, CUBLAS_COMPUTE_16F with an f16 result (Ampere and later)
std::string cuda_path(const std::string &ks, const Inputs &in, Precision &p) {
    auto has = [&](const char *s) { return ks.find(s) != std::string::npos; };
    const bool bf = in.wt == GGML_TYPE_BF16;
    if (has("mul_mat_vec_q") || has("mul_mat_q")) {
        p.act_quant = GGML_TYPE_Q8_1;
        return has("mul_mat_vec_q") ? "cuda:mmvq" : "cuda:mmq";
    }
    if (has("mul_mat_vec_f")) {
        if (in.wt == GGML_TYPE_F16) {
            p.operand_bits = 11;
            p.acc_bits = 11;
            p.chain = in.s.K / 64;
        }
        return "cuda:mmvf";
    }
    if (has("mul_mat_f")) {
        p.operand_bits = bf ? 8 : 11;
        return "cuda:mmf";
    }
    if (ks.empty()) {
        // no attribution: assume the worst (cuBLAS's)
        p.operand_bits = bf ? 8 : 11;
        p.acc_bits = bf ? 24 : 11;
        return "cuda:?";
    }
    if (in.wt != GGML_TYPE_F32) {
        p.operand_bits = bf ? 8 : 11;
        if (!bf) p.acc_bits = 11;
    }
    return "cuda:cublas";
}

void add_device(ggml_backend_dev_t dev) {
    std::string dname = ggml_backend_dev_name(dev);
    std::string rname = ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev));
    auto type = ggml_backend_dev_type(dev);
    bool cpu = type == GGML_BACKEND_DEVICE_TYPE_CPU;
    bool gpu = type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU;
    bool metal = gpu && dname.rfind("MTL", 0) == 0, cuda = gpu && rname == "CUDA";
    std::string name = cpu ? "ggml-cpu" : metal ? "ggml-metal" :
                                      cuda      ? "ggml-cuda" :
                                                  "ggml-" + lower(dname);
    providers().push_back({name, GQ_mul_mat, true, [dev, cpu, metal, cuda](const Inputs &in) -> std::unique_ptr<Kernel> {
                               std::optional<std::string> probe = cpu ? std::optional<std::string>("") : gpu_probe(in);
                               if (!probe) return nullptr;
                               auto k = make_graph(dev, nullptr, in);
                               if (!k) return nullptr;
                               if (cpu) {
                                   k->prec = cpu_precision(in);
                                   bool kai_f16 = in.wt == GGML_TYPE_F16 && in.at == GGML_TYPE_F32 && in.s.M > 1 && ggml_cpu_has_sme();
                                   k->path = kai_f16 ? "kleidiai:f16:SME" : std::string("vec_dot:") + ggml_type_name(dot_type(in.wt));
                                   // x86: GGML's tiled gemm (ggml-cpu/tiled) for these
                                   // weights at M >= 8, unless GGML_CPU_TILED_MM=0
                                   const char *tiled_env = getenv("GGML_CPU_TILED_MM");
                                   static const ggml_type tiled_types[] = {GGML_TYPE_Q2_K, GGML_TYPE_Q3_K, GGML_TYPE_Q4_K, GGML_TYPE_Q5_K, GGML_TYPE_Q6_K,
                                                                           GGML_TYPE_IQ4_XS, GGML_TYPE_IQ2_XXS, GGML_TYPE_IQ2_XS, GGML_TYPE_IQ2_S,
                                                                           GGML_TYPE_IQ3_XXS, GGML_TYPE_IQ3_S, GGML_TYPE_IQ1_S, GGML_TYPE_IQ1_M};
                                   if (ggml_cpu_has_avx() && in.s.M >= 8 && !(tiled_env && atoi(tiled_env) == 0) &&
                                       std::find(std::begin(tiled_types), std::end(tiled_types), in.wt) != std::end(tiled_types)) {
                                       k->path = std::string("tiled:") + ggml_type_name(in.wt);
                                   }
                               } else if (metal) {
                                   // mul_mm stages both operands in half; mul_mv may not: assume the worse.
                                   k->prec.operand_bits = 11;
                                   k->path = "metal:" + (probe->empty() ? std::string("?") : *probe);
                               } else if (cuda) {
                                   k->path = cuda_path(*probe, in, k->prec);
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
                                       // (AMX's: "<tensor> of type <type>", repacked to its tiles' layout)
                                       if (auto p = layout.find(" of type "); p != std::string::npos) layout = layout.substr(p + 9);
                                       // AMX keeps f16 as is and runs it on AVX-512, not tiles.
                                       if (layout.empty() && bname == "amx" && in.wt == GGML_TYPE_F16) layout = "f16(avx512)";
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

// Child process: run one case on the first GPU and print the kernels it ran:
// Metal's mul_m* pipelines (compiled, and logged, once per process, so a
// fresh process is the robust way to attribute them to a case), CUDA's
// launched kernels (CUPTI).
int probe_gpu(int argc, char **argv) {
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
        std::string names;
        if (std::string(ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev))) == "CUDA") {
            for (const auto &n : cuda_kernels(*k))
                names += (names.empty() ? "" : ",") + n;
        } else {
            k->run(1);
            for (const auto &l : take_log()) {
                std::string n = grep_log({l}, "name = '", '\'');
                if (n.rfind("kernel_mul_m", 0) == 0) names += (names.empty() ? "" : ",") + n;
            }
        }
        printf("%s\n", names.c_str());
        return 0;
    }
    return 1;
}

}  // namespace gq
