open Ir

let x = Var("x")
let y = Var("y")
let c = Var("c")

let outbuf = 2
let inbuf = 1

let prgm w h ch =
  let imAddr x y c =
    Add(x,
        Mul(IntImm(w),
            Add(y,
                Mul(IntImm(h), c))))
  in

  let imRef im x y c = { buf = im; idx = imAddr x y c } in

  let outRef = imRef outbuf (Mul(x, IntImm(4))) y c in

  (* Reverse every block of 4 pixels *)
  let lx k = Load(UInt(8), imRef inbuf (Add(Mul(x, IntImm(4)), IntImm(k))) y c) in
  
  let stmt = Store(
    MakeVector([
               lx 3; lx 2; lx 1; lx 0
              ]),
    outRef) in
   

  Map(
  {name = "c"; range = (0, ch)},
  Map(
  {name = "y"; range = (0, h)},
  Map(
  {name = "x"; range = (0, w/4)},
  stmt
 )
 )
 )
    
let () =
  Test_runner.main prgm "vector_test"
    
