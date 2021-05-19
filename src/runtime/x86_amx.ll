define weak_odr <1024 x i8> @tileloadd64_i8(i16 %rows, i16 %colbytes, i8* %ptr, i64 %off, i64 %stride) nounwind alwaysinline readonly {
  %1 = getelementptr i8, i8* %ptr, i64 %off
  %2 = tail call x86_amx @llvm.x86.tileloadd64.internal(i16 %rows, i16 %colbytes, i8* %1, i64 %stride) nounwind readonly
  %3 = bitcast x86_amx %2 to <1024 x i8>
  ret <1024 x i8> %3
}
declare x86_amx @llvm.x86.tileloadd64.internal(i16, i16, i8*, i64)

define weak_odr <512 x i16> @tileloadd64_bf16(i16 %rows, i16 %colbytes, i8* %ptr, i64 %off, i64 %stride) nounwind alwaysinline readonly {
  %1 = getelementptr i8, i8* %ptr, i64 %off
  %2 = tail call x86_amx @llvm.x86.tileloadd64.internal(i16 %rows, i16 %colbytes, i8* %1, i64 %stride) nounwind readonly
  %3 = bitcast x86_amx %2 to <512 x i16>
  ret <512 x i16> %3
}

define weak_odr <256 x i32> @tdpbssd(i16 %rows, i16 %colbytes, i16 %acc, <256 x i32> %out, <1024 x i8> %lhs, <1024 x i8> %rhs) nounwind alwaysinline readnone {
  %1 = bitcast <1024 x i8> %lhs to x86_amx
  %2 = bitcast <1024 x i8> %rhs to x86_amx
  %3 = bitcast <256 x i32> %out to x86_amx
  %4 = tail call x86_amx @llvm.x86.tdpbssd.internal(i16 %rows, i16 %colbytes, i16 %acc, x86_amx %3, x86_amx %1, x86_amx %2) nounwind readnone
  %5 = bitcast x86_amx %4 to <256 x i32>
  ret <256 x i32> %5
}
declare x86_amx @llvm.x86.tdpbssd.internal(i16, i16, i16, x86_amx, x86_amx, x86_amx)

define weak_odr <256 x i32> @tdpbsud(i16 %rows, i16 %colbytes, i16 %acc, <256 x i32> %out, <1024 x i8> %lhs, <1024 x i8> %rhs) nounwind alwaysinline readnone {
  %1 = bitcast <1024 x i8> %lhs to x86_amx
  %2 = bitcast <1024 x i8> %rhs to x86_amx
  %3 = bitcast <256 x i32> %out to x86_amx
  %4 = tail call x86_amx @llvm.x86.tdpbsud.internal(i16 %rows, i16 %colbytes, i16 %acc, x86_amx %3, x86_amx %1, x86_amx %2) nounwind readnone
  %5 = bitcast x86_amx %4 to <256 x i32>
  ret <256 x i32> %5
}
declare x86_amx @llvm.x86.tdpbsud.internal(i16, i16, i16, x86_amx, x86_amx, x86_amx)

define weak_odr <256 x i32> @tdpbusd(i16 %rows, i16 %colbytes, i16 %acc, <256 x i32> %out, <1024 x i8> %lhs, <1024 x i8> %rhs) nounwind alwaysinline readnone {
  %1 = bitcast <1024 x i8> %lhs to x86_amx
  %2 = bitcast <1024 x i8> %rhs to x86_amx
  %3 = bitcast <256 x i32> %out to x86_amx
  %4 = tail call x86_amx @llvm.x86.tdpbusd.internal(i16 %rows, i16 %colbytes, i16 %acc, x86_amx %3, x86_amx %1, x86_amx %2) nounwind readnone
  %5 = bitcast x86_amx %4 to <256 x i32>
  ret <256 x i32> %5
}
declare x86_amx @llvm.x86.tdpbusd.internal(i16, i16, i16, x86_amx, x86_amx, x86_amx)

define weak_odr <256 x i32> @tdpbuud(i16 %rows, i16 %colbytes, i16 %acc, <256 x i32> %out, <1024 x i8> %lhs, <1024 x i8> %rhs) nounwind alwaysinline readnone {
  %1 = bitcast <1024 x i8> %lhs to x86_amx
  %2 = bitcast <1024 x i8> %rhs to x86_amx
  %3 = bitcast <256 x i32> %out to x86_amx
  %4 = tail call x86_amx @llvm.x86.tdpbuud.internal(i16 %rows, i16 %colbytes, i16 %acc, x86_amx %3, x86_amx %1, x86_amx %2) nounwind readnone
  %5 = bitcast x86_amx %4 to <256 x i32>
  ret <256 x i32> %5
}
declare x86_amx @llvm.x86.tdpbuud.internal(i16, i16, i16, x86_amx, x86_amx, x86_amx)

define weak_odr <256 x float> @tdpbf16ps(i16 %rows, i16 %colbytes, i16 %acc, <256 x float> %out, <512 x i16> %lhs, <512 x i16> %rhs) nounwind alwaysinline readnone {
  %1 = bitcast <512 x i16> %lhs to x86_amx
  %2 = bitcast <512 x i16> %rhs to x86_amx
  %3 = bitcast <256 x float> %out to x86_amx
  %4 = tail call x86_amx @llvm.x86.tdpbf16ps.internal(i16 %rows, i16 %colbytes, i16 %acc, x86_amx %3, x86_amx %1, x86_amx %2) nounwind readnone
  %5 = bitcast x86_amx %4 to <256 x float>
  ret <256 x float> %5
}
declare x86_amx @llvm.x86.tdpbf16ps.internal(i16, i16, i16, x86_amx, x86_amx, x86_amx)

define weak_odr <2 x i1> @tilestored64_i32(i16 %rows, i16 %cols, i8* %ptr, i64 %off, i64 %stride, <256 x i32> %val) nounwind alwaysinline writeonly {
  %1 = getelementptr i8, i8* %ptr, i64 %off
  %2 = bitcast <256 x i32> %val to x86_amx
  tail call void @llvm.x86.tilestored64.internal(i16 %rows, i16 %cols, i8* %1, i64 %stride, x86_amx %2) nounwind writeonly
  ret <2 x i1> zeroinitializer
}
declare void @llvm.x86.tilestored64.internal(i16, i16, i8*, i64, x86_amx)

define weak_odr <2 x i1> @tilestored64_f32(i16 %rows, i16 %cols, i8* %ptr, i64 %off, i64 %stride, <256 x float> %val) nounwind alwaysinline writeonly {
  %1 = getelementptr i8, i8* %ptr, i64 %off
  %2 = bitcast <256 x float> %val to x86_amx
  tail call void @llvm.x86.tilestored64.internal(i16 %rows, i16 %cols, i8* %1, i64 %stride, x86_amx %2) nounwind writeonly
  ret <2 x i1> zeroinitializer
}

; NB: Even though this should be readnone, that will cause LLVM to try to
; generate a single zero tile, and copy it each time it is used. However the AMX
; registers cannot be copied, so this causes compilation failures:
;     LLVM ERROR: Cannot emit physreg copy instruction
;       renamable $tmm1 = COPY renamable $tmm0
define weak_odr <256 x i32> @tilezero_i32(i16 %rows, i16 %colbytes) nounwind alwaysinline {
  %1 = tail call x86_amx @llvm.x86.tilezero.internal(i16 %rows, i16 %colbytes) nounwind
  %2 = bitcast x86_amx %1 to <256 x i32>
  ret <256 x i32> %2
}

define weak_odr <256 x float> @tilezero_f32(i16 %rows, i16 %colbytes) nounwind alwaysinline {
  %1 = tail call x86_amx @llvm.x86.tilezero.internal(i16 %rows, i16 %colbytes) nounwind
  %2 = bitcast x86_amx %1 to <256 x float>
  ret <256 x float> %2
}
declare x86_amx @llvm.x86.tilezero.internal(i16, i16)
