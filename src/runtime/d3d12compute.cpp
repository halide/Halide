#ifndef BITS_64
    // Don't emit a message: some environments will consider this as a "warning",
    // and we generally build with warnings-as-errors enabled.
    // #pragma message "The Halide Direct3D 12 back-end is not yet supported on 32bit targets..."
#else  // BITS_64

// Debugging utilities for back-end developers:
#define HALIDE_D3D12_TRACE          (0)
#define HALIDE_D3D12_DEBUG_LAYER    (0)
#define HALIDE_D3D12_DEBUG_SHADERS  (0)
#define HALIDE_D3D12_PROFILING      (0)
#define HALIDE_D3D12_PIX            (0)
#define HALIDE_D3D12_RENDERDOC      (0)

// Halide debug target (Target::Debug, "-debug"):
// force-enable call-trace, d3d12 debug layer and shader debugging information
#ifdef DEBUG_RUNTIME

    #undef  HALIDE_D3D12_TRACE
    #define HALIDE_D3D12_TRACE (1)

    #undef  HALIDE_D3D12_DEBUG_LAYER
    #define HALIDE_D3D12_DEBUG_LAYER (1)

    #undef  HALIDE_D3D12_DEBUG_SHADERS
    #define HALIDE_D3D12_DEBUG_SHADERS (1)

#endif

#include "HalideRuntimeD3D12Compute.h"
#include "scoped_spin_lock.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"

#if !defined(INITGUID)
    #define  INITGUID
#endif
#if !defined(COBJMACROS)
    #define  COBJMACROS
#endif
#include "mini_d3d12.h"

#define HALIDE_D3D12_APPLY_ABI_PATCHES (1)
#include "d3d12_abi_patch_64.h"

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
#undef  HALIDE_D3D12_COMMAND_LIST_TYPE
#define HALIDE_D3D12_COMMAND_LIST_TYPE D3D12_COMMAND_LIST_TYPE_DIRECT
#endif

// A Printer that automatically reserves stack space for the printer buffer:
// (the managed printers in 'printer.h' rely on malloc)
template<uint64_t length = 1024, int type = BasicPrinter>
class StackPrinter : public Printer<type, length> {
public:
    StackPrinter(void *ctx = NULL) : Printer<type, length>(ctx, buffer) { }
    StackPrinter& operator () (void *ctx = NULL) {
        this->user_context = ctx;
        return *this;
    }
    uint64_t capacity() const { return length; }
private:
    char buffer [length];
};

// v trace and logging utilities for debugging v
// ! definitely not super thread-safe stuff... !
// in case there's no 'user_context' available in the scope of a function:
static void *const user_context = NULL;
//
#if HALIDE_D3D12_TRACE
    // it's better to use StackPrinter<> instead of Printer<> (debug(ctx)) when
    // tracing calls because Printer<> calls 'halide_malloc()' which can fail;
    // there's even a test for it (generator_aot_cleanup_on_error), simulating
    // a halide_malloc failure, and user code should not affect debug-tracing
    #define trace(x) StackPrinter<4096, BasicPrinter>(x)

    static volatile int indent_lock = 0;
    static const char indent_pattern [] = "   ";
    static char  indent [2048] = { };
    static int   indent_end   = 0;
    #define TRACEINDENT     ((const char*)indent)
    #define TRACEPRINT(msg) trace(user_context) << TRACEINDENT << msg;
    struct TraceLogScope {
        TraceLogScope() {
            while (__sync_lock_test_and_set(&indent_lock, 1)) {}
            for (const char *p = indent_pattern; *p; ++p) {
                indent[indent_end++] = *p;
            }
            __sync_lock_release(&indent_lock);
        }
        ~TraceLogScope() {
            while (__sync_lock_test_and_set(&indent_lock, 1)) {}
            for (const char *p = indent_pattern; *p; ++p) {
                indent[--indent_end] = '\0';
            }
            TRACEPRINT("^^^\n");
            __sync_lock_release(&indent_lock);
        }
    };
    #define TRACELOG        TRACEPRINT("[@]" << __FUNCTION__ << "\n"); TraceLogScope trace_scope___;
#else
    #define TRACEINDENT ""
    #define TRACEPRINT(msg)
    #define TRACELOG
#endif
//
#define ERRORLOG    error(user_context) << TRACEINDENT
// ^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^

static const char* d3d12_debug_dump();

#define d3d12_halt(...)             \
    ERRORLOG << __VA_ARGS__ << "\n" \
             << d3d12_debug_dump()  \
             << "!!! HALT !!!\n"

void *d3d12_load_library(const char *name) {
    TRACELOG;
    void *lib = halide_load_library(name);
    if (lib) {
        TRACEPRINT("Loaded runtime library '" << name << "' at location " << lib << "\n");
    } else {
        d3d12_halt("Unable to load runtime library: " << name);
    }
    return lib;
}

void *d3d12_get_library_symbol(void *lib, const char *name) {
    TRACELOG;
    void* symbol = halide_get_library_symbol(lib, name);
    if (symbol) {
        TRACEPRINT("Symbol '" << name << "' found at " << symbol << "\n");
    } else {
        d3d12_halt("Symbol not found: " << name);
    }
    return symbol;
}

#define d3d12_debug_break (*((volatile int8_t*)NULL) = 0)

#ifndef MAYBE_UNUSED
#define MAYBE_UNUSED(x) ((void) x)
#endif  //MAYBE_UNUSED

#if HALIDE_D3D12_RENDERDOC
#if HALIDE_D3D12_DEBUG_LAYER
    #pragma message "RenderDoc might not work well alongside Dirct3D debug layers..."
#endif
#define WIN32
#define RenderDocAssert(expr)           halide_assert(user_context, expr)
#define LoadRenderDocLibrary(dll)       d3d12_load_library(dll)
#define GetRenderDocProcAddr(dll,proc)  d3d12_get_library_symbol(dll, proc)
#define RENDERDOC_NO_STDINT
#define RENDERDOC_AUTOINIT              (0)
#include "renderdoc/RenderDocGlue.h"
// RenderDoc can only intercept commands in the graphics queue:
#undef  HALIDE_D3D12_COMMAND_LIST_TYPE
#define HALIDE_D3D12_COMMAND_LIST_TYPE D3D12_COMMAND_LIST_TYPE_DIRECT
#endif

static void *d3d12_malloc(size_t num_bytes) {
    TRACELOG;
    void *p = malloc(num_bytes);
    TRACEPRINT("allocated " << (uintptr_t)num_bytes << " bytes @ " << p <<"\n");
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
    T *p = (T*)d3d12_malloc(sizeof(T));
    return p;
}

template<typename T>
static T zero_struct() {
    T zero = { };
    return zero;
}

#define hashmap_malloc(user_context, size)  d3d12_malloc(size)
#define hashmap_free(user_context, memory)  d3d12_free(memory)
#include "hashmap.h"

template<typename ID3D12T>
static const char *d3d12typename(ID3D12T*) {
    return "UNKNOWN";
}
#define D3D12TYPENAME(T) static const char *d3d12typename(T*) { return #T; }
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
static bool D3DError(HRESULT result, ID3D12T *object, void *user_context, const char *message) {
    // HRESULT ERROR CODES:
    // D3D12: https://msdn.microsoft.com/en-us/library/windows/desktop/bb509553(v=vs.85).aspx
    // Win32: https://msdn.microsoft.com/en-us/library/windows/desktop/aa378137(v=vs.85).aspx
    if (FAILED(result) || !object) {
        d3d12_halt(
            message << " (HRESULT=" << (void*)(int64_t)result
            << ", object*=" << object << ")"
        );
        return true;
    }
    TRACEPRINT(d3d12typename(object) << " object created: " << object << "\n");
    return false;
}

static DXGI_FORMAT FindD3D12FormatForHalideType(halide_type_t type) {
    // DXGI Formats:
    // https://msdn.microsoft.com/en-us/library/windows/desktop/bb173059(v=vs.85).aspx

    // indexing scheme: [code][lane][bits]
    const DXGI_FORMAT FORMATS [3][4][4] =
    {
        // halide_type_int
        {
            // 1 lane
            {
                DXGI_FORMAT_R8_SINT,    //  8 bits
                DXGI_FORMAT_R16_SINT,   // 16 bits
                DXGI_FORMAT_R32_SINT,   // 32 bits
                DXGI_FORMAT_UNKNOWN,    // 64 bits
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
            }
        },
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
            }
        },
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
            }
        },
    };

    halide_assert(user_context, (type.code >= 0) && (type.code <= 2));
    halide_assert(user_context, (type.lanes > 0) && (type.lanes <= 4));

    int i = 0;
    switch (type.bytes()) {
        case 1  : i = 0; break;
        case 2  : i = 1; break;
        case 4  : i = 2; break;
        case 8  : i = 3; break;
        default : halide_assert(user_context, false);  break;
    }

    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    format = FORMATS[(int)type.code][type.lanes-1][i];
    return format;
}



// The default implementation of halide_d3d12_get_symbol attempts to load
// the D3D12 runtime shared library/DLL, and then get the symbol from it.
static void *lib_d3d12  = NULL;
static void *lib_D3DCompiler_47 = NULL;
static void *lib_dxgi = NULL;

struct LibrarySymbol {
    template<typename T>
    operator T () { return (T)symbol; }
    void *symbol;

    static LibrarySymbol get(void *user_context, void *lib, const char *name) {
        void *s = d3d12_get_library_symbol(lib, name);
        LibrarySymbol symbol = { s };
        return symbol;
    }
};

static PFN_D3D12_CREATE_DEVICE              D3D12CreateDevice           = NULL;
static PFN_D3D12_GET_DEBUG_INTERFACE        D3D12GetDebugInterface      = NULL;
static PFN_D3D12_SERIALIZE_ROOT_SIGNATURE   D3D12SerializeRootSignature = NULL;
static PFN_D3DCOMPILE                       D3DCompile                  = NULL;
static PFN_CREATEDXGIFACORY1                CreateDXGIFactory1          = NULL;

#if defined(__cplusplus) && !defined(_MSC_VER)
#if defined(__MINGW32__)
#undef __uuidof
#endif

#define UUIDOF(T) REFIID __uuidof(const T&) { return  IID_ ## T; }

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
    operator ID3D12Type *()    { return reinterpret_cast<ID3D12Type*>(this); }
    ID3D12Type *operator -> () { return reinterpret_cast<ID3D12Type*>(this); }
};

// !!! the d3d12 is managed internally; inherit from this class aggregate data
// to the managed object
template<typename ID3D12Type>
struct halide_d3d12_deep_wrapper {
    ID3D12Type *p;
    operator ID3D12Type*    () { return p; }
    ID3D12Type *operator -> () { return p; }
};

struct halide_d3d12compute_device : public halide_d3d12_wrapper<ID3D12Device> { };
struct halide_d3d12compute_command_queue : public halide_d3d12_wrapper<ID3D12CommandQueue> { };

namespace Halide { namespace Runtime { namespace Internal { namespace D3D12Compute {

typedef halide_d3d12compute_device        d3d12_device;
typedef halide_d3d12compute_command_queue d3d12_command_queue;

struct d3d12_buffer {
    ID3D12Resource *resource;
    UINT sizeInBytes;
    UINT offset;        // FirstElement
    UINT elements;      // NumElements
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
    } *xfer;

    bool mallocd;
    void *host_mirror;

    // if the buffer is an upload/readback staging heap:
    void *mapped;
    volatile uint64_t ref_count /*__attribute__((aligned(8)))*/;

    operator bool() const { return resource != NULL; }
};

struct d3d12_command_allocator     : public halide_d3d12_wrapper<ID3D12CommandAllocator> { };

struct d3d12_graphics_command_list : public halide_d3d12_deep_wrapper<ID3D12GraphicsCommandList> {
    uint64_t signal;
};

// NOTE(marcos): at the moment, D3D12 only exposes one type of command list
// (ID3D12GraphicsCommandList) which can also be used for either "compute"
// or "copy" command streams
typedef d3d12_graphics_command_list d3d12_command_list;
typedef d3d12_graphics_command_list d3d12_compute_command_list;
typedef d3d12_graphics_command_list d3d12_copy_command_list;

struct  d3d12_pipeline_state : public halide_d3d12_wrapper<ID3D12PipelineState> { };
typedef d3d12_pipeline_state d3d12_compute_pipeline_state;

struct d3d12_library {
    THashMap<char*, struct d3d12_function*> cache;
    int source_length;
    char source [1];
};

struct d3d12_function {
    ID3DBlob *shaderBlob;
    ID3D12RootSignature *rootSignature;
};

enum ResourceBindingSlots {
    UAV = 0,
    CBV,
    SRV,
    NumSlots
};

// These are "tier-1" d3d12 device limits (D3D12_RESOURCE_BINDING_TIER_1):
static const uint32_t ResourceBindingLimits [NumSlots] = {
    16, // UAV
    14, // CBV
    25, // SRV (the actual tier-1 limit is 128, but will allow only 25 for now)
    // TODO(marcos): we may consider increasing it to the limit once we have a
    // pool of d3d12_binder objects that are recycled whenever kernels are run
    // (at the moment, a new binder is created/destroyed with every kernel run)
};

struct d3d12_binder {
    ID3D12DescriptorHeap *descriptorHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE CPU [NumSlots];
    D3D12_GPU_DESCRIPTOR_HANDLE GPU [NumSlots];
    UINT descriptorSize;
};

struct d3d12_profiler {
    d3d12_buffer queryResultsBuffer;
    UINT64 tick_frequency;  // in Hz, may vary per command queue
    ID3D12QueryHeap *queryHeap;
    UINT next_free_query;
    UINT max_queries;
};

static size_t number_of_elements(const halide_buffer_t *buffer) {
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

WEAK IDXGIAdapter1 *dxgiAdapter = NULL;
WEAK d3d12_device *device = NULL;
WEAK d3d12_command_queue *queue = NULL;
WEAK ID3D12Fence *queue_fence = NULL;
WEAK volatile uint64_t queue_last_signal /*__attribute__((aligned(8)))*/ = 0;
WEAK ID3D12RootSignature *rootSignature = NULL;


WEAK d3d12_buffer upload   = { };   // staging buffer to transfer data to the device
WEAK d3d12_buffer readback = { };   // staging buffer to retrieve data from the device

template<typename d3d12_T>
static void release_d3d12_object(d3d12_T *obj) {
    TRACELOG;
    d3d12_halt("!!! ATTEMPTING TO RELEASE AN UNKNOWN OBJECT @ " << obj << " !!!");
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
    if (buffer->host_mirror != NULL) {
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

extern WEAK halide_device_interface_t d3d12compute_device_interface;

static d3d12_buffer *peel_buffer(struct halide_buffer_t *hbuffer) {
    TRACELOG;
    halide_assert(user_context, (hbuffer != NULL));
    halide_assert(user_context, (hbuffer->device_interface == &d3d12compute_device_interface));
    d3d12_buffer *dbuffer = reinterpret_cast<d3d12_buffer*>(hbuffer->device);
    halide_assert(user_context, (dbuffer != NULL));
    return dbuffer;
}

static const d3d12_buffer *peel_buffer(const struct halide_buffer_t *hbuffer) {
    return peel_buffer(const_cast<halide_buffer_t*>(hbuffer));
}

WEAK int wrap_buffer(struct halide_buffer_t *hbuffer, d3d12_buffer *dbuffer) {
    halide_assert(user_context, (hbuffer->device == 0));
    if (hbuffer->device != 0) {
        return -2;
    }

    halide_assert(user_context, (dbuffer->resource != NULL));

    dbuffer->offset = 0;
    dbuffer->offsetInBytes = 0;
    dbuffer->sizeInBytes = hbuffer->size_in_bytes();
    dbuffer->elements = number_of_elements(hbuffer);
    dbuffer->format = FindD3D12FormatForHalideType(hbuffer->type);
    if (dbuffer->format == DXGI_FORMAT_UNKNOWN) {
        d3d12_halt("unsupported buffer element type: " << hbuffer->type);
        return -3;
    }

    dbuffer->halide_type = hbuffer->type;
    hbuffer->device = reinterpret_cast<uint64_t>(dbuffer);
    halide_assert(user_context, (hbuffer->device_interface == NULL));
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
    buf->device_interface = NULL;
    buf->device = 0;

    return 0;
}

static void D3D12LoadDependencies(void *user_context) {
    TRACELOG;

    const char *lib_names [] = {
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
        void*& lib = *(lib_handles[i]);
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

    D3D12CreateDevice           = LibrarySymbol::get(user_context, lib_d3d12,           "D3D12CreateDevice");
    D3D12GetDebugInterface      = LibrarySymbol::get(user_context, lib_d3d12,           "D3D12GetDebugInterface");
    D3D12SerializeRootSignature = LibrarySymbol::get(user_context, lib_d3d12,           "D3D12SerializeRootSignature");
    D3DCompile                  = LibrarySymbol::get(user_context, lib_D3DCompiler_47,  "D3DCompile");
    CreateDXGIFactory1          = LibrarySymbol::get(user_context, lib_dxgi,            "CreateDXGIFactory1");

    // Windows x64 follows the LLP64 integer type convention:
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa383751(v=vs.85).aspx
    halide_assert(user_context, sizeof(BOOL)     == ( 32 / 8)); // BOOL      must be  32 bits
    halide_assert(user_context, sizeof(CHAR)     == (  8 / 8)); // CHAR      must be   8 bits
    halide_assert(user_context, sizeof(SHORT)    == ( 16 / 8)); // SHORT     must be  16 bits
    halide_assert(user_context, sizeof(LONG)     == ( 32 / 8)); // LONG      must be  32 bits
    halide_assert(user_context, sizeof(ULONG)    == ( 32 / 8)); // ULONG     must be  32 bits
    halide_assert(user_context, sizeof(LONGLONG) == ( 64 / 8)); // LONGLONG  must be  16 bits
    halide_assert(user_context, sizeof(BYTE)     == (  8 / 8)); // BYTE      must be   8 bits
    halide_assert(user_context, sizeof(WORD)     == ( 16 / 8)); // WORD      must be  16 bits
    halide_assert(user_context, sizeof(DWORD)    == ( 32 / 8)); // DWORD     must be  32 bits
    halide_assert(user_context, sizeof(WCHAR)    == ( 16 / 8)); // WCHAR     must be  16 bits
    halide_assert(user_context, sizeof(INT)      == ( 32 / 8)); // INT       must be  32 bits
    halide_assert(user_context, sizeof(UINT)     == ( 32 / 8)); // UINT      must be  32 bits
    halide_assert(user_context, sizeof(IID)      == (128 / 8)); // COM GUIDs must be 128 bits

    // Paranoid checks (I am not taking any chances...)
    halide_assert(user_context, sizeof(INT8)   == ( 8 / 8));
    halide_assert(user_context, sizeof(INT16)  == (16 / 8));
    halide_assert(user_context, sizeof(INT32)  == (32 / 8));
    halide_assert(user_context, sizeof(INT64)  == (64 / 8));
    halide_assert(user_context, sizeof(UINT8)  == ( 8 / 8));
    halide_assert(user_context, sizeof(UINT16) == (16 / 8));
    halide_assert(user_context, sizeof(UINT32) == (32 / 8));
    halide_assert(user_context, sizeof(UINT64) == (64 / 8));
#ifdef BITS_64
    halide_assert(user_context, sizeof(SIZE_T) == (64 / 8));
#else
    halide_assert(user_context, sizeof(SIZE_T) == (32 / 8));
#endif
}

#if HALIDE_D3D12_PIX
static void D3D12WaitForPix() {
    TRACELOG;
    TRACEPRINT("[[ delay for attaching to PIX... ]]\n");
    volatile uint32_t x = (1 << 31);
    while (--x > 0) { }
}
#endif

static d3d12_device *D3D12CreateSystemDefaultDevice(void *user_context) {
    TRACELOG;

#ifndef BITS_64
    d3d12_halt("Direct3D 12 back-end not yet supported on 32bit targets...");
    return NULL;
#endif

    D3D12LoadDependencies(user_context);

    HRESULT result = E_UNEXPECTED;

#if HALIDE_D3D12_DEBUG_LAYER
    TRACEPRINT("Using Direct3D 12 Debug Layer\n");
    ID3D12Debug *d3d12Debug = NULL;
    result = D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12Debug));
    if (D3DError(result, d3d12Debug, user_context, "Unable to retrieve the debug interface for Direct3D 12")) {
        return NULL;
    }
    d3d12Debug->EnableDebugLayer();
#endif

    IDXGIFactory1 *dxgiFactory = NULL;
    result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    if (D3DError(result, dxgiFactory, user_context, "Unable to create DXGI Factory (IDXGIFactory1)")) {
        return NULL;
    }

    halide_assert(user_context, (dxgiAdapter == NULL));
    for (int i = 0; ; ++i) {
        IDXGIAdapter1 *adapter = NULL;
        HRESULT result = dxgiFactory->EnumAdapters1(i, &adapter);
        if (DXGI_ERROR_NOT_FOUND == result) {
            break;
        }
        if (D3DError(result, adapter, user_context, "Unable to enumerate DXGI adapter (IDXGIAdapter1).")) {
            return NULL;
        }
        DXGI_ADAPTER_DESC1 desc = { };
        if (FAILED(adapter->GetDesc1(&desc))) {
            d3d12_halt("Unable to retrieve information (DXGI_ADAPTER_DESC1) about adapter number #" << i);
            return NULL;
        }
        char Description[128];
        for (int i = 0; i < 128; ++i) {
            Description[i] = desc.Description[i];
        }
        Description[127] = '\0';
        TRACEPRINT("-- Adapter #" << i << ": " << Description << "\n");
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            TRACEPRINT("-- this is a software adapter (skipping)\n");
            Release_ID3D12Object(adapter);
            continue;
        }
        // TODO(marcos): find a strategy to select the best adapter available;
        // unfortunately, most of the adapter capabilities can only be queried
        // after a logical device for it is created...
        // (see: ID3D12Device::CheckFeatureSupport)
        Release_ID3D12Object(dxgiAdapter);
        dxgiAdapter = adapter;
        break;  // <- for now, just pick the first (non-software) adapter
    }

    if (dxgiAdapter == NULL) {
        d3d12_halt("Unable to find a suitable D3D12 Adapter.");
        return NULL;
    }

#if 0
    // NOTE(marcos): ignoring IDXGIOutput setup since this back-end is compute only
    IDXGIOutput *dxgiDisplayOutput = NULL;
    result = dxgiAdapter->EnumOutputs(0, &dxgiDisplayOutput);
    if (D3DError(result, dxgiDisplayOutput, user_context, "Unable to enumerate DXGI outputs for adapter (IDXGIOutput)")) {
        return NULL;
    }
#endif

    ID3D12Device *device = NULL;
    D3D_FEATURE_LEVEL MinimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    result = D3D12CreateDevice(dxgiAdapter, MinimumFeatureLevel, IID_PPV_ARGS(&device));
    if (D3DError(result, device, user_context, "Unable to create the Direct3D 12 device")) {
        return NULL;
    }

    #if 0 & HALIDE_D3D12_PROFILING
    // Notes on NVIDIA GPU Boost:
    // https://developer.nvidia.com/setstablepowerstateexe-%20disabling%20-gpu-boost-windows-10-getting-more-deterministic-timestamp-queries
    // MSDN: "Do not call SetStablePowerState in shipped applications.
    //        This method only works while the machine is in developer mode.
    //        If developer mode is not enabled, then device removal will occur.
    //        (DXGI_ERROR_DEVICE_REMOVED : 0x887a0005)"
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn903835(v=vs.85).aspx
    result = device->SetStablePowerState(TRUE);
    if (D3DError(result, device, user_context, "Unable to activate stable power state")) {
        return NULL;
    }
    #endif

    Release_ID3D12Object(dxgiFactory);

    #if HALIDE_D3D12_PIX
    D3D12WaitForPix();
    #endif

    return reinterpret_cast<d3d12_device*>(device);
}

ID3D12RootSignature *D3D12CreateMasterRootSignature(ID3D12Device *device) {
    TRACELOG;

    // A single "master" root signature is suitable for all Halide kernels:
    // ideally, we would like to use "unbounded tables" for the descriptor
    // binding, but "tier-1" d3d12 devices (D3D12_RESOURCE_BINDING_TIER_1)
    // do not support unbounded descriptor tables...

    D3D12_ROOT_PARAMETER TableTemplate = { };
    {
        TableTemplate.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        TableTemplate.DescriptorTable.NumDescriptorRanges = 1;
        TableTemplate.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;   // compute must use this
    }
    D3D12_DESCRIPTOR_RANGE RangeTemplate = { };
    {
        RangeTemplate.NumDescriptors = -1;      // -1 for unlimited/unbounded tables
        RangeTemplate.BaseShaderRegister = 0;
        RangeTemplate.RegisterSpace = 0;
        RangeTemplate.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    D3D12_ROOT_PARAMETER rootParameterTables [NumSlots] = { };
    D3D12_DESCRIPTOR_RANGE descriptorRanges  [NumSlots] = { };
    {
        // UAVs: read-only, write-only and read-write buffers:
        D3D12_ROOT_PARAMETER& RootTableUAV = rootParameterTables[UAV];
        {
            RootTableUAV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE& UAVs = descriptorRanges[UAV];
            {
                UAVs = RangeTemplate;
                UAVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                UAVs.NumDescriptors = ResourceBindingLimits[UAV];
            }
            RootTableUAV.DescriptorTable.pDescriptorRanges = &UAVs;
        }
        // CBVs: read-only uniform/coherent/broadcast buffers:
        D3D12_ROOT_PARAMETER& RootTableCBV = rootParameterTables[CBV];
        {
            RootTableCBV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE& CBVs = descriptorRanges[CBV];
            {
                CBVs = RangeTemplate;
                CBVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                CBVs.NumDescriptors = ResourceBindingLimits[CBV];
            }
            RootTableCBV.DescriptorTable.pDescriptorRanges = &CBVs;
        }
        // SRVs: textures and read-only buffers:
        D3D12_ROOT_PARAMETER& RootTableSRV = rootParameterTables[SRV];
        {
            RootTableSRV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE& SRVs = descriptorRanges[SRV];
            {
                SRVs = RangeTemplate;
                SRVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                SRVs.NumDescriptors = ResourceBindingLimits[SRV];
            }
            RootTableSRV.DescriptorTable.pDescriptorRanges = &SRVs;
        }
    }

    D3D12_ROOT_SIGNATURE_DESC rsd = { };
    {
        rsd.NumParameters = NumSlots;
        rsd.pParameters = rootParameterTables;
        rsd.NumStaticSamplers = 0;
        rsd.pStaticSamplers = NULL;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    }
    D3D_ROOT_SIGNATURE_VERSION Version = D3D_ROOT_SIGNATURE_VERSION_1;
    ID3DBlob *pSignBlob  = NULL;
    ID3DBlob *pSignError = NULL;
    HRESULT result = D3D12SerializeRootSignature(&rsd, Version, &pSignBlob, &pSignError);
    if (D3DError(result, pSignBlob, NULL, "Unable to serialize the Direct3D 12 root signature")) {
        halide_assert(user_context, pSignError);
        d3d12_halt((const char*)pSignError->GetBufferPointer());
        return NULL;
    }

    ID3D12RootSignature *rootSignature = NULL;
    UINT nodeMask = 0;
    const void *pBlobWithRootSignature = pSignBlob->GetBufferPointer();
    SIZE_T blobLengthInBytes = pSignBlob->GetBufferSize();
    result = device->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, IID_PPV_ARGS(&rootSignature));
    if (D3DError(result, rootSignature, NULL, "Unable to create the Direct3D 12 root signature")) {
        return NULL;
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
        " blocks("  <<  blocks_x << ", " <<  blocks_y << ", " <<  blocks_z << ")"
        " threads(" << threads_x << ", " << threads_y << ", " << threads_z << ")\n"
    );

    (*cmdList)->Dispatch(blocks_x, blocks_y, blocks_z);
}

WEAK d3d12_buffer new_buffer_resource(d3d12_device *device, size_t length, D3D12_HEAP_TYPE heaptype) {
    D3D12_RESOURCE_DESC desc = { };
    {
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;                             // 0 defaults to 64KB alignment, which is mandatory for buffers
        desc.Width = length;
        desc.Height = 1;                                // for buffers, this must always be 1
        desc.DepthOrArraySize = 1;                      // ditto, (1)
        desc.MipLevels = 1;                             // ditto, (1)
        desc.Format = DXGI_FORMAT_UNKNOWN;              // ditto, (DXGI_FORMAT_UNKNOWN)
        desc.SampleDesc.Count = 1;                      // ditto, (1)
        desc.SampleDesc.Quality = 0;                    // ditto, (0)
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;   // ditto, (D3D12_TEXTURE_LAYOUT_ROW_MAJOR)
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    }

    D3D12_HEAP_PROPERTIES heapProps = { };              // CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_...)
    {
        heapProps.Type = heaptype;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 0;                 // 0 is equivalent to 0b0...01 (single adapter)
        heapProps.VisibleNodeMask  = 0;                 // ditto
    }

    D3D12_HEAP_PROPERTIES *pHeapProperties = &heapProps;
    D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    D3D12_RESOURCE_DESC *pDesc = &desc;
    D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_CLEAR_VALUE *pOptimizedClearValue = NULL;     // for buffers, this must be NULL

    switch (heaptype) {
        case D3D12_HEAP_TYPE_UPLOAD :
            // committed resources in UPLOAD heaps must start in and never change from GENERIC_READ state:
            InitialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
            break;
        case D3D12_HEAP_TYPE_READBACK :
            // committed resources in READBACK heaps must start in and never change from COPY_DEST state:
            InitialResourceState = D3D12_RESOURCE_STATE_COPY_DEST;
            break;
        case D3D12_HEAP_TYPE_DEFAULT :
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            InitialResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            break;
        default :
            TRACEPRINT("UNSUPPORTED D3D12 BUFFER HEAP TYPE: " << (int)heaptype << "\n");
            halide_assert(user_context, false);
            break;
    }

    d3d12_buffer buffer = { };
    ID3D12Resource *resource = NULL;
    // A commited resource manages its own private heap:
    HRESULT result = (*device)->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, IID_PPV_ARGS(&resource));
    if (D3DError(result, resource, NULL, "Unable to create the Direct3D 12 buffer")) {
        return buffer;
    }

    buffer.resource = resource;
    buffer.sizeInBytes = length;
    buffer.state = InitialResourceState;
    buffer.type = d3d12_buffer::Unknown;
    buffer.format = DXGI_FORMAT_UNKNOWN;
    buffer.mallocd = false;
    buffer.host_mirror = NULL;
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

// faux type name for logging purposes in D3DError()
struct ID3D12MemoryMappedResourceFAUX;
D3D12TYPENAME(ID3D12MemoryMappedResourceFAUX)

WEAK void *map_buffer(d3d12_buffer *buffer) {
    TRACELOG;

    if (buffer->mapped) {
        return buffer->mapped;
    }

    D3D12_RANGE readRange = { };
    switch (buffer->type) {
        case d3d12_buffer::Constant :
        case d3d12_buffer::Upload   :
            // upload buffers are write-only, so there is no read range
            readRange.Begin = 0;
            readRange.End = 0;
            break;
        case d3d12_buffer::ReadBack :
            // everything in the buffer might be read by the CPU
            // (we could also simply pass pReadRange = NULL to Map(), but that issues a debug-layer warning...)
            readRange.Begin = 0;
            readRange.End = buffer->sizeInBytes;
            break;
        default :
            TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
            halide_assert(user_context, false);
            break;
    }

    TRACEPRINT("[ Begin: " << readRange.Begin << " , End: " << readRange.End << " ]\n");

    // ID3D12Resource::Map never blocks, but will invalidate caches around the read range
    ID3D12Resource *resource = buffer->resource;
    UINT Subresource = 0;   // buffers contain only one subresource (at index 0)
    const D3D12_RANGE *pReadRange = &readRange;
    void *pData = NULL;
    HRESULT result = resource->Map(Subresource, pReadRange, &pData);
    if (D3DError(result, (ID3D12MemoryMappedResourceFAUX*)pData, NULL, "Unable to map Direct3D 12 staging buffer memory")) {
        return NULL;
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

    D3D12_RANGE writtenRange = { };
    switch (buffer->type) {
        case d3d12_buffer::Constant :
        case d3d12_buffer::Upload   :
            writtenRange.Begin = 0;
            writtenRange.End = buffer->sizeInBytes;
            break;
        case d3d12_buffer::ReadBack :
            // host/CPU never writes directly to a ReadBack buffer, it only reads from it
            writtenRange.Begin = 0;
            writtenRange.End = 0;
            break;
        default :
            TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
            halide_assert(user_context, false);
            break;
    }

    TRACEPRINT("[ Begin: " << writtenRange.Begin << " , End: " << writtenRange.End << " ]\n");

    // ID3D12Resource::Unmap will flush caches around the written range
    ID3D12Resource *resource = buffer->resource;
    UINT Subresource = 0;   // buffers contain only one subresource (at index 0)
    const D3D12_RANGE *pWrittenRange = &writtenRange;
    resource->Unmap(Subresource, pWrittenRange);
    if (D3DError(S_OK, (ID3D12MemoryMappedResourceFAUX*)pData, NULL, "Unable to unmap Direct3D 12 staging buffer memory")) {
        return;
    }

    buffer->mapped = NULL;
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
            case d3d12_buffer::Upload :
                halide_assert(user_context, (staging == &upload));
                *staging = new_upload_buffer(device, new_capacity);
                break;
            case d3d12_buffer::ReadBack :
                halide_assert(user_context, (staging == &readback));
                *staging = new_readback_buffer(device, new_capacity);
                break;
            default :
                TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)staging->type << "\n");
                halide_assert(user_context, false);
                break;
        }
    }

    halide_assert(user_context, (staging->sizeInBytes >= num_bytes));
    // ensure there are no pending transfers on this buffer
    uint64_t use_count = __atomic_add_fetch(&staging->ref_count, 1, __ATOMIC_SEQ_CST);
    halide_assert(user_context, (use_count == 1));
    size_t byte_offset = 0; // always zero, for now
    return byte_offset;
}

// faux type name for logging purposes in D3DError()
struct ID3D12CommandQueueTimestampFrequencyFAUX;
D3D12TYPENAME(ID3D12CommandQueueTimestampFrequencyFAUX)

d3d12_profiler *new_profiler(d3d12_device *device, size_t num_queries) {
    TRACELOG;

    UINT64 Frequency = 0;
    {
        HRESULT result = (*queue)->GetTimestampFrequency(&Frequency);
        if (D3DError(result, (ID3D12CommandQueueTimestampFrequencyFAUX*)Frequency, user_context, "Unable to query the timestamp frequency of the command queue")) {
            return NULL;
        }
        TRACEPRINT("tick frequency: " << Frequency << " Hz.\n");
    }

    ID3D12QueryHeap *pQueryHeap = NULL;
    {
        D3D12_QUERY_HEAP_DESC desc = { };
        {
            desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            desc.Count = num_queries;
            desc.NodeMask = 0;
        }
        HRESULT result = (*device)->CreateQueryHeap(&desc, IID_PPV_ARGS(&pQueryHeap));
        if (D3DError(result, pQueryHeap, user_context, "Unable to create timestamp query heap")) {
            return NULL;
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

    return(profiler);
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
    uint64_t *timestamp_array = (uint64_t*)buffer;
    return timestamp_array;
}

double get_elapsed_time(d3d12_profiler *profiler, size_t checkpoint1, size_t checkpoint2, double resolution=1e-6) {
    TRACELOG;
    uint64_t* timestamps = get_profiling_results(profiler);
    uint64_t ts1 = timestamps[checkpoint1];
    uint64_t ts2 = timestamps[checkpoint2];
    TRACEPRINT("ticks : [ " << ts1 << " , " << ts2 << " ]\n");
    uint64_t ticks = ts2 - ts1;
    double frequency = double(profiler->tick_frequency);
    frequency *= resolution;    // default is microsecond resolution (1e-6)
    double eps = ticks / frequency;
    return eps;
}

WEAK d3d12_command_queue *new_command_queue(d3d12_device *device) {
    TRACELOG;

    ID3D12CommandQueue *commandQueue = NULL;
    {
        D3D12_COMMAND_QUEUE_DESC cqDesc = { };
        {
            cqDesc.Type = HALIDE_D3D12_COMMAND_LIST_TYPE;
            cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            cqDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            cqDesc.NodeMask = 0;    // 0, for single GPU operation
        }
        HRESULT result = (*device)->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue));
        if (D3DError(result, commandQueue, NULL, "Unable to create the Direct3D 12 command queue")) {
            return NULL;
        }
    }

    ID3D12Fence *fence = NULL;
    {
        HRESULT result = (*device)->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (D3DError(result, fence, NULL, "Unable to create the Direct3D 12 fence for command queue")) {
            return NULL;
        }
    }

    queue_fence = fence;
    __atomic_store_n(&queue_last_signal, 0, __ATOMIC_SEQ_CST);

    return reinterpret_cast<d3d12_command_queue*>(commandQueue);
}

template<D3D12_COMMAND_LIST_TYPE Type>
static d3d12_command_allocator *new_command_allocator(d3d12_device *device) {
    TRACELOG;
    halide_assert(user_context, device);
    ID3D12CommandAllocator *commandAllocator = NULL;
    HRESULT result = (*device)->CreateCommandAllocator(Type, IID_PPV_ARGS(&commandAllocator));
    if (D3DError(result, commandAllocator, NULL, "Unable to create the Direct3D 12 command allocator")) {
        return NULL;
    }
    return reinterpret_cast<d3d12_command_allocator*>(commandAllocator);
}

template<D3D12_COMMAND_LIST_TYPE Type>
static d3d12_command_list *new_command_list(d3d12_device *device, d3d12_command_allocator *allocator) {
    TRACELOG;
    ID3D12GraphicsCommandList *commandList = NULL;
    UINT nodeMask = 0;
    ID3D12CommandAllocator *pCommandAllocator = (*allocator);
    ID3D12PipelineState *pInitialState = NULL;
    HRESULT result = (*device)->CreateCommandList(nodeMask, Type, pCommandAllocator, pInitialState, IID_PPV_ARGS(&commandList));
    if (D3DError(result, commandList, NULL, "Unable to create the Direct3D 12 command list")) {
        return NULL;
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
    ID3D12PipelineState *pipelineState = NULL;
    D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = { };
    {
        cpsd.pRootSignature = function->rootSignature;
        cpsd.CS.pShaderBytecode = function->shaderBlob->GetBufferPointer();
        cpsd.CS.BytecodeLength  = function->shaderBlob->GetBufferSize();
        cpsd.NodeMask = 0;
        cpsd.CachedPSO.pCachedBlob = NULL;
        cpsd.CachedPSO.CachedBlobSizeInBytes = 0;
        cpsd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    }
    HRESULT result = (*device)->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&pipelineState));
    if (D3DError(result, pipelineState, NULL, "Unable to create the Direct3D 12 pipeline state")) {
        return NULL;
    }
    return reinterpret_cast<d3d12_compute_pipeline_state*>(pipelineState);
}

static void set_compute_pipeline_state(d3d12_compute_command_list *cmdList, d3d12_compute_pipeline_state *pipeline_state, d3d12_function *function, d3d12_binder *binder) {
    TRACELOG;

    ID3D12RootSignature *rootSignature = function->rootSignature;
    (*cmdList)->SetComputeRootSignature(rootSignature);

    ID3D12PipelineState *pipelineState = (*pipeline_state);
    (*cmdList)->SetPipelineState(pipelineState);

    ID3D12DescriptorHeap *heaps[] = { binder->descriptorHeap };
    (*cmdList)->SetDescriptorHeaps(1, heaps);

    Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable((*cmdList), UAV, binder->GPU[UAV]);
    Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable((*cmdList), CBV, binder->GPU[CBV]);
    Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable((*cmdList), SRV, binder->GPU[SRV]);
}

static void end_recording(d3d12_compute_command_list *cmdList) {
    TRACELOG;
    (*cmdList)->Close();
}

static d3d12_binder *new_descriptor_binder(d3d12_device *device) {
    TRACELOG;
    ID3D12DescriptorHeap *descriptorHeap = NULL;
    D3D12_DESCRIPTOR_HEAP_DESC dhd = { };
    {
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dhd.NumDescriptors  = 0;
        dhd.NumDescriptors += ResourceBindingLimits[UAV];
        dhd.NumDescriptors += ResourceBindingLimits[CBV];
        dhd.NumDescriptors += ResourceBindingLimits[SRV];
        dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        dhd.NodeMask = 0;
    }
    HRESULT result = (*device)->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&descriptorHeap));
    if (D3DError(result, descriptorHeap, NULL, "Unable to create the Direct3D 12 descriptor heap")) {
        return NULL;
    }

    UINT descriptorSize = (*device)->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    TRACEPRINT("descriptor handle increment size: " << descriptorSize << "\n");

    d3d12_binder *binder = malloct<d3d12_binder>();
    binder->descriptorHeap = descriptorHeap;
    binder->descriptorSize = descriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE baseCPU = Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptorHeap);
    TRACEPRINT("descriptor heap base for CPU: " << baseCPU.ptr << "\n");
    binder->CPU[UAV].ptr = (baseCPU.ptr += descriptorSize * 0);
    binder->CPU[CBV].ptr = (baseCPU.ptr += descriptorSize * ResourceBindingLimits[UAV]);
    binder->CPU[SRV].ptr = (baseCPU.ptr += descriptorSize * ResourceBindingLimits[CBV]);

    D3D12_GPU_DESCRIPTOR_HANDLE baseGPU = Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptorHeap);
    TRACEPRINT("descriptor heap base for GPU: " << baseGPU.ptr << "\n");
    binder->GPU[UAV].ptr = (baseGPU.ptr += descriptorSize * 0);
    binder->GPU[CBV].ptr = (baseGPU.ptr += descriptorSize * ResourceBindingLimits[UAV]);
    binder->GPU[SRV].ptr = (baseGPU.ptr += descriptorSize * ResourceBindingLimits[CBV]);

    // initialize everything with null descriptors...
    for (uint32_t i = 0; i < ResourceBindingLimits[UAV]; ++i) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC NullDescUAV = { };
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
        hCPU.ptr += i*descriptorSize;
        (*device)->CreateUnorderedAccessView(NULL, NULL, &NullDescUAV, hCPU);
    }
    for (uint32_t i = 0; i < ResourceBindingLimits[CBV]; ++i) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC NullDescCBV = { };
        {
            NullDescCBV.BufferLocation = 0;
            NullDescCBV.SizeInBytes = 0;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE hCPU = binder->CPU[CBV];
        hCPU.ptr += i*descriptorSize;
        Call_ID3D12Device_CreateConstantBufferView((*device), &NullDescCBV, hCPU);
    }
    for (uint32_t i = 0; i < ResourceBindingLimits[SRV]; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC NullDescSRV = { };
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
        hCPU.ptr += i*descriptorSize;
        Call_ID3D12Device_CreateShaderResourceView((*device), NULL, &NullDescSRV, hCPU);
    }

    return binder;
}

static d3d12_library *new_library_with_source(d3d12_device *device, const char *source, size_t source_len) {
    TRACELOG;
    // Unlike Metal, Direct3D 12 does not have the concept of a "shader library"
    // We can emulate the library functionality by caching the source code until
    // the entry point is known since D3DCompile() requires the entry point name
    const int blocksize = sizeof(d3d12_library) + source_len;
    d3d12_library *library = (d3d12_library*)d3d12_malloc(blocksize);
    library->cache.inited = false;
    library->cache.init(NULL);
    library->source_length = source_len;
    for (size_t i = 0; i < source_len; ++i) {
        library->source[i] = source[i];
    }
    library->source[source_len] = '\0';
    return library;
}

static void dump_shader(const char *source, ID3DBlob *compiler_msgs = NULL) {
    const char *message = "<no error message reported>";
    if (compiler_msgs) {
        message = (const char*)compiler_msgs->GetBufferPointer();
    }

    StackPrinter<64*1024> dump;
    dump(user_context) << TRACEINDENT << "D3DCompile(): " << message << "\n";
    dump(user_context) << TRACEINDENT << ">>> HLSL shader source dump <<<\n" << source << "\n";
}

static d3d12_function *new_function_with_name(d3d12_device *device, d3d12_library *library, const char *name, size_t name_len,
                                              int shared_mem_bytes, int threadsX, int threadsY, int threadsZ) {
    TRACELOG;

    // Round shared memory size up to a multiple of 16:
    shared_mem_bytes = (shared_mem_bytes + 0xF) & ~0xF;
    TRACEPRINT("groupshared memory size: " << shared_mem_bytes << " bytes.\n");
    TRACEPRINT("numthreads( " << threadsX << ", " << threadsY << ", " << threadsZ << " )\n");

    // consult the compiled function cache in the library first:
    d3d12_function *function = NULL;
    StackPrinter<256, StringStreamPrinter> key;
    key << name << "_(" << threadsX << "," << threadsY << "," << threadsZ << ")_[" << shared_mem_bytes << "]";
    halide_assert(user_context, (key.size() < key.capacity()-1));    // make sure key fits into the stream
    int not_found = library->cache.lookup(user_context, (const uint8_t*)key.str(), key.size(), &function);
    if (!not_found) {
        halide_assert(user_context, (function != NULL));
        TRACEPRINT("-- function has been found in the cache!\n");
        return function;
    }

    // function has not been cached yet: must compile it
    halide_assert(user_context, (function == NULL));

    const char *source = library->source;
    int source_size = library->source_length;
    StackPrinter<16, StringStreamPrinter> SS [4];
    D3D_SHADER_MACRO pDefines[] = {
        { "__GROUPSHARED_SIZE_IN_BYTES", (SS[0] << shared_mem_bytes).str() },
        { "__NUM_TREADS_X",              (SS[1] << threadsX).str()         },
        { "__NUM_TREADS_Y",              (SS[2] << threadsY).str()         },
        { "__NUM_TREADS_Z",              (SS[3] << threadsZ).str()         },
        { NULL, NULL }
    };
    const char *shaderName = name;  // only used for debug information
    ID3DInclude *includeHandler = NULL;
    const char *entryPoint = name;
    const char *target = "cs_5_1";  // all d3d12 hardware support SM 5.1
    UINT flags1 = 0;
    UINT flags2 = 0;    // flags related to effects (.fx files)
    ID3DBlob *shaderBlob = NULL;
    ID3DBlob *errorMsgs  = NULL;

    flags1 |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#if HALIDE_D3D12_DEBUG_SHADERS
    flags1 |= D3DCOMPILE_DEBUG;
    flags1 |= D3DCOMPILE_SKIP_OPTIMIZATION;
    //flags1 |= D3DCOMPILE_RESOURCES_MAY_ALIAS;
    //flags1 |= D3DCOMPILE_ALL_RESOURCES_BOUND;
#endif

    //dump_shader(source);

    HRESULT result = D3DCompile(source, source_size, shaderName, pDefines, includeHandler, entryPoint, target, flags1, flags2, &shaderBlob, &errorMsgs);

    if (FAILED(result) || (shaderBlob == NULL)) {
        TRACEPRINT("Unable to compile D3D12 compute shader (HRESULT=" << (void*)(int64_t)result << ", ShaderBlob=" << shaderBlob << " entry=" << entryPoint << ").\n");
        dump_shader(source, errorMsgs);
        Release_ID3D12Object(errorMsgs);
        d3d12_halt("[end-of-shader-dump]");
        return NULL;
    }

    TRACEPRINT("SUCCESS while compiling D3D12 compute shader with entry name '" << entryPoint << "'!\n");

    // even though it was successful, there may have been warning messages emitted by the compiler:
    if (errorMsgs != NULL) {
        dump_shader(source, errorMsgs);
        Release_ID3D12Object(errorMsgs);
    }

    function = malloct<d3d12_function>();
    function->shaderBlob = shaderBlob;
    function->rootSignature = rootSignature;
    rootSignature->AddRef();

    // cache the compiled function for future use:
    library->cache.store(user_context, (const uint8_t*)key.str(), key.size(), &function);

    return function;
}

WEAK void set_input_buffer(d3d12_binder *binder, d3d12_buffer *input_buffer, uint32_t index) {
    TRACELOG;

    switch (input_buffer->type) {
        case d3d12_buffer::Constant : {
            TRACEPRINT("CBV" "\n");

            // NOTE(marcos): constant buffers are only used internally by the
            // runtime; users cannot create, control or access them, so it is
            // expected that no halide_buffer_t will be associated with them:
            halide_assert(user_context,  input_buffer->format == DXGI_FORMAT_UNKNOWN);

            ID3D12Resource *pResource = input_buffer->resource;
            D3D12_GPU_VIRTUAL_ADDRESS pGPU = pResource->GetGPUVirtualAddress();

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvd = { };
            {
                cbvd.BufferLocation = pGPU;
                cbvd.SizeInBytes = input_buffer->sizeInBytes;
            }

            halide_assert(user_context, (index < ResourceBindingLimits[CBV]));
            D3D12_CPU_DESCRIPTOR_HANDLE hDescCBV = binder->CPU[CBV];
            binder->CPU[CBV].ptr += binder->descriptorSize;

            Call_ID3D12Device_CreateConstantBufferView((*device), &cbvd, hDescCBV);

            break;
        }

        case d3d12_buffer::ReadOnly  :
            // TODO(marcos): read-only buffers should ideally be bound as SRV,
            // but Halide buffers (halide_buffer_t) do not distinguish between
            // read-only and read-write / write-only buffers... for the moment,
            // just bind read-only buffers with UAV descriptors:
        case d3d12_buffer::ReadWrite :
        case d3d12_buffer::WriteOnly : {
            TRACEPRINT("UAV" "\n");

            DXGI_FORMAT Format = input_buffer->format;
            if (Format == DXGI_FORMAT_UNKNOWN) {
                d3d12_halt("unsupported buffer element type: " << input_buffer->halide_type);
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

            TRACEPRINT("--- [" << index << "] : "
                << (void*)input_buffer << " | " << " | "
                << FirstElement << " : " << NumElements << " : " << SizeInBytes
                << "\n");

            // A View of a non-Structured Buffer cannot be created using a NULL Desc.
            // Default Desc parameters cannot be used, as a Format must be supplied.
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = { };
            {
                uavd.Format = Format;
                uavd.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uavd.Buffer.FirstElement = FirstElement;
                uavd.Buffer.NumElements = NumElements;
                uavd.Buffer.StructureByteStride = 0;
                uavd.Buffer.CounterOffsetInBytes = 0;   // 0, since this is not an atomic counter
                uavd.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            }

            halide_assert(user_context, (index < ResourceBindingLimits[UAV]));
            D3D12_CPU_DESCRIPTOR_HANDLE hDescUAV = binder->CPU[UAV];
            binder->CPU[UAV].ptr += binder->descriptorSize;

            ID3D12Resource *pResource = input_buffer->resource;
            ID3D12Resource *pCounterResource = NULL;    // for atomic counters

            (*device)->CreateUnorderedAccessView(pResource, pCounterResource, &uavd, hDescUAV);

            break;
        }

        case d3d12_buffer::Unknown  :
        case d3d12_buffer::Upload   :
        case d3d12_buffer::ReadBack :
        default :
            TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)input_buffer->type << "\n");
            halide_assert(user_context, false);
            break;
    }
}

static void commit_command_list(d3d12_compute_command_list *cmdList) {
    TRACELOG;
    end_recording(cmdList);
    ID3D12CommandList *lists[] = { (*cmdList) };
    (*queue)->ExecuteCommandLists(1, lists);
    cmdList->signal = __atomic_add_fetch(&queue_last_signal, 1, __ATOMIC_SEQ_CST); // ++last_signal
    (*queue)->Signal(queue_fence, cmdList->signal);
}

static void wait_until_completed(d3d12_compute_command_list *cmdList) {
    TRACELOG;

    // TODO(marcos): perhaps replace the busy-wait loop below by a blocking wait event?
    // HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    // queue->fence->SetEventOnCompletion(cmdList->signal, hEvent);
    // WaitForSingleObject(hEvent, INFINITE);
    // CloseHandle(hEvent);

    HRESULT result_before = (*device)->GetDeviceRemovedReason();

    while (queue_fence->GetCompletedValue() < cmdList->signal) { }

    HRESULT result_after = (*device)->GetDeviceRemovedReason();
    if (FAILED(result_after)) {
        d3d12_halt(
            "Device Lost! GetDeviceRemovedReason(): "
            << "before: " << (void*)(int64_t)result_before << " | "
            << "after: "  << (void*)(int64_t)result_after
        );
    }
}

static void *buffer_contents(d3d12_buffer *buffer) {
    TRACELOG;

    void *pData = NULL;

    switch (buffer->type) {

        case d3d12_buffer::ReadOnly :
            pData = buffer_contents(&readback);
            break;

        case d3d12_buffer::WriteOnly :
            pData = buffer_contents(&upload);
            break;

        case d3d12_buffer::Constant :
        case d3d12_buffer::Upload   :
            pData = buffer->mapped;
            break;

        case d3d12_buffer::ReadBack :
            // on readback heaps, map/unmap as needed, since the results are only effectively
            // published after a Map() call, and should ideally be in an unmapped state prior
            // to the CopyBufferRegion() call
            pData = map_buffer(&readback);
            break;

        /*
        // TODO(marcos): this is probably how these buffer types should be handled:
        case d3d12_buffer::ReadOnly  :
        case d3d12_buffer::WriteOnly :
        case d3d12_buffer::ReadWrite :
        {
            D3D12ContextHolder d3d12_context(user_context, true);
            if (d3d12_context.error != 0) {
                return NULL;
            }

            // 1. download data from device (copy to the "readback" staging memory):
            size_t total_size = buffer->sizeInBytes;
            size_t dev_byte_offset = buffer->offsetInBytes;  // handle cropping
            d3d12_buffer *staging = &readback;
            size_t staging_byte_offset = suballocate(d3d12_context.device, staging, total_size);
            d3d12compute_buffer_copy(d3d12_context.device, buffer,          staging,
                                                           dev_byte_offset, staging_byte_offset, total_size);
            void *staging_data_begin = map_buffer(staging);
            uint64_t address = reinterpret_cast<uint64_t>(staging_data_begin) + staging_byte_offset;
            pData = reinterpret_cast<void*>(address);
            break;
        }
        */

        case d3d12_buffer::Unknown :
        case d3d12_buffer::ReadWrite :
        default:
            TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
            halide_assert(user_context, false);
            break;
    }

    halide_assert(user_context, pData);

    return pData;
}

volatile int WEAK thread_lock = 0;

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    d3d12_library *library;
    module_state *next;
};
WEAK module_state *state_list = NULL;

}}}}

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::D3D12Compute;

extern "C" {

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

    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) { }

#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif

    if (create && (device == NULL)) {
        device = D3D12CreateSystemDefaultDevice(user_context);
        if (device == NULL) {
            __sync_lock_release(&thread_lock);
            return -1;
        }

        halide_assert(user_context, (rootSignature == NULL));
        rootSignature = D3D12CreateMasterRootSignature((*device));
        if (rootSignature == NULL) {
            release_object(device);
            device = NULL;
            __sync_lock_release(&thread_lock);
            return -1;
        }

        halide_assert(user_context, (queue == NULL));
        queue = new_command_queue(device);
        if (queue == NULL) {
            Release_ID3D12Object(rootSignature);
            release_object(device);
            device = NULL;
            __sync_lock_release(&thread_lock);
            return -1;
        }

        // NOTE(marcos): a small amount of hard-coded staging buffer storage is
        // sufficient to get started as suballocations will grow them as needed
        size_t heap_size = 4 * 1024 * 1024;
        upload = new_upload_buffer(device, heap_size);
        readback = new_readback_buffer(device, heap_size);
    }

    // If the device has already been initialized,
    // ensure the queue has as well.
    halide_assert(user_context, (device == 0) || (queue != 0));

    *device_ret = device;
    *queue_ret  = queue;
    return 0;
}

WEAK int halide_d3d12compute_release_context(void *user_context) {
    TRACELOG;
    __sync_lock_release(&thread_lock);
    return 0;
}

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal { namespace D3D12Compute {

class D3D12ContextHolder {
    void *user_context;

    // Define these out-of-line as WEAK, to avoid LLVM error "MachO doesn't support COMDATs"
    void save(void *user_context, bool create);
    void restore();

public:
    d3d12_device *device;
    d3d12_command_queue *queue;
    int error;

    __attribute__((always_inline)) D3D12ContextHolder(void *user_context, bool create) { save(user_context, create); }
    __attribute__((always_inline)) ~D3D12ContextHolder() { restore(); }
};

WEAK void D3D12ContextHolder::save(void *user_context_arg, bool create) {
    user_context = user_context_arg;
    error = halide_d3d12compute_acquire_context(user_context, &device, &queue, create);
}

WEAK void D3D12ContextHolder::restore() {
    halide_d3d12compute_release_context(user_context);
}

}}}} // namespace Halide::Runtime::Internal::D3D12Compute

static const char* d3d12_debug_dump() {
    TRACELOG;

    if (!device) {
        return "debug info not available: no device.";
    }

    halide_assert(user_context, (dxgiAdapter != NULL));
    DXGI_ADAPTER_DESC1 desc = { };
    if (FAILED(dxgiAdapter->GetDesc1(&desc))) {
        return "Unable to retrieve information about the adapter.\n";
    }

    // NOTE(marcos): this printer will leak, but that's fine since debug dump
    // is a panic mechanism that precedes an operational "halt":
    void* dump_buffer = d3d12_malloc(64*1024);
    if (!dump_buffer) {
        return "Unable to allocate memory for the debug dump.\n";
    }
    Printer<StringStreamPrinter, 64*1024> dump (user_context, (char*)dump_buffer);

    dump << "\n===== Debug Dump =====\n";

    // simple conversion from Windows 16bit wchar to char:
    char Description[128];
    for (int i = 0; i < 128; ++i) {
        Description[i] = desc.Description[i];
    }
    Description[127] = '\0';

    dump << "D3D12 Adapter: " << Description << "\n";

    return dump.str();
}

using namespace Halide::Runtime::Internal::D3D12Compute;

extern "C" {


WEAK int halide_d3d12compute_device_malloc(void *user_context, halide_buffer_t *buf) {
    TRACELOG;

    TRACEPRINT("(user_context: " << user_context << ", buf: " << buf << ")\n");

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);
    if (buf->device) {
        // This buffer already has a device allocation
        return 0;
    }

    // Check all strides positive
    for (int i = 0; i < buf->dimensions; i++) {
        halide_assert(user_context, buf->dim[i].stride > 0);
    }

    TRACEPRINT("allocating " << *buf << "\n");

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    d3d12_buffer *d3d12_buf = new_buffer(d3d12_context.device, size);
    if (d3d12_buf == 0) {
        d3d12_halt("D3D12: Failed to allocate buffer of size " << (int64_t)size);
        return -1;
    }

    if (0 != wrap_buffer(buf, d3d12_buf)) {
        d3d12_halt("D3D12: unable to wrap halide buffer and D3D12 buffer.");
        return -1;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    TRACEPRINT("Time: " << (t_after - t_before) / 1.0e6 << " ms\n");
    #endif

    return 0;
}

WEAK int halide_d3d12compute_device_free(void *user_context, halide_buffer_t *buf) {
    TRACELOG;

    TRACEPRINT("buf " << buf << " device is " << buf->device << "\n");

    if (buf->device == 0) {
        return 0;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_d3d12compute_detach_buffer(user_context, buf);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    TRACEPRINT("Time: " << (t_after - t_before) / 1.0e6 << " ms\n");
    #endif

    return 0;
}

WEAK int halide_d3d12compute_initialize_kernels(void *user_context, void **state_ptr, const char *source, int source_size) {
    TRACELOG;

    // Create the state object if necessary. This only happens once, regardless
    // of how many times halide_initialize_kernels/halide_release is called.
    // halide_release traverses this list and releases the module objects, but
    // it does not modify the list nodes created/inserted here.
    module_state *&state = *(module_state**)state_ptr;
    if (!state) {
        state = malloct<module_state>();
        state->library = NULL;
        state->next = state_list;
        state_list = state;
    }

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    if (state->library == NULL) {
        #ifdef DEBUG_RUNTIME
        uint64_t t_before_compile = halide_current_time_ns(user_context);
        #endif

        state->library = new_library_with_source(d3d12_context.device, source, source_size);
        if (state->library == 0) {
            d3d12_halt("D3D12Compute: new_library_with_source failed.");
            return -1;
        }

        #ifdef DEBUG_RUNTIME
        uint64_t t_after_compile = halide_current_time_ns(user_context);
        TRACEPRINT("Time for halide_d3d12compute_initialize_kernels compilation: " << (t_after_compile - t_before_compile) / 1.0e6 << " ms\n");
        #endif
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    TRACEPRINT("Time for halide_d3d12compute_initialize_kernels: " << (t_after - t_before) / 1.0e6 << " ms\n");
    #endif

    return 0;
}

namespace {

static void did_modify_range(d3d12_buffer *buffer, size_t offset, size_t length) {
    TRACELOG;

    d3d12_buffer::transfer_t *xfer = buffer->xfer;
    halide_assert(user_context, (xfer != NULL));
    xfer->offset = offset;
    xfer->size   = length;
}

static void buffer_copy_command(d3d12_copy_command_list *cmdList,
                                d3d12_buffer *src, d3d12_buffer *dst,
                                uint64_t src_byte_offset, uint64_t dst_byte_offset,
                                uint64_t num_bytes_copy) {
    TRACELOG;

    ID3D12Resource *pSrcBuffer = src->resource;
    ID3D12Resource *pDstBuffer = dst->resource;

    D3D12_RESOURCE_BARRIER src_barrier = { };
    {
        src_barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        src_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        src_barrier.Transition.pResource   = pSrcBuffer;
        src_barrier.Transition.Subresource = 0;
        src_barrier.Transition.StateBefore = src->state;
        src_barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (src->state == D3D12_RESOURCE_STATE_GENERIC_READ) {
            halide_assert(user_context, src->type == d3d12_buffer::Upload);
            src_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
        } else {
            halide_assert(user_context, src->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    D3D12_RESOURCE_BARRIER dst_barrier = { };
    {
        dst_barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        dst_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        dst_barrier.Transition.pResource   = pDstBuffer;
        dst_barrier.Transition.Subresource = 0;
        dst_barrier.Transition.StateBefore = dst->state;
        dst_barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        if (dst->state == D3D12_RESOURCE_STATE_COPY_DEST) {
            halide_assert(user_context, dst->type == d3d12_buffer::ReadBack);
        } else {
            halide_assert(user_context, dst->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    if (src_barrier.Transition.StateBefore != src_barrier.Transition.StateAfter)
        (*cmdList)->ResourceBarrier(1, &src_barrier);
    if (dst_barrier.Transition.StateBefore != dst_barrier.Transition.StateAfter)
        (*cmdList)->ResourceBarrier(1, &dst_barrier);

    UINT64 SrcOffset = src_byte_offset;
    UINT64 DstOffset = dst_byte_offset;
    UINT64 NumBytes  = num_bytes_copy;

    (*cmdList)->CopyBufferRegion(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes);

    swap(src_barrier.Transition.StateBefore, src_barrier.Transition.StateAfter);  // restore resource state
    swap(dst_barrier.Transition.StateBefore, dst_barrier.Transition.StateAfter);  // restore resource state

    if (src_barrier.Transition.StateBefore != src_barrier.Transition.StateAfter)
        (*cmdList)->ResourceBarrier(1, &src_barrier);
    if (dst_barrier.Transition.StateBefore != dst_barrier.Transition.StateAfter)
        (*cmdList)->ResourceBarrier(1, &dst_barrier);
}

static void synchronize_host_and_device_buffer_contents(d3d12_copy_command_list *cmdList, d3d12_buffer *buffer) {
    TRACELOG;

    d3d12_buffer::transfer_t *xfer = buffer->xfer;
    halide_assert(user_context, (xfer != NULL));

    d3d12_buffer* src = NULL;
    d3d12_buffer *dst = NULL;
    uint64_t src_byte_offset = 0;
    uint64_t dst_byte_offset = 0;
    uint64_t num_bytes_copy  = xfer->size;

    d3d12_buffer *staging = xfer->staging;
    switch (staging->type) {
        case d3d12_buffer::Upload :
            src = staging;
            dst = buffer;
            src_byte_offset = xfer->offset;
            dst_byte_offset = buffer->offsetInBytes;
            break;
        case d3d12_buffer::ReadBack :
            unmap_buffer(staging);
            src = buffer;
            dst = staging;
            src_byte_offset = buffer->offsetInBytes;
            dst_byte_offset = xfer->offset;
            break;
        default :
            TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
            halide_assert(user_context, false);
            break;
    }

    TRACEPRINT("--- "
        << (void*)buffer << " | " << buffer->halide_type << " | "
        << src_byte_offset << " : " << dst_byte_offset << " : " << num_bytes_copy
        << "\n");

    buffer_copy_command(cmdList, src, dst, src_byte_offset, dst_byte_offset, num_bytes_copy);
}

static void compute_barrier(d3d12_copy_command_list *cmdList, d3d12_buffer *buffer) {
    TRACELOG;

    D3D12_RESOURCE_BARRIER barrier = { };
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = buffer->resource;
    }

    (*cmdList)->ResourceBarrier(1, &barrier);
}

static bool is_buffer_managed(d3d12_buffer *buffer) {
    return buffer->xfer != NULL;
}

static void d3d12compute_device_sync_internal(d3d12_device *device, d3d12_buffer *dev_buffer) {
    TRACELOG;

    // NOTE(marcos): a copy/dma command list would be ideal here, but it would
    // also require a dedicated copy command queue to submit it... for now just
    // use the main compute queue and issue copies via compute command lists.
    //static const D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_COPY;

    static const D3D12_COMMAND_LIST_TYPE Type = HALIDE_D3D12_COMMAND_LIST_TYPE;
    d3d12_command_allocator *sync_command_allocator = new_command_allocator<Type>(device);
    d3d12_compute_command_list *blitCmdList = new_command_list<Type>(device, sync_command_allocator);
    if (dev_buffer != NULL) {
        if (is_buffer_managed(dev_buffer)) {
            synchronize_host_and_device_buffer_contents(blitCmdList, dev_buffer);
        }
    }
    commit_command_list(blitCmdList);
    wait_until_completed(blitCmdList);

    if (dev_buffer != NULL) {
        if (dev_buffer->xfer != NULL) {
            // for now, we expect to have been the only one with pending transfer on the staging buffer:
            d3d12_buffer *staging_buffer = dev_buffer->xfer->staging;
            uint64_t use_count = __atomic_sub_fetch(&staging_buffer->ref_count, 1, __ATOMIC_SEQ_CST);
            halide_assert(user_context, (use_count == 0));
            dev_buffer->xfer = NULL;
        }
    }

    release_object(blitCmdList);
    release_object(sync_command_allocator);
}

static void halide_d3d12compute_device_sync_internal(d3d12_device *device, struct halide_buffer_t *buffer) {
    TRACELOG;
    d3d12_buffer *dbuffer = peel_buffer(buffer);
    d3d12compute_device_sync_internal(device, dbuffer);
}

}

WEAK int halide_d3d12compute_device_sync(void *user_context, struct halide_buffer_t *buffer) {
    TRACELOG;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    halide_d3d12compute_device_sync_internal(d3d12_context.device, buffer);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    TRACEPRINT("Time for halide_d3d12compute_device_sync: " << (t_after - t_before) / 1.0e6 << " ms\n");
    #endif

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
        d3d12compute_device_sync_internal(device, NULL);

        // Unload the modules attached to this device. Note that the list
        // nodes themselves are not freed, only the program objects are
        // released. Subsequent calls to halide_init_kernels might re-create
        // the program object using the same list node to store the program
        // object.
        module_state *state = state_list;
        while (state) {
          if (state->library) {
                release_object(state->library);
                state->library = NULL;
            }
            state = state->next;
        }

        // Release the device itself, if we created it.
        if (acquired_device == device) {
            release_object(&upload);
            release_object(&readback);
            d3d12_buffer empty = { };
            upload = readback = empty;

            Release_ID3D12Object(rootSignature);
            rootSignature = NULL;

            release_object(queue);
            queue = NULL;

            release_object(device);
            device = NULL;
        }
    }

    halide_d3d12compute_release_context(user_context);

    return 0;
}

namespace {

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
        halide_assert(user_context, (dst->xfer == NULL));
        // TODO: assert that offsets and sizes are within bounds
        d3d12_buffer::transfer_t xfer = { };
            xfer.staging = src;
            xfer.offset  = 0;
            xfer.size    = 0;
        dst->xfer = &xfer;
        did_modify_range(dst, src_byte_offset, num_bytes);
        d3d12compute_device_sync_internal(device, dst);
        return 0;
    }

    if (dst->type == d3d12_buffer::ReadBack) {
        // device-to-host via staging buffer:
        halide_assert(user_context, (src->type != d3d12_buffer::Upload));
        halide_assert(user_context, (src->type != d3d12_buffer::ReadBack));
        halide_assert(user_context, (dst->xfer == NULL));
        // TODO: assert that offsets and sizes are within bounds
        d3d12_buffer::transfer_t xfer = { };
            xfer.staging = dst;
            xfer.offset  = 0;
            xfer.size    = 0;
        src->xfer = &xfer;
        // issue copy command from device to staging memory
        did_modify_range(src, dst_byte_offset, num_bytes);
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

    // TODO(marcos): this command list allocation is overkill: needs refactoring

    static const D3D12_COMMAND_LIST_TYPE Type = HALIDE_D3D12_COMMAND_LIST_TYPE;
    d3d12_command_allocator *sync_command_allocator = new_command_allocator<Type>(device);
    d3d12_compute_command_list *blitCmdList = new_command_list<Type>(device, sync_command_allocator);

    buffer_copy_command(blitCmdList, src, dst, src_byte_offset, dst_byte_offset, num_bytes);

    commit_command_list(blitCmdList);
    wait_until_completed(blitCmdList);

    release_object(blitCmdList);
    release_object(sync_command_allocator);

    return 0;
}

static int do_multidimensional_copy(d3d12_device *device, const device_copy &c,
                                    uint64_t src_offset,  uint64_t dst_offset,  int dimensions) {
    if (dimensions == 0) {
        d3d12_buffer *dsrc = reinterpret_cast<d3d12_buffer*>(c.src);
        d3d12_buffer *ddst = reinterpret_cast<d3d12_buffer*>(c.dst);
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
    for (uint64_t i = 0; i < c.extent[d-1]; i++) {
        src_offset += src_off;
        dst_offset += dst_off;
        int err = do_multidimensional_copy(device, c, src_offset, dst_offset, d-1);
        if (err) {
            return err;
        }
        dst_off = c.dst_stride_bytes[d-1];
        src_off = c.src_stride_bytes[d-1];
    }

    return 0;
}

}  // namespace

WEAK int halide_d3d12compute_copy_to_device(void *user_context, halide_buffer_t *buffer) {
    TRACELOG;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buffer);
    halide_assert(user_context, buffer->host && buffer->device);

    D3D12ContextHolder d3d12_context (user_context, true);
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
    size_t dev_byte_offset = dev_buffer->offsetInBytes; // handle cropping
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
        reinterpret_cast<void*>(c.dst),
        reinterpret_cast<void*>(c.src),
        total_size
    );

    // 2. upload data to device (through the "upload" staging memory):
    d3d12compute_buffer_copy(d3d12_context.device, staging,             dev_buffer,
                                                   staging_byte_offset, dev_byte_offset, total_size);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    TRACEPRINT("Time: " << (t_after - t_before) / 1.0e6 << " ms\n");
    #endif

    return 0;
}

WEAK int halide_d3d12compute_copy_to_host(void *user_context, halide_buffer_t *buffer) {
    TRACELOG;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buffer);
    halide_assert(user_context, buffer->host && buffer->device);
    if (buffer->dimensions > MAX_COPY_DIMS) {
        halide_assert(user_context, false);
        return -1;
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
    size_t dev_byte_offset = dev_buffer->offsetInBytes; // handle cropping
    size_t staging_byte_offset = suballocate(d3d12_context.device, staging, total_size);
    d3d12compute_buffer_copy(d3d12_context.device, dev_buffer,      staging,
                                                   dev_byte_offset, staging_byte_offset, total_size);

    // 2. memcpy from "readback" staging memory to halide host memory
    device_copy c = make_device_to_host_copy(buffer);
    void *staging_data = buffer_contents(staging);
    c.src = reinterpret_cast<uint64_t>(staging_data) + staging_byte_offset;
    // the 'host' buffer already points to the beginning of the cropped region
    c.dst = reinterpret_cast<uint64_t>(buffer->host) + 0;
    copy_memory(c, user_context);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    TRACEPRINT("Time: " << (t_after - t_before) / 1.0e6 << " ms\n");
    #endif

    return 0;
}

WEAK int halide_d3d12compute_run(void *user_context,
                                 void *state_ptr,
                                 const char *entry_name,
                                 int blocksX,  int blocksY,  int blocksZ,
                                 int threadsX, int threadsY, int threadsZ,
                                 int shared_mem_bytes,
                                 size_t arg_sizes[],
                                 void *args[],
                                 int8_t arg_is_buffer[],
                                 int num_attributes,
                                 float *vertex_buffer,
                                 int num_coords_dim0,
                                 int num_coords_dim1) {
    TRACELOG;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    d3d12_device *device = d3d12_context.device;

    #if HALIDE_D3D12_RENDERDOC
    TRACEPRINT(">>> RenderDoc Capture Start\n");
    StartCapturingGPUActivity();
    #endif

    d3d12_command_allocator *command_allocator = new_command_allocator<HALIDE_D3D12_COMMAND_LIST_TYPE>(device);
    if (command_allocator == 0) {
        d3d12_halt("D3D12Compute: Could not create compute command allocator.");
        return -1;
    }

    d3d12_compute_command_list *cmdList = new_compute_command_list(device, command_allocator);
    if (cmdList == 0) {
        d3d12_halt("D3D12Compute: Could not create compute command list.");
        return -1;
    }

    halide_assert(user_context, state_ptr);
    module_state *state = (module_state*)state_ptr;

    d3d12_function *function = new_function_with_name(device, state->library, entry_name, strlen(entry_name),
                                                      shared_mem_bytes, threadsX, threadsY, threadsZ);
    halide_assert(user_context, function);

    // prepare buffer resource binding:
    d3d12_binder *binder = new_descriptor_binder(device);
    d3d12_compute_pipeline_state *pipeline_state = new_compute_pipeline_state_with_function(d3d12_context.device, function);
    if (pipeline_state == 0) {
        d3d12_halt("D3D12Compute: Could not allocate pipeline state.");
        release_object(function);
        return -1;
    }
    set_compute_pipeline_state(cmdList, pipeline_state, function, binder);

    // pack all non-buffer arguments into a single "constant" allocation block:
    size_t total_args_size = 0;
    for (size_t i = 0; arg_sizes[i] != 0; i++) {
        if (arg_is_buffer[i]) {
            continue;
        }
        // Here, it's safe to mimic the Metal back-end behavior which enforces
        // natural alignment for all types in structures: each arg_sizes[i] has
        // to be a power-of-two and have the subsequent field start on the next
        // multiple of that power-of-two.
        halide_assert(user_context, (arg_sizes[i] & (arg_sizes[i] - 1)) == 0);
        // We can ignore vector arguments since they never show up in constant
        // blocks. Having to worry about scalar parameters only is convenient
        // since in HLSL SM 5.1 all scalar types are 32bit:
        halide_assert(user_context, arg_sizes[i] <= 4);
        size_t argsize = 4;     // force argument to 32bit
        total_args_size = (total_args_size + argsize - 1) & ~(argsize - 1);
        total_args_size += argsize;
    }
    d3d12_buffer args_buffer = { };
    if (total_args_size > 0) {
        // Direct3D 12 expects constant buffers to have sizes multiple of 256:
        size_t constant_buffer_size = (total_args_size + 255) & ~255;
        args_buffer = new_constant_buffer(d3d12_context.device, constant_buffer_size);
        if (!args_buffer) {
            d3d12_halt("D3D12Compute: Could not allocate arguments buffer.");
            release_object(function);
            return -1;
        }
        uint8_t *args_ptr = (uint8_t*)buffer_contents(&args_buffer);
        size_t offset = 0;
        for (size_t i = 0; arg_sizes[i] != 0; i++) {
            if (arg_is_buffer[i]) {
                continue;
            }
            halide_assert(user_context, arg_sizes[i] <= 4);
            union {
                void     *p;
                float    *f;
                uint8_t  *b;
                uint16_t *s;
                uint32_t *i;
            } arg;
            arg.p = args[i];
            size_t argsize = 4;
            uint32_t val = 0;
            switch (arg_sizes[i]) {
                case 1 : val = *arg.b; break;
                case 2 : val = *arg.s; break;
                case 4 : val = *arg.i; break;
                default: halide_assert(user_context, false); break;
            }
            memcpy(&args_ptr[offset], &val, argsize);
            offset = (offset + argsize - 1) & ~(argsize - 1);
            offset += argsize;
            TRACEPRINT(
                ">>> arg " << (int)i << " has size " << (int)arg_sizes[i] << " : "
                "float("  << *arg.f << ") or "
                "uint32(" << *arg.i << ") or "
                "int32("  << (int32_t&)*arg.i << ")\n"
            );
        }
        halide_assert(user_context, offset == total_args_size);
    }

    // setup/bind the argument buffer:
    if (args_buffer) {
        // always bind argument buffer at constant buffer binding 0
        int32_t cb_index = 0;   // a.k.a. register(c0)
        set_input_buffer(binder, &args_buffer, cb_index);
    }

    // setup/bind actual buffers:
    int32_t uav_index = 0;
    for (size_t i = 0; arg_sizes[i] != 0; i++) {
        if (!arg_is_buffer[i]) {
            continue;
        }
        halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
        halide_buffer_t *hbuffer = (halide_buffer_t*)args[i];
        d3d12_buffer *buffer = peel_buffer(hbuffer);
        set_input_buffer(binder, buffer, uav_index);    // register(u#)
        uav_index++;
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

    // TODO(marcos): avoid placing UAV barriers all the time after a dispatch...
    // in fact, only buffers written to by the dispatch will need barriers, and
    // only when later bound for read. For now, Halide does not provide enough
    // context for chosing the right time to place transition barriers.
    for (size_t i = 0; arg_sizes[i] != 0; i++) {
        if (!arg_is_buffer[i]) {
            continue;
        }
        halide_buffer_t *hbuffer = (halide_buffer_t*)args[i];
        d3d12_buffer *buffer = peel_buffer(hbuffer);
        compute_barrier(cmdList, buffer);
    }

    commit_command_list(cmdList);

    wait_until_completed(cmdList);

    #if HALIDE_D3D12_RENDERDOC
    FinishCapturingGPUActivity();
    TRACEPRINT("<<< RenderDoc Capture Ended\n");
    #endif

    #if HALIDE_D3D12_PROFILING
    uint64_t eps = (uint64_t)get_elapsed_time(profiler, ini, end);
    StackPrinter<64>() << "kernel execution time: " << eps << "us.\n";
    // TODO: keep some live performance stats in the d3d12_function object
    // (accumulate stats based on dispatch similarities -- e.g., blocksX|Y|Z)
    release_object(profiler);
    #endif

    release_object(cmdList);
    release_object(command_allocator);
    release_object(&args_buffer);
    release_object(pipeline_state);
    release_object(binder);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    TRACEPRINT("Time for halide_d3d12compute_device_run: " << (t_after - t_before) / 1.0e6 << " ms\n");
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
    // expects hallide_malloc() to be called here to allocate the host buffer.
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
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    // We only handle copies to d3d12 device or to host
    halide_assert(user_context, (dst_device_interface == NULL) ||
                                (dst_device_interface == &d3d12compute_device_interface));

    if ((src->device_dirty() || src->host == NULL) && 
        src->device_interface != &d3d12compute_device_interface) {
        halide_assert(user_context, dst_device_interface == &d3d12compute_device_interface);
        // This is handled at the higher level.
        return halide_error_code_incompatible_device_interface;
    }

    bool from_host = (src->device_interface != &d3d12compute_device_interface) ||
                     (src->device == 0) ||
                     (src->host_dirty() && src->host != NULL);
    bool to_host = !dst_device_interface;

    halide_assert(user_context, from_host || src->device);
    halide_assert(user_context,   to_host || dst->device);

    device_copy c = make_buffer_copy(src, from_host, dst, to_host);
    MAYBE_UNUSED(c);

    int err = 0;
    {
        TRACEPRINT(
            "(user_context: " << user_context <<
            ", src: " << src << ", dst: " << dst << ")\n"
        );

        #ifdef DEBUG_RUNTIME
        uint64_t t_before = halide_current_time_ns(user_context);
        #endif

        // Device only case
        if (!from_host && !to_host) {
            TRACEPRINT("device-to-device case\n");
            d3d12_buffer *dsrc = peel_buffer(src);
            d3d12_buffer *ddst = peel_buffer(dst);
            size_t src_offset = dsrc->offsetInBytes + c.src_begin;
            size_t dst_offset = ddst->offsetInBytes;
            D3D12ContextHolder d3d12_context (user_context, true);
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
                halide_assert(user_context, (src->host != NULL));
                // it's possible for 'dst->host' to be null, so we can't always memcpy from 'src->host'
                // to 'dst-host' and push/sync changes with 'halide_d3d12compute_copy_to_device' ...
                halide_assert(user_context, (dst->device == c.dst));
                if (dst->host != NULL) {
                    // 1. copy 'src->host' buffer to 'dst->host' buffer:
                    // host buffers already account for the beginning of cropped regions
                    c.dst = reinterpret_cast<uint64_t>(dst->host) + 0;
                    copy_memory(c, user_context);
                    // 2. sync 'dst->host' buffer with 'dst->device' buffer:
                    halide_d3d12compute_copy_to_device(user_context, dst);
                } else {
                    TRACEPRINT("dst->host is NULL\n");
                    D3D12ContextHolder d3d12_context (user_context, true);
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
                    d3d12compute_buffer_copy(d3d12_context.device, staging,             ddst,
                                                                   staging_byte_offset, dst_byte_offset, total_size);
                    uint64_t use_count = __atomic_sub_fetch(&staging->ref_count, 0, __ATOMIC_SEQ_CST);
                    halide_assert(user_context, (use_count == 0));
                }
            } else {
                // device-to-host:
                TRACEPRINT("device-to-host case\n");
                halide_assert(user_context, to_host);
                halide_assert(user_context, (src->device_interface == &d3d12compute_device_interface));
                halide_assert(user_context, (dst->device_interface == NULL));
                halide_assert(user_context, (dst->host != NULL));
                // it's possible for 'src->host' to be null, so we can't always pull/sync changes with
                // 'halide_d3d12compute_copy_to_host' and then memcpy from 'src->host' to 'dst-host'...
                halide_assert(user_context, (src->device == c.src));
                if (src->host != NULL) {
                    // 1. sync 'src->device' buffer with 'src->host' buffer:
                    halide_d3d12compute_copy_to_host(user_context, src);
                    // 2. copy 'src->host' buffer to 'dst->host' buffer:
                    // host buffers already account for the beginning of cropped regions
                    c.src = reinterpret_cast<uint64_t>(src->host) + 0;
                    copy_memory(c, user_context);
                } else {
                    TRACEPRINT("src->host is NULL\n");
                    D3D12ContextHolder d3d12_context (user_context, true);
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
                    d3d12compute_buffer_copy(d3d12_context.device, dsrc,            staging,
                                                                   src_byte_offset, staging_byte_offset, total_size);
                    // 2. memcpy readback staging buffer to 'dst->host' buffer
                    // host buffers already account for the beginning of cropped regions
                    c.src = reinterpret_cast<uint64_t>(staging_base) + staging_byte_offset;
                    c.dst = reinterpret_cast<uint64_t>(dst->host) + 0;
                    copy_memory(c, user_context);
                    uint64_t use_count = __atomic_sub_fetch(&staging->ref_count, 0, __ATOMIC_SEQ_CST);
                    halide_assert(user_context, (use_count == 0));
                }
            }
        }

        #ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        TRACEPRINT("    Time: " << (t_after - t_before) / 1.0e6 << " ms\n");
        #endif
    }

    return err;
}

namespace {

WEAK int d3d12compute_device_crop_from_offset(void *user_context,
                                              const struct halide_buffer_t *src,
                                              int64_t offset,   /* offset in elements, not in bytes */
                                              struct halide_buffer_t *dst) {
    TRACELOG;

    const d3d12_buffer *old_handle = peel_buffer(src);
    ID3D12Resource *pResource = old_handle->resource;
    uint64_t opaqued = reinterpret_cast<uint64_t>(pResource);

    int ret = halide_d3d12compute_wrap_buffer(user_context, dst, opaqued);
    if (ret != 0) {
        d3d12_halt("halide_d3d12compute_device_crop: failed when wrapping buffer.");
        return ret;
    }

    d3d12_buffer *new_handle = peel_buffer(dst);
    halide_assert(user_context, (new_handle != NULL));
    halide_assert(user_context, (new_handle->halide_type == dst->type));
    halide_assert(user_context, (src->device_interface == dst->device_interface));

    new_handle->offset = old_handle->offset + offset;
    new_handle->offsetInBytes = new_handle->offset * dst->type.bytes() * dst->type.lanes;
    // for some reason, 'dst->number_of_elements()' is always returning 1
    // later on when 'set_input()' is called...
    new_handle->elements = old_handle->elements - offset;

    TRACEPRINT(
           "--- "
        << (void*)old_handle  << " | " << " | "
        << old_handle->offset << " : " << old_handle->elements << " : " << old_handle->sizeInBytes
        << "   ->   "
        << (void*)new_handle  << " | " << " | "
        << new_handle->offset << " : " << new_handle->elements << " : " << new_handle->sizeInBytes
        << "\n"
    );

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
    offset /= src->type.bytes();
    return d3d12compute_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_d3d12compute_device_release_crop(void *user_context, struct halide_buffer_t *buf) {
    TRACELOG;
    // for D3D12, this just so happens to be exactly like halide_d3d12compute_device_free():
    return halide_d3d12compute_device_free(user_context, buf);
}

WEAK int halide_d3d12compute_wrap_buffer(void *user_context, struct halide_buffer_t *halide_buf, uint64_t d3d12_resource) {
    TRACELOG;

    ID3D12Resource *pResource = reinterpret_cast<ID3D12Resource*>(d3d12_resource);
    halide_assert(user_context, (pResource != NULL));

    d3d12_buffer sbuffer = { };
    sbuffer.resource = pResource;
    sbuffer.type = d3d12_buffer::ReadWrite;
    sbuffer.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    int ret = wrap_buffer(halide_buf, &sbuffer);
    if (ret != 0) {
        return ret;
    }

    d3d12_buffer *dbuffer = malloct<d3d12_buffer>();
    if (dbuffer == NULL) {
        unwrap_buffer(halide_buf);
        d3d12_halt("halide_d3d12compute_wrap_buffer: malloc failed making device handle.");
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

WEAK int halide_d3d12compute_detach_buffer(void *user_context, struct halide_buffer_t *buf) {
    TRACELOG;

    if (buf->device == 0) {
        return 0;
    }

    d3d12_buffer *dbuffer = peel_buffer(buf);
    unwrap_buffer(buf);

    // it is safe to simply call release_d3d12_object() here:
    // if 'buf' holds an user resource (from halide_d3d12compute_wrap_buffer),
    // the reference count of the resource will just get decremented without
    // actually freeing the underlying resource object;
    // if 'buf' holds an internally managed resource, it will either be freed
    // or have its reference count decreased (when 'buf' is a device_crop).
    release_d3d12_object(dbuffer);

    return 0;
}

WEAK uintptr_t halide_d3d12compute_get_buffer(void *user_context, struct halide_buffer_t *buf) {
    TRACELOG;
    if (buf->device == NULL) {
        return 0;
    }
    d3d12_buffer *dbuffer = peel_buffer(buf);
    ID3D12Resource *pResource = dbuffer->resource;
    uintptr_t opaqued = reinterpret_cast<uintptr_t>(pResource);
    return opaqued;
}

WEAK const struct halide_device_interface_t *halide_d3d12compute_device_interface() {
    TRACELOG;
    return &d3d12compute_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void halide_d3d12compute_cleanup() {
    TRACELOG;
    halide_d3d12compute_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace D3D12Compute {

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
    halide_d3d12compute_detach_buffer
};

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
    NULL,
    &d3d12compute_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::D3D12Compute

#endif  // BITS_64
