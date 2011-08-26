open Ir
open Util
open List
open Option

let vectorize_expr expr var width = 

  let is_vector e = match val_type_of_expr with
    | IntVector _ | UIntVector | FloatVector -> true
    | _ -> false
  
  and expand v s = if (is_vector v) then v else match s with 
    | 0 -> Broadcast (v, width)
    | _ -> 
      let range x = IntImm (s * x) in
      Bop (Add, Broadcast (v, width), MakeVector (map range (0 -- width)))      

  and reconcile a sa b sb = match (is_vector a, is_vector b) with
    | (false, false) when (sa = sb) -> (a, b)
    | _ -> (expand a sa, expand b sb)

  and map_bop op x y = match (x, y) with
    | (Some a, Some b) -> Some (op a b)
    | _ -> None

  and vec expr = match expr with
    | IntImm x | UIntImm x -> (expr, 0, Some x)
    | FloatImm _ -> (expr, 0, None)
    | Cast (t, expr) -> 
      let (v, s, c) = vec expr in
      if (is_vector v) then (Cast ((vector_of_val_type t width), v), 0, c)
      else (Cast (t, v), s, c)
    | Bop (op, a, b) -> 
      begin
        let (va, sa, ca) = vec a and (vb, sb, cb) = vec b in
        match (op, is_vector va, is_vector vb) with
          (* combine vector with linear *)
          | (_, true, false) -> (Bop (op, va, expand vb sb), 0, None)
          | (_, false, true) -> (Bop (op, expand va sa, vb), 0, None)
            
          (* add two linear (or two vectors) *)
          | (Add, _, _) -> (Bop (Add, va, vb), sa + sb, map_bop (+) ca cb)
          | (Sub, _, _) -> (Bop (Sub, va, vb), sa - sb, map_bop (-) ca cb)
            
          (* multiply linear by constant *)
          | (Mul, false, false) when (is_some ca || is_some cb) -> 
            begin
              match (ca, cb) with
                | (Some ca, None) -> (Bop (Mul, va, vb), ca * sb, None)
                | (None, Some cb) -> (Bop (Mul, va, vb), sa * cb, None)
                | (Some ca, Some cb) -> (Bop (Mul, va, vb), 0, Some (ca * cb))
            end            
              
          (* constant division *)
          | (Div, false, false) when (is_some ca && is_some cb) ->
            (Bop (Div, va, vb), 0, Some ((get ca) / (get cb)))

          (* give up and expand both sides *)
          | _ -> (Bop (op, expand va sa, expand vb sb), 0, None)
      end
    | Cmp (op, a, b) ->
        let (va, sa, _) = vec a and (vb, sb, _) = vec b in
        let (rva, rvb) = reconcile va sa vb sb in
        (Cmp(op, rva, rvb), 0, None)
    | And (a, b) ->
      let (va, sa, _) = vec a and (vb, sb, _) = vec b in
      (And (reconcile va sa vb sb), 0, None)
    | Or (a, b) ->
      let (va, sa, _) = vec a and (vb, sb, _) = vec b in
      (Or (reconcile va sa vb sb), 0, None)
    | Not a -> 
      let (va, _, _) = vec a in
      (Not va, 0, None)
    | Select (c, a, b) ->
      let (va, sa, ca) = vec a and (vb, sb, cb) = vec b and (vc, sc, cc) = vec c in
      if (!(is_vector va || is_vector vb || is_vector vc) && (sa = sb)) then
        (* Both linear (or scalar) *)
        (* Assume constant selects have been folded *)
        (Select (vc, va, vb), sa, None)
      else
        (* Give up and expand all three *)
        (Select (expand vc 0, expand va sa, expand vb sb), 0, None)
    | Load (t, mr) ->
      let (vidx, sidx, cidx) = vec mr.idx in
      if (is_vector vidx) then 
        (Load (vector_of_val_type t width, {buf = mr.buf; idx = vidx}), 0, None)
      else begin
        match sidx with
          | 0 -> (Load (t, {buf = mr.buf; idx = vidx}), 0, None)
          | 1 -> (Load (vector_of_val_type t width, {buf = mr.buf; idx = vidx}), 0, None)
          (* strided load. TODO: handle this. For now just expand the vector *)
          | _ -> (Load (vector_of_val_type t width, {buf = mr.buf; idx = vidx}), 0, None)
      end
    | _ -> raise Wtf("Can't vectorize vector code")
  in
  let (ve, vs, _) = vec expr in
  expand ve vs


(* TODO: vectorize statement *)
  

