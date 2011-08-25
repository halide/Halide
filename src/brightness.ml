open Ir

let brightness = 100

let i = Var("i")

let outbuf = 2
let inbuf = 1

(* TODO: can't work with vectors until we can lift contstants to vectors *)
let vecwidth = 16
let vt = UIntVector(8, vecwidth)
(*let vecwidth = 1*)
(*let vt = u8*)

let load = Load (vt, {buf = inbuf; idx = Mul(i, IntImm(vecwidth))})
let store vec = Store(vec, {buf = outbuf; idx = Mul(i, IntImm(vecwidth))})

exception Unsupported_type of val_type

(* saturating add *)
(* TODO: use this as model for built-in library functions? *)
let sadd(a, b) =
  let t = val_type_of_expr a in
  let max_val = match t with
    | UInt(8)  -> Cast(t, UIntImm(0xFF))
    | UInt(16) -> Cast(t, UIntImm(0xFFFF))
    | UInt(32) -> Cast(t, UIntImm(0xFFFFFFFF))
    | UIntVector(8, w) -> Broadcast(Cast(UInt(8), UIntImm(0xFF)), w)
    | t -> raise (Unsupported_type(t))
  in
    Select(a >. (max_val -. b), max_val, a +. b)

let prgm w h c =
  Map(
    {name = "i"; range = (0, (w*h*c)/vecwidth)},
    store (
      sadd(load, (Broadcast(Cast(UInt(8), UIntImm(brightness)), vecwidth)))
      (*sadd(load, (Cast(UInt(8), UIntImm(brightness))))*)
    )
  )

let () =
  Test_runner.main prgm "brightness"
