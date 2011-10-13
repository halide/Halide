open Ir

let vecwidth = 16

let x = Var("x")

let outbuf = 2
let inbuf = 1

(* TODO: test all 64 bits of the word *)
let v = UIntVector(8, vecwidth)
let load = Load (v, {buf = inbuf; idx = Bop(Mul, x, UIntImm(vecwidth))})
let store vec = Store(vec, {buf = outbuf; idx = Bop(Mul, x, UIntImm(vecwidth))})
let prgm w h c =
  Map("x", 0, (w*h*c)/vecwidth,
      store (Bop(Add, load, load)))

let () =
  Test_runner.main prgm "cg_test"
