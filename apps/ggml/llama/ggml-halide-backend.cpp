// A ggml backend (like ggml-blas, see README.md) whose device, HALIDE (ACCEL),
// runs the MUL_MATs of host weights with the Halide mul_mat kernels that take
// f32 activations (<weight>_f32_to_<act>_mul_mat: the activations' quantization
// fused), each one call on Halide's own thread pool. ggml's scheduler gives it
// graph splits of consecutive MUL_MATs; the rest runs on the CPU backend.
// Enabled by GGML_HALIDE=backend.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HalideRuntime.h"
#include "ggml-backend-impl.h"
#include "ggml-cpu.h"
#include "ggml-impl.h"
#include "thread_pool.h"

// Generated: the kernels' headers, and GGML_HALIDE_BACKEND_MUL_MAT(name,
// weight, act) per kernel.
#include "ggml_halide_backend_kernels.h"

extern "C" ggml_backend_reg_t ggml_backend_halide_reg(void);

namespace {

using Fn = int (*)(halide_buffer_t *, halide_buffer_t *, halide_buffer_t *);
using Md = const halide_filter_metadata_t *(*)();

struct MulMat {
    const char *name;
    ggml_type weight;
    Fn f;
    Md md;
};

const MulMat mul_mats[] = {
#define GGML_HALIDE_BACKEND_MUL_MAT(name, w, a, spec) {#name, GGML_TYPE_##w, name, name##_metadata},
    GGML_HALIDE_BACKEND_MUL_MATS
#undef GGML_HALIDE_BACKEND_MUL_MAT
};

const MulMat *mul_mat_for(ggml_type t) {
    for (const MulMat &k : mul_mats) {
        if (k.weight == t) return &k;
    }
    return nullptr;
}

struct Buf {
    halide_buffer_t b{};
    halide_dimension_t d[2]{};
    Buf(const void *host, halide_type_t t, int64_t e0, int64_t e1) {
        b.host = (uint8_t *)host;
        b.type = t;
        b.dimensions = 2;
        b.dim = d;
        d[0] = {0, (int32_t)e0, 1, 0};
        d[1] = {0, (int32_t)e1, (int32_t)e0, 0};
    }
};

struct Context {
    int n_threads = GGML_DEFAULT_N_THREADS;
};

bool supported(const ggml_tensor *op) {
    const ggml_tensor *src0 = op->src[0], *src1 = op->src[1];
    return op->op == GGML_OP_MUL_MAT && mul_mat_for(src0->type) && ggml_n_dims(src0) == 2 && ggml_is_contiguous(src0) &&
           src1->type == GGML_TYPE_F32 && src1->ne[2] == 1 && src1->ne[3] == 1 && ggml_is_contiguous(src1) &&
           ggml_is_contiguous(op);
}

void mul_mat(const ggml_tensor *op) {
    const ggml_tensor *src0 = op->src[0], *src1 = op->src[1];
    const MulMat &k = *mul_mat_for(src0->type);
    const int64_t K = src0->ne[0], N = src0->ne[1], M = src1->ne[1];
    Buf w(src0->data, k.md()->arguments[0].type, K / ggml_blck_size(src0->type), N);
    Buf a(src1->data, halide_type_t(halide_type_float, 32), K, M);
    Buf o(op->data, halide_type_t(halide_type_float, 32), N, M);
    if (int e = k.f(&w.b, &a.b, &o.b)) {
        fprintf(stderr, "ggml-halide: %s error %d\n", k.name, e);
        abort();
    }
}

const char *backend_name(ggml_backend_t) {
    return "HALIDE";
}

void backend_free(ggml_backend_t backend) {
    delete (Context *)backend->context;
    delete backend;
}

enum ggml_status graph_compute(ggml_backend_t backend, ggml_cgraph *cgraph) {
    Context *ctx = (Context *)backend->context;
    halide_set_num_threads(ctx->n_threads);
    // The pool's workers poll for the split's back-to-back loops; between
    // splits (the CPU backend's ops), they sleep.
    ggml_halide::ThreadPoolKeepAwake awake;
    for (int i = 0; i < cgraph->n_nodes; i++) {
        ggml_tensor *node = cgraph->nodes[i];
        if ((node->flags & GGML_TENSOR_FLAG_COMPUTE) == 0) continue;
        switch (node->op) {
        case GGML_OP_MUL_MAT:
            mul_mat(node);
            break;
        case GGML_OP_NONE:
        case GGML_OP_RESHAPE:
        case GGML_OP_VIEW:
        case GGML_OP_PERMUTE:
        case GGML_OP_TRANSPOSE:
            break;
        default:
            GGML_ABORT("%s: unsupported op %s\n", __func__, ggml_op_desc(node));
        }
    }
    return GGML_STATUS_SUCCESS;
}

ggml_guid_t guid() {
    static ggml_guid g = {0x48, 0x61, 0x6c, 0x69, 0x64, 0x65, 0x67, 0x67, 0x6d, 0x6c, 0x2d, 0x71, 0x75, 0x61, 0x6e, 0x74};
    return &g;
}

void set_n_threads(ggml_backend_t backend, int n_threads) {
    ((Context *)backend->context)->n_threads = n_threads;
}

ggml_backend_t backend_init() {
    static ggml_backend_i iface = {
        /* .get_name            = */ backend_name,
        /* .free                = */ backend_free,
        /* .set_tensor_async    = */ nullptr,
        /* .get_tensor_async    = */ nullptr,
        /* .set_tensor_2d_async = */ nullptr,
        /* .get_tensor_2d_async = */ nullptr,
        /* .cpy_tensor_async    = */ nullptr,
        /* .synchronize         = */ nullptr,
        /* .graph_plan_create   = */ nullptr,
        /* .graph_plan_free     = */ nullptr,
        /* .graph_plan_update   = */ nullptr,
        /* .graph_plan_compute  = */ nullptr,
        /* .graph_compute       = */ graph_compute,
        /* .event_record        = */ nullptr,
        /* .event_wait          = */ nullptr,
        /* .graph_optimize      = */ nullptr,
    };
    return new ggml_backend{guid(), iface, ggml_backend_reg_dev_get(ggml_backend_halide_reg(), 0), new Context};
}

// device

const char *dev_name(ggml_backend_dev_t) {
    return "HALIDE";
}

void dev_memory(ggml_backend_dev_t, size_t *free, size_t *total) {
    *free = *total = 0;
}

enum ggml_backend_dev_type dev_type(ggml_backend_dev_t) {
    return GGML_BACKEND_DEVICE_TYPE_ACCEL;
}

void dev_props(ggml_backend_dev_t dev, ggml_backend_dev_props *props) {
    props->name = props->description = dev_name(dev);
    props->type = dev_type(dev);
    dev_memory(dev, &props->memory_free, &props->memory_total);
    props->caps = {/* .async = */ false, /* .host_buffer = */ false, /* .buffer_from_host_ptr = */ true,
                   /* .events = */ false, /* .mmap_support = */ true};
}

const ggml_backend_device_i device_i = {
    /* .get_name             = */ dev_name,
    /* .get_description      = */ dev_name,
    /* .get_memory           = */ dev_memory,
    /* .get_type             = */ dev_type,
    /* .get_props            = */ dev_props,
    /* .init_backend         = */ [](ggml_backend_dev_t, const char *) { return backend_init(); },
    /* .get_buffer_type      = */ [](ggml_backend_dev_t) { return ggml_backend_cpu_buffer_type(); },
    /* .get_host_buffer_type = */ nullptr,
    /* .buffer_from_host_ptr = */
    [](ggml_backend_dev_t, void *ptr, size_t size, size_t) { return ggml_backend_cpu_buffer_from_ptr(ptr, size); },
    /* .supports_op          = */
    [](ggml_backend_dev_t, const ggml_tensor *op) {
        switch (op->op) {
        case GGML_OP_NONE:
        case GGML_OP_RESHAPE:
        case GGML_OP_VIEW:
        case GGML_OP_PERMUTE:
        case GGML_OP_TRANSPOSE:
            return true;
        default:
            return supported(op);
        }
    },
    /* .supports_buft        = */ [](ggml_backend_dev_t, ggml_backend_buffer_type_t buft) { return ggml_backend_buft_is_host(buft); },
    /* .offload_op           = */ nullptr,
    /* .event_new            = */ nullptr,
    /* .event_free           = */ nullptr,
    /* .event_synchronize    = */ nullptr,
};

// registry: no devices unless GGML_HALIDE has backend

size_t reg_device_count(ggml_backend_reg_t) {
    static const bool on = [] {
        const char *e = getenv("GGML_HALIDE");
        if (!e || !strstr(e, "backend")) return false;
        for (const MulMat &k : mul_mats) {
            fprintf(stderr, "ggml-halide: backend %s\n", k.name);
        }
        return true;
    }();
    return on ? 1 : 0;
}

ggml_backend_dev_t reg_device(ggml_backend_reg_t reg, size_t index) {
    GGML_ASSERT(index == 0);
    static ggml_backend_device dev = {device_i, reg, nullptr};
    return &dev;
}

void *reg_proc_address(ggml_backend_reg_t, const char *name) {
    if (!strcmp(name, "ggml_backend_set_n_threads")) return (void *)set_n_threads;
    return nullptr;
}

}  // namespace

extern "C" ggml_backend_reg_t ggml_backend_halide_reg(void) {
    static ggml_backend_reg_i iface = {
        /* .get_name         = */ [](ggml_backend_reg_t) { return "HALIDE"; },
        /* .get_device_count = */ reg_device_count,
        /* .get_device       = */ reg_device,
        /* .get_proc_address = */ reg_proc_address,
    };
    static ggml_backend_reg reg = {GGML_BACKEND_API_VERSION, iface, nullptr};
    return &reg;
}
