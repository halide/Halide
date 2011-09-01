open Ir

let x = Var("x")
let y = Var("y")
let c = Var("c")

let outbuf = 2
let inbuf = 1

let vecwidth = 2
let vt = UIntVector(8, vecwidth)

let prgm w h ch =
  (* imAddr = x + w * (y + h * c) *)
  let imAddr x y c =
    x +~ (IntImm(w) *~ (y +~ (c *~ IntImm(h))))
  in

  let imRef im x y c = { buf = im; idx = imAddr x y c } in

  let outRef = imRef outbuf (x *~ IntImm(vecwidth)) y c in
  let inRef = imRef inbuf (x *~ IntImm(vecwidth)) y c in
  let inRefNext = imRef inbuf ((x *~ IntImm(vecwidth)) +~ IntImm(1)) y c in

  Map("c", 0, ch,
    Map("y", 0, h,
      Map("x", 0, (w-1)/vecwidth,
        Store(Broadcast(Cast(u8, IntImm(127)), vecwidth) +~ Load(vt, inRefNext) -~ Load(vt, inRef), outRef)
      )
    )
  )

let () =
  Test_runner.main prgm "gradientx"
