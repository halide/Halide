open Ir
open Llvm_executionengine
open Ir_printer

let dom nm rn = { name = nm; range = rn }

let xdom = dom "x" (0,16)

let x = Var("x")

let outbuf = 2
let inbuf = 1

let inref = { buf=inbuf; idx=UIntImm(0) }
let outref = { buf=outbuf; idx=UIntImm(0) }

(* TODO: test all 64 bits of the word *)
(*let prgm = Store( Add( i64, ( Cast( i64, IntImm( 1 ) ), Load( i64, inref ) ) ), outref ) *)
(*let prgm = Store( Load( i64, inref ), outref )*)
(*let prgm = Store( Div( f32, ( Cast( f32, FloatImm( 17.4 ) ), Load( f32, inref ) ) ), outref ) *)
let v = Vector(UInt(8), 16) 
let load = Load (v, {buf = inbuf; idx = x})
let store vec = Store(vec, {buf = outbuf; idx = x})
let prgm = Map(xdom, store (Add(v, (load, load))))

let mkarr sz =
  let arr =
    Bigarray.Array1.create Bigarray.int8_unsigned Bigarray.c_layout sz in
    Bigarray.Array1.fill arr 0;
    arr

let () =

  Printf.printf "%s\n" (string_of_stmt prgm);
  
  Cg_llvm.codegen_to_file "cg_test.bc" prgm;
  
  (* JITing requires initializing targetdata to the native target, or else 
   * subtle endianness and similar bugs will crop up in generated code *)
  ignore (initialize_native_target ());
  
  let (m,f) = Cg_llvm.codegen_to_ocaml_callable prgm in
  
  let ee = ExecutionEngine.create m in
 
  let (w,h,inarr) = Imageio.load "test.png" in

  let copy_bigarray arr =
    let cp = Bigarray.Array1.create
               (Bigarray.Array1.kind arr)
               (Bigarray.Array1.layout arr)
               (Bigarray.Array1.dim arr) in
      Bigarray.Array1.blit arr cp;
      cp
  in
  let outarr = copy_bigarray inarr in

    ignore (
      ExecutionEngine.run_function
        f
        [| GenericValue.of_pointer inarr; GenericValue.of_pointer outarr |]
        ee
    );
  
    Imageio.save outarr w h "test.out.png"
