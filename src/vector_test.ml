open Ir

let x = Var("x")
let y = Var("y")
let c = Var("c")

let outbuf = 2
let inbuf = 1

let vecWidth = 16

let prgm w h ch =
  let imAddr x y c =
    Add(x,
        Mul(IntImm(w),
            Add(y,
                Mul(IntImm(h), c))))
  in

  let imRef im x y c = { buf = im; idx = imAddr x y c } in

  let outRef = imRef outbuf (Mul(x, IntImm(vecWidth))) y c in

  (* Reverse every block of 4 pixels *)
  let lx k = Load(UInt(8), imRef inbuf (Add(Mul(x, IntImm(vecWidth)), IntImm(k))) y c) in
  
  let stmt = Store(
    MakeVector([
               lx 3; lx 2; lx 1; lx 0;
               lx 7; lx 6; lx 5; lx 4;
               lx 11; lx 10; lx 9; lx 8;
               lx 15; lx 14; lx 13; lx 12
              ]),
    outRef) in
   

  Map(
  {name = "c"; range = (0, ch)},
  Map(
  {name = "y"; range = (0, h)},
  Map(
  {name = "x"; range = (0, w/vecWidth)},
  stmt
 )
 )
 )
    
let () =
  Test_runner.main prgm "vector_test"
    
