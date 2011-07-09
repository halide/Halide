open Ir
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
let () =

  
  Cg_llvm.codegen_to_file "cg_test.bc" prgm;
  
  (* load test.png into input and output arrays *)
  let (w,h,inarr) = Imageio.load "test.png" in
  let outarr = Util.copy_bigarray inarr in
    Runner.run prgm [| inarr; outarr |];
    Imageio.save outarr w h "test.out.png";

