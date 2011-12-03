open Llvm
open Ir

let cg_expr_arm (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  cg_expr expr

let cg_stmt_arm (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let initial_module_arm c =
  let m = create_module c "<fimage>" in
  Stdlib.init_module_arm m;
  m
