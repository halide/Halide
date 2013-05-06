(* Simple LLVM codegen test. *)
(* Spits out LLVM bitcode in argv[1] (or a.out by default) *)

open Llvm

let dbgprint = true

let main filename =
  let c = create_context () in

  let i8_t  = i8_type c in
  let i32_t = i32_type c in
  let f32_t = float_type c in
  let f32x4_t = vector_type f32_t 4 in

  let m = create_module c filename in

  (* @greeting = global [14 x i8] c"Hello, world!\00" *)
  let greeting =
   define_global "greeting" (const_string c "Hello, world!\000") m in

  (* declare i32 @puts(i8* ) *)
  let puts =
   declare_function "puts"
     (function_type i32_t [|pointer_type i8_t|]) m in

  (* declare void @printv(<4 x float>)
   * nonce extern which forces preservation of vector results *)
  let printv =
   declare_function "printv"
     (function_type (void_type c) [|f32x4_t|]) m in

  (* declare <4 x float> @llvm.x86.sse.sqrt.ps(<4 x float>) nounwind readnone *)
  let sqrtps =
   declare_function "llvm.x86.sse.sqrt.ps"
     (function_type f32x4_t [|f32x4_t|]) m in

  (* define i32 @main() { entry: *)
  let main = define_function "main" (function_type i32_t [| |]) m in
  let at_entry = builder_at_end c (entry_block main) in

  (*%sqrtps = call <4 x float> @llvm.x86.sse.sqrt.ps(<4 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00, float 4.000000e+00>)*)
  let cv1234 = const_vector [| const_float f32_t 1.0; const_float f32_t 2.0;
  const_float f32_t 3.0; const_float f32_t 4.0 |] in
  let sqrt = build_call sqrtps [| cv1234 |] "sqrtps" at_entry in
  
  (* call void printv(sqrtps) *)
  ignore (build_call printv [| sqrt |] "" at_entry);

  (* %tmp = getelementptr [14 x i8]* @greeting, i32 0, i32 0 *)
  let zero = const_int i32_t 0 in
  let str = build_gep greeting [| zero; zero |] "tmp" at_entry in

  (* call i32 @puts( i8* %tmp ) *)
  ignore (build_call puts [| str |] "" at_entry);

  (* ret void *)
  ignore (build_ret (const_null i32_t) at_entry);
  
  (* Print .ll to stderr *)
  if dbgprint then dump_module m;

  (* write the module to a file *)
  if not (Llvm_bitwriter.write_bitcode_file m filename) then exit 1;
  dispose_module m

let () = match Sys.argv with
  | [|_; filename|] -> main filename
  | _ -> main "a.out"
