open Ir
open Llvm

let cg_expr (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =

  (* let i8x16_t = vector_type (i8_type c) 16 in *)

  (* let paddb_16 = declare_function "paddb_16"
    (function_type (i8x16_t) [|i8x16_t; i8x16_t|]) m in *)

  match expr with 
    (* llvm sometimes codegens vector adds to silly things (e.g. vector shifts) *)
    | _ -> cg_expr expr 

let cg_stmt (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let initial_module c =
  let m = create_module c "<fimage>" in
  Stdlib.init_module_x86 m;
  m

let malloc (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (size : expr) =
  let malloc = declare_function "safe_malloc" (function_type (pointer_type (i8_type c)) [|i64_type c|]) m in  
  build_call malloc [|cg_expr (Cast (Int 64, size))|] "" b 


let free (c:llcontext) (m:llmodule) (b:llbuilder) (address:llvalue) =
  let free = declare_function "safe_free" (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
  build_call free [|address|] "" b   
