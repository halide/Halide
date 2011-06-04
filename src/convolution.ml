open Ir

let imWidth = 512
let imHeight = 512
let winWidth = 16
let winHeight = 16
let weight = 1.0 /. float_of_int ((winWidth+1)**2)

(* TODO: how to initialize reduction cell to 0? *)

let dom nm rn = { name = nm; range = rn }

let xdom = dom "x" (0,imWidth-1)
let ydom = dom "y" (0,imHeight-1)
let winxdom = dom "i" (-winWidth/2,winWidth/2)
let winydom = dom "j" (-winHeight/2,winHeight/2)

let x = Var("x")
let y = Var("y")
let i = Var("i")
let j = Var("j")

let inX = Add(i64, (x, i))
let inY = Add(i64, (y, j))

let imAddr x y = Add(i64, (Mul(i64, (y, IntImm(imWidth))), x))
let imRef im x y = { buf = im; idx = imAddr x y }

let outbuf = 2
let inbuf = 1

let outRef = imRef outbuf x y
let inRef = imRef inbuf inX inY

let accumStmt = Reduce(AddEq, Mul(u8, (Load(u8, inRef), FloatImm(weight))), outRef)

let winMap = Map(winydom, Map(winxdom, accumStmt))
let outMap = Map(ydom, Map(xdom, winMap))

let () =
    (*match inX with*)
    (*| Add(_, (Var(a),Var(b))) -> Printf.printf "%s + %s" a b*)
    (*| _ -> ()*)
  Printf.printf "%s\n" (Ir_printer.string_of_stmt outMap)
