open Ir
open Llvm_executionengine
open Ir_printer

let dom nm rn = { name = nm; range = rn }

let xdom = dom "x" (0,10)

let x = Var("x")

let outbuf = 2
let inbuf = 1

let inref = { buf=inbuf; idx=UIntImm(0) }
let outref = { buf=outbuf; idx=UIntImm(0) }

(* TODO: test all 64 bits of the word *)
(*let prgm = Store( Add( i64, ( Cast( i64, IntImm( 1 ) ), Load( i64, inref ) ) ), outref ) *)
(*let prgm = Store( Load( i64, inref ), outref )*)
(*let prgm = Store( Div( f32, ( Cast( f32, FloatImm( 17.4 ) ), Load( f32, inref ) ) ), outref ) *)
let prgm = Map(xdom, Store( Add(i64, (Load (i64, {buf = inbuf; idx = x}), 
                                      Cast (i64, IntImm(1)))),
                            {buf = outbuf; idx = x}))

let mkarr sz =
  let arr =
    Bigarray.Array1.create Bigarray.nativeint Bigarray.c_layout sz in
    Bigarray.Array1.fill arr (Nativeint.of_int 0);
    arr

let () =

  Printf.printf "%s\n" (string_of_stmt prgm);
  
  Cg_llvm.codegen_to_file "cg_test.bc" prgm;
  
  (* JITing requires initializing targetdata to the native target, or else 
   * subtle endianness and similar bugs will crop up in generated code *)
  ignore (initialize_native_target ());
  
  let (m,f) = Cg_llvm.codegen_to_ocaml_callable prgm in
  
  let ee = ExecutionEngine.create m in
  
  let inarr = mkarr 10 in
  let outarr = mkarr 10 in
  
  for i=0 to 9 do
    inarr.{i} <- Nativeint.of_int (i*17)
  done;
  
  ignore (
  ExecutionEngine.run_function
    f
    [| GenericValue.of_pointer inarr; GenericValue.of_pointer outarr |]
    ee
 );
  
  for i=0 to 9 do
    Printf.printf "%nd -> %nd\n" inarr.{i} outarr.{i} 
  done
