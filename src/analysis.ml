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
    (((op xr yr) + g) mod g, g)

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
  | Call (f, t, args)     -> Call (f, t, List.map mutator args)
  | Let (n, a, b)         -> Let (n, mutator a, mutator b)
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
    | Call (f, t, args) -> Call ((if f = oldname then newname else f), t, List.map subs args)
    | x                 -> mutate_children_in_expr subs x

and prefix_name_expr prefix expr =
  let recurse = prefix_name_expr prefix in
  match expr with 
    | Load (t, b, i)    -> Load (t, prefix ^ b, recurse i)
    | Var (t, n)        -> Var (t, prefix ^ n)
    | Let (n, a, b)     -> Let (prefix ^ n, recurse a, recurse b)
    | Call (f, t, args) -> Call (prefix ^ f, t, List.map recurse args)
    | x                 -> mutate_children_in_expr recurse x  

and prefix_name_stmt prefix stmt =
  let recurse_stmt = prefix_name_stmt prefix in
  let recurse_expr = prefix_name_expr prefix in
  match stmt with
    | For (name, min, n, order, body) ->
      For (prefix ^ name, recurse_expr min, recurse_expr n, order, recurse_stmt body)
    | Block l -> Block (List.map recurse_stmt l)
    | Store (expr, buf, idx) -> 
      Store (recurse_expr expr, prefix ^ buf, recurse_expr idx)
    | Pipeline (name, ty, size, produce, consume) -> 
      Pipeline (prefix ^ name, ty, recurse_expr size, recurse_stmt produce, recurse_stmt consume)      


(* Assuming a program has 1024 expressions, the probability of a collision using a k-bit hash is roughly:

   k = 32: 10^-4
   k = 64: 10^-14
   k = 128: 10^-33
   k = 256: 10^-72

   A tuple of 4 ocaml ints represents at least 124 bits, which should be
   enough to make collisions less likely than being eaten by wolves *)

(* Furthermore, we preserve algebraic properties while hashing, so
   that e.g. hash (a*(b+c)) = hash (a*c + b*a) *)

let hash_int x = (Hashtbl.hash (x+123456), 
                  Hashtbl.hash (x+123457),
                  Hashtbl.hash (x+123458),
                  Hashtbl.hash (x+123459))

let hash_float x = (Hashtbl.hash (x *. 1.01),
                    Hashtbl.hash (x *. 1.02),
                    Hashtbl.hash (x *. 1.03),
                    Hashtbl.hash (x *. 1.04))

let hash_str str = (Hashtbl.hash str, 
                    Hashtbl.hash (str ^ " "),
                    Hashtbl.hash (str ^ "  "),
                    Hashtbl.hash (str ^ "   "))

let hash_combine2 (a, b, c, d) (e, f, g, h) =
  (Hashtbl.hash (a, e), Hashtbl.hash (b, f), Hashtbl.hash (c, g), Hashtbl.hash (d, h))

let hash_combine3 (a, b, c, d) (e, f, g, h) (i, j, k, l) =
  (Hashtbl.hash (a, e, i), Hashtbl.hash (b, f, j), Hashtbl.hash (c, g, k), Hashtbl.hash (d, h, l))  

let hash_combine4 (a, b, c, d) (e, f, g, h) (i, j, k, l) (m, n, o, p) =
  (Hashtbl.hash (a, e, i, m), Hashtbl.hash (b, f, j, n), Hashtbl.hash (c, g, k, o), Hashtbl.hash (d, h, l, p))  

let hash_expand n = (Hashtbl.hash n, Hashtbl.hash n, Hashtbl.hash n, Hashtbl.hash n)

let hash_type = function
  | Int x -> hash_int (x + 100000)
  | UInt x -> hash_int (x + 200000)
  | Float x -> hash_int (x + 300000)
  | IntVector (x, n) -> hash_combine2 (hash_int (x + 400000)) (hash_int (n + 500000))
  | UIntVector (x, n) -> hash_combine2 (hash_int (x + 600000)) (hash_int (n + 700000))
  | FloatVector (x, n) -> hash_combine2 (hash_int (x + 800000)) (hash_int (n + 900000))

let rec hash_expr e =
  match e with
    (* Buffer names, variables, and other strings don't have spaces, so we can pad with spaces to generate different hashes *)
    | IntImm n -> hash_int n
    | UIntImm n -> hash_int n
    | FloatImm n -> hash_float n
    | Cast (t, e) -> hash_combine3 (hash_str "<Cast>") (hash_type t) (hash_expr e)
    | Var (t, str) -> hash_combine2 (hash_type t) (hash_str ("<Var>" ^ str))
    | Let (n, a, b) -> hash_combine4 (hash_str "<Let>") (hash_str n) (hash_expr a) (hash_expr b)
    | Bop (Add, a, b) -> 
      let (a1, a2, a3, a4) = hash_expr a 
      and (b1, b2, b3, b4) = hash_expr b 
      in (a1+b1, a2+b2, a3+b3, a4+b4)
    | Bop (Sub, a, b) -> 
      let (a1, a2, a3, a4) = hash_expr a 
      and (b1, b2, b3, b4) = hash_expr b 
      in (a1-b1, a2-b2, a3-b3, a4-b4)
    | Bop (Mul, a, b) -> 
      let (a1, a2, a3, a4) = hash_expr a 
      and (b1, b2, b3, b4) = hash_expr b 
      in (a1*b1, a2*b2, a3*b3, a4*b4)
    | Bop (op, a, b) -> hash_combine4 (hash_str "<Bop>") (hash_expand op) (hash_expr a) (hash_expr b)
    | Cmp (op, a, b) -> hash_combine4 (hash_str "<Cmp>") (hash_expand op) (hash_expr a) (hash_expr b)
    | And (a, b) -> 
      let (a1, a2, a3, a4) = hash_expr a 
      and (b1, b2, b3, b4) = hash_expr b 
      in (a1 land b1, a2 land b2, a3 land b3, a4 land b4) 
    | Or (a, b) ->
      let (a1, a2, a3, a4) = hash_expr a 
      and (b1, b2, b3, b4) = hash_expr b 
      in (a1 lor b1, a2 lor b2, a3 lor b3, a4 lor b4) 
    | Not a ->
      let (a1, a2, a3, a4) = hash_expr a 
      in (lnot a1, lnot a2, lnot a3, lnot a4)
    | Select (c, a, b) ->
      let (a1, a2, a3, a4) = hash_expr a 
      and (b1, b2, b3, b4) = hash_expr b 
      and (c1, c2, c3, c4) = hash_expr c 
      in ((c1 land a1) lor ((lnot c1) land b1),
          (c2 land a2) lor ((lnot c2) land b2),
          (c3 land a3) lor ((lnot c3) land b3),
          (c4 land a4) lor ((lnot c4) land b4))
    | Load (t, b, i) ->
      hash_combine4 (hash_str "<Load>") (hash_type t) (hash_str b) (hash_expr i)
    | MakeVector l -> 
      List.fold_left hash_combine2 (hash_str "<MakeVector>") (List.map hash_expr l)
    | Broadcast (e, n) ->
      hash_combine3 (hash_str "<Broadcast>") (hash_expr e) (hash_int n)
    | Ramp (b, s, n) ->
      hash_combine4 (hash_str "<Ramp>") (hash_expr b) (hash_expr s) (hash_int n)
    | ExtractElement (a, b) ->
      hash_combine3 (hash_str "<ExtractElement>") (hash_expr a) (hash_expr b)
    | Call (name, ty, args) ->
      List.fold_left hash_combine2 (hash_combine2 (hash_str "<Call>") (hash_type ty)) (List.map hash_expr args)
