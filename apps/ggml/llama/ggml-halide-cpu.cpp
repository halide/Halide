// The Halide kernels in llama.cpp's CPU backend (see README.md), by
// GGML_HALIDE (a list):
// - vec_dot: ggml-cpu's vec_dot type traits are the Halide vec_dot kernels
//   (GGML's vec_dot ABI, one row at a time: nrows = 1), still driven by
//   ggml-cpu's own mul_mat loop, threads and activation quantization.
// - mul_mat: an extra buffer type (HALIDE, as GGML's CPU_REPACK) whose
//   weights' MUL_MATs run the Halide mul_mat kernels on ggml-cpu's threads:
//   the activations quantized in parallel (GGML's from_float, as ggml-cpu),
//   then each thread the Halide kernel on chunks of the weight rows (Halide's
//   own parallel loops serial).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "HalideRuntime.h"
#include "ggml-backend-impl.h"
#include "ggml-cpu.h"
#include "ggml-impl.h"
#include "traits.h"

// Generated: the kernels' headers, and GGML_HALIDE_VEC_DOT(name, weight, act)
// per kernel.
#include "ggml_halide_kernels.h"

namespace {

using Fn = int (*)(halide_buffer_t *, halide_buffer_t *, halide_buffer_t *);
using Md = const halide_filter_metadata_t *(*)();

struct Buf {
    halide_buffer_t b{};
    halide_dimension_t d[2]{};
    void reset(halide_type_t t, int64_t e0) {
        b.type = t;
        b.dimensions = 2;
        b.dim = d;
        d[0] = {0, (int32_t)e0, 1, 0};
        d[1] = {0, 1, (int32_t)e0, 0};
    }
};

// One instantiation per kernel. ggml-cpu calls vec_dot from all its threads:
// each its own buffers, built once, only their host pointers and extents
// patched per call (building them per call costs more than a short dot).
template<Fn F, Md M>
struct VecDot {
    static inline int wshift, ashift;  // log2 of the block sizes: no divide per call
    struct Bufs {
        Buf w, a, o;
        Bufs() {
            w.reset(M()->arguments[0].type, 0);
            a.reset(M()->arguments[1].type, 0);
            o.reset(halide_type_t(halide_type_float, 32), 1);
        }
    };
    static void bind(ggml_type wt, ggml_type at) {
        wshift = __builtin_ctzll(ggml_blck_size(wt)), ashift = __builtin_ctzll(ggml_blck_size(at));
    }
    static void call(int n, float *s, size_t, const void *x, size_t, const void *y, size_t, int) {
        thread_local Bufs b;
        b.w.b.host = (uint8_t *)x, b.w.d[0].extent = b.w.d[1].stride = n >> wshift;
        b.a.b.host = (uint8_t *)y, b.a.d[0].extent = b.a.d[1].stride = n >> ashift;
        b.o.b.host = (uint8_t *)s;
        if (int e = F(&b.w.b, &b.a.b, &b.o.b)) {
            fprintf(stderr, "ggml-halide: vec_dot error %d\n", e);
            abort();
        }
    }
};

struct Kernel {
    const char *name;
    ggml_type weight, act;
    ggml_vec_dot_t call;
    void (*bind)(ggml_type, ggml_type);
};

const Kernel kernels[] = {
#define GGML_HALIDE_VEC_DOT(name, w, a) {#name, GGML_TYPE_##w, GGML_TYPE_##a, VecDot<name, name##_metadata>::call, VecDot<name, name##_metadata>::bind},
    GGML_HALIDE_VEC_DOTS
#undef GGML_HALIDE_VEC_DOT
};

bool enabled(const char *what) {
    const char *e = getenv("GGML_HALIDE");
    return e && strstr(e, what);
}

// mul_mat

struct MulMat {
    const char *name;
    ggml_type weight, act;
    Fn f;
    Md md;
};

const MulMat mul_mats[] = {
#define GGML_HALIDE_MUL_MAT(name, w, a) {#name, GGML_TYPE_##w, GGML_TYPE_##a, name, name##_metadata},
    GGML_HALIDE_MUL_MATS
#undef GGML_HALIDE_MUL_MAT
};

// Halide's parallel loops, inside a ggml-cpu thread's chunk: serial
int serial_do_par_for(void *uc, halide_task_t f, int min, int size, uint8_t *closure) {
    for (int i = min; i < min + size; i++) {
        if (int e = f(uc, i, closure)) return e;
    }
    return 0;
}

struct Buf2 : Buf {
    Buf2(void *host, halide_type_t t, int64_t e0, int64_t e1, int64_t s1) {
        reset(t, e0);
        b.host = (uint8_t *)host;
        d[1] = {0, (int32_t)e1, (int32_t)s1, 0};
    }
};

// A weight's MUL_MATs (tensor->extra)
class Traits : public ggml::cpu::tensor_traits {
    const MulMat &k;

public:
    explicit Traits(const MulMat &k)
        : k(k) {
    }

    bool work_size(int, const ggml_tensor *op, size_t &size) override {
        size = ggml_row_size(k.act, ggml_nelements(op->src[1]));
        return true;
    }

    bool compute_forward(ggml_compute_params *params, ggml_tensor *op) override {
        if (op->op != GGML_OP_MUL_MAT) return false;
        const ggml_tensor *src0 = op->src[0], *src1 = op->src[1];
        const int ith = params->ith, nth = params->nth;
        const int64_t K = src0->ne[0], N = src0->ne[1], M = src1->ne[1];
        const size_t arow = ggml_row_size(k.act, K);
        char *wdata = (char *)params->wdata;
        const ggml_from_float_t from_float = ggml_get_type_traits_cpu(k.act)->from_float;
        for (int64_t m = ith; m < M; m += nth) {
            from_float((const float *)((const char *)src1->data + m * src1->nb[1]), wdata + m * arow, K);
        }
        // Chunks of rows: 4 per thread (as GGML's), of 16 rows (Halide's
        // gemm and gemv tiles' multiple)
        const int64_t align = 16, nchunk = std::max<int64_t>(1, std::min<int64_t>(4 * nth, N / align));
        const int64_t dr = (N + nchunk - 1) / nchunk;
        if (ith == 0) ggml_threadpool_chunk_set(params->threadpool, nth);
        ggml_barrier(params->threadpool);
        const halide_type_t wt = k.md()->arguments[0].type, at = k.md()->arguments[1].type;
        const int64_t wrecs = K / ggml_blck_size(src0->type), arecs = K / ggml_blck_size(k.act);
        for (int64_t c = ith; c < nchunk; c = ggml_threadpool_chunk_add(params->threadpool, 1)) {
            int64_t n0 = std::min(N, (c * dr + align - 1) / align * align), n1 = std::min(N, ((c + 1) * dr + align - 1) / align * align);
            if (n0 >= n1) continue;
            Buf2 w((char *)src0->data + n0 * src0->nb[1], wt, wrecs, n1 - n0, src0->nb[1] / ggml_type_size(src0->type));
            Buf2 a(wdata, at, arecs, M, arow / ggml_type_size(k.act));
            // The kernels' out is dense: a chunk's, for M > 1, to scratch
            const int64_t nn = n1 - n0;
            float *dst = (float *)((char *)op->data + n0 * op->nb[0]);
            thread_local std::vector<float> scratch;
            if (M > 1 && nn != N) scratch.resize(nn * M);
            float *obuf = M > 1 && nn != N ? scratch.data() : dst;
            Buf2 o(obuf, halide_type_t(halide_type_float, 32), nn, M, nn);
            if (int e = k.f(&w.b, &a.b, &o.b)) {
                fprintf(stderr, "ggml-halide: %s error %d\n", k.name, e);
                abort();
            }
            if (obuf != dst) {
                for (int64_t m = 0; m < M; m++) {
                    memcpy((char *)dst + m * op->nb[1], obuf + m * nn, nn * sizeof(float));
                }
            }
        }
        return true;
    }
};

const MulMat *mul_mat_for(ggml_type t) {
    for (const MulMat &k : mul_mats) {
        if (k.weight == t && ggml_get_type_traits_cpu(t)->vec_dot_type == k.act) return &k;
    }
    return nullptr;
}

Traits *traits_for(ggml_type t) {
    static std::vector<std::unique_ptr<Traits>> ts = [] {
        std::vector<std::unique_ptr<Traits>> ts(GGML_TYPE_COUNT);
        for (int i = 0; i < GGML_TYPE_COUNT; i++) {
            if (const MulMat *k = mul_mat_for((ggml_type)i)) ts[i] = std::make_unique<Traits>(*k);
        }
        return ts;
    }();
    return ts[t].get();
}

ggml_backend_buffer_type_t halide_buffer_type();

enum ggml_status init_tensor(ggml_backend_buffer_t, ggml_tensor *t) {
    t->extra = traits_for(t->type);
    return GGML_STATUS_SUCCESS;
}

ggml_backend_buffer_t alloc_buffer(ggml_backend_buffer_type_t buft, size_t size) {
    ggml_backend_buffer_t b = ggml_backend_buft_alloc_buffer(ggml_backend_cpu_buffer_type(), size);
    if (!b) return nullptr;
    b->buft = buft;
    b->iface.init_tensor = init_tensor;
    return b;
}

class ExtraBufferType : public ggml::cpu::extra_buffer_type {
    bool supports_op(ggml_backend_dev_t, const ggml_tensor *op) override {
        const ggml_tensor *src0 = op->src[0], *src1 = op->src[1];
        return op->op == GGML_OP_MUL_MAT && src0->buffer && src0->buffer->buft == halide_buffer_type() &&
               ggml_n_dims(src0) == 2 && traits_for(src0->type) && src1->type == GGML_TYPE_F32 &&
               src1->ne[2] == 1 && src1->ne[3] == 1 && ggml_is_contiguous(src1) && ggml_is_contiguous(op) &&
               (!src1->buffer || ggml_backend_buft_is_host(src1->buffer->buft));
    }
    ggml::cpu::tensor_traits *get_tensor_traits(const ggml_tensor *op) override {
        if (op->op == GGML_OP_MUL_MAT && op->src[0]->buffer && op->src[0]->buffer->buft == halide_buffer_type()) {
            return (ggml::cpu::tensor_traits *)op->src[0]->extra;
        }
        return nullptr;
    }
};

ggml_backend_buffer_type_t halide_buffer_type() {
    static ggml_backend_buffer_type buft = {
        /* .iface    = */ {
            /* .get_name         = */ [](ggml_backend_buffer_type_t) { return "HALIDE"; },
            /* .alloc_buffer     = */ alloc_buffer,
            /* .alloc_buffer_n   = */ nullptr,
            /* .get_alignment    = */ [](ggml_backend_buffer_type_t) -> size_t { return 64; },
            /* .get_max_size     = */ nullptr,
            /* .get_alloc_size   = */ nullptr,
            /* .get_alloc_size_n = */ nullptr,
            /* .is_host          = */ nullptr,
        },
        /* .device  = */ ggml_backend_reg_dev_get(ggml_backend_cpu_reg(), 0),
        /* .context = */ new ExtraBufferType(),
    };
    return &buft;
}

}  // namespace

// The HALIDE extra buffer type, if GGML_HALIDE has mul_mat (else nullptr)
extern "C" ggml_backend_buffer_type_t ggml_backend_cpu_halide_buffer_type(void);

extern "C" ggml_backend_buffer_type_t ggml_backend_cpu_halide_buffer_type(void) {
    static const bool on = [] {
        if (!enabled("mul_mat")) return false;
        halide_set_num_threads(1);
        halide_set_custom_do_par_for(serial_do_par_for);
        for (const MulMat &k : mul_mats) {
            if (mul_mat_for(k.weight) == &k) fprintf(stderr, "ggml-halide: mul_mat %s\n", k.name);
        }
        return true;
    }();
    return on ? halide_buffer_type() : nullptr;
}

extern "C" void ggml_halide_override_traits(struct ggml_type_traits_cpu *traits, bool *overridden);

extern "C" void ggml_halide_override_traits(struct ggml_type_traits_cpu *traits, bool *overridden) {
    if (!enabled("vec_dot")) return;
    for (const Kernel &k : kernels) {
        if (traits[k.weight].vec_dot_type != k.act) continue;
        k.bind(k.weight, k.act);
        traits[k.weight].vec_dot = k.call;
        traits[k.weight].nrows = 1;
        overridden[k.weight] = true;  // (and not llamafile's sgemm)
        fprintf(stderr, "ggml-halide: vec_dot %s\n", k.name);
    }
}
