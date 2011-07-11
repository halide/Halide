open Ir

let winWidth = 16
let winHeight = 16
let weight = 1.0 /. float_of_int ((winWidth+1)*(winWidth+1))

(* TODO: how to initialize reduction cell to 0? *)

let winxdom = { name = "i"; range = (-winWidth/2,winWidth/2) }
let winydom = { name = "j"; range = (-winHeight/2,winHeight/2) }

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
  (* imAddr = c + Ch * (x + W * y) *)
  let imAddr x y c =
    Add(c,
        Mul(IntImm(ch),
            Add(x,
                Mul(IntImm(w), y))))
  in

  let imRef im x y c = { buf = im; idx = imAddr x y c } in

  let outRef = imRef outbuf x y c in
  let inRef = imRef inbuf inX inY c in

  (* No Reduce statement at lowest level *)
  (*let accumStmt = Reduce(AddEq, Mul(u8, (Load(u8, inRef), FloatImm(weight))), outRef) in*)
  let accumStmt =
    Store(
      Add(
        Cast(u8,
          Mul(
            Cast(f32,
                 Load(u8, inRef)),
            FloatImm(weight))),
        Load(u8, outRef)),
      outRef) in

  let winMap = Map(winydom, Map(winxdom, accumStmt)) in

  Map(
    {name = "y"; range = (0, h)},
    Map(
      {name = "x"; range = (0, w)},
      Map(
        {name = "c"; range = (0, 3)},
        winMap
      )
    )
  )

let () =
  Test_runner.run prgm "convolution"
