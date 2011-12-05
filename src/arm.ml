open Llvm
open Ir
open Util

let cg_expr (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  cg_expr expr

let cg_stmt (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let initial_module c =
  let m = create_module c "<fimage>" in
  Stdlib.init_module_arm m;
  m

let malloc = (fun _ _ _ _ _ -> raise (Wtf "No malloc for arm yet"))
let free = (fun _ _ _ _ -> raise (Wtf "No free for arm yet"))
let postprocess_function = (fun _ -> ())
let env = Environment.empty