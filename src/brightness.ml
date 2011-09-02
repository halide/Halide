open Ir

let brightness = Cast(UInt(8), UIntImm(100))

let i = Var("i")

let outbuf = "out"
let inbuf = "in"

let load = Load (u8, {buf = inbuf; idx = i})
let store x = Store(x, {buf = outbuf; idx = i})

exception Unsupported_type of val_type

(* saturating add *)
(* TODO: use this as model for built-in library functions? *)
let sadd(a, b) =
  let t = val_type_of_expr a in
  let max_val = match t with
    | UInt(8)  -> Cast(t, UIntImm(0xFF))
    | UInt(16) -> Cast(t, UIntImm(0xFFFF))
    | UInt(32) -> Cast(t, UIntImm(0xFFFFFFFF))
    | t -> raise (Unsupported_type(t))
  in
    Select(a >~ (max_val -~ b), max_val, a +~ b)

let prgm =
  Vectorize.vectorize_stmt
    (Map("i", IntImm(0), Cast(i32, Arg(i64, "w")) *~ Cast(i32, Arg(i64, "h")) *~ Cast(i32, Arg(i64, "c")),
        store (
          sadd(load, brightness)
        )
    ))
    "i"
    16

let () =
  Cg_llvm.codegen_to_file "brightness.bc" ([ Buffer "in";  Buffer "out"; Scalar("w", i64); Scalar("h", i64); Scalar("c", i64)], prgm)
  (*Test_runner.main prgm "brightness"*)
