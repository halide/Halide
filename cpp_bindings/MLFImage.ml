open Ir
open Ir_printer

let _ = 
  Callback.register "makeIntImm" (fun a -> IntImm a);
  Callback.register "makeAdd" (fun a b -> Bop (Add, a, b));
  Callback.register "makeVar" (fun a -> Var a);
  Callback.register "doPrint" (fun a -> Printf.printf "%s\n%!" (string_of_expr a))
