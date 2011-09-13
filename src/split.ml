open Ir
open Analysis
open Util

let rec split_stmt (var:string) (outer:string) (inner:string) (n:int) (stmt:stmt) =
  let recurse = split_stmt var outer inner n 
  and patch = subs_stmt (Var var) (((Var outer) *~ (IntImm n)) +~ (Var inner)) in

  match stmt with
  | Map (v, min, max, stmt) when v = var ->
    if (reduce_expr_modulo min n <> Some 0 ||
        reduce_expr_modulo max n <> Some 0) then 
      raise (Wtf("Couldn't tell if loop bounds are a multiple of n"))
    else 
      Map (outer, min /~ (IntImm n), max /~ (IntImm n), 
           Map (inner, (IntImm 0), (IntImm n), 
                patch stmt))
  | Map (v, min, max, stmt) -> Map (v, min, max, recurse stmt)
  | Block l -> Block (List.map recurse l)
  (* Anything that doesn't contain a sub-statement doesn't need to be touched *)
  | x -> x

