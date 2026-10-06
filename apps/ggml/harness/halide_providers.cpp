// Halide providers. Every row of formats.cmake becomes one GQ_HALIDE line in
// the generated halide_kernels.inc, registering two providers:
//   halide-<name>          bench build (GGML_BENCH_FEATURES), timed
//   halide-<name>:checked  default (checked) build, correctness only
// Kernel ABI (every generator): w: uint8 [row bytes, N] in ggml's layout of
// the weight type; a: [K, M] float32/float16, or uint8 [row bytes, M] for a
// quantized activation type; out: float32 [N, M]. vec_dot (N = M = 1) goes
// through an adapter with GGML's vec_dot ABI.
#include "harness.h"

#include "HalideRuntime.h"

#include <cstring>

namespace gq {
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

halide_type_t elem_type(ggml_type t) {
    return t == GGML_TYPE_F32 ? halide_type_t(halide_type_float, 32) : t == GGML_TYPE_F16 ? halide_type_t(halide_type_float, 16) :
                                                                                            halide_type_t(halide_type_uint, 8);
}

int64_t extent0(ggml_type t, int64_t K) {
    return ggml_is_quantized(t) ? (int64_t)ggml_row_size(t, K) : K;
}

struct Buf {
    halide_buffer_t b{};
    halide_dimension_t d[2]{};
    Buf() = default;
    Buf(const void *host, halide_type_t t, int64_t e0, int64_t e1) {
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
    Fn f;
    bool gpu;
    int threads;
    std::vector<std::unique_ptr<Buf>> w;
    std::unique_ptr<Buf> a, out;
    std::vector<float> host_out;
    ~HalideKernel() override {
        for (auto &b : w)
            halide_device_free(nullptr, &b->b);
        halide_device_free(nullptr, &a->b);
        halide_device_free(nullptr, &out->b);
    }
    void run(int reps) override {
        halide_set_num_threads(threads);
        for (int r = 0, c = 0; r < reps; r++, c = c + 1 == (int)w.size() ? 0 : c + 1) {
            if (int e = f(&w[c]->b, &a->b, &out->b)) {
                fprintf(stderr, "halide error %d\n", e);
                abort();
            }
        }
        if (gpu) halide_device_sync(nullptr, &out->b);
    }
    void read(float *o) override {
        if (gpu) halide_copy_to_host(nullptr, &out->b);
        memcpy(o, host_out.data(), host_out.size() * sizeof(float));
    }
};

// GGML's vec_dot ABI over a Halide kernel; one instantiation per kernel.
template<Fn F>
struct VecDot {
    static inline ggml_type wt, at;
    static void call(int n, float *s, size_t, const void *x, size_t, const void *y, size_t, int) {
        Buf w(x, halide_type_t(halide_type_uint, 8), ggml_row_size(wt, n), 1);
        Buf a(y, elem_type(at), extent0(at, n), 1);
        Buf o(s, halide_type_t(halide_type_float, 32), 1, 1);
        F(&w.b, &a.b, &o.b);
    }
};

struct Row {
    const char *name, *wt, *at;
    int ops;
    const char *features;
    Fn checked, bench;
    ggml_vec_dot_t vd_checked, vd_bench;
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

}  // namespace
}  // namespace gq

#define GQ_HALIDE(name, wt, at, ops, features)                                                    \
    static gq::AddRow name##_row({#name, #wt, #at, [] { using namespace gq; return (int)(ops); }(), features,                            \
                                  name##_checked, name##_bench, gq::VecDot<name##_checked>::call, \
                                  gq::VecDot<name##_bench>::call});
#include "halide_kernels.inc"

namespace gq {

void register_halide_providers() {
    for (const Row &row : rows()) {
        ggml_type wt = type_named(row.wt), at = type_named(row.at);
        bool gpu = strstr(row.features, "metal") != nullptr;
        for (bool bench : {false, true}) {
            Fn f = bench ? row.bench : row.checked;
            ggml_vec_dot_t vd = bench ? row.vd_bench : row.vd_checked;
            std::string name = std::string("halide-") + row.name + (bench ? "" : ":checked");
            if (row.ops & GQ_vec_dot && !gpu) {
                providers().push_back({name, GQ_vec_dot, bench, [=](const Inputs &in) -> std::unique_ptr<Kernel> {
                                           if (in.wt != wt || in.at != at) return nullptr;
                                           auto k = vec_dot_kernel(vd, in);
                                           k->path = "halide";
                                           return k;
                                       }});
            }
            if (row.ops & GQ_mul_mat) {
                providers().push_back({name, GQ_mul_mat, bench, [=](const Inputs &in) -> std::unique_ptr<Kernel> {
                                           if (in.wt != wt || in.at != at) return nullptr;
                                           auto k = std::make_unique<HalideKernel>();
                                           const auto &s = in.s;
                                           k->f = f;
                                           k->gpu = gpu;
                                           k->threads = in.threads;
                                           k->path = gpu ? "halide:metal" : "halide";
                                           size_t wbytes = ggml_row_size(wt, s.K) * s.N;
                                           for (int c = 0; c < in.copies; c++) {
                                               k->w.push_back(std::make_unique<Buf>(in.w->data() + c * wbytes, halide_type_t(halide_type_uint, 8), ggml_row_size(wt, s.K), s.N));
                                           }
                                           k->a = std::make_unique<Buf>(in.a->data(), elem_type(at), extent0(at, s.K), s.M);
                                           k->host_out.resize(s.N * s.M);
                                           k->out = std::make_unique<Buf>(k->host_out.data(), halide_type_t(halide_type_float, 32), s.N, s.M);
                                           if (gpu) {
                                               for (auto &b : k->w)
                                                   b->b.flags |= halide_buffer_flag_host_dirty;
                                               k->a->b.flags |= halide_buffer_flag_host_dirty;
                                           }
                                           return k;
                                       }});
            }
        }
    }
}

}  // namespace gq
