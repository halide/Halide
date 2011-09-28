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
    | And (a, b)
    | Or (a, b)             -> recurse a or recurse b

    | Select (c, a, b)      -> recurse c or recurse a or recurse b

    | MakeVector l
    | Call (_, _, l)        -> List.exists recurse l

    | x -> false
  in recurse e

    
(* Expression subsitution *)
let rec subs_stmt oldexpr newexpr = function
  | Map (name, min, max, stmt) -> Map (name, 
                                       subs_expr oldexpr newexpr min,
                                       subs_expr oldexpr newexpr max,
                                       subs_stmt oldexpr newexpr stmt)
  | Block l -> Block (List.map (subs_stmt oldexpr newexpr) l)
  | Store (expr, buf, idx) -> Store (subs_expr oldexpr newexpr expr, buf, subs_expr oldexpr newexpr idx)
  | Let (name, ty, size, produce, consume) -> Let (name, ty,
                                                   subs_expr oldexpr newexpr size,
                                                   subs_stmt oldexpr newexpr produce,
                                                   subs_stmt oldexpr newexpr consume)

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
        | Load (t, b, i)        -> Load (t, b, subs i)
        | MakeVector l          -> MakeVector (List.map subs l)
        | Broadcast (a, n)      -> Broadcast (subs a, n)
        | Ramp (b, s, n)        -> Ramp (subs b, subs s, n)
        | ExtractElement (a, b) -> ExtractElement (subs a, subs b)
        | Call (f, t, args)     -> Call (f, t, List.map subs args)
        | x -> x
    
and subs_buffer_name_stmt oldname newname stmt =
  let subs = subs_buffer_name_stmt oldname newname in
  let subs_expr = subs_buffer_name_expr oldname newname in
  match stmt with
    | Map (name, min, max, body) -> Map (name, subs_expr min, subs_expr max, subs body)
    | Block l -> Block (List.map subs l)
    | Store (expr, buf, idx) -> 
      Store (subs_expr expr,
             (if buf = oldname then newname else buf),
             subs_expr idx)
    | Let (name, ty, size, produce, consume) -> Let ((if name = oldname then newname else name), 
                                                     ty, 
                                                     subs_expr size,
                                                     subs produce,
                                                     subs consume)      

and subs_buffer_name_expr oldname newname expr =
  let subs = subs_buffer_name_expr oldname newname in
  match expr with 
    | Cast (t, e)           -> Cast (t, subs e)
    | Bop (op, a, b)        -> Bop (op, subs a, subs b)
    | Cmp (op, a, b)        -> Cmp (op, subs a, subs b)
    | And (a, b)            -> And (subs a, subs b)
    | Or (a, b)             -> Or (subs a, subs b)
    | Not a                 -> Not (subs a)
    | Select (c, a, b)      -> Select (subs c, subs a, subs b)
    | Load (t, b, i)        -> Load (t, (if b = oldname then newname else b), subs i)
    | MakeVector l          -> MakeVector (List.map subs l)
    | Broadcast (a, n)      -> Broadcast (subs a, n)
    | Ramp (b, s, n)        -> Ramp (subs b, subs s, n)
    | ExtractElement (a, b) -> ExtractElement (subs a, subs b)
    | x -> x


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
    | Var str -> hash_str ("var" ^ str)
    | Arg (t, s) -> hash_combine3 (hash_str "<Arg>") (hash_type t) (hash_str s)
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
