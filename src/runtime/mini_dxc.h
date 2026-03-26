#ifndef __mini_dxc_h__
#define __mini_dxc_h__

// Minimal DXC (DirectX Shader Compiler) interface declarations for use in the
// Halide D3D12Compute runtime. Only the interfaces and types needed to compile
// compute shaders via dxcompiler.dll are included here.
//
// These declarations are compatible with the real DXC SDK headers and follow
// the same dual C++/C-style interface pattern as mini_d3d12.h.

#include "mini_d3d12.h"

// SAL annotations not defined by mini_d3d12.h that appear in DXC headers.
#ifndef _COM_Outptr_result_maybenull_
#define _COM_Outptr_result_maybenull_
#endif

#ifndef _In_opt_count_
#define _In_opt_count_(n)
#endif

// REFCLSID is the same type as REFIID (both are aliases for const GUID&/const GUID*).
#ifndef REFCLSID
#define REFCLSID REFIID
#endif

// ---- GUIDs ----

// CLSID_DxcCompiler: {73E22D93-E6CE-47F3-B5BF-F0664F39C1B0}
DEFINE_GUID(CLSID_DxcCompiler,
            0x73e22d93, 0xe6ce, 0x47f3, 0xb5, 0xbf, 0xf0, 0x66, 0x4f, 0x39, 0xc1, 0xb0);

// IID_IDxcBlob: {8BA5FB08-5195-40E2-AC58-0D989C3A0102}
DEFINE_GUID(IID_IDxcBlob,
            0x8ba5fb08, 0x5195, 0x40e2, 0xac, 0x58, 0x0d, 0x98, 0x9c, 0x3a, 0x01, 0x02);

// IID_IDxcBlobEncoding: {7241D424-2646-4191-97C0-98E96E42FC68}
DEFINE_GUID(IID_IDxcBlobEncoding,
            0x7241d424, 0x2646, 0x4191, 0x97, 0xc0, 0x98, 0xe9, 0x6e, 0x42, 0xfc, 0x68);

// IID_IDxcBlobUtf8: {3DA636C9-BA71-4024-A301-30CBF125305B}
DEFINE_GUID(IID_IDxcBlobUtf8,
            0x3da636c9, 0xba71, 0x4024, 0xa3, 0x01, 0x30, 0xcb, 0xf1, 0x25, 0x30, 0x5b);

// IID_IDxcResult: {58346CDA-DDE7-4497-9461-6F87AF5E0659}
DEFINE_GUID(IID_IDxcResult,
            0x58346cda, 0xdde7, 0x4497, 0x94, 0x61, 0x6f, 0x87, 0xaf, 0x5e, 0x06, 0x59);

// IID_IDxcCompiler3: {228B4687-5A6A-4730-900C-9702B2203F54}
DEFINE_GUID(IID_IDxcCompiler3,
            0x228b4687, 0x5a6a, 0x4730, 0x90, 0x0c, 0x97, 0x02, 0xb2, 0x20, 0x3f, 0x54);

// IDxcCompiler (v1) fallback: stable since DXC 1.0, present in all DXC builds.
// IID_IDxcCompiler: {8C210BF3-011F-4422-8D70-6F9ACB8DB617}
DEFINE_GUID(IID_IDxcCompiler,
            0x8c210bf3, 0x011f, 0x4422, 0x8d, 0x70, 0x6f, 0x9a, 0xcb, 0x8d, 0xb6, 0x17);

// IDxcLibrary: needed to create source blobs for IDxcCompiler v1.
// CLSID_DxcLibrary: {6245D6AF-66E0-48FD-80B4-4D271796748C}
DEFINE_GUID(CLSID_DxcLibrary,
            0x6245d6af, 0x66e0, 0x48fd, 0x80, 0xb4, 0x4d, 0x27, 0x17, 0x96, 0x74, 0x8c);
// IID_IDxcLibrary: {E5204DC7-D18C-4C3C-BDFB-851673980FE7}
DEFINE_GUID(IID_IDxcLibrary,
            0xe5204dc7, 0xd18c, 0x4c3c, 0xbd, 0xfb, 0x85, 0x16, 0x73, 0x98, 0x0f, 0xe7);

// ---- DxcBuffer ----
// Describes a source buffer or compiled object for DXC operations.
typedef struct DxcBuffer {
    LPCVOID Ptr;
    SIZE_T Size;
    UINT Encoding;  // Use DXC_CP_UTF8 for UTF-8 HLSL source
} DxcBuffer;

#define DXC_CP_UTF8 65001

// ---- Interface forward declarations ----
#if defined(__cplusplus) && !defined(CINTERFACE)
typedef interface IDxcBlob IDxcBlob;
typedef interface IDxcBlobEncoding IDxcBlobEncoding;
typedef interface IDxcBlobUtf8 IDxcBlobUtf8;
typedef interface IDxcResult IDxcResult;
typedef interface IDxcCompiler IDxcCompiler;
typedef interface IDxcCompiler3 IDxcCompiler3;
typedef interface IDxcLibrary IDxcLibrary;
typedef interface IDxcIncludeHandler IDxcIncludeHandler;
#else
typedef struct IDxcBlob IDxcBlob;
typedef struct IDxcBlobEncoding IDxcBlobEncoding;
typedef struct IDxcBlobUtf8 IDxcBlobUtf8;
typedef struct IDxcResult IDxcResult;
typedef struct IDxcCompiler IDxcCompiler;
typedef struct IDxcCompiler3 IDxcCompiler3;
typedef struct IDxcLibrary IDxcLibrary;
typedef struct IDxcIncludeHandler IDxcIncludeHandler;
#endif

// ---- IDxcBlob ----
#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("8BA5FB08-5195-40E2-AC58-0D989C3A0102")
IDxcBlob : public IUnknown {
public:
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};

#else /* C style */

typedef struct IDxcBlobVtbl {
    BEGIN_INTERFACE
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDxcBlob *This, REFIID riid, _COM_Outptr_ void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDxcBlob *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDxcBlob *This);
    LPVOID(STDMETHODCALLTYPE *GetBufferPointer)(IDxcBlob *This);
    SIZE_T(STDMETHODCALLTYPE *GetBufferSize)(IDxcBlob *This);
    END_INTERFACE
} IDxcBlobVtbl;

struct IDxcBlob {
    CONST_VTBL IDxcBlobVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IDxcBlob_QueryInterface(This, riid, ppv) ((This)->lpVtbl->QueryInterface(This, riid, ppv))
#define IDxcBlob_AddRef(This) ((This)->lpVtbl->AddRef(This))
#define IDxcBlob_Release(This) ((This)->lpVtbl->Release(This))
#define IDxcBlob_GetBufferPointer(This) ((This)->lpVtbl->GetBufferPointer(This))
#define IDxcBlob_GetBufferSize(This) ((This)->lpVtbl->GetBufferSize(This))
#endif /* COBJMACROS */

#endif /* C style */

// ---- IDxcBlobEncoding (extends IDxcBlob) ----
#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("7241D424-2646-4191-97C0-98E96E42FC68")
IDxcBlobEncoding : public IDxcBlob {
public:
    virtual HRESULT STDMETHODCALLTYPE GetEncoding(_Out_ BOOL * pKnown, _Out_ UINT32 * pCodePage) = 0;
};

#else /* C style */

typedef struct IDxcBlobEncodingVtbl {
    BEGIN_INTERFACE
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDxcBlobEncoding *This, REFIID riid, _COM_Outptr_ void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDxcBlobEncoding *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDxcBlobEncoding *This);
    LPVOID(STDMETHODCALLTYPE *GetBufferPointer)(IDxcBlobEncoding *This);
    SIZE_T(STDMETHODCALLTYPE *GetBufferSize)(IDxcBlobEncoding *This);
    HRESULT(STDMETHODCALLTYPE *GetEncoding)(IDxcBlobEncoding *This, _Out_ BOOL *pKnown, _Out_ UINT32 *pCodePage);
    END_INTERFACE
} IDxcBlobEncodingVtbl;

struct IDxcBlobEncoding {
    CONST_VTBL IDxcBlobEncodingVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IDxcBlobEncoding_QueryInterface(This, riid, ppv) ((This)->lpVtbl->QueryInterface(This, riid, ppv))
#define IDxcBlobEncoding_AddRef(This) ((This)->lpVtbl->AddRef(This))
#define IDxcBlobEncoding_Release(This) ((This)->lpVtbl->Release(This))
#define IDxcBlobEncoding_GetBufferPointer(This) ((This)->lpVtbl->GetBufferPointer(This))
#define IDxcBlobEncoding_GetBufferSize(This) ((This)->lpVtbl->GetBufferSize(This))
#define IDxcBlobEncoding_GetEncoding(This, pKnown, pCP) ((This)->lpVtbl->GetEncoding(This, pKnown, pCP))
#endif /* COBJMACROS */

#endif /* C style */

// ---- IDxcBlobUtf8 (extends IDxcBlobEncoding) ----
#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("3DA636C9-BA71-4024-A301-30CBE17E3BF0")
IDxcBlobUtf8 : public IDxcBlobEncoding {
public:
    virtual LPCSTR STDMETHODCALLTYPE GetStringPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetStringLength() = 0;
};

#else /* C style */

typedef struct IDxcBlobUtf8Vtbl {
    BEGIN_INTERFACE
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDxcBlobUtf8 *This, REFIID riid, _COM_Outptr_ void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDxcBlobUtf8 *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDxcBlobUtf8 *This);
    LPVOID(STDMETHODCALLTYPE *GetBufferPointer)(IDxcBlobUtf8 *This);
    SIZE_T(STDMETHODCALLTYPE *GetBufferSize)(IDxcBlobUtf8 *This);
    HRESULT(STDMETHODCALLTYPE *GetEncoding)(IDxcBlobUtf8 *This, _Out_ BOOL *pKnown, _Out_ UINT32 *pCodePage);
    LPCSTR(STDMETHODCALLTYPE *GetStringPointer)(IDxcBlobUtf8 *This);
    SIZE_T(STDMETHODCALLTYPE *GetStringLength)(IDxcBlobUtf8 *This);
    END_INTERFACE
} IDxcBlobUtf8Vtbl;

struct IDxcBlobUtf8 {
    CONST_VTBL IDxcBlobUtf8Vtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IDxcBlobUtf8_QueryInterface(This, riid, ppv) ((This)->lpVtbl->QueryInterface(This, riid, ppv))
#define IDxcBlobUtf8_AddRef(This) ((This)->lpVtbl->AddRef(This))
#define IDxcBlobUtf8_Release(This) ((This)->lpVtbl->Release(This))
#define IDxcBlobUtf8_GetBufferPointer(This) ((This)->lpVtbl->GetBufferPointer(This))
#define IDxcBlobUtf8_GetBufferSize(This) ((This)->lpVtbl->GetBufferSize(This))
#define IDxcBlobUtf8_GetStringPointer(This) ((This)->lpVtbl->GetStringPointer(This))
#define IDxcBlobUtf8_GetStringLength(This) ((This)->lpVtbl->GetStringLength(This))
#endif /* COBJMACROS */

#endif /* C style */

// ---- IDxcResult (extends IUnknown) ----
// Contains the output of a DXC compilation.
#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("58346CDA-DDE7-4497-9461-6F87AF5E0659")
IDxcResult : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetStatus(_Out_ HRESULT * pStatus) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResult(_COM_Outptr_ IDxcBlob * *ppResult) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetErrorBuffer(_COM_Outptr_ IDxcBlobEncoding * *ppErrors) = 0;
};

#else /* C style */

typedef struct IDxcResultVtbl {
    BEGIN_INTERFACE
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDxcResult *This, REFIID riid, _COM_Outptr_ void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDxcResult *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDxcResult *This);
    HRESULT(STDMETHODCALLTYPE *GetStatus)(IDxcResult *This, _Out_ HRESULT *pStatus);
    HRESULT(STDMETHODCALLTYPE *GetResult)(IDxcResult *This, _COM_Outptr_ IDxcBlob **ppResult);
    HRESULT(STDMETHODCALLTYPE *GetErrorBuffer)(IDxcResult *This, _COM_Outptr_ IDxcBlobEncoding **ppErrors);
    END_INTERFACE
} IDxcResultVtbl;

struct IDxcResult {
    CONST_VTBL IDxcResultVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IDxcResult_QueryInterface(This, riid, ppv) ((This)->lpVtbl->QueryInterface(This, riid, ppv))
#define IDxcResult_AddRef(This) ((This)->lpVtbl->AddRef(This))
#define IDxcResult_Release(This) ((This)->lpVtbl->Release(This))
#define IDxcResult_GetStatus(This, pStatus) ((This)->lpVtbl->GetStatus(This, pStatus))
#define IDxcResult_GetResult(This, ppResult) ((This)->lpVtbl->GetResult(This, ppResult))
#define IDxcResult_GetErrorBuffer(This, ppErrors) ((This)->lpVtbl->GetErrorBuffer(This, ppErrors))
#endif /* COBJMACROS */

#endif /* C style */

// ---- IDxcIncludeHandler ----
// Only forward-declared; we always pass nullptr.
#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("7F61FC7D-950D-467F-B3E3-3C02FB49187C")
IDxcIncludeHandler : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE LoadSource(
        _In_z_ LPCWSTR pFilename,
        _COM_Outptr_result_maybenull_ IDxcBlob * *ppIncludeSource) = 0;
};

#else /* C style */

typedef struct IDxcIncludeHandlerVtbl {
    BEGIN_INTERFACE
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDxcIncludeHandler *This, REFIID riid, _COM_Outptr_ void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDxcIncludeHandler *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDxcIncludeHandler *This);
    HRESULT(STDMETHODCALLTYPE *LoadSource)(IDxcIncludeHandler *This, _In_z_ LPCWSTR pFilename,
                                           _COM_Outptr_result_maybenull_ IDxcBlob **ppIncludeSource);
    END_INTERFACE
} IDxcIncludeHandlerVtbl;

struct IDxcIncludeHandler {
    CONST_VTBL IDxcIncludeHandlerVtbl *lpVtbl;
};

#endif /* C style */

// ---- IDxcLibrary (extends IUnknown) ----
// Used to create source blobs for IDxcCompiler v1.
// Only CreateBlobWithEncodingFromPinned is declared here; the earlier slots
// (SetMalloc, CreateBlobFromBlob, CreateBlobFromFile) are placeholders that
// must match the real vtable order.
#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("E5204DC7-D18C-4C3C-BDFB-851673980FE7")
IDxcLibrary : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetMalloc(void *pMalloc) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBlobFromBlob(IDxcBlob * pBlob, UINT32 offset, UINT32 length,
                                                         _COM_Outptr_ IDxcBlob * *ppResult) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBlobFromFile(LPCWSTR pFileName, UINT32 * pCodePage,
                                                         _COM_Outptr_ IDxcBlobEncoding * *pBlobEncoding) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBlobWithEncodingFromPinned(
        LPCVOID pText, UINT32 size, UINT32 codePage,
        _COM_Outptr_ IDxcBlobEncoding * *pBlobEncoding) = 0;
};

#endif /* C++ only — C-style vtable omitted (not used) */

// ---- IDxcCompiler (v1, extends IUnknown) ----
// The original DXC compiler interface; present in all DXC builds including the
// Windows 11 inbox version. Used as fallback when IDxcCompiler3 is unavailable.
// Only Compile() is declared here; Preprocess/Disassemble placeholders omitted
// because we never call them.
#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("8C210BF3-011F-4422-8D70-6F9ACB374E6F")
IDxcCompiler : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Compile(
        IDxcBlob * pSource,
        _In_opt_ LPCWSTR pSourceName,
        LPCWSTR pEntryPoint,
        LPCWSTR pTargetProfile,
        _In_opt_count_(argCount) LPCWSTR * pArguments,
        UINT32 argCount,
        _In_opt_ const void *pDefines,
        UINT32 defineCount,
        _In_opt_ IDxcIncludeHandler *pIncludeHandler,
        _COM_Outptr_ IDxcResult **ppResult) = 0;
};

#endif /* C++ only */

// ---- IDxcCompiler3 (extends IUnknown) ----
// Primary interface for compiling HLSL source to DXIL.
#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("228B4D35-AAB1-4424-A100-30023112E96D")
IDxcCompiler3 : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Compile(
        _In_ const DxcBuffer *pSource,
        _In_opt_count_(argCount) LPCWSTR *pArguments,
        _In_ UINT32 argCount,
        _In_opt_ IDxcIncludeHandler *pIncludeHandler,
        REFIID riid,
        _COM_Outptr_ LPVOID *ppResult) = 0;
    virtual HRESULT STDMETHODCALLTYPE Disassemble(
        _In_ const DxcBuffer *pObject,
        REFIID riid,
        _COM_Outptr_ LPVOID *ppResult) = 0;
};

#else /* C style */

typedef struct IDxcCompiler3Vtbl {
    BEGIN_INTERFACE
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDxcCompiler3 *This, REFIID riid, _COM_Outptr_ void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDxcCompiler3 *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDxcCompiler3 *This);
    HRESULT(STDMETHODCALLTYPE *Compile)(IDxcCompiler3 *This,
                                        _In_ const DxcBuffer *pSource,
                                        _In_opt_count_(argCount) LPCWSTR *pArguments,
                                        _In_ UINT32 argCount,
                                        _In_opt_ IDxcIncludeHandler *pIncludeHandler,
                                        REFIID riid,
                                        _COM_Outptr_ LPVOID *ppResult);
    HRESULT(STDMETHODCALLTYPE *Disassemble)(IDxcCompiler3 *This,
                                            _In_ const DxcBuffer *pObject,
                                            REFIID riid,
                                            _COM_Outptr_ LPVOID *ppResult);
    END_INTERFACE
} IDxcCompiler3Vtbl;

struct IDxcCompiler3 {
    CONST_VTBL IDxcCompiler3Vtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IDxcCompiler3_QueryInterface(This, riid, ppv) ((This)->lpVtbl->QueryInterface(This, riid, ppv))
#define IDxcCompiler3_AddRef(This) ((This)->lpVtbl->AddRef(This))
#define IDxcCompiler3_Release(This) ((This)->lpVtbl->Release(This))
#define IDxcCompiler3_Compile(This, pSrc, pArgs, nArgs, pInc, riid, ppResult) \
    ((This)->lpVtbl->Compile(This, pSrc, pArgs, nArgs, pInc, riid, ppResult))
#endif /* COBJMACROS */

#endif /* C style */

// ---- DxcCreateInstance function pointer type ----
typedef HRESULT(WINAPI *PFN_DXC_CREATE_INSTANCE)(REFCLSID rclsid, REFIID riid, LPVOID *ppv);

#endif  // __mini_dxc_h__
