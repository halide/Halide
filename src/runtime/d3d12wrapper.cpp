#ifndef BITS_64
// Don't emit a message: some environments will consider this as a "warning",
// and we generally build with warnings-as-errors enabled.
// #pragma message "The Halide Direct3D 12 back-end is not yet supported on 32bit targets..."
#else  // BITS_64

#define INCLUDING_FROM_D3D12WRAPPER_CPP

#include "d3d12wrapper.h"

// HalideRuntime.h : needed for 'halide_get_library_symbol()'
// (is this going toc ause an issue with ABI linkage later?)
// (if it turns out to be problematic, we'll have to pass a
// dispatch table from the runtme module to this wrapper...)
#include "HalideRuntime.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace D3D12 {

PFN_D3D12_CREATE_DEVICE dllD3D12CreateDevice = NULL;
PFN_D3D12_GET_DEBUG_INTERFACE dllD3D12GetDebugInterface = NULL;
PFN_D3D12_SERIALIZE_ROOT_SIGNATURE dllD3D12SerializeRootSignature = NULL;
PFN_D3DCOMPILE dllD3DCompile = NULL;
PFN_CREATEDXGIFACORY1 dllCreateDXGIFactory1 = NULL;



HRESULT D3D12CreateDevice(_In_opt_ IUnknown * dxgiAdapter,
                          D3D_FEATURE_LEVEL MinimumFeatureLevel,
                          _COM_Outptr_opt_ ID3D12Device *& ppDevice) {
                          //_In_ REFIID riid, _COM_Outptr_opt_ void ** ppDevice) {
    ::ID3D12Device* true_device = NULL;
    HRESULT result = dllD3D12CreateDevice(dxgiAdapter, MinimumFeatureLevel, IID_PPV_ARGS(&true_device));
    if (FAILED(result)) {
        return result;
    }
    ppDevice = (ID3D12Device*)malloc(sizeof(ID3D12Device));
    ppDevice->device = true_device;
    return result;
}



ID3D12Device::ID3D12Device()
: device(NULL) {
 }

ID3D12Device::~ID3D12Device() {
    Release();
}

ULONG ID3D12Device::Release(void) {
    // TODO(marcos): this object was malloc'd during D3D12CreateDevice, so we
    // should remember to free it here when the ref-count reaches zero
    return device->Release();
}

HRESULT ID3D12Device::CreateCommandQueue(
    _In_ const D3D12_COMMAND_QUEUE_DESC *pDesc,
    _COM_Outptr_ ID3D12CommandQueue *& ppCommandQueue) {
    //REFIID riid, _COM_Outptr_ void **ppCommandQueue) {
        ::ID3D12CommandQueue* true_cmdqueue = NULL;
        HRESULT result = device->CreateCommandQueue(pDesc, IID_PPV_ARGS(&true_cmdqueue));
        if (FAILED(result)) {
            return result;
        }
        ppCommandQueue = (ID3D12CommandQueue*)malloc(sizeof(ID3D12CommandQueue));
        ppCommandQueue->cmdqueue = true_cmdqueue;
        return result;
    }

HRESULT ID3D12Device::CreateCommandAllocator(
    _In_ D3D12_COMMAND_LIST_TYPE type,
    REFIID riid,
    _COM_Outptr_ void **ppCommandAllocator) {
        return device->CreateCommandAllocator(type, riid, ppCommandAllocator);
    }

HRESULT ID3D12Device::CreateComputePipelineState(
    _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc,
    REFIID riid,
    _COM_Outptr_ void **ppPipelineState) {
        return device->CreateComputePipelineState(pDesc, riid, ppPipelineState);
    }

HRESULT ID3D12Device::CreateCommandList(
    _In_ UINT nodeMask,
    _In_ D3D12_COMMAND_LIST_TYPE type,
    _In_ ID3D12CommandAllocator * pCommandAllocator,
    _In_opt_ ID3D12PipelineState * pInitialState,
    _COM_Outptr_ ID3D12GraphicsCommandList *& ppCommandList) {
    //REFIID riid, _COM_Outptr_ void **ppCommandList) {
        ::ID3D12GraphicsCommandList* true_gfxcmdlist = NULL;
        HRESULT result = device->CreateCommandList(nodeMask, type, pCommandAllocator, pInitialState, IID_PPV_ARGS(&true_gfxcmdlist));
        if (FAILED(result)) {
            return result;
        }
        ppCommandList = (ID3D12GraphicsCommandList*)malloc(sizeof(ID3D12GraphicsCommandList));
        ppCommandList->gfxcmdlist = true_gfxcmdlist;
        ppCommandList->cmdlist = true_gfxcmdlist;
        return result;
    }

HRESULT ID3D12Device::CreateDescriptorHeap(
    _In_ const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc,
    _COM_Outptr_ ID3D12DescriptorHeap *& ppvHeap) {
    //REFIID riid, _COM_Outptr_ void **ppvHeap) {
        ::ID3D12DescriptorHeap* true_descheap = NULL;
        HRESULT result = device->CreateDescriptorHeap(pDescriptorHeapDesc, IID_PPV_ARGS(&true_descheap));
        if (FAILED(result)) {
            return result;
        }
        ppvHeap = (ID3D12DescriptorHeap*)malloc(sizeof(ID3D12DescriptorHeap));
        ppvHeap->descheap = true_descheap;
        return result;
    }

UINT ID3D12Device::GetDescriptorHandleIncrementSize(
    _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) {
        return device->GetDescriptorHandleIncrementSize(DescriptorHeapType);
    }

HRESULT ID3D12Device::CreateRootSignature(
    _In_ UINT nodeMask,
    _In_reads_(blobLengthInBytes) const void *pBlobWithRootSignature,
    _In_ SIZE_T blobLengthInBytes,
    REFIID riid,
    _COM_Outptr_ void **ppvRootSignature) {
        return device->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature);
    }

void ID3D12Device::CreateConstantBufferView(
    _In_opt_ const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
        return device->CreateConstantBufferView(pDesc, DestDescriptor);
    }

void ID3D12Device::CreateShaderResourceView(
    _In_opt_ ID3D12Resource * pResource,
    _In_opt_ const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
        return device->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
    }

void ID3D12Device::CreateUnorderedAccessView(
    _In_opt_ ID3D12Resource * pResource,
    _In_opt_ ID3D12Resource * pCounterResource,
    _In_opt_ const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
        return device->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
    }

HRESULT ID3D12Device::CreateCommittedResource(
    _In_ const D3D12_HEAP_PROPERTIES *pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags,
    _In_ const D3D12_RESOURCE_DESC *pDesc,
    D3D12_RESOURCE_STATES InitialResourceState,
    _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
    REFIID riidResource,
    _COM_Outptr_opt_ void **ppvResource) {
        return device->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource);
    }

HRESULT ID3D12Device::CreateFence(
    UINT64 InitialValue,
    D3D12_FENCE_FLAGS Flags,
    REFIID riid,
    _COM_Outptr_ void **ppFence) {
        return device->CreateFence(InitialValue, Flags, riid, ppFence);
    }

HRESULT ID3D12Device::GetDeviceRemovedReason(void) {
        return device->GetDeviceRemovedReason();
    }

HRESULT ID3D12Device::CreateQueryHeap(
    _In_ const D3D12_QUERY_HEAP_DESC *pDesc,
    REFIID riid,
    _COM_Outptr_opt_ void **ppvHeap) {
        return device->CreateQueryHeap(pDesc, riid, ppvHeap);
    }


ID3D12CommandQueue::ID3D12CommandQueue()
: cmdqueue(NULL) {
}

ID3D12CommandQueue::~ID3D12CommandQueue() {
    Release();
}

ULONG ID3D12CommandQueue::Release(void) {
    return cmdqueue->Release();
}

void ID3D12CommandQueue::ExecuteCommandLists(
    _In_ UINT NumCommandLists,
    _In_reads_(NumCommandLists) ID3D12CommandList *const *ppCommandLists) {
        ::ID3D12CommandList** cmdlists = (::ID3D12CommandList**)_alloca(NumCommandLists*sizeof(::ID3D12CommandList*));
        for (UINT i=0; i<NumCommandLists; ++i) {
            cmdlists[i] = ppCommandLists[i]->cmdlist;
        }
        return cmdqueue->ExecuteCommandLists(NumCommandLists, cmdlists);
    }

HRESULT ID3D12CommandQueue::Signal(
    ID3D12Fence * pFence,
    UINT64 Value) {
        return cmdqueue->Signal(pFence, Value);
    }

HRESULT ID3D12CommandQueue::GetTimestampFrequency(
    _Out_ UINT64 * pFrequency) {
        return cmdqueue->GetTimestampFrequency(pFrequency);
    }



ID3D12DescriptorHeap::ID3D12DescriptorHeap()
:  descheap(NULL) {
}

ID3D12DescriptorHeap::~ID3D12DescriptorHeap() {
    Release();
}

ULONG ID3D12DescriptorHeap::Release(void) {
    return descheap->Release();
}

D3D12_DESCRIPTOR_HEAP_DESC ID3D12DescriptorHeap::GetDesc(void) {
    return descheap->GetDesc();
}

D3D12_CPU_DESCRIPTOR_HANDLE ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart(void) {
    return descheap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE ID3D12DescriptorHeap::GetGPUDescriptorHandleForHeapStart(void) {
    return descheap->GetGPUDescriptorHandleForHeapStart();
}



ID3D12GraphicsCommandList::ID3D12GraphicsCommandList()
: gfxcmdlist(NULL) {
}

ID3D12GraphicsCommandList::~ID3D12GraphicsCommandList() {
    Release();
}

ULONG ID3D12GraphicsCommandList::Release(void) {
    return gfxcmdlist->Release();
}

HRESULT ID3D12GraphicsCommandList::Close(void) {
    return gfxcmdlist->Close();
}

void ID3D12GraphicsCommandList::Dispatch(
_In_ UINT ThreadGroupCountX,
_In_ UINT ThreadGroupCountY,
_In_ UINT ThreadGroupCountZ) {
    return gfxcmdlist->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

void ID3D12GraphicsCommandList::CopyBufferRegion(
_In_ ID3D12Resource * pDstBuffer,
UINT64 DstOffset,
_In_ ID3D12Resource * pSrcBuffer,
UINT64 SrcOffset,
UINT64 NumBytes) {
    return gfxcmdlist->CopyBufferRegion(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes);
}

void ID3D12GraphicsCommandList::SetPipelineState(
_In_ ID3D12PipelineState * pPipelineState) {
    return gfxcmdlist->SetPipelineState(pPipelineState);
}

void ID3D12GraphicsCommandList::ResourceBarrier(
_In_ UINT NumBarriers,
_In_reads_(NumBarriers) const D3D12_RESOURCE_BARRIER *pBarriers) {
    return gfxcmdlist->ResourceBarrier(NumBarriers, pBarriers);
}

void ID3D12GraphicsCommandList::SetDescriptorHeaps(
_In_ UINT NumDescriptorHeaps,
_In_reads_(NumDescriptorHeaps) ID3D12DescriptorHeap *const *ppDescriptorHeaps) {
    ::ID3D12DescriptorHeap** heaps = (::ID3D12DescriptorHeap**)_alloca(NumDescriptorHeaps*sizeof(::ID3D12DescriptorHeap*));
    for (UINT i=0; i<NumDescriptorHeaps; ++i) {
        heaps[i] = ppDescriptorHeaps[i]->descheap;
    }
    return gfxcmdlist->SetDescriptorHeaps(NumDescriptorHeaps, heaps);
}

void ID3D12GraphicsCommandList::SetComputeRootSignature(
_In_opt_ ID3D12RootSignature * pRootSignature) {
    return gfxcmdlist->SetComputeRootSignature(pRootSignature);
}

void ID3D12GraphicsCommandList::SetComputeRootDescriptorTable(
_In_ UINT RootParameterIndex,
_In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) {
    return gfxcmdlist->SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
}

void ID3D12GraphicsCommandList::BeginQuery(
_In_ ID3D12QueryHeap * pQueryHeap,
_In_ D3D12_QUERY_TYPE Type,
_In_ UINT Index) {
    return gfxcmdlist->BeginQuery(pQueryHeap, Type, Index);
}

void ID3D12GraphicsCommandList::EndQuery(
_In_ ID3D12QueryHeap * pQueryHeap,
_In_ D3D12_QUERY_TYPE Type,
_In_ UINT Index) {
    return gfxcmdlist->EndQuery(pQueryHeap, Type, Index);
}

void ID3D12GraphicsCommandList::ResolveQueryData(
_In_ ID3D12QueryHeap * pQueryHeap,
_In_ D3D12_QUERY_TYPE Type,
_In_ UINT StartIndex,
_In_ UINT NumQueries,
_In_ ID3D12Resource * pDestinationBuffer,
_In_ UINT64 AlignedDestinationBufferOffset) {
    return gfxcmdlist->ResolveQueryData(pQueryHeap, Type, StartIndex, NumQueries, pDestinationBuffer, AlignedDestinationBufferOffset);
}



}   // end of namespace D3D12
}   // end of namespace Internal
}   // end of namespace Runtime
}   // end of namespace Halide

#endif  // BITS_64
