@x = addrspace(4) global [5 x i32] zeroinitializer

define ptx_kernel void @f(i32* %in, i32* %out) {
  %1 = load i32* %in, align 4
  %2 = add nsw i32 %1, 1
  %addr = getelementptr [5 x i32] addrspace(4)* @x, i64 0, i64 0
  store i32 %2, i32* %addr
  store i32 %2, i32* %out, align 4
  ret void
}
