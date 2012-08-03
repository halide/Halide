open Ir

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

let hash_call_type = function
  | Func -> hash_int 0
  | Extern -> hash_int 1
  | Image -> hash_int 2

let hash_combine2 (a, b, c, d) (e, f, g, h) =
  let rand = Random.State.make [|a; b; c; d; e; f; g; h|] in  
  (Random.State.bits rand, Random.State.bits rand, Random.State.bits rand, Random.State.bits rand)

let hash_combine3 a b c =
  hash_combine2 (hash_combine2 a b) c

let hash_combine4 a b c d =
  hash_combine2 (hash_combine2 a b) (hash_combine2 c d)

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
    | Call (ct, ty, name, args) ->
        List.fold_left hash_combine2 
          (hash_combine4 (hash_str "<Call>") (hash_call_type ct) (hash_str name) (hash_type ty))
          (List.map hash_expr args)
    | Debug (e, fmt, args) ->
        List.fold_left hash_combine2 
          (hash_combine3 (hash_str "<Debug>") (hash_str fmt) (hash_expr e))
          (List.map hash_expr args)
