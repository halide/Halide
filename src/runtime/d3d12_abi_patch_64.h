#ifndef HALIDE_HALIDERUNTIMED3D12COMPUTE_ABIPATCH64_H
#define HALIDE_HALIDERUNTIMED3D12COMPUTE_ABIPATCH64_H

// D3D12 ABI patch trampolines
// (refer to 'd3d12_abi_patch_64.ll')

#ifdef __cplusplus
extern "C" {
#endif
    void Call_ID3D12DescriptorHeap_GetDesc(int64_t* descriptorheap, int64_t* desc);
    void Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(int64_t* descriptorheap, int64_t* cpuHandle);
    void Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(int64_t* descriptorheap, int64_t* gpuHandle);
    void Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE* pBaseDescriptor);
    void Call_ID3D12Device_CreateConstantBufferView(ID3D12Device* device, D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptor);
#ifdef __cplusplus
}
#endif

__attribute__((optnone))
__attribute__((noinline))
D3D12_DESCRIPTOR_HEAP_DESC Call_ID3D12DescriptorHeap_GetDesc(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    Call_ID3D12DescriptorHeap_GetDesc( (int64_t*)descriptorheap, (int64_t*)&desc );
    return(desc);
}

__attribute__((optnone))
__attribute__((noinline))
D3D12_CPU_DESCRIPTOR_HANDLE Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { };
    Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( (int64_t*)descriptorheap, (int64_t*)&cpuHandle );
    return(cpuHandle);
}

__attribute__((optnone))
__attribute__((noinline))
D3D12_GPU_DESCRIPTOR_HANDLE Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { };
    Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart( (int64_t*)descriptorheap, (int64_t*)&gpuHandle );
    return(gpuHandle);
}

__attribute__((optnone))
__attribute__((noinline))
void Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(commandList, RootParameterIndex, &BaseDescriptor);
}

__attribute__((optnone))
__attribute__((noinline))
void Call_ID3D12Device_CreateConstantBufferView(ID3D12Device* device, D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    Call_ID3D12Device_CreateConstantBufferView(device, pDesc, &DestDescriptor);
}

#endif // HALIDE_HALIDERUNTIMED3D12COMPUTE_ABIPATCH64_H
