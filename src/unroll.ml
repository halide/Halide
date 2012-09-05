open Ir
open Util
open Split
open Analysis

let rec unroll_stmt var stmt = 
  let unroll s = unroll_stmt var s in
  match stmt with
    (* At this stage we're outside the loop in this variable *)
    | For (v, min, n, order, substmt) when v = var ->
      begin match n with
        | IntImm size ->
          let gen_stmt i = subs_expr_in_stmt (Var (i32, var)) (min +~ (IntImm i)) substmt in
          For (v, IntImm 0, IntImm 1, false, Block (List.map gen_stmt (0 -- size)))
        | _ -> failwith "Can't unroll for with non-constant bounds"
      end
    | For (name, min, n, order, stmt) -> For (name, min, n, order, unroll stmt)
    | Block l -> Block (List.map unroll l)
    | Pipeline (name, ty, size, produce, consume) -> Pipeline (name, ty, size, unroll produce, unroll consume)
    (* Anything that does not contain a sub-statement is unchanged *)
    | x -> x
      
  
