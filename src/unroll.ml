open Ir
open Util


let rec unroll_stmt stmt var width = 
  let unroll s = unroll_stmt s var width in
  (* At this stage we're outside the loop in this variable *)
  match stmt with
    | Map (name, min, max, stmt) when name = var ->
      let substmt i = subs_stmt (Var var) (((Var var) *~ IntImm(width)) +~ IntImm(i)) stmt in      
      let newbody = List.map substmt (0 -- width) in
      Map (name, min /~ IntImm(width), max /~ IntImm(width), Block newbody)
    | Map (name, min, max, stmt) -> Map (name, min, max, unroll stmt)
    | Block l -> Block (List.map unroll l)
    | Store (expr, mr) -> Store (expr, mr)
      
  
