; ModuleID = 'bigarray_wrap.c'
;target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
;target triple = "x86_64-apple-darwin10.0.0"

define void @ptr_test(i8** %inval, i8** %outval) {
  %1 = getelementptr i8** %inval, i32 1
  %two = load i8** %1
  %2 = getelementptr i8* %two, i32 0
  %3 = bitcast i8* %2 to i64*
  %4 = getelementptr i8** %outval, i32 1
  %five = load i8** %4
  %5 = getelementptr i8* %five, i32 0
  %6 = bitcast i8* %5 to i64*
  %7 = load i64* %3
  store i64 %7, i64* %6
  ret void
}
