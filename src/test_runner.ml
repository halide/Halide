open Llvm_executionengine

(* TODO: support typed args array by defining `argument` algebraic type and
* defining generic_value_of_argument *)
let run (prgm:Ir.stmt) args =
  (* JITing requires initializing targetdata to the native target, or else 
   * subtle endianness and similar bugs will crop up in generated code *)
  ignore (initialize_native_target ());
  
  (* codegen the program, including an OCaml argument-passing wrapper *)
  let (m,f) = Cg_llvm.codegen_to_ocaml_callable prgm in
  
  let ee = ExecutionEngine.create m in

  ignore (
    ExecutionEngine.run_function f (Array.map GenericValue.of_pointer args) ee
  )

(* the program template type  *)
type program_template = int -> int -> int -> Ir.stmt

(* Run the program, parameterized on image w,h,channels, on the image loaded
 * from infile, saving the results to outfile, and writing  *)
let run_on_image (prgm:program_template) ?(dbgfile = "") infile outfile = 
  (* load test.png into input and output arrays *)
  let (w,h,inarr) = Imageio.load infile in
  let outarr = Util.clone_bigarray inarr in

  (* instantiate the program with image w,h,channels *)
  let p = prgm w h 3 in

    (* save bitcode for debugging *)
    if not (dbgfile = "") then Cg_llvm.codegen_to_file dbgfile p;

    (* run the program *)
    run p [| inarr; outarr |];

    (* save outarr to test.out.png *)
    Imageio.save outarr w h outfile
