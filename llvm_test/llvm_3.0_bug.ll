; ModuleID = '<stdin>'
target triple = "x86_64-apple-darwin11.1.0"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"

define void @test(<2 x float>* noalias %in, <2 x double>* noalias %out) nounwind {
entry:
  %x = load <2 x float>* %in
  %deadcode = fadd <2 x float> %x, %x
  %y = fpext <2 x float> %x to <2 x double>
  store <2 x double> %y, <2 x double>* %out
  ret void
}

define void @test2(<2 x float>* noalias %in, <2 x double>* noalias %out) nounwind {
entry:
  %x = load <2 x float>* %in
  ;%deadcode = fadd <2 x float> %x, %x
  %y = fpext <2 x float> %x to <2 x double>
  store <2 x double> %y, <2 x double>* %out
  ret void
}

;define <2 x double> @test3(<2 x float> *%in) {
;entry:
;  %x = load <2 x float>* %in
;  ;%deadcode = fadd <2 x float> %x, %x
;  %y = fpext <2 x float> %x to <2 x double>
;  ret <2 x double> %y
;}

;define <2 x double> @test4(<2 x float> *%in) {
;entry:
;  %x = load <2 x float>* %in
;  %deadcode = fadd <2 x float> %x, %x
;  %y = fpext <2 x float> %x to <2 x double>
;  ret <2 x double> %y
;}
