open Ir
open Analysis

let rec constant_fold_expr expr = 
  let recurse = constant_fold_expr in
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
        | (Sub, IntImm 0, x) | (Sub, UIntImm 0, x) | (Sub, FloatImm 0.0, x) 
        | (Sub, x, IntImm 0) | (Sub, x, UIntImm 0) | (Sub, x, FloatImm 0.0) 
        | (Add, IntImm 0, x) | (Add, UIntImm 0, x) | (Add, FloatImm 0.0, x) 
        | (Add, x, IntImm 0) | (Add, x, UIntImm 0) | (Add, x, FloatImm 0.0) 
        | (Mul, IntImm 1, x) | (Mul, UIntImm 1, x) | (Mul, FloatImm 1.0, x) 
        | (Mul, x, IntImm 1) | (Mul, x, UIntImm 1) | (Mul, x, FloatImm 1.0) 
        | (Div, x, IntImm 1) | (Div, x, UIntImm 1) | (Div, x, FloatImm 1.0) -> x

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
  | For (var, min, n, order, stmt) ->
    For (var, constant_fold_expr min, constant_fold_expr n, order, constant_fold_stmt stmt)
  | Block l ->
    Block (List.map constant_fold_stmt l)
  | Store (e, buf, idx) ->
    Store (constant_fold_expr e, buf, constant_fold_expr idx)
  | Pipeline (n, ty, size, produce, consume) -> 
    Pipeline (n, ty, constant_fold_expr size,
              constant_fold_stmt produce,
              constant_fold_stmt consume)
