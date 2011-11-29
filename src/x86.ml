open Ir
open Llvm

let cg_expr_x86 (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =

  (* let i8x16_t = vector_type (i8_type c) 16 in *)

  (* let paddb_16 = declare_function "paddb_16"
    (function_type (i8x16_t) [|i8x16_t; i8x16_t|]) m in *)

  match expr with 
    (* llvm sometimes codegens vector adds to silly things (e.g. vector shifts) *)
    | _ -> cg_expr expr 

let cg_stmt_x86 (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let initial_module_x86 c =
  let m = create_module c "<fimage>" in
  (* let m = Llvm_bitreader.parse_bitcode c (MemoryBuffer.of_file "x86.bc") in *)

  (* Set the target triple and target data for our dev machines *)
  set_target_triple "x86_64-apple-darwin11.1.0" m;
  set_data_layout "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64" m;

  m

let malloc_x86 (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  let malloc = declare_function "malloc" (function_type (pointer_type (i8_type c)) [|i64_type c|]) m in
  build_call malloc [|cg_expr (Cast (Int 64, expr))|] "" b  

let free_x86 (c:llcontext) (m:llmodule) (b:llbuilder) (address:llvalue) =
  let free = declare_function "free" (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
  build_call free [|address|] "" b   
