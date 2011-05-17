(* casting or separate op types for float/(u)int? *)

(* make different levels of IR subtype off increasingly restrictive, partially
 * compatible interface types? *)

type val_type = 
    | Int of int
    | UInt of int
    | Float of int

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

(* how to enforce valid arithmetic subtypes for different subexpressions? 
 * e.g. only logical values expressions for Logical? Declare interfaces for 
 * each subtype? *)
type expr =
    (* constants *)
    | IntImm of int
    | UIntImm of int
    | FloatImm of float

    (* arithmetic *)
    | Add of val_type * binop
    | Sub of val_type * binop
    | Mul of val_type * binop
    | Div of val_type * binop

    (* only for domain variables? *)
    | Var of string

    (* comparison *)
    | EQ of binop
    | NEQ of binop
    | LT of binop
    | LTE of binop
    | GT of binop
    | GTE of binop

    (* logical *)
    | And of binop
    | Or of binop
    | Not of expr

    (* memory *)
    | Load of val_type * memref

and binop = {
    l : expr;
    r : expr;
}

and memref = {
    (* how do we represent memory references? computed references? *)
    buf : buffer;
    idx : expr;
}

and buffer = int (* just an ID for now *)

(* does this really become a list of domain bindings? *)
type domain = {
    name : string; (* name binding *)
    range : program;
}

and program = int * int (* just intervals for initial testing *)

type stmt =
    | Store of expr * memref
    | If of expr * stmt
    | IfElse of expr * stmt * stmt
    | Block of stmt list
    | Map of domain * stmt
