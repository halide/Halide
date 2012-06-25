open Ir
open Ir_printer
open List
open Util
open Cg_util
open Analysis

type cstmt =
  | Stmt of string
  | Blk of cstmt list (* TODO: add decls? *)

let ctype_of_val_type = function
  |  Int( 8) -> "int8_t"
  |  Int(16) -> "int16_t"
  |  Int(32) -> "int32_t"
  |  Int(64) -> "int64_t"
  | UInt( 8) -> "uint8_t"
  | UInt(16) -> "uint16_t"
  | UInt(32) -> "uint32_t"
  | UInt(64) -> "uint64_t"
  | Float(32)-> "float"
  | Float(64)-> "double"
  | UInt( 1) -> "bool"
  | t -> failwith "Unsupported type " ^ (string_of_val_type t)

let rec string_of_cstmt = function
  | Stmt (s) -> s ^ ";\n"
  | Blk (s) -> "{\n" ^ (String.concat "" (List.map string_of_cstmt s)) ^ "}\n"

let cname name = Str.global_replace (Str.regexp "\\.") "__" name

let cg_binop op = match op with
  | Add | Sub | Mul | Div | Mod -> string_of_op op
  | _ -> failwith "Cg_c.cg_op of unsupported op " ^ (string_of_op op)

let cg_cmpop = string_of_cmp

let rec cg_expr = function
  | IntImm i
  | UIntImm i -> Printf.sprintf "%d" i
  | FloatImm f -> Printf.sprintf "%ff" f

  | Cast (t, e) -> "(" ^ (ctype_of_val_type t) ^ ")" ^ cg_expr e

  | Var (_, n) -> cname n

  (* TODO: replace the min/max cases with cg_expr Select(cmp, l, r) *)
  | Bop (Min, l, r) -> "(" ^ (cg_expr l) ^ "<" ^ (cg_expr r) ^
                       "?" ^ (cg_expr l) ^ ":" ^ (cg_expr r) ^ ")"
  | Bop (Max, l, r) -> "(" ^ (cg_expr l) ^ ">" ^ (cg_expr r) ^
                       "?" ^ (cg_expr l) ^ ":" ^ (cg_expr r) ^ ")"
  | Bop (op, l, r) -> "(" ^ (cg_expr l) ^ (cg_binop op) ^ (cg_expr r) ^ ")"

  | Cmp (op, l, r) -> "(" ^ (cg_expr l) ^ (cg_cmpop op) ^ (cg_expr r) ^ ")"

  | And (l, r) -> "(" ^ (cg_expr l) ^ "&&" ^ (cg_expr r) ^ ")"
  | Or  (l, r) -> "(" ^ (cg_expr l) ^ "||" ^ (cg_expr r) ^ ")"
  | Not (e)    ->"!(" ^ (cg_expr e) ^ ")"

  | Load (_, buf, idx) -> ((cname buf) ^ "->host") ^ "[" ^ (cg_expr idx) ^ "]"

  (* TODO: lets are going to require declarations be queued and returned as well as the final expression/statement *)

  | _ -> failwith "Unimplemented cg_expr"

let rec cg_stmt = function
  | Store (e, buf, idx) -> Stmt (cg_store e buf idx)
  
  | For (name, min, n, order, stmt) ->
      Stmt (cg_for name min n stmt)
  
  | Block(stmts) -> Blk (List.map cg_stmt stmts)

  | LetStmt (name, value, stmt) ->
      Blk [(Stmt (cg_decl name value)); cg_stmt stmt]

  | Pipeline (name, ty, size, produce, consume) ->

      (* allocate buffer *)
      let scratch = cg_malloc name size (ctype_of_val_type ty) in

      (* do produce, consume *)
      let prod = cg_stmt produce in
      let cons = cg_stmt consume in

      (* free buffer *)
      let free = cg_free name in

      Blk [(Stmt scratch); prod; cons; (Stmt free)]

  | s -> failwith (Printf.sprintf "Can't codegen: %s" (Ir_printer.string_of_stmt s))

and cg_for name min size stmt =
  "for (int32_t " ^ (cname name) ^ " = " ^ (cg_expr min) ^ "; "
      ^ (cname name) ^ " < " ^ (cg_expr (min +~ size)) ^ "; "
      ^ "++" ^ (cname name) ^ ")"
      ^ (string_of_cstmt (cg_stmt stmt))

and cg_malloc name size ctype =
  ctype ^ " *" ^ name ^ " = malloc(" ^ (cg_expr size) ^ " ^ *sizeof(" ^ ctype ^ "))"

and cg_free name = "free(" ^ name ^ ")"

and cg_decl name value =
  let ty = ctype_of_val_type (val_type_of_expr value) in
  ty ^ " " ^ name ^ " = " ^ (cg_expr value)

and cg_store e buf idx =
  match (is_vector e, is_vector idx) with
  | (_, true) -> failwith "Unimplemented: vector store"
  | (false, false) -> (cname buf) ^ "[" ^ (cg_expr idx) ^ "] = " ^ (cg_expr e)
  | (true, false) ->failwith "Can't store a vector to a scalar address"

let cg_entry e =
  let name,args,stmt = e in

  let carg = function
    | Scalar(n, t) -> (ctype_of_val_type t) ^ " " ^ (cname n)
    | Buffer(n) -> "buffer_t" ^ " " ^ (cname n)
  in

  let decl = "void " ^ (cname name) ^ "("
              ^ (String.concat ", " (List.map carg args)) ^
             ")" in
  let body = "{\n" ^ (string_of_cstmt (cg_stmt stmt)) ^ "\n}" in

  decl ^ "\n" ^ body
