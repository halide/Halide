open Llvm
open Ir

let cg_expr_arm (c:llcontext) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  cg_expr expr

let cg_stmt_arm (c:llcontext) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt


