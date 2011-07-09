open Llvm_executionengine

(* TODO: support typed args array by defining `argument` algebraic type and
* defining generic_value_of_argument *)
let run prgm args =
  (* JITing requires initializing targetdata to the native target, or else 
   * subtle endianness and similar bugs will crop up in generated code *)
  ignore (initialize_native_target ());
  
  (* codegen the program, including an OCaml argument-passing wrapper *)
  let (m,f) = Cg_llvm.codegen_to_ocaml_callable prgm in
  
  let ee = ExecutionEngine.create m in

  ignore (
    ExecutionEngine.run_function f (Array.map GenericValue.of_pointer args) ee
  )
