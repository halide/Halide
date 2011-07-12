open Ir

let rec string_of_val_type = function
  | Int(w) -> "[" ^ "i" ^ string_of_int w ^ "]"
  | UInt(w) -> "[" ^ "u" ^ string_of_int w ^ "]"
  | Float(w) -> "[" ^ "f" ^ string_of_int w ^ "]"
  | Vector(t, w) -> "[" ^ (string_of_val_type t) ^ "x" ^ (string_of_int w) ^ "]"

and string_of_expr = function
  | IntImm(i) -> string_of_int i
  | UIntImm(i) -> string_of_int i ^ "u"
  | FloatImm(f) -> string_of_float f

  | Cast(t, e) -> "cast" ^ "<" ^ string_of_val_type t ^ ">" ^ "(" ^ string_of_expr e ^")"

  | Add(l, r) ->
      "(" ^ string_of_expr l ^ "+" ^ string_of_expr r ^ ")"
  | Sub(l, r) ->
      "(" ^ string_of_expr l ^ "-" ^ string_of_expr r ^ ")"
  | Mul(l, r) ->
      "(" ^ string_of_expr l ^ "*" ^ string_of_expr r ^ ")"
  | Div(l, r) ->
      "(" ^ string_of_expr l ^ "/" ^ string_of_expr r ^ ")"

  | Select(c, t, f) ->
      "(" ^ string_of_expr c ^ "?" ^ string_of_expr t ^ ":" ^ string_of_expr f ^ ")"

  | Var(s) -> s

  | Load(t, mr) -> "load" ^ "(" ^ string_of_val_type t ^ "," ^ string_of_memref mr ^ ")"

  | MakeVector [] -> "vec[]"
  | MakeVector l -> 
      let els = List.map string_of_expr l in
      "vec[" ^ (List.fold_right
                  (fun x y -> x ^ ", " ^ y) 
                  (List.tl els)
                  (List.hd els)
               ) ^ "]"

  | _ -> "<<UNHANDLED>>"

and string_of_stmt = function
  | If(e, s) -> "if (" ^ string_of_expr e ^ ") " ^ string_of_stmt s
  | IfElse(e, ts, fs) -> "if (" ^ string_of_expr e ^ ") " ^ string_of_stmt ts ^
                         " else " ^ string_of_stmt fs
  | Map(d, s) -> "map (" ^ string_of_domain d ^ ") " ^ string_of_stmt s
  | Block(stmts) -> "{" ^ "\n" ^
                    String.concat ";\n" (List.map string_of_stmt stmts) ^
                    "}" ^ "\n"
  | Reduce(op, e, mr) -> string_of_memref mr ^ string_of_reduce_op op ^ string_of_expr e
  | Store(e, mr) -> string_of_memref mr ^ "=" ^ string_of_expr e

and string_of_reduce_op = function
  | AddEq -> "+="
  | SubEq -> "-="
  | MulEq -> "*="
  | DivEq -> "/="

and string_of_memref mr = string_of_buffer mr.buf ^ "[" ^ string_of_expr mr.idx ^ "]"

and string_of_buffer b = "buf" ^ string_of_int b

and string_of_domain d = d.name ^ "=" ^ string_of_program d.range

and string_of_program p = match p with (lo,hi) -> string_of_int lo ^ ".." ^ string_of_int hi
