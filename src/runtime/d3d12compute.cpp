// NOTE(marcos): this file should not be compiled stand-alone; instead, it must
// be included from a platform wrapper, as listed below:
// - windows_d3d12compute_x86.cpp

#ifndef HALIDE_D3D12_PLATFORM
#error Direct3D 12 Platform must be specified when compiling 'd3d12compute.cpp'.
#endif

#ifndef BITS_64
#error Direct3D 12 runtime module requires a 64-bit platform.
#endif

// Debugging utilities for back-end developers:
#define HALIDE_D3D12_TRACE (0)
#define HALIDE_D3D12_DEBUG_LAYER (0)
#define HALIDE_D3D12_DEBUG_SHADERS (0)
#define HALIDE_D3D12_PROFILING (0)
#define HALIDE_D3D12_TRACE_LEVEL (9)
#define HALIDE_D3D12_TRACE_TIME (0)
#define HALIDE_D3D12_TRACE_TIME_THRESHOLD (100) /* in microseconds */
#define HALIDE_D3D12_PIX (0)
#define HALIDE_D3D12_RENDERDOC (0)

// Halide debug target (Target::Debug, "-debug"):
// force-enable call-trace, d3d12 debug layer and shader debugging information
#ifdef DEBUG_RUNTIME

#undef HALIDE_D3D12_TRACE
#define HALIDE_D3D12_TRACE (1)

#undef HALIDE_D3D12_TRACE_TIME
#define HALIDE_D3D12_TRACE_TIME (1)

#undef HALIDE_D3D12_DEBUG_LAYER
#define HALIDE_D3D12_DEBUG_LAYER (1)

#undef HALIDE_D3D12_DEBUG_SHADERS
#define HALIDE_D3D12_DEBUG_SHADERS (1)

#endif

// NOTE(marcos): "HalideRuntimeD3D12Compute.h" includes "HalideRuntime.h",
// which in turn will include "runtime_internal.h" where the stdint types,
// malloc, free and memset are exposed.
#include "HalideRuntimeD3D12Compute.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "gpu_context_common.h"
#include "printer.h"
#include "scoped_spin_lock.h"

#if !defined(INITGUID)
#define INITGUID
#endif
#if !defined(COBJMACROS)
#define COBJMACROS
#endif
#include "mini_d3d12.h"

// For all intents and purposes, we always want to use COMPUTE command lists
// (and queues) ...
#define HALIDE_D3D12_COMMAND_LIST_TYPE D3D12_COMMAND_LIST_TYPE_COMPUTE
// ...  unless we need to debug with RenderDoc/PIX, or use the built-in ad-hoc
// ad-hoc profiler, in which case we need regular (DIRECT) graphics lists...
// (This is due to limitations of the D3D12 run-time, not Halide's fault.)
#if HALIDE_D3D12_PROFILING
// NOTE(marcos): timer queries are reporting exceedingly small elapsed deltas
// when placed in compute queues... this might be related to GPU Power Boost,
// or the default SetStablePowerState(FALSE), but it's inconclusive since the
// queries still misbehave regardless... for now, just use the graphics queue
// when profiling
#undef HALIDE_D3D12_COMMAND_LIST_TYPE
#define HALIDE_D3D12_COMMAND_LIST_TYPE D3D12_COMMAND_LIST_TYPE_DIRECT
#endif

// A Printer that automatically reserves stack space for the printer buffer:
// (the managed printers in 'printer.h' rely on malloc)
template<uint64_t length = 1024, int type = BasicPrinter>
class StackPrinter : public Printer<type, length> {
public:
    StackPrinter(void *ctx = nullptr)
        : Printer<type, length>(ctx, buffer) {
    }
    StackPrinter &operator()(void *ctx = nullptr) {
        this->user_context = ctx;
        return *this;
    }
    uint64_t capacity() const {
        return length;
    }

private:
    char buffer[length];
};

static void d3d12_debug_dump(error &err);

#define d3d12_panic(...)                               \
    do {                                               \
        error err(nullptr);                            \
        err << __VA_ARGS__ << "\n";                    \
        err << "vvvvv D3D12 Begin Debug Dump vvvvv\n"; \
        d3d12_debug_dump(err);                         \
        err << "^^^^^ D3D12  End  Debug Dump ^^^^^\n"; \
        err << "D3D12 HALT !!!\n";                     \
    } while (0)

// v trace and logging utilities for debugging v
// ! definitely not super thread-safe stuff... !
// in case there's no 'user_context' available in the scope of a function:
static void *const user_context = nullptr;
//

// Trace and logging utilities for debugging.
#if HALIDE_D3D12_TRACE
static volatile ScopedSpinLock::AtomicFlag trace_lock = 0;
static char trace_buf[4096] = {};
static int trace_indent = 0;

struct trace : public Printer<BasicPrinter, sizeof(trace_buf)> {
    ScopedSpinLock lock;
    explicit trace(void *user_context = nullptr)
        : Printer<BasicPrinter, sizeof(trace_buf)>(user_context, trace_buf),
          lock(&trace_lock) {
        for (int i = 0; i < trace_indent; i++) {
            *this << "    ";
        }
    }
};

#define TRACEPRINT(msg) trace() << msg;
#define TRACELEVEL(level, msg) \
    if (level <= HALIDE_D3D12_TRACE_LEVEL) TRACEPRINT(msg);
#define TRACEFATAL(msg)                             \
    TRACELEVEL(-3, "FATAL ERROR: " << msg << "\n"); \
    d3d12_panic(msg);
#define TRACEERROR(msg) TRACELEVEL(-2, "ERROR: " << msg);
#define TRACEWARN(msg) TRACELEVEL(-1, "WARNING: " << msg);
#define TRACEINFO(msg) TRACELEVEL(0, msg);

#ifdef HALIDE_D3D12_TRACE_TIME
#define TRACETIME_CHECKPOINT() halide_current_time_ns(user_context)
// NOTE(marcos): we used to report time in milliseconds as float/double with
// "(t1 - t0) / 1.0e6", but that tends to print some really annoying exponents
// like '4.2500e-2' which can be misleading when observing/comparing values...
// prefer printing in microseconds as integers to avoid these shenannigans:
#define TRACETIME_REPORT(t0, t1, ...) TRACEPRINT(__VA_ARGS__ << (t1 - t0) / 1000 << " us\n")
#else
#define TRACETIME_CHECKPOINT() 0
#define TRACETIME_REPORT(t0, t1, ...)
#endif

struct TraceScope {
    explicit TraceScope(const char *func) {
        TRACEPRINT("[@] " << func << "\n");
#ifdef HALIDE_D3D12_TRACE_TIME
        _func = func;
        t0 = TRACETIME_CHECKPOINT();
#endif
        ScopedSpinLock lock(&trace_lock);
        trace_indent++;
    }

    ~TraceScope() {
#ifdef HALIDE_D3D12_TRACE_TIME
        uint64_t t1 = TRACETIME_CHECKPOINT();
        if ((t1 - t0) >= (HALIDE_D3D12_TRACE_TIME_THRESHOLD * 1000)) {  // *1000 : microseconds to nanoseconds
            TRACETIME_REPORT(t0, t1, "Time [" << _func << "]: ");
        }
#endif
        ScopedSpinLock lock(&trace_lock);
        trace_indent--;
    }

#ifdef HALIDE_D3D12_TRACE_TIME
    const char *_func;
    uint64_t t0;
#endif
};

#define TRACE_SCOPE(name) TraceScope trace_scope__(name)
#define TRACELOG TRACE_SCOPE(__FUNCTION__)

#else
typedef SinkPrinter trace;
#define TRACE_SCOPE(name)
#define TRACELOG
#define TRACEPRINT(msg)
#define TRACELEVEL(level, msg)
#define TRACEFATAL(msg) d3d12_panic(msg);
#define TRACEERROR(msg) debug(user_context) << "ERROR: " << (msg);
#define TRACEWARN(msg) debug(user_context) << "WARNING: " << (msg);
#define TRACEINFO(msg)
#endif
//
// ^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^

void *d3d12_load_library(const char *name) {
    TRACELOG;
    void *lib = halide_load_library(name);
    if (lib) {
        TRACEPRINT("Loaded runtime library '" << name << "' at location " << lib << "\n");
    } else {
        TRACEFATAL("Unable to load runtime library: " << name);
    }
    return lib;
}

void *d3d12_get_library_symbol(void *lib, const char *name) {
    TRACELOG;
    void *symbol = halide_get_library_symbol(lib, name);
    if (symbol) {
        TRACEPRINT("Symbol '" << name << "' found @ " << symbol << "\n");
    } else {
        TRACEFATAL("Symbol not found: " << name);
    }
    return symbol;
}

#ifndef MAYBE_UNUSED
#define MAYBE_UNUSED(x) ((void)(x))
#endif  // MAYBE_UNUSED

#if HALIDE_D3D12_RENDERDOC
#if HALIDE_D3D12_DEBUG_LAYER
#pragma message "RenderDoc might not work well alongside Direct3D debug layers..."
#endif
#define WIN32
#define RenderDocAssert(expr) halide_assert(user_context, expr)
#define LoadRenderDocLibrary(dll) d3d12_load_library(dll)
#define GetRenderDocProcAddr(dll, proc) d3d12_get_library_symbol(dll, proc)
#define RENDERDOC_NO_STDINT
#define RENDERDOC_AUTOINIT (0)
#include "renderdoc/RenderDocGlue.h"
// RenderDoc can only intercept commands in the graphics queue:
#undef HALIDE_D3D12_COMMAND_LIST_TYPE
#define HALIDE_D3D12_COMMAND_LIST_TYPE D3D12_COMMAND_LIST_TYPE_DIRECT
#endif

static void *d3d12_malloc(size_t num_bytes) {
    TRACELOG;
    void *p = malloc(num_bytes);
    TRACEPRINT("allocated " << (uintptr_t)num_bytes << " bytes @ " << p << "\n");
    return p;
}

static void d3d12_free(void *p) {
    TRACELOG;
    TRACEPRINT("freeing bytes @ " << p << "\n");
    free(p);
}

template<typename T>
static T *malloct() {
    TRACELOG;
    T *p = nullptr;
    TRACEPRINT("allocating " << d3d12typename(p) << "\n");
    p = (T *)d3d12_malloc(sizeof(T));
#ifdef DEBUG_RUNTIME
    *p = zero_struct<T>();
#endif
    return p;
}

template<typename T>
static T zero_struct() {
    T zero = {};
    return zero;
}

#define hashmap_malloc(user_context, size) d3d12_malloc(size)
#define hashmap_free(user_context, memory) d3d12_free(memory)
#include "hashmap.h"

template<typename ID3D12T>
static const char *d3d12typename(ID3D12T *) {
    return "UNKNOWN";
}

#ifdef HALIDE_D3D12_TRACE
#define D3D12TYPENAME(T)                    \
    static const char *d3d12typename(T *) { \
        return #T;                          \
    }
#else
#define D3D12TYPENAME(T)
#endif
// d3d12.h
D3D12TYPENAME(ID3D12Device)
D3D12TYPENAME(ID3D12Debug)
D3D12TYPENAME(ID3D12CommandQueue)
D3D12TYPENAME(ID3D12CommandAllocator)
D3D12TYPENAME(ID3D12CommandList)
D3D12TYPENAME(ID3D12GraphicsCommandList)
D3D12TYPENAME(ID3D12Resource)
D3D12TYPENAME(ID3D12PipelineState)
D3D12TYPENAME(ID3D12RootSignature)
D3D12TYPENAME(ID3D12DescriptorHeap)
D3D12TYPENAME(ID3D12Fence)
D3D12TYPENAME(ID3D12QueryHeap)
// d3dcommon.h
D3D12TYPENAME(ID3DBlob)
// dxgi.h
D3D12TYPENAME(IDXGIFactory1)
D3D12TYPENAME(IDXGIAdapter1)
D3D12TYPENAME(IDXGIOutput)

template<typename ID3D12T>
static bool D3DErrorCheck(HRESULT result, ID3D12T *object, void *user_context, const char *message) {
    TRACELOG;
    // HRESULT ERROR CODES:
    // D3D12: https://msdn.microsoft.com/en-us/library/windows/desktop/bb509553(v=vs.85).aspx
    // Win32: https://msdn.microsoft.com/en-us/library/windows/desktop/aa378137(v=vs.85).aspx
    if (FAILED(result) || !object) {
        TRACEFATAL(
            message << " (HRESULT=" << (void *)(int64_t)result
                    << ", object*=" << object << ")");
        return true;
    }
    TRACEPRINT("SUCCESS: " << d3d12typename(object) << " object created: " << object << "\n");
    return false;
}

static DXGI_FORMAT FindD3D12FormatForHalideType(void *user_context, halide_type_t type) {
    // DXGI Formats:
    // https://msdn.microsoft.com/en-us/library/windows/desktop/bb173059(v=vs.85).aspx

    // indexing scheme: [code][lane][bits]
    const DXGI_FORMAT FORMATS[3][4][4] =
        {
            // halide_type_int
            {
                // 1 lane
                {
                    DXGI_FORMAT_R8_SINT,   //  8 bits
                    DXGI_FORMAT_R16_SINT,  // 16 bits
                    DXGI_FORMAT_R32_SINT,  // 32 bits
                    DXGI_FORMAT_UNKNOWN,   // 64 bits
                },
                // 2 lanes
                {
                    DXGI_FORMAT_R8G8_SINT,
                    DXGI_FORMAT_R16G16_SINT,
                    DXGI_FORMAT_R32G32_SINT,
                    DXGI_FORMAT_UNKNOWN,
                },
                // 3 lanes
                {
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_R32G32B32_SINT,
                    DXGI_FORMAT_UNKNOWN,
                },
                // 4 lanes
                {
                    DXGI_FORMAT_R8G8B8A8_SINT,
                    DXGI_FORMAT_R16G16B16A16_SINT,
                    DXGI_FORMAT_R32G32B32A32_SINT,
                    DXGI_FORMAT_UNKNOWN,
                }},
            // halide_type_uint
            {
                // 1 lane
                {
                    DXGI_FORMAT_R8_UINT,
                    DXGI_FORMAT_R16_UINT,
                    DXGI_FORMAT_R32_UINT,
                    DXGI_FORMAT_UNKNOWN,
                },
                // 2 lanes
                {
                    DXGI_FORMAT_R8G8_UINT,
                    DXGI_FORMAT_R16G16_UINT,
                    DXGI_FORMAT_R32G32_UINT,
                    DXGI_FORMAT_UNKNOWN,
                },
                // 3 lanes
                {
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_R32G32B32_UINT,
                    DXGI_FORMAT_UNKNOWN,
                },
                // 4 lanes
                {
                    DXGI_FORMAT_R8G8B8A8_UINT,
                    DXGI_FORMAT_R16G16B16A16_UINT,
                    DXGI_FORMAT_R32G32B32A32_UINT,
                    DXGI_FORMAT_UNKNOWN,
                }},
            // halide_type_float
            {
                // 1 lane
                {
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_R16_FLOAT,
                    DXGI_FORMAT_R32_FLOAT,
                    DXGI_FORMAT_UNKNOWN,
                },
                // 2 lanes
                {
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_R16G16_FLOAT,
                    DXGI_FORMAT_R32G32_FLOAT,
                    DXGI_FORMAT_UNKNOWN,
                },
                // 3 lanes
                {
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_R32G32B32_FLOAT,
                    DXGI_FORMAT_UNKNOWN,
                },
                // 4 lanes
                {
                    DXGI_FORMAT_UNKNOWN,
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    DXGI_FORMAT_R32G32B32A32_FLOAT,
                    DXGI_FORMAT_UNKNOWN,
                }},
        };

    halide_assert(user_context, (type.code >= 0) && (type.code <= 2));
    halide_assert(user_context, (type.lanes > 0) && (type.lanes <= 4));

    int i = 0;
    switch (type.bytes()) {
    case 1:
        i = 0;
        break;
    case 2:
        i = 1;
        break;
    case 4:
        i = 2;
        break;
    case 8:
        i = 3;
        break;
    default:
        halide_assert(user_context, false);
        break;
    }

    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    format = FORMATS[(int)type.code][type.lanes - 1][i];
    return format;
}

// The default implementation of halide_d3d12_get_symbol attempts to load
// the D3D12 runtime shared library/DLL, and then get the symbol from it.
static void *lib_d3d12 = nullptr;
static void *lib_D3DCompiler_47 = nullptr;
static void *lib_dxgi = nullptr;

struct LibrarySymbol {
    template<typename T>
    operator T() {
        return (T)symbol;
    }
    void *symbol;

    static LibrarySymbol get(void *user_context, void *lib, const char *name) {
        void *s = d3d12_get_library_symbol(lib, name);
        LibrarySymbol symbol = {s};
        return symbol;
    }
};

static PFN_D3D12_CREATE_DEVICE D3D12CreateDevice = nullptr;
static PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterface = nullptr;
static PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignature = nullptr;
static PFN_D3DCOMPILE D3DCompile = nullptr;
static PFN_CREATEDXGIFACORY1 CreateDXGIFactory1 = nullptr;

#if defined(__cplusplus) && !defined(_MSC_VER)
#if defined(__MINGW32__)
#undef __uuidof
#endif

#define UUIDOF(T)                \
    REFIID __uuidof(const T &) { \
        return IID_##T;          \
    }

UUIDOF(ID3D12Device)
UUIDOF(ID3D12Debug)
UUIDOF(ID3D12CommandQueue)
UUIDOF(ID3D12CommandAllocator)
UUIDOF(ID3D12CommandList)
UUIDOF(ID3D12GraphicsCommandList)
UUIDOF(ID3D12Resource)
UUIDOF(ID3D12PipelineState)
UUIDOF(ID3D12RootSignature)
UUIDOF(ID3D12DescriptorHeap)
UUIDOF(ID3D12Fence)
UUIDOF(ID3D12QueryHeap)

UUIDOF(IDXGIFactory1)
UUIDOF(IDXGIAdapter1)
UUIDOF(IDXGIOutput)
#endif

// !!! 'this' is THE actual d3d12 object (reinterpret is safe)
template<typename ID3D12Type>
struct halide_d3d12_wrapper {
    operator ID3D12Type *() {
        return reinterpret_cast<ID3D12Type *>(this);
    }
    ID3D12Type *operator->() {
        return reinterpret_cast<ID3D12Type *>(this);
    }
};

// !!! the d3d12 is managed internally; inherit from this class aggregate data
// to the managed object
template<typename ID3D12Type>
struct halide_d3d12_deep_wrapper {
    ID3D12Type *p;
    operator ID3D12Type *() {
        return p;
    }
    ID3D12Type *operator->() {
        return p;
    }
};

struct halide_d3d12compute_device : public halide_d3d12_wrapper<ID3D12Device> {};
struct halide_d3d12compute_command_queue : public halide_d3d12_wrapper<ID3D12CommandQueue> {};

namespace Halide {
namespace Runtime {
namespace Internal {
namespace D3D12Compute {

typedef halide_d3d12compute_device d3d12_device;
typedef halide_d3d12compute_command_queue d3d12_command_queue;

struct d3d12_buffer {
    ID3D12Resource *resource;
    UINT capacityInBytes;
    UINT sizeInBytes;
    UINT offset;    // FirstElement
    UINT elements;  // NumElements
    // NOTE(marcos): unfortunately, we need both 'offset' and 'offsetInBytes';
    // multi-dimensional buffers and crops (their strides/alignment/padding)
    // end up complicating the relationship between elements (offset) and bytes
    // so it is more practical (and less error prone) to memoize 'offsetInBytes'
    // once than recomputing it all the time it is needed
    UINT offsetInBytes;
    DXGI_FORMAT format;
    D3D12_RESOURCE_STATES state;

    enum {
        Unknown = 0,
        Constant,
        ReadWrite,
        ReadOnly,
        WriteOnly,
        Upload,
        ReadBack
    } type;

    // NOTE(marcos): it's UNSAFE to cache a pointer to a 'halide_buffer_t' here
    // since it is a POD type that can be re-assigned/re-scoped outside of this
    // runtime module.

    halide_type_t halide_type;

    struct transfer_t {
        d3d12_buffer *staging;
        size_t offset;
        size_t size;
    } * xfer;

    bool mallocd;
    void *host_mirror;

    // if the buffer is an upload/readback staging heap:
    void *mapped;
    volatile uint64_t ref_count /*__attribute__((aligned(8)))*/;

    uint64_t signal;

    operator bool() const {
        return resource != nullptr;
    }
};

struct d3d12_command_allocator : public halide_d3d12_wrapper<ID3D12CommandAllocator> {};

struct d3d12_graphics_command_list : public halide_d3d12_deep_wrapper<ID3D12GraphicsCommandList> {
    uint64_t signal;
};

// NOTE(marcos): at the moment, D3D12 only exposes one type of command list
// (ID3D12GraphicsCommandList) which can also be used for either "compute"
// or "copy" command streams
typedef d3d12_graphics_command_list d3d12_command_list;
typedef d3d12_graphics_command_list d3d12_compute_command_list;
typedef d3d12_graphics_command_list d3d12_copy_command_list;

struct d3d12_pipeline_state : public halide_d3d12_wrapper<ID3D12PipelineState> {};
typedef d3d12_pipeline_state d3d12_compute_pipeline_state;

struct d3d12_library {
    THashMap<char *, struct d3d12_function *> cache;
    int source_length;
    char source[1];
};

struct d3d12_function {
    ID3DBlob *shaderBlob;
    ID3D12RootSignature *rootSignature;
    d3d12_compute_pipeline_state *pipeline_state;
};

enum ResourceBindingSlots {
    UAV = 0,
    CBV,
    SRV,
    NumSlots
};

// These are "tier-1" d3d12 device limits (D3D12_RESOURCE_BINDING_TIER_1):
static const uint32_t ResourceBindingLimits[NumSlots] = {
    16,  // UAV
    14,  // CBV
    25,  // SRV (the actual tier-1 limit is 128, but will allow only 25 for now)
    // TODO(marcos): we may consider increasing it to the limit once we have a
    // pool of d3d12_binder objects that are recycled whenever kernels are run
    // (at the moment, a new binder is created/destroyed with every kernel run)
};

struct d3d12_binder {
    ID3D12DescriptorHeap *descriptorHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE baseCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE baseGPU;
    D3D12_CPU_DESCRIPTOR_HANDLE CPU[NumSlots];
    D3D12_GPU_DESCRIPTOR_HANDLE GPU[NumSlots];
    UINT descriptorSize;
};

struct d3d12_profiler {
    d3d12_buffer queryResultsBuffer;
    UINT64 tick_frequency;  // in Hz, may vary per command queue
    ID3D12QueryHeap *queryHeap;
    UINT next_free_query;
    UINT max_queries;
};

D3D12TYPENAME(d3d12_buffer)
D3D12TYPENAME(d3d12_profiler)
D3D12TYPENAME(d3d12_command_list)
D3D12TYPENAME(d3d12_binder)
D3D12TYPENAME(d3d12_function)

static size_t number_of_elements(void *user_context, const halide_buffer_t *buffer) {
    // halide_buffer_t::number_of_elements() does not necessarily map to D3D12
    // Buffer View 'NumElements' since the former does not account for "hidden"
    // elements in the stride regions.

    size_t size_in_bytes = buffer->size_in_bytes();
    halide_assert(user_context, (size_in_bytes > 0));

    size_t element_size = 1;
    element_size *= buffer->type.bytes();
    element_size *= buffer->type.lanes;
    halide_assert(user_context, (element_size > 0));

    size_t elements = size_in_bytes / element_size;
    halide_assert(user_context, (size_in_bytes % element_size) == 0);

    return elements;
}

#if HALIDE_D3D12_DEBUG_LAYER
WEAK ID3D12Debug *d3d12Debug = nullptr;
#endif
WEAK IDXGIAdapter1 *dxgiAdapter = nullptr;
WEAK d3d12_device *device = nullptr;
WEAK d3d12_command_queue *queue = nullptr;
WEAK ID3D12Fence *queue_fence = nullptr;
WEAK volatile uint64_t queue_last_signal /*__attribute__((aligned(8)))*/ = 0;
WEAK ID3D12RootSignature *rootSignature = nullptr;
WEAK d3d12_buffer upload = {};    // staging buffer to transfer data to the device
WEAK d3d12_buffer readback = {};  // staging buffer to retrieve data from the device

WEAK HANDLE hFenceEvent = nullptr;

WEAK d3d12_command_allocator *cmd_allocator_main = nullptr;

// NOTE(marcos): the term "frame" here is borrowed from graphics to delineate the
// lifetime of a kernel dispatch; more specifically, a number of "expensive" API
// objects is necessary for each dispatch, and they must remain alive and immutable
// until the kernel has finished executing on the device, at which point these API
// objects can be reclaimed and reused for subsequent kernel dispatches.
// As there's not enough information about full Pipelines and Stages in the runtime
// back-end to possibly group these API objects together in a more coarse "frame",
// each kernel dispatch must be seen as a "frame" on its own for lifetime tracking.
struct d3d12_frame {
    d3d12_compute_command_list *cmd_list;
    d3d12_binder *desc_binder;
    d3d12_buffer args_buffer;
    uint64_t fence_signal;
};

static const int MaxFrames = 8;
WEAK d3d12_frame frame_pool[MaxFrames] = {};
static uint64_t frame_selector = 0;

static void wait_until_completed(d3d12_compute_command_list *cmdList);
static d3d12_command_list *new_compute_command_list(d3d12_device *device, d3d12_command_allocator *allocator);
static d3d12_binder *new_descriptor_binder(d3d12_device *device);
static void commit_command_list(d3d12_compute_command_list *cmdList);

static d3d12_frame *acquire_frame(d3d12_device *device) {
    TRACELOG;

    // check for completed frames
    UINT64 fence_signal = queue_fence->GetCompletedValue();
    uint64_t i = frame_selector % MaxFrames;
    d3d12_frame &frame = frame_pool[i];
    if (fence_signal < frame.fence_signal) {
        // no frame available: must stall and wait
        TRACEPRINT("WARNING: [PERFORMANCE] too many in-flight/pending frames: stalling...\n");
        wait_until_completed(frame.cmd_list);
    }

    // initialize the frame object in the pool the first time through
    if (frame.cmd_list == nullptr) {
        frame.cmd_list = new_compute_command_list(device, cmd_allocator_main);
        if (frame.cmd_list == nullptr) {
            return nullptr;
        }
        frame.desc_binder = new_descriptor_binder(device);
        if (frame.desc_binder == nullptr) {
            return nullptr;
        }
    } else {
        (*frame.cmd_list)->Reset((*cmd_allocator_main), nullptr);

        d3d12_binder *binder = frame.desc_binder;
        UINT descriptorSize = binder->descriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE baseCPU = binder->baseCPU;
        binder->CPU[UAV].ptr = (baseCPU.ptr += descriptorSize * 0);
        binder->CPU[CBV].ptr = (baseCPU.ptr += descriptorSize * ResourceBindingLimits[UAV]);
        binder->CPU[SRV].ptr = (baseCPU.ptr += descriptorSize * ResourceBindingLimits[CBV]);
        D3D12_GPU_DESCRIPTOR_HANDLE baseGPU = binder->baseGPU;
        binder->GPU[UAV].ptr = (baseGPU.ptr += descriptorSize * 0);
        binder->GPU[CBV].ptr = (baseGPU.ptr += descriptorSize * ResourceBindingLimits[UAV]);
        binder->GPU[SRV].ptr = (baseGPU.ptr += descriptorSize * ResourceBindingLimits[CBV]);
    }

    ++frame_selector;

    return &frame;
}

static void enqueue_frame(d3d12_frame *frame) {
    TRACELOG;
    commit_command_list(frame->cmd_list);
    frame->fence_signal = frame->cmd_list->signal;
}

template<typename d3d12_T>
static void release_d3d12_object(d3d12_T *obj) {
    TRACELOG;
    TRACEFATAL("!!! ATTEMPTING TO RELEASE AN UNKNOWN OBJECT @ " << obj << " !!!");
}

template<typename d3d12_T>
static void release_object(d3d12_T *obj) {
    TRACELOG;
    if (!obj) {
        TRACEPRINT("null object -- nothing to be released.\n");
        return;
    }
    release_d3d12_object(obj);
}

template<typename ID3D12T>
static void Release_ID3D12Object(ID3D12T *obj) {
    TRACELOG;
    TRACEPRINT(d3d12typename(obj) << " @ " << obj << "\n");
    if (obj) {
        obj->Release();
    }
}

template<>
void release_d3d12_object<d3d12_device>(d3d12_device *device) {
    TRACELOG;
    ID3D12Device *p = (*device);
    Release_ID3D12Object(p);
    Release_ID3D12Object(dxgiAdapter);
    dxgiAdapter = nullptr;
#if HALIDE_D3D12_DEBUG_LAYER
    Release_ID3D12Object(d3d12Debug);
    d3d12Debug = nullptr;
#endif
}

template<>
void release_d3d12_object<d3d12_command_queue>(d3d12_command_queue *queue) {
    TRACELOG;
    ID3D12CommandQueue *p = (*queue);
    Release_ID3D12Object(p);
    Release_ID3D12Object(queue_fence);
}

template<>
void release_d3d12_object<d3d12_command_allocator>(d3d12_command_allocator *cmdAllocator) {
    TRACELOG;
    ID3D12CommandAllocator *p = (*cmdAllocator);
    Release_ID3D12Object(p);
}

template<>
void release_d3d12_object<d3d12_command_list>(d3d12_command_list *cmdList) {
    TRACELOG;
    Release_ID3D12Object(cmdList->p);
    d3d12_free(cmdList);
}

template<>
void release_d3d12_object<d3d12_binder>(d3d12_binder *binder) {
    TRACELOG;
    Release_ID3D12Object(binder->descriptorHeap);
    d3d12_free(binder);
}

template<>
void release_d3d12_object<d3d12_buffer>(d3d12_buffer *buffer) {
    TRACELOG;
    Release_ID3D12Object(buffer->resource);
    if (buffer->host_mirror != nullptr) {
        d3d12_free(buffer->host_mirror);
    }
    if (buffer->mallocd) {
        TRACEPRINT("freeing data structure 'd3d12_buffer' @ " << buffer << "\n");
        d3d12_free(buffer);
    }
}

template<>
void release_d3d12_object<d3d12_library>(d3d12_library *library) {
    TRACELOG;
    library->cache.cleanup();
    d3d12_free(library);
}

template<>
void release_d3d12_object<d3d12_function>(d3d12_function *function) {
    TRACELOG;
    Release_ID3D12Object(function->shaderBlob);
    Release_ID3D12Object(function->rootSignature);
    release_object(function->pipeline_state);
    d3d12_free(function);
}

template<>
void release_d3d12_object<d3d12_profiler>(d3d12_profiler *profiler) {
    TRACELOG;
    Release_ID3D12Object(profiler->queryHeap);
    release_d3d12_object(&profiler->queryResultsBuffer);
    d3d12_free(profiler);
}

template<>
void release_d3d12_object<d3d12_compute_pipeline_state>(d3d12_compute_pipeline_state *pso) {
    TRACELOG;
    ID3D12PipelineState *p = *(pso);
    Release_ID3D12Object(p);
}

template<>
void release_d3d12_object<d3d12_frame>(d3d12_frame *frame) {
    TRACELOG;
    release_object(frame->cmd_list);
    release_object(frame->desc_binder);
    release_object(&frame->args_buffer);
    frame->cmd_list = nullptr;
    frame->desc_binder = nullptr;
    frame->args_buffer = zero_struct<d3d12_buffer>();
    frame->fence_signal = 0;
}

extern WEAK halide_device_interface_t d3d12compute_device_interface;

static d3d12_buffer *peel_buffer(struct halide_buffer_t *hbuffer) {
    TRACELOG;
    halide_assert(user_context, (hbuffer != nullptr));
    halide_assert(user_context, (hbuffer->device_interface == &d3d12compute_device_interface));
    d3d12_buffer *dbuffer = reinterpret_cast<d3d12_buffer *>(hbuffer->device);
    halide_assert(user_context, (dbuffer != nullptr));
    return dbuffer;
}

static const d3d12_buffer *peel_buffer(const struct halide_buffer_t *hbuffer) {
    return peel_buffer(const_cast<halide_buffer_t *>(hbuffer));
}

WEAK int wrap_buffer(void *user_context, struct halide_buffer_t *hbuffer, d3d12_buffer *dbuffer) {
    halide_assert(user_context, (hbuffer->device == 0));
    if (hbuffer->device != 0) {
        return halide_error_code_device_wrap_native_failed;
    }

    halide_assert(user_context, (dbuffer->resource != nullptr));

    dbuffer->offset = 0;
    dbuffer->offsetInBytes = 0;
    dbuffer->sizeInBytes = hbuffer->size_in_bytes();
    dbuffer->elements = number_of_elements(user_context, hbuffer);
    dbuffer->format = FindD3D12FormatForHalideType(user_context, hbuffer->type);
    if (dbuffer->format == DXGI_FORMAT_UNKNOWN) {
        TRACEFATAL("unsupported buffer element type: " << hbuffer->type);
        return halide_error_code_device_wrap_native_failed;
    }

    dbuffer->halide_type = hbuffer->type;
    hbuffer->device = reinterpret_cast<uint64_t>(dbuffer);
    halide_assert(user_context, (hbuffer->device_interface == nullptr));
    hbuffer->device_interface = &d3d12compute_device_interface;
    hbuffer->device_interface->impl->use_module();

    return 0;
}

WEAK int unwrap_buffer(struct halide_buffer_t *buf) {
    TRACELOG;

    if (buf->device == 0) {
        return 0;
    }

    d3d12_buffer *dbuffer = peel_buffer(buf);

    dbuffer->halide_type = halide_type_t();
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;
    buf->device = 0;

    return 0;
}

static void D3D12LoadDependencies(void *user_context) {
    TRACELOG;

    const char *lib_names[] = {
        "d3d12.dll",
        "D3DCompiler_47.dll",
        "dxgi.dll",
    };
    static const int num_libs = sizeof(lib_names) / sizeof(lib_names[0]);
    void **lib_handles[num_libs] = {
        &lib_d3d12,
        &lib_D3DCompiler_47,
        &lib_dxgi,
    };
    for (size_t i = 0; i < num_libs; i++) {
        // Only attempt to load a library if the it has not been loaded already
        void *&lib = *(lib_handles[i]);
        if (lib) {
            continue;
        }
        lib = d3d12_load_library(lib_names[i]);
    }

#if HALIDE_D3D12_RENDERDOC
#if !RENDERDOC_AUTOINIT
    TRACEPRINT("Initializing RenderDoc\n");
    bool rdinit = InitRenderDoc();
    halide_assert(user_context, rdinit);
#endif
#endif

    D3D12CreateDevice = LibrarySymbol::get(user_context, lib_d3d12, "D3D12CreateDevice");
    D3D12GetDebugInterface = LibrarySymbol::get(user_context, lib_d3d12, "D3D12GetDebugInterface");
    D3D12SerializeRootSignature = LibrarySymbol::get(user_context, lib_d3d12, "D3D12SerializeRootSignature");
    D3DCompile = LibrarySymbol::get(user_context, lib_D3DCompiler_47, "D3DCompile");
    CreateDXGIFactory1 = LibrarySymbol::get(user_context, lib_dxgi, "CreateDXGIFactory1");

    // Windows x64 follows the LLP64 integer type convention:
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa383751(v=vs.85).aspx
    static_assert(sizeof(BOOL) == (32 / 8));      // BOOL      must be  32 bits
    static_assert(sizeof(CHAR) == (8 / 8));       // CHAR      must be   8 bits
    static_assert(sizeof(SHORT) == (16 / 8));     // SHORT     must be  16 bits
    static_assert(sizeof(LONG) == (32 / 8));      // LONG      must be  32 bits
    static_assert(sizeof(ULONG) == (32 / 8));     // ULONG     must be  32 bits
    static_assert(sizeof(LONGLONG) == (64 / 8));  // LONGLONG  must be  16 bits
    static_assert(sizeof(BYTE) == (8 / 8));       // BYTE      must be   8 bits
    static_assert(sizeof(WORD) == (16 / 8));      // WORD      must be  16 bits
    static_assert(sizeof(DWORD) == (32 / 8));     // DWORD     must be  32 bits
    static_assert(sizeof(WCHAR) == (16 / 8));     // WCHAR     must be  16 bits
    static_assert(sizeof(INT) == (32 / 8));       // INT       must be  32 bits
    static_assert(sizeof(UINT) == (32 / 8));      // UINT      must be  32 bits
    static_assert(sizeof(IID) == (128 / 8));      // COM GUIDs must be 128 bits

    // Paranoid checks (I am not taking any chances...)
    static_assert(sizeof(INT8) == (8 / 8));
    static_assert(sizeof(INT16) == (16 / 8));
    static_assert(sizeof(INT32) == (32 / 8));
    static_assert(sizeof(INT64) == (64 / 8));
    static_assert(sizeof(UINT8) == (8 / 8));
    static_assert(sizeof(UINT16) == (16 / 8));
    static_assert(sizeof(UINT32) == (32 / 8));
    static_assert(sizeof(UINT64) == (64 / 8));
#ifdef BITS_64
    static_assert(sizeof(SIZE_T) == (64 / 8));
#else
    static_assert(sizeof(SIZE_T) == (32 / 8));
#endif
}

#if HALIDE_D3D12_PIX
static void D3D12WaitForPix() {
    TRACELOG;
    TRACEPRINT("[[ delay for attaching to PIX... ]]\n");
    volatile uint32_t x = (1 << 31);
    while (--x > 0) {
    }
}
#endif

static ID3D12Device *D3D12CreateDeviceForAdapter(IDXGIAdapter1 *adapter) {
    TRACELOG;

    DXGI_ADAPTER_DESC1 desc = {};
    if (FAILED(dxgiAdapter->GetDesc1(&desc))) {
        TRACEFATAL("Unable to retrieve information (DXGI_ADAPTER_DESC1) about the selectd adapter.");
        return nullptr;
    }
    char Description[128];
    for (int i = 0; i < 128; ++i) {
        Description[i] = desc.Description[i];
    }
    Description[127] = '\0';
    TRACEPRINT("Device selected: " << Description << "\n");

    // NOTE(marcos): ignoring IDXGIOutput setup since this back-end is compute only
    // (still handy to have this block of code around for debugging purposes)
    const bool setup_display_output = false;
    if (setup_display_output) {
        IDXGIOutput *dxgiDisplayOutput = nullptr;
        HRESULT result = dxgiAdapter->EnumOutputs(0, &dxgiDisplayOutput);
        if (D3DErrorCheck(result, dxgiDisplayOutput, user_context, "Unable to enumerate DXGI outputs for adapter (IDXGIOutput)")) {
            return nullptr;
        }
    }

    // WARN(marcos): Direct3D 12 devices are singletons per adapter.
    // https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice
    // "If a Direct3D 12 device already exists in the current process for a given adapter,
    // then a subsequent call to D3D12CreateDevice returns the existing device."
    // This wouldn't be a problem in most circumstances, but if the non-debug module is
    // first utilized, followed by an usage of the -debug module, D3D12CreateDevice will
    // fail since ID3D12Debug->EnableDebugLayer() has now just been called after a device
    // already exists for that adapter, often returning a rather cryptic 0x887a0007 error
    // (DXGI_ERROR_DEVICE_RESET).
    ID3D12Device *device = nullptr;
    D3D_FEATURE_LEVEL MinimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT result = D3D12CreateDevice(dxgiAdapter, MinimumFeatureLevel, IID_PPV_ARGS(&device));
    if (result == (HRESULT)0x887a0007 /*DXGI_ERROR_DEVICE_RESET*/) {
        TRACEERROR("It looks like a device for this adapter has been created before (non-debug vs. -debug)\n");
    }
    if (D3DErrorCheck(result, device, user_context, "Unable to create the Direct3D 12 device")) {
        return nullptr;
    }

#if HALIDE_D3D12_PROFILING
    // Notes on NVIDIA GPU Boost:
    // https://developer.nvidia.com/setstablepowerstateexe-%20disabling%20-gpu-boost-windows-10-getting-more-deterministic-timestamp-queries
    // MSDN: "Do not call SetStablePowerState in shipped applications.
    //        This method only works while the machine is in developer mode.
    //        If developer mode is not enabled, then device removal will occur.
    //        (DXGI_ERROR_DEVICE_REMOVED : 0x887a0005)"
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn903835(v=vs.85).aspx
    const bool enable_stable_power_state = false;
    if (enable_stable_power_state) {
        result = device->SetStablePowerState(TRUE);
        if (D3DErrorCheck(result, device, user_context, "Unable to activate stable power state")) {
            return nullptr;
        }
    }
#endif

    return device;
}

static d3d12_device *D3D12CreateSystemDefaultDevice(void *user_context) {
    TRACELOG;

#ifndef BITS_64
    TRACEFATAL("Direct3D 12 back-end not yet supported on 32bit targets...");
    return nullptr;
#endif

    D3D12LoadDependencies(user_context);

    HRESULT result = E_UNEXPECTED;

#if HALIDE_D3D12_DEBUG_LAYER
    TRACEPRINT("Using Direct3D 12 Debug Layer\n");
    d3d12Debug = nullptr;
    result = D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12Debug));
    if (D3DErrorCheck(result, d3d12Debug, user_context, "Unable to retrieve the debug interface for Direct3D 12")) {
        return nullptr;
    }
    d3d12Debug->EnableDebugLayer();
#endif

    IDXGIFactory1 *dxgiFactory = nullptr;
    result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    if (D3DErrorCheck(result, dxgiFactory, user_context, "Unable to create DXGI Factory (IDXGIFactory1)")) {
        return nullptr;
    }

    halide_assert(user_context, (dxgiAdapter == nullptr));
    size_t vram_max = 0;
    for (int i = 0;; ++i) {
        IDXGIAdapter1 *adapter = nullptr;
        HRESULT result = dxgiFactory->EnumAdapters1(i, &adapter);
        if (DXGI_ERROR_NOT_FOUND == result) {
            break;
        }
        if (D3DErrorCheck(result, adapter, user_context, "Unable to enumerate DXGI adapter (IDXGIAdapter1).")) {
            return nullptr;
        }
        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(adapter->GetDesc1(&desc))) {
            TRACEFATAL("Unable to retrieve information (DXGI_ADAPTER_DESC1) about adapter number #" << i);
            return nullptr;
        }
        char Description[128];
        for (int i = 0; i < 128; ++i) {
            Description[i] = desc.Description[i];
        }
        Description[127] = '\0';
        TRACEPRINT("Adapter #" << i << ": " << Description << "\n");
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            TRACEPRINT("(this is a software adapter; skipping...)\n");
            Release_ID3D12Object(adapter);
            continue;
        }
        // TODO(marcos): find a strategy to select the best adapter available;
        // unfortunately, most of the adapter capabilities can only be queried
        // after a logical device for it is created...
        // (see: ID3D12Device::CheckFeatureSupport)
        // for now, just pick the one with the most amount of dedicated VRAM
        if (desc.DedicatedVideoMemory > vram_max) {
            TRACEPRINT("(this is the best device so far...)\n");
            vram_max = desc.DedicatedVideoMemory;
            Release_ID3D12Object(dxgiAdapter);
            dxgiAdapter = adapter;
        }
    }

    if (dxgiAdapter == nullptr) {
        TRACEFATAL("Unable to find a suitable D3D12 Adapter.");
        return nullptr;
    }

    ID3D12Device *device = D3D12CreateDeviceForAdapter(dxgiAdapter);

    Release_ID3D12Object(dxgiFactory);

#if HALIDE_D3D12_PIX
    D3D12WaitForPix();
#endif

    return reinterpret_cast<d3d12_device *>(device);
}

ID3D12RootSignature *D3D12CreateMasterRootSignature(ID3D12Device *device) {
    TRACELOG;

    // A single "master" root signature is suitable for all Halide kernels:
    // ideally, we would like to use "unbounded tables" for the descriptor
    // binding, but "tier-1" d3d12 devices (D3D12_RESOURCE_BINDING_TIER_1)
    // do not support unbounded descriptor tables...

    D3D12_ROOT_PARAMETER TableTemplate = {};
    {
        TableTemplate.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        TableTemplate.DescriptorTable.NumDescriptorRanges = 1;
        TableTemplate.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;  // compute must use this
    }
    D3D12_DESCRIPTOR_RANGE RangeTemplate = {};
    {
        RangeTemplate.NumDescriptors = -1;  // -1 for unlimited/unbounded tables
        RangeTemplate.BaseShaderRegister = 0;
        RangeTemplate.RegisterSpace = 0;
        RangeTemplate.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER rootParameterTables[NumSlots] = {};
    D3D12_DESCRIPTOR_RANGE descriptorRanges[NumSlots] = {};
    {
        // UAVs: read-only, write-only and read-write buffers:
        D3D12_ROOT_PARAMETER &RootTableUAV = rootParameterTables[UAV];
        {
            RootTableUAV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE &UAVs = descriptorRanges[UAV];
            {
                UAVs = RangeTemplate;
                UAVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                UAVs.NumDescriptors = ResourceBindingLimits[UAV];
            }
            RootTableUAV.DescriptorTable.pDescriptorRanges = &UAVs;
        }
        // CBVs: read-only uniform/coherent/broadcast buffers:
        D3D12_ROOT_PARAMETER &RootTableCBV = rootParameterTables[CBV];
        {
            RootTableCBV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE &CBVs = descriptorRanges[CBV];
            {
                CBVs = RangeTemplate;
                CBVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                CBVs.NumDescriptors = ResourceBindingLimits[CBV];
            }
            RootTableCBV.DescriptorTable.pDescriptorRanges = &CBVs;
        }
        // SRVs: textures and read-only buffers:
        D3D12_ROOT_PARAMETER &RootTableSRV = rootParameterTables[SRV];
        {
            RootTableSRV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE &SRVs = descriptorRanges[SRV];
            {
                SRVs = RangeTemplate;
                SRVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                SRVs.NumDescriptors = ResourceBindingLimits[SRV];
            }
            RootTableSRV.DescriptorTable.pDescriptorRanges = &SRVs;
        }
    }

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    {
        rsd.NumParameters = NumSlots;
        rsd.pParameters = rootParameterTables;
        rsd.NumStaticSamplers = 0;
        rsd.pStaticSamplers = nullptr;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    }
    D3D_ROOT_SIGNATURE_VERSION Version = D3D_ROOT_SIGNATURE_VERSION_1;
    ID3DBlob *pSignBlob = nullptr;
    ID3DBlob *pSignError = nullptr;
    HRESULT result = D3D12SerializeRootSignature(&rsd, Version, &pSignBlob, &pSignError);
    if (D3DErrorCheck(result, pSignBlob, nullptr, "Unable to serialize the Direct3D 12 root signature")) {
        halide_assert(user_context, pSignError);
        TRACEFATAL((const char *)pSignError->GetBufferPointer());
        return nullptr;
    }

    ID3D12RootSignature *rootSignature = nullptr;
    UINT nodeMask = 0;
    const void *pBlobWithRootSignature = pSignBlob->GetBufferPointer();
    SIZE_T blobLengthInBytes = pSignBlob->GetBufferSize();
    result = device->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, IID_PPV_ARGS(&rootSignature));
    if (D3DErrorCheck(result, rootSignature, nullptr, "Unable to create the Direct3D 12 root signature")) {
        return nullptr;
    }

    return rootSignature;
}

WEAK void dispatch_threadgroups(d3d12_compute_command_list *cmdList,
                                int32_t blocks_x, int32_t blocks_y, int32_t blocks_z,
                                int32_t threads_x, int32_t threads_y, int32_t threads_z) {
    TRACELOG;

    static int32_t total_dispatches = 0;
    MAYBE_UNUSED(total_dispatches);
    TRACEPRINT(
        "Dispatching threadgroups (number " << total_dispatches++ << ")"
                                                                     " blocks("
                                            << blocks_x << ", " << blocks_y << ", " << blocks_z << ")"
                                                                                                   " threads("
                                            << threads_x << ", " << threads_y << ", " << threads_z << ")\n");

    (*cmdList)->Dispatch(blocks_x, blocks_y, blocks_z);
}

WEAK d3d12_buffer new_buffer_resource(d3d12_device *device, size_t length, D3D12_HEAP_TYPE heaptype) {
    TRACELOG;

    D3D12_RESOURCE_DESC desc = {};
    {
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;  // 0 defaults to 64KB alignment, which is mandatory for buffers
        desc.Width = length;
        desc.Height = 1;                               // for buffers, this must always be 1
        desc.DepthOrArraySize = 1;                     // ditto, (1)
        desc.MipLevels = 1;                            // ditto, (1)
        desc.Format = DXGI_FORMAT_UNKNOWN;             // ditto, (DXGI_FORMAT_UNKNOWN)
        desc.SampleDesc.Count = 1;                     // ditto, (1)
        desc.SampleDesc.Quality = 0;                   // ditto, (0)
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // ditto, (D3D12_TEXTURE_LAYOUT_ROW_MAJOR)
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};  // CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_...)
    {
        heapProps.Type = heaptype;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 0;  // 0 is equivalent to 0b0...01 (single adapter)
        heapProps.VisibleNodeMask = 0;   // ditto
    }

    D3D12_HEAP_PROPERTIES *pHeapProperties = &heapProps;
    D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    D3D12_RESOURCE_DESC *pDesc = &desc;
    D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_CLEAR_VALUE *pOptimizedClearValue = nullptr;  // for buffers, this must be nullptr

    switch (heaptype) {
    case D3D12_HEAP_TYPE_UPLOAD:
        // committed resources in UPLOAD heaps must start in and never change from GENERIC_READ state:
        InitialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
        break;
    case D3D12_HEAP_TYPE_READBACK:
        // committed resources in READBACK heaps must start in and never change from COPY_DEST state:
        InitialResourceState = D3D12_RESOURCE_STATE_COPY_DEST;
        break;
    case D3D12_HEAP_TYPE_DEFAULT:
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        InitialResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        break;
    default:
        TRACEPRINT("UNSUPPORTED D3D12 BUFFER HEAP TYPE: " << (int)heaptype << "\n");
        halide_assert(user_context, false);
        break;
    }

    d3d12_buffer buffer = {};
    ID3D12Resource *resource = nullptr;
    // A commited resource manages its own private heap:
    HRESULT result = (*device)->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, IID_PPV_ARGS(&resource));
    if (D3DErrorCheck(result, resource, nullptr, "Unable to create the Direct3D 12 buffer")) {
        return buffer;
    }

    buffer.resource = resource;
    buffer.capacityInBytes = length;
    buffer.sizeInBytes = length;
    buffer.state = InitialResourceState;
    buffer.type = d3d12_buffer::Unknown;
    buffer.format = DXGI_FORMAT_UNKNOWN;
    buffer.mallocd = false;
    buffer.host_mirror = nullptr;
    buffer.signal = 0;
    __atomic_store_n(&buffer.ref_count, 0, __ATOMIC_SEQ_CST);

    return buffer;
}

WEAK d3d12_buffer new_device_buffer(d3d12_device *device, size_t length) {
    TRACELOG;
    // an upload heap would have been handy here since they are accessible both by CPU and GPU;
    // however, they are only good for streaming (write once, read once, discard, rinse and repeat) vertex and constant buffer data; for unordered-access views,
    // upload heaps are not allowed.
    d3d12_buffer buffer = new_buffer_resource(device, length, D3D12_HEAP_TYPE_DEFAULT);
    buffer.type = d3d12_buffer::ReadWrite;
    return buffer;
}

// faux type name for logging purposes in D3DErrorCheck()
struct ID3D12MemoryMappedResourceFAUX;
D3D12TYPENAME(ID3D12MemoryMappedResourceFAUX)

WEAK void *map_buffer(d3d12_buffer *buffer) {
    TRACELOG;

    if (buffer->mapped) {
        return buffer->mapped;
    }

    D3D12_RANGE readRange = {};
    switch (buffer->type) {
    case d3d12_buffer::Constant:
    case d3d12_buffer::Upload:
        // upload buffers are write-only, so there is no read range
        readRange.Begin = 0;
        readRange.End = 0;
        break;
    case d3d12_buffer::ReadBack:
        // everything in the buffer might be read by the CPU
        // (we could also simply pass pReadRange = nullptr to Map(), but that issues a debug-layer warning...)
        readRange.Begin = 0;
        readRange.End = buffer->sizeInBytes;
        break;
    default:
        TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
        halide_assert(user_context, false);
        break;
    }

    TRACEPRINT("[ Begin: " << readRange.Begin << " , End: " << readRange.End << " ]\n");

    // ID3D12Resource::Map never blocks, but will invalidate caches around the read range
    ID3D12Resource *resource = buffer->resource;
    UINT Subresource = 0;  // buffers contain only one subresource (at index 0)
    const D3D12_RANGE *pReadRange = &readRange;
    void *pData = nullptr;
    HRESULT result = resource->Map(Subresource, pReadRange, &pData);
    if (D3DErrorCheck(result, (ID3D12MemoryMappedResourceFAUX *)pData, nullptr, "Unable to map Direct3D 12 staging buffer memory")) {
        return nullptr;
    }

    halide_assert(user_context, pData);
    buffer->mapped = pData;

    return pData;
}

WEAK void unmap_buffer(d3d12_buffer *buffer) {
    TRACELOG;

    void *pData = buffer->mapped;
    if (!pData) {
        return;
    }

    D3D12_RANGE writtenRange = {};
    switch (buffer->type) {
    case d3d12_buffer::Constant:
    case d3d12_buffer::Upload:
        writtenRange.Begin = 0;
        writtenRange.End = buffer->sizeInBytes;
        break;
    case d3d12_buffer::ReadBack:
        // host/CPU never writes directly to a ReadBack buffer, it only reads from it
        writtenRange.Begin = 0;
        writtenRange.End = 0;
        break;
    default:
        TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
        halide_assert(user_context, false);
        break;
    }

    TRACEPRINT("[ Begin: " << writtenRange.Begin << " , End: " << writtenRange.End << " ]\n");

    // ID3D12Resource::Unmap will flush caches around the written range
    ID3D12Resource *resource = buffer->resource;
    UINT Subresource = 0;  // buffers contain only one subresource (at index 0)
    const D3D12_RANGE *pWrittenRange = &writtenRange;
    resource->Unmap(Subresource, pWrittenRange);
    if (D3DErrorCheck(S_OK, (ID3D12MemoryMappedResourceFAUX *)pData, nullptr, "Unable to unmap Direct3D 12 staging buffer memory")) {
        return;
    }

    buffer->mapped = nullptr;
}

WEAK d3d12_buffer new_upload_buffer(d3d12_device *device, size_t length) {
    TRACELOG;
    d3d12_buffer buffer = new_buffer_resource(device, length, D3D12_HEAP_TYPE_UPLOAD);
    buffer.type = d3d12_buffer::Upload;
    // upload heaps may keep the buffer mapped persistently
    map_buffer(&buffer);
    return buffer;
}

WEAK d3d12_buffer new_readback_buffer(d3d12_device *device, size_t length) {
    TRACELOG;
    d3d12_buffer buffer = new_buffer_resource(device, length, D3D12_HEAP_TYPE_READBACK);
    buffer.type = d3d12_buffer::ReadBack;
    return buffer;
}

WEAK d3d12_buffer new_constant_buffer(d3d12_device *device, size_t length) {
    TRACELOG;
    // CBV buffer can simply use an upload heap for host and device memory:
    d3d12_buffer buffer = new_upload_buffer(device, length);
    buffer.type = d3d12_buffer::Constant;
    return buffer;
}

WEAK d3d12_buffer *new_buffer(d3d12_device *device, size_t length) {
    TRACELOG;

    d3d12_buffer buffer = new_device_buffer(device, length);

    d3d12_buffer *pBuffer = malloct<d3d12_buffer>();
    *pBuffer = buffer;
    pBuffer->mallocd = true;
    return pBuffer;
}

WEAK size_t suballocate(d3d12_device *device, d3d12_buffer *staging, size_t num_bytes) {
    TRACELOG;

    // buffer not large enough for suballocation: must grow
    if (staging->sizeInBytes < num_bytes) {
        // ensure there are no pending transfers on this buffer
        uint64_t use_count = __atomic_add_fetch(&staging->ref_count, 1, __ATOMIC_SEQ_CST);
        halide_assert(user_context, (use_count == 1));
        // find a new "ideal" size: e.g., using a cumulative 2x heuristic
        size_t old_capacity = staging->sizeInBytes;
        size_t new_capacity = 2 * (old_capacity + num_bytes);
        TRACEPRINT("not enough storage: growing from " << (uintptr_t)old_capacity << " bytes to " << (uintptr_t)new_capacity << " bytes.\n");
        // release the old storage
        use_count = __atomic_sub_fetch(&staging->ref_count, 1, __ATOMIC_SEQ_CST);
        halide_assert(user_context, (use_count == 0));
        release_d3d12_object(staging);
        // and allocate a new one
        switch (staging->type) {
        case d3d12_buffer::Upload:
            halide_assert(user_context, (staging == &upload));
            *staging = new_upload_buffer(device, new_capacity);
            break;
        case d3d12_buffer::ReadBack:
            halide_assert(user_context, (staging == &readback));
            *staging = new_readback_buffer(device, new_capacity);
            break;
        default:
            TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)staging->type << "\n");
            halide_assert(user_context, false);
            break;
        }
    }

    halide_assert(user_context, (staging->sizeInBytes >= num_bytes));
    // this reference counter will be decremented later by 'd3d12compute_device_sync_internal()'
    uint64_t use_count = __atomic_add_fetch(&staging->ref_count, 1, __ATOMIC_SEQ_CST);
    // but for now we must ensure that there are no pending transfers on this buffer already
    halide_assert(user_context, (use_count == 1));
    size_t byte_offset = 0;  // always zero, for now
    return byte_offset;
}

// faux type name for logging purposes in D3DErrorCheck()
struct ID3D12CommandQueueTimestampFrequencyFAUX;
D3D12TYPENAME(ID3D12CommandQueueTimestampFrequencyFAUX)

d3d12_profiler *new_profiler(d3d12_device *device, size_t num_queries) {
    TRACELOG;

    UINT64 Frequency = 0;
    {
        HRESULT result = (*queue)->GetTimestampFrequency(&Frequency);
        if (D3DErrorCheck(result, (ID3D12CommandQueueTimestampFrequencyFAUX *)Frequency, user_context, "Unable to query the timestamp frequency of the command queue")) {
            return nullptr;
        }
        TRACEPRINT("tick frequency: " << Frequency << " Hz.\n");
    }

    ID3D12QueryHeap *pQueryHeap = nullptr;
    {
        D3D12_QUERY_HEAP_DESC desc = {};
        {
            desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            desc.Count = num_queries;
            desc.NodeMask = 0;
        }
        HRESULT result = (*device)->CreateQueryHeap(&desc, IID_PPV_ARGS(&pQueryHeap));
        if (D3DErrorCheck(result, pQueryHeap, user_context, "Unable to create timestamp query heap")) {
            return nullptr;
        }
    }

    // Query results can only be resolved to buffers whose current state is
    // D3D12_RESOURCE_STATE_COPY_DEST; for CPU access, the buffer must also
    // reside in a read-back heap.
    size_t size_in_bytes = num_queries * sizeof(uint64_t);
    d3d12_buffer queryResultsBuffer = new_readback_buffer(device, size_in_bytes);

    d3d12_profiler *profiler = malloct<d3d12_profiler>();
    {
        profiler->queryHeap = pQueryHeap;
        profiler->tick_frequency = Frequency;
        profiler->queryResultsBuffer = queryResultsBuffer;
        profiler->next_free_query = 0;
        profiler->max_queries = num_queries;
    }

    return (profiler);
}

size_t request_timestamp_checkpoint(d3d12_command_list *cmdList, d3d12_profiler *profiler) {
    TRACELOG;
    ID3D12QueryHeap *pQueryHeap = profiler->queryHeap;
    D3D12_QUERY_TYPE Type = D3D12_QUERY_TYPE_TIMESTAMP;
    UINT Index = profiler->next_free_query;
    ++(profiler->next_free_query);
    // D3D12_QUERY_TYPE_TIMESTAMP is the only query that supports EndQuery only.
    // BeginQuery cannot be called on a timestamp query.
    (*cmdList)->EndQuery(pQueryHeap, Type, Index);
    size_t checkpoint_id = Index;
    return checkpoint_id;
}

void begin_profiling(d3d12_command_list *cmdList, d3d12_profiler *profiler) {
    TRACELOG;
    unmap_buffer(&profiler->queryResultsBuffer);
    profiler->next_free_query = 0;
}

void end_profiling(d3d12_command_list *cmdList, d3d12_profiler *profiler) {
    TRACELOG;
    ID3D12QueryHeap *pQueryHeap = profiler->queryHeap;
    D3D12_QUERY_TYPE Type = D3D12_QUERY_TYPE_TIMESTAMP;
    UINT StartIndex = 0;
    UINT NumQueries = profiler->next_free_query;
    ID3D12Resource *pDestinationBuffer = profiler->queryResultsBuffer.resource;
    UINT64 AlignedDestinationBufferOffset = 0;  // Must be a multiple of 8 bytes.
    (*cmdList)->ResolveQueryData(pQueryHeap, Type, StartIndex, NumQueries, pDestinationBuffer, AlignedDestinationBufferOffset);
}

uint64_t *get_profiling_results(d3d12_profiler *profiler) {
    TRACELOG;
    void *buffer = map_buffer(&profiler->queryResultsBuffer);
    uint64_t *timestamp_array = (uint64_t *)buffer;
    return timestamp_array;
}

double get_elapsed_time(d3d12_profiler *profiler, size_t checkpoint1, size_t checkpoint2, double resolution = 1e-6) {
    TRACELOG;
    uint64_t *timestamps = get_profiling_results(profiler);
    uint64_t ts1 = timestamps[checkpoint1];
    uint64_t ts2 = timestamps[checkpoint2];
    TRACEPRINT("ticks : [ " << ts1 << " , " << ts2 << " ]\n");
    uint64_t ticks = ts2 - ts1;
    double frequency = double(profiler->tick_frequency);
    frequency *= resolution;  // default is microsecond resolution (1e-6)
    double eps = ticks / frequency;
    return eps;
}

WEAK d3d12_command_queue *new_command_queue(d3d12_device *device) {
    TRACELOG;

    ID3D12CommandQueue *commandQueue = nullptr;
    {
        D3D12_COMMAND_QUEUE_DESC cqDesc = {};
        {
            cqDesc.Type = HALIDE_D3D12_COMMAND_LIST_TYPE;
            cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            cqDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            cqDesc.NodeMask = 0;  // 0, for single GPU operation
        }
        HRESULT result = (*device)->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue));
        if (D3DErrorCheck(result, commandQueue, nullptr, "Unable to create the Direct3D 12 command queue")) {
            return nullptr;
        }
    }

    ID3D12Fence *fence = nullptr;
    {
        HRESULT result = (*device)->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (D3DErrorCheck(result, fence, nullptr, "Unable to create the Direct3D 12 fence for command queue")) {
            return nullptr;
        }
    }

    queue_fence = fence;
    __atomic_store_n(&queue_last_signal, 0, __ATOMIC_SEQ_CST);

    hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    return reinterpret_cast<d3d12_command_queue *>(commandQueue);
}

template<D3D12_COMMAND_LIST_TYPE Type>
static d3d12_command_allocator *new_command_allocator(d3d12_device *device) {
    TRACELOG;
    halide_assert(user_context, device);
    ID3D12CommandAllocator *commandAllocator = nullptr;
    HRESULT result = (*device)->CreateCommandAllocator(Type, IID_PPV_ARGS(&commandAllocator));
    if (D3DErrorCheck(result, commandAllocator, nullptr, "Unable to create the Direct3D 12 command allocator")) {
        return nullptr;
    }
    return reinterpret_cast<d3d12_command_allocator *>(commandAllocator);
}

template<D3D12_COMMAND_LIST_TYPE Type>
static d3d12_command_list *new_command_list(d3d12_device *device, d3d12_command_allocator *allocator) {
    TRACELOG;
    ID3D12GraphicsCommandList *commandList = nullptr;
    UINT nodeMask = 0;
    ID3D12CommandAllocator *pCommandAllocator = (*allocator);
    ID3D12PipelineState *pInitialState = nullptr;
    HRESULT result = (*device)->CreateCommandList(nodeMask, Type, pCommandAllocator, pInitialState, IID_PPV_ARGS(&commandList));
    if (D3DErrorCheck(result, commandList, nullptr, "Unable to create the Direct3D 12 command list")) {
        return nullptr;
    }

    d3d12_command_list *cmdList = malloct<d3d12_command_list>();
    cmdList->p = commandList;
    cmdList->signal = 0;

    return cmdList;
}

static d3d12_command_list *new_compute_command_list(d3d12_device *device, d3d12_command_allocator *allocator) {
    TRACELOG;
    return new_command_list<HALIDE_D3D12_COMMAND_LIST_TYPE>(device, allocator);
}

static d3d12_copy_command_list *new_copy_command_list(d3d12_device *device, d3d12_command_allocator *allocator) {
    TRACELOG;
    return new_command_list<D3D12_COMMAND_LIST_TYPE_COPY>(device, allocator);
}

static d3d12_compute_pipeline_state *new_compute_pipeline_state_with_function(d3d12_device *device, d3d12_function *function) {
    TRACELOG;
    ID3D12PipelineState *pipelineState = nullptr;
    D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = {};
    {
        cpsd.pRootSignature = function->rootSignature;
        cpsd.CS.pShaderBytecode = function->shaderBlob->GetBufferPointer();
        cpsd.CS.BytecodeLength = function->shaderBlob->GetBufferSize();
        cpsd.NodeMask = 0;
        cpsd.CachedPSO.pCachedBlob = nullptr;
        cpsd.CachedPSO.CachedBlobSizeInBytes = 0;
        cpsd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    }
    HRESULT result = (*device)->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&pipelineState));
    if (D3DErrorCheck(result, pipelineState, nullptr, "Unable to create the Direct3D 12 pipeline state")) {
        return nullptr;
    }
    return reinterpret_cast<d3d12_compute_pipeline_state *>(pipelineState);
}

static void set_compute_pipeline_state(d3d12_compute_command_list *cmdList, d3d12_compute_pipeline_state *pipeline_state, d3d12_function *function, d3d12_binder *binder) {
    TRACELOG;

    ID3D12RootSignature *rootSignature = function->rootSignature;
    (*cmdList)->SetComputeRootSignature(rootSignature);

    ID3D12PipelineState *pipelineState = (*pipeline_state);
    (*cmdList)->SetPipelineState(pipelineState);

    ID3D12DescriptorHeap *heaps[] = {binder->descriptorHeap};
    (*cmdList)->SetDescriptorHeaps(1, heaps);

    (*cmdList)->SetComputeRootDescriptorTable(UAV, binder->GPU[UAV]);
    (*cmdList)->SetComputeRootDescriptorTable(CBV, binder->GPU[CBV]);
    (*cmdList)->SetComputeRootDescriptorTable(SRV, binder->GPU[SRV]);
}

static void end_recording(d3d12_compute_command_list *cmdList) {
    TRACELOG;
    (*cmdList)->Close();
}

static d3d12_binder *new_descriptor_binder(d3d12_device *device) {
    TRACELOG;
    ID3D12DescriptorHeap *descriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
    {
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dhd.NumDescriptors = 0;
        dhd.NumDescriptors += ResourceBindingLimits[UAV];
        dhd.NumDescriptors += ResourceBindingLimits[CBV];
        dhd.NumDescriptors += ResourceBindingLimits[SRV];
        dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        dhd.NodeMask = 0;
    }
    HRESULT result = (*device)->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&descriptorHeap));
    if (D3DErrorCheck(result, descriptorHeap, nullptr, "Unable to create the Direct3D 12 descriptor heap")) {
        return nullptr;
    }

    UINT descriptorSize = (*device)->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    TRACEPRINT("descriptor handle increment size: " << descriptorSize << "\n");

    d3d12_binder *binder = malloct<d3d12_binder>();
    binder->descriptorHeap = descriptorHeap;
    binder->descriptorSize = descriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE baseCPU = binder->baseCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    TRACEPRINT("descriptor heap base for CPU: " << baseCPU.ptr << " (" << (void *)baseCPU.ptr << ")\n");
    binder->CPU[UAV].ptr = (baseCPU.ptr += descriptorSize * 0);
    binder->CPU[CBV].ptr = (baseCPU.ptr += descriptorSize * ResourceBindingLimits[UAV]);
    binder->CPU[SRV].ptr = (baseCPU.ptr += descriptorSize * ResourceBindingLimits[CBV]);

    D3D12_GPU_DESCRIPTOR_HANDLE baseGPU = binder->baseGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    TRACEPRINT("descriptor heap base for GPU: " << baseGPU.ptr << " (" << (void *)baseGPU.ptr << ")\n");
    binder->GPU[UAV].ptr = (baseGPU.ptr += descriptorSize * 0);
    binder->GPU[CBV].ptr = (baseGPU.ptr += descriptorSize * ResourceBindingLimits[UAV]);
    binder->GPU[SRV].ptr = (baseGPU.ptr += descriptorSize * ResourceBindingLimits[CBV]);

    // initialize everything with null descriptors...
    for (uint32_t i = 0; i < ResourceBindingLimits[UAV]; ++i) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC NullDescUAV = {};
        {
            NullDescUAV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // don't care, but can't be unknown...
            NullDescUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            NullDescUAV.Buffer.FirstElement = 0;
            NullDescUAV.Buffer.NumElements = 0;
            NullDescUAV.Buffer.StructureByteStride = 0;
            NullDescUAV.Buffer.CounterOffsetInBytes = 0;
            NullDescUAV.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE hCPU = binder->CPU[UAV];
        hCPU.ptr += i * descriptorSize;
        (*device)->CreateUnorderedAccessView(nullptr, nullptr, &NullDescUAV, hCPU);
    }
    for (uint32_t i = 0; i < ResourceBindingLimits[CBV]; ++i) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC NullDescCBV = {};
        {
            NullDescCBV.BufferLocation = 0;
            NullDescCBV.SizeInBytes = 0;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE hCPU = binder->CPU[CBV];
        hCPU.ptr += i * descriptorSize;
        (*device)->CreateConstantBufferView(&NullDescCBV, hCPU);
    }
    for (uint32_t i = 0; i < ResourceBindingLimits[SRV]; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC NullDescSRV = {};
        {
            NullDescSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // don't care, but can't be unknown...
            NullDescSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            NullDescSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            NullDescSRV.Buffer.FirstElement = 0;
            NullDescSRV.Buffer.NumElements = 0;
            NullDescSRV.Buffer.StructureByteStride = 0;
            NullDescSRV.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE hCPU = binder->CPU[SRV];
        hCPU.ptr += i * descriptorSize;
        (*device)->CreateShaderResourceView(nullptr, &NullDescSRV, hCPU);
    }

    return binder;
}

static d3d12_library *new_library_with_source(d3d12_device *device, const char *source, size_t source_len) {
    TRACELOG;

    // Unlike Metal, Direct3D 12 does not have the concept of a "shader library"
    // We can emulate the library functionality by caching the source code until
    // the entry point is known since D3DCompile() requires the entry point name
    const int blocksize = sizeof(d3d12_library) + source_len;
    d3d12_library *library = (d3d12_library *)d3d12_malloc(blocksize);
    library->cache.inited = false;
    library->cache.init(nullptr);
    library->source_length = source_len;
    for (size_t i = 0; i < source_len; ++i) {
        library->source[i] = source[i];
    }
    library->source[source_len] = '\0';

    return library;
}

static void dump_shader(const char *source, ID3DBlob *compiler_msgs = nullptr) {
    const char *message = "<no error message reported>";
    if (compiler_msgs) {
        message = (const char *)compiler_msgs->GetBufferPointer();
    }

    Printer<BasicPrinter, 64 * 1024>(user_context)
        << "D3DCompile(): " << message << "\n"
        << ">>> HLSL shader source dump <<<\n"
        << source << "\n";
}

static d3d12_function *d3d12_compile_shader(d3d12_device *device, d3d12_library *library, const char *name,
                                            int shared_mem_bytes, int threadsX, int threadsY, int threadsZ) {
    TRACELOG;

    // Round shared memory size up to a non-zero multiple of 16
    TRACEPRINT("groupshared memory size before modification: " << shared_mem_bytes << " bytes\n");
    shared_mem_bytes = ((shared_mem_bytes > 0 ? shared_mem_bytes : 1) + 0xF) & ~0xF;
    TRACEPRINT("groupshared memory size after modification: " << shared_mem_bytes << " bytes.\n");
    TRACEPRINT("numthreads( " << threadsX << ", " << threadsY << ", " << threadsZ << " )\n");

    const char *source = library->source;
    int source_size = library->source_length;
    Printer<StringStreamPrinter, 16> SS[4] = {nullptr, nullptr, nullptr, nullptr};
    D3D_SHADER_MACRO pDefines[] = {
        {"__GROUPSHARED_SIZE_IN_BYTES", (SS[0] << shared_mem_bytes).str()},
        {"__NUM_TREADS_X", (SS[1] << threadsX).str()},
        {"__NUM_TREADS_Y", (SS[2] << threadsY).str()},
        {"__NUM_TREADS_Z", (SS[3] << threadsZ).str()},
        {nullptr, nullptr}};
    const char *shaderName = name;  // only used for debug information
    ID3DInclude *includeHandler = nullptr;
    const char *entryPoint = name;
    const char *target = "cs_5_1";  // all d3d12 hardware support SM 5.1
    UINT flags1 = 0;
    UINT flags2 = 0;  // flags related to effects (.fx files)
    ID3DBlob *shaderBlob = nullptr;
    ID3DBlob *errorMsgs = nullptr;

    flags1 |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#if HALIDE_D3D12_DEBUG_SHADERS
    flags1 |= D3DCOMPILE_DEBUG;
    flags1 |= D3DCOMPILE_SKIP_OPTIMIZATION;
    //flags1 |= D3DCOMPILE_RESOURCES_MAY_ALIAS;
    //flags1 |= D3DCOMPILE_ALL_RESOURCES_BOUND;
#endif

    //dump_shader(source);

    HRESULT result = D3DCompile(source, source_size, shaderName, pDefines, includeHandler, entryPoint, target, flags1, flags2, &shaderBlob, &errorMsgs);

    if (FAILED(result) || (shaderBlob == nullptr)) {
        TRACEPRINT("Unable to compile D3D12 compute shader (HRESULT=" << (void *)(int64_t)result << ", ShaderBlob=" << shaderBlob << " entry=" << entryPoint << ").\n");
        dump_shader(source, errorMsgs);
        Release_ID3D12Object(errorMsgs);
        TRACEFATAL("[end-of-shader-dump]");
        return nullptr;
    }

    TRACEPRINT("SUCCESS while compiling D3D12 compute shader with entry name '" << entryPoint << "'!\n");

    // even though it was successful, there may have been warning messages emitted by the compiler:
    if (errorMsgs != nullptr) {
        dump_shader(source, errorMsgs);
        Release_ID3D12Object(errorMsgs);
    }

    d3d12_function *function = malloct<d3d12_function>();
    function->shaderBlob = shaderBlob;
    function->rootSignature = rootSignature;
    rootSignature->AddRef();

    d3d12_compute_pipeline_state *pipeline_state = new_compute_pipeline_state_with_function(device, function);
    if (pipeline_state == nullptr) {
        TRACEFATAL("D3D12Compute: Could not allocate pipeline state.");
        release_object(function);
        return nullptr;
    }
    function->pipeline_state = pipeline_state;

    return function;
}

static d3d12_function *new_function_with_name(d3d12_device *device, d3d12_library *library, const char *name, size_t name_len,
                                              int shared_mem_bytes, int threadsX, int threadsY, int threadsZ) {
    TRACELOG;

    // consult the compiled function cache in the library first:
    d3d12_function *function = nullptr;
    Printer<StringStreamPrinter, 256> key(nullptr);
    key << name << "_(" << threadsX << "," << threadsY << "," << threadsZ << ")_[" << shared_mem_bytes << "]";
    halide_assert(user_context, (key.size() < key.capacity() - 1));  // make sure key fits into the stream
    int not_found = library->cache.lookup(user_context, (const uint8_t *)key.str(), key.size(), &function);
    if (not_found) {
        // function has not been cached yet: must compile it
        halide_assert(user_context, (function == nullptr));
        function = d3d12_compile_shader(device, library, name, shared_mem_bytes, threadsX, threadsY, threadsZ);
        if (function == nullptr) {
            return nullptr;
        }
        // cache the compiled function for future use:
        library->cache.store(user_context, (const uint8_t *)key.str(), key.size(), &function);
    } else {
        TRACEPRINT("function has been found in the cache!\n");
    }

    halide_assert(user_context, (function != nullptr));

    return function;
}

WEAK void set_input_buffer(d3d12_binder *binder, d3d12_buffer *input_buffer, uint32_t index) {
    TRACELOG;

    switch (input_buffer->type) {
    case d3d12_buffer::Constant: {
        TRACELEVEL(1, "CBV\n");

        // NOTE(marcos): constant buffers are only used internally by the
        // runtime; users cannot create, control or access them, so it is
        // expected that no halide_buffer_t will be associated with them:
        halide_assert(user_context, input_buffer->format == DXGI_FORMAT_UNKNOWN);

        ID3D12Resource *pResource = input_buffer->resource;
        D3D12_GPU_VIRTUAL_ADDRESS pGPU = pResource->GetGPUVirtualAddress();

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvd = {};
        {
            cbvd.BufferLocation = pGPU;
            cbvd.SizeInBytes = input_buffer->sizeInBytes;
        }

        halide_assert(user_context, (index < ResourceBindingLimits[CBV]));
        D3D12_CPU_DESCRIPTOR_HANDLE hDescCBV = binder->CPU[CBV];
        binder->CPU[CBV].ptr += binder->descriptorSize;

        (*device)->CreateConstantBufferView(&cbvd, hDescCBV);

        break;
    }

    case d3d12_buffer::ReadOnly:
        // TODO(marcos): read-only buffers should ideally be bound as SRV,
        // but Halide buffers (halide_buffer_t) do not distinguish between
        // read-only and read-write / write-only buffers... for the moment,
        // just bind read-only buffers with UAV descriptors:
    case d3d12_buffer::ReadWrite:
    case d3d12_buffer::WriteOnly: {
        TRACELEVEL(1, "UAV\n");

        DXGI_FORMAT Format = input_buffer->format;
        if (Format == DXGI_FORMAT_UNKNOWN) {
            TRACEFATAL("unsupported buffer element type: " << input_buffer->halide_type);
        }

        UINT FirstElement = input_buffer->offset;
        // for some reason, 'input_buffer->halide->number_of_elements()' is
        // returning 1 for cropped buffers... ('size_in_bytes()' returns 0)
        UINT NumElements = input_buffer->elements;
        UINT SizeInBytes = input_buffer->sizeInBytes;

        // SizeInBytes is here just for debugging/logging purposes in TRACEPRINT.
        // Because TRACEPRINT might turn into "nothing" (when call-tracing is not
        // enabled) the compiler might warn about unused variables...
        MAYBE_UNUSED(SizeInBytes);

        TRACELEVEL(3, "[" << index << "] : "
                          << (void *)input_buffer
                          << " | "
                          << "offset " << FirstElement
                          << " | "
                          << NumElements
                          << "elements (" << SizeInBytes << "bytes)"
                          << "\n");

        // A View of a non-Structured Buffer cannot be created using a nullptr Desc.
        // Default Desc parameters cannot be used, as a Format must be supplied.
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        {
            uavd.Format = Format;
            uavd.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavd.Buffer.FirstElement = FirstElement;
            uavd.Buffer.NumElements = NumElements;
            uavd.Buffer.StructureByteStride = 0;
            uavd.Buffer.CounterOffsetInBytes = 0;  // 0, since this is not an atomic counter
            uavd.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        }

        halide_assert(user_context, (index < ResourceBindingLimits[UAV]));
        D3D12_CPU_DESCRIPTOR_HANDLE hDescUAV = binder->CPU[UAV];
        binder->CPU[UAV].ptr += binder->descriptorSize;

        ID3D12Resource *pResource = input_buffer->resource;
        ID3D12Resource *pCounterResource = nullptr;  // for atomic counters

        (*device)->CreateUnorderedAccessView(pResource, pCounterResource, &uavd, hDescUAV);

        break;
    }

    case d3d12_buffer::Unknown:
    case d3d12_buffer::Upload:
    case d3d12_buffer::ReadBack:
    default:
        TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)input_buffer->type << "\n");
        halide_assert(user_context, false);
        break;
    }
}

static uint64_t queue_insert_checkpoint() {
    TRACELOG;
    uint64_t signal = __atomic_add_fetch(&queue_last_signal, 1, __ATOMIC_SEQ_CST);  // ++queue_last_signal
    TRACEPRINT("latest queue checkpoint is now #" << signal << "...\n");
    (*queue)->Signal(queue_fence, signal);
    return signal;
}

static void commit_command_list(d3d12_compute_command_list *cmdList) {
    TRACELOG;
    end_recording(cmdList);
    ID3D12CommandList *lists[] = {(*cmdList)};
    (*queue)->ExecuteCommandLists(1, lists);
    cmdList->signal = queue_insert_checkpoint();
}

static bool spinlock_until_signaled(uint64_t signal) {
    TRACELOG;
    while (queue_fence->GetCompletedValue() < signal) {
        // nothing
    }
    return true;
}

static bool block_until_signaled(uint64_t signal) {
    TRACELOG;

    TRACEPRINT("Now syncing on queue signal #" << signal << "...\n");
    HRESULT result = queue_fence->SetEventOnCompletion(signal, hFenceEvent);
    if (FAILED(result)) {
        TRACEPRINT("ERROR: Unable to associate D3D12 Fence with Windows Event\n");
        return false;
    }

    const DWORD timeout_ms = 15 * 1000;
    result = WaitForSingleObject(hFenceEvent, timeout_ms);
    if (result != WAIT_OBJECT_0) {
        D3DErrorCheck(result, hFenceEvent, nullptr, "Unable to wait for Fence Event");
        return false;
    }

    return true;
}

static void wait_until_signaled(uint64_t signal) {
    TRACELOG;

    uint64_t current_signal = queue_fence->GetCompletedValue();
    if (current_signal >= signal) {
        TRACEPRINT("Already synced up!\n");
        return;
    }

    HRESULT device_status_before = (*device)->GetDeviceRemovedReason();

    block_until_signaled(signal);

    HRESULT device_status_after = (*device)->GetDeviceRemovedReason();
    if (FAILED(device_status_after)) {
        TRACEFATAL(
            "Device Lost! GetDeviceRemovedReason(): "
            << "before: " << (void *)(int64_t)device_status_before << " | "
            << "after: " << (void *)(int64_t)device_status_after);
    }
}

static void wait_until_completed(d3d12_compute_command_list *cmdList) {
    TRACELOG;
    wait_until_signaled(cmdList->signal);
}

static void wait_until_completed(d3d12_frame *frame) {
    TRACELOG;
    wait_until_completed(frame->cmd_list);
}

static void wait_until_idle() {
    TRACELOG;
    uint64_t signal = __atomic_load_n(&queue_last_signal, __ATOMIC_SEQ_CST);
    wait_until_signaled(signal);
}

class D3D12ContextHolder {
    void *const user_context;

public:
    d3d12_device *device;
    d3d12_command_queue *queue;
    int error;

    ALWAYS_INLINE D3D12ContextHolder(void *user_context, bool create)
        : user_context(user_context) {
        error = halide_d3d12compute_acquire_context(user_context, &device, &queue, create);
    }
    ALWAYS_INLINE ~D3D12ContextHolder() {
        halide_d3d12compute_release_context(user_context);
    }
};

static bool is_buffer_managed(d3d12_buffer *buffer) {
    return buffer->xfer != nullptr;
}

static void buffer_copy_command(d3d12_copy_command_list *cmdList,
                                d3d12_buffer *src, d3d12_buffer *dst,
                                uint64_t src_byte_offset, uint64_t dst_byte_offset,
                                uint64_t num_bytes_copy) {
    TRACELOG;

    ID3D12Resource *pSrcBuffer = src->resource;
    ID3D12Resource *pDstBuffer = dst->resource;

    D3D12_RESOURCE_BARRIER src_barrier = {};
    {
        src_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        src_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        src_barrier.Transition.pResource = pSrcBuffer;
        src_barrier.Transition.Subresource = 0;
        src_barrier.Transition.StateBefore = src->state;
        src_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (src->state == D3D12_RESOURCE_STATE_GENERIC_READ) {
            halide_assert(user_context, src->type == d3d12_buffer::Upload);
            src_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
        } else {
            halide_assert(user_context, src->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    D3D12_RESOURCE_BARRIER dst_barrier = {};
    {
        dst_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        dst_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        dst_barrier.Transition.pResource = pDstBuffer;
        dst_barrier.Transition.Subresource = 0;
        dst_barrier.Transition.StateBefore = dst->state;
        dst_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        if (dst->state == D3D12_RESOURCE_STATE_COPY_DEST) {
            halide_assert(user_context, dst->type == d3d12_buffer::ReadBack);
        } else {
            halide_assert(user_context, dst->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    if (src_barrier.Transition.StateBefore != src_barrier.Transition.StateAfter) {
        (*cmdList)->ResourceBarrier(1, &src_barrier);
    }
    if (dst_barrier.Transition.StateBefore != dst_barrier.Transition.StateAfter) {
        (*cmdList)->ResourceBarrier(1, &dst_barrier);
    }

    UINT64 SrcOffset = src_byte_offset;
    UINT64 DstOffset = dst_byte_offset;
    UINT64 NumBytes = num_bytes_copy;

    (*cmdList)->CopyBufferRegion(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes);

    swap(src_barrier.Transition.StateBefore, src_barrier.Transition.StateAfter);  // restore resource state
    swap(dst_barrier.Transition.StateBefore, dst_barrier.Transition.StateAfter);  // restore resource state

    if (src_barrier.Transition.StateBefore != src_barrier.Transition.StateAfter) {
        (*cmdList)->ResourceBarrier(1, &src_barrier);
    }
    if (dst_barrier.Transition.StateBefore != dst_barrier.Transition.StateAfter) {
        (*cmdList)->ResourceBarrier(1, &dst_barrier);
    }
}

static void synchronize_host_and_device_buffer_contents(d3d12_copy_command_list *cmdList, d3d12_buffer *buffer) {
    TRACELOG;

    d3d12_buffer::transfer_t *xfer = buffer->xfer;
    halide_assert(user_context, (xfer != nullptr));

    d3d12_buffer *src = nullptr;
    d3d12_buffer *dst = nullptr;
    uint64_t src_byte_offset = 0;
    uint64_t dst_byte_offset = 0;
    uint64_t num_bytes_copy = xfer->size;

    d3d12_buffer *staging = xfer->staging;
    switch (staging->type) {
    case d3d12_buffer::Upload:
        TRACEPRINT("uploading buffer to device\n")
        src = staging;
        dst = buffer;
        src_byte_offset = xfer->offset;
        dst_byte_offset = buffer->offsetInBytes;
        break;
    case d3d12_buffer::ReadBack:
        TRACEPRINT("reading-back buffer from device\n")
        unmap_buffer(staging);
        src = buffer;
        dst = staging;
        src_byte_offset = buffer->offsetInBytes;
        dst_byte_offset = xfer->offset;
        break;
    default:
        TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
        halide_assert(user_context, false);
        break;
    }

    TRACEPRINT("--- "
               << (void *)buffer << " | " << buffer->halide_type << " | "
               << src_byte_offset << " : " << dst_byte_offset << " : " << num_bytes_copy
               << "\n");

    buffer_copy_command(cmdList, src, dst, src_byte_offset, dst_byte_offset, num_bytes_copy);
}

static void d3d12compute_device_sync_internal(d3d12_device *device, d3d12_buffer *dev_buffer) {
    TRACELOG;

    // sync request not tied to buffer operation
    if (dev_buffer == nullptr) {
        return wait_until_idle();
    }

    if (is_buffer_managed(dev_buffer)) {
        // NOTE(marcos): a copy/dma command list would be ideal here, but it would
        // also require a dedicated copy command queue to submit it... for now just
        // use the main compute queue and issue copies via compute command lists.
        //static const D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_COPY;
        d3d12_frame *frame = acquire_frame(device);
        d3d12_compute_command_list *blitCmdList = frame->cmd_list;
        synchronize_host_and_device_buffer_contents(blitCmdList, dev_buffer);
        enqueue_frame(frame);
        wait_until_completed(frame);
    }

    if (dev_buffer->xfer != nullptr) {
        d3d12_buffer *staging_buffer = dev_buffer->xfer->staging;
        // decrement the reference counter that was incremented by 'suballocate()'
        uint64_t use_count = __atomic_sub_fetch(&staging_buffer->ref_count, 1, __ATOMIC_SEQ_CST);
        // for now, we expect to have been the only one with pending transfer on the staging buffer:
        halide_assert(user_context, (use_count == 0));
        dev_buffer->xfer = nullptr;
    }
}

static int d3d12compute_buffer_copy(d3d12_device *device,
                                    d3d12_buffer *src,
                                    d3d12_buffer *dst,
                                    uint64_t src_byte_offset,
                                    uint64_t dst_byte_offset,
                                    uint64_t num_bytes) {
    TRACELOG;

    halide_assert(user_context, device);
    halide_assert(user_context, src);
    halide_assert(user_context, dst);
    halide_assert(user_context, (src->type != d3d12_buffer::Unknown));
    halide_assert(user_context, (dst->type != d3d12_buffer::Unknown));
    // constant buffers are only used internally (Halide never expose them)
    // (uploads to constant buffers are managed automatically by Map/Unmap)
    halide_assert(user_context, (src->type != d3d12_buffer::Constant));
    halide_assert(user_context, (dst->type != d3d12_buffer::Constant));

    halide_assert(user_context, num_bytes > 0);

    if (src->type == d3d12_buffer::Upload) {
        // host-to-device via staging buffer:
        halide_assert(user_context, (dst->type != d3d12_buffer::Upload));
        halide_assert(user_context, (dst->type != d3d12_buffer::ReadBack));
        // TODO: assert that offsets and sizes are within bounds
        d3d12_buffer::transfer_t xfer = {};
        xfer.staging = src;
        xfer.offset = src_byte_offset;
        xfer.size = num_bytes;
        halide_assert(user_context, (dst->xfer == nullptr));
        dst->xfer = &xfer;
        d3d12compute_device_sync_internal(device, dst);
        return 0;
    }

    if (dst->type == d3d12_buffer::ReadBack) {
        // device-to-host via staging buffer:
        halide_assert(user_context, (src->type != d3d12_buffer::Upload));
        halide_assert(user_context, (src->type != d3d12_buffer::ReadBack));
        // TODO: assert that offsets and sizes are within bounds
        d3d12_buffer::transfer_t xfer = {};
        xfer.staging = dst;
        xfer.offset = dst_byte_offset;
        xfer.size = num_bytes;
        halide_assert(user_context, (src->xfer == nullptr));
        src->xfer = &xfer;
        d3d12compute_device_sync_internal(device, src);
        return 0;
    }

    // device-to-device:
    halide_assert(user_context, (src->type != d3d12_buffer::Upload));
    halide_assert(user_context, (dst->type != d3d12_buffer::Upload));
    halide_assert(user_context, (src->type != d3d12_buffer::ReadBack));
    halide_assert(user_context, (dst->type != d3d12_buffer::ReadBack));

    // ReadWrite, ReadOnly and WriteOnly are shader usage hints, not copy hints
    // (there's no need to worry about them during device-to-device transfers)

    d3d12_frame *frame = acquire_frame(device);
    d3d12_compute_command_list *blitCmdList = frame->cmd_list;
    buffer_copy_command(blitCmdList, src, dst, src_byte_offset, dst_byte_offset, num_bytes);
    enqueue_frame(frame);
    src->signal = frame->fence_signal;
    dst->signal = frame->fence_signal;

    return 0;
}

static void *buffer_contents(d3d12_buffer *buffer) {
    TRACELOG;

    void *pData = nullptr;

    switch (buffer->type) {

    case d3d12_buffer::Constant:
    case d3d12_buffer::Upload:
        pData = buffer->mapped;
        break;

    case d3d12_buffer::ReadBack:
        // on readback heaps, map/unmap as needed, since the results are only effectively
        // published after a Map() call, and should ideally be in an unmapped state prior
        // to the CopyBufferRegion() call
        pData = map_buffer(&readback);
        break;

    case d3d12_buffer::ReadOnly:
    case d3d12_buffer::WriteOnly:
    case d3d12_buffer::ReadWrite: {
        TRACEWARN("UNCHARTED TERRITORY! THIS CASE IS NOT EXPECTED TO HAPPEN FOR NOW!\n");
        halide_assert(user_context, false);

        D3D12ContextHolder d3d12_context(user_context, true);
        if (d3d12_context.error != 0) {
            return nullptr;
        }

        // 1. download data from device (copy to the "readback" staging memory):
        size_t total_size = buffer->sizeInBytes;
        size_t dev_byte_offset = buffer->offsetInBytes;  // handle cropping
        d3d12_buffer *staging = &readback;
        size_t staging_byte_offset = suballocate(d3d12_context.device, staging, total_size);
        d3d12compute_buffer_copy(d3d12_context.device, buffer, staging,
                                 dev_byte_offset, staging_byte_offset, total_size);
        void *staging_data_begin = map_buffer(staging);
        uint64_t address = reinterpret_cast<uint64_t>(staging_data_begin) + staging_byte_offset;
        pData = reinterpret_cast<void *>(address);
        break;
    }

    case d3d12_buffer::Unknown:
    default:
        TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
        halide_assert(user_context, false);
        break;
    }

    halide_assert(user_context, pData);

    return pData;
}

volatile ScopedSpinLock::AtomicFlag WEAK thread_lock = 0;

WEAK Halide::Internal::GPUCompilationCache<d3d12_device *, d3d12_library *> compilation_cache;

WEAK d3d12_library *compile_kernel(d3d12_device *device, const char *source, int source_size, int *error_ret) {
    d3d12_library *library = new_library_with_source(device, source, source_size);
    if (library == nullptr) {
        TRACEFATAL("D3D12Compute: new_library_with_source failed.");
        *error_ret = halide_error_code_out_of_memory;
        return nullptr;
    }

    return library;
}

}  // namespace D3D12Compute
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::D3D12Compute;

extern "C" {

static int d3d12_create_context(void *user_context) {
    TRACELOG;

#ifdef DEBUG_RUNTINE
    TRACEPRINT("THIS IS A DEBUG CONTEXT (-debug)");
#endif

    int status = halide_error_code_success;

    halide_assert(user_context, (device == nullptr));
    device = D3D12CreateSystemDefaultDevice(user_context);
    if (device == nullptr) {
        status = halide_error_code_generic_error;
    }

    if (status == halide_error_code_success) {
        halide_assert(user_context, (rootSignature == nullptr));
        rootSignature = D3D12CreateMasterRootSignature((*device));
        if (rootSignature == nullptr) {
            status = halide_error_code_generic_error;
        }
    }

    if (status == halide_error_code_success) {
        halide_assert(user_context, (queue == nullptr));
        queue = new_command_queue(device);
        if (queue == nullptr) {
            status = halide_error_code_generic_error;
        }
    }

    if (status == halide_error_code_success) {
        halide_assert(user_context, (cmd_allocator_main == nullptr));
        cmd_allocator_main = new_command_allocator<HALIDE_D3D12_COMMAND_LIST_TYPE>(device);
        if (cmd_allocator_main == nullptr) {
            status = halide_error_code_generic_error;
        }
    }

    if (status == halide_error_code_success) {
        // NOTE(marcos): a small amount of hard-coded staging buffer storage is
        // sufficient to get started as suballocations will grow them as needed
        halide_assert(user_context, (upload == 0));
        halide_assert(user_context, (readback == 0));
        size_t heap_size = 4 * 1024 * 1024;
        upload = new_upload_buffer(device, heap_size);
        readback = new_readback_buffer(device, heap_size);
    }

    if (status != halide_error_code_success) {
        if (cmd_allocator_main) {
            release_object(cmd_allocator_main);
            cmd_allocator_main = nullptr;
        }
        if (queue) {
            release_object(queue);
            queue = nullptr;
        }
        if (rootSignature) {
            Release_ID3D12Object(rootSignature);
            rootSignature = nullptr;
        }
        if (device) {
            release_object(device);
            device = nullptr;
        }
    }

    return status;
}

// The default implementation of halide_d3d12compute_acquire_context uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the following
// behavior:
// - halide_acquire_d3d12compute_context should always store a valid device/command
//   queue in device/q, or return an error code.
// - A call to halide_acquire_d3d12compute_context is followed by a matching call to
//   halide_release_d3d12compute_context. halide_acquire_d3d12compute_context should block while a
//   previous call (if any) has not yet been released via halide_release_d3d12compute_context.
WEAK int halide_d3d12compute_acquire_context(void *user_context, halide_d3d12compute_device **device_ret,
                                             halide_d3d12compute_command_queue **queue_ret, bool create) {
    TRACELOG;

#ifdef HALIDE_D3D12_TRACE_TIME
    halide_start_clock(user_context);
#endif

    halide_assert(user_context, &thread_lock != nullptr);
    while (__atomic_test_and_set(&thread_lock, __ATOMIC_ACQUIRE)) {
    }

    TRACEPRINT("user_context: " << user_context << " | create: " << create << "\n");
    TRACEPRINT("current d3d12_device: " << device << "\n");

    if (create && (device == nullptr)) {
        int status = d3d12_create_context(user_context);
        if (status != halide_error_code_success) {
            halide_d3d12compute_release_context(user_context);
            return status;
        }
    }

    // If the device has already been initialized,
    // ensure the queue has as well.
    halide_assert(user_context, (device == nullptr) || (queue != nullptr));

    *device_ret = device;
    *queue_ret = queue;

    return 0;
}

WEAK int halide_d3d12compute_release_context(void *user_context) {
    TRACELOG;
    __atomic_clear(&thread_lock, __ATOMIC_RELEASE);
    return 0;
}

}  // extern "C"

static void d3d12_debug_dump(error &err) {
    if (!device) {
        err << "Debug info not available: no device.\n";
        return;
    }

    halide_assert(user_context, (dxgiAdapter != nullptr));
    DXGI_ADAPTER_DESC1 desc = {};
    if (FAILED(dxgiAdapter->GetDesc1(&desc))) {
        err << "Unable to retrieve information about the device adapter.\n";
        return;
    }

    // NOTE(marcos): this printer will leak, but that's fine since debug dump
    // is a panic mechanism that precedes an operational "halt":
    void *dump_buffer = d3d12_malloc(64 * 1024);
    if (!dump_buffer) {
        err << "Unable to allocate memory for the debug dump.\n";
        return;
    }

    // simple conversion from Windows 16bit wchar to char:
    char Description[128];
    for (int i = 0; i < 128; ++i) {
        Description[i] = desc.Description[i];
    }
    Description[127] = '\0';

    err << "D3D12 Device Adapter: " << Description << "\n";
}

using namespace Halide::Runtime::Internal::D3D12Compute;

// NOTE(marcos): purposedly disabling cache on 'master' for now
WEAK bool enable_allocation_cache = false;
static const int MaxBuffersInCache = 32;
WEAK d3d12_buffer *buffer_pool[MaxBuffersInCache] = {};
WEAK halide_mutex buffer_pool_lock;

static d3d12_buffer *d3d12_allocation_cache_get_buffer(void *user_context, size_t size_in_bytes) {
    TRACELOG;

    if (!halide_can_reuse_device_allocations(user_context) || !enable_allocation_cache) {
        TRACEPRINT("(allocation cache is disabled...)\n");
        return nullptr;
    }

    ScopedMutexLock lock(&buffer_pool_lock);

    d3d12_buffer *d3d12_buf = nullptr;
    size_t best_fit_size = ~0;
    size_t best_fit_index = 0;
    for (size_t i = 0; i < MaxBuffersInCache; ++i) {
        if (buffer_pool[i] == nullptr) {
            continue;
        }
        d3d12_buffer *dbuffer = buffer_pool[i];
        if (dbuffer->capacityInBytes < size_in_bytes) {
            continue;
        }
        if (dbuffer->capacityInBytes > best_fit_size) {
            continue;
        }
        best_fit_size = dbuffer->capacityInBytes;
        best_fit_index = i;
        d3d12_buf = dbuffer;
    }
    if (d3d12_buf != nullptr) {
        TRACEPRINT("serving request from allocation cache: " << size_in_bytes << " bytes from capacity of " << best_fit_size << "\n");
        buffer_pool[best_fit_index] = nullptr;
    }

    return d3d12_buf;
}

static bool d3d12_allocation_cache_put_buffer(void *user_context, d3d12_buffer *dbuffer) {
    TRACELOG;

    if (!halide_can_reuse_device_allocations(user_context) || !enable_allocation_cache) {
        TRACEPRINT("(allocation cache is disabled...)\n");
        return false;
    }

    ScopedMutexLock lock(&buffer_pool_lock);

    for (auto &buffer : buffer_pool) {
        if (buffer != nullptr) {
            continue;
        }
        TRACEPRINT("caching allocation for later use...\n");
        buffer = dbuffer;
        return true;
    }

    TRACEPRINT("cache is full: discarding buffer...\n");
    return false;
}

extern "C" {

WEAK int halide_d3d12compute_device_malloc(void *user_context, halide_buffer_t *buf) {
    TRACELOG;

    TRACEPRINT("user_context: " << user_context << " | halide_buffer_t: " << buf << "\n");

    if (buf->device) {
        TRACEPRINT("(this buffer already has a device allocation...)\n");
        return 0;
    }

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size > 0);

    // Check all strides positive
    for (int i = 0; i < buf->dimensions; i++) {
        halide_assert(user_context, buf->dim[i].stride >= 0);
    }

    d3d12_buffer *d3d12_buf = d3d12_allocation_cache_get_buffer(user_context, size);

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    if (d3d12_buf == nullptr) {
        d3d12_buf = new_buffer(d3d12_context.device, size);
    }
    if (d3d12_buf == nullptr) {
        TRACEFATAL("D3D12: Failed to allocate buffer of size " << (int64_t)size);
        return halide_error_code_device_malloc_failed;
    }

    if (0 != wrap_buffer(user_context, buf, d3d12_buf)) {
        TRACEFATAL("D3D12: unable to wrap halide buffer and D3D12 buffer.");
        return halide_error_code_device_wrap_native_failed;
    }

    return 0;
}

WEAK int halide_d3d12compute_device_free(void *user_context, halide_buffer_t *buf) {
    TRACELOG;

    TRACEPRINT("user_context: " << user_context << " | halide_buffer_t: " << buf << "\n");

    if (buf->device == 0) {
        return 0;
    }

    d3d12_buffer *dbuffer = peel_buffer(buf);
    TRACEPRINT("d3d12_buffer: " << dbuffer << "\n");

    const bool cached = d3d12_allocation_cache_put_buffer(user_context, dbuffer);

    unwrap_buffer(buf);

    if (!cached) {
        // it is safe to call release_d3d12_object() here:
        // if 'buf' holds a user resource (from halide_d3d12compute_wrap_buffer),
        // the reference counter of the underlying ID3D12Resource will just get
        // decremented without actually freeing the underlying resource object
        // since it was incremented during thewrapping process.
        // If 'buf' holds a buffer created by halide_d3d12compute_device_malloc,
        // it will be freed.
        wait_until_signaled(dbuffer->signal);
        release_d3d12_object(dbuffer);
    }

    return 0;
}

WEAK int halide_d3d12compute_initialize_kernels(void *user_context, void **state_ptr, const char *source, int source_size) {
    TRACELOG;

    D3D12ContextHolder d3d12_context(user_context, true);

    int error = halide_error_code_generic_error;
    d3d12_library *library{};
    if (!compilation_cache.kernel_state_setup(user_context, state_ptr, d3d12_context.device,
                                              library, compile_kernel, d3d12_context.device,
                                              source, source_size, &error)) {
        return error;
    }

    return 0;
}

WEAK void halide_d3d12compute_finalize_kernels(void *user_context, void *state_ptr) {
    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error == 0) {
        compilation_cache.release_hold(user_context, d3d12_context.device, state_ptr);
    }
}

namespace {

void compute_barrier(d3d12_copy_command_list *cmdList, d3d12_buffer *buffer) {
    TRACELOG;

    D3D12_RESOURCE_BARRIER barrier = {};
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = buffer->resource;
    }

    (*cmdList)->ResourceBarrier(1, &barrier);
}

}  // namespace

WEAK int halide_d3d12compute_device_sync(void *user_context, struct halide_buffer_t *buffer) {
    TRACELOG;

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    d3d12_buffer *dbuffer = peel_buffer(buffer);
    d3d12compute_device_sync_internal(d3d12_context.device, dbuffer);

    return 0;
}

WEAK int halide_d3d12compute_device_release(void *user_context) {
    TRACELOG;

    // The D3D12Context object does not allow the context storage to be modified,
    // so we use halide_d3d12compute_acquire_context directly.
    int error;
    d3d12_device *acquired_device;
    d3d12_command_queue *acquired_queue;
    error = halide_d3d12compute_acquire_context(user_context, &acquired_device, &acquired_queue, false);
    if (error != 0) {
        return error;
    }

    if (device) {
        d3d12compute_device_sync_internal(device, nullptr);

        for (auto &frame : frame_pool) {
            release_object(&frame);
        }

        for (auto &buffer : buffer_pool) {
            release_object(buffer);
        }

        compilation_cache.delete_context(user_context, device, release_object<d3d12_library>);

        // Release the device itself, if we created it.
        if (acquired_device == device) {
            release_object(&upload);
            release_object(&readback);
            d3d12_buffer empty = {};
            upload = readback = empty;

            Release_ID3D12Object(rootSignature);
            rootSignature = nullptr;

            release_object(queue);
            queue = nullptr;

            CloseHandle(hFenceEvent);
            hFenceEvent = nullptr;

            release_object(cmd_allocator_main);
            cmd_allocator_main = nullptr;

            release_object(device);
            device = nullptr;
        }
    }

    halide_d3d12compute_release_context(user_context);

    return 0;
}

namespace {

int do_multidimensional_copy(d3d12_device *device, const device_copy &c,
                             uint64_t src_offset, uint64_t dst_offset, int dimensions) {
    TRACELOG;

    if (dimensions == 0) {
        d3d12_buffer *dsrc = reinterpret_cast<d3d12_buffer *>(c.src);
        d3d12_buffer *ddst = reinterpret_cast<d3d12_buffer *>(c.dst);
        halide_assert(user_context, (dsrc->halide_type == ddst->halide_type));
        TRACEPRINT("src_offset: " << src_offset << "\n");
        TRACEPRINT("dst_offset: " << dst_offset << "\n");
        TRACEPRINT("c.chunk_size: " << c.chunk_size << "\n");
        return d3d12compute_buffer_copy(device, dsrc, ddst, src_offset, dst_offset, c.chunk_size);
    }

    // TODO: deal with negative strides. Currently the code in
    // device_buffer_utils.h does not do so either.
    const int d = dimensions;
    uint64_t src_off = 0, dst_off = 0;
    for (uint64_t i = 0; i < c.extent[d - 1]; i++) {
        src_offset += src_off;
        dst_offset += dst_off;
        int err = do_multidimensional_copy(device, c, src_offset, dst_offset, d - 1);
        if (err) {
            return err;
        }
        dst_off = c.dst_stride_bytes[d - 1];
        src_off = c.src_stride_bytes[d - 1];
    }

    return 0;
}

}  // namespace

WEAK int halide_d3d12compute_copy_to_device(void *user_context, halide_buffer_t *buffer) {
    TRACELOG;

    halide_assert(user_context, buffer);
    halide_assert(user_context, buffer->host && buffer->device);

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    // 1. memcpy from halide host memory to "upload" staging memory
    device_copy c = make_host_to_device_copy(buffer);
    halide_assert(user_context, (c.dst == buffer->device));
    d3d12_buffer *dev_buffer = peel_buffer(buffer);
    halide_assert(user_context, buffer->size_in_bytes() == dev_buffer->sizeInBytes);
    size_t total_size = dev_buffer->sizeInBytes;
    d3d12_buffer *staging = &upload;
    size_t staging_byte_offset = suballocate(d3d12_context.device, staging, total_size);
    // the 'host' buffer already points to the beginning of the cropped region
    size_t dev_byte_offset = dev_buffer->offsetInBytes;  // handle cropping
    c.src = reinterpret_cast<uint64_t>(buffer->host) + 0;
    void *staging_base = buffer_contents(staging);
    c.dst = reinterpret_cast<uint64_t>(staging_base) + staging_byte_offset;

    // 'copy_memory()' will do the smart thing and copy slices (disjoint copies
    // via multiple 'memcpy()' calls), but 'd3d12compute_buffer_copy()' has no
    // such notion and can only write whole ranges... as such, untouched areas
    // of the upload staging buffer might leak-in and corrupt the device data;
    // for now, just use 'memcpy()' to keep things in sync
    //copy_memory(c, user_context);
    memcpy(
        reinterpret_cast<void *>(c.dst),
        reinterpret_cast<void *>(c.src),
        total_size);

    // 2. upload data to device (through the "upload" staging memory):
    d3d12compute_buffer_copy(d3d12_context.device, staging, dev_buffer,
                             staging_byte_offset, dev_byte_offset, total_size);

    return 0;
}

WEAK int halide_d3d12compute_copy_to_host(void *user_context, halide_buffer_t *buffer) {
    TRACELOG;

    halide_assert(user_context, buffer);
    halide_assert(user_context, buffer->host && buffer->device);
    if (buffer->dimensions > MAX_COPY_DIMS) {
        halide_assert(user_context, false);
        return halide_error_code_copy_to_host_failed;
    }

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    // 1. download data from device (copy to the "readback" staging memory):
    d3d12_buffer *dev_buffer = peel_buffer(buffer);
    d3d12_buffer *staging = &readback;
    halide_assert(user_context, buffer->size_in_bytes() == dev_buffer->sizeInBytes);
    size_t total_size = dev_buffer->sizeInBytes;
    size_t dev_byte_offset = dev_buffer->offsetInBytes;  // handle cropping
    size_t staging_byte_offset = suballocate(d3d12_context.device, staging, total_size);
    d3d12compute_buffer_copy(d3d12_context.device, dev_buffer, staging,
                             dev_byte_offset, staging_byte_offset, total_size);

    // 2. memcpy from "readback" staging memory to halide host memory
    device_copy c = make_device_to_host_copy(buffer);
    void *staging_data = buffer_contents(staging);
    c.src = reinterpret_cast<uint64_t>(staging_data) + staging_byte_offset;
    // the 'host' buffer already points to the beginning of the cropped region
    c.dst = reinterpret_cast<uint64_t>(buffer->host) + 0;
    copy_memory(c, user_context);

    return 0;
}

WEAK int halide_d3d12compute_run(void *user_context,
                                 void *state_ptr,
                                 const char *entry_name,
                                 int blocksX, int blocksY, int blocksZ,
                                 int threadsX, int threadsY, int threadsZ,
                                 int shared_mem_bytes,
                                 halide_type_t arg_types[], void *args[], int8_t arg_is_buffer[]) {
    TRACELOG;

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    d3d12_device *device = d3d12_context.device;

#if HALIDE_D3D12_RENDERDOC
    TRACEPRINT(">>> RenderDoc Capture Start\n");
    StartCapturingGPUActivity();
#endif

    d3d12_library *library{};
    bool found = compilation_cache.lookup(device, state_ptr, library);
    halide_assert(user_context, found && library != nullptr);

    d3d12_frame *frame = acquire_frame(device);
    d3d12_compute_command_list *cmdList = frame->cmd_list;
    d3d12_binder *binder = frame->desc_binder;
    d3d12_buffer &uniform_buffer = frame->args_buffer;

    // kernel code setup:
    d3d12_function *function = nullptr;
    d3d12_compute_pipeline_state *pipeline_state = nullptr;
    {
        TRACE_SCOPE("kernel shader selection");
        function = new_function_with_name(device, library, entry_name, strlen(entry_name),
                                          shared_mem_bytes, threadsX, threadsY, threadsZ);
        halide_assert(user_context, function);
        pipeline_state = function->pipeline_state;
        set_compute_pipeline_state(cmdList, pipeline_state, function, binder);
    }

    // kernel argument(s) setup:
    d3d12_buffer **buffer_args = nullptr;
    size_t num_buffer_args = 0;
    {
        TRACE_SCOPE("kernel argument setup");

        size_t num_kernel_args = 0;
        size_t *arg_sizes = nullptr;
        size_t total_uniform_args_size = 0;
        {
            TRACE_SCOPE("kernel args introspection");

            for (int i = 0; args[i] != nullptr; i++) {
                ++num_kernel_args;
            }

            buffer_args = (d3d12_buffer **)__builtin_alloca(num_kernel_args * sizeof(d3d12_buffer *));
            arg_sizes = (size_t *)__builtin_alloca(num_kernel_args * sizeof(size_t));
            for (size_t i = 0; i < num_kernel_args; i++) {
                if (arg_is_buffer[i]) {
                    arg_sizes[i] = sizeof(void *);
                    halide_buffer_t *hbuffer = (halide_buffer_t *)args[i];
                    d3d12_buffer *buffer = peel_buffer(hbuffer);
                    buffer_args[num_buffer_args] = buffer;
                    ++num_buffer_args;
                } else {
                    // Here, it's safe to mimic the Metal back-end behavior which enforces
                    // natural alignment for all types in structures: each uniform arg has
                    // to be a power-of-two and have the subsequent field start on the next
                    // multiple of that power-of-two.
                    halide_type_t arg_type = arg_types[i];
                    arg_sizes[i] = arg_type.bytes();
                    halide_assert(user_context, (arg_sizes[i] & (arg_sizes[i] - 1)) == 0);
                    // We can ignore vector arguments since they never show up in constant
                    // blocks. Having to worry about scalar parameters only is convenient
                    // since in HLSL SM 5.1 all scalar types are 32bit:
                    halide_assert(user_context, arg_type.lanes == 1);
                    halide_assert(user_context, arg_sizes[i] > 0);
                    halide_assert(user_context, arg_sizes[i] <= 4);
                    size_t packed_size = 4;  // force the final "packed" argument to be 32bit
                    total_uniform_args_size = (total_uniform_args_size + packed_size - 1) & ~(packed_size - 1);
                    total_uniform_args_size += packed_size;
                }
            }
        }

        // pack all non-buffer arguments into a single "constant" allocation block:
        bool has_uniform_arguments = (total_uniform_args_size > 0);
        if (has_uniform_arguments) {
            TRACE_SCOPE("argument buffer packing");
            // Direct3D 12 expects constant buffers to have sizes multiple of 256:
            size_t constant_buffer_size = (total_uniform_args_size + 255) & ~255;
            if (constant_buffer_size > uniform_buffer.sizeInBytes) {
                release_object(&uniform_buffer);
                uniform_buffer = new_constant_buffer(device, constant_buffer_size);
                if (!uniform_buffer) {
                    release_object(function);
                    TRACEFATAL("D3D12Compute: Could not allocate arguments buffer.");
                    return halide_error_code_out_of_memory;
                }
            }
            uint8_t *uniform_bytes = (uint8_t *)buffer_contents(&uniform_buffer);
            size_t offset = 0;
            int32_t uniform_word = 0;
            const size_t uniform_size = 4;
            for (size_t i = 0; i < num_kernel_args; i++) {
                if (arg_is_buffer[i]) {
                    continue;
                }
                const halide_type_t arg_type = arg_types[i];
                if (arg_type.code == halide_type_float) {
                    halide_assert(user_context, (arg_type.bits == 32));
                    float &uniform_value = ((float &)uniform_word);
                    uniform_value = *((float *)args[i]);
                    TRACELEVEL(3, "args[" << i << "] -> float32 = " << uniform_value << "\n");
                } else if (arg_type.code == halide_type_int) {
                    int32_t &uniform_value = ((int32_t &)uniform_word);
                    if (arg_type.bits == 1) {
                        uniform_value = *((int8_t *)args[i]);
                        uniform_value = (uniform_value == 0) ? 0 : 1;
                    } else if (arg_type.bits == 8) {
                        uniform_value = *((int8_t *)args[i]);
                    } else if (arg_type.bits == 16) {
                        uniform_value = *((int16_t *)args[i]);
                    } else if (arg_type.bits == 32) {
                        uniform_value = *((int32_t *)args[i]);
                    } else {
                        halide_assert(user_context, false);
                    }
                    TRACELEVEL(3, "args[" << i << "] -> int32 = " << uniform_value << "\n");
                } else if (arg_type.code == halide_type_uint) {
                    uint32_t &uniform_value = ((uint32_t &)uniform_word);
                    if (arg_type.bits == 1) {
                        uniform_value = *((uint8_t *)args[i]);
                        uniform_value = (uniform_value == 0) ? 0 : 1;
                    } else if (arg_type.bits == 8) {
                        uniform_value = *((uint8_t *)args[i]);
                    } else if (arg_type.bits == 16) {
                        uniform_value = *((uint16_t *)args[i]);
                    } else if (arg_type.bits == 32) {
                        uniform_value = *((uint32_t *)args[i]);
                    } else {
                        halide_assert(user_context, false);
                    }
                    TRACELEVEL(3, "args[" << i << "] -> uint32 = " << uniform_value << "\n");
                } else {
                    halide_assert(user_context, false);
                }
                memcpy(&uniform_bytes[offset], &uniform_word, uniform_size);
                offset = (offset + uniform_size - 1) & ~(uniform_size - 1);
                offset += uniform_size;
            }
            halide_assert(user_context, offset == total_uniform_args_size);
        }

        {
            TRACE_SCOPE("descriptor binding");

            // setup/bind the argument buffer:
            if (has_uniform_arguments) {
                // always bind argument buffer at constant buffer binding 0
                int32_t cb_index = 0;  // a.k.a. register(c0)
                set_input_buffer(binder, &uniform_buffer, cb_index);
            }

            // setup/bind actual buffers:
            for (size_t i = 0; i < num_buffer_args; i++) {
                d3d12_buffer *buffer = buffer_args[i];
                int32_t uav_index = (int32_t)i;
                set_input_buffer(binder, buffer, uav_index);  // register(u#)
            }
        }
    }

    {
        TRACE_SCOPE("pipeline barriers");
        // TODO(marcos): avoid placing UAV barriers all the time after a dispatch...
        // in fact, only buffers written to by the dispatch will need barriers, and
        // only when later bound for read. For now, Halide does not provide enough
        // context for choosing the right time to place transition barriers.
        // TODO(marcos): also, it's best if we batch them into a single ResourceBarrier call
        for (size_t i = 0; i < num_buffer_args; i++) {
            d3d12_buffer *buffer = buffer_args[i];
            compute_barrier(cmdList, buffer);
        }
    }

#if HALIDE_D3D12_PROFILING
    d3d12_profiler *profiler = new_profiler(device, 8);
    begin_profiling(cmdList, profiler);
    size_t ini = request_timestamp_checkpoint(cmdList, profiler);
#endif

    // run the kernel:
    dispatch_threadgroups(cmdList,
                          blocksX, blocksY, blocksZ,
                          threadsX, threadsY, threadsZ);

#if HALIDE_D3D12_PROFILING
    size_t end = request_timestamp_checkpoint(cmdList, profiler);
    end_profiling(cmdList, profiler);
#endif

    enqueue_frame(frame);

    // broadcast fence signal checkpoint to the buffers being used
    uint64_t checkpoint = frame->fence_signal;
    uniform_buffer.signal = checkpoint;
    for (size_t i = 0; i < num_buffer_args; i++) {
        d3d12_buffer *buffer = buffer_args[i];
        buffer->signal = checkpoint;
    }

#if HALIDE_D3D12_RENDERDOC
    FinishCapturingGPUActivity();
    TRACEPRINT("<<< RenderDoc Capture Ended\n");
#endif

#if HALIDE_D3D12_PROFILING
    uint64_t eps = (uint64_t)get_elapsed_time(profiler, ini, end);
    Printer<BasicPrinter, 64>() << "kernel execution time: " << eps << "us.\n";
    // TODO: keep some live performance stats in the d3d12_function object
    // (accumulate stats based on dispatch similarities -- e.g., blocksX|Y|Z)
    release_object(profiler);
#endif

    return 0;
}

WEAK int halide_d3d12compute_device_and_host_malloc(void *user_context, struct halide_buffer_t *buffer) {
    TRACELOG;
    // NOTE(marcos): it would be nice to have some "zero-copy" behavior here by
    // allocating the device memory and MapBuffering it to the host memory, but
    // sadly, even with a "dedicated" d3d12 staging heap just for this buffer,
    // d3d12 has no bi-directional system memory heap (a sigle resource that is
    // capable of both upload and readback). One possible workaround is to just
    // suballocate from two common heaps (one used for uploads and another for
    // readbacks) and dynamically change buffer->host accordingly. However, if
    // the user ever caches the buffer->host pointer, that would be really bad!
    //
    // Another complicating factor is the fact that the test "cleanup_on_error"
    // expects halide_malloc() to be called here to allocate the host buffer.
    // Arguably, the logic behind the "cleanup_on_error" test is flawed, since
    // back-ends are allowed (and encouraged) to have zero-copy behavior.
    //
    // Since we can't really have proper zero-copy behavior with d3d12 on halide
    // it's best we just defer this routine to the default implementation, which
    // is virtually identical to what we had before here anyway, except that it
    // will actually end up calling halide_malloc() for the host memory.
    return halide_default_device_and_host_malloc(user_context, buffer, &d3d12compute_device_interface);
}

WEAK int halide_d3d12compute_device_and_host_free(void *user_context, struct halide_buffer_t *buffer) {
    TRACELOG;
    // NOTE(marcos): see notes on "halide_d3d12compute_device_and_host_malloc()".
    return halide_default_device_and_host_free(user_context, buffer, &d3d12compute_device_interface);
}

WEAK int halide_d3d12compute_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                         const struct halide_device_interface_t *dst_device_interface,
                                         struct halide_buffer_t *dst) {
    TRACELOG;

    halide_assert(user_context, (src->dimensions == dst->dimensions));
    const int dimensions = dst->dimensions;
    if (dimensions > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU.\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    // We only handle copies to d3d12 device or to host
    halide_assert(user_context, (dst_device_interface == nullptr) ||
                                    (dst_device_interface == &d3d12compute_device_interface));

    if ((src->device_dirty() || src->host == nullptr) &&
        src->device_interface != &d3d12compute_device_interface) {
        halide_assert(user_context, dst_device_interface == &d3d12compute_device_interface);
        // This is handled at the higher level.
        return halide_error_code_incompatible_device_interface;
    }

    bool from_host = (src->device_interface != &d3d12compute_device_interface) ||
                     (src->device == 0) ||
                     (src->host_dirty() && src->host != nullptr);
    bool to_host = !dst_device_interface;

    halide_assert(user_context, from_host || src->device);
    halide_assert(user_context, to_host || dst->device);

    device_copy c = make_buffer_copy(src, from_host, dst, to_host);
    MAYBE_UNUSED(c);

    int err = 0;
    {
        TRACEPRINT(
            "(user_context: " << user_context << ", src: " << src << ", dst: " << dst << ")\n");

        // Device only case
        if (!from_host && !to_host) {
            TRACEPRINT("device-to-device case\n");
            d3d12_buffer *dsrc = peel_buffer(src);
            d3d12_buffer *ddst = peel_buffer(dst);
            size_t src_offset = dsrc->offsetInBytes + c.src_begin;
            size_t dst_offset = ddst->offsetInBytes;
            D3D12ContextHolder d3d12_context(user_context, true);
            if (d3d12_context.error != 0) {
                return d3d12_context.error;
            }
            err = do_multidimensional_copy(d3d12_context.device, c, src_offset, dst_offset, dimensions);
        } else {
            if (from_host) {
                // host-to-device:
                TRACEPRINT("host-to-device case\n");
                halide_assert(user_context, !to_host);
                halide_assert(user_context, (dst->device_interface == &d3d12compute_device_interface));
                halide_assert(user_context, (src->device_interface != &d3d12compute_device_interface));
                halide_assert(user_context, (src->host != nullptr));
                // it's possible for 'dst->host' to be null, so we can't always memcpy from 'src->host'
                // to 'dst-host' and push/sync changes with 'halide_d3d12compute_copy_to_device' ...
                halide_assert(user_context, (dst->device == c.dst));
                if (dst->host != nullptr) {
                    // 1. copy 'src->host' buffer to 'dst->host' buffer:
                    // host buffers already account for the beginning of cropped regions
                    c.dst = reinterpret_cast<uint64_t>(dst->host) + 0;
                    copy_memory(c, user_context);
                    // 2. sync 'dst->host' buffer with 'dst->device' buffer:
                    halide_d3d12compute_copy_to_device(user_context, dst);
                } else {
                    TRACEPRINT("dst->host is nullptr\n");
                    D3D12ContextHolder d3d12_context(user_context, true);
                    if (d3d12_context.error != 0) {
                        return d3d12_context.error;
                    }
                    // 1. memcpy from 'src->host' to upload staging buffer
                    size_t total_size = dst->size_in_bytes();
                    d3d12_buffer *staging = &upload;
                    // host buffers already account for the beginning of cropped regions
                    c.src = reinterpret_cast<uint64_t>(src->host) + 0;
                    size_t staging_byte_offset = suballocate(d3d12_context.device, staging, total_size);
                    void *staging_base = buffer_contents(staging);
                    c.dst = reinterpret_cast<uint64_t>(staging_base) + staging_byte_offset;
                    copy_memory(c, user_context);
                    // 2. d3dcpy from upload staging buffer to 'dst->device' buffer
                    d3d12_buffer *ddst = peel_buffer(dst);
                    size_t dst_byte_offset = ddst->offsetInBytes;  // handle cropping
                    d3d12compute_buffer_copy(d3d12_context.device, staging, ddst,
                                             staging_byte_offset, dst_byte_offset, total_size);
                    uint64_t use_count = __atomic_load_n(&staging->ref_count, __ATOMIC_SEQ_CST);
                    halide_assert(user_context, (use_count == 0));
                }
            } else {
                // device-to-host:
                TRACEPRINT("device-to-host case\n");
                halide_assert(user_context, to_host);
                halide_assert(user_context, (src->device_interface == &d3d12compute_device_interface));
                halide_assert(user_context, (dst->device_interface == nullptr));
                halide_assert(user_context, (dst->host != nullptr));
                // it's possible for 'src->host' to be null, so we can't always pull/sync changes with
                // 'halide_d3d12compute_copy_to_host' and then memcpy from 'src->host' to 'dst-host'...
                halide_assert(user_context, (src->device == c.src));
                if (src->host != nullptr) {
                    // 1. sync 'src->device' buffer with 'src->host' buffer:
                    halide_d3d12compute_copy_to_host(user_context, src);
                    // 2. copy 'src->host' buffer to 'dst->host' buffer:
                    // host buffers already account for the beginning of cropped regions
                    c.src = reinterpret_cast<uint64_t>(src->host) + 0;
                    copy_memory(c, user_context);
                } else {
                    TRACEPRINT("src->host is nullptr\n");
                    D3D12ContextHolder d3d12_context(user_context, true);
                    if (d3d12_context.error != 0) {
                        return d3d12_context.error;
                    }
                    // 1. d3dcpy from 'src->device' buffer to readback staging buffer
                    size_t total_size = src->size_in_bytes();
                    d3d12_buffer *dsrc = peel_buffer(src);
                    size_t src_byte_offset = dsrc->offsetInBytes;  // handle cropping
                    d3d12_buffer *staging = &readback;
                    size_t staging_byte_offset = suballocate(d3d12_context.device, staging, total_size);
                    void *staging_base = buffer_contents(staging);
                    d3d12compute_buffer_copy(d3d12_context.device, dsrc, staging,
                                             src_byte_offset, staging_byte_offset, total_size);
                    // 2. memcpy readback staging buffer to 'dst->host' buffer
                    // host buffers already account for the beginning of cropped regions
                    c.src = reinterpret_cast<uint64_t>(staging_base) + staging_byte_offset;
                    c.dst = reinterpret_cast<uint64_t>(dst->host) + 0;
                    copy_memory(c, user_context);
                    uint64_t use_count = __atomic_load_n(&staging->ref_count, __ATOMIC_SEQ_CST);
                    halide_assert(user_context, (use_count == 0));
                }
            }
        }
    }

    return err;
}

namespace {

WEAK int d3d12compute_device_crop_from_offset(void *user_context,
                                              const struct halide_buffer_t *src,
                                              int64_t offset, /* offset in elements, not in bytes */
                                              struct halide_buffer_t *dst) {
    TRACELOG;

    const d3d12_buffer *old_handle = peel_buffer(src);
    ID3D12Resource *pResource = old_handle->resource;
    uint64_t opaqued = reinterpret_cast<uint64_t>(pResource);

    int ret = halide_d3d12compute_wrap_buffer(user_context, dst, opaqued);
    if (ret != 0) {
        TRACEFATAL("halide_d3d12compute_device_crop: failed when wrapping buffer.");
        return ret;
    }

    d3d12_buffer *new_handle = peel_buffer(dst);
    halide_assert(user_context, (new_handle != nullptr));
    halide_assert(user_context, (new_handle->halide_type == dst->type));
    halide_assert(user_context, (src->device_interface == dst->device_interface));

    new_handle->offset = old_handle->offset + offset;
    new_handle->offsetInBytes = new_handle->offset * dst->type.bytes() * dst->type.lanes;
    // for some reason, 'dst->number_of_elements()' is always returning 1
    // later on when 'set_input()' is called...
    new_handle->elements = old_handle->elements - offset;

    TRACEPRINT(
        "--- "
        << (void *)old_handle << " | "
        << " | "
        << old_handle->offset << " : " << old_handle->elements << " : " << old_handle->sizeInBytes
        << "   ->   "
        << (void *)new_handle << " | "
        << " | "
        << new_handle->offset << " : " << new_handle->elements << " : " << new_handle->sizeInBytes
        << "\n");

    return 0;
}

}  // namespace

WEAK int halide_d3d12compute_device_crop(void *user_context,
                                         const struct halide_buffer_t *src,
                                         struct halide_buffer_t *dst) {
    TRACELOG;
    using namespace Halide::Runtime;
    int64_t offset = Internal::calc_device_crop_byte_offset(src, dst);
    // D3D12 buffer views are element-based, not byte-based
    halide_assert(user_context, (offset % src->type.bytes()) == 0);
    offset /= src->type.bytes();
    return d3d12compute_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_d3d12compute_device_slice(void *user_context,
                                          const struct halide_buffer_t *src,
                                          int slice_dim, int slice_pos,
                                          struct halide_buffer_t *dst) {
    TRACELOG;
    using namespace Halide::Runtime;
    int64_t offset = Internal::calc_device_slice_byte_offset(src, slice_dim, slice_pos);
    // D3D12 buffer views are element-based, not byte-based
    halide_assert(user_context, (offset % src->type.bytes()) == 0);
    offset /= src->type.bytes();
    return d3d12compute_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_d3d12compute_device_release_crop(void *user_context, struct halide_buffer_t *buf) {
    TRACELOG;

    TRACEPRINT("user_context: " << user_context << " | halide_buffer_t: " << buf << "\n");

    d3d12_buffer *dbuffer = peel_buffer(buf);
    TRACEPRINT("d3d12_buffer: " << dbuffer << "\n");

    unwrap_buffer(buf);

    // it is safe to call release_d3d12_object() here because 'buf' is known to
    // be a crop of a larger buffer, and release_d3d12_object() will decrement
    // the reference count of the underlying ID3D12Resource that was incremented
    // when the crop/slice was created.
    release_d3d12_object(dbuffer);

    return 0;
}

WEAK int halide_d3d12compute_detach_buffer(void *user_context, struct halide_buffer_t *buf) {
    TRACELOG;

    TRACEPRINT("user_context: " << user_context << " | halide_buffer_t: " << buf << "\n");

    if (buf->device == 0) {
        return 0;
    }

    unwrap_buffer(buf);

    return 0;
}

WEAK int halide_d3d12compute_wrap_buffer(void *user_context, struct halide_buffer_t *halide_buf, uint64_t d3d12_resource) {
    TRACELOG;

    ID3D12Resource *pResource = reinterpret_cast<ID3D12Resource *>(d3d12_resource);
    halide_assert(user_context, (pResource != nullptr));

    d3d12_buffer sbuffer = {};
    sbuffer.resource = pResource;
    sbuffer.type = d3d12_buffer::ReadWrite;
    sbuffer.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    int ret = wrap_buffer(user_context, halide_buf, &sbuffer);
    if (ret != 0) {
        return ret;
    }

    d3d12_buffer *dbuffer = malloct<d3d12_buffer>();
    if (dbuffer == nullptr) {
        unwrap_buffer(halide_buf);
        TRACEFATAL("halide_d3d12compute_wrap_buffer: malloc failed making device handle.");
        return halide_error_code_out_of_memory;
    }

    *dbuffer = sbuffer;
    dbuffer->mallocd = true;
    __atomic_store_n(&dbuffer->ref_count, 0, __ATOMIC_SEQ_CST);
    halide_buf->device = reinterpret_cast<uint64_t>(dbuffer);

    // increment the reference count of the resource here such that we can then
    // safely call release_d3d12_object() later without actually releasing the
    // user-managed resource object:
    pResource->AddRef();

    return 0;
}

WEAK const struct halide_device_interface_t *halide_d3d12compute_device_interface() {
    TRACELOG;
    return &d3d12compute_device_interface;
}

namespace {
WEAK __attribute__((destructor)) void halide_d3d12compute_cleanup() {
    TRACELOG;
    compilation_cache.release_all(nullptr, release_object<d3d12_library>);
    halide_d3d12compute_device_release(nullptr);
}
}  // namespace

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace D3D12Compute {

WEAK halide_device_interface_impl_t d3d12compute_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_d3d12compute_device_malloc,
    halide_d3d12compute_device_free,
    halide_d3d12compute_device_sync,
    halide_d3d12compute_device_release,
    halide_d3d12compute_copy_to_host,
    halide_d3d12compute_copy_to_device,
    halide_d3d12compute_device_and_host_malloc,
    halide_d3d12compute_device_and_host_free,
    halide_d3d12compute_buffer_copy,
    halide_d3d12compute_device_crop,
    halide_d3d12compute_device_slice,
    halide_d3d12compute_device_release_crop,
    halide_d3d12compute_wrap_buffer,
    halide_d3d12compute_detach_buffer};

WEAK halide_device_interface_t d3d12compute_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_buffer_copy,
    halide_device_crop,
    halide_device_slice,
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    nullptr,
    &d3d12compute_device_interface_impl};

}  // namespace D3D12Compute
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
