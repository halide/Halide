open Ir
open Ir_printer

let dom nm rn = { name = nm; range = rn }

let vecwidth = 16
let xdom len = dom "x" (0,len/vecwidth)

let x = Var("x")

let outbuf = 2
let inbuf = 1

let inref = { buf=inbuf; idx=UIntImm(0) }
let outref = { buf=outbuf; idx=UIntImm(0) }

(* TODO: test all 64 bits of the word *)
(*let prgm = Store( Add( i64, ( Cast( i64, IntImm( 1 ) ), Load( i64, inref ) ) ), outref ) *)
(*let prgm = Store( Load( i64, inref ), outref )*)
(*let prgm = Store( Div( f32, ( Cast( f32, FloatImm( 17.4 ) ), Load( f32, inref ) ) ), outref ) *)
let v = Vector(UInt(8), vecwidth)
let load = Load (v, {buf = inbuf; idx = x})
let store vec = Store(vec, {buf = outbuf; idx = x})
let prgm len = Map((xdom len), store (Add(v, (load, load))))
 
let () =
  (* load test.png into input and output arrays *)
  let (w,h,inarr) = Imageio.load "test.png" in
  let outarr = Util.copy_bigarray inarr in

  (* instantiate the program with x range = image size *)
  let p = prgm (w*h*3) in

    (* run the program *)
    Runner.run p [| inarr; outarr |];

    (* save outarr to test.out.png *)
    Imageio.save outarr w h "test.out.png";

    (* save bitcode for debugging *)
    Cg_llvm.codegen_to_file "cg_test.bc" p;
