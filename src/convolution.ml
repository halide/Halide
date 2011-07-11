open Ir

let winWidth = 5
let winHeight = 5

let winxdom = { name = "i"; range = (-winWidth/2,1+winWidth/2) }
let winydom = { name = "j"; range = (-winHeight/2,1+winHeight/2) }

let x = Var("x")
let y = Var("y")
let c = Var("c")
let i = Var("i")
let j = Var("j")

let inX = Add(x, i)
let inY = Add(y, j)

let outbuf = 2
let inbuf = 1

let prgm w h ch =
  (* imAddr = x + w * (y + h * c) *)
  let imAddr x y c =
    Add(x,
        Mul(IntImm(w),
            Add(y,
                Mul(IntImm(h), c))))
  in

  let imRef im x y c = { buf = im; idx = imAddr x y c } in

  let outRef = imRef outbuf x y c in
  let inRef = imRef inbuf inX inY c in

  (* TODO: initialize output to 0? *)

  (* No Reduce statement at lowest level *)
  (*let accumStmt = Reduce(AddEq, Mul(u8, (Load(u8, inRef), FloatImm(weight))), outRef) in*)
  let accumStmt =
    Store(
      Add(
        Div(
          Load(u8, inRef),
          Cast(u8, UIntImm((winWidth)*(winHeight)))
        ),
(*
        Cast(u8,
          Mul(
            Cast(f32,
                 Load(u8, inRef)),
            FloatImm(weight))),
 *)
        Load(u8, outRef)),
      outRef) in

  let winMap = Map(winydom, Map(winxdom, accumStmt)) in

  Map(
    {name = "c"; range = (0, ch)},
    Map(
      {name = "y"; range = (winHeight/2, h-winHeight/2)},
      Map(
        {name = "x"; range = (winWidth/2, w-winWidth/2)},
        winMap
      )
    )
  )

let () =
  Test_runner.main prgm "convolution"
