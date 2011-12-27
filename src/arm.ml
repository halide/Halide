open Llvm
open Ir
open Util
open Cg_llvm_util

let codegen_entry c m cg_entry e =
  (* set up module *)
  Stdlib.init_module_arm m;

  (* build the inner kernel, which takes raw byte*s *)
  let inner = cg_entry c m e in

  (* return the wrapper which takes buffer_t*s *)
  cg_wrapper c m e inner

let cg_expr (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  cg_expr expr

let cg_stmt (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let malloc = (fun _ _ _ _ _ -> raise (Wtf "No malloc for arm yet"))
let free = (fun _ _ _ _ -> raise (Wtf "No free for arm yet"))
let env = Environment.empty
