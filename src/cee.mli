type size           = int
type name           = string
type signed         = Signed | Unsigned
type iwidth         = Char | Short  | IInt | Long | LongLong
type fwidth         = FFloat  | Double | LongDouble

type function_type =
  { return:       ty
  ; args:         (name * ty) list
  ; varargs:      ty list
  }

and ty =
  | Void
  | Bool
  | Int           of iwidth * signed
  | Float         of fwidth 
  | Ptr           of ty
  | Array         of ty * size
  | Struct        of name * (name * ty) list option
  | Union         of name * (name * ty) list option
  | Fun           of function_type
  | TyName        of string
  | Bitfield      of int * signed

type infix   = Eq | Neq | GT | GE | LT | LE | Mult | Div | Mod | Add | Sub | LOr | LAnd     (* many more missing *)
type prefix  = Not | Neg                (* many more missing *)
type postfix = PostDec | PostInc        (* many more missing *)

type expr = 
  | ID            of string
  | IntConst      of int
  | Const         of string (* char, int, float, string *)
  | Call          of expr * expr list
  | Assign        of expr * expr
  | Access        of expr * expr  (* array , index *) 
  | Select        of expr * name  (* struct or union, member *)
  | Arrow         of expr * name
  | Deref         of expr 
  | AddrOf        of expr
  | Ternary       of expr * expr * expr (* a ? b : c *)
  | Infix         of expr * infix * expr
  | Prefix        of prefix * expr
  | Postfix       of expr * postfix
  | Cast          of ty   * expr
  | Type          of ty 

type init = 
  | SingleInit    of expr
  | CompoundInit  of init list

type decl = 
  | VarDecl of name * ty * init option
  | Typedef of name * ty

type stmt =
  | Expr          of expr
  | Block         of decl list * stmt list
  | Return        of expr option
  | IfThen        of expr * stmt
  | IfThenElse    of expr * stmt * stmt
  | For           of decl * expr * expr * stmt
                  (* decl, condition, update, body *)
  | Comment       of string
  | Nop
  (* incomplete *)

type fundef =
  { name:     name                    (* name of this function *)
  ; static:   bool
  ; ty:       function_type
  ; decls:    decl list               (* variable declarations in body *)
  ; body:     stmt list               (* statements in body *)
  }

type scope    = Static | Extern | Public
type toplevel =
  | TopDecl   of scope * decl
  | Function  of fundef
  | CPP       of string   (* C Preprocessor statement *)

type program = toplevel list

val unsigned: ty -> ty

val sizeof: ty -> int       (* not negative *)

val strip: ty -> ty

val variadic: function_type -> bool

val pointable: ty -> bool

val int:    ty     
val char:   ty
val string: ty

val include_headers: program -> program
