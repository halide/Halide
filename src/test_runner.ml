open Llvm_executionengine
open Bigarray
open Img

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
  let inarr = load_png infile in
  let outarr = Util.clone_bigarray inarr in

  (* instantiate the program *)
  let p = prgm (Array3.dim3 inarr) (Array3.dim2 inarr) (Array3.dim1 inarr) in

  (* save bitcode for debugging *)
  if not (dbgfile = "") then Cg_llvm.codegen_to_file dbgfile p;
  
  (* run the program *)
  run p [| inarr; outarr |];
  
  (* save outarr to test.out.png *)
  save_png outarr outfile

(* template for running simple test programs with argv[1] = input.png *)
let main ?(dbg = true) prgm name =
  (* TODO: extract name from argv[0] *)
  match Sys.argv with
    | [| _; infile |] -> run_on_image
                           prgm
                           infile
                           ("out_"^name^".png")
                           ~dbgfile:(if dbg then name^".bc" else "")

    | _ -> failwith ("Invalid arguments. Usage: "^name^" <input_image>")
