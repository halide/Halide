open Ir
open Util
open List

type vec_expr = 
  | Scalar of expr
  | Const of expr * int
  | Linear of expr * int
  | Vector of expr

(* Expand a vec_expr to a realized vector expr, of width *)
let expand e width = match e with

  (* Scalar expressions broadcast up to width *)
  | Scalar e | Const (e, _) -> Broadcast (e, width)

  (* Linear expressions are evaluated across a vector of width *)
  | Linear (e, s) ->
      let range x = IntImm (s * x) in
        Bop (Add, Broadcast (e, width), MakeVector (map range (0 -- width)))

  (* Vectors trivially expand *)
  | Vector e -> e

(* Unpack a vec_expr into the appropriate realized width expr *)
let unpack e width = match e with

  (* Scalar expressions stay scalar *)
  | Scalar e | Const (e, _) -> e

  (* Vector expressions are realized as vectors *)
  | Linear _ -> expand e width
  | Vector e -> e

let is_vector = function
  | Scalar _ | Const _ -> false
  | _ -> true

let vectorize_expr_packed expr var width = 

  let expand e = expand e width
  and unpack_scalar e = match e with
    | Scalar _ | Const _ -> unpack e width
    | _ -> raise (Wtf "Can't unpack vector vec_expr as a scalar")
  in
  
  (* Vectorize a trivial operation over operand list `operands`, with expr
   * constructor `ctor` *)
  let rec vec_trivial_op ctor operands =
    let vec_operands = map vec operands in
    if exists is_vector vec_operands then
      Vector(ctor (map expand vec_operands))
    else
      Scalar(ctor operands)

  and vec expr = match expr with
    (* Track Int immediates as Const ints throughout vectorization *)
    | IntImm x | UIntImm x -> Const (expr, x)
    | FloatImm _ -> Scalar expr (* Not strictly true. It's constant, but not a const int *)

    | Cast (t, expr) -> begin match (vec expr) with
        | Const (e, c)  -> Const  (Cast (t, e), c)
        | Scalar e      -> Scalar (Cast (t, e))
        | Linear (e, s) -> Linear (Cast (t, e), s)
        | Vector e      -> Vector (Cast (vector_of_val_type t width, e))
    end

    | Bop (op, a, b) -> begin
      let veca = vec a and vecb = vec b in
      match (op, veca, vecb) with
        (* Expand whenever either operand is a general Vector *)
        | (_, Vector _, _)
        | (_, _, Vector _) -> Vector (Bop(op, expand veca, expand vecb))

        (* Non-Const int Scalar expressions stay Scalar *)
        | (_, Scalar va, Scalar vb) 
        | (_, Scalar va, Const (vb, _)) 
        | (_, Const (va, _), Scalar vb) -> Scalar (Bop(op, va, vb))

        (* TODO: it really feels like most Const/Linear code should be
         * able to be factored out in common *)
        (* Propagate Const *)
        | (_, Const (va, ca), Const (vb, cb)) -> begin match op with
            | Add -> Const (Bop(op, va, vb), ca + cb)
            | Sub -> Const (Bop(op, va, vb), ca - cb)
            | Mul -> Const (Bop(op, va, vb), ca * cb)
            | Div -> Const (Bop(op, va, vb), ca / cb)
        end

        (* Propagate Linearity where possible *)
        | (_, Scalar va, Linear (vb, sb)) -> begin match op with
            | Add -> Linear (Bop(op, va, vb), sb)
            | Sub -> Linear (Bop(op, va, vb), -sb)
            | Mul | Div -> Vector (Bop(op, expand veca, expand vecb)) (* Vectorize *)
        end
        | (_, Linear (va, sa), Scalar vb) -> begin match op with
            | Add | Sub -> Linear (Bop(op, va, vb), sa)
            | Mul | Div -> Vector (Bop(op, expand veca, expand vecb)) (* Vectorize *)
        end
        | (_, Const (va, ca), Linear (vb, sb)) -> begin match op with
            | Add -> Linear (Bop(op, va, vb), sb)
            | Sub -> Linear (Bop(op, va, vb), -sb)
            | Mul -> Linear (Bop(op, va, vb), ca*sb)
            | Div -> Vector (Bop(op, expand veca, expand vecb))      (* Vectorize *)
        end
        | (_, Linear (va, sa), Const (vb, cb)) -> begin match op with
            | Add | Sub -> Linear (Bop(op, va, vb), sa)
            | Mul -> Linear (Bop(op, va, vb), sa*cb)
            | Div -> Vector (Bop(op, expand veca, expand vecb))      (* Vectorize *)
        end
        | (_, Linear (va, sa), Linear (vb, sb)) -> begin match op with
            | Add -> Linear (Bop(op, va, vb), sa + sb)
            | Sub -> Linear (Bop(op, va, vb), sa - sb)
            | _ -> Vector (Bop(op, expand veca, expand vecb))        (* Vectorize *)
        end
    end

    (* Cmp/And/Or/Not trivially expand:
     * vectorize both operands iff either is a vector *)
    | Cmp (op, a, b) -> vec_trivial_op (fun ops -> Cmp(op, hd ops, nth ops 1)) [a; b]
    | And (a, b)     -> vec_trivial_op (fun ops -> And(    hd ops, nth ops 1)) [a; b]
    | Or  (a, b)     -> vec_trivial_op (fun ops ->  Or(    hd ops, nth ops 1)) [a; b]
    | Not (a)        -> vec_trivial_op (fun ops -> Not(    hd ops))            [a]

    | Select (c, a, b) ->
      let veca = vec a and vecb = vec b and vecc = vec c in
      (* Stay scalar iff all operands are scalar, otherwise promote *)
      if (not (is_vector veca || is_vector vecb || is_vector vecc)) then
        Scalar(Select(unpack_scalar vecc, unpack_scalar veca, unpack_scalar vecb))

      else begin match (vecc, veca, vecb) with
        (* Scalar selection between linear expressions of equivalent stride
         * stays linear *)
        | (Scalar (vc), Linear (va, sa), Linear (vb, sb)) 
        | (Const (vc, _), Linear (va, sa), Linear (vb, sb)) when (sa = sb) ->
          Linear (Select(vc, va, vb), sa)

        (* Scalar condition does scalar select between vectors *)
        | (Scalar (vc), _, _) | (Const (vc, _), _, _) -> 
          Vector (Select(vc, expand veca, expand vecb))

        (* Otherwise, full vector select between vectors *)
        | _ -> 
          Vector (Select(expand vecc, expand veca, expand vecb))
      end

    | Load (t, mr) -> begin let veci = (vec mr.idx) in match veci with 
        (* Scalar load *)
        | Const (e, _) | Scalar e | Linear (e, 0) -> Scalar (Load (t, mr))
        (* Dense vector load *)
        | Linear (e, 1) -> Vector (Load (vector_of_val_type t width, {buf = mr.buf; idx = e}))
        (* TODO: strided load *)
        | Linear (e, _)
        (* Generalized gather *)
        | Vector (e) -> Vector (Load (vector_of_val_type t width, {buf = mr.buf; idx = expand veci}))
    end

    (* Vectorized Var vectorizes to strided expression version of itself *)
    | Var(name) when (name = var) -> Linear (IntImm(width) *~ expr, 1)
    (* Other Vars are Scalar relative to this vectorization *)
    | Var(_) -> Scalar(expr) 

    | _ -> raise (Wtf("Can't vectorize vector code"))

  in vec expr

(* Vectorize expr and realize unpacked result *)
let vectorize_expr e var width =
  unpack (vectorize_expr_packed e var width) width

let rec vectorize_stmt stmt var width = 

  let expand e = expand e width
  and vec s = vectorize_stmt s var width in

  match stmt with
    | Map (name, min, max, stmt) when name = var ->
      (* TODO: Currently we assume width divides min and max *)
      Map (name, min /~ IntImm(width), max /~ IntImm(width), vec stmt)
    | Map (name, min, max, stmt) -> Map (name, min, max, vec stmt)
    | Block l -> Block (map vec l)
    | Store (expr, mr) -> begin
      let e = vectorize_expr_packed expr var width in
      let idx = vectorize_expr_packed mr.idx var width in
      match idx with
        (* Storing to a scalar address *)
        | Scalar(i) | Const(i, _) | Linear(i, 0) -> begin match e with
            | Scalar(a) | Const(a, _) -> Store (a, {buf = mr.buf; idx = i})
            | _ -> raise (Wtf("Storing a vector at a scalar address. Result undefined."))              
        end
        (* Storing to a vector of addresses. *)
        | Linear(i, 1) -> Store (expand e, {buf = mr.buf; idx = i}) (* dense store *)
        | Linear(i, n) -> Store (expand e, {buf = mr.buf; idx = expand idx}) (* TODO: strided store *)
        | _ -> Store (expand e, {buf = mr.buf; idx = expand idx}) (* scatter *)
    end
    (* | _ -> raise (Wtf "don't know how to vectorize this statement yet") *)
