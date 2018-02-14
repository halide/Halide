#ifndef DEBUG_RUNTIME
#define DEBUG_RUNTIME   1   // for debug(NULL) print to work...
#endif//DEBUG_RUNTIME

#define HALIDE_D3D12_TRACE          (1)
#define HALIDE_D3D12_DEBUG_RUNTIME  (1)
#define HALIDE_D3D12_DEBUG_SHADERS  (1)
#define HALIDE_D3D12_PIX            (0)
#define HALIDE_D3D12_RENDERDOC      (0)

#include "HalideRuntimeD3D12Compute.h"
#include "scoped_spin_lock.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"

template<uint64_t length = 1024, int type = BasicPrinter>
class StackPrinter : public Printer<type, length>
{
public:
    StackPrinter(void* ctx = NULL) : Printer<type, length>(ctx, buffer) { }
    StackPrinter& operator () (void* ctx = NULL)
    {
        this->user_context = ctx;
        return(*this);
    }
private:
    char buffer [length];
};

#if HALIDE_D3D12_TRACE && (defined(DEBUG_RUNTIME) && DEBUG_RUNTIME)
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
    #define TRACEPRINT(msg) debug(user_context) << TRACEINDENT << msg;
    #define TRACELOG        TRACEPRINT("[@]" << __FUNCTION__ << "\n"); TraceLogScope trace_scope___;
#else
    #define TRACEINDENT ""
    #define TRACEPRINT(msg)
    #define TRACELOG
#endif

#define d3d12_load_library          halide_load_library
#define d3d12_get_library_symbol    halide_get_library_symbol

#define d3d12_debug_break (*((volatile int8_t*)NULL) = 0)

#define WIN32API
extern "C" WIN32API void* LoadLibraryA(const char *);
extern "C" WIN32API void* GetProcAddress(void *, const char *);

void* ll(const char *name)
{
    //fprintf(stdout, "ll(%s)\n", name);
    void* lib = LoadLibraryA(name);
    //debug(user_context) << TRACEINDENT << "ll(" << name << ") = " << lib <<"\n";
    return((void*)lib);
}
void* gpa(void* lib, const char *name)
{
    //fprintf(stdout, "gpa(%p, %s)\n", lib, name);
    void* proc = GetProcAddress(lib, name);
    //debug(user_context) << TRACEINDENT << "gpa(" << lib << ", " << name << ") = " << proc << "\n";
    return((void*)proc);
}

#ifndef UNUSED
#define UNUSED(x) ((void)x)
#endif//UNUSED

#if !defined(INITGUID)
    #define  INITGUID
#endif
#if !defined(COBJMACROS)
    #define  COBJMACROS
#endif
#define HALIDE_D3D12_APPLY_ABI_PATCHES (1)  // keep this def reserved for future use...
#include "mini_d3d12.h"
#include "d3d12_abi_patch_64.h"

static void* const user_context = NULL;   // in case there's no user context available in the scope of a function

#if HALIDE_D3D12_RENDERDOC && HALIDE_D3D12_DEBUG_RUNTIME
    #pragma message "RenderDoc might now work well along with the Dirct3D debug layers..."
#endif

#if HALIDE_D3D12_RENDERDOC
#define WIN32
#define RenderDocAssert(expr)           halide_assert(user_context, expr)
#define LoadRenderDocLibrary(dll)       d3d12_load_library(dll)
#define GetRenderDocProcAddr(dll,proc)  d3d12_get_library_symbol(dll, proc)
#define RENDERDOC_NO_STDINT
#define RENDERDOC_AUTOINIT              (0)
#include "renderdoc/RenderDocGlue.h"
#endif

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

D3D12TYPENAME(IDXGIFactory1)
D3D12TYPENAME(IDXGIAdapter1)
D3D12TYPENAME(IDXGIOutput)

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

    halide_assert(user_context, (type.code >= 0) && (type.code <= 2));
    halide_assert(user_context, (type.lanes > 0) && (type.lanes <= 4));

    int i = 0;
    switch (type.bytes())
    {
        case 1  : i = 0; break;
        case 2  : i = 1; break;
        case 4  : i = 2; break;
        case 8  : i = 3; break;
        default : halide_assert(user_context, false);  break;
    }

    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    format = FORMATS[(int)type.code][type.lanes-1][i];
    return(format);
}

#define INLINE inline __attribute__((always_inline))



// The default implementation of halide_d3d12_get_symbol attempts to load
// the D3D12 runtime shared library/DLL, and then get the symbol from it.
static void* lib_d3d12  = NULL;
static void* lib_D3DCompiler_47 = NULL;
static void* lib_dxgi = NULL;

struct LibrarySymbol
{
    template<typename T>
    operator T () { return((T)symbol); }
    void* symbol;

    static LibrarySymbol get(void* user_context, void* lib, const char* name)
    {
        void* s = d3d12_get_library_symbol(lib, name);
        if (!s)
        {
            error(user_context) << "Symbol not found: " << name << "\n";
        }
        debug(user_context) << TRACEINDENT << "Symbol '" << name << "' found at " << s << "\n";
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

UUIDOF(IDXGIFactory1)
UUIDOF(IDXGIAdapter1)
UUIDOF(IDXGIOutput)
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

struct d3d12_buffer
{
    ID3D12Resource* resource;
    UINT sizeInBytes;
    UINT offset;        // FirstElement
    UINT elements;      // NumElements
    UINT offsetInBytes;
    DXGI_FORMAT format;
    D3D12_RESOURCE_STATES state;

    volatile uint64_t ref_count;

    enum
    {
        Unknown = 0,
        Constant,
        ReadWrite,
        ReadOnly,
        WriteOnly,
        Upload,
        ReadBack
    } type;

    halide_buffer_t* halide;

    void* mapped;

    struct staging_t
    {
        d3d12_buffer* buffer;
        size_t offset;
        size_t size;
    }* staging;

    bool mallocd;

    operator bool() const { return(NULL != resource); }
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
    ID3DBlob* shaderBlob;
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
    return( halide_d3d12compute_wrap_buffer(user_context, buf, raw) );
}

WEAK d3d12_device* device = NULL;
WEAK d3d12_command_queue* queue = NULL;
WEAK ID3D12RootSignature* rootSignature = NULL;

WEAK d3d12_buffer upload   = { };   // staging buffer to transfer data to the device
WEAK d3d12_buffer readback = { };   // staging buffer to retrieve data from the device

template<typename d3d12_T>
static void release_d3d12_object(d3d12_T* obj)
{
    TRACELOG;
    debug(user_context) << TRACEINDENT << "!!!!!!!!!! RELEASING UNKNOWN D3D12 OBJECT !!!!!!!!!!\n";
}

template<typename d3d12_T>
static void release_object(d3d12_T* obj)
{
    TRACELOG;
    if (!obj)
    {
        TRACEPRINT("null object -- nothing to be released.\n");
        return;
    }
    release_d3d12_object(obj);
}

template<typename ID3D12T>
static void Release_ID3D12Object(ID3D12T* obj)
{
    TRACELOG;
    TRACEPRINT(d3d12typename(obj) << "\n");
    if (obj)
    {
        obj->Release();
    }
}

template<>
void release_d3d12_object<d3d12_device>(d3d12_device* device)
{
    TRACELOG;
    ID3D12Device* p = (*device);
    Release_ID3D12Object(p);
}

template<>
void release_d3d12_object<d3d12_command_queue>(d3d12_command_queue* queue)
{
    TRACELOG;
    Release_ID3D12Object(queue->p);
    Release_ID3D12Object(queue->fence);
    free(queue);
}

template<>
void release_d3d12_object<d3d12_command_allocator>(d3d12_command_allocator* cmdAllocator)
{
    TRACELOG;
    ID3D12CommandAllocator* p = (*cmdAllocator);
    Release_ID3D12Object(p);
}

template<>
void release_d3d12_object<d3d12_command_list>(d3d12_command_list* cmdList)
{
    TRACELOG;
    Release_ID3D12Object(cmdList->p);
    free(cmdList);
}

template<>
void release_d3d12_object<d3d12_binder>(d3d12_binder* binder)
{
    TRACELOG;
    Release_ID3D12Object(binder->descriptorHeap);
    free(binder);
}

template<>
void release_d3d12_object<d3d12_buffer>(d3d12_buffer* buffer)
{
    TRACELOG;
    Release_ID3D12Object(buffer->resource);
    if (buffer->mallocd)
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
    Release_ID3D12Object(function->shaderBlob);
    Release_ID3D12Object(function->rootSignature);
    free(function);
}

template<>
void release_d3d12_object<d3d12_compute_pipeline_state>(d3d12_compute_pipeline_state* pso)
{
    TRACELOG;
    ID3D12PipelineState* p = *(pso);
    Release_ID3D12Object(p);
}

static void D3D12LoadDependencies(void* user_context)
{
    TRACELOG;

    //halide_set_custom_load_library(ll);
    //halide_set_custom_get_library_symbol(gpa);

    const char* lib_names [] = {
        "d3d12.dll",
        "D3DCompiler_47.dll",
        "dxgi.dll",
    };
    static const int num_libs = sizeof(lib_names) / sizeof(lib_names[0]);
    void** lib_handles [num_libs] = {
        &lib_d3d12,
        &lib_D3DCompiler_47,
        &lib_dxgi,
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
        lib = d3d12_load_library(lib_names[i]);
        if (lib)
        {
            debug(user_context) << TRACEINDENT << "Loaded runtime library '" << lib_names[i] << "' at location " << lib << "\n";
        }
        else
        {
            error(user_context) << TRACEINDENT << "Unable to load runtime library: " << lib_names[i] << "\n";
        }
    }

    PFN_D3D12_CREATE_DEVICE D3D12CreateDevice1 = LibrarySymbol::get(user_context, lib_d3d12, "D3D12CreateDevice"); UNUSED(D3D12CreateDevice1);

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
static void D3D12WaitForPix()
{
    TRACELOG;
    TRACEPRINT("[[ delay for attaching to PIX... ]]\n");
    volatile uint32_t x = (1 << 31);
    while (--x > 0)
        ;
}
#endif

static d3d12_device* D3D12CreateSystemDefaultDevice(void* user_context)
{
    TRACELOG;

    D3D12LoadDependencies(user_context);

    HRESULT result = E_UNEXPECTED;

#if HALIDE_D3D12_DEBUG_RUNTIME
    TRACEPRINT("Using Direct3D 12 Debug Layer\n");
    ID3D12Debug* d3d12Debug = NULL;
    result = D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12Debug));
    if (D3DError(result, d3d12Debug, user_context, "Unable to retrieve the debug interface for Direct3D 12"))
        return(NULL);
    d3d12Debug->EnableDebugLayer();
#endif

    IDXGIFactory1* dxgiFactory = NULL;
    result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    if (D3DError(result, dxgiFactory, user_context, "Unable to create DXGI Factory (IDXGIFactory1)"))
        return(NULL);

    IDXGIAdapter1* dxgiAdapter = NULL;
    for (int i = 0; ; ++i)
    {
        IDXGIAdapter1* adapter = NULL;
        HRESULT result = dxgiFactory->EnumAdapters1(i, &adapter);
        #define DXGI_ERROR_NOT_FOUND 0x887A0002
        if (DXGI_ERROR_NOT_FOUND == result)
            break;
        if (D3DError(result, adapter, user_context, "Unable to enumerate DXGI adapter (IDXGIAdapter1)."))
            return(NULL);
        DXGI_ADAPTER_DESC1 desc = { };
        if (FAILED(adapter->GetDesc1(&desc)))
        {
            error(user_context) << "Unable to retrieve information (DXGI_ADAPTER_DESC1) about adapter number #" << i;
            return(NULL);
        }
        char Description [128];
        for (int i = 0; i < 128; ++i)
            Description[i] = desc.Description[i];
        TRACEPRINT("-- Adapter #" << i << ": " << Description << "\n");
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            TRACEPRINT("-- this is a software adapter (skipping)\n");
            adapter->Release();
            continue;
        }
        // TODO(marcos): find a strategy to select the best adapter available;
        // unfortunately, most of the adapter capabilities can only be queried
        // after a logical device for it is created...
        // (see: ID3D12Device::CheckFeatureSupport)
        if (dxgiAdapter)
            dxgiAdapter->Release();
        dxgiAdapter = adapter;
        break;  // <- for now, just pick the first (non-software) adapter
    }

    if (NULL == dxgiAdapter)
    {
        error(user_context) << "Unable to find a suitable D3D12 Adapter.\n";
        return(NULL);
    }

#if 0
    // NOTE(marcos): ignoring IDXGIOutput setup since this back-end is compute only
    IDXGIOutput* dxgiDisplayOutput = NULL;
    result = dxgiAdapter->EnumOutputs(0, &dxgiDisplayOutput);
    if (D3DError(result, dxgiDisplayOutput, user_context, "Unable to enumerate DXGI outputs for adapter (IDXGIOutput)"))
        return(NULL);
#endif

    ID3D12Device* device = NULL;
    D3D_FEATURE_LEVEL MinimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    result = D3D12CreateDevice(dxgiAdapter, MinimumFeatureLevel, IID_PPV_ARGS(&device));
    if (D3DError(result, device, user_context, "Unable to create the Direct3D 12 device"))
        return(NULL);

    dxgiAdapter->Release();
    dxgiFactory->Release();

    #if HALIDE_D3D12_PIX
    D3D12WaitForPix();
    #endif

    return(reinterpret_cast<d3d12_device*>(device));
}

ID3D12RootSignature* D3D12CreateMasterRootSignature(ID3D12Device* device)
{
    TRACELOG;

    // A single "master" root signature is suitable for all Halide kernels:
    // ideally, we would like to use "unbounded tables" for the descriptor
    // binding, but "tier-1" d3d12 devices (D3D12_RESOURCE_BINDING_TIER_1)
    // do not support unbounded descriptor tables...

    D3D12_ROOT_PARAMETER TableTemplate = { };
        TableTemplate.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        TableTemplate.DescriptorTable.NumDescriptorRanges = 1;
        TableTemplate.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;   // compute must use this
    D3D12_DESCRIPTOR_RANGE RangeTemplate = { };
        RangeTemplate.NumDescriptors = 25;
        RangeTemplate.BaseShaderRegister = 0;
        RangeTemplate.RegisterSpace = 0;
        RangeTemplate.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameterTables [NumSlots] = { };
    // UAVs: read-only, write-only and read-write buffers:
        D3D12_ROOT_PARAMETER& RootTableUAV = rootParameterTables[UAV];
            RootTableUAV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE UAVs = RangeTemplate;
                UAVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                UAVs.NumDescriptors = 16;     // tier-1 limit: 16 UAVs
            RootTableUAV.DescriptorTable.pDescriptorRanges = &UAVs;
    // CBVs: read-only uniform/coherent/broadcast buffers:
        D3D12_ROOT_PARAMETER& RootTableCBV = rootParameterTables[CBV];
            RootTableCBV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE CBVs = RangeTemplate;
                CBVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                CBVs.NumDescriptors = 14;     // tier-1 limit: 14 CBVs
            RootTableCBV.DescriptorTable.pDescriptorRanges = &CBVs;
    // SRVs: textures and read-only buffers:
        D3D12_ROOT_PARAMETER& RootTableSRV = rootParameterTables[SRV];
            RootTableSRV = TableTemplate;
            D3D12_DESCRIPTOR_RANGE SRVs = RangeTemplate;
                SRVs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                SRVs.NumDescriptors = 25;     // tier-1 limit: 128 SRVs
            RootTableSRV.DescriptorTable.pDescriptorRanges = &SRVs;

    D3D12_ROOT_SIGNATURE_DESC rsd = { };
        rsd.NumParameters = NumSlots;
        rsd.pParameters = rootParameterTables;
        rsd.NumStaticSamplers = 0;
        rsd.pStaticSamplers = NULL;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    D3D_ROOT_SIGNATURE_VERSION Version = D3D_ROOT_SIGNATURE_VERSION_1;
    ID3DBlob* pSignBlob  = NULL;
    ID3DBlob* pSignError = NULL;
    HRESULT result = D3D12SerializeRootSignature(&rsd, Version, &pSignBlob, &pSignError);
    if (D3DError(result, pSignBlob, NULL, "Unable to serialize the Direct3D 12 root signature"))
    {
        halide_assert(user_context, pSignError);
        error(user_context) << (const char*)pSignError->GetBufferPointer();
        return(NULL);
    }

    ID3D12RootSignature* rootSignature = NULL;
    UINT nodeMask = 0;
    const void* pBlobWithRootSignature = pSignBlob->GetBufferPointer();
    SIZE_T blobLengthInBytes = pSignBlob->GetBufferSize();
    result = device->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, IID_PPV_ARGS(&rootSignature));
    if (D3DError(result, rootSignature, NULL, "Unable to create the Direct3D 12 root signature"))
        return(NULL);

    return(rootSignature);
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

WEAK d3d12_buffer new_buffer_resource(d3d12_device* device, size_t length, D3D12_HEAP_TYPE heaptype)
{
    d3d12_buffer buffer = { };

    ID3D12Resource* resource = NULL;

    D3D12_RESOURCE_DESC desc = { };
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

    D3D12_HEAP_PROPERTIES heapProps = { };              // CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_...)
        heapProps.Type = heaptype;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 0;                 // 0 is equivalent to 0b0...01 (single adapter)
        heapProps.VisibleNodeMask  = 0;                 // ditto

    D3D12_HEAP_PROPERTIES* pHeapProperties = &heapProps;
    D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    D3D12_RESOURCE_DESC* pDesc = &desc;
    D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_CLEAR_VALUE* pOptimizedClearValue = NULL;     // for buffers, this must be NULL

    switch (heaptype)
    {
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

    // A commited resource manages its own private heap
    HRESULT result = (*device)->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, IID_PPV_ARGS(&resource));
    if (D3DError(result, resource, NULL, "Unable to create the Direct3D 12 buffer"))
        return(buffer);

    buffer.resource = resource;
    buffer.sizeInBytes = length;
    buffer.state = InitialResourceState;
    buffer.type = d3d12_buffer::Unknown;
    buffer.format = DXGI_FORMAT_UNKNOWN;
    buffer.mallocd = false;
    __atomic_store_n(&buffer.ref_count, 0, __ATOMIC_SEQ_CST);

    return(buffer);
}

WEAK d3d12_buffer new_device_buffer(d3d12_device* device, size_t length)
{
    TRACELOG;
    // an upload heap would have been handy here since they are accessible both by CPU and GPU;
    // however, they are only good for streaming (write once, read once, discard, rinse and repeat) vertex and constant buffer data; for unordered-access views,
    // upload heaps are not allowed.
    d3d12_buffer buffer = new_buffer_resource(device, length, D3D12_HEAP_TYPE_DEFAULT);
    buffer.type = d3d12_buffer::ReadWrite;
    return(buffer);
}

// faux type name for logging purposes in D3DError()
struct ID3D12MemoryMappedResource;
D3D12TYPENAME(ID3D12MemoryMappedResource)

WEAK void* map_buffer(d3d12_buffer* buffer)
{
    TRACELOG;

    if (buffer->mapped)
        return(buffer->mapped);

    D3D12_RANGE readRange = { };
    switch (buffer->type)
    {
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
    ID3D12Resource* resource = buffer->resource;
    UINT Subresource = 0;   // buffers contain only one subresource (at index 0)
    const D3D12_RANGE* pReadRange = &readRange;
    void* pData = NULL;
    HRESULT result = resource->Map(Subresource, pReadRange, &pData);
    if (D3DError(result, (ID3D12MemoryMappedResource*)pData, NULL, "Unable to map Direct3D 12 staging buffer memory"))
        return(NULL);

    halide_assert(user_context, pData);
    buffer->mapped = pData;

    return(pData);
}

WEAK void unmap_buffer(d3d12_buffer* buffer)
{
    TRACELOG;

    void* pData = buffer->mapped;
    if (!pData)
        return;

    D3D12_RANGE writtenRange = { };
    switch (buffer->type)
    {
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
    ID3D12Resource* resource = buffer->resource;
    UINT Subresource = 0;   // buffers contain only one subresource (at index 0)
    const D3D12_RANGE* pWrittenRange = &writtenRange;
    resource->Unmap(Subresource, pWrittenRange);
    if (D3DError(/*S_OK*/HRESULT(0x0), (ID3D12MemoryMappedResource*)pData, NULL, "Unable to unmap Direct3D 12 staging buffer memory"))
        return;

    buffer->mapped = NULL;
}

WEAK d3d12_buffer new_upload_buffer(d3d12_device* device, size_t length)
{
    TRACELOG;
    d3d12_buffer buffer = new_buffer_resource(device, length, D3D12_HEAP_TYPE_UPLOAD);
    buffer.type = d3d12_buffer::Upload;
    // upload heaps may keep the buffer mapped persistently
    map_buffer(&buffer);
    return(buffer);
}

WEAK d3d12_buffer new_readback_buffer(d3d12_device* device, size_t length)
{
    TRACELOG;
    d3d12_buffer buffer = new_buffer_resource(device, length, D3D12_HEAP_TYPE_READBACK);
    buffer.type = d3d12_buffer::ReadBack;
    return(buffer);
}

WEAK d3d12_buffer new_constant_buffer(d3d12_device* device, size_t length)
{
    TRACELOG;
    // CBV buffer can simply use an upload heap for host and device memory:
    d3d12_buffer buffer = new_upload_buffer(device, length);
    buffer.type = d3d12_buffer::Constant;
    return(buffer);
}

WEAK d3d12_buffer* new_buffer(d3d12_device* device, size_t length)
{
    TRACELOG;

    d3d12_buffer buffer = new_device_buffer(device, length);

    d3d12_buffer* pBuffer = malloct<d3d12_buffer>();
    *pBuffer = buffer;
    pBuffer->mallocd = true;
    return(pBuffer);
}

WEAK d3d12_command_queue* new_command_queue(d3d12_device* device)
{
    TRACELOG;

    ID3D12CommandQueue* commandQueue = NULL;
    {
        D3D12_COMMAND_QUEUE_DESC cqDesc = { };
            //cqDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
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
    halide_assert(user_context, device);
    ID3D12CommandAllocator* commandAllocator = NULL;
    HRESULT result = (*device)->CreateCommandAllocator(Type, IID_PPV_ARGS(&commandAllocator));
    if (D3DError(result, commandAllocator, NULL, "Unable to create the Direct3D 12 command allocator"))
        return(NULL);
    return(reinterpret_cast<d3d12_command_allocator*>(commandAllocator));
}

WEAK void add_command_list_completed_handler(d3d12_command_list* cmdList, struct command_list_completed_handler_block_literal* handler)
{
    TRACELOG;
    TRACEPRINT("... ignoring ...\n");
    // there's no equivalent to Metal's 'addCompletedHandler' in D3D12...
    // the only sensible way to emulate it would be to associate an event object
    // with a fence via ID3D12Fence::SetEventOnCompletion() and have a background
    // thread wait on that event and invoke the callback
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

    Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable((*cmdList), UAV, binder->GPU[UAV]);
    Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable((*cmdList), CBV, binder->GPU[CBV]);
    Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable((*cmdList), SRV, binder->GPU[SRV]);
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
    TRACEPRINT("descriptor handle increment size: " << descriptorSize << "\n");

    d3d12_binder* binder = malloct<d3d12_binder>();
    binder->descriptorHeap = descriptorHeap;
    binder->descriptorSize = descriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE baseCPU = Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptorHeap);
    TRACEPRINT("descriptor heap base for CPU: " << baseCPU.ptr << "\n");
    binder->CPU[UAV].ptr = baseCPU.ptr +  0*descriptorSize;
    binder->CPU[CBV].ptr = baseCPU.ptr + 25*descriptorSize;
    binder->CPU[SRV].ptr = baseCPU.ptr + 50*descriptorSize;

    D3D12_GPU_DESCRIPTOR_HANDLE baseGPU = Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptorHeap);
    TRACEPRINT("descriptor heap base for GPU: " << baseGPU.ptr << "\n");
    binder->GPU[UAV].ptr = baseGPU.ptr +  0*descriptorSize;
    binder->GPU[CBV].ptr = baseGPU.ptr + 25*descriptorSize;
    binder->GPU[SRV].ptr = baseGPU.ptr + 50*descriptorSize;

    // initialize everything with null descriptors...
    for (int i = 0; i < 25; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC NullDescUAV = { };
            NullDescUAV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // don't care, but can't be unknown...
            NullDescUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            NullDescUAV.Buffer.FirstElement = 0;
            NullDescUAV.Buffer.NumElements = 0;
            NullDescUAV.Buffer.StructureByteStride = 0;
            NullDescUAV.Buffer.CounterOffsetInBytes = 0;
            NullDescUAV.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        D3D12_CPU_DESCRIPTOR_HANDLE hCPU = binder->CPU[UAV];
        hCPU.ptr += i*descriptorSize;
        (*device)->CreateUnorderedAccessView(NULL, NULL, &NullDescUAV, hCPU);
    }
    for (int i = 0; i < 25; ++i)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC NullDescCBV = { };
            NullDescCBV.BufferLocation = 0;
            NullDescCBV.SizeInBytes = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE hCPU = binder->CPU[CBV];
        hCPU.ptr += i*descriptorSize;
        Call_ID3D12Device_CreateConstantBufferView((*device), &NullDescCBV, hCPU);
    }
    for (int i = 0; i < 25; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC NullDescSRV = { };
            NullDescSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // don't care, but can't be unknown...
            NullDescSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            NullDescSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            NullDescSRV.Buffer.FirstElement = 0;
            NullDescSRV.Buffer.NumElements = 0;
            NullDescSRV.Buffer.StructureByteStride = 0;
            NullDescSRV.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        D3D12_CPU_DESCRIPTOR_HANDLE hCPU = binder->CPU[SRV];
        hCPU.ptr += i*descriptorSize;
        Call_ID3D12Device_CreateShaderResourceView((*device), NULL, &NullDescSRV, hCPU);
    }

    return(binder);
}

struct NSRange {
    size_t location;
    size_t length;
};

WEAK void did_modify_range(d3d12_buffer* buffer, NSRange range)
{
    TRACELOG;

    halide_assert(user_context, (buffer->staging != NULL));
    buffer->staging->offset = range.location;
    buffer->staging->size = range.length;
}

WEAK void synchronize_resource(d3d12_copy_command_list* cmdList, d3d12_buffer* buffer)
{
    TRACELOG;

    d3d12_buffer::staging_t* staging = buffer->staging;
    halide_assert(NULL, (staging != NULL));

    UINT64 DstOffset = staging->offset;
    UINT64 SrcOffset = staging->offset;
    UINT64 NumBytes  = staging->size;
    ID3D12Resource* pDstBuffer = NULL;
    ID3D12Resource* pSrcBuffer = NULL;

    halide_assert(user_context, buffer->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_RESOURCE_BARRIER barrier = { };
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = buffer->resource;
        barrier.Transition.Subresource = 0;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // NOTE(marcos): can we leverage more asynchronous parallelism with special
    // flags like D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY / END_ONLY?

    switch (staging->buffer->type)
    {
        case d3d12_buffer::Upload :
            pDstBuffer = buffer->resource;
            pSrcBuffer = staging->buffer->resource;
            DstOffset = buffer->offsetInBytes;
            halide_assert(user_context, staging->buffer->state == D3D12_RESOURCE_STATE_GENERIC_READ);
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            break;
        case d3d12_buffer::ReadBack :
            unmap_buffer(staging->buffer);
            pDstBuffer = staging->buffer->resource;
            pSrcBuffer = buffer->resource;
            SrcOffset = buffer->offsetInBytes;
            halide_assert(user_context, staging->buffer->state == D3D12_RESOURCE_STATE_COPY_DEST);
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
            break;
        default :
            TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
            halide_assert(user_context, false);
            break;
    }

    TRACEPRINT("--- "
        << (void*)buffer << " | " << (void*)buffer->halide << " | "
        << SrcOffset << " : " << DstOffset << " : " << NumBytes
        << "\n");

    (*cmdList)->ResourceBarrier(1, &barrier);
    (*cmdList)->CopyBufferRegion(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes);
    swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);    // restore resource state
    (*cmdList)->ResourceBarrier(1, &barrier);
}

WEAK void compute_barrier(d3d12_copy_command_list* cmdList, d3d12_buffer* buffer)
{
    TRACELOG;

    D3D12_RESOURCE_BARRIER barrier = { };
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = buffer->resource;

    (*cmdList)->ResourceBarrier(1, &barrier);
}

WEAK bool is_buffer_managed(d3d12_buffer* buffer)
{
    return( buffer->staging != NULL );
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

static void dump_shader(const char* source, ID3DBlob* compiler_msgs = NULL)
{
    const char* message = "<no error message reported>";
    if (compiler_msgs)
    {
        message = (const char*)compiler_msgs->GetBufferPointer();
    }

    StackPrinter<64*1024> dump;
    dump(user_context) << TRACEINDENT << "D3DCompile(): " << message << "\n";
    dump(user_context) << TRACEINDENT << ">>> HLSL shader source dump <<<\n" << source << "\n";
}

static d3d12_function* new_function_with_name(d3d12_device* device, d3d12_library* library, const char* name, size_t name_len, int shared_mem_bytes, int threadsX, int threadsY, int threadsZ)
{
    TRACELOG;

    // TODO(marcos): cache the compiled function in the library to reduce the
    // overhead on 'halide_d3d12compute_run' (the only caller)

    // Round shared memory size up to a multiple of 16:
    shared_mem_bytes = (shared_mem_bytes + 0xF) & ~0xF;

    TRACEPRINT("groupshared memory size: " << shared_mem_bytes << " bytes.\n");
    TRACEPRINT("numthreads( " << threadsX << ", " << threadsY << ", " << threadsZ << " )\n");

    const char* source = library->source;
    int source_size = library->source_length;
    StackPrinter<16, StringStreamPrinter> SS [4];
    D3D_SHADER_MACRO pDefines [] =
    {
        { "__GROUPSHARED_SIZE_IN_BYTES", (SS[0] << shared_mem_bytes).str() },
        { "__NUM_TREADS_X",              (SS[1] << threadsX).str()         },
        { "__NUM_TREADS_Y",              (SS[2] << threadsY).str()         },
        { "__NUM_TREADS_Z",              (SS[3] << threadsZ).str()         },
        { NULL, NULL }
    };
    const char* shaderName = name;  // only used for debug information
    ID3DInclude* includeHandler = NULL;
    const char* entryPoint = name;
    const char* target = "cs_5_1";  // all d3d12 hardware support SM 5.1
    UINT flags1 = 0;
    UINT flags2 = 0;    // flags related to effects (.fx files)
    ID3DBlob* shaderBlob = NULL;
    ID3DBlob* errorMsgs  = NULL;

    flags1 |= D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#if HALIDE_D3D12_DEBUG_SHADERS
    flags1 |= D3DCOMPILE_DEBUG;
    flags1 |= D3DCOMPILE_SKIP_OPTIMIZATION;
    //flags1 |= D3DCOMPILE_RESOURCES_MAY_ALIAS;
    //flags1 |= D3DCOMPILE_ALL_RESOURCES_BOUND;
#endif

    //dump_shader(source);

    HRESULT result = D3DCompile(source, source_size, shaderName, pDefines, includeHandler, entryPoint, target, flags1, flags2, &shaderBlob, &errorMsgs);

    if (FAILED(result) || (NULL == shaderBlob))
    {
        debug(user_context) << TRACEINDENT << "Unable to compile D3D12 compute shader (HRESULT=" << (void*)(int64_t)result << ", ShaderBlob=" << shaderBlob << " entry=" << entryPoint << ").\n";
        dump_shader(source, errorMsgs);
        Release_ID3D12Object(errorMsgs);
        error(user_context) << "!!! HALT !!!";
        return(NULL);
    }

    debug(user_context) << TRACEINDENT << "SUCCESS while compiling D3D12 compute shader with entry name '" << entryPoint << "'!\n";

    // even though it was successful, there may have been warning messages emitted by the compiler:
    if (NULL != errorMsgs)
    {
        dump_shader(source, errorMsgs);
        Release_ID3D12Object(errorMsgs);
    }

    d3d12_function* function = malloct<d3d12_function>();
    function->shaderBlob = shaderBlob;
    function->rootSignature = rootSignature;
    rootSignature->AddRef();

    return(function);
}

WEAK void set_input_buffer(d3d12_compute_command_list* cmdList, d3d12_binder* binder, d3d12_buffer* input_buffer, uint32_t index)
{
    TRACELOG;

    switch (input_buffer->type)
    {
        case d3d12_buffer::Constant :
        {
            TRACEPRINT("CBV" "\n");

            // NOTE(marcos): constant buffers are only used internally by the
            // runtime; users cannot create, control or access them, so it is
            // expected that no halide_buffer_t will be associated with them:
            halide_assert(NULL, input_buffer->halide == NULL);
            halide_assert(NULL, input_buffer->format == DXGI_FORMAT_UNKNOWN);

            ID3D12Resource* pResource = input_buffer->resource;
            D3D12_GPU_VIRTUAL_ADDRESS pGPU = pResource->GetGPUVirtualAddress();

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvd = { };
                cbvd.BufferLocation = pGPU;
                cbvd.SizeInBytes = input_buffer->sizeInBytes;

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
        case d3d12_buffer::WriteOnly :
        {
            TRACEPRINT("UAV" "\n");

            DXGI_FORMAT Format = input_buffer->format;
            if (DXGI_FORMAT_UNKNOWN == Format)
            {
                error(user_context) << "unsupported buffer element type: " << input_buffer->halide->type << "\n";
            }

            UINT FirstElement = input_buffer->offset;
            // for some reason, 'input_buffer->halide->number_of_elements()' is
            // returning 1 for cropped buffers... ('size_in_bytes()' returns 0)
            UINT NumElements = input_buffer->elements;
            UINT SizeInBytes = input_buffer->sizeInBytes;
            UNUSED(SizeInBytes);

            TRACEPRINT("--- "
                << (void*)input_buffer << " | " << (void*)input_buffer->halide << " | "
                << FirstElement << " : " << NumElements << " : " << SizeInBytes
                << "\n");

            // A View of a non-Structured Buffer cannot be created using a NULL Desc.
            // Default Desc parameters cannot be used, as a Format must be supplied.
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = { };
                uavd.Format = Format;
                uavd.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uavd.Buffer.FirstElement = FirstElement;
                uavd.Buffer.NumElements = NumElements;
                uavd.Buffer.StructureByteStride = 0;
                uavd.Buffer.CounterOffsetInBytes = 0;   // 0, since this is not an atomic counter
                uavd.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

            // TODO(marcos): should probably use the "index" input argument here somewhere...
            D3D12_CPU_DESCRIPTOR_HANDLE hDescUAV = binder->CPU[UAV];
            binder->CPU[UAV].ptr += binder->descriptorSize;

            ID3D12Resource* pResource = input_buffer->resource;
            ID3D12Resource* pCounterResource = NULL;    // for atomic counters

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

static void commit_command_list(d3d12_compute_command_list* cmdList)
{
    TRACELOG;
    end_recording(cmdList);
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
        debug(user_context) << TRACEINDENT
                            << "Device Lost! GetDeviceRemovedReason(): "
                            << "before: " << (void*)(int64_t)result_before << " | "
                            << "after: "  << (void*)(int64_t)result_after  << "\n";
        error(user_context) << "!!! HALT !!!";
    }
}

static void* buffer_contents(d3d12_buffer* buffer)
{
    TRACELOG;

    void* pData = NULL;

    switch (buffer->type)
    {
        case d3d12_buffer::ReadOnly :
        {
            pData = buffer_contents(&readback);
            break;
        }

        case d3d12_buffer::WriteOnly :
        {
            pData = buffer_contents(&upload);
            break;
        }

        case d3d12_buffer::Constant :
        case d3d12_buffer::Upload   :
        {
            pData = buffer->mapped;
            break;
        }

        case d3d12_buffer::ReadBack :
        {
            // on readback heaps, map/unmap as needed, since the results are only effectively
            // published after a Map() call, and should ideally be in an unmapped state prior
            // to the CopyBufferRegion() call
            pData = map_buffer(&readback);
            break;
        }

        case d3d12_buffer::Unknown :
        case d3d12_buffer::ReadWrite :
        default:
            TRACEPRINT("UNSUPPORTED BUFFER TYPE: " << (int)buffer->type << "\n");
            halide_assert(user_context, false);
            break;
    }

    halide_assert(user_context, pData);

    return(pData);
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

        debug(user_context) << TRACEINDENT << "D3D12Compute - Allocating: master root signature\n";
        halide_assert(user_context, (NULL == rootSignature));
        rootSignature = D3D12CreateMasterRootSignature((*device));
        if (NULL == rootSignature)
        {
            error(user_context) << TRACEINDENT << "D3D12Compute: unable to create master root signature.\n";
            release_object(device);
            device = NULL;
            __sync_lock_release(&thread_lock);
            return -1;
        }

        halide_assert(user_context, (NULL == queue));
        debug(user_context) << TRACEINDENT << "D3D12Compute - Allocating: command queue\n";
        queue = new_command_queue(device);
        if (NULL == queue)
        {
            error(user_context) << TRACEINDENT << "D3D12Compute: cannot allocate command queue.\n";
            Release_ID3D12Object(rootSignature);
            release_object(device);
            device = NULL;
            __sync_lock_release(&thread_lock);
            return -1;
        }

        size_t heap_size = 64 * 1024 * 1024;
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

struct command_list_completed_handler_block_descriptor_1
{
    unsigned long reserved;
    unsigned long block_size;
};

struct command_list_completed_handler_block_literal
{
    void (*invoke)(command_list_completed_handler_block_literal*, d3d12_command_list* cmdList);
    struct command_list_completed_handler_block_descriptor_1* descriptor;
};

WEAK command_list_completed_handler_block_descriptor_1 command_list_completed_handler_descriptor =
{
};

static void command_list_completed_handler_invoke(command_list_completed_handler_block_literal* block, d3d12_command_list* cmdList)
{
    TRACELOG;
    TRACEPRINT("... ignoring ...\n")
    /*
    objc_id buffer_error = command_list_error(cmdList);
    if (buffer_error != NULL)
    {
        ns_log_object(buffer_error);
        release_object(buffer_error);
    }
    */
}

WEAK command_list_completed_handler_block_literal command_list_completed_handler_block =
{
    command_list_completed_handler_invoke,
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
    release_object(d3d12_buf);

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

    //d3d12_command_allocator* sync_command_allocator = new_command_allocator<D3D12_COMMAND_LIST_TYPE_COMPUTE>(device);
    //d3d12_compute_command_list* blitCmdList = new_compute_command_list(device, sync_command_allocator);
    d3d12_command_allocator* sync_command_allocator = new_command_allocator<D3D12_COMMAND_LIST_TYPE_DIRECT>(device);
    d3d12_compute_command_list* blitCmdList = new_command_list<D3D12_COMMAND_LIST_TYPE_DIRECT>(device, sync_command_allocator);
    d3d12_buffer* dev_buffer = NULL;
    if (buffer != NULL)
    {
        dev_buffer = (struct d3d12_buffer*)buffer->device;
        if (is_buffer_managed(dev_buffer))
        {
            synchronize_resource(blitCmdList, dev_buffer);
        }
    }
    commit_command_list(blitCmdList);
    wait_until_completed(blitCmdList);

    if (dev_buffer != NULL)
    {
        if (dev_buffer->staging != NULL)
        {
            // for now, we expect to have been the only one with pending transfer on the staging buffer:
            uint64_t use_count = __atomic_sub_fetch(&dev_buffer->staging->buffer->ref_count, 1, __ATOMIC_SEQ_CST);
            halide_assert(user_context, (use_count == 0));
            dev_buffer->staging = NULL;
        }
    }

    release_object(blitCmdList);
    release_object(sync_command_allocator);
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

WEAK int halide_d3d12compute_device_release(void* user_context)
{
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
                release_object(state->library);
                state->library = NULL;
            }
            state = state->next;
        }

        // Release the device itself, if we created it.
        if (acquired_device == device)
        {
            debug(user_context) <<  "D3D12Compute - Releasing: upload and readback heaps " << &upload << "," << &readback << "\n";
            release_object(&upload);
            release_object(&readback);
            d3d12_buffer empty = { };
            upload = readback = empty;

            debug(user_context) << "D3D12Compute - Releasing: master root signature " << rootSignature << "\n";
            Release_ID3D12Object(rootSignature);
            rootSignature = NULL;

            debug(user_context) <<  "D3D12Compute - Releasing: command queue " << queue << "\n";
            release_object(queue);
            queue = NULL;

            debug(user_context) << "D3D12Compute - Releasing: D3D12CreateSystemDefaultDevice " << device << "\n";
            release_object(device);
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

    halide_assert(user_context, buffer->host && buffer->device);

    D3D12ContextHolder d3d12_context (user_context, true);
    if (d3d12_context.error != 0)
    {
        return d3d12_context.error;
    }

    // 1. copy data from host src buffer to the upload staging buffer:
    device_copy c = make_host_to_device_copy(buffer);
    halide_assert(user_context, c.dst == buffer->device);
    d3d12_buffer* copy_dst = reinterpret_cast<d3d12_buffer*>(c.dst);
    debug(user_context) << TRACEINDENT
                        << "halide_d3d12compute_copy_to_device dev = " << (void*)buffer->device
                        << " d3d12_buffer = " << copy_dst
                        << " host = " << buffer->host
                        << "\n";
    // 'memcpy' to staging buffer:
    size_t total_size = buffer->size_in_bytes();
    halide_assert(user_context, total_size > 0);
    halide_assert(user_context, upload.sizeInBytes >= total_size);
    void* staging_ptr = buffer_contents(&upload);
    c.dst = reinterpret_cast<uint64_t>(staging_ptr);
    copy_memory(c, user_context);

    // 2. issue a copy command from the upload buffer to the device buffer
    halide_assert(user_context, (copy_dst->type == d3d12_buffer::ReadOnly) || (copy_dst->type == d3d12_buffer::ReadWrite));
    halide_assert(user_context, (upload.type == d3d12_buffer::Upload));
    halide_assert(user_context, (copy_dst->staging == NULL));
    // for now, we expect no other transfers to be pending on the staging buffer:
    uint64_t use_count = __atomic_add_fetch(&upload.ref_count, 1, __ATOMIC_SEQ_CST);
    halide_assert(user_context, (use_count == 1));
    d3d12_buffer::staging_t staging = { };
        staging.buffer = &upload;
        staging.offset = 0;
        staging.size = 0;
    copy_dst->staging = &staging;
    if (is_buffer_managed(copy_dst))
    {
        NSRange total_extent;
        total_extent.location = 0;
        total_extent.length = total_size;
        did_modify_range(copy_dst, total_extent);
        halide_d3d12compute_device_sync_internal(d3d12_context.device, buffer);
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT
                        << "Time for halide_d3d12compute_copy_to_device: "
                        << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_d3d12compute_copy_to_host(void* user_context, halide_buffer_t* buffer)
{
    TRACELOG;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buffer->host && buffer->device);
    halide_assert(user_context, buffer->dimensions <= MAX_COPY_DIMS);
    if (buffer->dimensions > MAX_COPY_DIMS) {
        return -1;
    }

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0)
    {
        return d3d12_context.error;
    }

    d3d12_buffer* dbuffer = reinterpret_cast<d3d12_buffer*>(buffer->device);

    // 1. issue copy command from device to staging memory
    // for now, we expect no other transfers to be pending on the staging buffer:
    uint64_t use_count = __atomic_add_fetch(&readback.ref_count, 1, __ATOMIC_SEQ_CST);
    halide_assert(user_context, (use_count == 1));
    d3d12_buffer::staging_t staging = { };
        staging.buffer = &readback;
        staging.offset = 0;
        staging.size   = dbuffer->sizeInBytes;
    dbuffer->staging = &staging;
    halide_d3d12compute_device_sync_internal(d3d12_context.device, buffer);

    // 2. memcopy from staging memory to host
    void* src_data = buffer_contents(staging.buffer);
    device_copy c = make_device_to_host_copy(buffer);
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
                           int num_coords_dim1)
{
    TRACELOG;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    D3D12ContextHolder d3d12_context(user_context, true);
    if (d3d12_context.error != 0) {
        return d3d12_context.error;
    }

    d3d12_device* device = d3d12_context.device;

    #if HALIDE_D3D12_RENDERDOC
    StartCapturingGPUActivity();
    #endif

    //d3d12_command_allocator* command_allocator = new_command_allocator<D3D12_COMMAND_LIST_TYPE_COMPUTE>(device);
    d3d12_command_allocator* command_allocator = new_command_allocator<D3D12_COMMAND_LIST_TYPE_DIRECT>(device);
    if (command_allocator == 0) {
        error(user_context) << "D3D12Compute: Could not create compute command allocator.\n";
        return -1;
    }

    //d3d12_compute_command_list* cmdList = new_compute_command_list(device, command_allocator);
    d3d12_compute_command_list* cmdList = new_command_list<D3D12_COMMAND_LIST_TYPE_DIRECT>(device, command_allocator);
    if (cmdList == 0) {
        error(user_context) << "D3D12Compute: Could not create compute command list.\n";
        return -1;
    }

    halide_assert(user_context, state_ptr);
    module_state *state = (module_state*)state_ptr;

    d3d12_function* function = new_function_with_name(device, state->library, entry_name, strlen(entry_name),
        shared_mem_bytes, threadsX, threadsY, threadsZ);
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
        halide_assert(user_context, arg_sizes[i] <= 4);
        size_t argsize = 4;
        total_args_size = (total_args_size + argsize - 1) & ~(argsize - 1);
        total_args_size += argsize;
    }
    d3d12_buffer args_buffer = { };
    int32_t buffer_index = 0;
    if (total_args_size > 0)
    {
        // Direct3D 12 expects constant buffers to have sizes multiple of 256:
        size_t constant_buffer_size = (total_args_size + 255) & ~255;
        args_buffer = new_constant_buffer(d3d12_context.device, constant_buffer_size);
        if (!args_buffer)
        {
            error(user_context) << "D3D12Compute: Could not allocate arguments buffer.\n";
            release_object(function);
            return -1;
        }
        uint8_t* args_ptr = (uint8_t*)buffer_contents(&args_buffer);
        size_t offset = 0;
        for (size_t i = 0; arg_sizes[i] != 0; i++)
        {
            if (arg_is_buffer[i])
                continue;
            halide_assert(user_context, arg_sizes[i] <= 4);
            union
            {
                void*     p;
                float*    f;
                uint8_t*  b;
                uint16_t* s;
                uint32_t* i;
            } arg;
            arg.p = args[i];
            size_t argsize = 4;
            uint32_t val = 0;
            switch (arg_sizes[i])
            {
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

    // setup/bind the argument buffer, if arguments have indeed been packed:
    if (args_buffer)
    {
        set_input_buffer(cmdList, binder, &args_buffer, buffer_index);
        buffer_index++;
    }

    // setup/bind actual buffers:
    for (size_t i = 0; arg_sizes[i] != 0; i++)
    {
        if (!arg_is_buffer[i])
            continue;
        halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
        halide_buffer_t* hbuffer = (halide_buffer_t*)args[i];
        uint64_t handle = hbuffer->device;
        d3d12_buffer* buffer = (d3d12_buffer*)handle;
        set_input_buffer(cmdList, binder, buffer, buffer_index);
        buffer_index++;
    }

    d3d12_compute_pipeline_state* pipeline_state = new_compute_pipeline_state_with_function(d3d12_context.device, function);
    if (pipeline_state == 0) {
        error(user_context) << "D3D12Compute: Could not allocate pipeline state.\n";
        release_object(function);
        return -1;
    }
    set_compute_pipeline_state(cmdList, pipeline_state, function, binder);

    dispatch_threadgroups(cmdList,
                          blocksX, blocksY, blocksZ,
                          threadsX, threadsY, threadsZ);

    // TODO(marcos): avoid placing UAV barriers all the time after a dispatch...
    // in addition, only buffers written by the dispatch need barriers...
    for (size_t i = 0; arg_sizes[i] != 0; i++)
    {
        if (!arg_is_buffer[i])
            continue;
        halide_buffer_t* hbuffer = (halide_buffer_t*)args[i];
        uint64_t handle = hbuffer->device;
        d3d12_buffer* buffer = (d3d12_buffer*)handle;
        compute_barrier(cmdList, buffer);
    }

    add_command_list_completed_handler(cmdList, &command_list_completed_handler_block);

    commit_command_list(cmdList);

    wait_until_completed(cmdList);  // TODO(marcos): find a way to gracefully handle this hard wait...

    #if HALIDE_D3D12_RENDERDOC
    FinishCapturingGPUActivity();
    #endif

    release_object(cmdList);
    release_object(command_allocator);
    release_object(&args_buffer);
    release_object(pipeline_state);
    release_object(binder);
    release_object(function);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << TRACEINDENT << "Time for halide_d3d12compute_device_run: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_d3d12compute_device_and_host_malloc(void* user_context, struct halide_buffer_t* buffer)
{
    TRACELOG;
    debug(user_context) << TRACEINDENT << "halide_d3d12compute_device_and_host_malloc called.\n";

    int result = halide_d3d12compute_device_malloc(user_context, buffer);
    if (result == 0)
    {
        d3d12_buffer* dev_buffer = (d3d12_buffer*)(buffer->device);
        // NOTE(marcos): maybe keep a dedicated upload heap for this buffer?
        //buffer->host = (uint8_t *)buffer_contents(dev_buffer);
        buffer->host = (uint8_t*)malloc(buffer->size_in_bytes());
        debug(user_context) << TRACEINDENT 
                            << "halide_d3d12compute_device_and_host_malloc"
                            << " device = " << (void*)buffer->device
                            << " d3d12_buffer = " << dev_buffer
                            << " host = " << buffer->host << "\n";
    }
    return result;
}

WEAK int halide_d3d12compute_device_and_host_free(void* user_context, struct halide_buffer_t* buffer)
{
    TRACELOG;
    debug(user_context) << TRACEINDENT << "halide_d3d12compute_device_and_host_free called.\n";
    halide_d3d12compute_device_free(user_context, buffer);
    free(buffer->host);
    buffer->host = NULL;
    return 0;
}

WEAK int halide_d3d12compute_device_crop(void *user_context,
                                         const struct halide_buffer_t *src,
                                         struct halide_buffer_t *dst) {
    TRACELOG;
    debug(user_context) << TRACEINDENT << "halide_d3d12compute_device_crop called.\n";

    D3D12ContextHolder d3d12_context (user_context, true);
    if (d3d12_context.error != 0)
    {
        return d3d12_context.error;
    }

    int64_t offset = 0;
    for (int i = 0; i < src->dimensions; i++)
    {
        offset += (dst->dim[i].min - src->dim[i].min) * src->dim[i].stride;
    }
    //offset *= src->type.bytes();

    d3d12_buffer* new_handle = malloct<d3d12_buffer>();
    if (new_handle == NULL) {
        error(user_context) << "halide_d3d12compute_device_crop: malloc failed making device handle.\n";
        return halide_error_code_out_of_memory;
    }

    d3d12_buffer* old_handle = reinterpret_cast<d3d12_buffer*>(src->device);
    *new_handle = *old_handle;
    new_handle->resource->AddRef();
    new_handle->halide = dst;
    new_handle->offset = old_handle->offset + offset;
    new_handle->offsetInBytes = new_handle->offset * dst->type.bytes() * dst->type.lanes;
    // for some reason, 'dst->number_of_elements()' is always returning 1
    // later on when 'set_input()' is called...
    new_handle->elements = old_handle->elements - offset;
    new_handle->sizeInBytes = dst->size_in_bytes();
    new_handle->mallocd = true;
    new_handle->staging = NULL;
    //halide_assert(user_context, (NULL == new_handle->staging));
    dst->device = reinterpret_cast<uint64_t>(new_handle);

    halide_assert(user_context, (NULL == dst->device_interface));
    dst->device_interface = src->device_interface;
    // must increment the reference count for the module here because later on
    // 'halide_d3d12compute_device_free' will end up indirectly decrementing it
    dst->device_interface->impl->use_module();

    TRACEPRINT("--- "
        << (void*)old_handle  << " | " << (void*)old_handle->halide << " | "
        << old_handle->offset << " : " << old_handle->elements << " : " << old_handle->sizeInBytes
        << "   ->   "
        << (void*)new_handle  << " | " << (void*)new_handle->halide << " | "
        << new_handle->offset << " : " << new_handle->elements << " : " << new_handle->sizeInBytes
        << "\n");

    return 0;
}

WEAK int halide_d3d12compute_device_release_crop(void* user_context,
                                                 struct halide_buffer_t* buf)
{
    TRACELOG;
    // for D3D12, this is exactly like halide_d3d12compute_device_free():
    return( halide_d3d12compute_device_free(user_context, buf) );
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
    d3d12_buf->offset = 0;
    d3d12_buf->elements = halide_buf->number_of_elements();
    d3d12_buf->offsetInBytes = 0;
    d3d12_buf->format = FindD3D12FormatForHalideType(halide_buf->type);
    if (DXGI_FORMAT_UNKNOWN == d3d12_buf->format)
    {
        error(user_context) << "unsupported buffer element type: " << halide_buf->type << "\n";
        return(-3);
    }

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
