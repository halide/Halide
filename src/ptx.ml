open Llvm
open Ir

(* Function calling conventions for PTX, from <llvm/CallingConv.h>
 * For whatever reason, the official OCaml binding only exposes a few
 * CallingConv constants. *)
let ptx_kernel = 71
let ptx_device = 72

let cg_expr_ptx (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  cg_expr expr

let cg_stmt_ptx (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let postprocess_function_ptx (f:llvalue) =
  set_function_call_conv ptx_kernel f

let initial_module_ptx c =
  let m = create_module c "<fimage>" in
  Stdlib.init_module_ptx m;
  m

let env_ptx =
  let ntid_decl   = (".llvm.ptx.read.ntid.x", [], i32, Extern) in
  let nctaid_decl = (".llvm.ptx.read.nctaid.x", [], i32, Extern) in
  
  let e = Environment.empty in
  let e = Environment.add "llvm.ptx.read.nctaid.x" nctaid_decl e in
  let e = Environment.add "llvm.ptx.read.ntid.x" ntid_decl e in
  
  e
