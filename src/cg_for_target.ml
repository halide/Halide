open Cg_llvm

let codegen_entry, codegen_c_wrapper, codegen_to_bitcode_and_header = 
  let module CgX86 = CodegenForArch(X86) in
  let module CgPTX = CodegenForArch(Ptx) in
  let module CgARM = CodegenForArch(Arm) in
  
  let cg_entry = function 
    | "x86_64"  -> CgX86.codegen_entry
    | "arm"     -> CgARM.codegen_entry
    | "ptx"     -> CgPTX.codegen_entry
    | target    -> failwith (Printf.sprintf "Unknown target: %s" target)
  in

  let cg_c_wrapper = function 
    | "x86_64"  -> CgX86.codegen_c_wrapper
    | "arm"     -> CgARM.codegen_c_wrapper
    | "ptx"     -> CgPTX.codegen_c_wrapper
    | target    -> failwith (Printf.sprintf "Unknown target: %s" target)
  in

  let cg_to_bitcode_and_header = function 
    | "x86_64"  -> CgX86.codegen_to_bitcode_and_header
    | "arm"     -> CgARM.codegen_to_bitcode_and_header
    | "ptx"     -> CgPTX.codegen_to_bitcode_and_header 
    | target    -> failwith (Printf.sprintf "Unknown target: %s" target)
  in

  (cg_entry, cg_c_wrapper, cg_to_bitcode_and_header)
