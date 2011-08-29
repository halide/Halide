open Ir
open Vectorize

let x = Var("x") and y = Var("y") and c = Var("c") 

let inbuf = 1
let tmpbuf = 2
let outbuf = 3

let prgm width height channels =
  let load b x y c = Load (u8, {buf = b; idx = x +~ (IntImm(width) *~ (y +~ (IntImm(height) *~ c)))}) in
  let store b x y c v = Store (v, {buf = b; idx = x +~ (IntImm(width) *~ (y +~ (IntImm(height) *~ c)))}) in

  let one = IntImm(1) in
  let two = Cast(u8, UIntImm(2)) in
  let four = Cast(u8, UIntImm(4)) in

  let horizontal_blur =
    Map ("c", 0, channels,
         Map ("y", 0, height,
              Map ("x", 0, width, 
                   let x0 = load 1 (x-~one) y c 
                   and x1 = load 1 (x) y c 
                   and x2 = load 1 (x+~one) y c in
                   store 3 x y c ((x0 +~ x2) /~ two)
              )
         )
    )
  in


  let vertical_blur =
    Map ("c", 0, channels,
         Map ("y", 0, height,
              Map ("x", 0, width, 
                   let y0 = load 3 x (y-~one) c
                   and y1 = load 3 x (y) c 
                   and y2 = load 3 x (y+~one) c in
                   store 2 x y c ((y0 +~ y2) /~ two)
              )
         )
    )
  in
  
  vectorize_stmt (Block [ horizontal_blur ; vertical_blur ]) "x" 16


let () =
  Test_runner.main prgm "fused_convolution"
