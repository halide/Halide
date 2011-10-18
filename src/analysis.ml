open Ir
open Ir_printer
open Util

exception ModulusOfNonInteger 
exception ModulusOfMakeVector
exception ModulusOfBroadcast
exception ModulusOfRamp

let rec gcd x y = match (x, y) with
  | (0, n) | (n, 0) -> n
  | (a, b) -> gcd b (a mod b)

let rec binop_remainder_modulus x y op = 
    let (xr, xm) = compute_remainder_modulus x in
    let (yr, ym) = compute_remainder_modulus y in
    let g = gcd xm ym in
    if g = 0 then (0, 0) else (((op xr yr) + g) mod g, g)

and compute_remainder_modulus = function
  | IntImm(x) | UIntImm(x) -> (x, 0)
  | Cast(t, x) -> compute_remainder_modulus x 
  | Bop(Add, x, y) -> binop_remainder_modulus x y ( + )
  | Bop(Sub, x, y) -> binop_remainder_modulus x y ( - )
  | Bop(Mul, x, y) -> 
    let (xr, xm) = compute_remainder_modulus x in
    let (yr, ym) = compute_remainder_modulus y in
    if xm = 0 then
      (xr * yr, xr * ym)
    else if ym = 0 then
      (yr * xr, yr * xm)
    else 
      binop_remainder_modulus x y ( * )
  | Bop _ | Load _ | Var _ | ExtractElement _ -> (0, 1)
  | MakeVector _ -> raise ModulusOfMakeVector
  | Broadcast _ -> raise ModulusOfBroadcast
  | Ramp _ -> raise ModulusOfRamp
  | e -> raise ModulusOfNonInteger

(* Reduces an expression modulo n *)
(* returns an integer in [0, m-1], or m if unknown *)
let reduce_expr_modulo expr n = 
  let (r, m) = compute_remainder_modulus expr in
  if (m mod n = 0) then Some (r mod n) else None


(* Expression search *)
let expr_contains_expr query e =
  let rec recurse = function
    | x when x = query -> true
      
    | Not e
    | Load (_, _, e)
    | Broadcast (e, _)
    | Cast (_, e)           -> recurse e

    | Ramp (a, b, _)
    | ExtractElement (a, b)
    | Bop (_, a, b)
    | Cmp (_, a, b)
    | Let (_, a, b)
    | And (a, b)
    | Or (a, b)             -> recurse a or recurse b

    | Select (c, a, b)      -> recurse c or recurse a or recurse b

    | MakeVector l
    | Call (_, _, l)        -> List.exists recurse l

    | x -> false
  in recurse e

let fold_children_in_expr mutator combiner base_case = function
  | Not e
  | Load (_, _, e)
  | Broadcast (e, _)
  | Cast (_, e)           -> mutator e
    
  | Ramp (a, b, _)
  | ExtractElement (a, b)
  | Bop (_, a, b)
  | Cmp (_, a, b)
  | Let (_, a, b)
  | And (a, b)
  | Or (a, b)             -> combiner (mutator a) (mutator b)
    
  | Select (c, a, b)      -> combiner (combiner (mutator c) (mutator a)) (mutator b)
    
  | MakeVector l
  | Call (_, _, l)        -> List.fold_left combiner base_case (List.map mutator l)

  | Debug (e, _, l)       -> List.fold_left combiner (mutator e) (List.map mutator l)

  | x                     -> base_case

let fold_children_in_stmt expr_mutator stmt_mutator combiner = function
  | For (name, min, n, order, body) ->
    combiner (combiner (expr_mutator min) (expr_mutator n)) (stmt_mutator body)
  | Block l -> List.fold_left combiner (stmt_mutator (List.hd l)) (List.map stmt_mutator (List.tl l))
  | Store (expr, buf, idx) -> combiner (expr_mutator expr) (expr_mutator idx)
  | Pipeline (name, ty, size, produce, consume) -> 
    combiner (combiner (expr_mutator size) (stmt_mutator produce)) (stmt_mutator consume)

(* E.g:
let rec stmt_contains_zero stmt =
  let rec expr_mutator = function
    | IntImm 0 -> true
    | x -> fold_children_in_expr expr_mutator (or) false x
  fold_children_in_stmt expr_mutator stmt_contains_zero (or)
*)

let expr_contains_expr query e =
  if (query = e) then 
    true 
  else
    fold_children_in_expr (expr_contains_expr query) (or) false e

let mutate_children_in_expr mutator = function
  | Cast (t, e)           -> Cast (t, mutator e)
  | Bop (op, a, b)        -> Bop (op, mutator a, mutator b)
  | Cmp (op, a, b)        -> Cmp (op, mutator a, mutator b)
  | And (a, b)            -> And (mutator a, mutator b)
  | Or (a, b)             -> Or (mutator a, mutator b)
  | Not a                 -> Not (mutator a)
  | Select (c, a, b)      -> Select (mutator c, mutator a, mutator b)
  | Load (t, b, i)        -> Load (t, b, mutator i)
  | MakeVector l          -> MakeVector (List.map mutator l)
  | Broadcast (a, n)      -> Broadcast (mutator a, n)
  | Ramp (b, s, n)        -> Ramp (mutator b, mutator s, n)
  | ExtractElement (a, b) -> ExtractElement (mutator a, mutator b)
  | Call (t, f, args)     -> Call (t, f, List.map mutator args)
  | Let (n, a, b)         -> Let (n, mutator a, mutator b)
  | Debug (e, fmt, args)  -> Debug (mutator e, fmt, List.map mutator args)
  | x -> x
    
let mutate_children_in_stmt expr_mutator stmt_mutator = function
  | For (name, min, n, order, body) ->
    For (name, expr_mutator min, expr_mutator n, order, stmt_mutator body)
  | Block l -> Block (List.map stmt_mutator l)
  | Store (expr, buf, idx) -> 
    Store (expr_mutator expr, buf, expr_mutator idx)
  | Pipeline (name, ty, size, produce, consume) -> 
    Pipeline (name, ty, expr_mutator size, stmt_mutator produce, stmt_mutator consume)

(* Expression subsitution *)
let rec subs_stmt oldexpr newexpr stmt =
  mutate_children_in_stmt (subs_expr oldexpr newexpr) (subs_stmt oldexpr newexpr) stmt

and subs_expr oldexpr newexpr expr = 
  if expr = oldexpr then 
    newexpr 
  else 
    mutate_children_in_expr (subs_expr oldexpr newexpr) expr
    
and subs_name_stmt oldname newname stmt =
  let subs = subs_name_stmt oldname newname in
  let subs_expr = subs_name_expr oldname newname in
  match stmt with
    | For (name, min, n, order, body) ->
      For ((if name = oldname then newname else name), 
        subs_expr min, subs_expr n, order, subs body)
    | Block l -> Block (List.map subs l)
    | Store (expr, buf, idx) -> 
      Store (subs_expr expr,
             (if buf = oldname then newname else buf),
             subs_expr idx)
    | Pipeline (name, ty, size, produce, consume) -> 
      Pipeline ((if name = oldname then newname else name), 
                ty, subs_expr size, subs produce, subs consume)      

and subs_name_expr oldname newname expr =
  let subs = subs_name_expr oldname newname in
  match expr with 
    | Load (t, b, i)    -> Load (t, (if b = oldname then newname else b), subs i)
    | Var (t, n)        -> Var (t, if n = oldname then newname else n)
    | Let (n, a, b)     -> Let ((if n = oldname then newname else n), subs a, subs b)
    | Call (t, f, args) -> Call (t, (if f = oldname then newname else f), List.map subs args)
    | x                 -> mutate_children_in_expr subs x

and prefix_non_global prefix name =
  let is_global = String.contains name '.' in
  if is_global then name else prefix ^ name

and prefix_name_expr prefix expr =
  let recurse = prefix_name_expr prefix in
  match expr with 
    | Load (t, b, i)    -> Load (t, prefix_non_global prefix b, recurse i)
    | Var (t, n)        -> Var (t, prefix_non_global prefix n)
    | Let (n, a, b)     -> Let (prefix ^ n, recurse a, recurse b)
    | Call (t, f, args) -> Call (t, prefix_non_global prefix f, List.map recurse args)
    | x                 -> mutate_children_in_expr recurse x  

and prefix_name_stmt prefix stmt =
  let recurse_stmt = prefix_name_stmt prefix in
  let recurse_expr = prefix_name_expr prefix in
  match stmt with
    | For (name, min, n, order, body) ->
      For (prefix ^ name, recurse_expr min, recurse_expr n, order, recurse_stmt body)
    | Block l -> Block (List.map recurse_stmt l)
    | Store (expr, buf, idx) -> 
      Store (recurse_expr expr, prefix_non_global prefix buf, recurse_expr idx)
    | Pipeline (name, ty, size, produce, consume) -> 
      Pipeline (prefix ^ name, ty, recurse_expr size, recurse_stmt produce, recurse_stmt consume)      
