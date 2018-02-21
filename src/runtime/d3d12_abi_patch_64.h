#ifndef HALIDE_HALIDERUNTIMED3D12COMPUTE_ABIPATCH64_H
#define HALIDE_HALIDERUNTIMED3D12COMPUTE_ABIPATCH64_H

// D3D12 ABI patch trampolines
// (refer to 'd3d12_abi_patch_64.ll')

// Some of the D3D12 methods require trampolines to bypass ABI inconsistencies
// because clang does not generate the correct code for the calling convention
// D3D12 expects; in particular, methods that take or return structs by value,
// even if the entire struct fits in a register.
// In fact, this is true even for the Microsoft C Compiler if one attempts to
// use the C-style D3D12 API.

#ifdef __cplusplus
extern "C" {
#endif
    // These are the prototype symbols corresponding to the trampoline routines
    // implemented in assembly in 'd3d12_abi_patch_64.ll'. Compared to "actual"
    // D3D12 methods, these routines never pass or return structs by value: all
    // structs are passed by pointer, even those that get returned (trampolines
    // return nothing). The order in which arguments are passed is such that it
    // matches the expected registers of the Windows x64 calling convention.
    void Call_ID3D12DescriptorHeap_GetDesc(ID3D12DescriptorHeap* descriptorheap, D3D12_DESCRIPTOR_HEAP_DESC* desc);
    void Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* descriptorheap, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle);
    void Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* descriptorheap, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);
    void Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE* pBaseDescriptor);
    void Call_ID3D12Device_CreateConstantBufferView(ID3D12Device* device, D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptor);
    void Call_ID3D12Device_CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptor);
#ifdef __cplusplus
}
#endif



// These are simple helpers that prevent compiler optimizations around the call
// site while also ensuring the compiler generates code that will jump to the
// trampoline code with the proper arguments in the expected CPU registers:

__attribute__((optnone))
__attribute__((noinline))
D3D12_DESCRIPTOR_HEAP_DESC Call_ID3D12DescriptorHeap_GetDesc(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
#if HALIDE_D3D12_APPLY_ABI_PATCHES
    Call_ID3D12DescriptorHeap_GetDesc(descriptorheap, &desc);
#else
    desc = descriptorheap->GetDesc();
#endif
    return(desc);
}

__attribute__((optnone))
__attribute__((noinline))
D3D12_CPU_DESCRIPTOR_HANDLE Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { };
#if HALIDE_D3D12_APPLY_ABI_PATCHES
    Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptorheap, &cpuHandle);
#else
    cpuHandle = descriptorheap->GetCPUDescriptorHandleForHeapStart();
#endif
    return(cpuHandle);
}

__attribute__((optnone))
__attribute__((noinline))
D3D12_GPU_DESCRIPTOR_HANDLE Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap* descriptorheap)
{
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = { };
#if HALIDE_D3D12_APPLY_ABI_PATCHES
    Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptorheap, &gpuHandle);
#else
    gpuHandle = descriptorheap->GetGPUDescriptorHandleForHeapStart();
#endif
    return(gpuHandle);
}

__attribute__((optnone))
__attribute__((noinline))
void Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
#if HALIDE_D3D12_APPLY_ABI_PATCHES
    Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(commandList, RootParameterIndex, &BaseDescriptor);
#else
    commandList->SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
#endif
}

__attribute__((optnone))
__attribute__((noinline))
void Call_ID3D12Device_CreateConstantBufferView(ID3D12Device* device, D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
#if HALIDE_D3D12_APPLY_ABI_PATCHES
    Call_ID3D12Device_CreateConstantBufferView(device, pDesc, &DestDescriptor);
#else
    device->CreateConstantBufferView(pDesc, DestDescriptor);
#endif
}

__attribute__((optnone))
__attribute__((noinline))
void Call_ID3D12Device_CreateShaderResourceView(ID3D12Device* device, ID3D12Resource* pResource, D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
#if HALIDE_D3D12_APPLY_ABI_PATCHES
    Call_ID3D12Device_CreateShaderResourceView(device, pResource, pDesc, &DestDescriptor);
#else
    device->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
#endif
}

#endif // HALIDE_HALIDERUNTIMED3D12COMPUTE_ABIPATCH64_H
