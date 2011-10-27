open Util
open Llvm
open X86
open Arm
open Ir

type architecture = {
  cg_expr : llcontext -> llmodule -> llbuilder -> (expr -> llvalue) -> expr -> llvalue;
  cg_stmt : llcontext -> llmodule -> llbuilder -> (stmt -> llvalue) -> stmt -> llvalue
}

let x86 = {cg_expr = cg_expr_x86; cg_stmt = cg_stmt_x86}
let arm = {cg_expr = cg_expr_arm; cg_stmt = cg_stmt_arm}
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
