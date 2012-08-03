target triple = "x86_64-apple-darwin11.1.0"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"

define <16 x i8> @test(<16 x i8> %a) {
       %b = add <16 x i8> %a, %a
       ret <16 x i8> %b
}