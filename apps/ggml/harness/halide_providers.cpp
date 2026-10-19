// Halide providers. Every row of formats.cmake becomes one GQ_HALIDE_<op> line in
// the generated halide_kernels.inc, registering two providers:
//   halide-<name>          bench build (GGML_BENCH_FEATURES), timed
//   halide-<name>:checked  default (checked) build, correctness only
// Kernel ABI (kernels/matmul.cpp): w: [K / block, N] and a: [K / block, M]
// records of their format (a GGML block struct, or float16/float32 for plain
// values; the element types come from the kernel's metadata; one buffer per
// port, e.g. per field of SoA weights), out: float32 [N, M]. mul_mat is called
// through its _argv entry point; vec_dot (N = M = 1) goes through an adapter
// with GGML's vec_dot ABI. KleidiAI's formats (qsi8cx weights, qai8dx acts;
// rowwise_format()) register under GGML's q8_0: their codes are [rows * QK,
// K / QK, N / rows] and the per-row fields [N] (rowwise_ports()); the q8_0
// weights are requantized to qsi8cx with its codec at setup, untimed, as
// GGML's KleidiAI backend does on load (and qai8dx acts encoded alike).
#include "harness.h"

#include "HalideRuntime.h"
#include "thread_pool.h"

#include <cstring>

namespace gq {

bool halide_keep_awake = true;

namespace {

using Fn = int (*)(halide_buffer_t *, halide_buffer_t *, halide_buffer_t *);

ggml_type type_named(const char *n) {
    for (int t = 0; t < GGML_TYPE_COUNT; t++) {
        const char *tn = ggml_get_type_traits((ggml_type)t)->type_name;
        if (tn && !strcmp(tn, n)) return (ggml_type)t;
    }
    fprintf(stderr, "unknown ggml type %s\n", n);
    abort();
}

int64_t records(ggml_type t, int64_t K) {
    return K / ggml_blck_size(t);
}

halide_type_t arg_type(const halide_filter_metadata_t *md, int i) {
    return md->arguments[i].type;
}

// A codec's quantize (x, ports...) into `ports`; nonzero on error.
int encode(Argv quantize, const HBuf &x, const std::vector<std::unique_ptr<HBuf>> &ports) {
    std::vector<void *> args{(void *)&x.b};
    for (const auto &p : ports)
        args.push_back(&p->b);
    return quantize(args.data());
}

// vec_dot's buffers: their host pointers and extents are patched per call.
struct Buf {
    halide_buffer_t b{};
    halide_dimension_t d[2]{};
    Buf() = default;
    Buf(const void *host, halide_type_t t, int64_t e0, int64_t e1) {
        reset(host, t, e0, e1);
    }
    void reset(const void *host, halide_type_t t, int64_t e0, int64_t e1) {
        b.host = (uint8_t *)host;
        b.type = t;
        b.dimensions = 2;
        b.dim = d;
        d[0] = {0, (int32_t)e0, 1, 0};
        d[1] = {0, (int32_t)e1, (int32_t)e0, 0};
    }
    Buf(const Buf &) = delete;
};

struct HalideKernel : Kernel {
    Argv f;
    bool gpu;
    int threads;
    std::vector<std::vector<std::unique_ptr<HBuf>>> bufs;  // per weight copy: w's ports, a, out
    std::vector<std::vector<void *>> args;
    std::vector<std::vector<uint8_t>> relaid;  // weights in the format's layout
    std::vector<float> host_out;
    halide_buffer_t *out() {
        return &bufs[0].back()->b;
    }
    ~HalideKernel() override {
        for (auto &c : bufs)
            for (auto &b : c)
                halide_device_free(nullptr, &b->b);
    }
    void run(int reps) override {
        halide_set_num_threads(threads);
        // Like GGML's threadpool, which spins during graph compute and is
        // paused between samples, keep the pool's idle threads polling for
        // work for the duration of the sample.
        const bool keep_awake = halide_keep_awake && !gpu;
        if (keep_awake) ggml_halide_thread_pool_keep_awake(true);
        for (int r = 0, c = 0; r < reps; r++, c = c + 1 == (int)args.size() ? 0 : c + 1) {
            if (int e = f(args[c].data())) {
                fprintf(stderr, "halide error %d\n", e);
                abort();
            }
        }
        if (keep_awake) ggml_halide_thread_pool_keep_awake(false);
        if (gpu) halide_device_sync(nullptr, out());
    }
    void read(float *o) override {
        if (gpu) halide_copy_to_host(nullptr, out());
        memcpy(o, host_out.data(), host_out.size() * sizeof(float));
    }
};

// GGML's vec_dot ABI over a Halide kernel; one instantiation per kernel. The
// buffers are built once (vec_dot runs single-threaded) and only their host
// pointers and extents are patched per call: building three halide_buffer_t
// per call costs ~3 ns, more than the n = 256 dot itself.
template<Fn F, const halide_filter_metadata_t *(*M)()>
struct VecDot {
    static inline int wshift, ashift;  // log2 of the block sizes: no divide per call
    static inline Buf w, a, o;
    static void bind(ggml_type wt, ggml_type at) {
        wshift = __builtin_ctzll(ggml_blck_size(wt)), ashift = __builtin_ctzll(ggml_blck_size(at));
        w.reset(nullptr, arg_type(M(), 0), 0, 1);
        a.reset(nullptr, arg_type(M(), 1), 0, 1);
        o.reset(nullptr, halide_type_t(halide_type_float, 32), 1, 1);
    }
    static void call(int n, float *s, size_t, const void *x, size_t, const void *y, size_t, int) {
        w.b.host = (uint8_t *)x, w.d[0].extent = w.d[1].stride = n >> wshift;
        a.b.host = (uint8_t *)y, a.d[0].extent = a.d[1].stride = n >> ashift;
        o.b.host = (uint8_t *)s;
        F(&w.b, &a.b, &o.b);
    }
};

struct Row {
    const char *name, *wt, *at, *aq;  // aq: the act's compute type (at: re-quantized inside)
    int ops;
    const char *features;
    Argv checked, bench;
    ggml_vec_dot_t vd_checked, vd_bench;  // vec_dot only
    void (*bind_checked)(ggml_type, ggml_type), (*bind_bench)(ggml_type, ggml_type);
    const halide_filter_metadata_t *(*md)();
};

std::vector<Row> &rows() {
    static std::vector<Row> r;
    return r;
}

struct AddRow {
    explicit AddRow(Row r) {
        rows().push_back(r);
    }
};

struct AddCodec {
    explicit AddCodec(CodecRow r) {
        codec_rows().push_back(r);
    }
};

}  // namespace
}  // namespace gq

#define GQ_HALIDE_vec_dot(name, wt, at, aq, features)                                                                    \
    static gq::AddRow name##_row({#name, wt, #at, #aq, gq::GQ_vec_dot, features, name##_checked_argv, name##_bench_argv, \
                                  gq::VecDot<name##_checked, name##_checked_metadata>::call,                             \
                                  gq::VecDot<name##_bench, name##_bench_metadata>::call,                                 \
                                  gq::VecDot<name##_checked, name##_checked_metadata>::bind,                             \
                                  gq::VecDot<name##_bench, name##_bench_metadata>::bind, name##_checked_metadata});
#define GQ_HALIDE_mul_mat(name, wt, at, aq, features) \
    static gq::AddRow name##_row({#name, wt, #at, #aq, gq::GQ_mul_mat, features, name##_checked_argv, name##_bench_argv, nullptr, nullptr, nullptr, nullptr, name##_checked_metadata});
#define GQ_CODEC(t, spec) \
    static gq::AddCodec t##_codec({spec, {t##_quantize_checked_argv, t##_quantize_bench_argv}, {t##_dequantize_checked_argv, t##_dequantize_bench_argv}, t##_quantize_checked_metadata, {t##_quantize_scaled_checked_argv, t##_dequantize_scaled_checked_argv}});
#include "halide_kernels.inc"

namespace gq {

std::vector<CodecRow> &codec_rows() {
    static std::vector<CodecRow> r;
    return r;
}

const CodecRow *codec_row(const std::string &spec) {
    for (const CodecRow &r : codec_rows()) {
        if (r.type == spec) return &r;
    }
    return nullptr;
}

void register_halide_providers() {
    for (const Row &row : rows()) {
        FormatSpec fs = parse_format(row.wt);
        // KleidiAI's formats run as its GGML backend does q8_0 matmuls: the
        // q8_0 weights re-quantized to qsi8cx at setup (untimed, as GGML's
        // packer at load), f32 activations quantized to qai8dx, inside the
        // kernel (f32:qai8dx) or at setup (qai8dx).
        bool kai = rowwise_format(fs), kai_act = kai && rowwise_format(parse_format(row.at));
        ggml_type wt = kai ? GGML_TYPE_Q8_0 : type_named(fs.type.c_str()), at = kai ? GGML_TYPE_F32 : type_named(row.at),
                  aq = kai ? GGML_TYPE_Q8_0 : type_named(parse_format(row.aq).type.c_str());
        bool gpu = strstr(row.features, "metal") != nullptr;
        for (bool bench : {false, true}) {
            Argv f = bench ? row.bench : row.checked;
            ggml_vec_dot_t vd = bench ? row.vd_bench : row.vd_checked;
            if (row.ops & GQ_vec_dot) (bench ? row.bind_bench : row.bind_checked)(wt, at);
            std::string name = std::string("halide-") + row.name + (bench ? "" : ":checked");
            if (row.ops & GQ_vec_dot && !gpu) {
                providers().push_back({name, GQ_vec_dot, bench, [=](const Inputs &in) -> std::unique_ptr<Kernel> {
                                           if (in.wt != wt || in.at != at) return nullptr;
                                           auto k = vec_dot_kernel(vd, in);
                                           k->path = "halide";
                                           if (aq != at) k->prec.act_quant = aq;
                                           return k;
                                       }});
            }
            if (row.ops & GQ_mul_mat) {
                providers().push_back({name, GQ_mul_mat, bench, [=](const Inputs &in) -> std::unique_ptr<Kernel> {
                                           if (in.wt != wt || in.at != at || in.s.N % fs.rows) return nullptr;
                                           auto k = std::make_unique<HalideKernel>();
                                           const auto &s = in.s;
                                           const halide_filter_metadata_t *md = row.md();
                                           k->f = f;
                                           k->gpu = gpu;
                                           k->threads = in.threads;
                                           k->path = gpu ? "halide:metal" : "halide";
                                           if (aq != at) k->prec.act_quant = aq;
                                           if (kai) k->prec.act_block = k->prec.w_block = s.K;  // as ggml-cpu-kleidiai's
                                           size_t wbytes = ggml_row_size(wt, s.K) * s.N;
                                           // w's ports, a's, then out
                                           int nw = 0;
                                           for (int i = 0; i < md->num_arguments; i++)
                                               nw += md->arguments[i].name[0] == 'w';
                                           int na = md->num_arguments - 1 - nw;
                                           const CodecRow *wcodec = kai ? codec_row(row.wt) : nullptr, *acodec = kai_act ? codec_row(row.at) : nullptr;
                                           if (kai && (!wcodec || (kai_act && !acodec))) return nullptr;
                                           k->host_out.resize(s.N * s.M);
                                           for (int c = 0; c < in.copies; c++) {
                                               const uint8_t *wc = in.w->data() + c * wbytes;
                                               std::vector<std::unique_ptr<HBuf>> bs;
                                               if (kai) {
                                                   std::vector<float> x(s.N * s.K);
                                                   to_float(wt, wc, x.data(), s.N * s.K);
                                                   HBuf bx(x.data(), halide_type_t(halide_type_float, 32), {s.K, s.N});
                                                   bs = rowwise_ports(md, 0, nw, fs, s.K, s.N, k->relaid);
                                                   if (encode(wcodec->quantize[0], bx, bs)) return nullptr;
                                               } else {
                                                   if (fs.rows > 1 || fs.soa) {
                                                       k->relaid.emplace_back(wbytes);
                                                       if (!relayout(fs, wt, wc, k->relaid.back().data(), s.N, s.K)) return nullptr;
                                                       wc = k->relaid.back().data();
                                                   }
                                                   bs = port_buffers(md, 0, nw, wc, {records(wt, s.K), s.N / fs.rows}, ggml_type_size(wt) * fs.rows);
                                               }
                                               if (!c) {
                                                   // one copy of a and out, shared by every weight copy
                                                   if (kai_act) {
                                                       HBuf bx(in.a->data(), halide_type_t(halide_type_float, 32), {s.K, s.M});
                                                       auto ba = rowwise_ports(md, nw, na, parse_format(row.at), s.K, s.M, k->relaid);
                                                       if (encode(acodec->quantize[0], bx, ba)) return nullptr;
                                                       for (auto &b : ba)
                                                           bs.push_back(std::move(b));
                                                   } else {
                                                       bs.push_back(std::make_unique<HBuf>(in.a->data(), arg_type(md, nw), std::vector<int64_t>{records(at, s.K), s.M}));
                                                   }
                                                   bs.push_back(std::make_unique<HBuf>(k->host_out.data(), halide_type_t(halide_type_float, 32), std::vector<int64_t>{s.N, s.M}));
                                               }
                                               k->args.emplace_back();
                                               for (int i = 0; i < md->num_arguments; i++) {
                                                   k->args.back().push_back(&(i < nw || !c ? bs[i] : k->bufs[0][i])->b);
                                                   if (gpu && i < nw + na && (i < nw || !c)) bs[i]->b.flags |= halide_buffer_flag_host_dirty;
                                               }
                                               k->bufs.push_back(std::move(bs));
                                           }
                                           return k;
                                       }});
            }
        }
    }
}

}  // namespace gq
