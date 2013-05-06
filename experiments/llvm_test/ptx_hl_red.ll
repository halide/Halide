@__nothing = addrspace(4) global i64 zeroinitializer

define ptx_kernel void @kernel(i8* %.i0, i8* %.result) {
entry:

  %memref_elem_ptr = bitcast i8* %.result to i32*
  ;%memref_elem_ptr5 = bitcast i8* %.i0 to i32*
  
  %memref_elem_ptr5 = bitcast i8* %.i0 to float*
  %mr = getelementptr float* %memref_elem_ptr5, i64 0
  %val = load float* %mr
  %idx = fptoui float %val to i32
  %memref6 = getelementptr i32* %memref_elem_ptr, i32 %idx

  ; breaks:
  ;%memref = getelementptr i32* %memref_elem_ptr5, i64 0;%9
  ;%idx = load i32* %memref
  ;%memref6 = getelementptr i32* %memref_elem_ptr, i32 %idx

  ;%idx64 = sext i32 %idx to i64
  ;%idxtop = ashr i64 %idx64, 32
  ;%idxOr = or i64 %idx64, 50000000000
  ;%memref6 = getelementptr i32* %memref_elem_ptr, 5

  ;%dump = ptrtoint i32* %memref6 to i64
  ;store i64 %dump, i64 addrspace(4)* @__nothing

  ; breaks
  ;%idx64 = sext i32 %idx to i64
  ;%base = ptrtoint i32* %memref_elem_ptr to i64
  ;%addr = add i64 %base, %idx64
  ;%memref6 = inttoptr i64 %addr to i32*

  ; works 1: upcast first and store to stack - causes sext %idx to move to a vreg before the load I think
  ;%idx64 = sext i32 %idx to i64
  ;%stack = alloca i64
  ;store i64 %idx64, i64* %stack
  ;%memref6 = getelementptr i32* %memref_elem_ptr, i64 %idx64 ;i32 %idx

  ; works 2: load a 64 bit value directly from memory and use that as the offset
  ;%memref32 = getelementptr i32* %memref_elem_ptr5, i32 0;%9
  ;%memref = bitcast i32* %memref32 to i64*
  ;%idx = load i64* %memref
  ;%memref6 = getelementptr i32* %memref_elem_ptr, i64 %idx64

  store i32 1, i32* %memref6

  ret void
}
