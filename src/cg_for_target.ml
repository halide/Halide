open Cg_llvm

type arch = X86 | PTX | ARM | Android

let (target, target_opts) =
  let target_str = 
    try
      Sys.getenv "HL_TARGET"
    with Not_found ->            
      Printf.eprintf "HL_TARGET not set - inferring from host architecture...";
      let out = Unix.open_process_in "uname -m" in
      let str = input_line out in
      Printf.eprintf "got %s\n%!" str;
      str
  in  
  let (target_str, opts) = match Util.split_name target_str with
    | (first::rest) -> (first, rest) 
    | [] -> failwith "split_name returned an empty list"
  in
  let target = begin match target_str with
    | "x86_64" | "amd64" | "i386" -> X86
    | "ptx" -> PTX
    | "armv7l" -> ARM
    | "android" -> Android
    | arch -> Printf.eprintf "`%s` is not a supported arch\n%!" arch; exit (-1) 
  end in
  (target, opts)
    
let codegen_entry,
    codegen_c_wrapper,
    codegen_to_bitcode_and_header =
  let module CgX86 = CodegenForArch(X86) in
  let module CgPTX = CodegenForArch(Ptx) in
  let module CgARM = CodegenForArch(Arm) in
  let module CgAndroid = CodegenForArch(Android) in
  match target with
    | X86 -> (CgX86.codegen_entry,
              CgX86.codegen_c_wrapper,
              CgX86.codegen_to_bitcode_and_header)
    | PTX -> (CgPTX.codegen_entry,
              CgPTX.codegen_c_wrapper,
              CgPTX.codegen_to_bitcode_and_header)
    | ARM -> (CgARM.codegen_entry,
              CgARM.codegen_c_wrapper,
              CgARM.codegen_to_bitcode_and_header)
    | Android -> (CgAndroid.codegen_entry,
                  CgAndroid.codegen_c_wrapper,
                  CgAndroid.codegen_to_bitcode_and_header)
