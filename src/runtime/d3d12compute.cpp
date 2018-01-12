#define DEBUG_RUNTIME   1   // for debug(NULL) print to work...

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
#define HALIDE_D3D12_APPLY_ABI_PATCHES (1)
#include "mini_d3d12.h"

#define HALIDE_D3D12_DEBUG (1)

#if HALIDE_D3D12_DEBUG
    static const char indent_pattern [] = "   ";
    static char  indent [128] = { };
    static int   indent_end   = 0;
    struct TraceLogScope
    {
        TraceLogScope()
        {
            for (const char* p = indent_pattern; *p; ++p)
                indent[indent_end++] = *p;
        }
        ~TraceLogScope()
        {
            for (const char* p = indent_pattern; *p; ++p)
                indent[--indent_end] = '\0';
        }
    };
    #define TRACEINDENT     ((const char*)indent)
    #define TRACEPRINT(msg) debug(NULL) << TRACEINDENT << msg;
    #define TRACELOG        TRACEPRINT("[@]" << __FUNCTION__ << "\n"); TraceLogScope trace_scope___;
#else
    #define TRACEINDENT ""
    #define TRACEPRINT(msg)
    #define TRACELOG
#endif

static void* user_context = NULL;   // in case there's no user context available in the scope of a function

template<typename T>
static T* malloct()
{
    T* p = (T*)malloc(sizeof(T));
    return(p);
}

template<typename ID3D12T>
static const char* d3d12typename(ID3D12T*)
{
    return("UNKNOWN");
}

#define D3D12TYPENAME(T) static const char* d3d12typename(T*) { return(#T); }
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
D3D12TYPENAME(ID3DBlob)

template<typename ID3D12T>
static bool D3DError(HRESULT result, ID3D12T* object, void* user_context, const char* message)
{
    // HRESULT ERROR CODES:
    // D3D12: https://msdn.microsoft.com/en-us/library/windows/desktop/bb509553(v=vs.85).aspx
    // Win32: https://msdn.microsoft.com/en-us/library/windows/desktop/aa378137(v=vs.85).aspx
    if (FAILED(result) || !object)
    {
        error(user_context) << TRACEINDENT
                            << message
                            << " (HRESULT=" << (void*)(int64_t)result
                            << ", object*=" << object << ").\n";
        return(true);
    }
    debug(user_context) << TRACEINDENT << d3d12typename(object) << " object created: " << object << "\n";
    return(false);
}

static DXGI_FORMAT FindD3D12FormatForHalideType(halide_type_t type)
{
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

    halide_assert(NULL, (type.code >= 0) && (type.code <= 2));
    halide_assert(NULL, (type.lanes > 0) && (type.lanes <= 4));

    int i = 0;
    switch (type.bytes())
    {
        case 1  : i = 0; break;
        case 2  : i = 1; break;
        case 4  : i = 2; break;
        case 8  : i = 3; break;
        default : halide_assert(NULL, false);  break;
    }

    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    format = FORMATS[(int)type.code][type.lanes-1][i];
    return(format);
}

#define INLINE inline __attribute__((always_inline))

// ---
extern "C" {
typedef void *objc_id;
typedef void *objc_sel;
}

namespace Halide { namespace Runtime { namespace Internal {

objc_sel sel_getUid(const char *string) { return(0); }
objc_id objc_msgSend(objc_id self, objc_sel op, ...) { return(0); }

extern "C" size_t strlen(const char *string);

WEAK void ns_log_object(objc_id obj) { }

}}}

extern "C" {
struct ObjectiveCClass { void* dummy; };
}

ObjectiveCClass _NSConcreteGlobalBlock = { 0 };
// ---

// The default implementation of halide_d3d12_get_symbol attempts to load
// the D3D12 runtime shared library/DLL, and then get the symbol from it.
static void* lib_d3d12  = NULL;
static void* lib_D3DCompiler_47 = NULL;

struct LibrarySymbol
{
    template<typename T>
    operator T () { return((T)symbol); }
    void* symbol;
};
static INLINE LibrarySymbol get_symbol(void* user_context, void* lib, const char* name)
{
    void* s = halide_get_library_symbol(lib, name);
    if (!s)
    {
        error(user_context) << "Symbol not found: " << name << "\n";
    }
    LibrarySymbol symbol = { s };
    return symbol;
}

static PFN_D3D12_CREATE_DEVICE              D3D12CreateDevice           = NULL;
static PFN_D3D12_GET_DEBUG_INTERFACE        D3D12GetDebugInterface      = NULL;
static PFN_D3D12_SERIALIZE_ROOT_SIGNATURE   D3D12SerializeRootSignature = NULL;
static PFN_D3DCOMPILE                       D3DCompile                  = NULL;

#if defined(__cplusplus) && !defined(_MSC_VER)
#if defined(__MINGW32__)
#undef __uuidof
#endif
#define UUIDOF(T) REFIID __uuidof(const T&) { return( IID_ ## T ); }
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
#endif

template<typename ID3D12Type>
struct halide_d3d12_wrapper
{
    operator ID3D12Type* ()    { return(reinterpret_cast<ID3D12Type*>(this)); }
    ID3D12Type* operator -> () { return(reinterpret_cast<ID3D12Type*>(this)); }
};

template<typename ID3D12Type>
struct halide_d3d12_deep_wrapper
{
    ID3D12Type* p;
    operator ID3D12Type*    () { return(p); }
    ID3D12Type* operator -> () { return(p); }
};

struct halide_d3d12compute_device : public halide_d3d12_wrapper<ID3D12Device> { };
struct halide_d3d12compute_command_queue : public halide_d3d12_deep_wrapper<ID3D12CommandQueue>
{
    ID3D12Fence* fence;
    volatile uint64_t last_signal;
};

namespace Halide { namespace Runtime { namespace Internal { namespace D3D12Compute {

typedef halide_d3d12compute_device        d3d12_device;
typedef halide_d3d12compute_command_queue d3d12_command_queue;

struct  d3d12_resource : public halide_d3d12_wrapper<ID3D12Resource> { };
//typedef d3d12_resource d3d12_buffer;

struct d3d12_buffer
{
    ID3D12Resource* resource;
    ID3D12Resource* staging;    // TODO(marcos): ugly memory duplication here...
    halide_buffer_t* halide;
    void* mapped;
    UINT size;                  // size in bytes
};

struct d3d12_command_allocator     : public halide_d3d12_wrapper<ID3D12CommandAllocator> { };

struct d3d12_graphics_command_list : public halide_d3d12_deep_wrapper<ID3D12GraphicsCommandList>
{
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

struct d3d12_library
{
    int source_length;
    char source [1];
};

struct d3d12_function
{
    HRESULT   status;
    ID3DBlob* shaderBlob;
    ID3DBlob* errorMsgs;
    ID3D12RootSignature* rootSignature;
};

enum ResourceBindingSlots
{
    UAV = 0,
    CBV,
    SRV,
    NumSlots
};

struct d3d12_binder
{
    ID3D12DescriptorHeap* descriptorHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE CPU [NumSlots];
    D3D12_GPU_DESCRIPTOR_HANDLE GPU [NumSlots];
    UINT descriptorSize;
};

struct d3d12_compile_options;

WEAK int wrap_buffer(void* user_context, struct halide_buffer_t* buf, d3d12_buffer* d3d12_buf)
{
    TRACELOG;
    uint64_t raw = reinterpret_cast<uint64_t>(d3d12_buf);
    return(halide_d3d12compute_wrap_buffer(user_context, buf, raw));
}

WEAK d3d12_device* device = NULL;
WEAK d3d12_command_queue* queue = NULL;

template<typename d3d12_T>
static void release_d3d12_object(d3d12_T* obj)
{
    TRACELOG;
    debug(NULL) << TRACEINDENT << "!!!!!!!!!! RELEASING UNKNOWN D3D12 OBJECT !!!!!!!!!!\n";
}

template<typename d3d12_T>
static void release_ns_object(d3d12_T* obj)
{
    TRACELOG;
    release_d3d12_object(obj);
}

template<>
void release_d3d12_object<d3d12_device>(d3d12_device* device)
{
    TRACELOG;
    (*device)->Release();
}

template<>
void release_d3d12_object<d3d12_command_queue>(d3d12_command_queue* queue)
{
    TRACELOG;
    queue->p->Release();
    queue->fence->Release();
    free(queue);
}

template<>
void release_d3d12_object<d3d12_command_list>(d3d12_command_list* cmdList)
{
    TRACELOG;
    cmdList->p->Release();
    free(cmdList);
}

template<>
void release_d3d12_object<d3d12_buffer>(d3d12_buffer* buffer)
{
    TRACELOG;
    buffer->resource->Release();
    buffer->staging->Release();
    free(buffer);
}

template<>
void release_d3d12_object<d3d12_library>(d3d12_library* library)
{
    TRACELOG;
    free(library);
}

template<>
void release_d3d12_object<d3d12_function>(d3d12_function* function)
{
    TRACELOG;
    if (function->shaderBlob)
        function->shaderBlob->Release();
    if (function->errorMsgs)
        function->errorMsgs->Release();
    if (function->rootSignature)
        function->rootSignature->Release();
    free(function);
}

template<>
void release_d3d12_object<d3d12_compute_pipeline_state>(d3d12_compute_pipeline_state* pso)
{
    TRACELOG;
    (*pso)->Release();
}

static void D3D12LoadDependencies(void* user_context)
{
    TRACELOG;

    const char* lib_names [] = {
        "d3d12.dll",
        "D3DCompiler_47.dll",
    };
    static const int num_libs = sizeof(lib_names) / sizeof(lib_names[0]);
    void** lib_handles [num_libs] = {
        &lib_d3d12,
        &lib_D3DCompiler_47,
    };
    for (size_t i = 0; i < num_libs; i++)
    {
        // Only try to load the library if the library isn't already
        // loaded, or we can't load the symbol from the process already.
        void*& lib = *(lib_handles[i]);
        if (lib)
        {
            continue;
        }
        lib = halide_load_library(lib_names[i]);
        if (lib)
        {
            debug(user_context) << TRACEINDENT << "Loaded runtime library: " << lib_names[i] << "\n";
        }
        else
        {
            error(user_context) << TRACEINDENT << "Unable to load runtime library: " << lib_names[i] << "\n";
        }
    }

    D3D12CreateDevice           = get_symbol(user_context, lib_d3d12,           "D3D12CreateDevice");
    D3D12GetDebugInterface      = get_symbol(user_context, lib_d3d12,           "D3D12GetDebugInterface");
    D3D12SerializeRootSignature = get_symbol(user_context, lib_d3d12,           "D3D12SerializeRootSignature");
    D3DCompile                  = get_symbol(user_context, lib_D3DCompiler_47,  "D3DCompile");

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

//#pragma clang diagnostic ignored "-Wignored-attributes"
//static void TestMethodSignature(D3D12_CPU_DESCRIPTOR_HANDLE(__cdecl ID3D12DescriptorHeap::*pfn_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart)(void))
//{
//}

// D3D12 ABI patch trampolines (refer to 'd3d12_abi_patch_64.ll')
#ifdef __cplusplus
extern "C" {
#endif
    void Call_ID3D12DescriptorHeap_GetDesc(int64_t* descriptorheap, int64_t* desc);
    void Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(int64_t* descriptorheap, int64_t* cpuHandle);
    void Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(int64_t* descriptorheap, int64_t* gpuHandle);
#ifdef __cplusplus
}
#endif

D3D12_DESCRIPTOR_HEAP_DESC Call_ID3D12DescriptorHeap_GetDesc(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    Call_ID3D12DescriptorHeap_GetDesc( (int64_t*)descriptorheap, (int64_t*)&desc );
    return(desc);
}

D3D12_CPU_DESCRIPTOR_HANDLE Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { };
    Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( (int64_t*)descriptorheap, (int64_t*)&cpuHandle );
    return(cpuHandle);
}

D3D12_GPU_DESCRIPTOR_HANDLE Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { };
    Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart( (int64_t*)descriptorheap, (int64_t*)&gpuHandle );
    return(gpuHandle);
}

static d3d12_device* D3D12CreateSystemDefaultDevice(void* user_context)
{
    TRACELOG;

    D3D12LoadDependencies(user_context);

    HRESULT result = E_UNEXPECTED;

#if HALIDE_D3D12_DEBUG
    ID3D12Debug* d3d12Debug = NULL;
    result = D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12Debug));
    if (D3DError(result, d3d12Debug, user_context, "Unable to retrieve the debug interface for Direct3D 12"))
        return(NULL);
    d3d12Debug->EnableDebugLayer();
#endif

#if 0
    IDXGIFactory4* dxgiFactory = NULL;
    result = CreateDXGIFactory1(__uuidof(IDXGIFactory4), IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(result))
    {
        return(NULL);
    }

    IDXGIAdapter* dxgiAdapter = NULL;
    result = dxgiFactory->EnumAdapters(0, &dxgiAdapter);
    if (FAILED(result))
    {
        return(NULL);
    }

    // NOTE(marcos): ignoring IDXGIOutput setup since this is for compute only
    IDXGIOutput* dxgiDisplayOutput = NULL;
    result = dxgiAdapter->EnumOutputs(0, &dxgiDisplayOutput);
    if(FAILED(result))
    {
        return(NULL);
    }
#endif

    IDXGIAdapter* dxgiAdapter = NULL;    // NULL -> default adapter
    ID3D12Device* device = NULL;
    result = D3D12CreateDevice(dxgiAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (D3DError(result, device, user_context, "Unable to create the Direct3D 12 device"))
        return(NULL);

#if 0 && HALIDE_D3D12_APPLY_ABI_PATCHES
    {
    d3d12_device* dev = reinterpret_cast<d3d12_device*>(device);
    debug(NULL) << "!!!!!!!!!! BINDER-INI !!!!!!!!!!\n";
    ID3D12DescriptorHeap* descriptorHeap = NULL;
    D3D12_DESCRIPTOR_HEAP_DESC dhd = { };
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dhd.NumDescriptors  = 0;    // TODO(marcos): replace this arbitrary descriptor count...
        dhd.NumDescriptors += 25;   // have some descriptors for the unbounded UAV table
        dhd.NumDescriptors += 25;   // then some for the unbounded CBV table
        dhd.NumDescriptors += 25;   // then some for the unbounded SRV table
        dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        dhd.NodeMask = 0;
    HRESULT result = (*dev)->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&descriptorHeap));
    if (D3DError(result, descriptorHeap, NULL, "Unable to create the Direct3D 12 descriptor heap"))
        return(NULL);
    debug(NULL) << "!!!!!!!!!! HRESULT: " << result << "\n";
    debug(NULL) << "!!!!!!!!!! ID3D12DescriptorHeap: " << (uint64_t)descriptorHeap << "\n";
    UINT descriptorSize = (*dev)->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    debug(NULL) << "!!!!!!!!!! descriptor handle increment size: " << descriptorSize << "\n";

    debug(NULL) << "!!!!!!!!!! D3D12_DESCRIPTOR_HEAP_DESC: " << (int32_t)sizeof(D3D12_DESCRIPTOR_HEAP_DESC) << "\n";
    debug(NULL) << "!!!!!!!!!! dhd.Type: " << (int32_t)sizeof(dhd.Type) << "\n";
    debug(NULL) << "!!!!!!!!!! dhd.NumDescriptors: " << (int32_t)sizeof(dhd.NumDescriptors) << "\n";
    debug(NULL) << "!!!!!!!!!! dhd.Flags: " << (int32_t)sizeof(dhd.Flags) << "\n";
    debug(NULL) << "!!!!!!!!!! dhd.NodeMask: " << (int32_t)sizeof(dhd.NodeMask) << "\n";
    debug(NULL) << "!!!!!!!!!! D3D12_CPU_DESCRIPTOR_HANDLE: " << (int32_t)sizeof(D3D12_CPU_DESCRIPTOR_HANDLE) << "\n";
    D3D12_CPU_DESCRIPTOR_HANDLE bCPU = { };
    debug(NULL) << "!!!!!!!!!! ptr: " << (int32_t)sizeof(bCPU.ptr) << "\n";
    debug(NULL) << "!!!!!!!!!! D3D12_GPU_DESCRIPTOR_HANDLE: " << (int32_t)sizeof(D3D12_GPU_DESCRIPTOR_HANDLE) << "\n";
    D3D12_GPU_DESCRIPTOR_HANDLE bGPU = { };
    debug(NULL) << "!!!!!!!!!! ptr: " << (int32_t)sizeof(bGPU.ptr) << "\n";
    debug(NULL) << "!!!!!!!!!! ID3D12DescriptorHeap: " << (int32_t)sizeof(ID3D12DescriptorHeap) << "\n";
    debug(NULL) << "!!!!!!!!!! d3d12_binder: " << (int32_t)sizeof(d3d12_binder) << "\n";

    //TestMethodSignature(&ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart);

    debug(NULL) << "!!!!!!!!!! ID3D12DescriptorHeap: " << (uint64_t)descriptorHeap << "\n";
    int a = 1;
    D3D12_DESCRIPTOR_HEAP_DESC dhd2 = Call_ID3D12DescriptorHeap_GetDesc(descriptorHeap);
    a = 2;
    debug(NULL) << "!!!!!!!!!! descriptor heap desc: " << (uint64_t)dhd2.Type << ":" << dhd2.NumDescriptors << ":" << dhd2.Flags << ":" << dhd2.NodeMask << "\n";
    debug(NULL) << "!!!!!!!!!! ID3D12DescriptorHeap: " << (uint64_t)descriptorHeap << "\n";

    D3D12_CPU_DESCRIPTOR_HANDLE baseCPU = Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptorHeap);
    debug(NULL) << "!!!!!!!!!! descriptor heap base for CPU: " << baseCPU.ptr << "\n";
    debug(NULL) << "!!!!!!!!!! ID3D12DescriptorHeap: " << (uint64_t)descriptorHeap << "\n";

    D3D12_GPU_DESCRIPTOR_HANDLE baseGPU = Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptorHeap);
    debug(NULL) << "!!!!!!!!!! descriptor heap base for GPU: " << baseGPU.ptr << "\n";
    debug(NULL) << "!!!!!!!!!! ID3D12DescriptorHeap: " << (uint64_t)descriptorHeap << "\n";

    d3d12_binder* binder = malloct<d3d12_binder>();
    binder->descriptorHeap = descriptorHeap;
    binder->descriptorSize = descriptorSize;
    binder->CPU[UAV].ptr = baseCPU.ptr +  0*descriptorSize;
    binder->CPU[CBV].ptr = baseCPU.ptr + 25*descriptorSize;
    binder->CPU[SRV].ptr = baseCPU.ptr + 50*descriptorSize;
    binder->GPU[UAV].ptr = baseGPU.ptr +  0*descriptorSize;
    binder->GPU[CBV].ptr = baseGPU.ptr + 25*descriptorSize;
    binder->GPU[SRV].ptr = baseGPU.ptr + 50*descriptorSize;
    debug(NULL) << "!!!!!!!!!! BINDER-END !!!!!!!!!!\n";
    error(NULL) << ":)";
    }
#endif

    return(reinterpret_cast<d3d12_device*>(device));
}

WEAK void dispatch_threadgroups(d3d12_compute_command_list* cmdList,
                                int32_t blocks_x, int32_t blocks_y, int32_t blocks_z,
                                int32_t threads_x, int32_t threads_y, int32_t threads_z)
{
    TRACELOG;

    static int32_t total_dispatches = 0;
    debug(user_context) << TRACEINDENT
                        << "Dispatching threadgroups (number " << total_dispatches++ << ") "
                           "blocks(" << blocks_x << ", " << blocks_y << ", " << blocks_z << " ) "
                           "threads(" << threads_x << ", " << threads_y << ", " << threads_z << ")\n";

    (*cmdList)->Dispatch(blocks_x, blocks_y, blocks_z);
}

static ID3D12Resource* new_staging_buffer(d3d12_device* device, size_t length)
{
    ID3D12Resource* resource = NULL;

    D3D12_RESOURCE_DESC desc = { };
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;                             // 0 defaults to 64KB alignment, which is mandatory for buffers
        desc.Width = length;
        desc.Height = 1;                                // for buffers, must always be 1
        desc.DepthOrArraySize = 1;                      // for buffers, must always be 1
        desc.MipLevels = 1;                             // for buffers, must always be 1
        desc.Format = DXGI_FORMAT_UNKNOWN;              // for buffers, must always be DXGI_FORMAT_UNKNOWN
        desc.SampleDesc.Count = 1;                      // for buffers, must always be 1
        desc.SampleDesc.Quality = 0;                    // for buffers, must always be 0
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;   // for buffers, must always be D3D12_TEXTURE_LAYOUT_ROW_MAJOR
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = { };  // CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_...)
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 0;             // 0 is equivalent to 0b0...01 (single adapter)
        heapProps.VisibleNodeMask  = 0;             // (the same applies here)

    D3D12_HEAP_PROPERTIES* pHeapProperties = &heapProps;
    D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    D3D12_RESOURCE_DESC* pDesc = &desc;
    D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
    D3D12_CLEAR_VALUE* pOptimizedClearValue = NULL; // for textures only; must be null for buffers

    // A commited resource manages its own private heap
    HRESULT result = (*device)->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, IID_PPV_ARGS(&resource));
    if (D3DError(result, resource, NULL, "Unable to create the Direct3D 12 staging buffer resource"))
        return(NULL);

    return(resource);
}

WEAK d3d12_buffer* new_buffer(d3d12_device* device, size_t length)
{
    TRACELOG;

    ID3D12Resource* resource = NULL;

    D3D12_RESOURCE_DESC desc = { };
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;                             // 0 defaults to 64KB alignment, which is mandatory for buffers
        desc.Width = length;
        desc.Height = 1;                                // for buffers, must always be 1
        desc.DepthOrArraySize = 1;                      // for buffers, must always be 1
        desc.MipLevels = 1;                             // for buffers, must always be 1
        desc.Format = DXGI_FORMAT_UNKNOWN;              // for buffers, must always be DXGI_FORMAT_UNKNOWN
        desc.SampleDesc.Count = 1;                      // for buffers, must always be 1
        desc.SampleDesc.Quality = 0;                    // for buffers, must always be 0
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;   // for buffers, must always be D3D12_TEXTURE_LAYOUT_ROW_MAJOR
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // upload heaps are handy since they are accessible both by CPU and GPU; however, they are only good for streaming
    // (write once, read once, discard, rinse and repeat) vertex and constant buffer data; for unordered-access views,
    // upload heaps are not allowed.
    D3D12_HEAP_PROPERTIES heapProps = { };  // CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_...)
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 0;             // 0 is equivalent to 0b0...01 (single adapter)
        heapProps.VisibleNodeMask  = 0;             // (the same applies here)

    D3D12_HEAP_PROPERTIES* pHeapProperties = &heapProps;
    D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    D3D12_RESOURCE_DESC* pDesc = &desc;
    D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
    D3D12_CLEAR_VALUE* pOptimizedClearValue = NULL; // for textures only; must be null for buffers

    // A commited resource manages its own private heap
    HRESULT result = (*device)->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, IID_PPV_ARGS(&resource));
    if (D3DError(result, resource, NULL, "Unable to create the Direct3D 12 buffer resource"))
        return(NULL);

    d3d12_buffer* buffer = malloct<d3d12_buffer>();
    buffer->resource = resource;
    buffer->halide = NULL;
    buffer->mapped = NULL;
    buffer->staging = new_staging_buffer(device, length);
    buffer->size = length;

    return(buffer);
}

WEAK d3d12_command_queue* new_command_queue(d3d12_device* device)
{
    TRACELOG;

    ID3D12CommandQueue* commandQueue = NULL;
    {
        D3D12_COMMAND_QUEUE_DESC cqDesc = { };
            cqDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            cqDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            cqDesc.NodeMask = 0;    // 0, for single GPU operation
        HRESULT result = (*device)->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue));
        if (D3DError(result, commandQueue, NULL, "Unable to create the Direct3D 12 command queue"))
            return(NULL);
    }

    ID3D12Fence* fence = NULL;
    {
        HRESULT result = (*device)->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (D3DError(result, fence, NULL, "Unable to create the Direct3D 12 fence for command queue"))
            return(NULL);
    }

    d3d12_command_queue* q = malloct<d3d12_command_queue>();
    q->p = commandQueue;
    q->fence = fence;
    __atomic_store_n(&q->last_signal, 0, __ATOMIC_SEQ_CST);

    return(q);
}

template<D3D12_COMMAND_LIST_TYPE Type>
static d3d12_command_allocator* new_command_allocator(d3d12_device* device)
{
    TRACELOG;
    halide_assert(NULL, device);
    ID3D12CommandAllocator* commandAllocator = NULL;
    HRESULT result = (*device)->CreateCommandAllocator(Type, IID_PPV_ARGS(&commandAllocator));
    if (D3DError(result, commandAllocator, NULL, "Unable to create the Direct3D 12 command allocator"))
        return(NULL);
    return(reinterpret_cast<d3d12_command_allocator*>(commandAllocator));
}

WEAK void add_command_list_completed_handler(d3d12_command_list* cmdList, struct command_list_completed_handler_block_literal *handler) {
    TRACELOG;
    TRACEPRINT("WHAT SHOULD BE DONE HERE? JUST INSERT A FENCE?\n");
    typedef void (*add_completed_handler_method)(objc_id cmdList, objc_sel sel, struct command_list_completed_handler_block_literal *handler);
    add_completed_handler_method method = (add_completed_handler_method)&objc_msgSend;
    (*method)(cmdList, sel_getUid("addCompletedHandler:"), handler);
}

WEAK objc_id command_list_error(d3d12_command_list* cmdList) {
    TRACELOG;
    return objc_msgSend(cmdList, sel_getUid("error"));
}

template<D3D12_COMMAND_LIST_TYPE Type>
static d3d12_command_list* new_command_list(d3d12_device* device, d3d12_command_allocator* allocator)
{
    TRACELOG;
    ID3D12GraphicsCommandList* commandList = NULL;
    UINT nodeMask = 0;
    ID3D12CommandAllocator* pCommandAllocator = (*allocator);
    ID3D12PipelineState* pInitialState = NULL;
    HRESULT result = (*device)->CreateCommandList(nodeMask, Type, pCommandAllocator, pInitialState, IID_PPV_ARGS(&commandList));
    if (D3DError(result, commandList, NULL, "Unable to create the Direct3D 12 command list"))
        return(NULL);

    d3d12_command_list* cmdList = malloct<d3d12_command_list>();
    cmdList->p = commandList;
    cmdList->signal = 0;

    return(cmdList);
}

static d3d12_command_list* new_compute_command_list(d3d12_device* device, d3d12_command_allocator* allocator)
{
    TRACELOG;
    return new_command_list<D3D12_COMMAND_LIST_TYPE_COMPUTE>(device, allocator);
}

static d3d12_copy_command_list* new_copy_command_list(d3d12_device* device, d3d12_command_allocator* allocator)
{
    TRACELOG;
    return new_command_list<D3D12_COMMAND_LIST_TYPE_COPY>(device, allocator);
}

static d3d12_compute_pipeline_state* new_compute_pipeline_state_with_function(d3d12_device* device, d3d12_function* function)
{
    TRACELOG;
    ID3D12PipelineState* pipelineState = NULL;
    D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = { };
        cpsd.pRootSignature = function->rootSignature;
        cpsd.CS.pShaderBytecode = function->shaderBlob->GetBufferPointer();
        cpsd.CS.BytecodeLength  = function->shaderBlob->GetBufferSize();
        cpsd.NodeMask = 0;
        cpsd.CachedPSO.pCachedBlob = NULL;
        cpsd.CachedPSO.CachedBlobSizeInBytes = 0;
        cpsd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    HRESULT result = (*device)->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&pipelineState));
    if (D3DError(result, pipelineState, NULL, "Unable to create the Direct3D 12 pipeline state"))
        return(NULL);
    return(reinterpret_cast<d3d12_compute_pipeline_state*>(pipelineState));
}

static void set_compute_pipeline_state(d3d12_compute_command_list* cmdList, d3d12_compute_pipeline_state* pipeline_state, d3d12_function* function, d3d12_binder* binder)
{
    TRACELOG;

    ID3D12RootSignature* rootSignature = function->rootSignature;
    (*cmdList)->SetComputeRootSignature(rootSignature);

    ID3D12PipelineState* pipelineState = (*pipeline_state);
    (*cmdList)->SetPipelineState(pipelineState);

    ID3D12DescriptorHeap* heaps [] = { binder->descriptorHeap };
    (*cmdList)->SetDescriptorHeaps(1, heaps);

    // more ABI issues.......
#if HALIDE_D3D12_APPLY_ABI_PATCHES
    #pragma message ("WARN(marcos): UGLY ABI PATCH HERE!")
    (*cmdList)->SetComputeRootDescriptorTable(UAV, binder->GPU[UAV].ptr);
    (*cmdList)->SetComputeRootDescriptorTable(CBV, binder->GPU[CBV].ptr);
    (*cmdList)->SetComputeRootDescriptorTable(SRV, binder->GPU[SRV].ptr);
#else
    (*cmdList)->SetComputeRootDescriptorTable(UAV, binder->GPU[UAV]);
    (*cmdList)->SetComputeRootDescriptorTable(CBV, binder->GPU[CBV]);
    (*cmdList)->SetComputeRootDescriptorTable(SRV, binder->GPU[SRV]);
#endif
}

static void end_recording(d3d12_compute_command_list* cmdList)
{
    TRACELOG;
    (*cmdList)->Close();
}

static d3d12_binder* new_descriptor_binder(d3d12_device* device)
{
    TRACELOG;
    ID3D12DescriptorHeap* descriptorHeap = NULL;
    D3D12_DESCRIPTOR_HEAP_DESC dhd = { };
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dhd.NumDescriptors  = 0;    // TODO(marcos): replace this arbitrary descriptor count...
        dhd.NumDescriptors += 25;   // have some descriptors for the unbounded UAV table
        dhd.NumDescriptors += 25;   // then some for the unbounded CBV table
        dhd.NumDescriptors += 25;   // then some for the unbounded SRV table
        dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        dhd.NodeMask = 0;
    HRESULT result = (*device)->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&descriptorHeap));
    if (D3DError(result, descriptorHeap, NULL, "Unable to create the Direct3D 12 descriptor heap"))
        return(NULL);

    UINT descriptorSize = (*device)->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    //TRACEPRINT("!!! descriptor handle increment size: " << descriptorSize << "\n");

    d3d12_binder* binder = malloct<d3d12_binder>();
    binder->descriptorHeap = descriptorHeap;
    binder->descriptorSize = descriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE baseCPU = Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptorHeap);
    //TRACEPRINT("!!! descriptor heap base for CPU: " << baseCPU.ptr << "\n");
    binder->CPU[UAV].ptr = baseCPU.ptr +  0*descriptorSize;
    binder->CPU[CBV].ptr = baseCPU.ptr + 25*descriptorSize;
    binder->CPU[SRV].ptr = baseCPU.ptr + 50*descriptorSize;

    D3D12_GPU_DESCRIPTOR_HANDLE baseGPU = Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptorHeap);
    //TRACEPRINT("!!! descriptor heap base for GPU: " << baseGPU.ptr << "\n");
    binder->GPU[UAV].ptr = baseGPU.ptr +  0*descriptorSize;
    binder->GPU[CBV].ptr = baseGPU.ptr + 25*descriptorSize;
    binder->GPU[SRV].ptr = baseGPU.ptr + 50*descriptorSize;

    return(binder);
}

struct NSRange {
    size_t location;
    size_t length;
};

WEAK void did_modify_range(d3d12_buffer *buffer, NSRange range) {
    TRACELOG;
    typedef void (*did_modify_range_method)(objc_id obj, objc_sel sel, NSRange range);
    did_modify_range_method method = (did_modify_range_method)&objc_msgSend;
    (*method)(buffer, sel_getUid("didModifyRange:"), range);
}

WEAK void synchronize_resource(d3d12_copy_command_list* cmdList, d3d12_buffer *buffer) {
    TRACELOG;
    typedef void (*synchronize_resource_method)(objc_id obj, objc_sel sel, d3d12_buffer *buffer);
    synchronize_resource_method method = (synchronize_resource_method)&objc_msgSend;
    (*method)(cmdList, sel_getUid("synchronizeResource:"), buffer);
}

WEAK bool is_buffer_managed(d3d12_buffer *buffer) {
    TRACELOG;
    typedef bool (*responds_to_selector_method)(objc_id obj, objc_sel sel_1, objc_sel sel_2);
    responds_to_selector_method method1 = (responds_to_selector_method)&objc_msgSend;
    objc_sel storage_mode_sel = sel_getUid("storageMode");
    if ((*method1)(buffer, sel_getUid("respondsToSelector:"), storage_mode_sel)) {
        typedef int (*storage_mode_method)(objc_id obj, objc_sel sel);
        storage_mode_method method = (storage_mode_method)&objc_msgSend;
        int storage_mode = (*method)(buffer, storage_mode_sel);
        return storage_mode == 1; // MTLStorageModeManaged
    }
    return false;
}

static d3d12_library* new_library_with_source(d3d12_device* device, const char* source, size_t source_len)
{
    TRACELOG;
    // Unlike Metal, Direct3D 12 does not have the concept of a "shader library"
    // We can emulate the library functionality by caching the source code until
    // the entry point is known since D3DCompile() requires the entry point name
    const int blocksize = sizeof(d3d12_library) + source_len;
    d3d12_library* library = (d3d12_library*)malloc(blocksize);
    library->source_length = source_len;
    for (int i = 0; i < source_len; ++i)
    {
        library->source[i] = source[i];
    }
    library->source[source_len] = '\0';
    return(library);
}

static d3d12_function* new_function_with_name(d3d12_device* device, d3d12_library* library, const char* name, size_t name_len)
{
    TRACELOG;

    const char* source = library->source;
    int source_size = library->source_length;
    D3D_SHADER_MACRO pDefines [] = { { NULL, NULL } };
    const char* shaderName = name;  // only used for debug information
    ID3DInclude* includeHandler = NULL;
    const char* entryPoint = name;
    const char* target = "cs_5_0";
    UINT flags1 = 0;
    UINT flags2 = 0;
    ID3DBlob* shaderBlob = NULL;
    ID3DBlob* errorMsgs  = NULL;
    HRESULT result = D3DCompile(source, source_size, shaderName, pDefines, includeHandler, entryPoint, target, flags1, flags2, &shaderBlob, &errorMsgs);

    if (FAILED(result) || (NULL == shaderBlob))
    {
        debug(user_context) << TRACEINDENT << "Unable to compile D3D12 compute shader (HRESULT=" << result << ", ShaderBlob=" << shaderBlob << " entry=" << entryPoint << ").\n";
        if (errorMsgs)
        {
            const char* errorMessage = (const char*)errorMsgs->GetBufferPointer();
            debug(user_context) << TRACEINDENT << "D3D12Compute: ERROR: D3DCompiler: " << errorMessage << "\n";
            errorMsgs->Release();
        }
        debug(user_context) << TRACEINDENT << source << "\n";
        error(user_context) << "!!! HALT !!!";
        return(NULL);
    }

    halide_assert(user_context, (NULL == errorMsgs));
    debug(user_context) << TRACEINDENT << "SUCCESS while compiling D3D12 compute shader with entry name '" << entryPoint << "'!\n";

    // TODO(marcos): since a single "uber" root signature can fit all kernels,
    // the root signature should be created/serialized at device creation time
    // unbounded descriptor tables to accommodate all buffers:
    D3D12_ROOT_PARAMETER rootParameterTables [NumSlots] = { };
    // UAVs: read-only, write-only and read-write buffers:
        D3D12_ROOT_PARAMETER& RootTableUAV = rootParameterTables[UAV];
        RootTableUAV.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        RootTableUAV.DescriptorTable.NumDescriptorRanges = 1;
            D3D12_DESCRIPTOR_RANGE UAVs = { };
                UAVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                UAVs.NumDescriptors = -1;   // unbounded size
                UAVs.BaseShaderRegister = 0;
                UAVs.RegisterSpace = 0;
                UAVs.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        RootTableUAV.DescriptorTable.pDescriptorRanges = &UAVs;
        RootTableUAV.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;   // <- compute must use this
    // CBVs: read-only uniform/coherent/broadcast buffers:
        D3D12_ROOT_PARAMETER& RootTableCBV = rootParameterTables[CBV];
        RootTableCBV.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        RootTableCBV.DescriptorTable.NumDescriptorRanges = 1;
            D3D12_DESCRIPTOR_RANGE CBVs = { };
                CBVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                CBVs.NumDescriptors = -1;   // unbounded size
                CBVs.BaseShaderRegister = 0;
                CBVs.RegisterSpace = 0;
                CBVs.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        RootTableCBV.DescriptorTable.pDescriptorRanges = &CBVs;
        RootTableCBV.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;   // <- compute must use this
    // SRVs: textures and read-only buffers:
        D3D12_ROOT_PARAMETER& RootTableSRV = rootParameterTables[SRV];
        RootTableSRV.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        RootTableSRV.DescriptorTable.NumDescriptorRanges = 1;
            D3D12_DESCRIPTOR_RANGE SRVs = { };
                SRVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                SRVs.NumDescriptors = -1;   // unbounded size
                SRVs.BaseShaderRegister = 0;
                SRVs.RegisterSpace = 0;
                SRVs.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        RootTableSRV.DescriptorTable.pDescriptorRanges = &SRVs;
        RootTableSRV.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;   // <- compute must use this

    D3D12_ROOT_SIGNATURE_DESC rsd = { };
        rsd.NumParameters = NumSlots;
        rsd.pParameters = rootParameterTables;
        rsd.NumStaticSamplers = 0;
        rsd.pStaticSamplers = NULL;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    D3D_ROOT_SIGNATURE_VERSION Version = D3D_ROOT_SIGNATURE_VERSION_1;
    ID3DBlob* pSignBlob  = NULL;
    ID3DBlob* pSignError = NULL;
    result = D3D12SerializeRootSignature(&rsd, Version, &pSignBlob, &pSignError);
    if (D3DError(result, pSignBlob, NULL, "Unable to serialize the Direct3D 12 root signature"))
    {
        halide_assert(NULL, pSignError);
        error(NULL) << (const char*)pSignError->GetBufferPointer();
        return(NULL);
    }

    ID3D12RootSignature* rootSignature = NULL;
    UINT nodeMask = 0;
    const void* pBlobWithRootSignature = pSignBlob->GetBufferPointer();
    SIZE_T blobLengthInBytes = pSignBlob->GetBufferSize();
    result = (*device)->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, IID_PPV_ARGS(&rootSignature));
    if (D3DError(result, rootSignature, NULL, "Unable to create the Direct3D 12 root signature"))
        return(NULL);

    d3d12_function* function = malloct<d3d12_function>();
    function->status     = result;
    function->shaderBlob = shaderBlob;
    function->errorMsgs  = errorMsgs;
    function->rootSignature = rootSignature;

    return(function);
}

WEAK void set_input_buffer(d3d12_compute_command_list* cmdList, d3d12_binder* binder, d3d12_buffer* input_buffer, uint32_t index)
{
    TRACELOG;

    // NOTE(marcos): if there is no associated Halide buffer, it is probably a
    // constant buffer managed internally by the runtime:
    if (input_buffer->halide == NULL)
    {
        ID3D12Resource* pResource = input_buffer->resource;
        D3D12_GPU_VIRTUAL_ADDRESS pGPU = pResource->GetGPUVirtualAddress();

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvd = { };
            cbvd.BufferLocation = pGPU;
            cbvd.SizeInBytes = input_buffer->size;

        D3D12_CPU_DESCRIPTOR_HANDLE hDescCBV = binder->CPU[CBV];
        binder->CPU[CBV].ptr += binder->descriptorSize;

        #if HALIDE_D3D12_APPLY_ABI_PATCHES
            #pragma message ("WARN(marcos): UGLY ABI PATCH HERE!")
            (*device)->CreateConstantBufferView(&cbvd, hDescCBV.ptr);
        #else
            (*device)->CreateConstantBufferView(&cbvd, hDescCBV);
        #endif
    }
    else
    {
        const halide_type_t& type = input_buffer->halide->type;

        //DXGI_FORMAT Format = FindD3D12FormatForHalideType(type);
        UINT NumElements = input_buffer->halide->number_of_elements();
        UINT Stride = type.bytes() * type.lanes;

        // A View of a non-Structured Buffer cannot be created using a NULL Desc.
        // Default Desc parameters cannot be used, as a Format must be supplied.
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = { };
            uavd.Format = DXGI_FORMAT_UNKNOWN;
            uavd.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavd.Buffer.FirstElement = 0;
            uavd.Buffer.NumElements = NumElements;
            uavd.Buffer.StructureByteStride = Stride;
            uavd.Buffer.CounterOffsetInBytes = 0;               // 0, since this is not an atomic counter
            uavd.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        // TODO(marcos): should probably use "index" here somewhere
        D3D12_CPU_DESCRIPTOR_HANDLE hDescUAV = binder->CPU[UAV];
        binder->CPU[UAV].ptr += binder->descriptorSize;

        ID3D12Resource* pResource = input_buffer->resource;
        ID3D12Resource* pCounterResource = NULL;    // for atomic counters

        (*device)->CreateUnorderedAccessView(pResource, pCounterResource, &uavd, hDescUAV);
    }
}

WEAK void set_threadgroup_memory_length(d3d12_compute_command_list* cmdList, uint32_t length, uint32_t index) {
    TRACELOG;
    TRACEPRINT("IS THIS EVEN NECESSARY ON D3D12?\n");
    typedef void (*set_threadgroup_memory_length_method)(objc_id encoder, objc_sel sel,
                                                         size_t length, size_t index);
    set_threadgroup_memory_length_method method = (set_threadgroup_memory_length_method)&objc_msgSend;
    (*method)(cmdList, sel_getUid("setThreadgroupMemoryLength:atIndex:"),
              length, index);
}

static void commit_command_list(d3d12_compute_command_list* cmdList)
{
    TRACELOG;
    ID3D12CommandList* lists [] = { (*cmdList) };
    (*queue)->ExecuteCommandLists(1, lists);
    cmdList->signal = __atomic_add_fetch(&queue->last_signal, 1, __ATOMIC_SEQ_CST); // ++last_signal
    (*queue)->Signal(queue->fence, cmdList->signal);
}

static void wait_until_completed(d3d12_compute_command_list* cmdList)
{
    TRACELOG;

    // TODO(marcos): perhaps replace the busy-wait loop below by a blocking wait event?
    // HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    // queue->fence->SetEventOnCompletion(cmdList->signal, hEvent);
    // WaitForSingleObject(hEvent, INFINITE);
    // CloseHandle(hEvent);

    HRESULT result_before = (*device)->GetDeviceRemovedReason();

    while (queue->fence->GetCompletedValue() < cmdList->signal)
        ;

    HRESULT result_after = (*device)->GetDeviceRemovedReason();
    if (FAILED(result_after))
    {
        debug(NULL) << TRACEINDENT
                    << "Device Lost! GetDeviceRemovedReason(): "
                    << "before: " << (void*)(int64_t)result_before << " | "
                    << "after: "  << (void*)(int64_t)result_after  << "\n";
        error(NULL) << "!!! HALT !!!";
    }
}

static void* buffer_contents(d3d12_buffer* buffer)
{
    TRACELOG;
    halide_assert(NULL, !buffer->mapped);
    UINT Subresource = 0;
    const D3D12_RANGE* pReadRange = NULL;
    void* pData = NULL;
    HRESULT result = buffer->staging->Map(Subresource, pReadRange, &pData);
    if (D3DError(result, pData, NULL, "Unable to map Direct3D 12 staging buffer memory"))
        return(NULL);
    buffer->mapped = pData;
    return(pData);
#if 0
    return objc_msgSend(buffer, sel_getUid("contents"));
#endif
}

extern WEAK halide_device_interface_t d3d12compute_device_interface;

volatile int WEAK thread_lock = 0;

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    d3d12_library *library;
    module_state *next;
};
WEAK module_state* state_list = NULL;

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
WEAK int halide_d3d12compute_acquire_context(void* user_context, halide_d3d12compute_device** device_ret,
                                      halide_d3d12compute_command_queue** queue_ret, bool create)
{
    TRACELOG;

    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) { }

#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif

    if (create && (NULL == device))
    {
        debug(user_context) << TRACEINDENT << "D3D12Compute - Allocating: D3D12CreateSystemDefaultDevice\n";
        device = D3D12CreateSystemDefaultDevice(user_context);
        if (NULL == device)
        {
            error(user_context) << TRACEINDENT << "D3D12Compute: cannot allocate system default device.\n";
            __sync_lock_release(&thread_lock);
            return -1;
        }
        debug(user_context) << TRACEINDENT << "D3D12Compute - Allocating: new_command_queue\n";
        queue = new_command_queue(device);
        if (NULL == queue)
        {
            error(user_context) << TRACEINDENT << "D3D12Compute: cannot allocate command queue.\n";
            release_ns_object(device);
            device = NULL;
            __sync_lock_release(&thread_lock);
            return -1;
        }
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

class D3D12ContextHolder
{
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

WEAK void D3D12ContextHolder::restore()
{
    halide_d3d12compute_release_context(user_context);
}

struct command_list_completed_handler_block_descriptor_1 {
    unsigned long reserved;
    unsigned long block_size;
};

struct command_list_completed_handler_block_literal {
    void *isa;
    int flags;
    int reserved;
    void (*invoke)(command_list_completed_handler_block_literal *, d3d12_command_list* cmdList);
    struct command_list_completed_handler_block_descriptor_1 *descriptor;
};

WEAK command_list_completed_handler_block_descriptor_1 command_list_completed_handler_descriptor = {
    0, sizeof(command_list_completed_handler_block_literal)
};

static void command_list_completed_handler_invoke(command_list_completed_handler_block_literal* block, d3d12_command_list* cmdList)
{
    TRACELOG;
    objc_id buffer_error = command_list_error(cmdList);
    if (buffer_error != NULL) {
        ns_log_object(buffer_error);
        release_ns_object(buffer_error);
    }
}

WEAK command_list_completed_handler_block_literal command_list_completed_handler_block = {
    &_NSConcreteGlobalBlock,
    (1 << 28) | (1 << 29), // BLOCK_IS_GLOBAL | BLOCK_HAS_DESCRIPTOR
    0, command_list_completed_handler_invoke,
    &command_list_completed_handler_descriptor
};

}}}} // namespace Halide::Runtime::Internal::D3D12Compute

using namespace Halide::Runtime::Internal::D3D12Compute;

extern "C" {


WEAK int halide_d3d12compute_device_malloc(void *user_context, halide_buffer_t* buf)
{
    TRACELOG;

    debug(user_context) << TRACEINDENT
                        << "(user_context: " << user_context
                        << ", buf: " << buf << ")\n";

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

    debug(user_context) << TRACEINDENT << "allocating " << *buf << "\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    d3d12_buffer* d3d12_buf = new_buffer(d3d12_context.device, size);
    if (d3d12_buf == 0) {
        error(user_context) << "D3d12: Failed to allocate buffer of size " << (int64_t)size << ".\n";
        return -1;
    }

    if (0 != wrap_buffer(user_context, buf, d3d12_buf))
    {
        error(user_context) << "D3d12: unable to wrap halide buffer and D3D12 buffer.\n";
        return -1;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT << "Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_d3d12compute_device_free(void *user_context, halide_buffer_t* buf) {
    TRACELOG;

    debug(user_context) << TRACEINDENT
                        << "halide_d3d12compute_device_free called on buf "
                        << buf << " device is " << buf->device << "\n";

    if (buf->device == 0) {
        return 0;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    d3d12_buffer *d3d12_buf = (d3d12_buffer *)buf->device;
    release_ns_object(d3d12_buf);

    halide_d3d12compute_detach_buffer(user_context, buf);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT << "Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_d3d12compute_initialize_kernels(void *user_context, void **state_ptr, const char* source, int source_size) {
    TRACELOG;

    // Create the state object if necessary. This only happens once, regardless
    // of how many times halide_initialize_kernels/halide_release is called.
    // halide_release traverses this list and releases the module objects, but
    // it does not modify the list nodes created/inserted here.
    module_state*& state = *(module_state**)state_ptr;
    if (!state)
    {
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

    if (NULL == state->library)
    {
        #ifdef DEBUG_RUNTIME
        uint64_t t_before_compile = halide_current_time_ns(user_context);
        #endif

        debug(user_context) << TRACEINDENT << "D3D12Compute - Allocating: new_library_with_source " << state->library << "\n";
        state->library = new_library_with_source(d3d12_context.device, source, source_size);
        if (state->library == 0) {
            error(user_context) << "D3D12Compute: new_library_with_source failed.\n";
            return -1;
        }

        #ifdef DEBUG_RUNTIME
        uint64_t t_after_compile = halide_current_time_ns(user_context);
        debug(user_context) << TRACEINDENT << "Time for halide_d3d12compute_initialize_kernels compilation: " << (t_after_compile - t_before_compile) / 1.0e6 << " ms\n";
        #endif
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT << "Time for halide_d3d12compute_initialize_kernels: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

namespace {

inline void halide_d3d12compute_device_sync_internal(d3d12_device* device, struct halide_buffer_t* buffer)
{
    TRACELOG;
    // NOTE(marcos): ideally, a copy engine command list would be ideal here,
    // but it would also require a copy engine queue to submit it... for now
    // just use a single compute queue for everything..
    //d3d12_command_allocator* sync_command_allocator = new_command_allocator<D3D12_COMMAND_LIST_TYPE_COPY>(device);
    //d3d12_copy_command_list* blitCmdList = new_copy_command_list(device, sync_command_allocator);
    d3d12_command_allocator* sync_command_allocator = new_command_allocator<D3D12_COMMAND_LIST_TYPE_COMPUTE>(device);
    d3d12_compute_command_list* blitCmdList = new_compute_command_list(device, sync_command_allocator);
    if (buffer != NULL)
    {
        d3d12_buffer* d3d12_buffer = (struct d3d12_buffer*)buffer->device;
        if (is_buffer_managed(d3d12_buffer))
        {
            halide_assert(NULL, d3d12_buffer->mapped);
            ID3D12Resource* resource = d3d12_buffer->resource;
            UINT Subresource = 0;
            D3D12_RANGE* pWrittenRange = NULL;
            resource->Unmap(Subresource, pWrittenRange);
            synchronize_resource(blitCmdList, d3d12_buffer);
            //end_recording(blitCmdList);
        }
    }
    end_recording(blitCmdList);
    commit_command_list(blitCmdList);
    wait_until_completed(blitCmdList);
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
    debug(user_context) << TRACEINDENT << "Time for halide_d3d12compute_device_sync: " << (t_after - t_before) / 1.0e6 << " ms\n";
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
        halide_d3d12compute_device_sync_internal(device, NULL);

        // Unload the modules attached to this device. Note that the list
        // nodes themselves are not freed, only the program objects are
        // released. Subsequent calls to halide_init_kernels might re-create
        // the program object using the same list node to store the program
        // object.
        module_state *state = state_list;
        while (state) {
          if (state->library) {
                debug(user_context) << "D3D12Compute - Releasing: new_library_with_source " << state->library << "\n";
                release_ns_object(state->library);
                state->library = NULL;
            }
            state = state->next;
        }

        // Release the device itself, if we created it.
        if (acquired_device == device) {
            debug(user_context) <<  "D3D12Compute - Releasing: new_command_queue " << queue << "\n";
            release_ns_object(queue);
            queue = NULL;

            debug(user_context) << "D3D12Compute - Releasing: D3D12CreateSystemDefaultDevice " << device << "\n";
            release_ns_object(device);
            device = NULL;
        }
    }

    halide_d3d12compute_release_context(user_context);

    return 0;
}

WEAK int halide_d3d12compute_copy_to_device(void *user_context, halide_buffer_t* buffer)
{
    TRACELOG;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0)
    {
        return d3d12_context.error;
    }

    halide_assert(user_context, buffer->host && buffer->device);

    device_copy c = make_host_to_device_copy(buffer);
    halide_assert(user_context, c.dst == buffer->device);
    d3d12_buffer* copy_dst = reinterpret_cast<d3d12_buffer*>(c.dst);
    void* dst_data = buffer_contents(copy_dst);
    c.dst = reinterpret_cast<uint64_t>(dst_data);

    debug(user_context) << TRACEINDENT
                        << "halide_d3d12compute_copy_to_device dev = " << (void*)buffer->device
                        << " d3d12_buffer = " << copy_dst
                        << " host = " << buffer->host
                        << "\n";

    copy_memory(c, user_context);

    if (is_buffer_managed(copy_dst))
    {
        size_t total_size = buffer->size_in_bytes();
        halide_assert(user_context, total_size != 0);
        NSRange total_extent;
        total_extent.location = 0;
        total_extent.length = total_size;
        did_modify_range(copy_dst, total_extent);
    }
    halide_d3d12compute_device_sync_internal(d3d12_context.device, buffer);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT
                        << "Time for halide_d3d12compute_copy_to_device: "
                        << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_d3d12compute_copy_to_host(void *user_context, halide_buffer_t* buffer) {
    TRACELOG;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    halide_d3d12compute_device_sync_internal(d3d12_context.device, buffer);

    halide_assert(user_context, buffer->host && buffer->device);
    halide_assert(user_context, buffer->dimensions <= MAX_COPY_DIMS);
    if (buffer->dimensions > MAX_COPY_DIMS) {
        return -1;
    }

    device_copy c = make_device_to_host_copy(buffer);
    d3d12_buffer* copy_src = reinterpret_cast<d3d12_buffer*>(c.src);
    void* src_data = buffer_contents(copy_src);
    c.src = reinterpret_cast<uint64_t>(src_data);

    copy_memory(c, user_context);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT << "Time for halide_d3d12compute_copy_to_host: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_d3d12compute_run(void *user_context,
                           void *state_ptr,
                           const char* entry_name,
                           int blocksX, int blocksY, int blocksZ,
                           int threadsX, int threadsY, int threadsZ,
                           int shared_mem_bytes,
                           size_t arg_sizes[],
                           void* args[],
                           int8_t arg_is_buffer[],
                           int num_attributes,
                           float* vertex_buffer,
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

    d3d12_device* device = d3d12_context.device;

    d3d12_command_allocator* command_allocator = new_command_allocator<D3D12_COMMAND_LIST_TYPE_COMPUTE>(device);
    if (command_allocator == 0) {
        error(user_context) << "D3D12Compute: Could not create compute command allocator.\n";
        return -1;
    }

    d3d12_compute_command_list* cmdList = new_compute_command_list(device, command_allocator);
    if (cmdList == 0) {
        error(user_context) << "D3D12Compute: Could not create compute command list.\n";
        return -1;
    }

    halide_assert(user_context, state_ptr);
    module_state *state = (module_state*)state_ptr;

    d3d12_function* function = new_function_with_name(device, state->library, entry_name, strlen(entry_name));
    halide_assert(user_context, function);

    // TODO(marcos): seems like a good place to create the descriptor heaps and tables...
    d3d12_binder* binder = new_descriptor_binder(device);

    // pack all non-buffer arguments into a single allocation block:
    size_t total_args_size = 0;
    for (size_t i = 0; arg_sizes[i] != 0; i++)
    {
        if (arg_is_buffer[i])
            continue;
        // Metal requires natural alignment for all types in structures.
        // Assert arg_size is exactly a power of two and adjust size to start
        // on the next multiple of that power of two.
        halide_assert(user_context, (arg_sizes[i] & (arg_sizes[i] - 1)) == 0);
        total_args_size = (total_args_size + arg_sizes[i] - 1) & ~(arg_sizes[i] - 1);
        total_args_size += arg_sizes[i];
    }
    d3d12_buffer* args_buffer = NULL;
    if (total_args_size > 0)
    {
        // Direct3D 12 expects constant buffers to have sizes multiple of 256:
        size_t constant_buffer_size = (total_args_size + 255) & ~255;
        args_buffer = new_buffer(d3d12_context.device, constant_buffer_size);
        if (args_buffer == 0)
        {
            error(user_context) << "D3D12Compute: Could not allocate arguments buffer.\n";
            release_ns_object(function);
            return -1;
        }
        char* args_ptr = (char*)buffer_contents(args_buffer);
        size_t offset = 0;
        for (size_t i = 0; arg_sizes[i] != 0; i++)
        {
            if (arg_is_buffer[i])
                continue;
            memcpy(&args_ptr[offset], args[i], arg_sizes[i]);
            offset = (offset + arg_sizes[i] - 1) & ~(arg_sizes[i] - 1);
            offset += arg_sizes[i];
        }
        halide_assert(user_context, offset == total_args_size);
    }

    // setup/bind the argument buffer, if arguments have indeed been packed:
    int32_t buffer_index = 0;
    if (args_buffer)
    {
        set_input_buffer(cmdList, binder, args_buffer, buffer_index);
        release_ns_object(args_buffer);
        buffer_index++;
    }

    // setup/bind actual buffers:
    for (size_t i = 0; arg_sizes[i] != 0; i++)
    {
        if (!arg_is_buffer[i])
            continue;
        halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
        uint64_t handle = ((halide_buffer_t *)args[i])->device;
        d3d12_buffer* buffer = (d3d12_buffer *)handle;
        set_input_buffer(cmdList, binder, buffer, buffer_index);
        buffer_index++;
    }

    d3d12_compute_pipeline_state* pipeline_state = new_compute_pipeline_state_with_function(d3d12_context.device, function);
    if (pipeline_state == 0) {
        error(user_context) << "D3D12Compute: Could not allocate pipeline state.\n";
        release_ns_object(function);
        return -1;
    }
    set_compute_pipeline_state(cmdList, pipeline_state, function, binder);

    // Round shared memory size up to a multiple of 16, as required by setThreadgroupMemoryLength.
    shared_mem_bytes = (shared_mem_bytes + 0xF) & ~0xF;
    debug(user_context) << TRACEINDENT << "Setting shared memory length to " << shared_mem_bytes << "\n";
    set_threadgroup_memory_length(cmdList, shared_mem_bytes, 0);

    dispatch_threadgroups(cmdList,
                          blocksX, blocksY, blocksZ,
                          threadsX, threadsY, threadsZ);
    end_recording(cmdList);

    add_command_list_completed_handler(cmdList, &command_list_completed_handler_block);

    commit_command_list(cmdList);

    wait_until_completed(cmdList);  // TODO(marcos): find a way to gracefully handle this hard wait...

    release_ns_object(pipeline_state);
    release_ns_object(function);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT << "Time for halide_d3d12compute_device_run: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_d3d12compute_device_and_host_malloc(void *user_context, struct halide_buffer_t *buffer) {
    TRACELOG;
    debug(user_context) << TRACEINDENT << "halide_d3d12compute_device_and_host_malloc called.\n";
    int result = halide_d3d12compute_device_malloc(user_context, buffer);
    if (result == 0) {
        d3d12_buffer *metal_buffer = (d3d12_buffer *)(buffer->device);
        buffer->host = (uint8_t *)buffer_contents(metal_buffer);
        debug(user_context) << TRACEINDENT 
                            << "halide_d3d12compute_device_and_host_malloc"
                            << " device = " << (void*)buffer->device
                            << " metal_buffer = " << metal_buffer
                            << " host = " << buffer->host << "\n";
    }
    return result;
}

WEAK int halide_d3d12compute_device_and_host_free(void *user_context, struct halide_buffer_t *buffer) {
    TRACELOG;
    debug(user_context) << TRACEINDENT << "halide_d3d12compute_device_and_host_free called.\n";
    halide_d3d12compute_device_free(user_context, buffer);
    buffer->host = NULL;
    return 0;
}

WEAK int halide_d3d12compute_device_crop(void *user_context,
                                         const struct halide_buffer_t *src,
                                         struct halide_buffer_t *dst) {
    TRACELOG;
    debug(user_context) << TRACEINDENT << "halide_d3d12compute_device_crop called.\n";
/*
    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    dst->device_interface = src->device_interface;
    int64_t offset = 0;
    for (int i = 0; i < src->dimensions; i++) {
        offset += (dst->dim[i].min - src->dim[i].min) * src->dim[i].stride;
    }
    offset *= src->type.bytes();

    device_handle *new_handle = (device_handle *)malloc(sizeof(device_handle));
    if (new_handle == NULL) {
        error(user_context) << "halide_metal_device_crop: malloc failed making device handle.\n";
        return halide_error_code_out_of_memory;
    }

    retain_ns_object(((device_handle *)src->device)->buf);
    new_handle->buf = ((device_handle *)src->device)->buf;
    new_handle->offset = ((device_handle *)src->device)->offset + offset;
    dst->device = (uint64_t)new_handle;
*/
    return 0;
}

WEAK int halide_d3d12compute_device_release_crop(void *user_context,
                                                 struct halide_buffer_t *buf) {
    // Basically the same code as in halide_metal_device_free, but with
    // enough differences to require separate code.

    TRACELOG;
    debug(user_context) << TRACEINDENT 
                        << "halide_d3d12compute_device_release_crop called on buf "
                        << buf << " device is " << buf->device << "\n";
    if (buf->device == 0) {
        return 0;
    }
    /*
    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    device_handle *handle = (device_handle *)buf->device;
    
    release_ns_object(handle->buf);
    free(handle);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT << "Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif
    */
    return 0;
}

WEAK int halide_d3d12compute_wrap_buffer(void* user_context, struct halide_buffer_t* halide_buf, uint64_t device_buf_handle)
{
    TRACELOG;

    halide_assert(user_context, halide_buf->device == 0);
    if (halide_buf->device != 0)
    {
        return -2;
    }

    d3d12_buffer* d3d12_buf = reinterpret_cast<d3d12_buffer*>(device_buf_handle);
    halide_assert(user_context, d3d12_buf->halide == 0);
    d3d12_buf->halide = halide_buf;

    halide_buf->device = device_buf_handle;
    halide_buf->device_interface = &d3d12compute_device_interface;
    halide_buf->device_interface->impl->use_module();

    if (halide_buf->device == 0)
    {
        return -1;
    }

    return 0;
}

WEAK int halide_d3d12compute_detach_buffer(void* user_context, struct halide_buffer_t* buf)
{
    TRACELOG;

    if (buf->device == 0)
    {
        return 0;
    }

    uint64_t device_buf_handle = buf->device;
    d3d12_buffer* d3d12_buf = reinterpret_cast<d3d12_buffer*>(device_buf_handle);
    halide_assert(user_context, d3d12_buf->halide != NULL);
    d3d12_buf->halide = NULL;

    halide_assert(user_context, buf->device_interface != 0);
    halide_assert(user_context, buf->device_interface == &d3d12compute_device_interface);
    buf->device_interface->impl->release_module();
    buf->device_interface = NULL;
    buf->device = 0;

    return 0;
}

WEAK uintptr_t halide_d3d12compute_get_buffer(void *user_context, struct halide_buffer_t *buf) {
    TRACELOG;
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &d3d12compute_device_interface);
    return (uintptr_t)(buf->device);
}

WEAK const struct halide_device_interface_t *halide_d3d12compute_device_interface() {
    return &d3d12compute_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void halide_d3d12compute_cleanup() {
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
    halide_default_buffer_copy,
    halide_d3d12compute_device_crop,
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
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    &d3d12compute_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::D3D12Compute
