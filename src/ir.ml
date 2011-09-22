open Util

(* casting or separate op types for float/(u)int? *)

(* make different levels of IR subtype off increasingly restrictive, partially
 * compatible interface types? *)

(* bits per element *)
type val_type = 
    | Int of int 
    | UInt of int 
    | Float of int 
    (* TODO: this technically allows Vector(Vector(...)) which the pattern
     * matching checks don't like, among other things. *)
    (* TODO: switch to explicit IntVector, UIntVector, FloatVector? *)
    (* TODO: separate BoolVector or mask type? *)
    | IntVector of int * int
    | UIntVector of int * int
    | FloatVector of int * int

let bool1 = UInt(1)
let u8 = UInt(8)
let u16 = UInt(16)
let u32 = UInt(32)
let u64 = UInt(64)
let i8 = Int(8)
let i16 = Int(16)
let i32 = Int(32)
let i64 = Int(64)
let f16 = Float(16)
let f32 = Float(32)
let f64 = Float(64)

let element_val_type = function
  | IntVector(x, _) -> Int(x)
  | UIntVector(x, _) -> UInt(x)
  | FloatVector(x, _) -> Float(x)
  | x -> x

let vector_of_val_type t n = match t with
  | Int(x) -> IntVector(x, n)
  | UInt(x) -> UIntVector(x, n)
  | Float(x) -> FloatVector(x, n)
  | _ -> raise (Wtf("vector_of_val_type called on vector type"))

let bit_width = function
  | Int(x) | UInt(x) | Float(x) -> x
  | IntVector(x, n) | UIntVector(x, n) | FloatVector(x, n) -> x*n

let element_width t = bit_width (element_val_type t)

let vector_elements = function
  | Int(x) | UInt(x) | Float(x) -> 1
  | IntVector(x, n) | UIntVector(x, n) | FloatVector(x, n) -> n

type binop = Add | Sub | Mul | Div
type cmpop = EQ | NE | LT | LE | GT | GE

let caml_iop_of_bop = function
  | Add -> ( + ) 
  | Sub -> ( - )
  | Mul -> ( * )
  | Div -> ( / )

let caml_fop_of_bop = function
  | Add -> ( +. ) 
  | Sub -> ( -. )
  | Mul -> ( *. )
  | Div -> ( /. )

let caml_op_of_cmp = function
  | EQ -> ( =  )
  | NE -> ( <> )
  | LT -> ( <  )
  | LE -> ( <= )
  | GT -> ( >  )
  | GE -> ( >= )

(* how to enforce valid arithmetic subtypes for different subexpressions? 
 * e.g. only logical values expressions for Logical? Declare interfaces for 
 * each subtype? *)
type expr =
    (* constants *)
    (* TODO: add val_type to immediates? *)
    | IntImm of int
    | UIntImm of int
    | FloatImm of float

    (* TODO: separate "cast" from "convert"?
     * Cast changes operational interpretation of variables, but doesn't touch
     * stored bits.
     * Convert changes data to bits with equivalent interpretation in new type. *)
    (* TODO: validate operand type matches with Caml type parameters *)
    | Cast of val_type * expr

    (* only for domain variables? *)
    | Var of string

    (* scalar arguments *)
    (* TODO: specify type interpretation explicitly for easier val_type_of_expr? *)
    | Arg of val_type * string

    (* basic binary ops *)
    | Bop of binop * expr * expr

    (* comparison *)
    | Cmp of cmpop * expr * expr

    (* logical *)
    | And of expr * expr
    | Or  of expr * expr
    | Not of expr

    (* Select of [condition], [true val], [false val] *)
    (* Type is type of [true val] & [false val], which must match *)
    | Select of expr * expr * expr

    (* memory *)
    | Load of val_type * buffer * expr
          
    (* Two different ways to make vectors *)
    | MakeVector of (expr list)
    | Broadcast of expr * int

    (* TODO: Unpack/swizzle vectors *)
    | ExtractElement of expr * expr

    (* TODO: function calls? *)




and buffer = string (* TODO: just an ID for now *)

exception ArithmeticTypeMismatch of val_type * val_type

(* TODO: assert that l,r subexpressions match. Do as separate checking pass? *)
let rec val_type_of_expr = function
  | IntImm _ -> i32
  | UIntImm _ -> u32
  | FloatImm _ -> f32
  | Cast(t,_) -> t
  | Bop(_, l, r) | Select(_,l,r) ->
      let lt = val_type_of_expr l and rt = val_type_of_expr r in
      if (lt <> rt) then raise (ArithmeticTypeMismatch(lt,rt));
      lt
  | Var _ -> i32 (* Vars are only defined as integer programs so must be ints *)
  | Arg (vt,_) -> vt
  (* boolean expressions on vector types return bool vectors of equal length*)
  (* boolean expressions on scalars return scalar bools *)
  | Cmp(_, l, r) -> 
      let lt = val_type_of_expr l and rt = val_type_of_expr r in
      if (lt <> rt) then raise (ArithmeticTypeMismatch(lt,rt));
      begin
        match lt with
          | IntVector(_, len) | UIntVector(_, len) | FloatVector(_, len) ->
              IntVector(1, len)
          | _ -> bool1
      end
  (* And/Or/Not only allow boolean operands, and return the same type *)
  | And(e,_) | Or(e,_) | Not e ->
      (* TODO: add checks *)
      val_type_of_expr e
  | Load(t,_,_) -> t
  | MakeVector(l) -> vector_of_val_type (val_type_of_expr (List.hd l)) (List.length l)
  | Broadcast(e,len) -> vector_of_val_type (val_type_of_expr e) len
  | ExtractElement(e, idx) -> element_val_type (val_type_of_expr e)

(* does this really become a list of domain bindings? *)
type domain = {
    name : string; (* name binding *)
    range : program; (* TODO: rename - polygon, region, polyhedron, ... *)
}

and program = int * int (* just intervals for initial testing *)

type stmt =
  (* TODO: | Blit of -- how to express sub-ranges? -- split and merge
   * sub-ranges *)
  (* | If of expr * stmt  *)
  (* | IfElse of expr * stmt * stmt  *)
  | Map of string * expr * expr * stmt
  (* | For of domain * stmt *)
    (* TODO: For might need landing pad: always executes before, only if *any* iteration
     * fired. Same for Map - useful for loop invariant code motion. Easier for
     * Map if multiple dimensions are fused into 1. *)
  | Block of stmt list
  (* | Reduce of reduce_op * expr * memref *) (* TODO: initializer expression? *)
  | Store of expr * buffer * expr

(*
(* TODO:  *)
and reduce_op =
    | AddEq
    | SubEq
    | MulEq
    | DivEq
 *)

type arg =
  | Scalar of string * val_type
  | Buffer of string

type entrypoint = arg list * stmt

(* Some sugar for the operators that are naturally infix *)
let ( +~ ) a b  = Bop (Add, a, b)
let ( -~ ) a b  = Bop (Sub, a, b)
let ( *~ ) a b  = Bop (Mul, a, b)
let ( /~ ) a b  = Bop (Div, a, b) 
let ( >~ ) a b  = Cmp (GT, a, b)
let ( >=~ ) a b = Cmp (GE, a, b)
let ( <~ ) a b  = Cmp (LT, a, b)
let ( <=~ ) a b = Cmp (LE, a, b)
let ( =~ ) a b  = Cmp (EQ, a, b)
let ( <>~ ) a b = Cmp (NE, a, b)
let ( ||~ ) a b = Or (a, b)
let ( &&~ ) a b = And (a, b)
let ( !~ ) a    = Not a
