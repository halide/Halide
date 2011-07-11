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
    | Vector of val_type * int

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
    (* TODO: switch to Int64 storage type *)
    (* TODO: add val_type to immediates? *)
    | IntImm of int
    | UIntImm of int
    | FloatImm of float

    (* TODO: require casts to match types *)
    (* TODO: validate operand type matches with Caml type parameters *)
    | Cast of val_type * expr

    (* arithmetic *)
    (* TODO: drop these types, and just require binops to match *)
    (* TODO: replace binop with just expr*expr for cleaner pattern matches *)
    | Add of val_type * binop
    | Sub of val_type * binop
    | Mul of val_type * binop
    | Div of val_type * binop

    (* only for domain variables? *)
    | Var of string

    (* comparison *)
    | EQ of binop
    | NE of binop
    | LT of binop
    | LE of binop
    | GT of binop
    | GE of binop

    (* logical *)
    | And of binop
    | Or of binop
    | Not of expr

    (* Select of [condition], [true val], [false val] *)
    (* Type is type of [true val] & [false val], which must match *)
    | Select of expr * expr * expr

    (* memory *)
    | Load of val_type * memref
          
    (* Make and break vectors? *)
    (*
    | PackVector of val_type * (expr list)
    | UnpackVector of val_type * (expr * int)
    *)
    (* TODO: function calls? *)

and binop = expr*expr

and memref = {
    (* how do we represent memory references? computed references? *)
    buf : buffer;
    idx : expr;
}

and buffer = int (* TODO: just an ID for now *)

let rec val_type_of_expr = function
  | IntImm _ -> i32
  | UIntImm _ -> u32
  | FloatImm _ -> f32
  | Cast(t,_) -> t
  | Add(t,_) | Sub(t,_) | Mul(t,_) | Div(t,_) -> t
  | Var _ -> i64 (* Vars are only defined as integer programs so must be ints *)
  | EQ _ | NE _ | LT _ | LE _ | GT _ | GE _ | And _ | Or _ | Not _ -> bool1
  (* TODO: check that b matches a *)
  | Select(_,a,b) -> val_type_of_expr a
  | Load(t,_) -> t

(* does this really become a list of domain bindings? *)
type domain = {
    name : string; (* name binding *)
    range : program; (* TODO: rename - polygon, region, polyhedron, ... *)
}

and program = int * int (* just intervals for initial testing *)

type stmt =
    (* TODO: | Blit of -- how to express sub-ranges? -- split and merge
     * sub-ranges *)
    | If of expr * stmt
    | IfElse of expr * stmt * stmt
    | Map of domain * stmt
    (* TODO: add For *)
    (* TODO: For might need landing pad: always executes before, only if *any* iteration
    * fired. Same for Map - useful for loop invariant code motion. Easier for
    * Map if multiple dimensions are fused into 1. *)
    | Block of stmt list
    | Reduce of reduce_op * expr * memref (* TODO: initializer expression? *)
    | Store of expr * memref

(* TODO:  *)
and reduce_op =
    | AddEq
    | SubEq
    | MulEq
    | DivEq
