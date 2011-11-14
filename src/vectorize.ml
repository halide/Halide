open Ir
open Util
open List
open Analysis

let expand e width = 
  if (is_scalar e) then Broadcast (e, width) else e

(* Substitute var for something that might be a vector in expr and propagate the vectorness upwards *)
let vector_subs_expr (var:string) (replacement:expr) (expr:expr) =
  let width = vector_elements (val_type_of_expr replacement) in
  if width = 1 then
    (* If you pass a scalar, vector_subs_expr just acts as subs_expr *)
    subs_expr (Var (i32, var)) replacement expr
  else

    let expand e = expand e width in
    let rec vec expr = match expr with
      | x when is_scalar x && not (expr_contains_expr (Var (i32, var)) expr) -> x
          
      | Cast (t, expr) -> Cast (vector_of_val_type t width, vec expr)
          
      | Bop (op, a, b) -> 
          let va = vec a and vb = vec b in
          begin match (op, va, vb) with
            | (Add, Ramp (ba, sa, _), Ramp (bb, sb, _)) -> Ramp (ba +~ bb, sa +~ sb, width)
            | (Sub, Ramp (ba, sa, _), Ramp (bb, sb, _)) -> Ramp (ba -~ bb, sa -~ sb, width)
            | (Mul, Ramp (b, s, _), x) -> Ramp (b *~ x, s *~ x, width)
            | (Mul, x, Ramp (b, s, _)) -> Ramp (x *~ b, x *~ s, width)
            | (Div, Ramp (b, s, _), x) -> Ramp (b /~ x, s /~ x, width)
            | (Add, Ramp (b, s, _), x) -> Ramp (b +~ x, s, width)
            | (Add, x, Ramp (b, s, _)) -> Ramp (x +~ b, s, width)
            | (Sub, Ramp (b, s, _), x) -> Ramp (b -~ x, s, width)
            | (Sub, x, Ramp (b, s, _)) -> Ramp (x -~ b, (Cast (val_type_of_expr s, IntImm 0)) -~ s, width)
            | _ -> Bop (op, expand va, expand vb)
          end
            
      (* Cmp/And/Or/Not trivially expand:
       * vectorize both operands iff either is a vector *)
      | Cmp (op, a, b) -> Cmp (op, expand (vec a), expand (vec b))
      | And (a, b)     -> And (expand (vec a), expand (vec b))
      | Or (a, b)      -> Or (expand (vec a), expand (vec b))
      | Not (a)        -> Not (vec a)
          
      | Let (name, a, b) -> Let (name, expand (vec a), expand (vec b))
          
      | Select (c, a, b) ->
          let va = vec a and vb = vec b and vc = vec c in
          if is_scalar vc then
            (* Condition is scalar *)
            match (va, vb) with
              (* Scalar selection between ramps of matching stride we can handle specially *)
              | (Ramp (ba, sa, _), Ramp (bb, sb, _)) when sa = sb -> 
                  Ramp (Select (c, ba, bb), sa, width)
              | _ -> Select (c, expand va, expand vb)
          else
            (* Condition is a vector *)
            Select (vc, expand va, expand vb)
              
      | Load (t, buf, idx) -> Load (vector_of_val_type t width, buf, vec idx)
      (* TODO: detect calls to extern functions, and replicate them instead. *)
      (* Currently requires the environment to find out...*)
      | Call (t, f, args) -> Call (vector_of_val_type t width, f, List.map vec args)     

      | Var (t, name) -> assert (name = var && t = i32); replacement

      | Debug (e, prefix, args) -> Debug (vec e, prefix, List.map vec args)
          
      | _ -> raise (Wtf("Can't vectorize vector code"))
    in vec expr

let vectorize_expr (var:string) (min:expr) (width:int) (expr:expr) = 
  vector_subs_expr var (Ramp (min, IntImm 1, width)) expr

let rec vectorize_stmt var stmt =
  let rec vectorize_stmt_inner (min:expr) (width:int) stmt =
    let vec = vectorize_stmt_inner min width 
    and vec_expr = vectorize_expr var min width in
    match stmt with
      | For (v, min, n, order, stmt) -> For (v, min, n, order, vec stmt)
      | Block l -> Block (map vec l)
      | Store (expr, buf, idx) -> Store (vec_expr expr, buf, vec_expr idx)
      | Pipeline _ -> raise (Wtf "Can't vectorize an inner pipeline (yet?)")
  in
  match stmt with        
    | For (name, min, n, order, stmt) when name = var ->
      assert (not order); (* Doesn't make sense to vectorize ordered For *)
      begin match n with
        | IntImm size
        | UIntImm size ->
          For (name, IntImm 0, IntImm 1, false, vectorize_stmt_inner min size stmt)
        | _ -> raise (Wtf "Can't vectorize map with non-constant size")
      end
    | For (name, min, n, order, stmt) -> For (name, min, n, order, vectorize_stmt var stmt)
    | Block l -> Block (map (vectorize_stmt var) l)
    | Pipeline (name, ty, size, produce, consume) -> 
      Pipeline (name, ty, size,
                vectorize_stmt var produce,
                vectorize_stmt var consume)
    (* Anything that doesn't contain a sub-statement is unchanged *)
    | x -> x
