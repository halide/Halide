target datalayout = "e-m:e-p:32:32-i64:64-v128:64:128-a:0:32-n32-S64"
target triple = "armv7-none-linux-gnueabi"

%struct.rs_allocation = type { i32* }

; Function Attrs: nounwind readnone
; declare <4 x float> @rsUnpackColor8888(<4 x i8>) #1

declare <4 x i8> @_Z21rsGetElementAt_uchar413rs_allocationjj([1 x i32], i32, i32)
declare zeroext i8 @_Z20rsGetElementAt_uchar13rs_allocationjjj([1 x i32], i32, i32, i32)


; Function Attrs: nounwind readnone
declare float @_Z3dotDv3_fS_(<3 x float>, <3 x float>) #1

; Function Attrs: nounwind readnone
; declare <4 x i8> @_Z17rsPackColorTo8888Dv3_f(<3 x float>) #1

declare void @_Z21rsSetElementAt_uchar413rs_allocationDv4_hjj([1 x i32], <4 x i8>, i32, i32)
declare void @_Z20rsSetElementAt_uchar13rs_allocationhjjj([1 x i32], i8 zeroext, i32, i32, i32)

; Function Attrs: nounwind
;define void @.rs.dtor() #0 {
;  tail call void @_Z13rsClearObjectP13rs_allocation(%struct.rs_allocation* @alloc_in) #0
;  tail call void @_Z13rsClearObjectP13rs_allocation(%struct.rs_allocation* @alloc_out) #0
;  ret void
;}

declare void @_Z13rsClearObjectP13rs_allocation(%struct.rs_allocation*)
