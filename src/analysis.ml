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

and compute_remainder_modulus expr =   
  let (m, r) = match expr with
  | IntImm(x) | UIntImm(x) -> (x, 0)
  | Cast(t, x) ->  (* TODO: this is dubious *)
    begin match val_type_of_expr x with
      | Int _ | UInt _ -> compute_remainder_modulus x 
      | _ -> (0, 1)
    end
  | Bop(Add, x, y) -> binop_remainder_modulus x y ( + )
  | Bop(Sub, x, y) -> binop_remainder_modulus x y ( - )
  | Bop(Mul, x, y) -> 
    let (xr, xm) = compute_remainder_modulus x in
    let (yr, ym) = compute_remainder_modulus y in
    if xm = 0 then
      (xr * yr, xr * ym)
    else if ym = 0 then
      (yr * xr, yr * xm)
    else if (xr, yr) = (0, 0) then
      (0, xm*ym)
    else if xr = 0 then
      (0, xm)
    else if yr = 0 then
      (0, ym) 
    else 
      binop_remainder_modulus x y ( * )
  | Bop _ | Load _ | Var _ | ExtractElement _ -> (0, 1)
  | Debug (e, _, _) -> compute_remainder_modulus e
  | MakeVector _ -> raise ModulusOfMakeVector
  | Broadcast _ -> raise ModulusOfBroadcast
  | Ramp _ -> raise ModulusOfRamp
  | Call (_, _, _, _) -> (0, 1)
  | e -> 
      Printf.printf "%s\n%!" (string_of_expr expr);
      raise ModulusOfNonInteger
  in
  (* Printf.printf "%s -> %d %d\n" (string_of_expr expr) m r; *)
  (m, r)

(* Reduces an expression modulo n *)
(* returns an integer in [0, m-1], or m if unknown *)
let reduce_expr_modulo expr n = 
  let (r, m) = compute_remainder_modulus expr in
  (* Printf.printf "%s mod %d = (%d, %d)\n" (string_of_expr expr) n r m; *)
  if (m mod n = 0) then Some (r mod n) else None

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
  | Call (_, _, _, l)     -> List.fold_left combiner base_case (List.map mutator l)

  | Debug (e, _, l)       -> List.fold_left combiner (mutator e) (List.map mutator l)

  | x                     -> base_case

let fold_children_in_stmt expr_mutator stmt_mutator combiner = function
  | For (name, min, n, order, body) ->
    combiner (combiner (expr_mutator min) (expr_mutator n)) (stmt_mutator body)
  | Block l -> List.fold_left combiner (stmt_mutator (List.hd l)) (List.map stmt_mutator (List.tl l))
  | Store (expr, buf, idx) -> combiner (expr_mutator expr) (expr_mutator idx)
  | Provide (expr, func, args) -> List.fold_left combiner (expr_mutator expr) (List.map expr_mutator args)
  | LetStmt (name, value, stmt) -> combiner (expr_mutator value) (stmt_mutator stmt)
  | Pipeline (name, ty, size, produce, consume) -> 
    combiner (combiner (expr_mutator size) (stmt_mutator produce)) (stmt_mutator consume)
  | Print (_, []) -> expr_mutator (IntImm 0)
  | Print (_, l) -> List.fold_left combiner (expr_mutator (List.hd l)) (List.map expr_mutator (List.tl l))
  | Assert (e, _) -> expr_mutator e

(* E.g:
let rec stmt_contains_zero stmt =
  let rec expr_mutator = function
    | IntImm 0 -> true
    | x -> fold_children_in_expr expr_mutator (or) false x
  fold_children_in_stmt expr_mutator stmt_contains_zero (or) stmt
*)

let rec expr_contains_expr query expr = 
  if (query = expr) then true else
    fold_children_in_expr (expr_contains_expr query) (fun x y -> x or y) false expr

let rec stmt_contains_expr query stmt =
  fold_children_in_stmt (expr_contains_expr query) (stmt_contains_expr query) (fun x y -> x or y) stmt


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
  | Call (ct, rt, f, args)-> Call (ct, rt, f, List.map mutator args)
  | Let (n, a, b)         -> Let (n, mutator a, mutator b)
  | Debug (e, fmt, args)  -> Debug (mutator e, fmt, List.map mutator args)
  | x -> x
    
let mutate_children_in_stmt expr_mutator stmt_mutator = function
  | For (name, min, n, order, body) ->
      For (name, expr_mutator min, expr_mutator n, order, stmt_mutator body)
  | Block l -> Block (List.map stmt_mutator l)
  | Store (expr, buf, idx) -> 
      Store (expr_mutator expr, buf, expr_mutator idx)
  | Provide (expr, func, args) ->
      Provide (expr_mutator expr, func, List.map expr_mutator args)
  | LetStmt (name, value, stmt) ->
      LetStmt (name, expr_mutator value, stmt_mutator stmt)
  | Pipeline (name, ty, size, produce, consume) -> 
      Pipeline (name, ty, expr_mutator size, stmt_mutator produce, stmt_mutator consume)
  | Print (p, l) -> Print (p, List.map expr_mutator l)
  | Assert (e, str) -> Assert (expr_mutator e, str)

(* Statement subsitution *)
(*
let rec subs_stmt oldstmt newstmt stmt =
  if stmt = oldstmt then
    newstmt
  else
    mutate_children_in_stmt (fun e -> e) (subs_stmt oldstmt newstmt) stmt
*)

(* Expression subsitution *)
let rec subs_expr_in_stmt oldexpr newexpr stmt =
  mutate_children_in_stmt (subs_expr oldexpr newexpr) (subs_expr_in_stmt oldexpr newexpr) stmt

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
    | Provide (expr, func, args) ->
        Provide (subs_expr expr,
                 (if func = oldname then newname else func),
                 List.map subs_expr args)
    | LetStmt (name, value, stmt) ->
        LetStmt ((if name = oldname then newname else name), 
                 subs_expr value, subs stmt)
    | Pipeline (name, ty, size, produce, consume) -> 
        Pipeline ((if name = oldname then newname else name), 
                  ty, subs_expr size, subs produce, subs consume)      
    | Print (p, l) -> Print (p, List.map subs_expr l)
    | Assert(e, str) -> Assert(subs_expr e, str)

and subs_name_expr oldname newname expr =
  let subs = subs_name_expr oldname newname in
  match expr with 
    | Load (t, b, i)    -> Load (t, (if b = oldname then newname else b), subs i)
    | Var (t, n)        -> Var (t, if n = oldname then newname else n)
    | Let (n, a, b)     -> Let ((if n = oldname then newname else n), subs a, subs b)
    | Call (ct, rt, f, args) -> Call (ct, rt, (if f = oldname then newname else f), List.map subs args)
    | x                 -> mutate_children_in_expr subs x

and prefix_non_global prefix name =
  let is_global = name.[0] = '.' in
  if is_global then name else prefix ^ name

and prefix_name_expr prefix expr =
  let recurse = prefix_name_expr prefix in
  match expr with 
    | Load (t, b, i)    -> Load (t, prefix_non_global prefix b, recurse i)
    | Var (t, n)        -> Var (t, prefix_non_global prefix n)
    | Let (n, a, b)     -> Let (prefix ^ n, recurse a, recurse b)
    | Call (ct, rt, f, args) -> Call (ct, rt, prefix_non_global prefix f, List.map recurse args)
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
    | Provide (expr, func, args) ->
        Provide (recurse_expr expr, prefix_non_global prefix func, List.map recurse_expr args)
    | LetStmt (name, value, stmt) ->
        LetStmt (prefix ^ name, recurse_expr value, recurse_stmt stmt)
    | Pipeline (name, ty, size, produce, consume) -> 
        Pipeline (prefix ^ name, ty, recurse_expr size, recurse_stmt produce, recurse_stmt consume)      
    | Print (p, l) ->
        Print (p, List.map recurse_expr l)
    | Assert(e, str) ->
        Assert (recurse_expr e, str)

(* Find all references in stmt/expr to things outside of it.
 * Return a set of pairs of names and their storage sizes, for e.g. building a closure. *)
let rec find_names_in_stmt internal ptrsize stmt =
  let recs = find_names_in_stmt internal ptrsize in
  let rece = find_names_in_expr internal ptrsize in
  match stmt with
  | Store (e, buf, idx) ->
      let inner = StringIntSet.union (rece e) (rece idx) in
      if (StringSet.mem buf internal) then inner else (StringIntSet.add (buf, ptrsize) inner)
  | Pipeline (name, ty, size, produce, consume) ->
      let internal = StringSet.add name internal in
      let recs = find_names_in_stmt internal ptrsize in
      let rece = find_names_in_expr internal ptrsize in
      string_int_set_concat [rece size; recs produce; recs consume]
  | LetStmt (name, value, stmt) ->
      let internal = StringSet.add name internal in
      let recs = find_names_in_stmt internal ptrsize in
      let rece = find_names_in_expr internal ptrsize in
      StringIntSet.union (rece value) (recs stmt)
  | Block l ->
      string_int_set_concat (List.map recs l)
  | For (var_name, min, size, order, body) ->
      let internal = StringSet.add var_name internal in
      let recs = find_names_in_stmt internal ptrsize in
      let rece = find_names_in_expr internal ptrsize in
      string_int_set_concat [rece min; rece size; recs body]
  | Print (fmt, args) ->
      string_int_set_concat (List.map rece args)
  | Assert (e, str) ->
      rece e
  | Provide (fn, _, _) ->
      failwith "Encountered a provide during cg. These should have been lowered away."
and find_names_in_expr internal ptrsize expr =
  let rece = find_names_in_expr internal ptrsize in
  match expr with 
  | Load (_, buf, idx) ->
      let inner = rece idx in
      if (StringSet.mem buf internal) then inner else (StringIntSet.add (buf, ptrsize) inner)
  | Var (t, n) ->
      let size = (bit_width t)/8 in
      if (StringSet.mem n internal) then StringIntSet.empty else (StringIntSet.add (n, size) StringIntSet.empty)
  | Let (n, a, b) ->
      let internal = StringSet.add n internal in
      let rece = find_names_in_expr internal ptrsize in
      StringIntSet.union (rece a) (rece b)
  | x -> fold_children_in_expr rece StringIntSet.union StringIntSet.empty x

let rec find_loads_in_expr = function
  | Load (_, buf, idx) ->
      StringSet.union (find_loads_in_expr idx) (StringSet.singleton buf)
  | e ->
      fold_children_in_expr find_loads_in_expr StringSet.union StringSet.empty e

let rec find_loads_in_stmt s =
  fold_children_in_stmt find_loads_in_expr find_loads_in_stmt StringSet.union s

let rec find_stores_in_stmt = function
  | Store (_, buf, _) ->
      StringSet.add buf StringSet.empty
  | s ->
      fold_children_in_stmt
        (fun _ -> StringSet.empty)
        find_stores_in_stmt
        StringSet.union
        s

(* Is each pair of lanes identical? *)
let rec duplicated_lanes expr = match expr with
  | Broadcast _ -> true
  | Bop (Div, Ramp (base, one, n), two) 
      when (one = make_const (val_type_of_expr base) 1 &&
          two = make_const (val_type_of_expr expr) 2) -> true
  | MakeVector _ -> false
  | Ramp _ -> false
  | Var _ -> false
  | expr -> fold_children_in_expr duplicated_lanes (&&) true expr

(* For a vector in which each pair of lanes is identical, half the vector width and make the lanes unique *)
let rec deduplicate_lanes expr = match expr with
  | Broadcast (e, n) -> Broadcast (e, n/2)
  | Bop (Div, Ramp (base, one, n), two) 
      when (one = make_const (val_type_of_expr base) 1 &&
          two = make_const (val_type_of_expr expr) 2) -> 
      Ramp (base /~ (make_const (val_type_of_expr base) 2), one, n/2)
  | Load (t, buf, idx) when is_vector idx ->
      Load (vector_of_val_type (element_val_type t) ((vector_elements t)/2), 
            buf, deduplicate_lanes idx)
  | MakeVector _ -> failwith "Can't deduplicate the lanes of a MakeVector"
  | Ramp _ -> failwith "Can't deduplicate the lanes of a generic Ramp"
  | Var _ -> failwith "Can't deduplicate the lanes of a generic Var"
  | expr -> mutate_children_in_expr deduplicate_lanes expr

