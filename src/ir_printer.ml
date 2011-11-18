open Ir
open Printf

let rec string_of_val_type = function
  | Int(w) -> sprintf "i%d" w
  | UInt(w) -> sprintf "u%d" w
  | Float(w) -> sprintf "f%d" w
  | IntVector(w, n) -> sprintf "i%dx%d" w n
  | UIntVector(w, n) -> sprintf "u%dx%d" w n
  | FloatVector(w, n) -> sprintf "f%dx%d" w n

and string_of_op = function
  | Add -> "+"
  | Sub -> "-"
  | Mul -> "*"
  | Div -> "/"
  | Mod -> "%"
  | Min -> "(min)"
  | Max -> "(max)"

and string_of_cmp = function
  | EQ -> "=="
  | NE -> "!="
  | LT -> "<"
  | LE -> "<="
  | GT -> ">"
  | GE -> ">="

and string_of_expr = function
  | IntImm(i) -> string_of_int i
  | UIntImm(i) -> string_of_int i ^ "u"
  | FloatImm(f) -> string_of_float f
  | Cast(t, e) -> "cast" ^ "<" ^ string_of_val_type t ^ ">" ^ "(" ^ string_of_expr e ^")"
  | Bop(op, l, r) -> "(" ^ string_of_expr l ^ string_of_op op ^ string_of_expr r ^ ")"
  | Cmp(op, l, r) -> "(" ^ string_of_expr l ^ string_of_cmp op ^ string_of_expr r ^ ")"
  | Select(c, t, f) ->
    "(" ^ string_of_expr c ^ "?" ^ string_of_expr t ^ ":" ^ string_of_expr f ^ ")"
  | Var(t, s) -> s
  | Load(t, b, i) -> "load(" ^ string_of_val_type t ^ "," ^ string_of_buffer b ^ "[" ^ string_of_expr i ^ "])"
  | Call(t, name, args) -> name ^ "<" ^ string_of_val_type t ^ ">(" ^ (String.concat ", " (List.map string_of_expr args)) ^ ")"
  | MakeVector l -> "vec[" ^ (String.concat ", " (List.map string_of_expr l)) ^ "]"
  | Broadcast(e, n) -> "broadcast[" ^ string_of_expr e ^ ", " ^ string_of_int n ^ "]"
  | Ramp(b, s, n) -> "ramp[" ^ string_of_expr b ^ ", " ^ string_of_expr s ^ ", " ^ string_of_int n ^ "]"
  | ExtractElement(a, b) -> "(" ^ string_of_expr a ^ "@" ^ string_of_expr b ^ ")"
  | Let(n, a, b) -> "(let " ^ n ^ " = " ^ string_of_expr a ^ " in " ^ string_of_expr b ^ ")"
  | Debug(e, prefix, args) -> "Debug(" ^ string_of_expr e ^ ", " ^ prefix ^ " " ^ 
      (String.concat ", " (List.map string_of_expr args)) ^ ")"
  | _ -> "<<UNHANDLED>>"

and string_of_stmt stmt = 
  let rec string_stmt p stmt =
    let sp = p ^ "  " in
    match stmt with
      (* | If(e, s) -> "if (" ^ string_of_expr e ^ ") " ^ string_of_stmt s *)
      (* | IfElse(e, ts, fs) -> "if (" ^ string_of_expr e ^ ") " ^ string_of_stmt ts ^ 
       " else " ^ string_of_stmt fs *)
      | For(name, min, n, order, s) -> 
          (p ^ (sprintf "%s (%s: %s, %s) {\n" 
                  (if order then "for" else "pfor")
                  name (string_of_expr min) (string_of_expr n)) ^
             (string_stmt sp s) ^ 
             p ^ "}\n")
      | Block(stmts) -> 
          (p ^ "{" ^ "\n" ^
             String.concat "" (List.map (string_stmt sp) stmts) ^
             p ^ "}\n")
      (* | Reduce(op, e, mr) -> string_of_memref mr ^ string_of_reduce_op op ^ string_of_expr e *)
      | Store(e, b, i) -> 
          (p ^ string_of_buffer b ^ "[" ^ string_of_expr i ^ "] = \n" ^
             sp ^ string_of_expr e ^ ";\n")             
        
      | Pipeline(name, ty, size, produce, consume) -> 
          (p ^ "pipeline " ^ name ^ "[" ^ string_of_expr size ^ "] {\n" ^ 
             string_stmt sp produce ^ 
             p ^ "} consumer {" ^ "\n" ^ 
             string_stmt sp consume ^
             p ^ "}\n")
      | Print (m, l) -> p ^ "Print(" ^ m ^ (String.concat ", " (List.map string_of_expr l)) ^ ")"
          
  in
  string_stmt "" stmt
    
(*
and string_of_reduce_op = function
  | AddEq -> "+="
  | SubEq -> "-="
  | MulEq -> "*="
  | DivEq -> "/="
 *)

and string_of_buffer b = b

and string_of_toplevel (a, s) = "func(" ^ String.concat ", " (List.map string_of_arg a) ^ ") =\n" ^ (string_of_stmt s)

and string_of_arg = function 
  | Buffer (b) -> b
  | Scalar (s, t) -> (string_of_val_type t) ^ " " ^ s

and string_of_definition (name, args, rtype, body) =
  let s = name ^ "(" ^
          String.concat ", "
            (List.map (fun (t,n) -> n ^ ":" ^ (string_of_val_type t)) args) ^
          ")" ^ " -> " ^ (string_of_val_type rtype)
  in
  match body with
    | Pure(e) -> s ^ " = " ^ (string_of_expr e)
    | _ -> s ^ " = {IMPURE}"

and string_of_environment env =
  let (_,defs) = List.split (Environment.bindings env) in
  String.concat "\n" (List.map string_of_definition defs)
