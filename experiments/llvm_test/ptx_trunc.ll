define ptx_kernel void @i64(i64 %in, i32* %o32, i16* %o16, i8* %o8) {
  trunc i64 %in to i32
  trunc i64 %in to i16
  trunc i64 %in to i8
  store i32 %1, i32* %o32
  store i16 %2, i16* %o16
  ;store i8 %3, i8* %o8 ; TODO: doesn't know how to expand truncstore i64 -> i8
  ret void
}

define ptx_kernel void @i32(i32 %in, i16* %o16, i8* %o8) {
  trunc i32 %in to i16
  trunc i32 %in to i8
  store i16 %1, i16* %o16
  store i8 %2, i8* %o8 ; TODO: doesn't know how to expand truncstore i32 -> i8
  ret void
}

define ptx_kernel void @i16(i16 %in, i8* %o8) {
  trunc i16 %in to i8
  store i8 %1, i8* %o8 ; TODO: doesn't know how to expand truncstore i16 -> i8
  ret void
}
