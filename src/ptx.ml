open Llvm
open Ir
open Util

(* Function calling conventions for PTX, from <llvm/CallingConv.h>
 * For whatever reason, the official OCaml binding only exposes a few
 * CallingConv constants. *)
let ptx_kernel = 71
let ptx_device = 72

let cg_expr (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  cg_expr expr

let cg_stmt (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let postprocess_function (f:llvalue) =
  set_function_call_conv ptx_kernel f

let rec codegen_entry host_ctx host_mod cg_entry entry =
  (* create separate device module *)
  let dev_ctx = create_context () in
  let dev_mod = create_module dev_ctx "<halide-device-ptx>" in

  (* set up module templates *)
  (*Stdlib.init_host_module_ptx host_mod;*)
  (*Stdlib.init_device_module_ptx dev_mod;*)

  (* codegen the main kernel *)
  let f = cg_entry dev_ctx dev_mod entry in

  (* set calling convention to __global__ for PTX *)
  set_function_call_conv ptx_kernel f;

  let ptx_src = Llutil.compile_module_to_string dev_mod in
  Printf.printf "PTX:\n%s\n%!" ptx_src;

  (* free memory *)
  dispose_module dev_mod;
  dispose_context dev_ctx;

  (* return the generated function *)
  f

let malloc = (fun _ _ _ _ _ -> raise (Wtf "No malloc for PTX yet"))
let free = (fun _ _ _ _ -> raise (Wtf "No free for PTX yet"))

let env =
  let ntid_decl   = (".llvm.ptx.read.ntid.x", [], i32, Extern) in
  let nctaid_decl = (".llvm.ptx.read.nctaid.x", [], i32, Extern) in
  
  let e = Environment.empty in
  let e = Environment.add "llvm.ptx.read.nctaid.x" nctaid_decl e in
  let e = Environment.add "llvm.ptx.read.ntid.x" ntid_decl e in
  
  e
