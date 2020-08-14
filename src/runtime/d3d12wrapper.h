#ifndef INCLUDING_FROM_D3D12WRAPPER_CPP
#define __ID3D12Device_FWD_DEFINED__
#define __ID3D12Device_INTERFACE_DEFINED__
#define __ID3D12DescriptorHeap_FWD_DEFINED__
#define __ID3D12DescriptorHeap_INTERFACE_DEFINED__
#define __ID3D12GraphicsCommandList_FWD_DEFINED__
#define __ID3D12GraphicsCommandList_INTERFACE_DEFINED__
#endif//INCLUDING_FROM_D3D12WRAPPER_CPP
#include "mini_d3d12.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace D3D12 {

extern PFN_D3D12_CREATE_DEVICE dllD3D12CreateDevice;
extern PFN_D3D12_GET_DEBUG_INTERFACE dllD3D12GetDebugInterface;
extern PFN_D3D12_SERIALIZE_ROOT_SIGNATURE dllD3D12SerializeRootSignature;
extern PFN_D3DCOMPILE dllD3DCompile;
extern PFN_CREATEDXGIFACORY1 dllCreateDXGIFactory1;

struct ID3D12Device;
struct ID3D12DescriptorHeap;
struct ID3D12GraphicsCommandList;

struct ID3D12Device
{
#ifndef INCLUDING_FROM_D3D12WRAPPER_CPP
    struct ID3D12Device* device;
#else
    ::ID3D12Device* device;
#endif//INCLUDING_FROM_D3D12WRAPPER_CPP
    ID3D12Device();
    ~ID3D12Device();

    ULONG Release(void);

/*
    UINT GetNodeCount(void);
*/
    HRESULT CreateCommandQueue(
        _In_ const D3D12_COMMAND_QUEUE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppCommandQueue);

    HRESULT CreateCommandAllocator(
        _In_ D3D12_COMMAND_LIST_TYPE type,
        REFIID riid,
        _COM_Outptr_ void **ppCommandAllocator);
/*
    HRESULT CreateGraphicsPipelineState(
        _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppPipelineState);
*/
    HRESULT CreateComputePipelineState(
        _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_ void **ppPipelineState);

    HRESULT CreateCommandList(
        _In_ UINT nodeMask,
        _In_ D3D12_COMMAND_LIST_TYPE type,
        _In_ ID3D12CommandAllocator * pCommandAllocator,
        _In_opt_ ID3D12PipelineState * pInitialState,
        _COM_Outptr_ ID3D12GraphicsCommandList *& ppCommandList);
        //REFIID riid, _COM_Outptr_ void **ppCommandList);
/*
    HRESULT CheckFeatureSupport(
        D3D12_FEATURE Feature,
        _Inout_updates_bytes_(FeatureSupportDataSize) void *pFeatureSupportData,
        UINT FeatureSupportDataSize);
*/
    HRESULT CreateDescriptorHeap(
        _In_ const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc,
        _COM_Outptr_ ID3D12DescriptorHeap *& ppvHeap);
        //REFIID riid, _COM_Outptr_ void **ppvHeap);

    UINT GetDescriptorHandleIncrementSize(
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType);

    HRESULT CreateRootSignature(
        _In_ UINT nodeMask,
        _In_reads_(blobLengthInBytes) const void *pBlobWithRootSignature,
        _In_ SIZE_T blobLengthInBytes,
        REFIID riid,
        _COM_Outptr_ void **ppvRootSignature);

    void CreateConstantBufferView(
        _In_opt_ const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    void CreateShaderResourceView(
        _In_opt_ ID3D12Resource * pResource,
        _In_opt_ const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    void CreateUnorderedAccessView(
        _In_opt_ ID3D12Resource * pResource,
        _In_opt_ ID3D12Resource * pCounterResource,
        _In_opt_ const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
/*
    void CreateRenderTargetView(
        _In_opt_ ID3D12Resource * pResource,
        _In_opt_ const D3D12_RENDER_TARGET_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
*/
/*
    void CreateDepthStencilView(
        _In_opt_ ID3D12Resource * pResource,
        _In_opt_ const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
*/
/*
    void CreateSampler(
        _In_ const D3D12_SAMPLER_DESC *pDesc,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
*/
/*
    void CopyDescriptors(
        _In_ UINT NumDestDescriptorRanges,
        _In_reads_(NumDestDescriptorRanges) const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts,
        _In_reads_opt_(NumDestDescriptorRanges) const UINT *pDestDescriptorRangeSizes,
        _In_ UINT NumSrcDescriptorRanges,
        _In_reads_(NumSrcDescriptorRanges) const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts,
        _In_reads_opt_(NumSrcDescriptorRanges) const UINT *pSrcDescriptorRangeSizes,
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
*/
/*
    void CopyDescriptorsSimple(
        _In_ UINT NumDescriptors,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
*/
/*
    D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo(
        _In_ UINT visibleMask,
        _In_ UINT numResourceDescs,
        _In_reads_(numResourceDescs) const D3D12_RESOURCE_DESC *pResourceDescs);
*/
/*
    D3D12_HEAP_PROPERTIES GetCustomHeapProperties(
        _In_ UINT nodeMask,
        D3D12_HEAP_TYPE heapType);
*/
    HRESULT CreateCommittedResource(
        _In_ const D3D12_HEAP_PROPERTIES *pHeapProperties,
        D3D12_HEAP_FLAGS HeapFlags,
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialResourceState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riidResource,
        _COM_Outptr_opt_ void **ppvResource);
/*
    HRESULT CreateHeap(
        _In_ const D3D12_HEAP_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvHeap);
*/
/*
    HRESULT CreatePlacedResource(
        _In_ ID3D12Heap * pHeap,
        UINT64 HeapOffset,
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvResource);
*/
/*
    HRESULT CreateReservedResource(
        _In_ const D3D12_RESOURCE_DESC *pDesc,
        D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE *pOptimizedClearValue,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvResource);
*/
/*
    HRESULT CreateSharedHandle(
        _In_ ID3D12DeviceChild * pObject,
        _In_opt_ const SECURITY_ATTRIBUTES *pAttributes,
        DWORD Access,
        _In_opt_ LPCWSTR Name,
        _Out_ HANDLE *pHandle);
*/
/*
    HRESULT OpenSharedHandle(
        _In_ HANDLE NTHandle,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvObj);
*/
/*
    HRESULT OpenSharedHandleByName(
        _In_ LPCWSTR Name,
        DWORD Access,
        _Out_ HANDLE * pNTHandle);
*/
/*
    HRESULT MakeResident(
        UINT NumObjects,
        _In_reads_(NumObjects) ID3D12Pageable *const *ppObjects);
*/
/*
    HRESULT Evict(
        UINT NumObjects,
        _In_reads_(NumObjects) ID3D12Pageable *const *ppObjects);
*/
    HRESULT CreateFence(
        UINT64 InitialValue,
        D3D12_FENCE_FLAGS Flags,
        REFIID riid,
        _COM_Outptr_ void **ppFence);

    HRESULT GetDeviceRemovedReason(void);
/*
    void GetCopyableFootprints(
        _In_ const D3D12_RESOURCE_DESC *pResourceDesc,
        _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
        _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
        UINT64 BaseOffset,
        _Out_writes_opt_(NumSubresources) D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts,
        _Out_writes_opt_(NumSubresources) UINT *pNumRows,
        _Out_writes_opt_(NumSubresources) UINT64 *pRowSizeInBytes,
        _Out_opt_ UINT64 *pTotalBytes);
*/
    HRESULT CreateQueryHeap(
        _In_ const D3D12_QUERY_HEAP_DESC *pDesc,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvHeap);
/*
    HRESULT SetStablePowerState(
        BOOL Enable);
*/
/*
    HRESULT CreateCommandSignature(
        _In_ const D3D12_COMMAND_SIGNATURE_DESC *pDesc,
        _In_opt_ ID3D12RootSignature *pRootSignature,
        REFIID riid,
        _COM_Outptr_opt_ void **ppvCommandSignature);
*/
/*
    void GetResourceTiling(
        _In_ ID3D12Resource * pTiledResource,
        _Out_opt_ UINT * pNumTilesForEntireResource,
        _Out_opt_ D3D12_PACKED_MIP_INFO * pPackedMipDesc,
        _Out_opt_ D3D12_TILE_SHAPE * pStandardTileShapeForNonPackedMips,
        _Inout_opt_ UINT * pNumSubresourceTilings,
        _In_ UINT FirstSubresourceTilingToGet,
        _Out_writes_(*pNumSubresourceTilings) D3D12_SUBRESOURCE_TILING * pSubresourceTilingsForNonPackedMips);
*/
/*
    LUID GetAdapterLuid(void);
*/
};

struct ID3D12DescriptorHeap
{
#ifndef INCLUDING_FROM_D3D12WRAPPER_CPP
    struct ID3D12DescriptorHeap* descheap;
#else
    ::ID3D12DescriptorHeap* descheap;
#endif//INCLUDING_FROM_D3D12WRAPPER_CPP
    ID3D12DescriptorHeap();
    ~ID3D12DescriptorHeap();

    ULONG Release(void);

    D3D12_DESCRIPTOR_HEAP_DESC GetDesc(void);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForHeapStart(void);

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForHeapStart(void);
};

struct ID3D12GraphicsCommandList
{
#ifndef INCLUDING_FROM_D3D12WRAPPER_CPP
    struct ID3D12GraphicsCommandList* gfxcmdlist;
#else
    ::ID3D12GraphicsCommandList* gfxcmdlist;
#endif//INCLUDING_FROM_D3D12WRAPPER_CPP
    ID3D12GraphicsCommandList();
    ~ID3D12GraphicsCommandList();

    ULONG Release(void);

    // TODO(marcos): ugly hack... how to abstract it more cleanly?
    ID3D12CommandList* base();

    HRESULT Close(void);
/*
    HRESULT Reset(
    _In_ ID3D12CommandAllocator * pAllocator,
    _In_opt_ ID3D12PipelineState * pInitialState) ;
*/
/*
    void ClearState(
    _In_opt_ ID3D12PipelineState * pPipelineState) ;
*/
/*
    void DrawInstanced(
    _In_ UINT VertexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartVertexLocation,
    _In_ UINT StartInstanceLocation) ;
*/
/*
    void DrawIndexedInstanced(
    _In_ UINT IndexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation,
    _In_ UINT StartInstanceLocation) ;
*/
    void Dispatch(
    _In_ UINT ThreadGroupCountX,
    _In_ UINT ThreadGroupCountY,
    _In_ UINT ThreadGroupCountZ) ;

    void CopyBufferRegion(
    _In_ ID3D12Resource * pDstBuffer,
    UINT64 DstOffset,
    _In_ ID3D12Resource * pSrcBuffer,
    UINT64 SrcOffset,
    UINT64 NumBytes) ;
/*
    void CopyTextureRegion(
    _In_ const D3D12_TEXTURE_COPY_LOCATION *pDst,
    UINT DstX,
    UINT DstY,
    UINT DstZ,
    _In_ const D3D12_TEXTURE_COPY_LOCATION *pSrc,
    _In_opt_ const D3D12_BOX *pSrcBox) ;
*/
/*
    void CopyResource(
    _In_ ID3D12Resource * pDstResource,
    _In_ ID3D12Resource * pSrcResource) ;
*/
/*
    void CopyTiles(
    _In_ ID3D12Resource * pTiledResource,
    _In_ const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
    _In_ const D3D12_TILE_REGION_SIZE *pTileRegionSize,
    _In_ ID3D12Resource *pBuffer,
    UINT64 BufferStartOffsetInBytes,
    D3D12_TILE_COPY_FLAGS Flags) ;
*/
/*
    void ResolveSubresource(
    _In_ ID3D12Resource * pDstResource,
    _In_ UINT DstSubresource,
    _In_ ID3D12Resource * pSrcResource,
    _In_ UINT SrcSubresource,
    _In_ DXGI_FORMAT Format) ;
*/
/*
    void IASetPrimitiveTopology(
    _In_ D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) ;
*/
/*
    void RSSetViewports(
    _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumViewports,
    _In_reads_(NumViewports) const D3D12_VIEWPORT *pViewports) ;
*/
/*
    void RSSetScissorRects(
    _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT *pRects) ;
*/
/*
    void OMSetBlendFactor(
    _In_opt_ const FLOAT BlendFactor[4]) ;
*/
/*
    void OMSetStencilRef(
    _In_ UINT StencilRef) ;
*/
    void SetPipelineState(
    _In_ ID3D12PipelineState * pPipelineState) ;

    void ResourceBarrier(
    _In_ UINT NumBarriers,
    _In_reads_(NumBarriers) const D3D12_RESOURCE_BARRIER *pBarriers) ;
/*
    void ExecuteBundle(
    _In_ ID3D12GraphicsCommandList * pCommandList) ;
*/
    void SetDescriptorHeaps(
    _In_ UINT NumDescriptorHeaps,
    _In_reads_(NumDescriptorHeaps) ID3D12DescriptorHeap *const *ppDescriptorHeaps) ;

    void SetComputeRootSignature(
    _In_opt_ ID3D12RootSignature * pRootSignature) ;
/*
    void SetGraphicsRootSignature(
    _In_opt_ ID3D12RootSignature * pRootSignature) ;
*/
    void SetComputeRootDescriptorTable(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) ;
/*
    void SetGraphicsRootDescriptorTable(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) ;
*/
/*
    void SetComputeRoot32BitConstant(
    _In_ UINT RootParameterIndex,
    _In_ UINT SrcData,
    _In_ UINT DestOffsetIn32BitValues) ;
*/
/*
    void SetGraphicsRoot32BitConstant(
    _In_ UINT RootParameterIndex,
    _In_ UINT SrcData,
    _In_ UINT DestOffsetIn32BitValues) ;
*/
/*
    void SetComputeRoot32BitConstants(
    _In_ UINT RootParameterIndex,
    _In_ UINT Num32BitValuesToSet,
    _In_reads_(Num32BitValuesToSet * sizeof(UINT)) const void *pSrcData,
    _In_ UINT DestOffsetIn32BitValues) ;
*/
/*
    void SetGraphicsRoot32BitConstants(
    _In_ UINT RootParameterIndex,
    _In_ UINT Num32BitValuesToSet,
    _In_reads_(Num32BitValuesToSet * sizeof(UINT)) const void *pSrcData,
    _In_ UINT DestOffsetIn32BitValues) ;
*/
/*
    void SetComputeRootConstantBufferView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) ;
*/
/*
    void SetGraphicsRootConstantBufferView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) ;
*/
/*
    void SetComputeRootShaderResourceView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) ;
*/
/*
    void SetGraphicsRootShaderResourceView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) ;
*/
/*
    void SetComputeRootUnorderedAccessView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) ;
*/
/*
    void SetGraphicsRootUnorderedAccessView(
    _In_ UINT RootParameterIndex,
    _In_ D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) ;
*/
/*
    void IASetIndexBuffer(
    _In_opt_ const D3D12_INDEX_BUFFER_VIEW *pView) ;
*/
/*
    void IASetVertexBuffers(
    _In_ UINT StartSlot,
    _In_ UINT NumViews,
    _In_reads_opt_(NumViews) const D3D12_VERTEX_BUFFER_VIEW *pViews) ;
*/
/*
    void SOSetTargets(
    _In_ UINT StartSlot,
    _In_ UINT NumViews,
    _In_reads_opt_(NumViews) const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews) ;
*/
/*
    void OMSetRenderTargets(
    _In_ UINT NumRenderTargetDescriptors,
    _In_opt_ const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
    _In_ BOOL RTsSingleHandleToDescriptorRange,
    _In_opt_ const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor) ;
*/
/*
    void ClearDepthStencilView(
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView,
    _In_ D3D12_CLEAR_FLAGS ClearFlags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil,
    _In_ UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT *pRects) ;
*/
/*
    void ClearRenderTargetView(
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
    _In_ const FLOAT ColorRGBA[4],
    _In_ UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT *pRects) ;
*/
/*
    void ClearUnorderedAccessViewUint(
    _In_ D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
    _In_ ID3D12Resource * pResource,
    _In_ const UINT Values[4],
    _In_ UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT *pRects) ;
*/
/*
    void ClearUnorderedAccessViewFloat(
    _In_ D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
    _In_ ID3D12Resource * pResource,
    _In_ const FLOAT Values[4],
    _In_ UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT *pRects) ;
*/
/*
    void DiscardResource(
    _In_ ID3D12Resource * pResource,
    _In_opt_ const D3D12_DISCARD_REGION *pRegion) ;
*/
    void BeginQuery(
    _In_ ID3D12QueryHeap * pQueryHeap,
    _In_ D3D12_QUERY_TYPE Type,
    _In_ UINT Index) ;

    void EndQuery(
    _In_ ID3D12QueryHeap * pQueryHeap,
    _In_ D3D12_QUERY_TYPE Type,
    _In_ UINT Index) ;

    void ResolveQueryData(
    _In_ ID3D12QueryHeap * pQueryHeap,
    _In_ D3D12_QUERY_TYPE Type,
    _In_ UINT StartIndex,
    _In_ UINT NumQueries,
    _In_ ID3D12Resource * pDestinationBuffer,
    _In_ UINT64 AlignedDestinationBufferOffset) ;
/*
    void SetPredication(
    _In_opt_ ID3D12Resource * pBuffer,
    _In_ UINT64 AlignedBufferOffset,
    _In_ D3D12_PREDICATION_OP Operation) ;
*/
/*
    void SetMarker(
    UINT Metadata,
    _In_reads_bytes_opt_(Size) const void *pData,
    UINT Size) ;
*/
/*
    void BeginEvent(
    UINT Metadata,
    _In_reads_bytes_opt_(Size) const void *pData,
    UINT Size) ;
*/
/*
    void EndEvent(void) ;
*/
/*
    void ExecuteIndirect(
    _In_ ID3D12CommandSignature * pCommandSignature,
    _In_ UINT MaxCommandCount,
    _In_ ID3D12Resource * pArgumentBuffer,
    _In_ UINT64 ArgumentBufferOffset,
    _In_opt_ ID3D12Resource * pCountBuffer,
    _In_ UINT64 CountBufferOffset) ;
*/
};

HRESULT D3D12CreateDevice(_In_opt_ IUnknown * pAdapter,
                          D3D_FEATURE_LEVEL MinimumFeatureLevel,
                          _COM_Outptr_opt_ ID3D12Device *& ppDevice);
                          //_In_ REFIID riid, _COM_Outptr_opt_ void ** ppDevice);

}   // end of namespace D3D12
}   // end of namespace Internal
}   // end of namespace Runtime
}   // end of namespace Halide

/*
inline
REFIID __uuidof(const Halide::Runtime::Internal::D3D12::ID3D12Device & wrapped) {
    return IID_ID3D12Device;
}

void **IID_PPV_ARGS_Helper(Halide::Runtime::Internal::D3D12::ID3D12Device **pp) {
    return (void**)pp;
}
*/

#ifndef INCLUDING_FROM_D3D12WRAPPER_CPP
//typedef Halide::Runtime::Internal::D3D12::ID3D12Device ID3D12Device;
using namespace Halide::Runtime::Internal::D3D12;
#endif//INCLUDING_FROM_D3D12WRAPPER_CPP
