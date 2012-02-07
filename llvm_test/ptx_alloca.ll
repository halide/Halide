; works: static alloca becomes thread-local memory
define ptx_kernel void @f(i32* %in, i32* %out) {
  %1 = alloca i32, align 4
  store i32 5, i32* %1, align 4
  %2 = load i32* %1, align 4
  %3 = add nsw i32 %2, 1
  ret void
}

; doesn't work: dynamic size
; define ptx_kernel void @f(i32 %size, i32* %in, i32* %out) {
;   %1 = alloca i32, i32 %size, align 4
;   store i32 5, i32* %1, align 4
;   %2 = load i32* %1, align 4
;   %3 = add nsw i32 %2, 1
;   ret void
; }
