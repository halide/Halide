open Ir
open Util
open Split
open Analysis

let rec unroll_stmt var stmt = 
  let unroll s = unroll_stmt var s in
  match stmt with
    (* At this stage we're outside the loop in this variable *)
    | For (v, min, n, order, substmt) when v = var ->
      begin match (min, n) with
        (* TODO: this could actually work with only cost size - min doesn't need to be const *)
        | (IntImm a, IntImm b) 
        | (IntImm a, UIntImm b) 
        | (UIntImm a, IntImm b) 
        | (UIntImm a, UIntImm b) ->
          let gen_stmt i = subs_stmt (Var (i32, var)) (IntImm i) substmt in
          Block (List.map gen_stmt (a -- (a+b)))
        | _ -> raise (Wtf "Can't unroll for with non-constant bounds")
      end
    | For (name, min, n, order, stmt) -> For (name, min, n, order, unroll stmt)
    | Block l -> Block (List.map unroll l)
    (* Anything that does not contain a sub-statement is unchanged *)
    | x -> x
      
  
