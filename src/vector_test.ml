open Ir

let x = Var("x")
let y = Var("y")
let c = Var("c")

let outbuf = 2
let inbuf = 1

let vecWidth = 16

let prgm w h ch =
  let imAddr x y c =
    Bop(Add, x,
        Bop(Mul, IntImm(w),
            Bop(Add, y,
                Bop(Mul, IntImm(h), c))))
  in

  (* TODO: alignment problem is that 600px * 3 channels does not evenly divide
   * by 16. *)
  (* TODO: ...fix by clamping x range to skip non-vector-divisible values *)
  let imRef im x y c = { buf = im; idx = imAddr x y c } in

  let outRef = imRef outbuf (Bop(Mul, x, IntImm(vecWidth))) y c in

  (* Reverse every block of 4 pixels *)
  let lx k = Load(UInt(8), imRef inbuf (Bop(Add, Bop(Mul, x, IntImm(vecWidth)), IntImm(k))) y c) in
  
  let stmt = Store(
    MakeVector([
               lx 3; lx 2; lx 1; lx 0;
               lx 7; lx 6; lx 5; lx 4;
               lx 11; lx 10; lx 9; lx 8;
               lx 15; lx 14; lx 13; lx 12
              ]),
    outRef) in
   

  Map("c", 0, ch,
  Map("y", 0, h,
  Map(
  "x", 0, w/vecWidth,
  stmt
 )
 )
 )
    
let () =
  (*Test_runner.main prgm "vector_test"*)

  let i = Var("i") in
  let lx k = Load(UInt(8), {buf = inbuf; idx = Bop(Add, Bop(Mul, i, IntImm(vecWidth)), IntImm(k))}) in

  let s = Map("i", 0, 800*600*3/vecWidth,
    Store(
      MakeVector([
                 lx 3; lx 2; lx 1; lx 0;
                 lx 7; lx 6; lx 5; lx 4;
                 lx 11; lx 10; lx 9; lx 8;
                 lx 15; lx 14; lx 13; lx 12
                ]),
      {buf = outbuf; idx = Bop(Mul, i, IntImm(vecWidth))})
  ) in
    
(*
  let inArr = Bigarray.Array1.create Bigarray.int8_unsigned Bigarray.c_layout vecWidth in
    for i = 0 to vecWidth-1 do
      inArr.{i} <- i;
    done;
 *)
    Cg_llvm.codegen_to_file "vector_test.bc" s
