open Ir
open Analysis
open Util

let rec split_stmt (var:string) (outer:string) (inner:string) (factor:int) (stmt:stmt) =
  let recurse = split_stmt var outer inner factor
  and patch = subs_expr_in_stmt (Var (i32, var)) (((Var (i32, outer)) *~ (IntImm factor)) +~ (Var (i32, inner))) in

  match stmt with
  | For (v, min, n, order, stmt) when v = var ->
    if (reduce_expr_modulo min factor <> Some 0 ||
        reduce_expr_modulo n factor <> Some 0) then 
      failwith "Couldn't tell if loop bounds are a multiple of n"
    else 
      For (outer, min /~ (IntImm factor), n /~ (IntImm factor), order, 
           For (inner, (IntImm 0), (IntImm factor), order, 
                patch stmt))
  | For (v, min, n, order, stmt) -> For (v, min, n, order, recurse stmt)
  | Block l -> Block (List.map recurse l)
  (* Anything that doesn't contain a sub-statement doesn't need to be touched *)
  | x -> x

