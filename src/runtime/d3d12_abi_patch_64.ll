; In order to check the generated assembly code:
; $ llvm-as d3d12_abi_patch.ll
; $ llc -x86-asm-syntax=intel d3d12_abi_patch.bc -o d3d12_abi_patch.s



; anatomy of the trampoline/stub routine:
;
;    push    rbp         ; setup stack frame for this routine
;    mov     rbp, rsp    ;
;
;    sub     rsp, 32     ; allocate shadow space for internal subroutine calls
;    and     spl, -16    ; align stack at 16 byte boundary
;
;    ; whoever called this trampoline must adhere to the Windows x64 calling convention!
;    ; in particular, the following is expected:
;    ; rcx is assumed to contain 'descriptorHeap'
;    ; rdx is assumed to contain a pointer to the struct to be returned:
;    ;     struct D3D12_DESCRIPTOR_HEAP_DESC  for descriptorHeap->GetDesc()
;    ;     struct D3D12_CPU_DESCRIPTOR_HANDLE for descriptorHeap->GetCPUDescriptorHandleForHeapStart()
;    ;     struct D3D12_GPU_DESCRIPTOR_HANDLE for descriptorHeap->GetGPUDescriptorHandleForHeapStart()
;
;    lea     rax, qword ptr [rcx]    ; rax := descriptorHeap
;    mov     rax, qword ptr [rax]    ; rax := descriptorHeap->lpVtbl
;
;    call    qword ptr [rax+40h]     ; for descriptorHeap->lpVtbl->GetDesc()
;    call    qword ptr [rax+48h]     ; for descriptorHeap->lpVtbl->GetCPUDescriptorHandleForHeapStart()
;    call    qword ptr [rax+50h]     ; for descriptorHeap->lpVtbl->GetGPUDescriptorHandleForHeapStart()
;
;    leave               ; restore stack (rsp) and frame pointer (rbp)
;    ret



define weak_odr void @Call_ID3D12DescriptorHeap_GetDesc(i64* %descriptorheap, i64* %desc) naked nounwind uwtable
{
    call void asm sideeffect
    inteldialect
    "
    push    rbp
    mov     rbp, rsp
    sub     rsp,  32
    and     spl, -16
    lea     rax, qword ptr [rcx]
    mov     rax, qword ptr [rax]
    call    qword ptr [rax+40h]
    leave
    "
    ,
    "~{rsp},~{rbp},~{rax}"();
    ret void
}

define weak_odr void @Call_ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(i64* %descriptorheap, i64* %handle) naked nounwind uwtable
{
    call void asm sideeffect
    inteldialect
    "
    push    rbp
    mov     rbp, rsp
    sub     rsp,  32
    and     spl, -16
    lea     rax, qword ptr [rcx]
    mov     rax, qword ptr [rax]
    call    qword ptr [rax+48h]
    leave
    "
    ,
    "~{rsp},~{rbp},~{rax}"();
    ret void
}

define weak_odr void @Call_ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(i64* %descriptorheap, i64* %desc) naked nounwind uwtable
{
    call void asm sideeffect
    inteldialect
    "
    push    rbp
    mov     rbp, rsp
    sub     rsp,  32
    and     spl, -16
    lea     rax, qword ptr [rcx]
    mov     rax, qword ptr [rax]
    call    qword ptr [rax+50h]
    leave
    "
    ,
    "~{rsp},~{rbp},~{rax}"();
    ret void
}

define weak_odr void @Call_ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(i64* %commandList, i64 %RootParameterIndex, i64* %pBaseDescriptor) naked nounwind uwtable
{
    call void asm sideeffect
    inteldialect
    "
    push    rbp
    mov     rbp, rsp
    sub     rsp,  32
    and     spl, -16
    lea     rax, qword ptr [rcx]
    mov     rax, qword ptr [rax]
    mov     r8,  qword ptr [r8]
    call    qword ptr [rax+0F8h]
    leave
    "
    ,
    "~{rsp},~{rbp},~{rax},~{r8}"();
    ret void
}

define weak_odr void @Call_ID3D12Device_CreateConstantBufferView(i64* %device, i64* %pDesc, i64* %pDestDescriptor) naked nounwind uwtable
{
    call void asm sideeffect
    inteldialect
    "
    push    rbp
    mov     rbp, rsp
    sub     rsp,  32
    and     spl, -16
    lea     rax, qword ptr [rcx]
    mov     rax, qword ptr [rax]
    mov     r8,  qword ptr [r8]
    call    qword ptr [rax+88h]
    leave
    "
    ,
    "~{rsp},~{rbp},~{rax},~{r8}"();
    ret void
}
