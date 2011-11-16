open Llvm
open Ir

let cg_expr_arm (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  cg_expr expr

let cg_stmt_arm (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let initial_module_arm c =
  create_module c "<fimage>"
  (* let m = Llvm_bitreader.parse_bitcode c (MemoryBuffer.of_file "arm.bc") in *)
