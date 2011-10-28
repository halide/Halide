open Ir
open Llvm

let cg_expr_x86 (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =

  let i8x16_t = vector_type (i8_type c) 16 in

  let paddb_16 = declare_function "paddb_16"
    (function_type (i8x16_t) [|i8x16_t; i8x16_t|]) m in

  match expr with 
    (* llvm sometimes codegens vector adds to silly things (e.g. vector shifts) *)
    | _ -> cg_expr expr 

let cg_stmt_x86 (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt


    
