open Cg_llvm

type arch = X86_64 | PTX | ARM | Android

let target =
  let targetstr = 
    try
      Sys.getenv "HL_TARGET"
    with Not_found ->
      Printf.eprintf "HL_TARGET not set - inferring from host architecture...";
      let out = Unix.open_process_in "uname -m" in
      let str = input_line out in
      Printf.eprintf "got %s\n%!" str;
      str
  in
  begin match targetstr with
    | "x86_64" | "amd64" | "i386" -> X86_64
    | "ptx" -> PTX
    | "armv7l" -> ARM
    | "android" -> Android
    | arch -> Printf.eprintf "`%s` is not a supported arch\n%!" arch; exit (-1) end
    
let codegen_entry,
    codegen_c_wrapper,
    codegen_to_bitcode_and_header,
    codegen_to_file =
  let module CgX86 = CodegenForArch(X86) in
  let module CgPTX = CodegenForArch(Ptx) in
  let module CgARM = CodegenForArch(Arm) in
  let module CgAndroid = CodegenForArch(Android) in
  match target with
    | X86_64 -> (CgX86.codegen_entry,
                 CgX86.codegen_c_wrapper,
                 CgX86.codegen_to_bitcode_and_header,
                 CgX86.codegen_to_file)
    | PTX ->    (CgPTX.codegen_entry,
                 CgPTX.codegen_c_wrapper,
                 CgPTX.codegen_to_bitcode_and_header,
                 CgPTX.codegen_to_file)
    | ARM ->    (CgARM.codegen_entry,
                 CgARM.codegen_c_wrapper,
                 CgARM.codegen_to_bitcode_and_header,
                 CgARM.codegen_to_file)
    | Android -> (CgAndroid.codegen_entry,
                  CgAndroid.codegen_c_wrapper,
                  CgAndroid.codegen_to_bitcode_and_header,
                  CgAndroid.codegen_to_file)
