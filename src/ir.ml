open Util

type val_type = 
  (* bits per element *)    
  | Int of int 
  | UInt of int 
  | Float of int 
  (* bits per element, vector width *)
  | IntVector of int * int
  | UIntVector of int * int
  | FloatVector of int * int

let bool1 = UInt 1
let u8    = UInt 8
let u16   = UInt 16
let u32   = UInt 32
let u64   = UInt 64
let i8    = Int 8
let i16   = Int 16
let i32   = Int 32
let i64   = Int 64
let f16   = Float 16
let f32   = Float 32
let f64   = Float 64

let element_val_type = function
  | IntVector   (x, _) -> Int x
  | UIntVector  (x, _) -> UInt x
  | FloatVector (x, _) -> Float x
  | x -> x

let vector_of_val_type t n = match t with
  | Int x   -> IntVector (x, n)
  | UInt x  -> UIntVector (x, n)
  | Float x -> FloatVector (x, n)
  | _ -> raise (Wtf("vector_of_val_type called on vector type"))

let bit_width = function
  | Int x 
  | UInt x 
  | Float x -> x
  | IntVector (x, n) 
  | UIntVector (x, n) 
  | FloatVector (x, n) -> x*n

let element_width t = bit_width (element_val_type t)

let vector_elements = function
  | Int x 
  | UInt x 
  | Float x -> 1
  | IntVector (x, n) 
  | UIntVector (x, n)
  | FloatVector (x, n) -> n

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

type expr =
  (* constants *)
  (* TODO: add val_type to immediates? *)
  | IntImm of int
  | UIntImm of int
  | FloatImm of float
      
  (* TODO: separate "cast" from "convert"?  Cast changes operational
   * interpretation of variables, but doesn't touch stored bits.
   * Convert changes data to bits with equivalent interpretation in
   * new type. *)

  | Cast of val_type * expr

  (* Variables. May be loop variables, function argument, or a
     variable introduced with Let *)
  | Var of val_type * string

  (* basic binary ops *)
  | Bop of binop * expr * expr

  (* comparison *)
  | Cmp of cmpop * expr * expr

  (* logical *)
  | And of expr * expr
  | Or  of expr * expr
  | Not of expr

  (* Select: condition, then case, else case *)
  | Select of expr * expr * expr

  (* Load: type, buffer name, index. If type is a vector, index refers to the element type. *)
  | Load of val_type * buffer * expr
      
  (* Three different ways to make vectors *)
  | MakeVector of (expr list)
  | Broadcast of expr * int
  | Ramp of expr * expr * int (* Base, stride, length *)

  (* Extract an element: vector, index *)
  | ExtractElement of expr * expr

  (* Function call: return type, function name, args *) 
  | Call of val_type * string * (expr list)

  (* Let expressions *)
  | Let of string * expr * expr

  (* An IR node for debugging. Evaluates to the sub-expression, but printf out to stderr when it gets evaluated *)
  | Debug of expr * string * (expr list)

and buffer = string (* just a name for now *)

exception ArithmeticTypeMismatch of val_type * val_type

let rec val_type_of_expr = function
  (* Immediates are currently all 32-bit *)
  | IntImm _   -> i32
  | UIntImm _  -> u32
  | FloatImm _ -> f32
      
  (* Binary operators and selects expect matching types on their
     sub-expressions *)
  | Bop (_, l, r) 
  | Select (_, l, r) ->
      let lt = val_type_of_expr l and rt = val_type_of_expr r in
      if (lt <> rt) then raise (ArithmeticTypeMismatch (lt, rt));
      lt

  (* And, Or, and Not all have the same type as their first arg (which
     should be a bool or bool vector) *)
  | And (e, _) 
  | Or (e, _) 
  | Debug (e, _, _) 
  | Not e -> val_type_of_expr e

  (* Comparisons on vector types return bool vectors of equal length*)
  (* Comparisons on scalars return scalar bools *)
  | Cmp (_, l, r) -> 
      let lt = val_type_of_expr l and rt = val_type_of_expr r in
      if (lt <> rt) then raise (ArithmeticTypeMismatch (lt, rt));
      begin match lt with
        | IntVector (_, n) 
        | UIntVector (_, n) 
        | FloatVector (_, n) -> IntVector (1, n)
        | _ -> bool1
      end

  (* Ops that construct vectors have vector types *)
  | MakeVector l          -> vector_of_val_type (val_type_of_expr (List.hd l)) (List.length l)
  | Broadcast (e, n)      -> vector_of_val_type (val_type_of_expr e) n
  | Ramp (b, s, n)        -> vector_of_val_type (val_type_of_expr b) n

  (* Extracting an element from a vector gives the element type *)
  | ExtractElement (e, _) -> element_val_type (val_type_of_expr e)

  (* A Let expression has the type of its body (rather than the type
     of the thing its defining) *)
  | Let (_, _, b)         -> val_type_of_expr b

  (* Some ops explicitly declare their result type *)
  | Cast (t, _)
  | Var  (t, _) 
  | Load (t, _, _)
  | Call (t, _, _) -> t


type stmt =
  | For of string * expr * expr * bool * stmt

  (* An in-order sequence of statements *)
  | Block of stmt list

  (* Store an expression to a buffer at a given index *)
  | Store of expr * buffer * expr

  (* Allocate temporary storage of a given size, produce into it with
     the first statement, consume from it with the second statement.
     For the first statement only the temporary storage is writeable.
     For the second statement the temporary storage is read-only.
  *)
  | Pipeline of buffer * val_type * expr * stmt * stmt

(* A function definition: (name, args, return type, body) *)
and definition = (string * ((val_type * string) list) * val_type * function_body)

and function_body = 
  | Pure of expr (* Evaluates to the return value *)
  | Impure of stmt (* Fills in an array called "result" *)

module Environment = Map.Make(String)
type environment = definition Environment.t

type arg =
  | Scalar of string * val_type
  | Buffer of string

type entrypoint = arg list * stmt

exception BadTypeCoercion

(* Make the types of the args of a binary op or comparison match *)
let match_types expr =
  let fix (a, b) =
    match (val_type_of_expr a, val_type_of_expr b) with
      | (ta, tb) when ta = tb -> (a, b)
      | (IntVector (vb, n), Int sb) when vb = sb -> (a, Broadcast (b, n))
      | (Int sb, IntVector (vb, n)) when vb = sb -> (Broadcast (a, n), b)
      | (UIntVector (vb, n), UInt sb) when vb = sb -> (a, Broadcast (b, n))
      | (UInt sb, UIntVector (vb, n)) when vb = sb -> (Broadcast (a, n), b)
      | (FloatVector (vb, n), Float sb) when vb = sb -> (a, Broadcast (b, n))
      | (Float sb, FloatVector (vb, n)) when vb = sb -> (Broadcast (a, n), b)
      | _ -> raise (Wtf "I can't perform this cast")
  in match expr with      
    | Bop (op, a, b) -> let (ma, mb) = fix (a, b) in Bop (op, ma, mb)
    | Cmp (op, a, b) -> let (ma, mb) = fix (a, b) in Cmp (op, ma, mb)
    | Or (a, b)      -> let (ma, mb) = fix (a, b) in Or (ma, mb)
    | And (a, b)     -> let (ma, mb) = fix (a, b) in And (ma, mb)
    | x -> x

(* Some sugar for the operators that are naturally infix *)
let ( +~ ) a b  = match_types (Bop (Add, a, b))
let ( -~ ) a b  = match_types (Bop (Sub, a, b))
let ( *~ ) a b  = match_types (Bop (Mul, a, b))
let ( /~ ) a b  = match_types (Bop (Div, a, b))
let ( >~ ) a b  = match_types (Cmp (GT, a, b))
let ( >=~ ) a b = match_types (Cmp (GE, a, b))
let ( <~ ) a b  = match_types (Cmp (LT, a, b))
let ( <=~ ) a b = match_types (Cmp (LE, a, b))
let ( =~ ) a b  = match_types (Cmp (EQ, a, b))
let ( <>~ ) a b = match_types (Cmp (NE, a, b))
let ( ||~ ) a b = match_types (Or (a, b))
let ( &&~ ) a b = match_types (And (a, b))
let ( !~ ) a    = Not a

(* More helpers for examining ir nodes *)
let is_scalar x = (vector_elements (val_type_of_expr x)) = 1
let is_vector x = not (is_scalar x)

let rec make_zero = function
  | Int 32   -> IntImm 0
  | UInt 32  -> UIntImm 0
  | Float 32 -> FloatImm 0.0
  | Int b   -> Cast (Int b, IntImm 0)
  | UInt b  -> Cast (UInt b, UIntImm 0)
  | Float b -> Cast (Float b, FloatImm 0.0)
  | IntVector (b, n)   -> Broadcast (make_zero (Int b), n)
  | UIntVector (b, n)  -> Broadcast (make_zero (UInt b), n)
  | FloatVector (b, n) -> Broadcast (make_zero (Float b), n)
