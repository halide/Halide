open Ir
open Util

let rec subs_stmt oldexpr newexpr = function
  | Map (name, min, max, stmt) -> Map (name, 
                                       subs_expr oldexpr newexpr min,
                                       subs_expr oldexpr newexpr max,
                                       subs_stmt oldexpr newexpr stmt)
  | Block l -> Block (List.map (subs_stmt oldexpr newexpr) l)
  | Store (expr, mr) -> Store (subs_expr oldexpr newexpr expr, {buf=mr.buf; idx=subs_expr oldexpr newexpr mr.idx})

and subs_expr oldexpr newexpr expr = 
    let subs = subs_expr oldexpr newexpr in
    if expr = oldexpr then newexpr else
      match expr with
        | Cast (t, e)           -> Cast (t, subs e)
        | Bop (op, a, b)        -> Bop (op, subs a, subs b)
        | Cmp (op, a, b)        -> Cmp (op, subs a, subs b)
        | And (a, b)            -> And (subs a, subs b)
        | Or (a, b)             -> Or (subs a, subs b)
        | Not a                 -> Not (subs a)
        | Select (c, a, b)      -> Select (subs c, subs a, subs b)
        | Load (t, mr)          -> Load (t, {buf = mr.buf; idx = subs mr.idx})
        | MakeVector l          -> MakeVector (List.map subs l)
        | Broadcast (a, n)      -> Broadcast (subs a, n)
        | ExtractElement (a, b) -> ExtractElement (subs a, subs b)
        | x -> x
    

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
      
  
