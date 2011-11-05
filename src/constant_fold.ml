open Ir
open Analysis

let rec constant_fold_expr expr = 
  let recurse = constant_fold_expr in
  
  let is_const_zero = function
    | IntImm 0 
    | UIntImm 0
    | FloatImm 0.0
    | Broadcast (IntImm 0, _)
    | Broadcast (UIntImm 0, _)
    | Broadcast (FloatImm 0.0, _) -> true
    | _ -> false
  and is_const_one = function
    | IntImm 1
    | UIntImm 1
    | FloatImm 1.0
    | Broadcast (IntImm 1, _)
    | Broadcast (UIntImm 1, _)
    | Broadcast (FloatImm 1.0, _) -> true
    | _ -> false
  in

  match expr with
    (* Ignoring const-casts for now, because we can't represent immediates of arbitrary types *)
    | Cast (t, e) -> Cast (t, recurse e) 

    (* basic binary ops *)
    | Bop (op, a, b) ->
      begin match (op, recurse a, recurse b) with
        | (_, IntImm   x, IntImm   y) -> IntImm   (caml_iop_of_bop op x y)
        | (_, UIntImm  x, UIntImm  y) -> UIntImm  (caml_iop_of_bop op x y)
        | (_, FloatImm x, FloatImm y) -> FloatImm (caml_fop_of_bop op x y)

        (* Identity operations. These are not strictly constant
           folding, but they tend to come up at the same time *)
        | (Add, x, y) when is_const_zero x -> y
        | (Add, x, y) when is_const_zero y -> x
        | (Sub, x, y) when is_const_zero y -> x
        | (Mul, x, y) when is_const_one x -> y
        | (Mul, x, y) when is_const_one y -> x
        | (Mul, x, y) when is_const_zero x -> x
        | (Mul, x, y) when is_const_zero y -> y
        | (Div, x, y) when is_const_one y -> x

        (* op (Ramp, Broadcast) should be folded into the ramp *)
        | (Add, Broadcast (e, _), Ramp (b, s, n)) 
        | (Add, Ramp (b, s, n), Broadcast (e, _)) -> Ramp (recurse (b +~ e), s, n)
        | (Sub, Ramp (b, s, n), Broadcast (e, _)) -> Ramp (recurse (b -~ e), s, n)
        | (Mul, Broadcast (e, _), Ramp (b, s, n)) 
        | (Mul, Ramp (b, s, n), Broadcast (e, _)) -> Ramp (recurse (b *~ e), recurse (s *~ e), n)
        | (Div, Ramp (b, s, n), Broadcast (e, _)) -> Ramp (recurse (b /~ e), recurse (s /~ e), n)

        (* op (Broadcast, Broadcast) should be folded into the broadcast *)
        | (Add, Broadcast (a, n), Broadcast(b, _)) -> Broadcast (recurse (a +~ b), n)
        | (Sub, Broadcast (a, n), Broadcast(b, _)) -> Broadcast (recurse (a -~ b), n)
        | (Mul, Broadcast (a, n), Broadcast(b, _)) -> Broadcast (recurse (a *~ b), n)
        | (Div, Broadcast (a, n), Broadcast(b, _)) -> Broadcast (recurse (a /~ b), n)

        | (op, x, y) -> Bop (op, x, y)
      end

    (* comparison *)
    | Cmp (op, a, b) ->
      begin match (recurse a, recurse b) with
        | (IntImm   x, IntImm   y)
        | (UIntImm  x, UIntImm  y) -> UIntImm (if caml_op_of_cmp op x y then 1 else 0)
        | (FloatImm x, FloatImm y) -> UIntImm (if caml_op_of_cmp op x y then 1 else 0)
        | (x, y) -> Cmp (op, x, y)
      end

    (* logical *)
    | And (a, b) ->
      begin match (recurse a, recurse b) with
        | (UIntImm 0, _)
        | (_, UIntImm 0) -> UIntImm 0
        | (UIntImm 1, x)
        | (x, UIntImm 1) -> x
        | (x, y) -> And (x, y)
      end
    | Or (a, b) ->
      begin match (recurse a, recurse b) with
        | (UIntImm 1, _)
        | (_, UIntImm 1) -> UIntImm 1
        | (UIntImm 0, x)
        | (x, UIntImm 0) -> x
        | (x, y) -> Or (x, y)
      end
    | Not a ->
      begin match recurse a with
        | UIntImm 0 -> UIntImm 1
        | UIntImm 1 -> UIntImm 0
        | x -> Not x
      end
    | Select (c, a, b) ->
      begin match (recurse c, recurse a, recurse b) with
        | (UIntImm 0, _, x) -> x
        | (UIntImm 1, x, _) -> x
        | (c, x, y) -> Select (c, x, y)
      end
    | Load (t, buf, idx) -> Load (t, buf, recurse idx)
    | MakeVector l -> MakeVector (List.map recurse l)
    | Broadcast (e, n) -> Broadcast (recurse e, n)
    | Ramp (b, s, n) -> Ramp (recurse b, recurse s, n)
    | ExtractElement (a, b) -> ExtractElement (recurse a, recurse b)

    (* Immediates are unchanged *)
    | x -> x

let rec constant_fold_stmt = function
  | For (var, min, size, order, stmt) ->
      (* Remove trivial for loops *)
      let min = constant_fold_expr min in 
      let size = constant_fold_expr size in
      if size = IntImm 1 or size = UIntImm 1 then
        constant_fold_stmt (subs_stmt (Var (i32, var)) min stmt)
      else
        For (var, min, size, order, constant_fold_stmt stmt)
  | Block l ->
      Block (List.map constant_fold_stmt l)
  | Store (e, buf, idx) ->
      Store (constant_fold_expr e, buf, constant_fold_expr idx)
  | Pipeline (n, ty, size, produce, consume) -> 
      Pipeline (n, ty, constant_fold_expr size,
                constant_fold_stmt produce,
                constant_fold_stmt consume)
  | Print (p, l) -> 
      Print (p, List.map constant_fold_expr l)
