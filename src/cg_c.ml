open Ir
open Ir_printer
open List
open Util
open Cg_util
open Analysis

module C = Cee

let buffer_t = C.TyName "buffer_t"

let ctype_of_val_type = function
  |  Int( 8) -> C.Int (C.Char,  C.Signed)
  |  Int(16) -> C.Int (C.Short, C.Signed)
  |  Int(32) -> C.Int (C.IInt,  C.Signed)
  |  Int(64) -> C.Int (C.Long,  C.Signed)
  | UInt( 8) -> C.Int (C.Char,  C.Unsigned)
  | UInt(16) -> C.Int (C.Short, C.Unsigned)
  | UInt(32) -> C.Int (C.IInt,  C.Unsigned)
  | UInt(64) -> C.Int (C.Long,  C.Unsigned)
  | Float(32)-> C.Float C.FFloat
  | Float(64)-> C.Float C.Double
  | UInt( 1) -> C.Bool
  | t -> failwith ("Unsupported type " ^ (string_of_val_type t))

let ( <*> ) x y = C.Infix (x, C.Mult, y)
let ( <+> ) x y = C.Infix (x, C.Add, y)
let ( <-> ) x y = C.Infix (x, C.Sub, y)
let ( </> ) x y = C.Infix (x, C.Div, y)
let ( <<> ) x y = C.Infix (x, C.LT, y)

let cname name =
  let components = split_name name in
  Str.global_replace (Str.regexp "\\.") "__" name

let malloc_fn = C.ID "malloc"
let free_fn = C.ID "free"

let cinit e = Some (C.SingleInit (e))
let csizeof cty = C.Call(C.ID "sizeof", [C.Type cty])

let cg_binop = function
  | Add -> C.Add | Sub -> C.Sub | Mul -> C.Mult | Div -> C.Div
  | op -> failwith ("Cg_c.cg_op of unsupported op " ^ (string_of_op op))

let cg_cmpop = function
  | EQ -> C.Eq | NE -> C.Neq | GT -> C.GT | GE -> C.GE | LT -> C.LT | LE -> C.LE

let rec cg_expr = function
  | IntImm i
  | UIntImm i -> C.IntConst i
  | FloatImm f -> C.Const (Printf.sprintf "%ff" f)

  | Cast (ty, e) -> C.Cast ((ctype_of_val_type ty), (cg_expr e))

  | Var (_, n) -> C.ID (cname n)

  (* TODO: replace the min/max cases with cg_expr Select(cmp, l, r) *)
  | Bop (Min, l, r) -> cg_expr (Select (Cmp(LE, l, r), l, r))
  | Bop (Max, l, r) -> cg_expr (Select (Cmp(GE, l, r), l, r))
  | Bop (op, l, r) -> C.Infix ((cg_expr l), (cg_binop op), (cg_expr r))

  | Cmp (op, l, r) -> C.Infix ((cg_expr l), (cg_cmpop op), (cg_expr r))

  | And (l, r) -> C.Infix ((cg_expr l), C.LAnd, (cg_expr r))
  | Or  (l, r) -> C.Infix ((cg_expr l), C.LOr,  (cg_expr r))
  | Not (e)    -> C.Prefix (C.Not, (cg_expr e))

  | Select (cond, t, f) -> C.Ternary ((cg_expr cond), cg_expr t, cg_expr f)

  | Load (ty, buf, idx) -> cg_buf_access ty buf idx

  (* TODO: lets are going to require declarations be queued and returned as well as the final expression/statement *)

  | _ -> failwith "Unimplemented cg_expr"

and cg_buf_access ty buf idx =
  let host_buf = C.Arrow ((C.ID (cname buf)), "host") in
  let typed_buf = C.Cast ((C.Ptr (ctype_of_val_type ty)), host_buf) in
  C.Access (typed_buf, (cg_expr idx))

let rec cg_stmt = function
  | Store (e, buf, idx) -> cg_store e buf idx
  
  | For (name, min, n, order, stmt) ->
      cg_for name min n stmt
  
  | Block(stmts) -> C.Block ([], (List.map cg_stmt stmts))

  | LetStmt (name, value, stmt) ->
      C.Block ([C.VarDecl(name,
                          ctype_of_val_type (val_type_of_expr value),
                          Some(C.SingleInit(cg_expr value)))],
                [cg_stmt stmt])

  | Pipeline (name, ty, size, produce, consume) ->
      (* allocate buffer *)
      let scratch_init = cg_malloc name size ty in

      (* do produce, consume *)
      let prod = [C.Comment ("produce " ^ name); cg_stmt produce] in
      let cons = [C.Comment ("consume " ^ name); cg_stmt consume] in

      (* free buffer *)
      let free = [C.Expr(cg_free name)] in

      C.Block ([scratch_init], prod @ cons @ free)

  | s -> failwith (Printf.sprintf "Can't codegen: %s" (Ir_printer.string_of_stmt s))

and cg_for name min size stmt =
  let iter_t = ctype_of_val_type (Int 32) in
  let iter_var = C.ID (cname name) in
  C.For (
    C.VarDecl ((cname name), iter_t, cinit (cg_expr min)),
    iter_var <<> (cg_expr (min +~ size)),
    C.Postfix (iter_var, C.PostInc),
    cg_stmt stmt
  )

and cg_malloc name size ty =
  let cty = ctype_of_val_type ty in
  C.VarDecl (
    cname name,
    C.Ptr cty,
    cinit (
      C.Call(malloc_fn, [(cg_expr size) <*> (csizeof cty)])
    )
  )

and cg_free name = C.Call (free_fn, [C.ID (cname name)])

and cg_store e buf idx =
  match (is_vector e, is_vector idx) with

    | (_, true) ->
        failwith "Unimplemented: vector store"

    | (false, false) ->
        C.Expr (C.Assign ((cg_buf_access (val_type_of_expr e) buf idx), (cg_expr e)))

    | (true, false) ->
        failwith "Can't store a vector to a scalar address"

let cg_entry e =
  let name,args,stmt = e in

  let carg = function
    | Scalar(n, t) -> (cname n, ctype_of_val_type t)
    | Buffer(n) -> (cname n, C.Ptr buffer_t)
  in

  let body = [cg_stmt stmt] in

  [C.Function
    {
      C.name = name;
      C.static = false;
      C.ty = { C.return = C.Void; C.args = List.map carg args; C.varargs = [] };
      C.decls = [];
      C.body = body;
    }]
