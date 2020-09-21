#ifndef __mini_d3d12_h__
#define __mini_d3d12_h__

#ifdef __clang__
//    #define __stdcall __attribute__ ((stdcall))
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunused-value"
#define __export
#ifndef _WIN32
#define _WIN32 1
#endif          //_WIN32
#ifdef BITS_64  // <- Halide back-end compilation flag (-DBITS_${j})
#ifndef _WIN64
#define _WIN64 1
#endif  //_WIN64
#else
#define _STDCALL_SUPPORTED
#endif
#endif

#ifndef __midl
#define __midl
#endif  //__midl

/* sal.h */
// Clear-out SAL macros (Source-code Annotation Language)
#ifndef _In_
#define _In_
#endif  //_In_

#ifndef _In_z_
#define _In_z_
#endif  //_In_z_

#ifndef _In_opt_
#define _In_opt_
#endif  //_In_opt_

#ifndef _Out_
#define _Out_
#endif  //_Out_

#ifndef _Out_opt_
#define _Out_opt_
#endif  //_Out_opt_

#ifndef _Inout_
#define _Inout_
#endif  //_Inout_

#ifndef _Inout_opt_
#define _Inout_opt_
#endif  //_Inout_opt_

#ifndef _COM_Outptr_
#define _COM_Outptr_
#endif  //_COM_Outptr_

#ifndef _COM_Outptr_opt_
#define _COM_Outptr_opt_
#endif  //_COM_Outptr_opt_

#ifndef _Check_return_
#define _Check_return_
#endif  //_Check_return_

#ifndef _Null_terminated_
#define _Null_terminated_
#endif  //_Null_terminated_

#ifndef _Return_type_success_
#define _Return_type_success_(expr)
#endif  //_Return_type_success_

#ifndef _Post_equal_to_
#define _Post_equal_to_(e)
#endif  //_Post_equal_to_

#ifndef _Post_satisfies_
#define _Post_satisfies_(expr)
#endif  //_Post_satisfies_

#ifndef _In_range_
#define _In_range_(lb, ub)
#endif  //_In_range_

#ifndef _In_reads_
#define _In_reads_(size)
#endif  //_In_reads_

#ifndef _In_reads_opt_
#define _In_reads_opt_(size)
#endif  //_In_reads_opt_

#ifndef _In_reads_bytes_
#define _In_reads_bytes_(size)
#endif  //_In_reads_bytes_

#ifndef _In_reads_bytes_opt_
#define _In_reads_bytes_opt_(size)
#endif  //_In_reads_bytes_opt_

#ifndef _Out_writes_
#define _Out_writes_(size)
#endif  //_Out_writes_

#ifndef _Out_writes_opt_
#define _Out_writes_opt_(size)
#endif  //_Out_writes_opt_

#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(size)
#endif  //_Out_writes_bytes_

#ifndef _Out_writes_bytes_opt_
#define _Out_writes_bytes_opt_(size)
#endif  //_Out_writes_bytes_opt_

#ifndef _Inout_updates_bytes_
#define _Inout_updates_bytes_(size)
#endif  //_Inout_updates_bytes_

#ifndef _Field_size_full_
#define _Field_size_full_(size)
#endif  //_Field_size_full_

#ifndef _Field_size_bytes_full_
#define _Field_size_bytes_full_(size)
#endif  //_Field_size_bytes_full_

#ifndef _Outptr_opt_result_bytebuffer_
#define _Outptr_opt_result_bytebuffer_(size)
#endif  //_Outptr_opt_result_bytebuffer_

#ifndef _Must_inspect_result_
#define _Must_inspect_result_
#endif  //_Must_inspect_result_

#ifndef __specstrings
#define __specstrings
#endif  //__specstrings

#ifndef _Always_
#define _Always_(annos)
#endif  //_Always_

/* rpcsal.h */
#ifndef __RPC_string
#define __RPC_string
#endif  //__RPC_string

#ifndef __RPC__deref_out
#define __RPC__deref_out
#endif  //__RPC__deref_out

/* minwindef.h */
/* 
 * On ARM and x64 processors, __stdcall is accepted and ignored by the compiler;
 * on ARM and x64 architectures, by convention, arguments are passed in registers
 * when possible, and subsequent arguments are passed on the stack.
 */
#define WINAPI __stdcall

#define VOID void
typedef char CHAR;
typedef short SHORT;
typedef unsigned char BYTE;
typedef unsigned short WORD;
#ifdef __clang__
// must enforce LLP64 for Windows x64 ...
typedef int32_t LONG;  // long is 64bits on clang-x64
typedef int32_t INT;   // and so is int as well...
typedef uint32_t ULONG;
typedef uint32_t UINT;
typedef uint32_t DWORD;
#else
typedef long LONG;
typedef int INT;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef unsigned long DWORD;
#endif
typedef float FLOAT;

#define CONST const
#define far
#define near
typedef CONST void far *LPCVOID;
typedef void far *LPVOID;

typedef INT BOOL;

#ifdef __clang__
typedef unsigned short WCHAR;  // clang's wchar_t is 32bits by default...
#else
#ifndef _MAC
typedef wchar_t WCHAR;  // Windows wchar_t : 16-bit UNICODE character
#else
// some Macintosh compilers don't define wchar_t in a convenient location, or define it as a char
typedef unsigned short WCHAR;  // wc,   16-bit UNICODE character
#endif
#endif
typedef _Null_terminated_ CHAR *NPSTR, *LPSTR, *PSTR;
typedef _Null_terminated_ CONST CHAR *LPCSTR, *PCSTR;
typedef _Null_terminated_ CONST WCHAR *LPCWSTR, *PCWSTR;

#define FAR far
#define NEAR near

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/* basestd.h */
typedef CHAR INT8, *PINT8;
typedef SHORT INT16, *PINT16;
typedef INT INT32, *PINT32;
typedef BYTE UINT8, *PUINT8;
typedef WORD UINT16, *PUINT16;
typedef UINT UINT32, *PUINT32;
#ifdef __clang__
typedef int64_t INT64, *PINT64;
typedef uint64_t UINT64, *PUINT64;
#else
typedef __int64 INT64, *PINT64;
typedef unsigned __int64 UINT64, *PUINT64;
#endif
#if !defined(_W64)
#if !defined(__midl) && (defined(_X86_) || defined(_M_IX86) || defined(_ARM_) || defined(_M_ARM)) && _MSC_VER >= 1300
#define _W64 __w64
#else
#define _W64
#endif
#endif
#if defined(_WIN64)
typedef INT64 INT_PTR, *PINT_PTR;
typedef UINT64 UINT_PTR, *PUINT_PTR;

typedef INT64 LONG_PTR, *PLONG_PTR;
typedef UINT64 ULONG_PTR, *PULONG_PTR;

#define __int3264 INT64

#else
typedef _W64 INT32 INT_PTR, *PINT_PTR;
typedef _W64 UINT32 UINT_PTR, *PUINT_PTR;

typedef _W64 INT32 LONG_PTR, *PLONG_PTR;
typedef _W64 UINT32 ULONG_PTR, *PULONG_PTR;

#define __int3264 INT32

#endif
typedef ULONG_PTR SIZE_T, *PSIZE_T;
typedef LONG_PTR SSIZE_T, *PSSIZE_T;

#ifndef NO_STRICT
#ifndef STRICT
#define STRICT 1
#endif
#endif /* NO_STRICT */

/* minwinbase.h */
typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

/* winnt.h */
#ifdef __clang__
typedef int64_t LONGLONG;
#else
typedef __int64 LONGLONG;
#endif

typedef _Return_type_success_(return >= 0) LONG HRESULT;

#if defined(_WIN32) || defined(_MPPC_)

// Win32 doesn't support __export

#ifdef _68K_
#define STDMETHODCALLTYPE __cdecl
#else
#define STDMETHODCALLTYPE __stdcall
#endif
#define STDMETHODVCALLTYPE __cdecl

#define STDAPICALLTYPE __stdcall
#define STDAPIVCALLTYPE __cdecl

#else

#define STDMETHODCALLTYPE __export __stdcall
#define STDMETHODVCALLTYPE __export __cdecl

#define STDAPICALLTYPE __export __stdcall
#define STDAPIVCALLTYPE __export __cdecl

#endif

#define DUMMYSTRUCTNAME

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    } DUMMYSTRUCTNAME;
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;

#ifdef __cplusplus
#define EXTERN_C extern "C"
#define EXTERN_C_START extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C extern
#define EXTERN_C_START
#define EXTERN_C_END
#endif

#if (defined(_M_IX86) || defined(_M_IA64) || defined(_M_AMD64) || defined(_M_ARM) || defined(_M_ARM64)) && !defined(MIDL_PASS)
#define DECLSPEC_IMPORT __declspec(dllimport)
#else
#define DECLSPEC_IMPORT
#endif

#ifdef __cplusplus

// Define operator overloads to enable bit operations on enum values that are
// used to define flags. Use DEFINE_ENUM_FLAG_OPERATORS(YOUR_TYPE) to enable these
// operators on YOUR_TYPE.

// Moved here from objbase.w.

// Templates are defined here in order to avoid a dependency on C++ <type_traits> header file,
// or on compiler-specific contructs.
extern "C++" {

template<size_t S>
struct _ENUM_FLAG_INTEGER_FOR_SIZE;

template<>
struct _ENUM_FLAG_INTEGER_FOR_SIZE<1> {
    typedef INT8 type;
};

template<>
struct _ENUM_FLAG_INTEGER_FOR_SIZE<2> {
    typedef INT16 type;
};

template<>
struct _ENUM_FLAG_INTEGER_FOR_SIZE<4> {
    typedef INT32 type;
};

// used as an approximation of std::underlying_type<T>
template<class T>
struct _ENUM_FLAG_SIZED_INTEGER {
    typedef typename _ENUM_FLAG_INTEGER_FOR_SIZE<sizeof(T)>::type type;
};
}

#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE)                                                                                   \
    extern "C++" {                                                                                                             \
    inline ENUMTYPE operator|(ENUMTYPE a, ENUMTYPE b) {                                                                        \
        return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) | ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));        \
    }                                                                                                                          \
    inline ENUMTYPE &operator|=(ENUMTYPE &a, ENUMTYPE b) {                                                                     \
        return (ENUMTYPE &)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type &)a) |= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); \
    }                                                                                                                          \
    inline ENUMTYPE operator&(ENUMTYPE a, ENUMTYPE b) {                                                                        \
        return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) & ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));        \
    }                                                                                                                          \
    inline ENUMTYPE &operator&=(ENUMTYPE &a, ENUMTYPE b) {                                                                     \
        return (ENUMTYPE &)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type &)a) &= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); \
    }                                                                                                                          \
    inline ENUMTYPE operator~(ENUMTYPE a) {                                                                                    \
        return ENUMTYPE(~((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a));                                                       \
    }                                                                                                                          \
    inline ENUMTYPE operator^(ENUMTYPE a, ENUMTYPE b) {                                                                        \
        return ENUMTYPE(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)a) ^ ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b));        \
    }                                                                                                                          \
    inline ENUMTYPE &operator^=(ENUMTYPE &a, ENUMTYPE b) {                                                                     \
        return (ENUMTYPE &)(((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type &)a) ^= ((_ENUM_FLAG_SIZED_INTEGER<ENUMTYPE>::type)b)); \
    }                                                                                                                          \
    }
#else
#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE)  // NOP, C allows these operators.
#endif

#ifndef DECLSPEC_NOTHROW
#if (_MSC_VER >= 1200) && !defined(MIDL_PASS)
#define DECLSPEC_NOTHROW __declspec(nothrow)
#else
#define DECLSPEC_NOTHROW
#endif
#endif

#ifdef STRICT
typedef void *HANDLE;
#if 0 && (_MSC_VER > 1000)
#define DECLARE_HANDLE(name) \
    struct name##__;         \
    typedef struct name##__ *name
#else
#define DECLARE_HANDLE(name) \
    struct name##__ {        \
        int unused;          \
    };                       \
    typedef struct name##__ *name
#endif
#else
typedef PVOID HANDLE;
#define DECLARE_HANDLE(name) typedef HANDLE name
#endif
typedef HANDLE *PHANDLE;

/* windef.h */
typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, NEAR *NPRECT, FAR *LPRECT;

DECLARE_HANDLE(HWND);

/* minwindef.h (2) */
DECLARE_HANDLE(HINSTANCE);
typedef HINSTANCE HMODULE; /* HMODULEs can be used in place of HINSTANCEs */

/* winerror.h */
#define _HRESULT_TYPEDEF_(_sc) ((HRESULT)_sc)
#define S_OK _HRESULT_TYPEDEF_(0x0L)
#define E_UNEXPECTED _HRESULT_TYPEDEF_(0x8000FFFFL)
#define DXGI_ERROR_NOT_FOUND _HRESULT_TYPEDEF_(0x887a0002)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

/* guiddef.h */
typedef struct _GUID {
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} GUID;
typedef GUID IID;
typedef IID *LPIID;

#ifdef __midl_proxy
#define __MIDL_CONST
#else
#define __MIDL_CONST const
#endif

#ifdef __cplusplus
#define REFIID const IID &
#else
#define REFIID const IID *__MIDL_CONST
#endif

#ifdef __cplusplus
#define REFGUID const GUID &
#else
#define REFGUID const GUID *__MIDL_CONST
#endif

#ifndef DECLSPEC_SELECTANY
#if (_MSC_VER >= 1100)
#define DECLSPEC_SELECTANY __declspec(selectany)
#else
#define DECLSPEC_SELECTANY
#endif
#endif

#ifdef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID DECLSPEC_SELECTANY name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID FAR name
#endif  // INITGUID

/* rpc.h */
#if defined(__specstrings)
typedef _Return_type_success_(return == 0) LONG RPC_STATUS;
#else
typedef LONG RPC_STATUS;
#endif

#if !defined(__RPC_MAC__) && ((_MSC_VER >= 800) || defined(_STDCALL_SUPPORTED))
#define __RPC_API __stdcall
#define __RPC_USER __stdcall
#define __RPC_STUB __stdcall
#define RPC_ENTRY __stdcall
#else  // Not Win32/Win64
#define __RPC_API
#define __RPC_USER
#define __RPC_STUB
#define RPC_ENTRY
#endif

#define __RPC_FAR

typedef void *I_RPC_HANDLE;

/* rpcdce.h */
#ifndef UUID_DEFINED
#define UUID_DEFINED
typedef GUID UUID;
#ifndef uuid_t
#define uuid_t UUID
#endif
#endif

typedef I_RPC_HANDLE RPC_BINDING_HANDLE;
typedef RPC_BINDING_HANDLE handle_t;
#define rpc_binding_handle_t RPC_BINDING_HANDLE

#define RPC_MGR_EPV void

/* rpcdcep.h */
//#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP | WINAPI_PARTITION_SYSTEM)

typedef struct _RPC_VERSION {
    unsigned short MajorVersion;
    unsigned short MinorVersion;
} RPC_VERSION;

typedef struct _RPC_SYNTAX_IDENTIFIER {
    GUID SyntaxGUID;
    RPC_VERSION SyntaxVersion;
} RPC_SYNTAX_IDENTIFIER, __RPC_FAR *PRPC_SYNTAX_IDENTIFIER;

typedef struct _RPC_MESSAGE {
    RPC_BINDING_HANDLE Handle;
    unsigned long DataRepresentation;
    void __RPC_FAR *Buffer;
    unsigned int BufferLength;
    unsigned int ProcNum;
    PRPC_SYNTAX_IDENTIFIER TransferSyntax;
    void __RPC_FAR *RpcInterfaceInformation;
    void __RPC_FAR *ReservedForRuntime;
    RPC_MGR_EPV __RPC_FAR *ManagerEpv;
    void __RPC_FAR *ImportContext;
    unsigned long RpcFlags;
} RPC_MESSAGE, __RPC_FAR *PRPC_MESSAGE;

//#endif /* WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP | WINAPI_PARTITION_SYSTEM) */

/* rpcndr.h */
#ifndef DECLSPEC_NOVTABLE
#if (_MSC_VER >= 1100) && defined(__cplusplus)
#define DECLSPEC_NOVTABLE __declspec(novtable)
#else
#define DECLSPEC_NOVTABLE
#endif
#endif

#ifndef DECLSPEC_UUID
#if (_MSC_VER >= 1100) && defined(__cplusplus)
#define DECLSPEC_UUID(x) __declspec(uuid(x))
#else
#define DECLSPEC_UUID(x)
#endif
#endif

#define MIDL_INTERFACE(x) struct DECLSPEC_UUID(x) DECLSPEC_NOVTABLE

/* combaseapi.h */
#define BEGIN_INTERFACE
#define END_INTERFACE

#ifdef _68K_
#ifndef REQUIRESAPPLEPASCAL
#define WINOLEAPI EXTERN_C DECLSPEC_IMPORT HRESULT PASCAL
#define WINOLEAPI_(type) EXTERN_C DECLSPEC_IMPORT type PASCAL
#else
#define WINOLEAPI EXTERN_C DECLSPEC_IMPORT PASCAL HRESULT
#define WINOLEAPI_(type) EXTERN_C DECLSPEC_IMPORT PASCAL type
#endif
#else
#define WINOLEAPI EXTERN_C DECLSPEC_IMPORT HRESULT STDAPICALLTYPE
#define WINOLEAPI_(type) EXTERN_C DECLSPEC_IMPORT type STDAPICALLTYPE
#endif

#ifdef COM_STDMETHOD_CAN_THROW
#define COM_DECLSPEC_NOTHROW
#else
#define COM_DECLSPEC_NOTHROW DECLSPEC_NOTHROW
#endif

#if defined(__cplusplus) && !defined(CINTERFACE)

#ifdef COM_STDMETHOD_CAN_THROW
#define COM_DECLSPEC_NOTHROW
#else
#define COM_DECLSPEC_NOTHROW DECLSPEC_NOTHROW
#endif

#define __STRUCT__ struct
#define interface __STRUCT__
#define STDMETHOD(method) virtual COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual COM_DECLSPEC_NOTHROW type STDMETHODCALLTYPE method
#define STDMETHODV(method) virtual COM_DECLSPEC_NOTHROW HRESULT STDMETHODVCALLTYPE method
#define STDMETHODV_(type, method) virtual COM_DECLSPEC_NOTHROW type STDMETHODVCALLTYPE method
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(iface) interface DECLSPEC_NOVTABLE iface
#define DECLARE_INTERFACE_(iface, baseiface) interface DECLSPEC_NOVTABLE iface : public baseiface
#define DECLARE_INTERFACE_IID(iface, iid) interface DECLSPEC_UUID(iid) DECLSPEC_NOVTABLE iface
#define DECLARE_INTERFACE_IID_(iface, baseiface, iid) interface DECLSPEC_UUID(iid) DECLSPEC_NOVTABLE iface : public baseiface

#define IFACEMETHOD(method) __override STDMETHOD(method)
#define IFACEMETHOD_(type, method) __override STDMETHOD_(type, method)
#define IFACEMETHODV(method) __override STDMETHODV(method)
#define IFACEMETHODV_(type, method) __override STDMETHODV_(type, method)

#if !defined(BEGIN_INTERFACE)

#if defined(_MPPC_) && ((defined(_MSC_VER) || defined(__SC__) || defined(__MWERKS__)) && !defined(NO_NULL_VTABLE_ENTRY))
#define BEGIN_INTERFACE \
    virtual void a() {  \
    }
#define END_INTERFACE
#else
#define BEGIN_INTERFACE
#define END_INTERFACE
#endif
#endif

//  IID_PPV_ARGS(ppType)
//      ppType is the variable of type IType that will be filled
//
//      RESULTS in:  IID_IType, ppvType
//      will create a compiler error if wrong level of indirection is used.
//

/* Unknwn.h */
#ifndef __IUnknown_FWD_DEFINED__
#define __IUnknown_FWD_DEFINED__
typedef interface IUnknown IUnknown;

#endif /* __IUnknown_FWD_DEFINED__ */

extern "C++" {
template<typename T>
_Post_equal_to_(pp) _Post_satisfies_(return == pp) void **IID_PPV_ARGS_Helper(T **pp) {
#pragma prefast(suppress : 6269, "Tool issue with unused static_cast")
    static_cast<IUnknown *>(*pp);  // make sure everyone derives from IUnknown
    return reinterpret_cast<void **>(pp);
}
}

#define IID_PPV_ARGS(ppType) __uuidof(**(ppType)), IID_PPV_ARGS_Helper(ppType)

#else

#define interface struct

#define STDMETHOD(method) HRESULT(STDMETHODCALLTYPE *method)
#define STDMETHOD_(type, method) type(STDMETHODCALLTYPE *method)
#define STDMETHODV(method) HRESULT(STDMETHODVCALLTYPE *method)
#define STDMETHODV_(type, method) type(STDMETHODVCALLTYPE *method)

#define IFACEMETHOD(method) __override STDMETHOD(method)
#define IFACEMETHOD_(type, method) __override STDMETHOD_(type, method)
#define IFACEMETHODV(method) __override STDMETHODV(method)
#define IFACEMETHODV_(type, method) __override STDMETHODV_(type, method)

#if !defined(BEGIN_INTERFACE)

#if defined(_MPPC_)
#define BEGIN_INTERFACE void *b;
#define END_INTERFACE
#else
#define BEGIN_INTERFACE
#define END_INTERFACE
#endif
#endif

#define PURE
#define THIS_ INTERFACE FAR *This,
#define THIS INTERFACE FAR *This
#ifdef CONST_VTABLE
#undef CONST_VTBL
#define CONST_VTBL const
#define DECLARE_INTERFACE(iface)                  \
    typedef interface iface {                     \
        const struct iface##Vtbl FAR *lpVtbl;     \
    }                                             \
    iface;                                        \
    typedef const struct iface##Vtbl iface##Vtbl; \
    const struct iface##Vtbl
#else
#undef CONST_VTBL
#define CONST_VTBL
#define DECLARE_INTERFACE(iface)            \
    typedef interface iface {               \
        struct iface##Vtbl FAR *lpVtbl;     \
    }                                       \
    iface;                                  \
    typedef struct iface##Vtbl iface##Vtbl; \
    struct iface##Vtbl
#endif
#define DECLARE_INTERFACE_(iface, baseiface) DECLARE_INTERFACE(iface)
#define DECLARE_INTERFACE_IID(iface, iid) DECLARE_INTERFACE(iface)
#define DECLARE_INTERFACE_IID_(iface, baseiface, iid) DECLARE_INTERFACE_(iface, baseiface)
#endif

typedef interface IRpcStubBuffer IRpcStubBuffer;
typedef interface IRpcChannelBuffer IRpcChannelBuffer;

/* Unknwn.h */
#ifndef __IUnknown_FWD_DEFINED__
#define __IUnknown_FWD_DEFINED__
typedef interface IUnknown IUnknown;

#endif /* __IUnknown_FWD_DEFINED__ */

#ifndef __IUnknown_INTERFACE_DEFINED__
#define __IUnknown_INTERFACE_DEFINED__

/* interface IUnknown */
/* [unique][uuid][object][local] */

typedef /* [unique] */ IUnknown *LPUNKNOWN;

//////////////////////////////////////////////////////////////////
// IID_IUnknown and all other system IIDs are provided in UUID.LIB
// Link that library in with your proxies, clients and servers
//////////////////////////////////////////////////////////////////

#if (_MSC_VER >= 1100) && defined(__cplusplus) && !defined(CINTERFACE)
EXTERN_C const IID IID_IUnknown;
extern "C++" {
MIDL_INTERFACE("00000000-0000-0000-C000-000000000046")
IUnknown {
public:
    BEGIN_INTERFACE
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(
        /* [in] */ REFIID riid,
        /* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject) = 0;

    virtual ULONG STDMETHODCALLTYPE AddRef(void) = 0;

    virtual ULONG STDMETHODCALLTYPE Release(void) = 0;

    template<class Q>
    HRESULT
#ifdef _M_CEE_PURE
        __clrcall
#else
        STDMETHODCALLTYPE
#endif
        QueryInterface(_COM_Outptr_ Q * *pp) {
        return QueryInterface(__uuidof(Q), (void **)pp);
    }

    END_INTERFACE
};
}  // extern C++
HRESULT STDMETHODCALLTYPE IUnknown_QueryInterface_Proxy(
    IUnknown __RPC_FAR *This,
    /* [in] */ REFIID riid,
    /* [iid_is][out] */ __RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject);

void __RPC_STUB IUnknown_QueryInterface_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);

ULONG STDMETHODCALLTYPE IUnknown_AddRef_Proxy(
    IUnknown __RPC_FAR *This);

void __RPC_STUB IUnknown_AddRef_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);

ULONG STDMETHODCALLTYPE IUnknown_Release_Proxy(
    IUnknown __RPC_FAR *This);

void __RPC_STUB IUnknown_Release_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);
#else

EXTERN_C const IID IID_IUnknown;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("00000000-0000-0000-C000-000000000046")
IUnknown {
public:
    BEGIN_INTERFACE
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */
        _COM_Outptr_ void **ppvObject) = 0;

    virtual ULONG STDMETHODCALLTYPE AddRef(void) = 0;

    virtual ULONG STDMETHODCALLTYPE Release(void) = 0;

    END_INTERFACE
};

#else /* C style interface */

typedef struct IUnknownVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        IUnknown *This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        IUnknown *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        IUnknown *This);

    END_INTERFACE
} IUnknownVtbl;

interface IUnknown {
    CONST_VTBL struct IUnknownVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define IUnknown_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IUnknown_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IUnknown_Release(This) \
    ((This)->lpVtbl->Release(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#if 0
HRESULT STDMETHODCALLTYPE IUnknown_QueryInterface_Proxy( 
    IUnknown * This,
    /* [in] */ REFIID riid,
    /* [annotation][iid_is][out] */ 
    _COM_Outptr_  void **ppvObject);


void __RPC_STUB IUnknown_QueryInterface_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


ULONG STDMETHODCALLTYPE IUnknown_AddRef_Proxy( 
    IUnknown * This);


void __RPC_STUB IUnknown_AddRef_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


ULONG STDMETHODCALLTYPE IUnknown_Release_Proxy( 
    IUnknown * This);


void __RPC_STUB IUnknown_Release_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);
#endif

#endif /* LEGACY MSVC CHECK */

#endif /* __IUnknown_INTERFACE_DEFINED__ */

/* WTypesbase.h */
#if defined(_WIN32) && !defined(OLE2ANSI)
typedef WCHAR OLECHAR;

typedef /* [string] */ __RPC_string OLECHAR *LPOLESTR;

typedef /* [string] */ __RPC_string const OLECHAR *LPCOLESTR;

#define OLESTR(str) L##str

#else

typedef char OLECHAR;
typedef LPSTR LPOLESTR;
typedef LPCSTR LPCOLESTR;
#define OLESTR(str) str
#endif

/* d3dcommon.h */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum D3D_FEATURE_LEVEL {
    D3D_FEATURE_LEVEL_9_1 = 0x9100,
    D3D_FEATURE_LEVEL_9_2 = 0x9200,
    D3D_FEATURE_LEVEL_9_3 = 0x9300,
    D3D_FEATURE_LEVEL_10_0 = 0xa000,
    D3D_FEATURE_LEVEL_10_1 = 0xa100,
    D3D_FEATURE_LEVEL_11_0 = 0xb000,
    D3D_FEATURE_LEVEL_11_1 = 0xb100,
    D3D_FEATURE_LEVEL_12_0 = 0xc000,
    D3D_FEATURE_LEVEL_12_1 = 0xc100
} D3D_FEATURE_LEVEL;

typedef enum D3D_PRIMITIVE_TOPOLOGY {
    D3D_PRIMITIVE_TOPOLOGY_UNDEFINED = 0,
    D3D_PRIMITIVE_TOPOLOGY_POINTLIST = 1,
    D3D_PRIMITIVE_TOPOLOGY_LINELIST = 2,
    D3D_PRIMITIVE_TOPOLOGY_LINESTRIP = 3,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = 5,
    D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ = 10,
    D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ = 11,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ = 12,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ = 13,
    D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST = 33,
    D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST = 34,
    D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST = 35,
    D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST = 36,
    D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST = 37,
    D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST = 38,
    D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST = 39,
    D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST = 40,
    D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST = 41,
    D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST = 42,
    D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST = 43,
    D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST = 44,
    D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST = 45,
    D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST = 46,
    D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST = 47,
    D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST = 48,
    D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST = 49,
    D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST = 50,
    D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST = 51,
    D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST = 52,
    D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST = 53,
    D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST = 54,
    D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST = 55,
    D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST = 56,
    D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST = 57,
    D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST = 58,
    D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST = 59,
    D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST = 60,
    D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST = 61,
    D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST = 62,
    D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST = 63,
    D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST = 64,
    D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED,
    D3D10_PRIMITIVE_TOPOLOGY_POINTLIST = D3D_PRIMITIVE_TOPOLOGY_POINTLIST,
    D3D10_PRIMITIVE_TOPOLOGY_LINELIST = D3D_PRIMITIVE_TOPOLOGY_LINELIST,
    D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,
    D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
    D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
    D3D10_PRIMITIVE_TOPOLOGY_LINELIST_ADJ = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,
    D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ,
    D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,
    D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ,
    D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED,
    D3D11_PRIMITIVE_TOPOLOGY_POINTLIST = D3D_PRIMITIVE_TOPOLOGY_POINTLIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST = D3D_PRIMITIVE_TOPOLOGY_LINELIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,
    D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ,
    D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST,
    D3D11_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST = D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST
} D3D_PRIMITIVE_TOPOLOGY;

#ifdef __cplusplus
}
#endif

/* dxgiformat.h */
typedef enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R32G32B32A32_TYPELESS = 1,
    DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
    DXGI_FORMAT_R32G32B32A32_UINT = 3,
    DXGI_FORMAT_R32G32B32A32_SINT = 4,
    DXGI_FORMAT_R32G32B32_TYPELESS = 5,
    DXGI_FORMAT_R32G32B32_FLOAT = 6,
    DXGI_FORMAT_R32G32B32_UINT = 7,
    DXGI_FORMAT_R32G32B32_SINT = 8,
    DXGI_FORMAT_R16G16B16A16_TYPELESS = 9,
    DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
    DXGI_FORMAT_R16G16B16A16_UNORM = 11,
    DXGI_FORMAT_R16G16B16A16_UINT = 12,
    DXGI_FORMAT_R16G16B16A16_SNORM = 13,
    DXGI_FORMAT_R16G16B16A16_SINT = 14,
    DXGI_FORMAT_R32G32_TYPELESS = 15,
    DXGI_FORMAT_R32G32_FLOAT = 16,
    DXGI_FORMAT_R32G32_UINT = 17,
    DXGI_FORMAT_R32G32_SINT = 18,
    DXGI_FORMAT_R32G8X24_TYPELESS = 19,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
    DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
    DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
    DXGI_FORMAT_R10G10B10A2_TYPELESS = 23,
    DXGI_FORMAT_R10G10B10A2_UNORM = 24,
    DXGI_FORMAT_R10G10B10A2_UINT = 25,
    DXGI_FORMAT_R11G11B10_FLOAT = 26,
    DXGI_FORMAT_R8G8B8A8_TYPELESS = 27,
    DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
    DXGI_FORMAT_R8G8B8A8_UINT = 30,
    DXGI_FORMAT_R8G8B8A8_SNORM = 31,
    DXGI_FORMAT_R8G8B8A8_SINT = 32,
    DXGI_FORMAT_R16G16_TYPELESS = 33,
    DXGI_FORMAT_R16G16_FLOAT = 34,
    DXGI_FORMAT_R16G16_UNORM = 35,
    DXGI_FORMAT_R16G16_UINT = 36,
    DXGI_FORMAT_R16G16_SNORM = 37,
    DXGI_FORMAT_R16G16_SINT = 38,
    DXGI_FORMAT_R32_TYPELESS = 39,
    DXGI_FORMAT_D32_FLOAT = 40,
    DXGI_FORMAT_R32_FLOAT = 41,
    DXGI_FORMAT_R32_UINT = 42,
    DXGI_FORMAT_R32_SINT = 43,
    DXGI_FORMAT_R24G8_TYPELESS = 44,
    DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
    DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
    DXGI_FORMAT_R8G8_TYPELESS = 48,
    DXGI_FORMAT_R8G8_UNORM = 49,
    DXGI_FORMAT_R8G8_UINT = 50,
    DXGI_FORMAT_R8G8_SNORM = 51,
    DXGI_FORMAT_R8G8_SINT = 52,
    DXGI_FORMAT_R16_TYPELESS = 53,
    DXGI_FORMAT_R16_FLOAT = 54,
    DXGI_FORMAT_D16_UNORM = 55,
    DXGI_FORMAT_R16_UNORM = 56,
    DXGI_FORMAT_R16_UINT = 57,
    DXGI_FORMAT_R16_SNORM = 58,
    DXGI_FORMAT_R16_SINT = 59,
    DXGI_FORMAT_R8_TYPELESS = 60,
    DXGI_FORMAT_R8_UNORM = 61,
    DXGI_FORMAT_R8_UINT = 62,
    DXGI_FORMAT_R8_SNORM = 63,
    DXGI_FORMAT_R8_SINT = 64,
    DXGI_FORMAT_A8_UNORM = 65,
    DXGI_FORMAT_R1_UNORM = 66,
    DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67,
    DXGI_FORMAT_R8G8_B8G8_UNORM = 68,
    DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
    DXGI_FORMAT_BC1_TYPELESS = 70,
    DXGI_FORMAT_BC1_UNORM = 71,
    DXGI_FORMAT_BC1_UNORM_SRGB = 72,
    DXGI_FORMAT_BC2_TYPELESS = 73,
    DXGI_FORMAT_BC2_UNORM = 74,
    DXGI_FORMAT_BC2_UNORM_SRGB = 75,
    DXGI_FORMAT_BC3_TYPELESS = 76,
    DXGI_FORMAT_BC3_UNORM = 77,
    DXGI_FORMAT_BC3_UNORM_SRGB = 78,
    DXGI_FORMAT_BC4_TYPELESS = 79,
    DXGI_FORMAT_BC4_UNORM = 80,
    DXGI_FORMAT_BC4_SNORM = 81,
    DXGI_FORMAT_BC5_TYPELESS = 82,
    DXGI_FORMAT_BC5_UNORM = 83,
    DXGI_FORMAT_BC5_SNORM = 84,
    DXGI_FORMAT_B5G6R5_UNORM = 85,
    DXGI_FORMAT_B5G5R5A1_UNORM = 86,
    DXGI_FORMAT_B8G8R8A8_UNORM = 87,
    DXGI_FORMAT_B8G8R8X8_UNORM = 88,
    DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
    DXGI_FORMAT_B8G8R8A8_TYPELESS = 90,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
    DXGI_FORMAT_B8G8R8X8_TYPELESS = 92,
    DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
    DXGI_FORMAT_BC6H_TYPELESS = 94,
    DXGI_FORMAT_BC6H_UF16 = 95,
    DXGI_FORMAT_BC6H_SF16 = 96,
    DXGI_FORMAT_BC7_TYPELESS = 97,
    DXGI_FORMAT_BC7_UNORM = 98,
    DXGI_FORMAT_BC7_UNORM_SRGB = 99,
    DXGI_FORMAT_AYUV = 100,
    DXGI_FORMAT_Y410 = 101,
    DXGI_FORMAT_Y416 = 102,
    DXGI_FORMAT_NV12 = 103,
    DXGI_FORMAT_P010 = 104,
    DXGI_FORMAT_P016 = 105,
    DXGI_FORMAT_420_OPAQUE = 106,
    DXGI_FORMAT_YUY2 = 107,
    DXGI_FORMAT_Y210 = 108,
    DXGI_FORMAT_Y216 = 109,
    DXGI_FORMAT_NV11 = 110,
    DXGI_FORMAT_AI44 = 111,
    DXGI_FORMAT_IA44 = 112,
    DXGI_FORMAT_P8 = 113,
    DXGI_FORMAT_A8P8 = 114,
    DXGI_FORMAT_B4G4R4A4_UNORM = 115,
    DXGI_FORMAT_FORCE_UINT = 0xffffffff
} DXGI_FORMAT;

/* dxgitype.h */
typedef struct DXGI_SAMPLE_DESC {
    UINT Count;
    UINT Quality;
} DXGI_SAMPLE_DESC;

/* d3dcommon.h */
typedef enum _D3D_INCLUDE_TYPE {
    D3D_INCLUDE_LOCAL = 0,
    D3D_INCLUDE_SYSTEM = (D3D_INCLUDE_LOCAL + 1),
    D3D10_INCLUDE_LOCAL = D3D_INCLUDE_LOCAL,
    D3D10_INCLUDE_SYSTEM = D3D_INCLUDE_SYSTEM,
    D3D_INCLUDE_FORCE_DWORD = 0x7fffffff
} D3D_INCLUDE_TYPE;

typedef interface ID3DInclude ID3DInclude;
#undef INTERFACE
#define INTERFACE ID3DInclude
DECLARE_INTERFACE(ID3DInclude) {
    STDMETHOD(Open)
    (THIS_ D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID * ppData, UINT * pBytes) PURE;
    STDMETHOD(Close)
    (THIS_ LPCVOID pData) PURE;
};

typedef struct _D3D_SHADER_MACRO {
    LPCSTR Name;
    LPCSTR Definition;
} D3D_SHADER_MACRO;

typedef struct _D3D_SHADER_MACRO *LPD3D_SHADER_MACRO;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("8BA5FB08-5195-40e2-AC58-0D989C3A0102")
ID3D10Blob : public IUnknown {
public:
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer(void) = 0;

    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize(void) = 0;
};

#else /* C style interface */

#ifndef __ID3D10Blob_FWD_DEFINED__
#define __ID3D10Blob_FWD_DEFINED__
typedef interface ID3D10Blob ID3D10Blob;

#endif /* __ID3D10Blob_FWD_DEFINED__ */

typedef struct ID3D10BlobVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D10Blob *This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D10Blob *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D10Blob *This);

    LPVOID(STDMETHODCALLTYPE *GetBufferPointer)
    (
        ID3D10Blob *This);

    SIZE_T(STDMETHODCALLTYPE *GetBufferSize)
    (
        ID3D10Blob *This);

    END_INTERFACE
} ID3D10BlobVtbl;

interface ID3D10Blob {
    CONST_VTBL struct ID3D10BlobVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D10Blob_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D10Blob_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D10Blob_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D10Blob_GetBufferPointer(This) \
    ((This)->lpVtbl->GetBufferPointer(This))

#define ID3D10Blob_GetBufferSize(This) \
    ((This)->lpVtbl->GetBufferSize(This))

#endif /* COBJMACROS */

#endif /* C style interface */

typedef interface ID3D10Blob *LPD3D10BLOB;
typedef ID3D10Blob ID3DBlob;

typedef ID3DBlob *LPD3DBLOB;
#define IID_ID3DBlob IID_ID3D10Blob

/* d3d12.h */
/* Forward Declarations */

#ifndef __ID3D12Object_FWD_DEFINED__
#define __ID3D12Object_FWD_DEFINED__
typedef interface ID3D12Object ID3D12Object;

#endif /* __ID3D12Object_FWD_DEFINED__ */

#ifndef __ID3D12DeviceChild_FWD_DEFINED__
#define __ID3D12DeviceChild_FWD_DEFINED__
typedef interface ID3D12DeviceChild ID3D12DeviceChild;

#endif /* __ID3D12DeviceChild_FWD_DEFINED__ */

#ifndef __ID3D12RootSignature_FWD_DEFINED__
#define __ID3D12RootSignature_FWD_DEFINED__
typedef interface ID3D12RootSignature ID3D12RootSignature;

#endif /* __ID3D12RootSignature_FWD_DEFINED__ */

#ifndef __ID3D12RootSignatureDeserializer_FWD_DEFINED__
#define __ID3D12RootSignatureDeserializer_FWD_DEFINED__
typedef interface ID3D12RootSignatureDeserializer ID3D12RootSignatureDeserializer;

#endif /* __ID3D12RootSignatureDeserializer_FWD_DEFINED__ */

#ifndef __ID3D12VersionedRootSignatureDeserializer_FWD_DEFINED__
#define __ID3D12VersionedRootSignatureDeserializer_FWD_DEFINED__
typedef interface ID3D12VersionedRootSignatureDeserializer ID3D12VersionedRootSignatureDeserializer;

#endif /* __ID3D12VersionedRootSignatureDeserializer_FWD_DEFINED__ */

#ifndef __ID3D12Pageable_FWD_DEFINED__
#define __ID3D12Pageable_FWD_DEFINED__
typedef interface ID3D12Pageable ID3D12Pageable;

#endif /* __ID3D12Pageable_FWD_DEFINED__ */

#ifndef __ID3D12Heap_FWD_DEFINED__
#define __ID3D12Heap_FWD_DEFINED__
typedef interface ID3D12Heap ID3D12Heap;

#endif /* __ID3D12Heap_FWD_DEFINED__ */

#ifndef __ID3D12Resource_FWD_DEFINED__
#define __ID3D12Resource_FWD_DEFINED__
typedef interface ID3D12Resource ID3D12Resource;

#endif /* __ID3D12Resource_FWD_DEFINED__ */

#ifndef __ID3D12CommandAllocator_FWD_DEFINED__
#define __ID3D12CommandAllocator_FWD_DEFINED__
typedef interface ID3D12CommandAllocator ID3D12CommandAllocator;

#endif /* __ID3D12CommandAllocator_FWD_DEFINED__ */

#ifndef __ID3D12Fence_FWD_DEFINED__
#define __ID3D12Fence_FWD_DEFINED__
typedef interface ID3D12Fence ID3D12Fence;

#endif /* __ID3D12Fence_FWD_DEFINED__ */

#ifndef __ID3D12PipelineState_FWD_DEFINED__
#define __ID3D12PipelineState_FWD_DEFINED__
typedef interface ID3D12PipelineState ID3D12PipelineState;

#endif /* __ID3D12PipelineState_FWD_DEFINED__ */

#ifndef __ID3D12DescriptorHeap_FWD_DEFINED__
#define __ID3D12DescriptorHeap_FWD_DEFINED__
typedef interface ID3D12DescriptorHeap ID3D12DescriptorHeap;

#endif /* __ID3D12DescriptorHeap_FWD_DEFINED__ */

#ifndef __ID3D12QueryHeap_FWD_DEFINED__
#define __ID3D12QueryHeap_FWD_DEFINED__
typedef interface ID3D12QueryHeap ID3D12QueryHeap;

#endif /* __ID3D12QueryHeap_FWD_DEFINED__ */

#ifndef __ID3D12CommandSignature_FWD_DEFINED__
#define __ID3D12CommandSignature_FWD_DEFINED__
typedef interface ID3D12CommandSignature ID3D12CommandSignature;

#endif /* __ID3D12CommandSignature_FWD_DEFINED__ */

#ifndef __ID3D12CommandList_FWD_DEFINED__
#define __ID3D12CommandList_FWD_DEFINED__
typedef interface ID3D12CommandList ID3D12CommandList;

#endif /* __ID3D12CommandList_FWD_DEFINED__ */

#ifndef __ID3D12GraphicsCommandList_FWD_DEFINED__
#define __ID3D12GraphicsCommandList_FWD_DEFINED__
typedef interface ID3D12GraphicsCommandList ID3D12GraphicsCommandList;

#endif /* __ID3D12GraphicsCommandList_FWD_DEFINED__ */

#ifndef __ID3D12GraphicsCommandList1_FWD_DEFINED__
#define __ID3D12GraphicsCommandList1_FWD_DEFINED__
typedef interface ID3D12GraphicsCommandList1 ID3D12GraphicsCommandList1;

#endif /* __ID3D12GraphicsCommandList1_FWD_DEFINED__ */

#ifndef __ID3D12CommandQueue_FWD_DEFINED__
#define __ID3D12CommandQueue_FWD_DEFINED__
typedef interface ID3D12CommandQueue ID3D12CommandQueue;

#endif /* __ID3D12CommandQueue_FWD_DEFINED__ */

#ifndef __ID3D12Device_FWD_DEFINED__
#define __ID3D12Device_FWD_DEFINED__
typedef interface ID3D12Device ID3D12Device;

#endif /* __ID3D12Device_FWD_DEFINED__ */

#ifndef __ID3D12PipelineLibrary_FWD_DEFINED__
#define __ID3D12PipelineLibrary_FWD_DEFINED__
typedef interface ID3D12PipelineLibrary ID3D12PipelineLibrary;

#endif /* __ID3D12PipelineLibrary_FWD_DEFINED__ */

#ifndef __ID3D12PipelineLibrary1_FWD_DEFINED__
#define __ID3D12PipelineLibrary1_FWD_DEFINED__
typedef interface ID3D12PipelineLibrary1 ID3D12PipelineLibrary1;

#endif /* __ID3D12PipelineLibrary1_FWD_DEFINED__ */

#ifndef __ID3D12Device1_FWD_DEFINED__
#define __ID3D12Device1_FWD_DEFINED__
typedef interface ID3D12Device1 ID3D12Device1;

#endif /* __ID3D12Device1_FWD_DEFINED__ */

#ifndef __ID3D12Device2_FWD_DEFINED__
#define __ID3D12Device2_FWD_DEFINED__
typedef interface ID3D12Device2 ID3D12Device2;

#endif /* __ID3D12Device2_FWD_DEFINED__ */

#ifndef __ID3D12Tools_FWD_DEFINED__
#define __ID3D12Tools_FWD_DEFINED__
typedef interface ID3D12Tools ID3D12Tools;

#endif /* __ID3D12Tools_FWD_DEFINED__ */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("c4fec28f-7966-4e95-9f94-f431cb56c3b8")
ID3D12Object : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(
        _In_ REFGUID guid,
        _Inout_ UINT * pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetName(
        _In_z_ LPCWSTR Name) = 0;
};

#else /* C style interface */

typedef struct ID3D12ObjectVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12Object *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12Object *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12Object *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12Object *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12Object *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12Object *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12Object *This,
        _In_z_ LPCWSTR Name);

    END_INTERFACE
} ID3D12ObjectVtbl;

interface ID3D12Object {
    CONST_VTBL struct ID3D12ObjectVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12Object_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12Object_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12Object_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12Object_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12Object_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12Object_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12Object_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#endif /* COBJMACROS */

#endif /* C style interface */

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("905db94b-a00c-4140-9df5-2b64ca9ea357")
ID3D12DeviceChild : public ID3D12Object {
public:
    virtual HRESULT STDMETHODCALLTYPE GetDevice(
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice) = 0;
};

#else /* C style interface */

typedef struct ID3D12DeviceChildVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12DeviceChild *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12DeviceChild *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12DeviceChild *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12DeviceChild *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12DeviceChild *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12DeviceChild *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12DeviceChild *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12DeviceChild *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    END_INTERFACE
} ID3D12DeviceChildVtbl;

interface ID3D12DeviceChild {
    CONST_VTBL struct ID3D12DeviceChildVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12DeviceChild_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12DeviceChild_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12DeviceChild_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12DeviceChild_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12DeviceChild_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12DeviceChild_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12DeviceChild_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12DeviceChild_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#endif /* COBJMACROS */

#endif /* C style interface */

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("c54a6b66-72df-4ee8-8be5-a946a1429214")
ID3D12RootSignature : public ID3D12DeviceChild{
    public :
};

#else /* C style interface */

typedef struct ID3D12RootSignatureVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12RootSignature *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12RootSignature *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12RootSignature *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12RootSignature *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12RootSignature *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12RootSignature *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12RootSignature *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12RootSignature *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    END_INTERFACE
} ID3D12RootSignatureVtbl;

interface ID3D12RootSignature {
    CONST_VTBL struct ID3D12RootSignatureVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12RootSignature_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12RootSignature_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12RootSignature_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12RootSignature_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12RootSignature_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12RootSignature_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12RootSignature_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12RootSignature_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#endif /* COBJMACROS */

#endif /* C style interface */

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("63ee58fb-1268-4835-86da-f008ce62f0d6")
ID3D12Pageable : public ID3D12DeviceChild{
    public :
};

#else /* C style interface */

#ifndef __ID3D12Pageable_FWD_DEFINED__
#define __ID3D12Pageable_FWD_DEFINED__
typedef interface ID3D12Pageable ID3D12Pageable;

#endif /* __ID3D12Pageable_FWD_DEFINED__ */

typedef struct ID3D12PageableVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12Pageable *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12Pageable *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12Pageable *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12Pageable *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12Pageable *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12Pageable *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12Pageable *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12Pageable *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    END_INTERFACE
} ID3D12PageableVtbl;

interface ID3D12Pageable {
    CONST_VTBL struct ID3D12PageableVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12Pageable_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12Pageable_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12Pageable_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12Pageable_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12Pageable_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12Pageable_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12Pageable_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12Pageable_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#endif /* COBJMACROS */

#endif /* C style interface */

typedef struct D3D12_RANGE {
    SIZE_T Begin;
    SIZE_T End;
} D3D12_RANGE;

typedef struct D3D12_BOX {
    UINT left;
    UINT top;
    UINT front;
    UINT right;
    UINT bottom;
    UINT back;
} D3D12_BOX;

typedef enum D3D12_COMMAND_LIST_TYPE {
    D3D12_COMMAND_LIST_TYPE_DIRECT = 0,
    D3D12_COMMAND_LIST_TYPE_BUNDLE = 1,
    D3D12_COMMAND_LIST_TYPE_COMPUTE = 2,
    D3D12_COMMAND_LIST_TYPE_COPY = 3
} D3D12_COMMAND_LIST_TYPE;

typedef enum D3D12_COMMAND_QUEUE_FLAGS {
    D3D12_COMMAND_QUEUE_FLAG_NONE = 0,
    D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT = 0x1
} D3D12_COMMAND_QUEUE_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_COMMAND_QUEUE_FLAGS);
typedef enum D3D12_COMMAND_QUEUE_PRIORITY {
    D3D12_COMMAND_QUEUE_PRIORITY_NORMAL = 0,
    D3D12_COMMAND_QUEUE_PRIORITY_HIGH = 100,
    D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME = 10000
} D3D12_COMMAND_QUEUE_PRIORITY;

typedef struct D3D12_COMMAND_QUEUE_DESC {
    D3D12_COMMAND_LIST_TYPE Type;
    INT Priority;
    D3D12_COMMAND_QUEUE_FLAGS Flags;
    UINT NodeMask;
} D3D12_COMMAND_QUEUE_DESC;

typedef struct D3D12_SHADER_BYTECODE {
    _Field_size_bytes_full_(BytecodeLength) const void *pShaderBytecode;
    SIZE_T BytecodeLength;
} D3D12_SHADER_BYTECODE;

typedef struct D3D12_SO_DECLARATION_ENTRY {
    UINT Stream;
    LPCSTR SemanticName;
    UINT SemanticIndex;
    BYTE StartComponent;
    BYTE ComponentCount;
    BYTE OutputSlot;
} D3D12_SO_DECLARATION_ENTRY;

typedef struct D3D12_STREAM_OUTPUT_DESC {
    _Field_size_full_(NumEntries) const D3D12_SO_DECLARATION_ENTRY *pSODeclaration;
    UINT NumEntries;
    _Field_size_full_(NumStrides) const UINT *pBufferStrides;
    UINT NumStrides;
    UINT RasterizedStream;
} D3D12_STREAM_OUTPUT_DESC;

typedef enum D3D12_BLEND {
    D3D12_BLEND_ZERO = 1,
    D3D12_BLEND_ONE = 2,
    D3D12_BLEND_SRC_COLOR = 3,
    D3D12_BLEND_INV_SRC_COLOR = 4,
    D3D12_BLEND_SRC_ALPHA = 5,
    D3D12_BLEND_INV_SRC_ALPHA = 6,
    D3D12_BLEND_DEST_ALPHA = 7,
    D3D12_BLEND_INV_DEST_ALPHA = 8,
    D3D12_BLEND_DEST_COLOR = 9,
    D3D12_BLEND_INV_DEST_COLOR = 10,
    D3D12_BLEND_SRC_ALPHA_SAT = 11,
    D3D12_BLEND_BLEND_FACTOR = 14,
    D3D12_BLEND_INV_BLEND_FACTOR = 15,
    D3D12_BLEND_SRC1_COLOR = 16,
    D3D12_BLEND_INV_SRC1_COLOR = 17,
    D3D12_BLEND_SRC1_ALPHA = 18,
    D3D12_BLEND_INV_SRC1_ALPHA = 19
} D3D12_BLEND;

typedef enum D3D12_BLEND_OP {
    D3D12_BLEND_OP_ADD = 1,
    D3D12_BLEND_OP_SUBTRACT = 2,
    D3D12_BLEND_OP_REV_SUBTRACT = 3,
    D3D12_BLEND_OP_MIN = 4,
    D3D12_BLEND_OP_MAX = 5
} D3D12_BLEND_OP;

typedef enum D3D12_LOGIC_OP {
    D3D12_LOGIC_OP_CLEAR = 0,
    D3D12_LOGIC_OP_SET = (D3D12_LOGIC_OP_CLEAR + 1),
    D3D12_LOGIC_OP_COPY = (D3D12_LOGIC_OP_SET + 1),
    D3D12_LOGIC_OP_COPY_INVERTED = (D3D12_LOGIC_OP_COPY + 1),
    D3D12_LOGIC_OP_NOOP = (D3D12_LOGIC_OP_COPY_INVERTED + 1),
    D3D12_LOGIC_OP_INVERT = (D3D12_LOGIC_OP_NOOP + 1),
    D3D12_LOGIC_OP_AND = (D3D12_LOGIC_OP_INVERT + 1),
    D3D12_LOGIC_OP_NAND = (D3D12_LOGIC_OP_AND + 1),
    D3D12_LOGIC_OP_OR = (D3D12_LOGIC_OP_NAND + 1),
    D3D12_LOGIC_OP_NOR = (D3D12_LOGIC_OP_OR + 1),
    D3D12_LOGIC_OP_XOR = (D3D12_LOGIC_OP_NOR + 1),
    D3D12_LOGIC_OP_EQUIV = (D3D12_LOGIC_OP_XOR + 1),
    D3D12_LOGIC_OP_AND_REVERSE = (D3D12_LOGIC_OP_EQUIV + 1),
    D3D12_LOGIC_OP_AND_INVERTED = (D3D12_LOGIC_OP_AND_REVERSE + 1),
    D3D12_LOGIC_OP_OR_REVERSE = (D3D12_LOGIC_OP_AND_INVERTED + 1),
    D3D12_LOGIC_OP_OR_INVERTED = (D3D12_LOGIC_OP_OR_REVERSE + 1)
} D3D12_LOGIC_OP;

typedef struct D3D12_RENDER_TARGET_BLEND_DESC {
    BOOL BlendEnable;
    BOOL LogicOpEnable;
    D3D12_BLEND SrcBlend;
    D3D12_BLEND DestBlend;
    D3D12_BLEND_OP BlendOp;
    D3D12_BLEND SrcBlendAlpha;
    D3D12_BLEND DestBlendAlpha;
    D3D12_BLEND_OP BlendOpAlpha;
    D3D12_LOGIC_OP LogicOp;
    UINT8 RenderTargetWriteMask;
} D3D12_RENDER_TARGET_BLEND_DESC;

typedef struct D3D12_BLEND_DESC {
    BOOL AlphaToCoverageEnable;
    BOOL IndependentBlendEnable;
    D3D12_RENDER_TARGET_BLEND_DESC RenderTarget[8];
} D3D12_BLEND_DESC;

typedef enum D3D12_FILL_MODE {
    D3D12_FILL_MODE_WIREFRAME = 2,
    D3D12_FILL_MODE_SOLID = 3
} D3D12_FILL_MODE;

typedef enum D3D12_CULL_MODE {
    D3D12_CULL_MODE_NONE = 1,
    D3D12_CULL_MODE_FRONT = 2,
    D3D12_CULL_MODE_BACK = 3
} D3D12_CULL_MODE;

typedef enum D3D12_CONSERVATIVE_RASTERIZATION_MODE {
    D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF = 0,
    D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON = 1
} D3D12_CONSERVATIVE_RASTERIZATION_MODE;

typedef struct D3D12_RASTERIZER_DESC {
    D3D12_FILL_MODE FillMode;
    D3D12_CULL_MODE CullMode;
    BOOL FrontCounterClockwise;
    INT DepthBias;
    FLOAT DepthBiasClamp;
    FLOAT SlopeScaledDepthBias;
    BOOL DepthClipEnable;
    BOOL MultisampleEnable;
    BOOL AntialiasedLineEnable;
    UINT ForcedSampleCount;
    D3D12_CONSERVATIVE_RASTERIZATION_MODE ConservativeRaster;
} D3D12_RASTERIZER_DESC;

typedef enum D3D12_DEPTH_WRITE_MASK {
    D3D12_DEPTH_WRITE_MASK_ZERO = 0,
    D3D12_DEPTH_WRITE_MASK_ALL = 1
} D3D12_DEPTH_WRITE_MASK;

typedef enum D3D12_COMPARISON_FUNC {
    D3D12_COMPARISON_FUNC_NEVER = 1,
    D3D12_COMPARISON_FUNC_LESS = 2,
    D3D12_COMPARISON_FUNC_EQUAL = 3,
    D3D12_COMPARISON_FUNC_LESS_EQUAL = 4,
    D3D12_COMPARISON_FUNC_GREATER = 5,
    D3D12_COMPARISON_FUNC_NOT_EQUAL = 6,
    D3D12_COMPARISON_FUNC_GREATER_EQUAL = 7,
    D3D12_COMPARISON_FUNC_ALWAYS = 8
} D3D12_COMPARISON_FUNC;

typedef enum D3D12_STENCIL_OP {
    D3D12_STENCIL_OP_KEEP = 1,
    D3D12_STENCIL_OP_ZERO = 2,
    D3D12_STENCIL_OP_REPLACE = 3,
    D3D12_STENCIL_OP_INCR_SAT = 4,
    D3D12_STENCIL_OP_DECR_SAT = 5,
    D3D12_STENCIL_OP_INVERT = 6,
    D3D12_STENCIL_OP_INCR = 7,
    D3D12_STENCIL_OP_DECR = 8
} D3D12_STENCIL_OP;

typedef struct D3D12_DEPTH_STENCILOP_DESC {
    D3D12_STENCIL_OP StencilFailOp;
    D3D12_STENCIL_OP StencilDepthFailOp;
    D3D12_STENCIL_OP StencilPassOp;
    D3D12_COMPARISON_FUNC StencilFunc;
} D3D12_DEPTH_STENCILOP_DESC;

typedef struct D3D12_DEPTH_STENCIL_DESC {
    BOOL DepthEnable;
    D3D12_DEPTH_WRITE_MASK DepthWriteMask;
    D3D12_COMPARISON_FUNC DepthFunc;
    BOOL StencilEnable;
    UINT8 StencilReadMask;
    UINT8 StencilWriteMask;
    D3D12_DEPTH_STENCILOP_DESC FrontFace;
    D3D12_DEPTH_STENCILOP_DESC BackFace;
} D3D12_DEPTH_STENCIL_DESC;

typedef enum D3D12_INPUT_CLASSIFICATION {
    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA = 0,
    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA = 1
} D3D12_INPUT_CLASSIFICATION;

typedef struct D3D12_INPUT_ELEMENT_DESC {
    LPCSTR SemanticName;
    UINT SemanticIndex;
    DXGI_FORMAT Format;
    UINT InputSlot;
    UINT AlignedByteOffset;
    D3D12_INPUT_CLASSIFICATION InputSlotClass;
    UINT InstanceDataStepRate;
} D3D12_INPUT_ELEMENT_DESC;

typedef struct D3D12_INPUT_LAYOUT_DESC {
    _Field_size_full_(NumElements) const D3D12_INPUT_ELEMENT_DESC *pInputElementDescs;
    UINT NumElements;
} D3D12_INPUT_LAYOUT_DESC;

typedef enum D3D12_PIPELINE_STATE_FLAGS {
    D3D12_PIPELINE_STATE_FLAG_NONE = 0,
    D3D12_PIPELINE_STATE_FLAG_TOOL_DEBUG = 0x1
} D3D12_PIPELINE_STATE_FLAGS;

typedef enum D3D12_INDEX_BUFFER_STRIP_CUT_VALUE {
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED = 0,
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF = 1,
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF = 2
} D3D12_INDEX_BUFFER_STRIP_CUT_VALUE;

typedef enum D3D12_PRIMITIVE_TOPOLOGY_TYPE {
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED = 0,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT = 1,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE = 2,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE = 3,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH = 4
} D3D12_PRIMITIVE_TOPOLOGY_TYPE;

typedef struct D3D12_CACHED_PIPELINE_STATE {
    _Field_size_bytes_full_(CachedBlobSizeInBytes) const void *pCachedBlob;
    SIZE_T CachedBlobSizeInBytes;
} D3D12_CACHED_PIPELINE_STATE;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_PIPELINE_STATE_FLAGS);
typedef struct D3D12_GRAPHICS_PIPELINE_STATE_DESC {
    ID3D12RootSignature *pRootSignature;
    D3D12_SHADER_BYTECODE VS;
    D3D12_SHADER_BYTECODE PS;
    D3D12_SHADER_BYTECODE DS;
    D3D12_SHADER_BYTECODE HS;
    D3D12_SHADER_BYTECODE GS;
    D3D12_STREAM_OUTPUT_DESC StreamOutput;
    D3D12_BLEND_DESC BlendState;
    UINT SampleMask;
    D3D12_RASTERIZER_DESC RasterizerState;
    D3D12_DEPTH_STENCIL_DESC DepthStencilState;
    D3D12_INPUT_LAYOUT_DESC InputLayout;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType;
    UINT NumRenderTargets;
    DXGI_FORMAT RTVFormats[8];
    DXGI_FORMAT DSVFormat;
    DXGI_SAMPLE_DESC SampleDesc;
    UINT NodeMask;
    D3D12_CACHED_PIPELINE_STATE CachedPSO;
    D3D12_PIPELINE_STATE_FLAGS Flags;
} D3D12_GRAPHICS_PIPELINE_STATE_DESC;

typedef struct D3D12_COMPUTE_PIPELINE_STATE_DESC {
    ID3D12RootSignature *pRootSignature;
    D3D12_SHADER_BYTECODE CS;
    UINT NodeMask;
    D3D12_CACHED_PIPELINE_STATE CachedPSO;
    D3D12_PIPELINE_STATE_FLAGS Flags;
} D3D12_COMPUTE_PIPELINE_STATE_DESC;

typedef struct D3D12_RESOURCE_ALLOCATION_INFO {
    UINT64 SizeInBytes;
    UINT64 Alignment;
} D3D12_RESOURCE_ALLOCATION_INFO;

typedef enum D3D12_RESOURCE_DIMENSION {
    D3D12_RESOURCE_DIMENSION_UNKNOWN = 0,
    D3D12_RESOURCE_DIMENSION_BUFFER = 1,
    D3D12_RESOURCE_DIMENSION_TEXTURE1D = 2,
    D3D12_RESOURCE_DIMENSION_TEXTURE2D = 3,
    D3D12_RESOURCE_DIMENSION_TEXTURE3D = 4
} D3D12_RESOURCE_DIMENSION;

typedef enum D3D12_TEXTURE_LAYOUT {
    D3D12_TEXTURE_LAYOUT_UNKNOWN = 0,
    D3D12_TEXTURE_LAYOUT_ROW_MAJOR = 1,
    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE = 2,
    D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE = 3
} D3D12_TEXTURE_LAYOUT;

typedef enum D3D12_RESOURCE_FLAGS {
    D3D12_RESOURCE_FLAG_NONE = 0,
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET = 0x1,
    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL = 0x2,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS = 0x4,
    D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE = 0x8,
    D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER = 0x10,
    D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS = 0x20
} D3D12_RESOURCE_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_RESOURCE_FLAGS);
typedef struct D3D12_RESOURCE_DESC {
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Alignment;
    UINT64 Width;
    UINT Height;
    UINT16 DepthOrArraySize;
    UINT16 MipLevels;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;
} D3D12_RESOURCE_DESC;

typedef enum D3D12_CPU_PAGE_PROPERTY {
    D3D12_CPU_PAGE_PROPERTY_UNKNOWN = 0,
    D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE = 1,
    D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE = 2,
    D3D12_CPU_PAGE_PROPERTY_WRITE_BACK = 3
} D3D12_CPU_PAGE_PROPERTY;

typedef enum D3D12_MEMORY_POOL {
    D3D12_MEMORY_POOL_UNKNOWN = 0,
    D3D12_MEMORY_POOL_L0 = 1,
    D3D12_MEMORY_POOL_L1 = 2
} D3D12_MEMORY_POOL;

typedef enum D3D12_HEAP_TYPE {
    D3D12_HEAP_TYPE_DEFAULT = 1,
    D3D12_HEAP_TYPE_UPLOAD = 2,
    D3D12_HEAP_TYPE_READBACK = 3,
    D3D12_HEAP_TYPE_CUSTOM = 4
} D3D12_HEAP_TYPE;

typedef struct D3D12_HEAP_PROPERTIES {
    D3D12_HEAP_TYPE Type;
    D3D12_CPU_PAGE_PROPERTY CPUPageProperty;
    D3D12_MEMORY_POOL MemoryPoolPreference;
    UINT CreationNodeMask;
    UINT VisibleNodeMask;
} D3D12_HEAP_PROPERTIES;

typedef enum D3D12_HEAP_FLAGS {
    D3D12_HEAP_FLAG_NONE = 0,
    D3D12_HEAP_FLAG_SHARED = 0x1,
    D3D12_HEAP_FLAG_DENY_BUFFERS = 0x4,
    D3D12_HEAP_FLAG_ALLOW_DISPLAY = 0x8,
    D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER = 0x20,
    D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES = 0x40,
    D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES = 0x80,
    D3D12_HEAP_FLAG_HARDWARE_PROTECTED = 0x100,
    D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH = 0x200,
    D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES = 0,
    D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS = 0xc0,
    D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES = 0x44,
    D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES = 0x84
} D3D12_HEAP_FLAGS;

typedef enum D3D12_TILE_MAPPING_FLAGS {
    D3D12_TILE_MAPPING_FLAG_NONE = 0,
    D3D12_TILE_MAPPING_FLAG_NO_HAZARD = 0x1
} D3D12_TILE_MAPPING_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_TILE_MAPPING_FLAGS);
typedef enum D3D12_TILE_COPY_FLAGS {
    D3D12_TILE_COPY_FLAG_NONE = 0,
    D3D12_TILE_COPY_FLAG_NO_HAZARD = 0x1,
    D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE = 0x2,
    D3D12_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER = 0x4
} D3D12_TILE_COPY_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_TILE_COPY_FLAGS);
typedef enum D3D12_RESOURCE_STATES {
    D3D12_RESOURCE_STATE_COMMON = 0,
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER = 0x1,
    D3D12_RESOURCE_STATE_INDEX_BUFFER = 0x2,
    D3D12_RESOURCE_STATE_RENDER_TARGET = 0x4,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS = 0x8,
    D3D12_RESOURCE_STATE_DEPTH_WRITE = 0x10,
    D3D12_RESOURCE_STATE_DEPTH_READ = 0x20,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE = 0x40,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 0x80,
    D3D12_RESOURCE_STATE_STREAM_OUT = 0x100,
    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT = 0x200,
    D3D12_RESOURCE_STATE_COPY_DEST = 0x400,
    D3D12_RESOURCE_STATE_COPY_SOURCE = 0x800,
    D3D12_RESOURCE_STATE_RESOLVE_DEST = 0x1000,
    D3D12_RESOURCE_STATE_RESOLVE_SOURCE = 0x2000,
    D3D12_RESOURCE_STATE_GENERIC_READ = (((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
    D3D12_RESOURCE_STATE_PRESENT = 0,
    D3D12_RESOURCE_STATE_PREDICATION = 0x200
} D3D12_RESOURCE_STATES;

typedef struct D3D12_DEPTH_STENCIL_VALUE {
    FLOAT Depth;
    UINT8 Stencil;
} D3D12_DEPTH_STENCIL_VALUE;

typedef struct D3D12_CLEAR_VALUE {
    DXGI_FORMAT Format;
    union {
        FLOAT Color[4];
        D3D12_DEPTH_STENCIL_VALUE DepthStencil;
    };
} D3D12_CLEAR_VALUE;

typedef enum D3D12_FEATURE {
    D3D12_FEATURE_D3D12_OPTIONS = 0,
    D3D12_FEATURE_ARCHITECTURE = 1,
    D3D12_FEATURE_FEATURE_LEVELS = 2,
    D3D12_FEATURE_FORMAT_SUPPORT = 3,
    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS = 4,
    D3D12_FEATURE_FORMAT_INFO = 5,
    D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT = 6,
    D3D12_FEATURE_SHADER_MODEL = 7,
    D3D12_FEATURE_D3D12_OPTIONS1 = 8,
    D3D12_FEATURE_ROOT_SIGNATURE = 12,
    D3D12_FEATURE_ARCHITECTURE1 = 16,
    D3D12_FEATURE_D3D12_OPTIONS2 = 18,
    D3D12_FEATURE_SHADER_CACHE = 19,
    D3D12_FEATURE_COMMAND_QUEUE_PRIORITY = 20
} D3D12_FEATURE;

typedef enum D3D12_CLEAR_FLAGS {
    D3D12_CLEAR_FLAG_DEPTH = 0x1,
    D3D12_CLEAR_FLAG_STENCIL = 0x2
} D3D12_CLEAR_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_CLEAR_FLAGS);
typedef enum D3D12_FENCE_FLAGS {
    D3D12_FENCE_FLAG_NONE = 0,
    D3D12_FENCE_FLAG_SHARED = 0x1,
    D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER = 0x2
} D3D12_FENCE_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_FENCE_FLAGS);
typedef enum D3D12_DESCRIPTOR_HEAP_TYPE {
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV = 0,
    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER = (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV + 1),
    D3D12_DESCRIPTOR_HEAP_TYPE_RTV = (D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER + 1),
    D3D12_DESCRIPTOR_HEAP_TYPE_DSV = (D3D12_DESCRIPTOR_HEAP_TYPE_RTV + 1),
    D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES = (D3D12_DESCRIPTOR_HEAP_TYPE_DSV + 1)
} D3D12_DESCRIPTOR_HEAP_TYPE;

typedef enum D3D12_DESCRIPTOR_HEAP_FLAGS {
    D3D12_DESCRIPTOR_HEAP_FLAG_NONE = 0,
    D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE = 0x1
} D3D12_DESCRIPTOR_HEAP_FLAGS;

typedef enum D3D12_SHADER_COMPONENT_MAPPING {
    D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0 = 0,
    D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1 = 1,
    D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2 = 2,
    D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3 = 3,
    D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0 = 4,
    D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1 = 5
} D3D12_SHADER_COMPONENT_MAPPING;

#define D3D12_SHADER_COMPONENT_MAPPING_MASK 0x7
#define D3D12_SHADER_COMPONENT_MAPPING_SHIFT 3
#define D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES (1 << (D3D12_SHADER_COMPONENT_MAPPING_SHIFT * 4))
#define D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(Src0, Src1, Src2, Src3) ((((Src0)&D3D12_SHADER_COMPONENT_MAPPING_MASK) |                                                 \
                                                                          (((Src1)&D3D12_SHADER_COMPONENT_MAPPING_MASK) << D3D12_SHADER_COMPONENT_MAPPING_SHIFT) |       \
                                                                          (((Src2)&D3D12_SHADER_COMPONENT_MAPPING_MASK) << (D3D12_SHADER_COMPONENT_MAPPING_SHIFT * 2)) | \
                                                                          (((Src3)&D3D12_SHADER_COMPONENT_MAPPING_MASK) << (D3D12_SHADER_COMPONENT_MAPPING_SHIFT * 3)) | \
                                                                          D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES))
#define D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(ComponentToExtract, Mapping) ((D3D12_SHADER_COMPONENT_MAPPING)(Mapping >> (D3D12_SHADER_COMPONENT_MAPPING_SHIFT * ComponentToExtract) & D3D12_SHADER_COMPONENT_MAPPING_MASK))
#define D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 2, 3)

typedef enum D3D12_BUFFER_SRV_FLAGS {
    D3D12_BUFFER_SRV_FLAG_NONE = 0,
    D3D12_BUFFER_SRV_FLAG_RAW = 0x1
} D3D12_BUFFER_SRV_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_BUFFER_SRV_FLAGS);
typedef struct D3D12_BUFFER_SRV {
    UINT64 FirstElement;
    UINT NumElements;
    UINT StructureByteStride;
    D3D12_BUFFER_SRV_FLAGS Flags;
} D3D12_BUFFER_SRV;

typedef struct D3D12_TEX1D_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    FLOAT ResourceMinLODClamp;
} D3D12_TEX1D_SRV;

typedef struct D3D12_TEX1D_ARRAY_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    UINT FirstArraySlice;
    UINT ArraySize;
    FLOAT ResourceMinLODClamp;
} D3D12_TEX1D_ARRAY_SRV;

typedef struct D3D12_TEX2D_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    UINT PlaneSlice;
    FLOAT ResourceMinLODClamp;
} D3D12_TEX2D_SRV;

typedef struct D3D12_TEX2D_ARRAY_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    UINT FirstArraySlice;
    UINT ArraySize;
    UINT PlaneSlice;
    FLOAT ResourceMinLODClamp;
} D3D12_TEX2D_ARRAY_SRV;

typedef struct D3D12_TEX3D_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    FLOAT ResourceMinLODClamp;
} D3D12_TEX3D_SRV;

typedef struct D3D12_TEXCUBE_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    FLOAT ResourceMinLODClamp;
} D3D12_TEXCUBE_SRV;

typedef struct D3D12_TEXCUBE_ARRAY_SRV {
    UINT MostDetailedMip;
    UINT MipLevels;
    UINT First2DArrayFace;
    UINT NumCubes;
    FLOAT ResourceMinLODClamp;
} D3D12_TEXCUBE_ARRAY_SRV;

typedef struct D3D12_TEX2DMS_SRV {
    UINT UnusedField_NothingToDefine;
} D3D12_TEX2DMS_SRV;

typedef struct D3D12_TEX2DMS_ARRAY_SRV {
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D12_TEX2DMS_ARRAY_SRV;

typedef enum D3D12_SRV_DIMENSION {
    D3D12_SRV_DIMENSION_UNKNOWN = 0,
    D3D12_SRV_DIMENSION_BUFFER = 1,
    D3D12_SRV_DIMENSION_TEXTURE1D = 2,
    D3D12_SRV_DIMENSION_TEXTURE1DARRAY = 3,
    D3D12_SRV_DIMENSION_TEXTURE2D = 4,
    D3D12_SRV_DIMENSION_TEXTURE2DARRAY = 5,
    D3D12_SRV_DIMENSION_TEXTURE2DMS = 6,
    D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY = 7,
    D3D12_SRV_DIMENSION_TEXTURE3D = 8,
    D3D12_SRV_DIMENSION_TEXTURECUBE = 9,
    D3D12_SRV_DIMENSION_TEXTURECUBEARRAY = 10
} D3D12_SRV_DIMENSION;

typedef struct D3D12_SHADER_RESOURCE_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D12_SRV_DIMENSION ViewDimension;
    UINT Shader4ComponentMapping;
    union {
        D3D12_BUFFER_SRV Buffer;
        D3D12_TEX1D_SRV Texture1D;
        D3D12_TEX1D_ARRAY_SRV Texture1DArray;
        D3D12_TEX2D_SRV Texture2D;
        D3D12_TEX2D_ARRAY_SRV Texture2DArray;
        D3D12_TEX2DMS_SRV Texture2DMS;
        D3D12_TEX2DMS_ARRAY_SRV Texture2DMSArray;
        D3D12_TEX3D_SRV Texture3D;
        D3D12_TEXCUBE_SRV TextureCube;
        D3D12_TEXCUBE_ARRAY_SRV TextureCubeArray;
    };
} D3D12_SHADER_RESOURCE_VIEW_DESC;

typedef UINT64 D3D12_GPU_VIRTUAL_ADDRESS;

typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
    UINT SizeInBytes;
} D3D12_CONSTANT_BUFFER_VIEW_DESC;

typedef struct D3D12_CPU_DESCRIPTOR_HANDLE {
    SIZE_T ptr;
} D3D12_CPU_DESCRIPTOR_HANDLE;

typedef struct D3D12_GPU_DESCRIPTOR_HANDLE {
    UINT64 ptr;
} D3D12_GPU_DESCRIPTOR_HANDLE;

typedef enum D3D12_BUFFER_UAV_FLAGS {
    D3D12_BUFFER_UAV_FLAG_NONE = 0,
    D3D12_BUFFER_UAV_FLAG_RAW = 0x1
} D3D12_BUFFER_UAV_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_BUFFER_UAV_FLAGS);
typedef struct D3D12_BUFFER_UAV {
    UINT64 FirstElement;
    UINT NumElements;
    UINT StructureByteStride;
    UINT64 CounterOffsetInBytes;
    D3D12_BUFFER_UAV_FLAGS Flags;
} D3D12_BUFFER_UAV;

typedef struct D3D12_TEX1D_UAV {
    UINT MipSlice;
} D3D12_TEX1D_UAV;

typedef struct D3D12_TEX1D_ARRAY_UAV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D12_TEX1D_ARRAY_UAV;

typedef struct D3D12_TEX2D_UAV {
    UINT MipSlice;
    UINT PlaneSlice;
} D3D12_TEX2D_UAV;

typedef struct D3D12_TEX2D_ARRAY_UAV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
    UINT PlaneSlice;
} D3D12_TEX2D_ARRAY_UAV;

typedef struct D3D12_TEX3D_UAV {
    UINT MipSlice;
    UINT FirstWSlice;
    UINT WSize;
} D3D12_TEX3D_UAV;

typedef enum D3D12_UAV_DIMENSION {
    D3D12_UAV_DIMENSION_UNKNOWN = 0,
    D3D12_UAV_DIMENSION_BUFFER = 1,
    D3D12_UAV_DIMENSION_TEXTURE1D = 2,
    D3D12_UAV_DIMENSION_TEXTURE1DARRAY = 3,
    D3D12_UAV_DIMENSION_TEXTURE2D = 4,
    D3D12_UAV_DIMENSION_TEXTURE2DARRAY = 5,
    D3D12_UAV_DIMENSION_TEXTURE3D = 8
} D3D12_UAV_DIMENSION;

typedef struct D3D12_UNORDERED_ACCESS_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D12_UAV_DIMENSION ViewDimension;
    union {
        D3D12_BUFFER_UAV Buffer;
        D3D12_TEX1D_UAV Texture1D;
        D3D12_TEX1D_ARRAY_UAV Texture1DArray;
        D3D12_TEX2D_UAV Texture2D;
        D3D12_TEX2D_ARRAY_UAV Texture2DArray;
        D3D12_TEX3D_UAV Texture3D;
    };
} D3D12_UNORDERED_ACCESS_VIEW_DESC;

typedef struct D3D12_BUFFER_RTV {
    UINT64 FirstElement;
    UINT NumElements;
} D3D12_BUFFER_RTV;

typedef struct D3D12_TEX1D_RTV {
    UINT MipSlice;
} D3D12_TEX1D_RTV;

typedef struct D3D12_TEX1D_ARRAY_RTV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D12_TEX1D_ARRAY_RTV;

typedef struct D3D12_TEX2D_RTV {
    UINT MipSlice;
    UINT PlaneSlice;
} D3D12_TEX2D_RTV;

typedef struct D3D12_TEX2DMS_RTV {
    UINT UnusedField_NothingToDefine;
} D3D12_TEX2DMS_RTV;

typedef struct D3D12_TEX2D_ARRAY_RTV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
    UINT PlaneSlice;
} D3D12_TEX2D_ARRAY_RTV;

typedef struct D3D12_TEX2DMS_ARRAY_RTV {
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D12_TEX2DMS_ARRAY_RTV;

typedef struct D3D12_TEX3D_RTV {
    UINT MipSlice;
    UINT FirstWSlice;
    UINT WSize;
} D3D12_TEX3D_RTV;

typedef enum D3D12_RTV_DIMENSION {
    D3D12_RTV_DIMENSION_UNKNOWN = 0,
    D3D12_RTV_DIMENSION_BUFFER = 1,
    D3D12_RTV_DIMENSION_TEXTURE1D = 2,
    D3D12_RTV_DIMENSION_TEXTURE1DARRAY = 3,
    D3D12_RTV_DIMENSION_TEXTURE2D = 4,
    D3D12_RTV_DIMENSION_TEXTURE2DARRAY = 5,
    D3D12_RTV_DIMENSION_TEXTURE2DMS = 6,
    D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY = 7,
    D3D12_RTV_DIMENSION_TEXTURE3D = 8
} D3D12_RTV_DIMENSION;

typedef struct D3D12_RENDER_TARGET_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D12_RTV_DIMENSION ViewDimension;
    union {
        D3D12_BUFFER_RTV Buffer;
        D3D12_TEX1D_RTV Texture1D;
        D3D12_TEX1D_ARRAY_RTV Texture1DArray;
        D3D12_TEX2D_RTV Texture2D;
        D3D12_TEX2D_ARRAY_RTV Texture2DArray;
        D3D12_TEX2DMS_RTV Texture2DMS;
        D3D12_TEX2DMS_ARRAY_RTV Texture2DMSArray;
        D3D12_TEX3D_RTV Texture3D;
    };
} D3D12_RENDER_TARGET_VIEW_DESC;

typedef struct D3D12_TEX1D_DSV {
    UINT MipSlice;
} D3D12_TEX1D_DSV;

typedef struct D3D12_TEX1D_ARRAY_DSV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D12_TEX1D_ARRAY_DSV;

typedef struct D3D12_TEX2D_DSV {
    UINT MipSlice;
} D3D12_TEX2D_DSV;

typedef struct D3D12_TEX2D_ARRAY_DSV {
    UINT MipSlice;
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D12_TEX2D_ARRAY_DSV;

typedef struct D3D12_TEX2DMS_DSV {
    UINT UnusedField_NothingToDefine;
} D3D12_TEX2DMS_DSV;

typedef struct D3D12_TEX2DMS_ARRAY_DSV {
    UINT FirstArraySlice;
    UINT ArraySize;
} D3D12_TEX2DMS_ARRAY_DSV;

typedef enum D3D12_DSV_FLAGS {
    D3D12_DSV_FLAG_NONE = 0,
    D3D12_DSV_FLAG_READ_ONLY_DEPTH = 0x1,
    D3D12_DSV_FLAG_READ_ONLY_STENCIL = 0x2
} D3D12_DSV_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_DSV_FLAGS);
typedef enum D3D12_DSV_DIMENSION {
    D3D12_DSV_DIMENSION_UNKNOWN = 0,
    D3D12_DSV_DIMENSION_TEXTURE1D = 1,
    D3D12_DSV_DIMENSION_TEXTURE1DARRAY = 2,
    D3D12_DSV_DIMENSION_TEXTURE2D = 3,
    D3D12_DSV_DIMENSION_TEXTURE2DARRAY = 4,
    D3D12_DSV_DIMENSION_TEXTURE2DMS = 5,
    D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY = 6
} D3D12_DSV_DIMENSION;

typedef struct D3D12_DEPTH_STENCIL_VIEW_DESC {
    DXGI_FORMAT Format;
    D3D12_DSV_DIMENSION ViewDimension;
    D3D12_DSV_FLAGS Flags;
    union {
        D3D12_TEX1D_DSV Texture1D;
        D3D12_TEX1D_ARRAY_DSV Texture1DArray;
        D3D12_TEX2D_DSV Texture2D;
        D3D12_TEX2D_ARRAY_DSV Texture2DArray;
        D3D12_TEX2DMS_DSV Texture2DMS;
        D3D12_TEX2DMS_ARRAY_DSV Texture2DMSArray;
    };
} D3D12_DEPTH_STENCIL_VIEW_DESC;

typedef enum D3D12_FILTER {
    D3D12_FILTER_MIN_MAG_MIP_POINT = 0,
    D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR = 0x1,
    D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x4,
    D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR = 0x5,
    D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT = 0x10,
    D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x11,
    D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT = 0x14,
    D3D12_FILTER_MIN_MAG_MIP_LINEAR = 0x15,
    D3D12_FILTER_ANISOTROPIC = 0x55,
    D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT = 0x80,
    D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR = 0x81,
    D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x84,
    D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR = 0x85,
    D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT = 0x90,
    D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x91,
    D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT = 0x94,
    D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR = 0x95,
    D3D12_FILTER_COMPARISON_ANISOTROPIC = 0xd5,
    D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT = 0x100,
    D3D12_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR = 0x101,
    D3D12_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x104,
    D3D12_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR = 0x105,
    D3D12_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT = 0x110,
    D3D12_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x111,
    D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT = 0x114,
    D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR = 0x115,
    D3D12_FILTER_MINIMUM_ANISOTROPIC = 0x155,
    D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT = 0x180,
    D3D12_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR = 0x181,
    D3D12_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x184,
    D3D12_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR = 0x185,
    D3D12_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT = 0x190,
    D3D12_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x191,
    D3D12_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT = 0x194,
    D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR = 0x195,
    D3D12_FILTER_MAXIMUM_ANISOTROPIC = 0x1d5
} D3D12_FILTER;

typedef enum D3D12_TEXTURE_ADDRESS_MODE {
    D3D12_TEXTURE_ADDRESS_MODE_WRAP = 1,
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR = 2,
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP = 3,
    D3D12_TEXTURE_ADDRESS_MODE_BORDER = 4,
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE = 5
} D3D12_TEXTURE_ADDRESS_MODE;

typedef struct D3D12_SAMPLER_DESC {
    D3D12_FILTER Filter;
    D3D12_TEXTURE_ADDRESS_MODE AddressU;
    D3D12_TEXTURE_ADDRESS_MODE AddressV;
    D3D12_TEXTURE_ADDRESS_MODE AddressW;
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    D3D12_COMPARISON_FUNC ComparisonFunc;
    FLOAT BorderColor[4];
    FLOAT MinLOD;
    FLOAT MaxLOD;
} D3D12_SAMPLER_DESC;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_DESCRIPTOR_HEAP_FLAGS);
typedef struct D3D12_DESCRIPTOR_HEAP_DESC {
    D3D12_DESCRIPTOR_HEAP_TYPE Type;
    UINT NumDescriptors;
    D3D12_DESCRIPTOR_HEAP_FLAGS Flags;
    UINT NodeMask;
} D3D12_DESCRIPTOR_HEAP_DESC;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_HEAP_FLAGS);
typedef struct D3D12_HEAP_DESC {
    UINT64 SizeInBytes;
    D3D12_HEAP_PROPERTIES Properties;
    UINT64 Alignment;
    D3D12_HEAP_FLAGS Flags;
} D3D12_HEAP_DESC;

typedef struct D3D12_SUBRESOURCE_FOOTPRINT {
    DXGI_FORMAT Format;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT RowPitch;
} D3D12_SUBRESOURCE_FOOTPRINT;

typedef struct D3D12_PLACED_SUBRESOURCE_FOOTPRINT {
    UINT64 Offset;
    D3D12_SUBRESOURCE_FOOTPRINT Footprint;
} D3D12_PLACED_SUBRESOURCE_FOOTPRINT;

typedef enum D3D12_QUERY_HEAP_TYPE {
    D3D12_QUERY_HEAP_TYPE_OCCLUSION = 0,
    D3D12_QUERY_HEAP_TYPE_TIMESTAMP = 1,
    D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS = 2,
    D3D12_QUERY_HEAP_TYPE_SO_STATISTICS = 3
} D3D12_QUERY_HEAP_TYPE;

typedef struct D3D12_QUERY_HEAP_DESC {
    D3D12_QUERY_HEAP_TYPE Type;
    UINT Count;
    UINT NodeMask;
} D3D12_QUERY_HEAP_DESC;

typedef enum D3D12_INDIRECT_ARGUMENT_TYPE {
    D3D12_INDIRECT_ARGUMENT_TYPE_DRAW = 0,
    D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED = (D3D12_INDIRECT_ARGUMENT_TYPE_DRAW + 1),
    D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH = (D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED + 1),
    D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW = (D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH + 1),
    D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW = (D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW + 1),
    D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT = (D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW + 1),
    D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW = (D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT + 1),
    D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW = (D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW + 1),
    D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW = (D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW + 1)
} D3D12_INDIRECT_ARGUMENT_TYPE;

typedef struct D3D12_INDIRECT_ARGUMENT_DESC {
    D3D12_INDIRECT_ARGUMENT_TYPE Type;
    union {
        struct
        {
            UINT Slot;
        } VertexBuffer;
        struct
        {
            UINT RootParameterIndex;
            UINT DestOffsetIn32BitValues;
            UINT Num32BitValuesToSet;
        } Constant;
        struct
        {
            UINT RootParameterIndex;
        } ConstantBufferView;
        struct
        {
            UINT RootParameterIndex;
        } ShaderResourceView;
        struct
        {
            UINT RootParameterIndex;
        } UnorderedAccessView;
    };
} D3D12_INDIRECT_ARGUMENT_DESC;

typedef struct D3D12_COMMAND_SIGNATURE_DESC {
    UINT ByteStride;
    UINT NumArgumentDescs;
    _Field_size_full_(NumArgumentDescs) const D3D12_INDIRECT_ARGUMENT_DESC *pArgumentDescs;
    UINT NodeMask;
} D3D12_COMMAND_SIGNATURE_DESC;

typedef struct D3D12_PACKED_MIP_INFO {
    UINT8 NumStandardMips;
    UINT8 NumPackedMips;
    UINT NumTilesForPackedMips;
    UINT StartTileIndexInOverallResource;
} D3D12_PACKED_MIP_INFO;

typedef struct D3D12_TILE_SHAPE {
    UINT WidthInTexels;
    UINT HeightInTexels;
    UINT DepthInTexels;
} D3D12_TILE_SHAPE;

typedef struct D3D12_SUBRESOURCE_TILING {
    UINT WidthInTiles;
    UINT16 HeightInTiles;
    UINT16 DepthInTiles;
    UINT StartTileIndexInOverallResource;
} D3D12_SUBRESOURCE_TILING;

typedef struct D3D12_TILED_RESOURCE_COORDINATE {
    UINT X;
    UINT Y;
    UINT Z;
    UINT Subresource;
} D3D12_TILED_RESOURCE_COORDINATE;

typedef struct D3D12_TILE_REGION_SIZE {
    UINT NumTiles;
    BOOL UseBox;
    UINT Width;
    UINT16 Height;
    UINT16 Depth;
} D3D12_TILE_REGION_SIZE;

typedef enum D3D12_TILE_RANGE_FLAGS {
    D3D12_TILE_RANGE_FLAG_NONE = 0,
    D3D12_TILE_RANGE_FLAG_NULL = 1,
    D3D12_TILE_RANGE_FLAG_SKIP = 2,
    D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE = 4
} D3D12_TILE_RANGE_FLAGS;

typedef enum D3D12_TEXTURE_COPY_TYPE {
    D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX = 0,
    D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT = 1
} D3D12_TEXTURE_COPY_TYPE;

typedef struct D3D12_TEXTURE_COPY_LOCATION {
    ID3D12Resource *pResource;
    D3D12_TEXTURE_COPY_TYPE Type;
    union {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT PlacedFootprint;
        UINT SubresourceIndex;
    };
} D3D12_TEXTURE_COPY_LOCATION;

typedef D3D_PRIMITIVE_TOPOLOGY D3D12_PRIMITIVE_TOPOLOGY;

#define D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE (16)

typedef struct D3D12_INDEX_BUFFER_VIEW {
    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
    UINT SizeInBytes;
    DXGI_FORMAT Format;
} D3D12_INDEX_BUFFER_VIEW;

typedef struct D3D12_VIEWPORT {
    FLOAT TopLeftX;
    FLOAT TopLeftY;
    FLOAT Width;
    FLOAT Height;
    FLOAT MinDepth;
    FLOAT MaxDepth;
} D3D12_VIEWPORT;

typedef RECT D3D12_RECT;

typedef struct D3D12_RESOURCE_TRANSITION_BARRIER {
    ID3D12Resource *pResource;
    UINT Subresource;
    D3D12_RESOURCE_STATES StateBefore;
    D3D12_RESOURCE_STATES StateAfter;
} D3D12_RESOURCE_TRANSITION_BARRIER;

typedef struct D3D12_RESOURCE_ALIASING_BARRIER {
    ID3D12Resource *pResourceBefore;
    ID3D12Resource *pResourceAfter;
} D3D12_RESOURCE_ALIASING_BARRIER;

typedef struct D3D12_RESOURCE_UAV_BARRIER {
    ID3D12Resource *pResource;
} D3D12_RESOURCE_UAV_BARRIER;

typedef enum D3D12_RESOURCE_BARRIER_FLAGS {
    D3D12_RESOURCE_BARRIER_FLAG_NONE = 0,
    D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY = 0x1,
    D3D12_RESOURCE_BARRIER_FLAG_END_ONLY = 0x2
} D3D12_RESOURCE_BARRIER_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_RESOURCE_STATES);
typedef enum D3D12_RESOURCE_BARRIER_TYPE {
    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION = 0,
    D3D12_RESOURCE_BARRIER_TYPE_ALIASING = (D3D12_RESOURCE_BARRIER_TYPE_TRANSITION + 1),
    D3D12_RESOURCE_BARRIER_TYPE_UAV = (D3D12_RESOURCE_BARRIER_TYPE_ALIASING + 1)
} D3D12_RESOURCE_BARRIER_TYPE;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_RESOURCE_BARRIER_FLAGS);
typedef struct D3D12_RESOURCE_BARRIER {
    D3D12_RESOURCE_BARRIER_TYPE Type;
    D3D12_RESOURCE_BARRIER_FLAGS Flags;
    union {
        D3D12_RESOURCE_TRANSITION_BARRIER Transition;
        D3D12_RESOURCE_ALIASING_BARRIER Aliasing;
        D3D12_RESOURCE_UAV_BARRIER UAV;
    };
} D3D12_RESOURCE_BARRIER;

typedef struct D3D12_VERTEX_BUFFER_VIEW {
    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
    UINT SizeInBytes;
    UINT StrideInBytes;
} D3D12_VERTEX_BUFFER_VIEW;

typedef struct D3D12_STREAM_OUTPUT_BUFFER_VIEW {
    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
    UINT64 SizeInBytes;
    D3D12_GPU_VIRTUAL_ADDRESS BufferFilledSizeLocation;
} D3D12_STREAM_OUTPUT_BUFFER_VIEW;

typedef struct D3D12_DISCARD_REGION {
    UINT NumRects;
    _In_reads_(NumRects) const D3D12_RECT *pRects;
    UINT FirstSubresource;
    UINT NumSubresources;
} D3D12_DISCARD_REGION;

typedef enum D3D12_QUERY_TYPE {
    D3D12_QUERY_TYPE_OCCLUSION = 0,
    D3D12_QUERY_TYPE_BINARY_OCCLUSION = 1,
    D3D12_QUERY_TYPE_TIMESTAMP = 2,
    D3D12_QUERY_TYPE_PIPELINE_STATISTICS = 3,
    D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 = 4,
    D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1 = 5,
    D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2 = 6,
    D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3 = 7
} D3D12_QUERY_TYPE;

typedef enum D3D12_PREDICATION_OP {
    D3D12_PREDICATION_OP_EQUAL_ZERO = 0,
    D3D12_PREDICATION_OP_NOT_EQUAL_ZERO = 1
} D3D12_PREDICATION_OP;

typedef enum D3D12_ROOT_SIGNATURE_FLAGS {
    D3D12_ROOT_SIGNATURE_FLAG_NONE = 0,
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT = 0x1,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS = 0x2,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS = 0x4,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS = 0x8,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS = 0x10,
    D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS = 0x20,
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT = 0x40
} D3D12_ROOT_SIGNATURE_FLAGS;

typedef enum D3D12_ROOT_PARAMETER_TYPE {
    D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE = 0,
    D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS = (D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE + 1),
    D3D12_ROOT_PARAMETER_TYPE_CBV = (D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS + 1),
    D3D12_ROOT_PARAMETER_TYPE_SRV = (D3D12_ROOT_PARAMETER_TYPE_CBV + 1),
    D3D12_ROOT_PARAMETER_TYPE_UAV = (D3D12_ROOT_PARAMETER_TYPE_SRV + 1)
} D3D12_ROOT_PARAMETER_TYPE;

typedef enum D3D12_DESCRIPTOR_RANGE_TYPE {
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV = 0,
    D3D12_DESCRIPTOR_RANGE_TYPE_UAV = (D3D12_DESCRIPTOR_RANGE_TYPE_SRV + 1),
    D3D12_DESCRIPTOR_RANGE_TYPE_CBV = (D3D12_DESCRIPTOR_RANGE_TYPE_UAV + 1),
    D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER = (D3D12_DESCRIPTOR_RANGE_TYPE_CBV + 1)
} D3D12_DESCRIPTOR_RANGE_TYPE;

typedef struct D3D12_DESCRIPTOR_RANGE {
    D3D12_DESCRIPTOR_RANGE_TYPE RangeType;
    UINT NumDescriptors;
    UINT BaseShaderRegister;
    UINT RegisterSpace;
    UINT OffsetInDescriptorsFromTableStart;
} D3D12_DESCRIPTOR_RANGE;

typedef struct D3D12_ROOT_DESCRIPTOR_TABLE {
    UINT NumDescriptorRanges;
    _Field_size_full_(NumDescriptorRanges) const D3D12_DESCRIPTOR_RANGE *pDescriptorRanges;
} D3D12_ROOT_DESCRIPTOR_TABLE;

typedef struct D3D12_ROOT_CONSTANTS {
    UINT ShaderRegister;
    UINT RegisterSpace;
    UINT Num32BitValues;
} D3D12_ROOT_CONSTANTS;

typedef struct D3D12_ROOT_DESCRIPTOR {
    UINT ShaderRegister;
    UINT RegisterSpace;
} D3D12_ROOT_DESCRIPTOR;

typedef enum D3D12_SHADER_VISIBILITY {
    D3D12_SHADER_VISIBILITY_ALL = 0,
    D3D12_SHADER_VISIBILITY_VERTEX = 1,
    D3D12_SHADER_VISIBILITY_HULL = 2,
    D3D12_SHADER_VISIBILITY_DOMAIN = 3,
    D3D12_SHADER_VISIBILITY_GEOMETRY = 4,
    D3D12_SHADER_VISIBILITY_PIXEL = 5
} D3D12_SHADER_VISIBILITY;

typedef struct D3D12_ROOT_PARAMETER {
    D3D12_ROOT_PARAMETER_TYPE ParameterType;
    union {
        D3D12_ROOT_DESCRIPTOR_TABLE DescriptorTable;
        D3D12_ROOT_CONSTANTS Constants;
        D3D12_ROOT_DESCRIPTOR Descriptor;
    };
    D3D12_SHADER_VISIBILITY ShaderVisibility;
} D3D12_ROOT_PARAMETER;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_ROOT_SIGNATURE_FLAGS);
typedef enum D3D12_STATIC_BORDER_COLOR {
    D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK = 0,
    D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK = (D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK + 1),
    D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE = (D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK + 1)
} D3D12_STATIC_BORDER_COLOR;

typedef struct D3D12_STATIC_SAMPLER_DESC {
    D3D12_FILTER Filter;
    D3D12_TEXTURE_ADDRESS_MODE AddressU;
    D3D12_TEXTURE_ADDRESS_MODE AddressV;
    D3D12_TEXTURE_ADDRESS_MODE AddressW;
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    D3D12_COMPARISON_FUNC ComparisonFunc;
    D3D12_STATIC_BORDER_COLOR BorderColor;
    FLOAT MinLOD;
    FLOAT MaxLOD;
    UINT ShaderRegister;
    UINT RegisterSpace;
    D3D12_SHADER_VISIBILITY ShaderVisibility;
} D3D12_STATIC_SAMPLER_DESC;

typedef struct D3D12_ROOT_SIGNATURE_DESC {
    UINT NumParameters;
    _Field_size_full_(NumParameters) const D3D12_ROOT_PARAMETER *pParameters;
    UINT NumStaticSamplers;
    _Field_size_full_(NumStaticSamplers) const D3D12_STATIC_SAMPLER_DESC *pStaticSamplers;
    D3D12_ROOT_SIGNATURE_FLAGS Flags;
} D3D12_ROOT_SIGNATURE_DESC;

typedef enum D3D12_DESCRIPTOR_RANGE_FLAGS {
    D3D12_DESCRIPTOR_RANGE_FLAG_NONE = 0,
    D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE = 0x1,
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE = 0x2,
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE = 0x4,
    D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC = 0x8
} D3D12_DESCRIPTOR_RANGE_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_DESCRIPTOR_RANGE_FLAGS);
typedef struct D3D12_DESCRIPTOR_RANGE1 {
    D3D12_DESCRIPTOR_RANGE_TYPE RangeType;
    UINT NumDescriptors;
    UINT BaseShaderRegister;
    UINT RegisterSpace;
    D3D12_DESCRIPTOR_RANGE_FLAGS Flags;
    UINT OffsetInDescriptorsFromTableStart;
} D3D12_DESCRIPTOR_RANGE1;

typedef struct D3D12_ROOT_DESCRIPTOR_TABLE1 {
    UINT NumDescriptorRanges;
    _Field_size_full_(NumDescriptorRanges) const D3D12_DESCRIPTOR_RANGE1 *pDescriptorRanges;
} D3D12_ROOT_DESCRIPTOR_TABLE1;

typedef enum D3D12_ROOT_DESCRIPTOR_FLAGS {
    D3D12_ROOT_DESCRIPTOR_FLAG_NONE = 0,
    D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE = 0x2,
    D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE = 0x4,
    D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC = 0x8
} D3D12_ROOT_DESCRIPTOR_FLAGS;

DEFINE_ENUM_FLAG_OPERATORS(D3D12_ROOT_DESCRIPTOR_FLAGS);
typedef struct D3D12_ROOT_DESCRIPTOR1 {
    UINT ShaderRegister;
    UINT RegisterSpace;
    D3D12_ROOT_DESCRIPTOR_FLAGS Flags;
} D3D12_ROOT_DESCRIPTOR1;

typedef struct D3D12_ROOT_PARAMETER1 {
    D3D12_ROOT_PARAMETER_TYPE ParameterType;
    union {
        D3D12_ROOT_DESCRIPTOR_TABLE1 DescriptorTable;
        D3D12_ROOT_CONSTANTS Constants;
        D3D12_ROOT_DESCRIPTOR1 Descriptor;
    };
    D3D12_SHADER_VISIBILITY ShaderVisibility;
} D3D12_ROOT_PARAMETER1;

typedef struct D3D12_ROOT_SIGNATURE_DESC1 {
    UINT NumParameters;
    _Field_size_full_(NumParameters) const D3D12_ROOT_PARAMETER1 *pParameters;
    UINT NumStaticSamplers;
    _Field_size_full_(NumStaticSamplers) const D3D12_STATIC_SAMPLER_DESC *pStaticSamplers;
    D3D12_ROOT_SIGNATURE_FLAGS Flags;
} D3D12_ROOT_SIGNATURE_DESC1;

typedef enum D3D_ROOT_SIGNATURE_VERSION {
    D3D_ROOT_SIGNATURE_VERSION_1 = 0x1,
    D3D_ROOT_SIGNATURE_VERSION_1_0 = 0x1,
    D3D_ROOT_SIGNATURE_VERSION_1_1 = 0x2
} D3D_ROOT_SIGNATURE_VERSION;

typedef struct D3D12_VERSIONED_ROOT_SIGNATURE_DESC {
    D3D_ROOT_SIGNATURE_VERSION Version;
    union {
        D3D12_ROOT_SIGNATURE_DESC Desc_1_0;
        D3D12_ROOT_SIGNATURE_DESC1 Desc_1_1;
    };
} D3D12_VERSIONED_ROOT_SIGNATURE_DESC;

#define D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND (0xffffffff)

#ifndef __ID3D12Resource_INTERFACE_DEFINED__
#define __ID3D12Resource_INTERFACE_DEFINED__

/* interface ID3D12Resource */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12Resource;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("696442be-a72e-4059-bc79-5b5c98040fad")
ID3D12Resource : public ID3D12Pageable {
public:
    virtual HRESULT STDMETHODCALLTYPE Map(
        UINT Subresource,
        _In_opt_ const D3D12_RANGE *pReadRange,
        _Outptr_opt_result_bytebuffer_(_Inexpressible_("Dependent on resource")) void **ppData) = 0;

    virtual void STDMETHODCALLTYPE Unmap(
        UINT Subresource,
        _In_opt_ const D3D12_RANGE *pWrittenRange) = 0;

    virtual D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc(void) = 0;

    virtual D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE WriteToSubresource(
        UINT DstSubresource,
        _In_opt_ const D3D12_BOX *pDstBox,
        _In_ const void *pSrcData,
        UINT SrcRowPitch,
        UINT SrcDepthPitch) = 0;

    virtual HRESULT STDMETHODCALLTYPE ReadFromSubresource(
        _Out_ void *pDstData,
        UINT DstRowPitch,
        UINT DstDepthPitch,
        UINT SrcSubresource,
        _In_opt_ const D3D12_BOX *pSrcBox) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetHeapProperties(
        _Out_opt_ D3D12_HEAP_PROPERTIES * pHeapProperties,
        _Out_opt_ D3D12_HEAP_FLAGS * pHeapFlags) = 0;
};

#else /* C style interface */

typedef struct ID3D12ResourceVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12Resource *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12Resource *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12Resource *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12Resource *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12Resource *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12Resource *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12Resource *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12Resource *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    HRESULT(STDMETHODCALLTYPE *Map)
    (
        ID3D12Resource *This,
        UINT Subresource,
        _In_opt_ const D3D12_RANGE *pReadRange,
        _Outptr_opt_result_bytebuffer_(_Inexpressible_("Dependent on resource")) void **ppData);

    void(STDMETHODCALLTYPE *Unmap)(
        ID3D12Resource *This,
        UINT Subresource,
        _In_opt_ const D3D12_RANGE *pWrittenRange);

    D3D12_RESOURCE_DESC(STDMETHODCALLTYPE *GetDesc)
    (
        ID3D12Resource *This);

    D3D12_GPU_VIRTUAL_ADDRESS(STDMETHODCALLTYPE *GetGPUVirtualAddress)
    (
        ID3D12Resource *This);

    HRESULT(STDMETHODCALLTYPE *WriteToSubresource)
    (
        ID3D12Resource *This,
        UINT DstSubresource,
        _In_opt_ const D3D12_BOX *pDstBox,
        _In_ const void *pSrcData,
        UINT SrcRowPitch,
        UINT SrcDepthPitch);

    HRESULT(STDMETHODCALLTYPE *ReadFromSubresource)
    (
        ID3D12Resource *This,
        _Out_ void *pDstData,
        UINT DstRowPitch,
        UINT DstDepthPitch,
        UINT SrcSubresource,
        _In_opt_ const D3D12_BOX *pSrcBox);

    HRESULT(STDMETHODCALLTYPE *GetHeapProperties)
    (
        ID3D12Resource *This,
        _Out_opt_ D3D12_HEAP_PROPERTIES *pHeapProperties,
        _Out_opt_ D3D12_HEAP_FLAGS *pHeapFlags);

    END_INTERFACE
} ID3D12ResourceVtbl;

interface ID3D12Resource {
    CONST_VTBL struct ID3D12ResourceVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12Resource_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12Resource_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12Resource_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12Resource_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12Resource_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12Resource_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12Resource_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12Resource_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#define ID3D12Resource_Map(This, Subresource, pReadRange, ppData) \
    ((This)->lpVtbl->Map(This, Subresource, pReadRange, ppData))

#define ID3D12Resource_Unmap(This, Subresource, pWrittenRange) \
    ((This)->lpVtbl->Unmap(This, Subresource, pWrittenRange))

#define ID3D12Resource_GetDesc(This) \
    ((This)->lpVtbl->GetDesc(This))

#define ID3D12Resource_GetGPUVirtualAddress(This) \
    ((This)->lpVtbl->GetGPUVirtualAddress(This))

#define ID3D12Resource_WriteToSubresource(This, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch) \
    ((This)->lpVtbl->WriteToSubresource(This, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch))

#define ID3D12Resource_ReadFromSubresource(This, pDstData, DstRowPitch, DstDepthPitch, SrcSubresource, pSrcBox) \
    ((This)->lpVtbl->ReadFromSubresource(This, pDstData, DstRowPitch, DstDepthPitch, SrcSubresource, pSrcBox))

#define ID3D12Resource_GetHeapProperties(This, pHeapProperties, pHeapFlags) \
    ((This)->lpVtbl->GetHeapProperties(This, pHeapProperties, pHeapFlags))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12Resource_INTERFACE_DEFINED__ */

#ifndef __ID3D12CommandAllocator_INTERFACE_DEFINED__
#define __ID3D12CommandAllocator_INTERFACE_DEFINED__

/* interface ID3D12CommandAllocator */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12CommandAllocator;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("6102dee4-af59-4b09-b999-b44d73f09b24")
ID3D12CommandAllocator : public ID3D12Pageable {
public:
    virtual HRESULT STDMETHODCALLTYPE Reset(void) = 0;
};

#else /* C style interface */

typedef struct ID3D12CommandAllocatorVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12CommandAllocator *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12CommandAllocator *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12CommandAllocator *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12CommandAllocator *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12CommandAllocator *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12CommandAllocator *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12CommandAllocator *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12CommandAllocator *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    HRESULT(STDMETHODCALLTYPE *Reset)
    (
        ID3D12CommandAllocator *This);

    END_INTERFACE
} ID3D12CommandAllocatorVtbl;

interface ID3D12CommandAllocator {
    CONST_VTBL struct ID3D12CommandAllocatorVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12CommandAllocator_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12CommandAllocator_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12CommandAllocator_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12CommandAllocator_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12CommandAllocator_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12CommandAllocator_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12CommandAllocator_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12CommandAllocator_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#define ID3D12CommandAllocator_Reset(This) \
    ((This)->lpVtbl->Reset(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12CommandAllocator_INTERFACE_DEFINED__ */

#ifndef __ID3D12CommandList_INTERFACE_DEFINED__
#define __ID3D12CommandList_INTERFACE_DEFINED__

/* interface ID3D12CommandList */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12CommandList;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("7116d91c-e7e4-47ce-b8c6-ec8168f437e5")
ID3D12CommandList : public ID3D12DeviceChild {
public:
    virtual D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType(void) = 0;
};

#else /* C style interface */

typedef struct ID3D12CommandListVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12CommandList *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12CommandList *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12CommandList *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12CommandList *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12CommandList *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12CommandList *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12CommandList *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12CommandList *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    D3D12_COMMAND_LIST_TYPE(STDMETHODCALLTYPE *GetType)
    (
        ID3D12CommandList *This);

    END_INTERFACE
} ID3D12CommandListVtbl;

interface ID3D12CommandList {
    CONST_VTBL struct ID3D12CommandListVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12CommandList_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12CommandList_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12CommandList_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12CommandList_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12CommandList_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12CommandList_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12CommandList_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12CommandList_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#define ID3D12CommandList_GetType(This) \
    ((This)->lpVtbl->GetType(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12CommandList_INTERFACE_DEFINED__ */

#ifndef __ID3D12GraphicsCommandList_INTERFACE_DEFINED__
#define __ID3D12GraphicsCommandList_INTERFACE_DEFINED__

/* interface ID3D12GraphicsCommandList */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12GraphicsCommandList;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("5b160d0f-ac1b-4185-8ba8-b3ae42a5a455")
ID3D12GraphicsCommandList : public ID3D12CommandList {
public:
    virtual HRESULT STDMETHODCALLTYPE Close(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE Reset(
        _In_ ID3D12CommandAllocator * pAllocator,
        _In_opt_ ID3D12PipelineState * pInitialState) = 0;

    virtual void STDMETHODCALLTYPE ClearState(
        _In_opt_ ID3D12PipelineState * pPipelineState) = 0;

    virtual void STDMETHODCALLTYPE DrawInstanced(
        _In_ UINT VertexCountPerInstance,
        _In_ UINT InstanceCount,
        _In_ UINT StartVertexLocation,
        _In_ UINT StartInstanceLocation) = 0;

    virtual void STDMETHODCALLTYPE DrawIndexedInstanced(
        _In_ UINT IndexCountPerInstance,
        _In_ UINT InstanceCount,
        _In_ UINT StartIndexLocation,
        _In_ INT BaseVertexLocation,
        _In_ UINT StartInstanceLocation) = 0;

    virtual void STDMETHODCALLTYPE Dispatch(
        _In_ UINT ThreadGroupCountX,
        _In_ UINT ThreadGroupCountY,
        _In_ UINT ThreadGroupCountZ) = 0;

    virtual void STDMETHODCALLTYPE CopyBufferRegion(
        _In_ ID3D12Resource * pDstBuffer,
        UINT64 DstOffset,
        _In_ ID3D12Resource * pSrcBuffer,
        UINT64 SrcOffset,
        UINT64 NumBytes) = 0;

    virtual void STDMETHODCALLTYPE CopyTextureRegion(
        _In_ const D3D12_TEXTURE_COPY_LOCATION *pDst,
        UINT DstX,
        UINT DstY,
        UINT DstZ,
        _In_ const D3D12_TEXTURE_COPY_LOCATION *pSrc,
        _In_opt_ const D3D12_BOX *pSrcBox) = 0;

    virtual void STDMETHODCALLTYPE CopyResource(
        _In_ ID3D12Resource * pDstResource,
        _In_ ID3D12Resource * pSrcResource) = 0;

    virtual void STDMETHODCALLTYPE CopyTiles(
        _In_ ID3D12Resource * pTiledResource,
        _In_ const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
        _In_ const D3D12_TILE_REGION_SIZE *pTileRegionSize,
        _In_ ID3D12Resource *pBuffer,
        UINT64 BufferStartOffsetInBytes,
        D3D12_TILE_COPY_FLAGS Flags) = 0;

    virtual void STDMETHODCALLTYPE ResolveSubresource(
        _In_ ID3D12Resource * pDstResource,
        _In_ UINT DstSubresource,
        _In_ ID3D12Resource * pSrcResource,
        _In_ UINT SrcSubresource,
        _In_ DXGI_FORMAT Format) = 0;

    virtual void STDMETHODCALLTYPE IASetPrimitiveTopology(
        _In_ D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) = 0;

    virtual void STDMETHODCALLTYPE RSSetViewports(
        _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumViewports,
        _In_reads_(NumViewports) const D3D12_VIEWPORT *pViewports) = 0;

    virtual void STDMETHODCALLTYPE RSSetScissorRects(
        _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects) = 0;

    virtual void STDMETHODCALLTYPE OMSetBlendFactor(
        _In_opt_ const FLOAT BlendFactor[4]) = 0;

    virtual void STDMETHODCALLTYPE OMSetStencilRef(
        _In_ UINT StencilRef) = 0;

    virtual void STDMETHODCALLTYPE SetPipelineState(
        _In_ ID3D12PipelineState * pPipelineState) = 0;

    virtual void STDMETHODCALLTYPE ResourceBarrier(
        _In_ UINT NumBarriers,
        _In_reads_(NumBarriers) const D3D12_RESOURCE_BARRIER *pBarriers) = 0;

    virtual void STDMETHODCALLTYPE ExecuteBundle(
        _In_ ID3D12GraphicsCommandList * pCommandList) = 0;

    virtual void STDMETHODCALLTYPE SetDescriptorHeaps(
        _In_ UINT NumDescriptorHeaps,
        _In_reads_(NumDescriptorHeaps) ID3D12DescriptorHeap *const *ppDescriptorHeaps) = 0;

    virtual void STDMETHODCALLTYPE SetComputeRootSignature(
        _In_opt_ ID3D12RootSignature * pRootSignature) = 0;

    virtual void STDMETHODCALLTYPE SetGraphicsRootSignature(
        _In_opt_ ID3D12RootSignature * pRootSignature) = 0;

    virtual void STDMETHODCALLTYPE SetComputeRootDescriptorTable(
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) = 0;

    virtual void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) = 0;

    virtual void STDMETHODCALLTYPE SetComputeRoot32BitConstant(
        _In_ UINT RootParameterIndex,
        _In_ UINT SrcData,
        _In_ UINT DestOffsetIn32BitValues) = 0;

    virtual void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(
        _In_ UINT RootParameterIndex,
        _In_ UINT SrcData,
        _In_ UINT DestOffsetIn32BitValues) = 0;

    virtual void STDMETHODCALLTYPE SetComputeRoot32BitConstants(
        _In_ UINT RootParameterIndex,
        _In_ UINT Num32BitValuesToSet,
        _In_reads_(Num32BitValuesToSet * sizeof(UINT)) const void *pSrcData,
        _In_ UINT DestOffsetIn32BitValues) = 0;

    virtual void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(
        _In_ UINT RootParameterIndex,
        _In_ UINT Num32BitValuesToSet,
        _In_reads_(Num32BitValuesToSet * sizeof(UINT)) const void *pSrcData,
        _In_ UINT DestOffsetIn32BitValues) = 0;

    virtual void STDMETHODCALLTYPE SetComputeRootConstantBufferView(
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) = 0;

    virtual void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) = 0;

    virtual void STDMETHODCALLTYPE SetComputeRootShaderResourceView(
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) = 0;

    virtual void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) = 0;

    virtual void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) = 0;

    virtual void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) = 0;

    virtual void STDMETHODCALLTYPE IASetIndexBuffer(
        _In_opt_ const D3D12_INDEX_BUFFER_VIEW *pView) = 0;

    virtual void STDMETHODCALLTYPE IASetVertexBuffers(
        _In_ UINT StartSlot,
        _In_ UINT NumViews,
        _In_reads_opt_(NumViews) const D3D12_VERTEX_BUFFER_VIEW *pViews) = 0;

    virtual void STDMETHODCALLTYPE SOSetTargets(
        _In_ UINT StartSlot,
        _In_ UINT NumViews,
        _In_reads_opt_(NumViews) const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews) = 0;

    virtual void STDMETHODCALLTYPE OMSetRenderTargets(
        _In_ UINT NumRenderTargetDescriptors,
        _In_opt_ const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
        _In_ BOOL RTsSingleHandleToDescriptorRange,
        _In_opt_ const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor) = 0;

    virtual void STDMETHODCALLTYPE ClearDepthStencilView(
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView,
        _In_ D3D12_CLEAR_FLAGS ClearFlags,
        _In_ FLOAT Depth,
        _In_ UINT8 Stencil,
        _In_ UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects) = 0;

    virtual void STDMETHODCALLTYPE ClearRenderTargetView(
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
        _In_ const FLOAT ColorRGBA[4],
        _In_ UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects) = 0;

    virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
        _In_ ID3D12Resource * pResource,
        _In_ const UINT Values[4],
        _In_ UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects) = 0;

    virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
        _In_ ID3D12Resource * pResource,
        _In_ const FLOAT Values[4],
        _In_ UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects) = 0;

    virtual void STDMETHODCALLTYPE DiscardResource(
        _In_ ID3D12Resource * pResource,
        _In_opt_ const D3D12_DISCARD_REGION *pRegion) = 0;

    virtual void STDMETHODCALLTYPE BeginQuery(
        _In_ ID3D12QueryHeap * pQueryHeap,
        _In_ D3D12_QUERY_TYPE Type,
        _In_ UINT Index) = 0;

    virtual void STDMETHODCALLTYPE EndQuery(
        _In_ ID3D12QueryHeap * pQueryHeap,
        _In_ D3D12_QUERY_TYPE Type,
        _In_ UINT Index) = 0;

    virtual void STDMETHODCALLTYPE ResolveQueryData(
        _In_ ID3D12QueryHeap * pQueryHeap,
        _In_ D3D12_QUERY_TYPE Type,
        _In_ UINT StartIndex,
        _In_ UINT NumQueries,
        _In_ ID3D12Resource * pDestinationBuffer,
        _In_ UINT64 AlignedDestinationBufferOffset) = 0;

    virtual void STDMETHODCALLTYPE SetPredication(
        _In_opt_ ID3D12Resource * pBuffer,
        _In_ UINT64 AlignedBufferOffset,
        _In_ D3D12_PREDICATION_OP Operation) = 0;

    virtual void STDMETHODCALLTYPE SetMarker(
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void *pData,
        UINT Size) = 0;

    virtual void STDMETHODCALLTYPE BeginEvent(
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void *pData,
        UINT Size) = 0;

    virtual void STDMETHODCALLTYPE EndEvent(void) = 0;

    virtual void STDMETHODCALLTYPE ExecuteIndirect(
        _In_ ID3D12CommandSignature * pCommandSignature,
        _In_ UINT MaxCommandCount,
        _In_ ID3D12Resource * pArgumentBuffer,
        _In_ UINT64 ArgumentBufferOffset,
        _In_opt_ ID3D12Resource * pCountBuffer,
        _In_ UINT64 CountBufferOffset) = 0;
};

#else /* C style interface */

typedef struct ID3D12GraphicsCommandListVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12GraphicsCommandList *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12GraphicsCommandList *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12GraphicsCommandList *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12GraphicsCommandList *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12GraphicsCommandList *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12GraphicsCommandList *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12GraphicsCommandList *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12GraphicsCommandList *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    D3D12_COMMAND_LIST_TYPE(STDMETHODCALLTYPE *GetType)
    (
        ID3D12GraphicsCommandList *This);

    HRESULT(STDMETHODCALLTYPE *Close)
    (
        ID3D12GraphicsCommandList *This);

    HRESULT(STDMETHODCALLTYPE *Reset)
    (
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12CommandAllocator *pAllocator,
        _In_opt_ ID3D12PipelineState *pInitialState);

    void(STDMETHODCALLTYPE *ClearState)(
        ID3D12GraphicsCommandList *This,
        _In_opt_ ID3D12PipelineState *pPipelineState);

    void(STDMETHODCALLTYPE *DrawInstanced)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT VertexCountPerInstance,
        _In_ UINT InstanceCount,
        _In_ UINT StartVertexLocation,
        _In_ UINT StartInstanceLocation);

    void(STDMETHODCALLTYPE *DrawIndexedInstanced)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT IndexCountPerInstance,
        _In_ UINT InstanceCount,
        _In_ UINT StartIndexLocation,
        _In_ INT BaseVertexLocation,
        _In_ UINT StartInstanceLocation);

    void(STDMETHODCALLTYPE *Dispatch)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT ThreadGroupCountX,
        _In_ UINT ThreadGroupCountY,
        _In_ UINT ThreadGroupCountZ);

    void(STDMETHODCALLTYPE *CopyBufferRegion)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12Resource *pDstBuffer,
        UINT64 DstOffset,
        _In_ ID3D12Resource *pSrcBuffer,
        UINT64 SrcOffset,
        UINT64 NumBytes);

    void(STDMETHODCALLTYPE *CopyTextureRegion)(
        ID3D12GraphicsCommandList *This,
        _In_ const D3D12_TEXTURE_COPY_LOCATION *pDst,
        UINT DstX,
        UINT DstY,
        UINT DstZ,
        _In_ const D3D12_TEXTURE_COPY_LOCATION *pSrc,
        _In_opt_ const D3D12_BOX *pSrcBox);

    void(STDMETHODCALLTYPE *CopyResource)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12Resource *pDstResource,
        _In_ ID3D12Resource *pSrcResource);

    void(STDMETHODCALLTYPE *CopyTiles)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12Resource *pTiledResource,
        _In_ const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
        _In_ const D3D12_TILE_REGION_SIZE *pTileRegionSize,
        _In_ ID3D12Resource *pBuffer,
        UINT64 BufferStartOffsetInBytes,
        D3D12_TILE_COPY_FLAGS Flags);

    void(STDMETHODCALLTYPE *ResolveSubresource)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12Resource *pDstResource,
        _In_ UINT DstSubresource,
        _In_ ID3D12Resource *pSrcResource,
        _In_ UINT SrcSubresource,
        _In_ DXGI_FORMAT Format);

    void(STDMETHODCALLTYPE *IASetPrimitiveTopology)(
        ID3D12GraphicsCommandList *This,
        _In_ D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology);

    void(STDMETHODCALLTYPE *RSSetViewports)(
        ID3D12GraphicsCommandList *This,
        _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumViewports,
        _In_reads_(NumViewports) const D3D12_VIEWPORT *pViewports);

    void(STDMETHODCALLTYPE *RSSetScissorRects)(
        ID3D12GraphicsCommandList *This,
        _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects);

    void(STDMETHODCALLTYPE *OMSetBlendFactor)(
        ID3D12GraphicsCommandList *This,
        _In_opt_ const FLOAT BlendFactor[4]);

    void(STDMETHODCALLTYPE *OMSetStencilRef)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT StencilRef);

    void(STDMETHODCALLTYPE *SetPipelineState)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12PipelineState *pPipelineState);

    void(STDMETHODCALLTYPE *ResourceBarrier)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT NumBarriers,
        _In_reads_(NumBarriers) const D3D12_RESOURCE_BARRIER *pBarriers);

    void(STDMETHODCALLTYPE *ExecuteBundle)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12GraphicsCommandList *pCommandList);

    void(STDMETHODCALLTYPE *SetDescriptorHeaps)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT NumDescriptorHeaps,
        _In_reads_(NumDescriptorHeaps) ID3D12DescriptorHeap *const *ppDescriptorHeaps);

    void(STDMETHODCALLTYPE *SetComputeRootSignature)(
        ID3D12GraphicsCommandList *This,
        _In_opt_ ID3D12RootSignature *pRootSignature);

    void(STDMETHODCALLTYPE *SetGraphicsRootSignature)(
        ID3D12GraphicsCommandList *This,
        _In_opt_ ID3D12RootSignature *pRootSignature);

    void(STDMETHODCALLTYPE *SetComputeRootDescriptorTable)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);

    void(STDMETHODCALLTYPE *SetGraphicsRootDescriptorTable)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);

    void(STDMETHODCALLTYPE *SetComputeRoot32BitConstant)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ UINT SrcData,
        _In_ UINT DestOffsetIn32BitValues);

    void(STDMETHODCALLTYPE *SetGraphicsRoot32BitConstant)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ UINT SrcData,
        _In_ UINT DestOffsetIn32BitValues);

    void(STDMETHODCALLTYPE *SetComputeRoot32BitConstants)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ UINT Num32BitValuesToSet,
        _In_reads_(Num32BitValuesToSet * sizeof(UINT)) const void *pSrcData,
        _In_ UINT DestOffsetIn32BitValues);

    void(STDMETHODCALLTYPE *SetGraphicsRoot32BitConstants)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ UINT Num32BitValuesToSet,
        _In_reads_(Num32BitValuesToSet * sizeof(UINT)) const void *pSrcData,
        _In_ UINT DestOffsetIn32BitValues);

    void(STDMETHODCALLTYPE *SetComputeRootConstantBufferView)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);

    void(STDMETHODCALLTYPE *SetGraphicsRootConstantBufferView)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);

    void(STDMETHODCALLTYPE *SetComputeRootShaderResourceView)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);

    void(STDMETHODCALLTYPE *SetGraphicsRootShaderResourceView)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);

    void(STDMETHODCALLTYPE *SetComputeRootUnorderedAccessView)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);

    void(STDMETHODCALLTYPE *SetGraphicsRootUnorderedAccessView)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT RootParameterIndex,
        _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);

    void(STDMETHODCALLTYPE *IASetIndexBuffer)(
        ID3D12GraphicsCommandList *This,
        _In_opt_ const D3D12_INDEX_BUFFER_VIEW *pView);

    void(STDMETHODCALLTYPE *IASetVertexBuffers)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT StartSlot,
        _In_ UINT NumViews,
        _In_reads_opt_(NumViews) const D3D12_VERTEX_BUFFER_VIEW *pViews);

    void(STDMETHODCALLTYPE *SOSetTargets)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT StartSlot,
        _In_ UINT NumViews,
        _In_reads_opt_(NumViews) const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews);

    void(STDMETHODCALLTYPE *OMSetRenderTargets)(
        ID3D12GraphicsCommandList *This,
        _In_ UINT NumRenderTargetDescriptors,
        _In_opt_ const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
        _In_ BOOL RTsSingleHandleToDescriptorRange,
        _In_opt_ const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor);

    void(STDMETHODCALLTYPE *ClearDepthStencilView)(
        ID3D12GraphicsCommandList *This,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView,
        _In_ D3D12_CLEAR_FLAGS ClearFlags,
        _In_ FLOAT Depth,
        _In_ UINT8 Stencil,
        _In_ UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects);

    void(STDMETHODCALLTYPE *ClearRenderTargetView)(
        ID3D12GraphicsCommandList *This,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
        _In_ const FLOAT ColorRGBA[4],
        _In_ UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects);

    void(STDMETHODCALLTYPE *ClearUnorderedAccessViewUint)(
        ID3D12GraphicsCommandList *This,
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
        _In_ ID3D12Resource *pResource,
        _In_ const UINT Values[4],
        _In_ UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects);

    void(STDMETHODCALLTYPE *ClearUnorderedAccessViewFloat)(
        ID3D12GraphicsCommandList *This,
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
        _In_ ID3D12Resource *pResource,
        _In_ const FLOAT Values[4],
        _In_ UINT NumRects,
        _In_reads_(NumRects) const D3D12_RECT *pRects);

    void(STDMETHODCALLTYPE *DiscardResource)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12Resource *pResource,
        _In_opt_ const D3D12_DISCARD_REGION *pRegion);

    void(STDMETHODCALLTYPE *BeginQuery)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12QueryHeap *pQueryHeap,
        _In_ D3D12_QUERY_TYPE Type,
        _In_ UINT Index);

    void(STDMETHODCALLTYPE *EndQuery)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12QueryHeap *pQueryHeap,
        _In_ D3D12_QUERY_TYPE Type,
        _In_ UINT Index);

    void(STDMETHODCALLTYPE *ResolveQueryData)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12QueryHeap *pQueryHeap,
        _In_ D3D12_QUERY_TYPE Type,
        _In_ UINT StartIndex,
        _In_ UINT NumQueries,
        _In_ ID3D12Resource *pDestinationBuffer,
        _In_ UINT64 AlignedDestinationBufferOffset);

    void(STDMETHODCALLTYPE *SetPredication)(
        ID3D12GraphicsCommandList *This,
        _In_opt_ ID3D12Resource *pBuffer,
        _In_ UINT64 AlignedBufferOffset,
        _In_ D3D12_PREDICATION_OP Operation);

    void(STDMETHODCALLTYPE *SetMarker)(
        ID3D12GraphicsCommandList *This,
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void *pData,
        UINT Size);

    void(STDMETHODCALLTYPE *BeginEvent)(
        ID3D12GraphicsCommandList *This,
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void *pData,
        UINT Size);

    void(STDMETHODCALLTYPE *EndEvent)(
        ID3D12GraphicsCommandList *This);

    void(STDMETHODCALLTYPE *ExecuteIndirect)(
        ID3D12GraphicsCommandList *This,
        _In_ ID3D12CommandSignature *pCommandSignature,
        _In_ UINT MaxCommandCount,
        _In_ ID3D12Resource *pArgumentBuffer,
        _In_ UINT64 ArgumentBufferOffset,
        _In_opt_ ID3D12Resource *pCountBuffer,
        _In_ UINT64 CountBufferOffset);

    END_INTERFACE
} ID3D12GraphicsCommandListVtbl;

interface ID3D12GraphicsCommandList {
    CONST_VTBL struct ID3D12GraphicsCommandListVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12GraphicsCommandList_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12GraphicsCommandList_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12GraphicsCommandList_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12GraphicsCommandList_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12GraphicsCommandList_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12GraphicsCommandList_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12GraphicsCommandList_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12GraphicsCommandList_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#define ID3D12GraphicsCommandList_GetType(This) \
    ((This)->lpVtbl->GetType(This))

#define ID3D12GraphicsCommandList_Close(This) \
    ((This)->lpVtbl->Close(This))

#define ID3D12GraphicsCommandList_Reset(This, pAllocator, pInitialState) \
    ((This)->lpVtbl->Reset(This, pAllocator, pInitialState))

#define ID3D12GraphicsCommandList_ClearState(This, pPipelineState) \
    ((This)->lpVtbl->ClearState(This, pPipelineState))

#define ID3D12GraphicsCommandList_DrawInstanced(This, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation) \
    ((This)->lpVtbl->DrawInstanced(This, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation))

#define ID3D12GraphicsCommandList_DrawIndexedInstanced(This, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation) \
    ((This)->lpVtbl->DrawIndexedInstanced(This, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation))

#define ID3D12GraphicsCommandList_Dispatch(This, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ) \
    ((This)->lpVtbl->Dispatch(This, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ))

#define ID3D12GraphicsCommandList_CopyBufferRegion(This, pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes) \
    ((This)->lpVtbl->CopyBufferRegion(This, pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes))

#define ID3D12GraphicsCommandList_CopyTextureRegion(This, pDst, DstX, DstY, DstZ, pSrc, pSrcBox) \
    ((This)->lpVtbl->CopyTextureRegion(This, pDst, DstX, DstY, DstZ, pSrc, pSrcBox))

#define ID3D12GraphicsCommandList_CopyResource(This, pDstResource, pSrcResource) \
    ((This)->lpVtbl->CopyResource(This, pDstResource, pSrcResource))

#define ID3D12GraphicsCommandList_CopyTiles(This, pTiledResource, pTileRegionStartCoordinate, pTileRegionSize, pBuffer, BufferStartOffsetInBytes, Flags) \
    ((This)->lpVtbl->CopyTiles(This, pTiledResource, pTileRegionStartCoordinate, pTileRegionSize, pBuffer, BufferStartOffsetInBytes, Flags))

#define ID3D12GraphicsCommandList_ResolveSubresource(This, pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format) \
    ((This)->lpVtbl->ResolveSubresource(This, pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format))

#define ID3D12GraphicsCommandList_IASetPrimitiveTopology(This, PrimitiveTopology) \
    ((This)->lpVtbl->IASetPrimitiveTopology(This, PrimitiveTopology))

#define ID3D12GraphicsCommandList_RSSetViewports(This, NumViewports, pViewports) \
    ((This)->lpVtbl->RSSetViewports(This, NumViewports, pViewports))

#define ID3D12GraphicsCommandList_RSSetScissorRects(This, NumRects, pRects) \
    ((This)->lpVtbl->RSSetScissorRects(This, NumRects, pRects))

#define ID3D12GraphicsCommandList_OMSetBlendFactor(This, BlendFactor) \
    ((This)->lpVtbl->OMSetBlendFactor(This, BlendFactor))

#define ID3D12GraphicsCommandList_OMSetStencilRef(This, StencilRef) \
    ((This)->lpVtbl->OMSetStencilRef(This, StencilRef))

#define ID3D12GraphicsCommandList_SetPipelineState(This, pPipelineState) \
    ((This)->lpVtbl->SetPipelineState(This, pPipelineState))

#define ID3D12GraphicsCommandList_ResourceBarrier(This, NumBarriers, pBarriers) \
    ((This)->lpVtbl->ResourceBarrier(This, NumBarriers, pBarriers))

#define ID3D12GraphicsCommandList_ExecuteBundle(This, pCommandList) \
    ((This)->lpVtbl->ExecuteBundle(This, pCommandList))

#define ID3D12GraphicsCommandList_SetDescriptorHeaps(This, NumDescriptorHeaps, ppDescriptorHeaps) \
    ((This)->lpVtbl->SetDescriptorHeaps(This, NumDescriptorHeaps, ppDescriptorHeaps))

#define ID3D12GraphicsCommandList_SetComputeRootSignature(This, pRootSignature) \
    ((This)->lpVtbl->SetComputeRootSignature(This, pRootSignature))

#define ID3D12GraphicsCommandList_SetGraphicsRootSignature(This, pRootSignature) \
    ((This)->lpVtbl->SetGraphicsRootSignature(This, pRootSignature))

#define ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor) \
    ((This)->lpVtbl->SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor))

#define ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor) \
    ((This)->lpVtbl->SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor))

#define ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(This, RootParameterIndex, SrcData, DestOffsetIn32BitValues) \
    ((This)->lpVtbl->SetComputeRoot32BitConstant(This, RootParameterIndex, SrcData, DestOffsetIn32BitValues))

#define ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(This, RootParameterIndex, SrcData, DestOffsetIn32BitValues) \
    ((This)->lpVtbl->SetGraphicsRoot32BitConstant(This, RootParameterIndex, SrcData, DestOffsetIn32BitValues))

#define ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(This, RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues) \
    ((This)->lpVtbl->SetComputeRoot32BitConstants(This, RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues))

#define ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(This, RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues) \
    ((This)->lpVtbl->SetGraphicsRoot32BitConstants(This, RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues))

#define ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(This, RootParameterIndex, BufferLocation) \
    ((This)->lpVtbl->SetComputeRootConstantBufferView(This, RootParameterIndex, BufferLocation))

#define ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(This, RootParameterIndex, BufferLocation) \
    ((This)->lpVtbl->SetGraphicsRootConstantBufferView(This, RootParameterIndex, BufferLocation))

#define ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(This, RootParameterIndex, BufferLocation) \
    ((This)->lpVtbl->SetComputeRootShaderResourceView(This, RootParameterIndex, BufferLocation))

#define ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(This, RootParameterIndex, BufferLocation) \
    ((This)->lpVtbl->SetGraphicsRootShaderResourceView(This, RootParameterIndex, BufferLocation))

#define ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(This, RootParameterIndex, BufferLocation) \
    ((This)->lpVtbl->SetComputeRootUnorderedAccessView(This, RootParameterIndex, BufferLocation))

#define ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(This, RootParameterIndex, BufferLocation) \
    ((This)->lpVtbl->SetGraphicsRootUnorderedAccessView(This, RootParameterIndex, BufferLocation))

#define ID3D12GraphicsCommandList_IASetIndexBuffer(This, pView) \
    ((This)->lpVtbl->IASetIndexBuffer(This, pView))

#define ID3D12GraphicsCommandList_IASetVertexBuffers(This, StartSlot, NumViews, pViews) \
    ((This)->lpVtbl->IASetVertexBuffers(This, StartSlot, NumViews, pViews))

#define ID3D12GraphicsCommandList_SOSetTargets(This, StartSlot, NumViews, pViews) \
    ((This)->lpVtbl->SOSetTargets(This, StartSlot, NumViews, pViews))

#define ID3D12GraphicsCommandList_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor) \
    ((This)->lpVtbl->OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor))

#define ID3D12GraphicsCommandList_ClearDepthStencilView(This, DepthStencilView, ClearFlags, Depth, Stencil, NumRects, pRects) \
    ((This)->lpVtbl->ClearDepthStencilView(This, DepthStencilView, ClearFlags, Depth, Stencil, NumRects, pRects))

#define ID3D12GraphicsCommandList_ClearRenderTargetView(This, RenderTargetView, ColorRGBA, NumRects, pRects) \
    ((This)->lpVtbl->ClearRenderTargetView(This, RenderTargetView, ColorRGBA, NumRects, pRects))

#define ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(This, ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects) \
    ((This)->lpVtbl->ClearUnorderedAccessViewUint(This, ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects))

#define ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(This, ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects) \
    ((This)->lpVtbl->ClearUnorderedAccessViewFloat(This, ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects))

#define ID3D12GraphicsCommandList_DiscardResource(This, pResource, pRegion) \
    ((This)->lpVtbl->DiscardResource(This, pResource, pRegion))

#define ID3D12GraphicsCommandList_BeginQuery(This, pQueryHeap, Type, Index) \
    ((This)->lpVtbl->BeginQuery(This, pQueryHeap, Type, Index))

#define ID3D12GraphicsCommandList_EndQuery(This, pQueryHeap, Type, Index) \
    ((This)->lpVtbl->EndQuery(This, pQueryHeap, Type, Index))

#define ID3D12GraphicsCommandList_ResolveQueryData(This, pQueryHeap, Type, StartIndex, NumQueries, pDestinationBuffer, AlignedDestinationBufferOffset) \
    ((This)->lpVtbl->ResolveQueryData(This, pQueryHeap, Type, StartIndex, NumQueries, pDestinationBuffer, AlignedDestinationBufferOffset))

#define ID3D12GraphicsCommandList_SetPredication(This, pBuffer, AlignedBufferOffset, Operation) \
    ((This)->lpVtbl->SetPredication(This, pBuffer, AlignedBufferOffset, Operation))

#define ID3D12GraphicsCommandList_SetMarker(This, Metadata, pData, Size) \
    ((This)->lpVtbl->SetMarker(This, Metadata, pData, Size))

#define ID3D12GraphicsCommandList_BeginEvent(This, Metadata, pData, Size) \
    ((This)->lpVtbl->BeginEvent(This, Metadata, pData, Size))

#define ID3D12GraphicsCommandList_EndEvent(This) \
    ((This)->lpVtbl->EndEvent(This))

#define ID3D12GraphicsCommandList_ExecuteIndirect(This, pCommandSignature, MaxCommandCount, pArgumentBuffer, ArgumentBufferOffset, pCountBuffer, CountBufferOffset) \
    ((This)->lpVtbl->ExecuteIndirect(This, pCommandSignature, MaxCommandCount, pArgumentBuffer, ArgumentBufferOffset, pCountBuffer, CountBufferOffset))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12GraphicsCommandList_INTERFACE_DEFINED__ */

#ifndef __ID3D12CommandQueue_INTERFACE_DEFINED__
#define __ID3D12CommandQueue_INTERFACE_DEFINED__

/* interface ID3D12CommandQueue */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12CommandQueue;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("0ec870a6-5d7e-4c22-8cfc-5baae07616ed")
ID3D12CommandQueue : public ID3D12Pageable {
public:
    virtual void STDMETHODCALLTYPE UpdateTileMappings(
        _In_ ID3D12Resource * pResource,
        UINT NumResourceRegions,
        _In_reads_opt_(NumResourceRegions) const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
        _In_reads_opt_(NumResourceRegions) const D3D12_TILE_REGION_SIZE *pResourceRegionSizes,
        _In_opt_ ID3D12Heap *pHeap,
        UINT NumRanges,
        _In_reads_opt_(NumRanges) const D3D12_TILE_RANGE_FLAGS *pRangeFlags,
        _In_reads_opt_(NumRanges) const UINT *pHeapRangeStartOffsets,
        _In_reads_opt_(NumRanges) const UINT *pRangeTileCounts,
        D3D12_TILE_MAPPING_FLAGS Flags) = 0;

    virtual void STDMETHODCALLTYPE CopyTileMappings(
        _In_ ID3D12Resource * pDstResource,
        _In_ const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate,
        _In_ ID3D12Resource *pSrcResource,
        _In_ const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate,
        _In_ const D3D12_TILE_REGION_SIZE *pRegionSize,
        D3D12_TILE_MAPPING_FLAGS Flags) = 0;

    virtual void STDMETHODCALLTYPE ExecuteCommandLists(
        _In_ UINT NumCommandLists,
        _In_reads_(NumCommandLists) ID3D12CommandList *const *ppCommandLists) = 0;

    virtual void STDMETHODCALLTYPE SetMarker(
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void *pData,
        UINT Size) = 0;

    virtual void STDMETHODCALLTYPE BeginEvent(
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void *pData,
        UINT Size) = 0;

    virtual void STDMETHODCALLTYPE EndEvent(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE Signal(
        ID3D12Fence * pFence,
        UINT64 Value) = 0;

    virtual HRESULT STDMETHODCALLTYPE Wait(
        ID3D12Fence * pFence,
        UINT64 Value) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetTimestampFrequency(
        _Out_ UINT64 * pFrequency) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetClockCalibration(
        _Out_ UINT64 * pGpuTimestamp,
        _Out_ UINT64 * pCpuTimestamp) = 0;

    virtual D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc(void) = 0;
};

#else /* C style interface */

typedef struct ID3D12CommandQueueVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12CommandQueue *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12CommandQueue *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12CommandQueue *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12CommandQueue *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12CommandQueue *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12CommandQueue *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12CommandQueue *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12CommandQueue *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    void(STDMETHODCALLTYPE *UpdateTileMappings)(
        ID3D12CommandQueue *This,
        _In_ ID3D12Resource *pResource,
        UINT NumResourceRegions,
        _In_reads_opt_(NumResourceRegions) const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
        _In_reads_opt_(NumResourceRegions) const D3D12_TILE_REGION_SIZE *pResourceRegionSizes,
        _In_opt_ ID3D12Heap *pHeap,
        UINT NumRanges,
        _In_reads_opt_(NumRanges) const D3D12_TILE_RANGE_FLAGS *pRangeFlags,
        _In_reads_opt_(NumRanges) const UINT *pHeapRangeStartOffsets,
        _In_reads_opt_(NumRanges) const UINT *pRangeTileCounts,
        D3D12_TILE_MAPPING_FLAGS Flags);

    void(STDMETHODCALLTYPE *CopyTileMappings)(
        ID3D12CommandQueue *This,
        _In_ ID3D12Resource *pDstResource,
        _In_ const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate,
        _In_ ID3D12Resource *pSrcResource,
        _In_ const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate,
        _In_ const D3D12_TILE_REGION_SIZE *pRegionSize,
        D3D12_TILE_MAPPING_FLAGS Flags);

    void(STDMETHODCALLTYPE *ExecuteCommandLists)(
        ID3D12CommandQueue *This,
        _In_ UINT NumCommandLists,
        _In_reads_(NumCommandLists) ID3D12CommandList *const *ppCommandLists);

    void(STDMETHODCALLTYPE *SetMarker)(
        ID3D12CommandQueue *This,
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void *pData,
        UINT Size);

    void(STDMETHODCALLTYPE *BeginEvent)(
        ID3D12CommandQueue *This,
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void *pData,
        UINT Size);

    void(STDMETHODCALLTYPE *EndEvent)(
        ID3D12CommandQueue *This);

    HRESULT(STDMETHODCALLTYPE *Signal)
    (
        ID3D12CommandQueue *This,
        ID3D12Fence *pFence,
        UINT64 Value);

    HRESULT(STDMETHODCALLTYPE *Wait)
    (
        ID3D12CommandQueue *This,
        ID3D12Fence *pFence,
        UINT64 Value);

    HRESULT(STDMETHODCALLTYPE *GetTimestampFrequency)
    (
        ID3D12CommandQueue *This,
        _Out_ UINT64 *pFrequency);

    HRESULT(STDMETHODCALLTYPE *GetClockCalibration)
    (
        ID3D12CommandQueue *This,
        _Out_ UINT64 *pGpuTimestamp,
        _Out_ UINT64 *pCpuTimestamp);

    D3D12_COMMAND_QUEUE_DESC(STDMETHODCALLTYPE *GetDesc)
    (
        ID3D12CommandQueue *This);

    END_INTERFACE
} ID3D12CommandQueueVtbl;

interface ID3D12CommandQueue {
    CONST_VTBL struct ID3D12CommandQueueVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12CommandQueue_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12CommandQueue_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12CommandQueue_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12CommandQueue_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12CommandQueue_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12CommandQueue_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12CommandQueue_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12CommandQueue_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#define ID3D12CommandQueue_UpdateTileMappings(This, pResource, NumResourceRegions, pResourceRegionStartCoordinates, pResourceRegionSizes, pHeap, NumRanges, pRangeFlags, pHeapRangeStartOffsets, pRangeTileCounts, Flags) \
    ((This)->lpVtbl->UpdateTileMappings(This, pResource, NumResourceRegions, pResourceRegionStartCoordinates, pResourceRegionSizes, pHeap, NumRanges, pRangeFlags, pHeapRangeStartOffsets, pRangeTileCounts, Flags))

#define ID3D12CommandQueue_CopyTileMappings(This, pDstResource, pDstRegionStartCoordinate, pSrcResource, pSrcRegionStartCoordinate, pRegionSize, Flags) \
    ((This)->lpVtbl->CopyTileMappings(This, pDstResource, pDstRegionStartCoordinate, pSrcResource, pSrcRegionStartCoordinate, pRegionSize, Flags))

#define ID3D12CommandQueue_ExecuteCommandLists(This, NumCommandLists, ppCommandLists) \
    ((This)->lpVtbl->ExecuteCommandLists(This, NumCommandLists, ppCommandLists))

#define ID3D12CommandQueue_SetMarker(This, Metadata, pData, Size) \
    ((This)->lpVtbl->SetMarker(This, Metadata, pData, Size))

#define ID3D12CommandQueue_BeginEvent(This, Metadata, pData, Size) \
    ((This)->lpVtbl->BeginEvent(This, Metadata, pData, Size))

#define ID3D12CommandQueue_EndEvent(This) \
    ((This)->lpVtbl->EndEvent(This))

#define ID3D12CommandQueue_Signal(This, pFence, Value) \
    ((This)->lpVtbl->Signal(This, pFence, Value))

#define ID3D12CommandQueue_Wait(This, pFence, Value) \
    ((This)->lpVtbl->Wait(This, pFence, Value))

#define ID3D12CommandQueue_GetTimestampFrequency(This, pFrequency) \
    ((This)->lpVtbl->GetTimestampFrequency(This, pFrequency))

#define ID3D12CommandQueue_GetClockCalibration(This, pGpuTimestamp, pCpuTimestamp) \
    ((This)->lpVtbl->GetClockCalibration(This, pGpuTimestamp, pCpuTimestamp))

#define ID3D12CommandQueue_GetDesc(This) \
    ((This)->lpVtbl->GetDesc(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12CommandQueue_INTERFACE_DEFINED__ */

#ifdef __midl
#ifndef LUID_DEFINED
#define LUID_DEFINED 1
typedef struct __LUID {
    DWORD LowPart;
    LONG HighPart;
} LUID;

typedef struct __LUID *PLUID;

#endif
#endif

#define D3D12_REQ_SUBRESOURCES (30720)

#ifndef __ID3D12Device_INTERFACE_DEFINED__
#define __ID3D12Device_INTERFACE_DEFINED__

/* interface ID3D12Device */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12Device;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("189819f1-1db6-4b57-be54-1821339b85f7")
ID3D12Device : public ID3D12Object {
public:
    virtual UINT STDMETHODCALLTYPE GetNodeCount(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateCommandQueue(
        _In_ const D3D12_COMMAND_QUEUE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppCommandQueue) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateCommandAllocator(
        _In_ D3D12_COMMAND_LIST_TYPE type,
        REFIID riid,
        _COM_Outptr_ void **ppCommandAllocator) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(
        _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppPipelineState) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateComputePipelineState(
        _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppPipelineState) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateCommandList(
        _In_ UINT nodeMask,
        _In_ D3D12_COMMAND_LIST_TYPE type,
        _In_ ID3D12CommandAllocator * pCommandAllocator,
        _In_opt_ ID3D12PipelineState * pInitialState,
        REFIID riid,
        _COM_Outptr_ void **ppCommandList) = 0;

    virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
        D3D12_FEATURE Feature,
        _Inout_updates_bytes_(FeatureSupportDataSize) void *pFeatureSupportData,
        UINT FeatureSupportDataSize) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(
        _In_ const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc,
        REFIID riid,
        _COM_Outptr_ void **ppvHeap) = 0;

    virtual UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateRootSignature(
        _In_ UINT nodeMask,
        _In_reads_(blobLengthInBytes) const void *pBlobWithRootSignature,
        _In_ SIZE_T blobLengthInBytes,
        REFIID riid,
        _COM_Outptr_ void **ppvRootSignature) = 0;

    virtual void STDMETHODCALLTYPE CreateConstantBufferView(
        _In_opt_ const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;

    virtual void STDMETHODCALLTYPE CreateShaderResourceView(
        _In_opt_ ID3D12Resource * pResource,
        _In_opt_ const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;

    virtual void STDMETHODCALLTYPE CreateUnorderedAccessView(
        _In_opt_ ID3D12Resource * pResource,
        _In_opt_ ID3D12Resource * pCounterResource,
        _In_opt_ const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;

    virtual void STDMETHODCALLTYPE CreateRenderTargetView(
        _In_opt_ ID3D12Resource * pResource,
        _In_opt_ const D3D12_RENDER_TARGET_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;

    virtual void STDMETHODCALLTYPE CreateDepthStencilView(
        _In_opt_ ID3D12Resource * pResource,
        _In_opt_ const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;

    virtual void STDMETHODCALLTYPE CreateSampler(
        _In_ const D3D12_SAMPLER_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;

    virtual void STDMETHODCALLTYPE CopyDescriptors(
        _In_ UINT NumDestDescriptorRanges,
        _In_reads_(NumDestDescriptorRanges) const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts,
        _In_reads_opt_(NumDestDescriptorRanges) const UINT *pDestDescriptorRangeSizes,
        _In_ UINT NumSrcDescriptorRanges,
        _In_reads_(NumSrcDescriptorRanges) const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts,
        _In_reads_opt_(NumSrcDescriptorRanges) const UINT *pSrcDescriptorRangeSizes,
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) = 0;

    virtual void STDMETHODCALLTYPE CopyDescriptorsSimple(
        _In_ UINT NumDescriptors,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) = 0;

    virtual D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(
        _In_ UINT visibleMask,
        _In_ UINT numResourceDescs,
        _In_reads_(numResourceDescs) const D3D12_RESOURCE_DESC *pResourceDescs) = 0;

    virtual D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(
        _In_ UINT nodeMask,
        D3D12_HEAP_TYPE heapType) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateCommittedResource(
        _In_ const D3D12_HEAP_PROPERTIES *pHeapProperties,
        D3D12_HEAP_FLAGS HeapFlags,
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialResourceState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riidResource,
        _COM_Outptr_opt_ void **ppvResource) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateHeap(
        _In_ const D3D12_HEAP_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvHeap) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreatePlacedResource(
        _In_ ID3D12Heap * pHeap,
        UINT64 HeapOffset,
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvResource) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateReservedResource(
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvResource) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateSharedHandle(
        _In_ ID3D12DeviceChild * pObject,
        _In_opt_ const SECURITY_ATTRIBUTES *pAttributes,
        DWORD Access,
        _In_opt_ LPCWSTR Name,
        _Out_ HANDLE *pHandle) = 0;

    virtual HRESULT STDMETHODCALLTYPE OpenSharedHandle(
        _In_ HANDLE NTHandle,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvObj) = 0;

    virtual HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(
        _In_ LPCWSTR Name,
        DWORD Access,
        /* [annotation][out] */
        _Out_ HANDLE * pNTHandle) = 0;

    virtual HRESULT STDMETHODCALLTYPE MakeResident(
        UINT NumObjects,
        _In_reads_(NumObjects) ID3D12Pageable *const *ppObjects) = 0;

    virtual HRESULT STDMETHODCALLTYPE Evict(
        UINT NumObjects,
        _In_reads_(NumObjects) ID3D12Pageable *const *ppObjects) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateFence(
        UINT64 InitialValue,
        D3D12_FENCE_FLAGS Flags,
        REFIID riid,
        _COM_Outptr_ void **ppFence) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason(void) = 0;

    virtual void STDMETHODCALLTYPE GetCopyableFootprints(
        _In_ const D3D12_RESOURCE_DESC *pResourceDesc,
        _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
        _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
        UINT64 BaseOffset,
        _Out_writes_opt_(NumSubresources) D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts,
        _Out_writes_opt_(NumSubresources) UINT *pNumRows,
        _Out_writes_opt_(NumSubresources) UINT64 *pRowSizeInBytes,
        _Out_opt_ UINT64 *pTotalBytes) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateQueryHeap(
        _In_ const D3D12_QUERY_HEAP_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvHeap) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetStablePowerState(
        BOOL Enable) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateCommandSignature(
        _In_ const D3D12_COMMAND_SIGNATURE_DESC *pDesc,
        _In_opt_ ID3D12RootSignature *pRootSignature,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvCommandSignature) = 0;

    virtual void STDMETHODCALLTYPE GetResourceTiling(
        _In_ ID3D12Resource * pTiledResource,
        _Out_opt_ UINT * pNumTilesForEntireResource,
        _Out_opt_ D3D12_PACKED_MIP_INFO * pPackedMipDesc,
        _Out_opt_ D3D12_TILE_SHAPE * pStandardTileShapeForNonPackedMips,
        _Inout_opt_ UINT * pNumSubresourceTilings,
        _In_ UINT FirstSubresourceTilingToGet,
        _Out_writes_(*pNumSubresourceTilings) D3D12_SUBRESOURCE_TILING * pSubresourceTilingsForNonPackedMips) = 0;

    virtual LUID STDMETHODCALLTYPE GetAdapterLuid(void) = 0;
};

#else /* C style interface */

typedef struct ID3D12DeviceVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12Device *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12Device *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12Device *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12Device *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12Device *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12Device *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12Device *This,
        _In_z_ LPCWSTR Name);

    UINT(STDMETHODCALLTYPE *GetNodeCount)
    (
        ID3D12Device *This);

    HRESULT(STDMETHODCALLTYPE *CreateCommandQueue)
    (
        ID3D12Device *This,
        _In_ const D3D12_COMMAND_QUEUE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppCommandQueue);

    HRESULT(STDMETHODCALLTYPE *CreateCommandAllocator)
    (
        ID3D12Device *This,
        _In_ D3D12_COMMAND_LIST_TYPE type,
        REFIID riid,
        _COM_Outptr_ void **ppCommandAllocator);

    HRESULT(STDMETHODCALLTYPE *CreateGraphicsPipelineState)
    (
        ID3D12Device *This,
        _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppPipelineState);

    HRESULT(STDMETHODCALLTYPE *CreateComputePipelineState)
    (
        ID3D12Device *This,
        _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppPipelineState);

    HRESULT(STDMETHODCALLTYPE *CreateCommandList)
    (
        ID3D12Device *This,
        _In_ UINT nodeMask,
        _In_ D3D12_COMMAND_LIST_TYPE type,
        _In_ ID3D12CommandAllocator *pCommandAllocator,
        _In_opt_ ID3D12PipelineState *pInitialState,
        REFIID riid,
        _COM_Outptr_ void **ppCommandList);

    HRESULT(STDMETHODCALLTYPE *CheckFeatureSupport)
    (
        ID3D12Device *This,
        D3D12_FEATURE Feature,
        _Inout_updates_bytes_(FeatureSupportDataSize) void *pFeatureSupportData,
        UINT FeatureSupportDataSize);

    HRESULT(STDMETHODCALLTYPE *CreateDescriptorHeap)
    (
        ID3D12Device *This,
        _In_ const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc,
        REFIID riid,
        _COM_Outptr_ void **ppvHeap);

    UINT(STDMETHODCALLTYPE *GetDescriptorHandleIncrementSize)
    (
        ID3D12Device *This,
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType);

    HRESULT(STDMETHODCALLTYPE *CreateRootSignature)
    (
        ID3D12Device *This,
        _In_ UINT nodeMask,
        _In_reads_(blobLengthInBytes) const void *pBlobWithRootSignature,
        _In_ SIZE_T blobLengthInBytes,
        REFIID riid,
        _COM_Outptr_ void **ppvRootSignature);

    void(STDMETHODCALLTYPE *CreateConstantBufferView)(
        ID3D12Device *This,
        _In_opt_ const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    void(STDMETHODCALLTYPE *CreateShaderResourceView)(
        ID3D12Device *This,
        _In_opt_ ID3D12Resource *pResource,
        _In_opt_ const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    void(STDMETHODCALLTYPE *CreateUnorderedAccessView)(
        ID3D12Device *This,
        _In_opt_ ID3D12Resource *pResource,
        _In_opt_ ID3D12Resource *pCounterResource,
        _In_opt_ const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    void(STDMETHODCALLTYPE *CreateRenderTargetView)(
        ID3D12Device *This,
        _In_opt_ ID3D12Resource *pResource,
        _In_opt_ const D3D12_RENDER_TARGET_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    void(STDMETHODCALLTYPE *CreateDepthStencilView)(
        ID3D12Device *This,
        _In_opt_ ID3D12Resource *pResource,
        _In_opt_ const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    void(STDMETHODCALLTYPE *CreateSampler)(
        ID3D12Device *This,
        _In_ const D3D12_SAMPLER_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    void(STDMETHODCALLTYPE *CopyDescriptors)(
        ID3D12Device *This,
        _In_ UINT NumDestDescriptorRanges,
        _In_reads_(NumDestDescriptorRanges) const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts,
        _In_reads_opt_(NumDestDescriptorRanges) const UINT *pDestDescriptorRangeSizes,
        _In_ UINT NumSrcDescriptorRanges,
        _In_reads_(NumSrcDescriptorRanges) const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts,
        _In_reads_opt_(NumSrcDescriptorRanges) const UINT *pSrcDescriptorRangeSizes,
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);

    void(STDMETHODCALLTYPE *CopyDescriptorsSimple)(
        ID3D12Device *This,
        _In_ UINT NumDescriptors,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);

    D3D12_RESOURCE_ALLOCATION_INFO(STDMETHODCALLTYPE *GetResourceAllocationInfo)
    (
        ID3D12Device *This,
        _In_ UINT visibleMask,
        _In_ UINT numResourceDescs,
        _In_reads_(numResourceDescs) const D3D12_RESOURCE_DESC *pResourceDescs);

    D3D12_HEAP_PROPERTIES(STDMETHODCALLTYPE *GetCustomHeapProperties)
    (
        ID3D12Device *This,
        _In_ UINT nodeMask,
        D3D12_HEAP_TYPE heapType);

    HRESULT(STDMETHODCALLTYPE *CreateCommittedResource)
    (
        ID3D12Device *This,
        _In_ const D3D12_HEAP_PROPERTIES *pHeapProperties,
        D3D12_HEAP_FLAGS HeapFlags,
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialResourceState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riidResource,
        _COM_Outptr_opt_ void **ppvResource);

    HRESULT(STDMETHODCALLTYPE *CreateHeap)
    (
        ID3D12Device *This,
        _In_ const D3D12_HEAP_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvHeap);

    HRESULT(STDMETHODCALLTYPE *CreatePlacedResource)
    (
        ID3D12Device *This,
        _In_ ID3D12Heap *pHeap,
        UINT64 HeapOffset,
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvResource);

    HRESULT(STDMETHODCALLTYPE *CreateReservedResource)
    (
        ID3D12Device *This,
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvResource);

    HRESULT(STDMETHODCALLTYPE *CreateSharedHandle)
    (
        ID3D12Device *This,
        _In_ ID3D12DeviceChild *pObject,
        _In_opt_ const SECURITY_ATTRIBUTES *pAttributes,
        DWORD Access,
        _In_opt_ LPCWSTR Name,
        _Out_ HANDLE *pHandle);

    HRESULT(STDMETHODCALLTYPE *OpenSharedHandle)
    (
        ID3D12Device *This,
        _In_ HANDLE NTHandle,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvObj);

    HRESULT(STDMETHODCALLTYPE *OpenSharedHandleByName)
    (
        ID3D12Device *This,
        _In_ LPCWSTR Name,
        DWORD Access,
        /* [annotation][out] */
        _Out_ HANDLE *pNTHandle);

    HRESULT(STDMETHODCALLTYPE *MakeResident)
    (
        ID3D12Device *This,
        UINT NumObjects,
        _In_reads_(NumObjects) ID3D12Pageable *const *ppObjects);

    HRESULT(STDMETHODCALLTYPE *Evict)
    (
        ID3D12Device *This,
        UINT NumObjects,
        _In_reads_(NumObjects) ID3D12Pageable *const *ppObjects);

    HRESULT(STDMETHODCALLTYPE *CreateFence)
    (
        ID3D12Device *This,
        UINT64 InitialValue,
        D3D12_FENCE_FLAGS Flags,
        REFIID riid,
        _COM_Outptr_ void **ppFence);

    HRESULT(STDMETHODCALLTYPE *GetDeviceRemovedReason)
    (
        ID3D12Device *This);

    void(STDMETHODCALLTYPE *GetCopyableFootprints)(
        ID3D12Device *This,
        _In_ const D3D12_RESOURCE_DESC *pResourceDesc,
        _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
        _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
        UINT64 BaseOffset,
        _Out_writes_opt_(NumSubresources) D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts,
        _Out_writes_opt_(NumSubresources) UINT *pNumRows,
        _Out_writes_opt_(NumSubresources) UINT64 *pRowSizeInBytes,
        _Out_opt_ UINT64 *pTotalBytes);

    HRESULT(STDMETHODCALLTYPE *CreateQueryHeap)
    (
        ID3D12Device *This,
        _In_ const D3D12_QUERY_HEAP_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvHeap);

    HRESULT(STDMETHODCALLTYPE *SetStablePowerState)
    (
        ID3D12Device *This,
        BOOL Enable);

    HRESULT(STDMETHODCALLTYPE *CreateCommandSignature)
    (
        ID3D12Device *This,
        _In_ const D3D12_COMMAND_SIGNATURE_DESC *pDesc,
        _In_opt_ ID3D12RootSignature *pRootSignature,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvCommandSignature);

    void(STDMETHODCALLTYPE *GetResourceTiling)(
        ID3D12Device *This,
        _In_ ID3D12Resource *pTiledResource,
        _Out_opt_ UINT *pNumTilesForEntireResource,
        _Out_opt_ D3D12_PACKED_MIP_INFO *pPackedMipDesc,
        _Out_opt_ D3D12_TILE_SHAPE *pStandardTileShapeForNonPackedMips,
        _Inout_opt_ UINT *pNumSubresourceTilings,
        _In_ UINT FirstSubresourceTilingToGet,
        _Out_writes_(*pNumSubresourceTilings) D3D12_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips);

    LUID(STDMETHODCALLTYPE *GetAdapterLuid)
    (
        ID3D12Device *This);

    END_INTERFACE
} ID3D12DeviceVtbl;

interface ID3D12Device {
    CONST_VTBL struct ID3D12DeviceVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12Device_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12Device_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12Device_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12Device_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12Device_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12Device_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12Device_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12Device_GetNodeCount(This) \
    ((This)->lpVtbl->GetNodeCount(This))

#define ID3D12Device_CreateCommandQueue(This, pDesc, riid, ppCommandQueue) \
    ((This)->lpVtbl->CreateCommandQueue(This, pDesc, riid, ppCommandQueue))

#define ID3D12Device_CreateCommandAllocator(This, type, riid, ppCommandAllocator) \
    ((This)->lpVtbl->CreateCommandAllocator(This, type, riid, ppCommandAllocator))

#define ID3D12Device_CreateGraphicsPipelineState(This, pDesc, riid, ppPipelineState) \
    ((This)->lpVtbl->CreateGraphicsPipelineState(This, pDesc, riid, ppPipelineState))

#define ID3D12Device_CreateComputePipelineState(This, pDesc, riid, ppPipelineState) \
    ((This)->lpVtbl->CreateComputePipelineState(This, pDesc, riid, ppPipelineState))

#define ID3D12Device_CreateCommandList(This, nodeMask, type, pCommandAllocator, pInitialState, riid, ppCommandList) \
    ((This)->lpVtbl->CreateCommandList(This, nodeMask, type, pCommandAllocator, pInitialState, riid, ppCommandList))

#define ID3D12Device_CheckFeatureSupport(This, Feature, pFeatureSupportData, FeatureSupportDataSize) \
    ((This)->lpVtbl->CheckFeatureSupport(This, Feature, pFeatureSupportData, FeatureSupportDataSize))

#define ID3D12Device_CreateDescriptorHeap(This, pDescriptorHeapDesc, riid, ppvHeap) \
    ((This)->lpVtbl->CreateDescriptorHeap(This, pDescriptorHeapDesc, riid, ppvHeap))

#define ID3D12Device_GetDescriptorHandleIncrementSize(This, DescriptorHeapType) \
    ((This)->lpVtbl->GetDescriptorHandleIncrementSize(This, DescriptorHeapType))

#define ID3D12Device_CreateRootSignature(This, nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature) \
    ((This)->lpVtbl->CreateRootSignature(This, nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature))

#define ID3D12Device_CreateConstantBufferView(This, pDesc, DestDescriptor) \
    ((This)->lpVtbl->CreateConstantBufferView(This, pDesc, DestDescriptor))

#define ID3D12Device_CreateShaderResourceView(This, pResource, pDesc, DestDescriptor) \
    ((This)->lpVtbl->CreateShaderResourceView(This, pResource, pDesc, DestDescriptor))

#define ID3D12Device_CreateUnorderedAccessView(This, pResource, pCounterResource, pDesc, DestDescriptor) \
    ((This)->lpVtbl->CreateUnorderedAccessView(This, pResource, pCounterResource, pDesc, DestDescriptor))

#define ID3D12Device_CreateRenderTargetView(This, pResource, pDesc, DestDescriptor) \
    ((This)->lpVtbl->CreateRenderTargetView(This, pResource, pDesc, DestDescriptor))

#define ID3D12Device_CreateDepthStencilView(This, pResource, pDesc, DestDescriptor) \
    ((This)->lpVtbl->CreateDepthStencilView(This, pResource, pDesc, DestDescriptor))

#define ID3D12Device_CreateSampler(This, pDesc, DestDescriptor) \
    ((This)->lpVtbl->CreateSampler(This, pDesc, DestDescriptor))

#define ID3D12Device_CopyDescriptors(This, NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes, NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapsType) \
    ((This)->lpVtbl->CopyDescriptors(This, NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes, NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapsType))

#define ID3D12Device_CopyDescriptorsSimple(This, NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType) \
    ((This)->lpVtbl->CopyDescriptorsSimple(This, NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType))

#define ID3D12Device_GetResourceAllocationInfo(This, visibleMask, numResourceDescs, pResourceDescs) \
    ((This)->lpVtbl->GetResourceAllocationInfo(This, visibleMask, numResourceDescs, pResourceDescs))

#define ID3D12Device_GetCustomHeapProperties(This, nodeMask, heapType) \
    ((This)->lpVtbl->GetCustomHeapProperties(This, nodeMask, heapType))

#define ID3D12Device_CreateCommittedResource(This, pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource) \
    ((This)->lpVtbl->CreateCommittedResource(This, pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource))

#define ID3D12Device_CreateHeap(This, pDesc, riid, ppvHeap) \
    ((This)->lpVtbl->CreateHeap(This, pDesc, riid, ppvHeap))

#define ID3D12Device_CreatePlacedResource(This, pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource) \
    ((This)->lpVtbl->CreatePlacedResource(This, pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource))

#define ID3D12Device_CreateReservedResource(This, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource) \
    ((This)->lpVtbl->CreateReservedResource(This, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource))

#define ID3D12Device_CreateSharedHandle(This, pObject, pAttributes, Access, Name, pHandle) \
    ((This)->lpVtbl->CreateSharedHandle(This, pObject, pAttributes, Access, Name, pHandle))

#define ID3D12Device_OpenSharedHandle(This, NTHandle, riid, ppvObj) \
    ((This)->lpVtbl->OpenSharedHandle(This, NTHandle, riid, ppvObj))

#define ID3D12Device_OpenSharedHandleByName(This, Name, Access, pNTHandle) \
    ((This)->lpVtbl->OpenSharedHandleByName(This, Name, Access, pNTHandle))

#define ID3D12Device_MakeResident(This, NumObjects, ppObjects) \
    ((This)->lpVtbl->MakeResident(This, NumObjects, ppObjects))

#define ID3D12Device_Evict(This, NumObjects, ppObjects) \
    ((This)->lpVtbl->Evict(This, NumObjects, ppObjects))

#define ID3D12Device_CreateFence(This, InitialValue, Flags, riid, ppFence) \
    ((This)->lpVtbl->CreateFence(This, InitialValue, Flags, riid, ppFence))

#define ID3D12Device_GetDeviceRemovedReason(This) \
    ((This)->lpVtbl->GetDeviceRemovedReason(This))

#define ID3D12Device_GetCopyableFootprints(This, pResourceDesc, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes) \
    ((This)->lpVtbl->GetCopyableFootprints(This, pResourceDesc, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes))

#define ID3D12Device_CreateQueryHeap(This, pDesc, riid, ppvHeap) \
    ((This)->lpVtbl->CreateQueryHeap(This, pDesc, riid, ppvHeap))

#define ID3D12Device_SetStablePowerState(This, Enable) \
    ((This)->lpVtbl->SetStablePowerState(This, Enable))

#define ID3D12Device_CreateCommandSignature(This, pDesc, pRootSignature, riid, ppvCommandSignature) \
    ((This)->lpVtbl->CreateCommandSignature(This, pDesc, pRootSignature, riid, ppvCommandSignature))

#define ID3D12Device_GetResourceTiling(This, pTiledResource, pNumTilesForEntireResource, pPackedMipDesc, pStandardTileShapeForNonPackedMips, pNumSubresourceTilings, FirstSubresourceTilingToGet, pSubresourceTilingsForNonPackedMips) \
    ((This)->lpVtbl->GetResourceTiling(This, pTiledResource, pNumTilesForEntireResource, pPackedMipDesc, pStandardTileShapeForNonPackedMips, pNumSubresourceTilings, FirstSubresourceTilingToGet, pSubresourceTilingsForNonPackedMips))

#define ID3D12Device_GetAdapterLuid(This) \
    ((This)->lpVtbl->GetAdapterLuid(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12Device_INTERFACE_DEFINED__ */

#ifndef __ID3D12Fence_INTERFACE_DEFINED__
#define __ID3D12Fence_INTERFACE_DEFINED__

/* interface ID3D12Fence */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12Fence;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("0a753dcf-c4d8-4b91-adf6-be5a60d95a76")
ID3D12Fence : public ID3D12Pageable {
public:
    virtual UINT64 STDMETHODCALLTYPE GetCompletedValue(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetEventOnCompletion(
        UINT64 Value,
        HANDLE hEvent) = 0;

    virtual HRESULT STDMETHODCALLTYPE Signal(
        UINT64 Value) = 0;
};

#else /* C style interface */

typedef struct ID3D12FenceVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12Fence *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12Fence *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12Fence *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12Fence *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12Fence *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12Fence *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12Fence *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12Fence *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    UINT64(STDMETHODCALLTYPE *GetCompletedValue)
    (
        ID3D12Fence *This);

    HRESULT(STDMETHODCALLTYPE *SetEventOnCompletion)
    (
        ID3D12Fence *This,
        UINT64 Value,
        HANDLE hEvent);

    HRESULT(STDMETHODCALLTYPE *Signal)
    (
        ID3D12Fence *This,
        UINT64 Value);

    END_INTERFACE
} ID3D12FenceVtbl;

interface ID3D12Fence {
    CONST_VTBL struct ID3D12FenceVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12Fence_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12Fence_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12Fence_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12Fence_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12Fence_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12Fence_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12Fence_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12Fence_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#define ID3D12Fence_GetCompletedValue(This) \
    ((This)->lpVtbl->GetCompletedValue(This))

#define ID3D12Fence_SetEventOnCompletion(This, Value, hEvent) \
    ((This)->lpVtbl->SetEventOnCompletion(This, Value, hEvent))

#define ID3D12Fence_Signal(This, Value) \
    ((This)->lpVtbl->Signal(This, Value))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12Fence_INTERFACE_DEFINED__ */

#ifndef __ID3D12PipelineState_INTERFACE_DEFINED__
#define __ID3D12PipelineState_INTERFACE_DEFINED__

/* interface ID3D12PipelineState */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12PipelineState;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("765a30f3-f624-4c6f-a828-ace948622445")
ID3D12PipelineState : public ID3D12Pageable {
public:
    virtual HRESULT STDMETHODCALLTYPE GetCachedBlob(
        _COM_Outptr_ ID3DBlob * *ppBlob) = 0;
};

#else /* C style interface */

typedef struct ID3D12PipelineStateVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12PipelineState *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12PipelineState *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12PipelineState *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12PipelineState *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12PipelineState *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12PipelineState *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12PipelineState *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12PipelineState *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    HRESULT(STDMETHODCALLTYPE *GetCachedBlob)
    (
        ID3D12PipelineState *This,
        _COM_Outptr_ ID3DBlob **ppBlob);

    END_INTERFACE
} ID3D12PipelineStateVtbl;

interface ID3D12PipelineState {
    CONST_VTBL struct ID3D12PipelineStateVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12PipelineState_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12PipelineState_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12PipelineState_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12PipelineState_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12PipelineState_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12PipelineState_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12PipelineState_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12PipelineState_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#define ID3D12PipelineState_GetCachedBlob(This, ppBlob) \
    ((This)->lpVtbl->GetCachedBlob(This, ppBlob))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12PipelineState_INTERFACE_DEFINED__ */

#ifndef __ID3D12DescriptorHeap_INTERFACE_DEFINED__
#define __ID3D12DescriptorHeap_INTERFACE_DEFINED__

/* interface ID3D12DescriptorHeap */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12DescriptorHeap;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("8efb471d-616c-4f49-90f7-127bb763fa51")
ID3D12DescriptorHeap : public ID3D12Pageable {
public:
    virtual D3D12_DESCRIPTOR_HEAP_DESC STDMETHODCALLTYPE GetDesc(void) = 0;

    virtual D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetCPUDescriptorHandleForHeapStart(void) = 0;

    virtual D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE GetGPUDescriptorHandleForHeapStart(void) = 0;
};

#else /* C style interface */

typedef struct ID3D12DescriptorHeapVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12DescriptorHeap *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12DescriptorHeap *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12DescriptorHeap *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12DescriptorHeap *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12DescriptorHeap *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12DescriptorHeap *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12DescriptorHeap *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12DescriptorHeap *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    D3D12_DESCRIPTOR_HEAP_DESC(STDMETHODCALLTYPE *GetDesc)
    (
        ID3D12DescriptorHeap *This);

    D3D12_CPU_DESCRIPTOR_HANDLE(STDMETHODCALLTYPE *GetCPUDescriptorHandleForHeapStart)
    (
        ID3D12DescriptorHeap *This);

    D3D12_GPU_DESCRIPTOR_HANDLE(STDMETHODCALLTYPE *GetGPUDescriptorHandleForHeapStart)
    (
        ID3D12DescriptorHeap *This);

    END_INTERFACE
} ID3D12DescriptorHeapVtbl;

interface ID3D12DescriptorHeap {
    CONST_VTBL struct ID3D12DescriptorHeapVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12DescriptorHeap_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12DescriptorHeap_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12DescriptorHeap_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12DescriptorHeap_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12DescriptorHeap_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12DescriptorHeap_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12DescriptorHeap_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12DescriptorHeap_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#define ID3D12DescriptorHeap_GetDesc(This) \
    ((This)->lpVtbl->GetDesc(This))

#define ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(This) \
    ((This)->lpVtbl->GetCPUDescriptorHandleForHeapStart(This))

#define ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(This) \
    ((This)->lpVtbl->GetGPUDescriptorHandleForHeapStart(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12DescriptorHeap_INTERFACE_DEFINED__ */

#ifndef __ID3D12QueryHeap_INTERFACE_DEFINED__
#define __ID3D12QueryHeap_INTERFACE_DEFINED__

/* interface ID3D12QueryHeap */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12QueryHeap;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("0d9658ae-ed45-469e-a61d-970ec583cab4")
ID3D12QueryHeap : public ID3D12Pageable{
    public :
};

#else /* C style interface */

typedef struct ID3D12QueryHeapVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12QueryHeap *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12QueryHeap *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12QueryHeap *This);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        ID3D12QueryHeap *This,
        _In_ REFGUID guid,
        _Inout_ UINT *pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        ID3D12QueryHeap *This,
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        ID3D12QueryHeap *This,
        _In_ REFGUID guid,
        _In_opt_ const IUnknown *pData);

    HRESULT(STDMETHODCALLTYPE *SetName)
    (
        ID3D12QueryHeap *This,
        _In_z_ LPCWSTR Name);

    HRESULT(STDMETHODCALLTYPE *GetDevice)
    (
        ID3D12QueryHeap *This,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvDevice);

    END_INTERFACE
} ID3D12QueryHeapVtbl;

interface ID3D12QueryHeap {
    CONST_VTBL struct ID3D12QueryHeapVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12QueryHeap_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12QueryHeap_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12QueryHeap_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12QueryHeap_GetPrivateData(This, guid, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, guid, pDataSize, pData))

#define ID3D12QueryHeap_SetPrivateData(This, guid, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, guid, DataSize, pData))

#define ID3D12QueryHeap_SetPrivateDataInterface(This, guid, pData) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, guid, pData))

#define ID3D12QueryHeap_SetName(This, Name) \
    ((This)->lpVtbl->SetName(This, Name))

#define ID3D12QueryHeap_GetDevice(This, riid, ppvDevice) \
    ((This)->lpVtbl->GetDevice(This, riid, ppvDevice))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12QueryHeap_INTERFACE_DEFINED__ */

typedef HRESULT(WINAPI *PFN_D3D12_CREATE_DEVICE)(_In_opt_ IUnknown *,
                                                 D3D_FEATURE_LEVEL,
                                                 _In_ REFIID, _COM_Outptr_opt_ void **);

typedef HRESULT(WINAPI *PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(
    _In_ const D3D12_ROOT_SIGNATURE_DESC *pRootSignature,
    _In_ D3D_ROOT_SIGNATURE_VERSION Version,
    _Out_ ID3DBlob **ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob **ppErrorBlob);

typedef HRESULT(WINAPI *PFN_D3D12_GET_DEBUG_INTERFACE)(_In_ REFIID, _COM_Outptr_opt_ void **);

DEFINE_GUID(IID_ID3D12Object, 0xc4fec28f, 0x7966, 0x4e95, 0x9f, 0x94, 0xf4, 0x31, 0xcb, 0x56, 0xc3, 0xb8);
DEFINE_GUID(IID_ID3D12DeviceChild, 0x905db94b, 0xa00c, 0x4140, 0x9d, 0xf5, 0x2b, 0x64, 0xca, 0x9e, 0xa3, 0x57);
DEFINE_GUID(IID_ID3D12RootSignature, 0xc54a6b66, 0x72df, 0x4ee8, 0x8b, 0xe5, 0xa9, 0x46, 0xa1, 0x42, 0x92, 0x14);
DEFINE_GUID(IID_ID3D12RootSignatureDeserializer, 0x34AB647B, 0x3CC8, 0x46AC, 0x84, 0x1B, 0xC0, 0x96, 0x56, 0x45, 0xC0, 0x46);
DEFINE_GUID(IID_ID3D12VersionedRootSignatureDeserializer, 0x7F91CE67, 0x090C, 0x4BB7, 0xB7, 0x8E, 0xED, 0x8F, 0xF2, 0xE3, 0x1D, 0xA0);
DEFINE_GUID(IID_ID3D12Pageable, 0x63ee58fb, 0x1268, 0x4835, 0x86, 0xda, 0xf0, 0x08, 0xce, 0x62, 0xf0, 0xd6);
DEFINE_GUID(IID_ID3D12Heap, 0x6b3b2502, 0x6e51, 0x45b3, 0x90, 0xee, 0x98, 0x84, 0x26, 0x5e, 0x8d, 0xf3);
DEFINE_GUID(IID_ID3D12Resource, 0x696442be, 0xa72e, 0x4059, 0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad);
DEFINE_GUID(IID_ID3D12CommandAllocator, 0x6102dee4, 0xaf59, 0x4b09, 0xb9, 0x99, 0xb4, 0x4d, 0x73, 0xf0, 0x9b, 0x24);
DEFINE_GUID(IID_ID3D12Fence, 0x0a753dcf, 0xc4d8, 0x4b91, 0xad, 0xf6, 0xbe, 0x5a, 0x60, 0xd9, 0x5a, 0x76);
DEFINE_GUID(IID_ID3D12PipelineState, 0x765a30f3, 0xf624, 0x4c6f, 0xa8, 0x28, 0xac, 0xe9, 0x48, 0x62, 0x24, 0x45);
DEFINE_GUID(IID_ID3D12DescriptorHeap, 0x8efb471d, 0x616c, 0x4f49, 0x90, 0xf7, 0x12, 0x7b, 0xb7, 0x63, 0xfa, 0x51);
DEFINE_GUID(IID_ID3D12QueryHeap, 0x0d9658ae, 0xed45, 0x469e, 0xa6, 0x1d, 0x97, 0x0e, 0xc5, 0x83, 0xca, 0xb4);
DEFINE_GUID(IID_ID3D12CommandSignature, 0xc36a797c, 0xec80, 0x4f0a, 0x89, 0x85, 0xa7, 0xb2, 0x47, 0x50, 0x82, 0xd1);
DEFINE_GUID(IID_ID3D12CommandList, 0x7116d91c, 0xe7e4, 0x47ce, 0xb8, 0xc6, 0xec, 0x81, 0x68, 0xf4, 0x37, 0xe5);
DEFINE_GUID(IID_ID3D12GraphicsCommandList, 0x5b160d0f, 0xac1b, 0x4185, 0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55);
DEFINE_GUID(IID_ID3D12GraphicsCommandList1, 0x553103fb, 0x1fe7, 0x4557, 0xbb, 0x38, 0x94, 0x6d, 0x7d, 0x0e, 0x7c, 0xa7);
DEFINE_GUID(IID_ID3D12CommandQueue, 0x0ec870a6, 0x5d7e, 0x4c22, 0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed);
DEFINE_GUID(IID_ID3D12Device, 0x189819f1, 0x1db6, 0x4b57, 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7);
DEFINE_GUID(IID_ID3D12PipelineLibrary, 0xc64226a8, 0x9201, 0x46af, 0xb4, 0xcc, 0x53, 0xfb, 0x9f, 0xf7, 0x41, 0x4f);
DEFINE_GUID(IID_ID3D12PipelineLibrary1, 0x80eabf42, 0x2568, 0x4e5e, 0xbd, 0x82, 0xc3, 0x7f, 0x86, 0x96, 0x1d, 0xc3);
DEFINE_GUID(IID_ID3D12Device1, 0x77acce80, 0x638e, 0x4e65, 0x88, 0x95, 0xc1, 0xf2, 0x33, 0x86, 0x86, 0x3e);
DEFINE_GUID(IID_ID3D12Device2, 0x30baa41e, 0xb15b, 0x475c, 0xa0, 0xbb, 0x1a, 0xf5, 0xc5, 0xb6, 0x43, 0x28);
DEFINE_GUID(IID_ID3D12Tools, 0x7071e1f0, 0xe84b, 0x4b33, 0x97, 0x4f, 0x12, 0xfa, 0x49, 0xde, 0x65, 0xc5);

#ifdef __cplusplus
}
#endif

/* d3d12sdklayers.h */
#ifndef __ID3D12Debug_FWD_DEFINED__
#define __ID3D12Debug_FWD_DEFINED__
typedef interface ID3D12Debug ID3D12Debug;

#endif /* __ID3D12Debug_FWD_DEFINED__ */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __ID3D12Debug_INTERFACE_DEFINED__
#define __ID3D12Debug_INTERFACE_DEFINED__

/* interface ID3D12Debug */
/* [unique][local][object][uuid] */

EXTERN_C const IID IID_ID3D12Debug;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("344488b7-6846-474b-b989-f027448245e0")
ID3D12Debug : public IUnknown {
public:
    virtual void STDMETHODCALLTYPE EnableDebugLayer(void) = 0;
};

#else /* C style interface */

typedef struct ID3D12DebugVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        ID3D12Debug *This,
        REFIID riid,
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        ID3D12Debug *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        ID3D12Debug *This);

    void(STDMETHODCALLTYPE *EnableDebugLayer)(
        ID3D12Debug *This);

    END_INTERFACE
} ID3D12DebugVtbl;

interface ID3D12Debug {
    CONST_VTBL struct ID3D12DebugVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define ID3D12Debug_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ID3D12Debug_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ID3D12Debug_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ID3D12Debug_EnableDebugLayer(This) \
    ((This)->lpVtbl->EnableDebugLayer(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __ID3D12Debug_INTERFACE_DEFINED__ */

DEFINE_GUID(IID_ID3D12Debug, 0x344488b7, 0x6846, 0x474b, 0xb9, 0x89, 0xf0, 0x27, 0x44, 0x82, 0x45, 0xe0);

#ifdef __cplusplus
}
#endif

/* dxgi.h */
#ifndef __IDXGIObject_FWD_DEFINED__
#define __IDXGIObject_FWD_DEFINED__
typedef interface IDXGIObject IDXGIObject;

#endif /* __IDXGIObject_FWD_DEFINED__ */

#ifndef __IDXGIAdapter_FWD_DEFINED__
#define __IDXGIAdapter_FWD_DEFINED__
typedef interface IDXGIAdapter IDXGIAdapter;

#endif /* __IDXGIAdapter_FWD_DEFINED__ */

#ifndef __IDXGIOutput_FWD_DEFINED__
#define __IDXGIOutput_FWD_DEFINED__
typedef interface IDXGIOutput IDXGIOutput;

#endif /* __IDXGIOutput_FWD_DEFINED__ */

#ifndef __IDXGISwapChain_FWD_DEFINED__
#define __IDXGISwapChain_FWD_DEFINED__
typedef interface IDXGISwapChain IDXGISwapChain;

#endif /* __IDXGISwapChain_FWD_DEFINED__ */

#ifndef __IDXGIFactory_FWD_DEFINED__
#define __IDXGIFactory_FWD_DEFINED__
typedef interface IDXGIFactory IDXGIFactory;

#endif /* __IDXGIFactory_FWD_DEFINED__ */

#ifndef __IDXGIFactory1_FWD_DEFINED__
#define __IDXGIFactory1_FWD_DEFINED__
typedef interface IDXGIFactory1 IDXGIFactory1;

#endif /* __IDXGIFactory1_FWD_DEFINED__ */

#ifndef __IDXGIAdapter1_FWD_DEFINED__
#define __IDXGIAdapter1_FWD_DEFINED__
typedef interface IDXGIAdapter1 IDXGIAdapter1;

#endif /* __IDXGIAdapter1_FWD_DEFINED__ */

typedef struct DXGI_ADAPTER_DESC {
    WCHAR Description[128];
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID AdapterLuid;
} DXGI_ADAPTER_DESC;

typedef enum DXGI_ADAPTER_FLAG {
    DXGI_ADAPTER_FLAG_NONE = 0,
    DXGI_ADAPTER_FLAG_REMOTE = 1,
    DXGI_ADAPTER_FLAG_SOFTWARE = 2,
    DXGI_ADAPTER_FLAG_FORCE_DWORD = 0xffffffff
} DXGI_ADAPTER_FLAG;

typedef struct DXGI_ADAPTER_DESC1 {
    WCHAR Description[128];
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID AdapterLuid;
    UINT Flags;
} DXGI_ADAPTER_DESC1;

typedef struct DXGI_SWAP_CHAIN_DESC DXGI_SWAP_CHAIN_DESC;

#ifndef __IDXGIObject_INTERFACE_DEFINED__
#define __IDXGIObject_INTERFACE_DEFINED__

/* interface IDXGIObject */
/* [unique][local][uuid][object] */

EXTERN_C const IID IID_IDXGIObject;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("aec22fb8-76f3-4639-9be0-28eb43a67a2e")
IDXGIObject : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [in] */ UINT DataSize,
        /* [annotation][in] */
        _In_reads_bytes_(DataSize) const void *pData) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][in] */
        _In_opt_ const IUnknown *pUnknown) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][out][in] */
        _Inout_ UINT * pDataSize,
        /* [annotation][out] */
        _Out_writes_bytes_(*pDataSize) void *pData) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetParent(
        /* [annotation][in] */
        _In_ REFIID riid,
        /* [annotation][retval][out] */
        _COM_Outptr_ void **ppParent) = 0;
};

#else /* C style interface */

typedef struct IDXGIObjectVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        IDXGIObject *This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        IDXGIObject *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        IDXGIObject *This);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        IDXGIObject *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [in] */ UINT DataSize,
        /* [annotation][in] */
        _In_reads_bytes_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        IDXGIObject *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][in] */
        _In_opt_ const IUnknown *pUnknown);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        IDXGIObject *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][out][in] */
        _Inout_ UINT *pDataSize,
        /* [annotation][out] */
        _Out_writes_bytes_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *GetParent)
    (
        IDXGIObject *This,
        /* [annotation][in] */
        _In_ REFIID riid,
        /* [annotation][retval][out] */
        _COM_Outptr_ void **ppParent);

    END_INTERFACE
} IDXGIObjectVtbl;

interface IDXGIObject {
    CONST_VTBL struct IDXGIObjectVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define IDXGIObject_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDXGIObject_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IDXGIObject_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IDXGIObject_SetPrivateData(This, Name, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, Name, DataSize, pData))

#define IDXGIObject_SetPrivateDataInterface(This, Name, pUnknown) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, Name, pUnknown))

#define IDXGIObject_GetPrivateData(This, Name, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, Name, pDataSize, pData))

#define IDXGIObject_GetParent(This, riid, ppParent) \
    ((This)->lpVtbl->GetParent(This, riid, ppParent))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDXGIObject_INTERFACE_DEFINED__ */

#ifndef __IDXGIFactory_INTERFACE_DEFINED__
#define __IDXGIFactory_INTERFACE_DEFINED__

/* interface IDXGIFactory */
/* [unique][local][uuid][object] */

EXTERN_C const IID IID_IDXGIFactory;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("7b7166ec-21c7-44ae-b21a-c9ae321ae369")
IDXGIFactory : public IDXGIObject {
public:
    virtual HRESULT STDMETHODCALLTYPE EnumAdapters(
        /* [in] */ UINT Adapter,
        /* [annotation][out] */
        _COM_Outptr_ IDXGIAdapter * *ppAdapter) = 0;

    virtual HRESULT STDMETHODCALLTYPE MakeWindowAssociation(
        HWND WindowHandle,
        UINT Flags) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetWindowAssociation(
        /* [annotation][out] */
        _Out_ HWND * pWindowHandle) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateSwapChain(
        /* [annotation][in] */
        _In_ IUnknown * pDevice,
        /* [annotation][in] */
        _In_ DXGI_SWAP_CHAIN_DESC * pDesc,
        /* [annotation][out] */
        _COM_Outptr_ IDXGISwapChain * *ppSwapChain) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(
        /* [in] */ HMODULE Module,
        /* [annotation][out] */
        _COM_Outptr_ IDXGIAdapter * *ppAdapter) = 0;
};

#else /* C style interface */

typedef struct IDXGIFactoryVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        IDXGIFactory *This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        IDXGIFactory *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        IDXGIFactory *This);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        IDXGIFactory *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [in] */ UINT DataSize,
        /* [annotation][in] */
        _In_reads_bytes_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        IDXGIFactory *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][in] */
        _In_opt_ const IUnknown *pUnknown);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        IDXGIFactory *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][out][in] */
        _Inout_ UINT *pDataSize,
        /* [annotation][out] */
        _Out_writes_bytes_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *GetParent)
    (
        IDXGIFactory *This,
        /* [annotation][in] */
        _In_ REFIID riid,
        /* [annotation][retval][out] */
        _COM_Outptr_ void **ppParent);

    HRESULT(STDMETHODCALLTYPE *EnumAdapters)
    (
        IDXGIFactory *This,
        /* [in] */ UINT Adapter,
        /* [annotation][out] */
        _COM_Outptr_ IDXGIAdapter **ppAdapter);

    HRESULT(STDMETHODCALLTYPE *MakeWindowAssociation)
    (
        IDXGIFactory *This,
        HWND WindowHandle,
        UINT Flags);

    HRESULT(STDMETHODCALLTYPE *GetWindowAssociation)
    (
        IDXGIFactory *This,
        /* [annotation][out] */
        _Out_ HWND *pWindowHandle);

    HRESULT(STDMETHODCALLTYPE *CreateSwapChain)
    (
        IDXGIFactory *This,
        /* [annotation][in] */
        _In_ IUnknown *pDevice,
        /* [annotation][in] */
        _In_ DXGI_SWAP_CHAIN_DESC *pDesc,
        /* [annotation][out] */
        _COM_Outptr_ IDXGISwapChain **ppSwapChain);

    HRESULT(STDMETHODCALLTYPE *CreateSoftwareAdapter)
    (
        IDXGIFactory *This,
        /* [in] */ HMODULE Module,
        /* [annotation][out] */
        _COM_Outptr_ IDXGIAdapter **ppAdapter);

    END_INTERFACE
} IDXGIFactoryVtbl;

interface IDXGIFactory {
    CONST_VTBL struct IDXGIFactoryVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define IDXGIFactory_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDXGIFactory_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IDXGIFactory_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IDXGIFactory_SetPrivateData(This, Name, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, Name, DataSize, pData))

#define IDXGIFactory_SetPrivateDataInterface(This, Name, pUnknown) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, Name, pUnknown))

#define IDXGIFactory_GetPrivateData(This, Name, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, Name, pDataSize, pData))

#define IDXGIFactory_GetParent(This, riid, ppParent) \
    ((This)->lpVtbl->GetParent(This, riid, ppParent))

#define IDXGIFactory_EnumAdapters(This, Adapter, ppAdapter) \
    ((This)->lpVtbl->EnumAdapters(This, Adapter, ppAdapter))

#define IDXGIFactory_MakeWindowAssociation(This, WindowHandle, Flags) \
    ((This)->lpVtbl->MakeWindowAssociation(This, WindowHandle, Flags))

#define IDXGIFactory_GetWindowAssociation(This, pWindowHandle) \
    ((This)->lpVtbl->GetWindowAssociation(This, pWindowHandle))

#define IDXGIFactory_CreateSwapChain(This, pDevice, pDesc, ppSwapChain) \
    ((This)->lpVtbl->CreateSwapChain(This, pDevice, pDesc, ppSwapChain))

#define IDXGIFactory_CreateSoftwareAdapter(This, Module, ppAdapter) \
    ((This)->lpVtbl->CreateSoftwareAdapter(This, Module, ppAdapter))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDXGIFactory_INTERFACE_DEFINED__ */

struct DXGI_ADAPTER_DESC;

#ifndef __IDXGIAdapter_INTERFACE_DEFINED__
#define __IDXGIAdapter_INTERFACE_DEFINED__

/* interface IDXGIAdapter */
/* [unique][local][uuid][object] */

EXTERN_C const IID IID_IDXGIAdapter;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("2411e7e1-12ac-4ccf-bd14-9798e8534dc0")
IDXGIAdapter : public IDXGIObject {
public:
    virtual HRESULT STDMETHODCALLTYPE EnumOutputs(
        /* [in] */ UINT Output,
        /* [annotation][out][in] */
        _COM_Outptr_ IDXGIOutput * *ppOutput) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDesc(
        /* [annotation][out] */
        _Out_ DXGI_ADAPTER_DESC * pDesc) = 0;

    virtual HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(
        /* [annotation][in] */
        _In_ REFGUID InterfaceName,
        /* [annotation][out] */
        _Out_ LARGE_INTEGER * pUMDVersion) = 0;
};

#else /* C style interface */

typedef struct IDXGIAdapterVtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        IDXGIAdapter *This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        IDXGIAdapter *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        IDXGIAdapter *This);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        IDXGIAdapter *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [in] */ UINT DataSize,
        /* [annotation][in] */
        _In_reads_bytes_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        IDXGIAdapter *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][in] */
        _In_opt_ const IUnknown *pUnknown);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        IDXGIAdapter *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][out][in] */
        _Inout_ UINT *pDataSize,
        /* [annotation][out] */
        _Out_writes_bytes_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *GetParent)
    (
        IDXGIAdapter *This,
        /* [annotation][in] */
        _In_ REFIID riid,
        /* [annotation][retval][out] */
        _COM_Outptr_ void **ppParent);

    HRESULT(STDMETHODCALLTYPE *EnumOutputs)
    (
        IDXGIAdapter *This,
        /* [in] */ UINT Output,
        /* [annotation][out][in] */
        _COM_Outptr_ IDXGIOutput **ppOutput);

    HRESULT(STDMETHODCALLTYPE *GetDesc)
    (
        IDXGIAdapter *This,
        /* [annotation][out] */
        _Out_ DXGI_ADAPTER_DESC *pDesc);

    HRESULT(STDMETHODCALLTYPE *CheckInterfaceSupport)
    (
        IDXGIAdapter *This,
        /* [annotation][in] */
        _In_ REFGUID InterfaceName,
        /* [annotation][out] */
        _Out_ LARGE_INTEGER *pUMDVersion);

    END_INTERFACE
} IDXGIAdapterVtbl;

interface IDXGIAdapter {
    CONST_VTBL struct IDXGIAdapterVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define IDXGIAdapter_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDXGIAdapter_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IDXGIAdapter_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IDXGIAdapter_SetPrivateData(This, Name, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, Name, DataSize, pData))

#define IDXGIAdapter_SetPrivateDataInterface(This, Name, pUnknown) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, Name, pUnknown))

#define IDXGIAdapter_GetPrivateData(This, Name, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, Name, pDataSize, pData))

#define IDXGIAdapter_GetParent(This, riid, ppParent) \
    ((This)->lpVtbl->GetParent(This, riid, ppParent))

#define IDXGIAdapter_EnumOutputs(This, Output, ppOutput) \
    ((This)->lpVtbl->EnumOutputs(This, Output, ppOutput))

#define IDXGIAdapter_GetDesc(This, pDesc) \
    ((This)->lpVtbl->GetDesc(This, pDesc))

#define IDXGIAdapter_CheckInterfaceSupport(This, InterfaceName, pUMDVersion) \
    ((This)->lpVtbl->CheckInterfaceSupport(This, InterfaceName, pUMDVersion))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDXGIAdapter_INTERFACE_DEFINED__ */

#ifndef __IDXGIFactory1_INTERFACE_DEFINED__
#define __IDXGIFactory1_INTERFACE_DEFINED__

/* interface IDXGIFactory1 */
/* [unique][local][uuid][object] */

EXTERN_C const IID IID_IDXGIFactory1;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("770aae78-f26f-4dba-a829-253c83d1b387")
IDXGIFactory1 : public IDXGIFactory {
public:
    virtual HRESULT STDMETHODCALLTYPE EnumAdapters1(
        /* [in] */ UINT Adapter,
        /* [annotation][out] */
        _COM_Outptr_ IDXGIAdapter1 * *ppAdapter) = 0;

    virtual BOOL STDMETHODCALLTYPE IsCurrent(void) = 0;
};

#else /* C style interface */

typedef struct IDXGIFactory1Vtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        IDXGIFactory1 *This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        IDXGIFactory1 *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        IDXGIFactory1 *This);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        IDXGIFactory1 *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [in] */ UINT DataSize,
        /* [annotation][in] */
        _In_reads_bytes_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        IDXGIFactory1 *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][in] */
        _In_opt_ const IUnknown *pUnknown);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        IDXGIFactory1 *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][out][in] */
        _Inout_ UINT *pDataSize,
        /* [annotation][out] */
        _Out_writes_bytes_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *GetParent)
    (
        IDXGIFactory1 *This,
        /* [annotation][in] */
        _In_ REFIID riid,
        /* [annotation][retval][out] */
        _COM_Outptr_ void **ppParent);

    HRESULT(STDMETHODCALLTYPE *EnumAdapters)
    (
        IDXGIFactory1 *This,
        /* [in] */ UINT Adapter,
        /* [annotation][out] */
        _COM_Outptr_ IDXGIAdapter **ppAdapter);

    HRESULT(STDMETHODCALLTYPE *MakeWindowAssociation)
    (
        IDXGIFactory1 *This,
        HWND WindowHandle,
        UINT Flags);

    HRESULT(STDMETHODCALLTYPE *GetWindowAssociation)
    (
        IDXGIFactory1 *This,
        /* [annotation][out] */
        _Out_ HWND *pWindowHandle);

    HRESULT(STDMETHODCALLTYPE *CreateSwapChain)
    (
        IDXGIFactory1 *This,
        /* [annotation][in] */
        _In_ IUnknown *pDevice,
        /* [annotation][in] */
        _In_ DXGI_SWAP_CHAIN_DESC *pDesc,
        /* [annotation][out] */
        _COM_Outptr_ IDXGISwapChain **ppSwapChain);

    HRESULT(STDMETHODCALLTYPE *CreateSoftwareAdapter)
    (
        IDXGIFactory1 *This,
        /* [in] */ HMODULE Module,
        /* [annotation][out] */
        _COM_Outptr_ IDXGIAdapter **ppAdapter);

    HRESULT(STDMETHODCALLTYPE *EnumAdapters1)
    (
        IDXGIFactory1 *This,
        /* [in] */ UINT Adapter,
        /* [annotation][out] */
        _COM_Outptr_ IDXGIAdapter1 **ppAdapter);

    BOOL(STDMETHODCALLTYPE *IsCurrent)
    (
        IDXGIFactory1 *This);

    END_INTERFACE
} IDXGIFactory1Vtbl;

interface IDXGIFactory1 {
    CONST_VTBL struct IDXGIFactory1Vtbl *lpVtbl;
};

#ifdef COBJMACROS

#define IDXGIFactory1_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDXGIFactory1_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IDXGIFactory1_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IDXGIFactory1_SetPrivateData(This, Name, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, Name, DataSize, pData))

#define IDXGIFactory1_SetPrivateDataInterface(This, Name, pUnknown) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, Name, pUnknown))

#define IDXGIFactory1_GetPrivateData(This, Name, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, Name, pDataSize, pData))

#define IDXGIFactory1_GetParent(This, riid, ppParent) \
    ((This)->lpVtbl->GetParent(This, riid, ppParent))

#define IDXGIFactory1_EnumAdapters(This, Adapter, ppAdapter) \
    ((This)->lpVtbl->EnumAdapters(This, Adapter, ppAdapter))

#define IDXGIFactory1_MakeWindowAssociation(This, WindowHandle, Flags) \
    ((This)->lpVtbl->MakeWindowAssociation(This, WindowHandle, Flags))

#define IDXGIFactory1_GetWindowAssociation(This, pWindowHandle) \
    ((This)->lpVtbl->GetWindowAssociation(This, pWindowHandle))

#define IDXGIFactory1_CreateSwapChain(This, pDevice, pDesc, ppSwapChain) \
    ((This)->lpVtbl->CreateSwapChain(This, pDevice, pDesc, ppSwapChain))

#define IDXGIFactory1_CreateSoftwareAdapter(This, Module, ppAdapter) \
    ((This)->lpVtbl->CreateSoftwareAdapter(This, Module, ppAdapter))

#define IDXGIFactory1_EnumAdapters1(This, Adapter, ppAdapter) \
    ((This)->lpVtbl->EnumAdapters1(This, Adapter, ppAdapter))

#define IDXGIFactory1_IsCurrent(This) \
    ((This)->lpVtbl->IsCurrent(This))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDXGIFactory1_INTERFACE_DEFINED__ */

#ifndef __IDXGIAdapter1_INTERFACE_DEFINED__
#define __IDXGIAdapter1_INTERFACE_DEFINED__

/* interface IDXGIAdapter1 */
/* [unique][local][uuid][object] */

EXTERN_C const IID IID_IDXGIAdapter1;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("29038f61-3839-4626-91fd-086879011a05")
IDXGIAdapter1 : public IDXGIAdapter {
public:
    virtual HRESULT STDMETHODCALLTYPE GetDesc1(
        /* [annotation][out] */
        _Out_ DXGI_ADAPTER_DESC1 * pDesc) = 0;
};

#else /* C style interface */

typedef struct IDXGIAdapter1Vtbl {
    BEGIN_INTERFACE

    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (
        IDXGIAdapter1 *This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */
        _COM_Outptr_ void **ppvObject);

    ULONG(STDMETHODCALLTYPE *AddRef)
    (
        IDXGIAdapter1 *This);

    ULONG(STDMETHODCALLTYPE *Release)
    (
        IDXGIAdapter1 *This);

    HRESULT(STDMETHODCALLTYPE *SetPrivateData)
    (
        IDXGIAdapter1 *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [in] */ UINT DataSize,
        /* [annotation][in] */
        _In_reads_bytes_(DataSize) const void *pData);

    HRESULT(STDMETHODCALLTYPE *SetPrivateDataInterface)
    (
        IDXGIAdapter1 *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][in] */
        _In_opt_ const IUnknown *pUnknown);

    HRESULT(STDMETHODCALLTYPE *GetPrivateData)
    (
        IDXGIAdapter1 *This,
        /* [annotation][in] */
        _In_ REFGUID Name,
        /* [annotation][out][in] */
        _Inout_ UINT *pDataSize,
        /* [annotation][out] */
        _Out_writes_bytes_(*pDataSize) void *pData);

    HRESULT(STDMETHODCALLTYPE *GetParent)
    (
        IDXGIAdapter1 *This,
        /* [annotation][in] */
        _In_ REFIID riid,
        /* [annotation][retval][out] */
        _COM_Outptr_ void **ppParent);

    HRESULT(STDMETHODCALLTYPE *EnumOutputs)
    (
        IDXGIAdapter1 *This,
        /* [in] */ UINT Output,
        /* [annotation][out][in] */
        _COM_Outptr_ IDXGIOutput **ppOutput);

    HRESULT(STDMETHODCALLTYPE *GetDesc)
    (
        IDXGIAdapter1 *This,
        /* [annotation][out] */
        _Out_ DXGI_ADAPTER_DESC *pDesc);

    HRESULT(STDMETHODCALLTYPE *CheckInterfaceSupport)
    (
        IDXGIAdapter1 *This,
        /* [annotation][in] */
        _In_ REFGUID InterfaceName,
        /* [annotation][out] */
        _Out_ LARGE_INTEGER *pUMDVersion);

    HRESULT(STDMETHODCALLTYPE *GetDesc1)
    (
        IDXGIAdapter1 *This,
        /* [annotation][out] */
        _Out_ DXGI_ADAPTER_DESC1 *pDesc);

    END_INTERFACE
} IDXGIAdapter1Vtbl;

interface IDXGIAdapter1 {
    CONST_VTBL struct IDXGIAdapter1Vtbl *lpVtbl;
};

#ifdef COBJMACROS

#define IDXGIAdapter1_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDXGIAdapter1_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IDXGIAdapter1_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IDXGIAdapter1_SetPrivateData(This, Name, DataSize, pData) \
    ((This)->lpVtbl->SetPrivateData(This, Name, DataSize, pData))

#define IDXGIAdapter1_SetPrivateDataInterface(This, Name, pUnknown) \
    ((This)->lpVtbl->SetPrivateDataInterface(This, Name, pUnknown))

#define IDXGIAdapter1_GetPrivateData(This, Name, pDataSize, pData) \
    ((This)->lpVtbl->GetPrivateData(This, Name, pDataSize, pData))

#define IDXGIAdapter1_GetParent(This, riid, ppParent) \
    ((This)->lpVtbl->GetParent(This, riid, ppParent))

#define IDXGIAdapter1_EnumOutputs(This, Output, ppOutput) \
    ((This)->lpVtbl->EnumOutputs(This, Output, ppOutput))

#define IDXGIAdapter1_GetDesc(This, pDesc) \
    ((This)->lpVtbl->GetDesc(This, pDesc))

#define IDXGIAdapter1_CheckInterfaceSupport(This, InterfaceName, pUMDVersion) \
    ((This)->lpVtbl->CheckInterfaceSupport(This, InterfaceName, pUMDVersion))

#define IDXGIAdapter1_GetDesc1(This, pDesc) \
    ((This)->lpVtbl->GetDesc1(This, pDesc))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDXGIAdapter1_INTERFACE_DEFINED__ */

// NOTE(marcos): declaring CreateDXGIFactory "1" since it works on UWP as well
//HRESULT WINAPI CreateDXGIFactory1(REFIID riid, _COM_Outptr_ void **ppFactory);
typedef HRESULT(WINAPI *PFN_CREATEDXGIFACORY1)(REFIID riid, _COM_Outptr_ void **ppFactory);

DEFINE_GUID(IID_IDXGIObject, 0xaec22fb8, 0x76f3, 0x4639, 0x9b, 0xe0, 0x28, 0xeb, 0x43, 0xa6, 0x7a, 0x2e);
DEFINE_GUID(IID_IDXGIDeviceSubObject, 0x3d3e0379, 0xf9de, 0x4d58, 0xbb, 0x6c, 0x18, 0xd6, 0x29, 0x92, 0xf1, 0xa6);
DEFINE_GUID(IID_IDXGIResource, 0x035f3ab4, 0x482e, 0x4e50, 0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b);
DEFINE_GUID(IID_IDXGIKeyedMutex, 0x9d8e1289, 0xd7b3, 0x465f, 0x81, 0x26, 0x25, 0x0e, 0x34, 0x9a, 0xf8, 0x5d);
DEFINE_GUID(IID_IDXGISurface, 0xcafcb56c, 0x6ac3, 0x4889, 0xbf, 0x47, 0x9e, 0x23, 0xbb, 0xd2, 0x60, 0xec);
DEFINE_GUID(IID_IDXGISurface1, 0x4AE63092, 0x6327, 0x4c1b, 0x80, 0xAE, 0xBF, 0xE1, 0x2E, 0xA3, 0x2B, 0x86);
DEFINE_GUID(IID_IDXGIAdapter, 0x2411e7e1, 0x12ac, 0x4ccf, 0xbd, 0x14, 0x97, 0x98, 0xe8, 0x53, 0x4d, 0xc0);
DEFINE_GUID(IID_IDXGIOutput, 0xae02eedb, 0xc735, 0x4690, 0x8d, 0x52, 0x5a, 0x8d, 0xc2, 0x02, 0x13, 0xaa);
DEFINE_GUID(IID_IDXGISwapChain, 0x310d36a0, 0xd2e7, 0x4c0a, 0xaa, 0x04, 0x6a, 0x9d, 0x23, 0xb8, 0x88, 0x6a);
DEFINE_GUID(IID_IDXGIFactory, 0x7b7166ec, 0x21c7, 0x44ae, 0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69);
DEFINE_GUID(IID_IDXGIDevice, 0x54ec77fa, 0x1377, 0x44e6, 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c);
DEFINE_GUID(IID_IDXGIFactory1, 0x770aae78, 0xf26f, 0x4dba, 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87);
DEFINE_GUID(IID_IDXGIAdapter1, 0x29038f61, 0x3839, 0x4626, 0x91, 0xfd, 0x08, 0x68, 0x79, 0x01, 0x1a, 0x05);
DEFINE_GUID(IID_IDXGIDevice1, 0x77db970f, 0x6276, 0x48ba, 0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c);

/* d3dcompiler.h */
typedef HRESULT(WINAPI *PFN_D3DCOMPILE)(
    _In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSize,
    _In_opt_ LPCSTR pSourceName,
    _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO *pDefines,
    _In_opt_ ID3DInclude *pInclude,
    _In_opt_ LPCSTR pEntrypoint,
    _In_ LPCSTR pTarget,
    _In_ UINT Flags1,
    _In_ UINT Flags2,
    _Out_ ID3DBlob **ppCode,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob **ppErrorMsgs);

#define D3DCOMPILE_DEBUG (1 << 0)
#define D3DCOMPILE_SKIP_VALIDATION (1 << 1)
#define D3DCOMPILE_SKIP_OPTIMIZATION (1 << 2)
#define D3DCOMPILE_PACK_MATRIX_ROW_MAJOR (1 << 3)
#define D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR (1 << 4)
#define D3DCOMPILE_PARTIAL_PRECISION (1 << 5)
#define D3DCOMPILE_FORCE_VS_SOFTWARE_NO_OPT (1 << 6)
#define D3DCOMPILE_FORCE_PS_SOFTWARE_NO_OPT (1 << 7)
#define D3DCOMPILE_NO_PRESHADER (1 << 8)
#define D3DCOMPILE_AVOID_FLOW_CONTROL (1 << 9)
#define D3DCOMPILE_PREFER_FLOW_CONTROL (1 << 10)
#define D3DCOMPILE_ENABLE_STRICTNESS (1 << 11)
#define D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY (1 << 12)
#define D3DCOMPILE_IEEE_STRICTNESS (1 << 13)
#define D3DCOMPILE_OPTIMIZATION_LEVEL0 (1 << 14)
#define D3DCOMPILE_OPTIMIZATION_LEVEL1 0
#define D3DCOMPILE_OPTIMIZATION_LEVEL2 ((1 << 14) | (1 << 15))
#define D3DCOMPILE_OPTIMIZATION_LEVEL3 (1 << 15)
#define D3DCOMPILE_RESERVED16 (1 << 16)
#define D3DCOMPILE_RESERVED17 (1 << 17)
#define D3DCOMPILE_WARNINGS_ARE_ERRORS (1 << 18)
#define D3DCOMPILE_RESOURCES_MAY_ALIAS (1 << 19)
#define D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES (1 << 20)
#define D3DCOMPILE_ALL_RESOURCES_BOUND (1 << 21)

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif /*__mini_d3d12_h__*/
