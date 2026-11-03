// The Halide kernels in llama.cpp's CPU backend (see README.md), by
// GGML_HALIDE (a list):
// - vec_dot: ggml-cpu's vec_dot type traits are the Halide vec_dot kernels
//   (GGML's vec_dot ABI, one row at a time: nrows = 1), still driven by
//   ggml-cpu's own mul_mat loop, threads and activation quantization.
// - mul_mat: an extra buffer type (HALIDE, as GGML's CPU_REPACK) whose
//   weights, converted at load to their kernel's layout (e.g. GGML's
//   repack's q4_K_8x8), run the Halide mul_mat kernels (f32 activations,
//   quantized inside) on ggml-cpu's threads: thread 0 calls the kernel, whose
//   parallel loops' tasks the others take.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "HalideRuntime.h"
#include "ggml-backend-impl.h"
#include "ggml-cpu.h"
#include "ggml-impl.h"
#include "harness.h"
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
#define GGML_HALIDE_VEC_DOT(name, w, a, spec) {#name, GGML_TYPE_##w, GGML_TYPE_##a, VecDot<name, name##_metadata>::call, VecDot<name, name##_metadata>::bind},
    GGML_HALIDE_VEC_DOTS
#undef GGML_HALIDE_VEC_DOT
};

bool enabled(const char *what) {
    const char *e = getenv("GGML_HALIDE");
    return e && strstr(e, what);
}

// mul_mat

// spec: the weight's format (e.g. q4_K.8x8: GGML's repack's layout, converted
// to at load, set_tensor); act: the type the kernel quantizes to (GGML's
// vec_dot_type)
struct MulMat {
    const char *name, *spec;
    ggml_type weight, act;
    Fn f;
    Md md;
};

const MulMat mul_mats[] = {
#define GGML_HALIDE_MUL_MAT(name, w, a, spec) {#name, spec, GGML_TYPE_##w, GGML_TYPE_##a, name, name##_metadata},
    GGML_HALIDE_MUL_MATS
#undef GGML_HALIDE_MUL_MAT
};

inline void cpu_relax() {
#if defined(__aarch64__)
    __asm__ __volatile__("yield");
#elif defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}

// A MUL_MAT's ggml-cpu threads (in its wdata) as Halide's: thread 0 runs the
// kernel; each of its parallel loops is a job, whose tasks all threads claim,
// a grain at a time, from one word, job << 40 | size << 20 | next task: a
// claim succeeds only while its job has tasks left, so the task's fields
// (published before the job) are still the job's. (The claim word, the done
// count and the rest each have their own cache line: on one, the claims'
// and dones' traffic doubled a gemv's time.)
struct Pool {
    static constexpr int bits = 20;
    static constexpr uint64_t mask = (1 << bits) - 1;
    static constexpr size_t line = 128;
    alignas(line) std::atomic<uint64_t> claim{0};
    alignas(line) std::atomic<int> done{0};
    alignas(line) std::atomic<bool> finished{false};
    std::atomic<int> err{0};
    std::atomic<halide_task_t> f{nullptr};
    std::atomic<void *> uc{nullptr};
    std::atomic<uint8_t *> closure{nullptr};
    std::atomic<int> min{0}, grain{1};

    // Claims and runs the current job's tasks; false if none
    bool work() {
        uint64_t c = claim.load(std::memory_order_acquire);
        bool any = false;
        while ((c & mask) < (c >> bits & mask)) {
            halide_task_t fn = f.load(std::memory_order_relaxed);
            void *u = uc.load(std::memory_order_relaxed);
            uint8_t *cl = closure.load(std::memory_order_relaxed);
            int mn = min.load(std::memory_order_relaxed);
            int n = std::min<int>(grain.load(std::memory_order_relaxed), (int)((c >> bits & mask) - (c & mask)));
            if (!claim.compare_exchange_weak(c, c + n, std::memory_order_acq_rel, std::memory_order_acquire)) continue;
            for (int i = 0; i < n; i++) {
                if (int e = fn(u, mn + (int)(c & mask) + i, cl)) err.store(e, std::memory_order_relaxed);
            }
            done.fetch_add(n, std::memory_order_release);
            c = claim.load(std::memory_order_acquire);
            any = true;
        }
        return any;
    }
};

thread_local Pool *pool = nullptr;  // thread 0's, while it runs a kernel

// Halide's parallel loops: on the MUL_MAT's threads (not nested; elsewhere,
// e.g. the vec_dots, serial)
int do_par_for(void *uc, halide_task_t f, int min, int size, uint8_t *closure) {
    Pool *p = pool;
    if (!p || size <= 1 || (uint64_t)size > Pool::mask) {
        for (int i = min; i < min + size; i++) {
            if (int e = f(uc, i, closure)) return e;
        }
        return 0;
    }
    pool = nullptr;
    uint64_t job = (p->claim.load(std::memory_order_relaxed) >> (2 * Pool::bits)) + 1;
    p->f.store(f, std::memory_order_relaxed);
    p->uc.store(uc, std::memory_order_relaxed);
    p->closure.store(closure, std::memory_order_relaxed);
    p->min.store(min, std::memory_order_relaxed);
    p->grain.store(std::max(1, size / (8 * halide_get_num_threads())), std::memory_order_relaxed);
    p->done.store(0, std::memory_order_relaxed);
    p->claim.store(job << (2 * Pool::bits) | (uint64_t)size << Pool::bits, std::memory_order_release);
    p->work();
    while (p->done.load(std::memory_order_acquire) < size) {
        cpu_relax();
    }
    pool = p;
    return p->err.load(std::memory_order_relaxed);
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
public:
    const MulMat &k;
    const gq::FormatSpec fs;

    explicit Traits(const MulMat &k)
        : k(k), fs(gq::parse_format(k.spec)) {
    }

    bool work_size(int, const ggml_tensor *, size_t &size) override {
        size = sizeof(Pool) + alignof(Pool);
        return true;
    }

    bool compute_forward(ggml_compute_params *params, ggml_tensor *op) override {
        if (op->op != GGML_OP_MUL_MAT) return false;
        const ggml_tensor *src0 = op->src[0], *src1 = op->src[1];
        const int ith = params->ith, nth = params->nth;
        uintptr_t at = ((uintptr_t)params->wdata + alignof(Pool) - 1) & ~(uintptr_t)(alignof(Pool) - 1);
        Pool *p = (Pool *)at;
        if (ith == 0) new (p) Pool();
        ggml_barrier(params->threadpool);
        if (ith != 0) {
            // (each job's tasks, until thread 0's kernel returns)
            while (!p->finished.load(std::memory_order_acquire)) {
                if (!p->work()) cpu_relax();
            }
            return true;
        }
        const int64_t K = src0->ne[0], N = src0->ne[1], M = src1->ne[1];
        // (a group of fs.rows rows: one row of records of fs.rows blocks)
        Buf2 w(src0->data, k.md()->arguments[0].type, K / ggml_blck_size(src0->type), N / fs.rows, src0->nb[1] / ggml_type_size(src0->type));
        Buf2 a(src1->data, halide_type_t(halide_type_float, 32), K, M, src1->nb[1] / sizeof(float));
        Buf2 o(op->data, halide_type_t(halide_type_float, 32), N, M, op->nb[1] / sizeof(float));
        if (halide_get_num_threads() != nth) halide_set_num_threads(nth);
        pool = p;
        int e = k.f(&w.b, &a.b, &o.b);
        pool = nullptr;
        p->finished.store(true, std::memory_order_release);
        if (e) {
            fprintf(stderr, "ggml-halide: %s error %d\n", k.name, e);
            abort();
        }
        return true;
    }
};

// The first kernel in GGML_HALIDE_MUL_MATS for a weight of type t with N rows
// (2D; a multiple of its format's rows) and GGML's act type
Traits *traits_for(ggml_type t, int64_t N) {
    static std::vector<std::unique_ptr<Traits>> ts = [] {
        std::vector<std::unique_ptr<Traits>> ts;
        for (const MulMat &k : mul_mats) {
            if (ggml_get_type_traits_cpu(k.weight)->vec_dot_type == k.act) ts.push_back(std::make_unique<Traits>(k));
        }
        return ts;
    }();
    for (auto &tr : ts) {
        if (tr->k.weight == t && N % tr->fs.rows == 0) return tr.get();
    }
    return nullptr;
}

ggml_backend_buffer_type_t halide_buffer_type();

enum ggml_status init_tensor(ggml_backend_buffer_t, ggml_tensor *t) {
    t->extra = ggml_n_dims(t) == 2 ? traits_for(t->type, t->ne[1]) : nullptr;
    return GGML_STATUS_SUCCESS;
}

// Weights to their kernel's format (GGML's repack's, by the harness's
// transcription of it), whole; others as the CPU buffer's
void set_tensor(ggml_backend_buffer_t, ggml_tensor *t, const void *data, size_t offset, size_t size) {
    const Traits *tr = (const Traits *)t->extra;
    if (tr && tr->fs.rows > 1) {
        GGML_ASSERT(offset == 0 && size == ggml_nbytes(t));
        if (!gq::relayout(tr->fs, t->type, (const uint8_t *)data, (uint8_t *)t->data, t->ne[1], t->ne[0])) {
            GGML_ABORT("ggml-halide: no %s layout for %s", tr->k.spec, t->name);
        }
        return;
    }
    memcpy((char *)t->data + offset, data, size);
}

ggml_backend_buffer_t alloc_buffer(ggml_backend_buffer_type_t buft, size_t size) {
    ggml_backend_buffer_t b = ggml_backend_buft_alloc_buffer(ggml_backend_cpu_buffer_type(), size);
    if (!b) return nullptr;
    b->buft = buft;
    b->iface.init_tensor = init_tensor;
    b->iface.set_tensor = set_tensor;
    // (as CPU_REPACK's: the relaid bytes aren't the tensor's)
    b->iface.get_tensor = nullptr;
    b->iface.cpy_tensor = nullptr;
    return b;
}

class ExtraBufferType : public ggml::cpu::extra_buffer_type {
    bool supports_op(ggml_backend_dev_t, const ggml_tensor *op) override {
        const ggml_tensor *src0 = op->src[0], *src1 = op->src[1];
        return op->op == GGML_OP_MUL_MAT && src0->buffer && src0->buffer->buft == halide_buffer_type() &&
               ggml_n_dims(src0) == 2 && traits_for(src0->type, src0->ne[1]) && src1->type == GGML_TYPE_F32 &&
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
        halide_set_custom_do_par_for(do_par_for);
        for (const MulMat &k : mul_mats) {
            fprintf(stderr, "ggml-halide: mul_mat %s\n", k.name);
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
