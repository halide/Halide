define weak_odr <16 x i8>  @paddusbx16(<16 x i8> %a0, <16 x i8> %a1) nounwind alwaysinline {
  %1 = add <16 x i8> %a0, %a1
  %2 = icmp ugt <16 x i8> %a0, %1
  %3 = select <16 x i1> %2, <16 x i8> <i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1>, <16 x i8> %1
  ret <16 x i8> %3
}

define weak_odr <8 x i16> @padduswx8(<8 x i16> %a0, <8 x i16> %a1) nounwind alwaysinline {
  %1 = add <8 x i16> %a0, %a1
  %2 = icmp ugt <8 x i16> %a0, %1
  %3 = select <8 x i1> %2, <8 x i16> <i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1>, <8 x i16> %1
  ret <8 x i16> %3
}

define weak_odr <16 x i8> @psubusbx16(<16 x i8> %a0, <16 x i8> %a1) nounwind alwaysinline {
  %1 = icmp ugt <16 x i8> %a0, %a1
  %2 = select <16 x i1> %1, <16 x i8> %a0, <16 x i8> %a1
  %3 = sub <16 x i8> %2, %a1
  ret <16 x i8> %3
}

define weak_odr <8 x i16> @psubuswx8(<8 x i16> %a0, <8 x i16> %a1) nounwind alwaysinline {
  %1 = icmp ugt <8 x i16> %a0, %a1
  %2 = select <8 x i1> %1, <8 x i16> %a0, <8 x i16> %a1
  %3 = sub <8 x i16> %2, %a1
  ret <8 x i16> %3
}

declare <16 x i8> @llvm.x86.sse2.packsswb.128(<8 x i16>, <8 x i16>)
declare <16 x i8> @llvm.x86.sse2.packuswb.128(<8 x i16>, <8 x i16>)
declare <8 x i16> @llvm.x86.sse2.packssdw.128(<4 x i32>, <4 x i32>)

define weak_odr <16 x i8>  @packsswbx16(<16 x i16> %arg) nounwind alwaysinline {
  %1 = shufflevector <16 x i16> %arg, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = shufflevector <16 x i16> %arg, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %3 = tail call <16 x i8> @llvm.x86.sse2.packsswb.128(<8 x i16> %1, <8 x i16> %2)
  ret <16 x i8> %3
}

define weak_odr <16 x i8>  @packuswbx16(<16 x i16> %arg) nounwind alwaysinline {
  %1 = shufflevector <16 x i16> %arg, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = shufflevector <16 x i16> %arg, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %3 = tail call <16 x i8> @llvm.x86.sse2.packuswb.128(<8 x i16> %1, <8 x i16> %2)
  ret <16 x i8> %3
}

define weak_odr <8 x i16>  @packssdwx8(<8 x i32> %arg) nounwind alwaysinline {
  %1 = shufflevector <8 x i32> %arg, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <8 x i32> %arg, <8 x i32> undef, <4 x i32> < i32 4, i32 5, i32 6, i32 7>
  %3 = tail call <8 x i16> @llvm.x86.sse2.packssdw.128(<4 x i32> %1, <4 x i32> %2)
  ret <8 x i16> %3
}

define weak_odr <4 x float> @sqrt_f32x4(<4 x float> %x) nounwind uwtable readnone alwaysinline {
  %1 = tail call <4 x float> @llvm.x86.sse.sqrt.ps(<4 x float> %x) nounwind
  ret <4 x float> %1
}

declare <2 x double> @llvm.x86.sse2.sqrt.pd(<2 x double>) nounwind readnone

define weak_odr <2 x double> @sqrt_f64x2(<2 x double> %x) nounwind uwtable readnone alwaysinline {
  %1 = tail call <2 x double> @llvm.x86.sse2.sqrt.pd(<2 x double> %x) nounwind
  ret <2 x double> %1
}

declare <4 x float> @llvm.x86.sse.sqrt.ps(<4 x float>) nounwind readnone

define weak_odr <4 x float> @abs_f32x4(<4 x float> %x) nounwind uwtable readnone alwaysinline {
  %arg = bitcast <4 x float> %x to <4 x i32>
  %mask = lshr <4 x i32> <i32 -1, i32 -1, i32 -1, i32 -1>, <i32 1, i32 1, i32 1, i32 1>
  %masked = and <4 x i32> %arg, %mask
  %result = bitcast <4 x i32> %masked to <4 x float>
  ret <4 x float> %result
}

define weak_odr <2 x double> @abs_f64x2(<2 x double> %x) nounwind uwtable readnone alwaysinline {
  %arg = bitcast <2 x double> %x to <2 x i64>
  %mask = lshr <2 x i64> <i64 -1, i64 -1>, <i64 1, i64 1>
  %masked = and <2 x i64> %arg, %mask
  %result = bitcast <2 x i64> %masked to <2 x double>
  ret <2 x double> %result
}

declare <4 x float> @llvm.x86.sse.rcp.ss(<4 x float>) nounwind readnone
define weak_odr float @fast_inverse_f32(float %x) nounwind uwtable readnone alwaysinline {
  %vec = insertelement <4 x float> undef, float %x, i32 0
  %approx = tail call <4 x float> @llvm.x86.sse.rcp.ss(<4 x float> %vec)
  %result = extractelement <4 x float> %approx, i32 0
  ret float %result
}

declare <4 x float> @llvm.x86.sse.rcp.ps(<4 x float>) nounwind readnone

define weak_odr <4 x float> @fast_inverse_f32x4(<4 x float> %x) nounwind uwtable readnone alwaysinline {
  %approx = tail call <4 x float> @llvm.x86.sse.rcp.ps(<4 x float> %x);
  ret <4 x float> %approx
}

declare <4 x float> @llvm.x86.sse.rsqrt.ss(<4 x float>) nounwind readnone

define weak_odr float @fast_inverse_sqrt_f32(float %x) nounwind uwtable readnone alwaysinline {
  %vec = insertelement <4 x float> undef, float %x, i32 0
  %approx = tail call <4 x float> @llvm.x86.sse.rsqrt.ss(<4 x float> %vec)
  %result = extractelement <4 x float> %approx, i32 0
  ret float %result
}

declare <4 x float> @llvm.x86.sse.rsqrt.ps(<4 x float>) nounwind readnone

define weak_odr <4 x float> @fast_inverse_sqrt_f32x4(<4 x float> %x) nounwind uwtable readnone alwaysinline {
  %approx = tail call <4 x float> @llvm.x86.sse.rsqrt.ps(<4 x float> %x);
  ret <4 x float> %approx
}

; An admittedly ugly but functional version: "info" is an in-out parameter,
; with the selector being passed as info[0] and the ecx value as info[1] (other fields ignored on input),
; and info[0...3] as output. This is regrettable but solves two issues:
; -- A saner API can easily be written that spills to/from the stack internally,
; but it's not feasible to write one that is compatible across all LLVM versions we
; support. (TODO: now that LLVM3.6 is no longer supported, this could change.)
; -- A version without stack spills tends to confuse the x86-32 code generator
; and cause it to fail via running out of registers.
define weak_odr void @x86_cpuid_halide(i32* %info) nounwind uwtable {
  call void asm sideeffect inteldialect "xchg ebx, esi\0A\09mov eax, dword ptr $$0 $0\0A\09mov ecx, dword ptr $$4 $0\0A\09cpuid\0A\09mov dword ptr $$0 $0, eax\0A\09mov dword ptr $$4 $0, ebx\0A\09mov dword ptr $$8 $0, ecx\0A\09mov dword ptr $$12 $0, edx\0A\09xchg ebx, esi", "=*m,~{eax},~{ebx},~{ecx},~{edx},~{esi},~{dirflag},~{fpsr},~{flags}"(i32* elementtype(i32) %info)
  ret void
}

; Same, but ensure that we save/restore the full 64 bits of rbx/rsi.
define weak_odr void @x64_cpuid_halide(i32* %info) nounwind uwtable {
  call void asm sideeffect inteldialect "xchg rbx, rsi\0A\09mov eax, dword ptr $$0 $0\0A\09mov ecx, dword ptr $$4 $0\0A\09cpuid\0A\09mov dword ptr $$0 $0, eax\0A\09mov dword ptr $$4 $0, ebx\0A\09mov dword ptr $$8 $0, ecx\0A\09mov dword ptr $$12 $0, edx\0A\09xchg rbx, rsi", "=*m,~{eax},~{ebx},~{ecx},~{edx},~{esi},~{dirflag},~{fpsr},~{flags}"(i32* elementtype(i32) %info)
  ret void
}
