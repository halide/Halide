open Ir

let dom nm rn = { name = nm; range = rn }

let xdom = dom "x" (0,0)

let x = Var("x")

let outbuf = 2
let inbuf = 1

let inref = { buf=inbuf; idx=UIntImm(0) }
let outref = { buf=outbuf; idx=UIntImm(0) }

let prgm = Store( Add( u32, ( IntImm( 0xDEADBEEF ), Load( u32, inref ) ) ), outref )

let () =
  Printf.printf "%s\n" (Ir_printer.string_of_stmt prgm);
  (*Cg_llvm.codegen_to_file "cg_test.bc" prgm*)
  let (m,f) = Cg_llvm.codegen_to_ocaml_callable prgm in

    Llvm.dump_module m
