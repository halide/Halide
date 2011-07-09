open Ir

let brightness = 50

let i = Var("i")

let outbuf = 2
let inbuf = 1

(* TODO: can't work with vectors until we can lift contstants to vectors *)
(* let vecwidth = 16 *)
(*let vt = Vector(u8, vecwidth)*)
let vecwidth = 1
let vt = u8

let load = Load (vt, {buf = inbuf; idx = i})
let store vec = Store(vec, {buf = outbuf; idx = i})

let prgm w h c =
  Map({name = "i"; range = (0, (w*h*c)/vecwidth)},
      (* TODO: add max to clamp add *)
      store (Add(vt, (load, Cast(u8, UIntImm(brightness))))))

let () =
  Test_runner.run prgm "brightness"
