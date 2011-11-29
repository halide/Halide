open Util
open Llvm
open X86
open Arm
open Ptx
open Ir

type architecture = {
  cg_expr : llcontext -> llmodule -> llbuilder -> (expr -> llvalue) -> expr -> llvalue;
  cg_stmt : llcontext -> llmodule -> llbuilder -> (stmt -> llvalue) -> stmt -> llvalue;
  malloc : llcontext -> llmodule -> llbuilder -> (expr -> llvalue) -> expr -> llvalue;
  free : llcontext -> llmodule -> llbuilder -> llvalue -> llvalue;
  (* codegen : llcontext -> entrypoint -> llmodule * llvalue; *)
  (* TODO: this postprocess_function interface is ugly, but the easiest hack around
   * circular dependencies for now. *)
  postprocess_function : llvalue -> unit;
  initial_module : llcontext -> llmodule;
}

(* This is ugly, but these have to live here for the moment to avoid circular dependencies *)
(* TODO: solve this by making codegen, architecture into parameterized modules *)
(* let codegen_x86 c e =
  Cg_llvm.codegen_to_c_callable c e x86

let codegen_arm c e =
  Cg_llvm.codegen_to_c_callable c e arm

let codegen_ptx c e =
  Cg_llvm.codegen c e ptx *)

let x86 = {
  cg_expr = cg_expr_x86;
  cg_stmt = cg_stmt_x86;
  malloc = malloc_x86;
  free = free_x86;
  (* codegen = codegen_x86; *)
  postprocess_function = (fun _ -> ());
  initial_module = initial_module_x86
}

let arm = {
  cg_expr = cg_expr_arm;
  cg_stmt = cg_stmt_arm;
  malloc = (fun _ _ _ _ _ -> raise (Wtf "No malloc for arm yet"));
  free = (fun _ _ _ _ -> raise (Wtf "No free for arm yet"));
  (* codegen = codegen_arm; *)
  postprocess_function = (fun _ -> ());
  initial_module = initial_module_arm
}

let ptx = {
  cg_expr = cg_expr_ptx;
  cg_stmt = cg_stmt_ptx;
  malloc = (fun _ _ _ _ _ -> raise (Wtf "No malloc for ptx yet"));
  free = (fun _ _ _ _ -> raise (Wtf "No free for ptx yet"));
  (* codegen = codegen_ptx; *)
  postprocess_function = postprocess_function_ptx;
  initial_module = initial_module_ptx
}

let host = 
  let hosttype = 
    try
      Sys.getenv "HOSTTYPE" 
    with Not_found ->
      Printf.printf "Could not detect host architecture (HOSTTYPE not set). Assuming x86_64.\n";
      "x86_64"      
  in
  if hosttype = "arm" then arm 
  else if hosttype = "x86_64" then x86 
  else raise (Wtf ("Unknown host architecture " ^ hosttype))
