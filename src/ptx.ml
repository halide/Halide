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
  create_module c "<fimage>"
  (* let m = Llvm_bitreader.parse_bitcode c (MemoryBuffer.of_file "arm.bc") in *)
