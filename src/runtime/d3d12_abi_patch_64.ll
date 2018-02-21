; In order to check the generated assembly code:
; $ llvm-as d3d12_abi_patch.ll
; $ llc -x86-asm-syntax=intel d3d12_abi_patch.bc -o d3d12_abi_patch.s



; anatomy of the trampoline/stub routine:
;
;    ; all assembly routines start with some bookkeeping prologue:
;    push    rbp         ; setup stack frame for this routine
;    mov     rbp, rsp    ;
;
;    sub     rsp, 32     ; allocate shadow space for internal subroutine calls
;    and     spl, -16    ; align stack at 16 byte boundary
;
;    ; rcx is expected to contain a pointer to a D3D12 object
;    ;     (such as a descriptorHeap, a commandList or a device)
;    ; the vtable of such D3D12 object is accessed as follows:
;    lea     rax, qword ptr [rcx]    ; rax := descriptorHeap
;    mov     rax, qword ptr [rax]    ; rax := descriptorHeap->lpVtbl
;
;    ; a method in the vtable is invoked as such:
;    call    qword ptr [rax+40h]     ; for descriptorHeap->lpVtbl->GetDesc()
;    call    qword ptr [rax+48h]     ; for descriptorHeap->lpVtbl->GetCPUDescriptorHandleForHeapStart()
;    call    qword ptr [rax+50h]     ; for descriptorHeap->lpVtbl->GetGPUDescriptorHandleForHeapStart()
;    call    qword ptr [rax+0F8h]    ; for commandList->lpVtbl->SetComputeRootDescriptorTable()
;    call    qword ptr [rax+88h]     ; for device->lpVtbl->CreateConstantBufferView()
;    call    qword ptr [rax+90h]     ; for device->lpVtbl->CreateShaderResourceView()
;
;    ; all other routine parameters are expected to have been passed through
;    ; registers rdx, r8 and r9 (in this order).
;    ; note that further register data transformations might be needed prior
;    ; to calling the target vtable function.
;
;    ; all assembly routines end with the following epilogue:
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

define weak_odr void @Call_ID3D12Device_CreateShaderResourceView(i64* %device, i64* %pResource, i64* %pDesc, i64* %pDestDescriptor) naked nounwind uwtable
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
    mov     r9,  qword ptr [r9]
    call    qword ptr [rax+90h]
    leave
    "
    ,
    "~{rsp},~{rbp},~{rax},~{r8}"();
    ret void
}
