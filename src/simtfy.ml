open Ir
open Ir_printer

let is_simt_var name =
  let name = base_name name in
  List.exists
    (fun v -> name = v)
    ["threadidx"; "threadidy"; "threadidz"; "threadidw"; "blockidx"; "blockidy"; "blockidz"; "blockidz"]

exception Unknown_intrinsic of string

(* TODO: replace references to loop bounds with blockDim, gridDim? *)
let simt_intrinsic name =
  (* TODO: pass through dotted extern function names *)
  match base_name name with
    | "threadidx" -> ".llvm.ptx.read.tid.x"
    | "threadidy" -> ".llvm.ptx.read.tid.y"
    | "threadidz" -> ".llvm.ptx.read.tid.z"
    | "threadidw" -> ".llvm.ptx.read.tid.w"
    | "blockidx"  -> ".llvm.ptx.read.ctaid.x"
    | "blockidy"  -> ".llvm.ptx.read.ctaid.y"
    | "blockidz"  -> ".llvm.ptx.read.ctaid.z"
    | "blockidw"  -> ".llvm.ptx.read.ctaid.w"
    | n -> raise (Unknown_intrinsic n)
    (* Can also support:
        laneid
        warpid
        nwarpid
        smid
        nsmid
        gridid
        clock
        clock64
        pm0
        pm1
        pm2
        pm3 *)

let rec simtfy_expr = function
  (* Turn SIMT variables into extern function calls to the corresponding intrinsics *)
  | Var (t, name) when is_simt_var name ->
      assert (t = i32); (* Only makes sense with index types *)
      Call (t, simt_intrinsic name, [])
  
  (* Recurse into subexpressions *)
  | Cast (t, e) -> Cast (t, simtfy_expr e)
  
  | Bop (op, a, b) -> Bop (op, simtfy_expr a, simtfy_expr b)
  | Cmp (op, a, b) -> Cmp (op, simtfy_expr a, simtfy_expr b)

  | And (a, b) -> And (simtfy_expr a, simtfy_expr b)
  | Or  (a, b) ->  Or (simtfy_expr a, simtfy_expr b)
  | Not (a)    -> Not (simtfy_expr a)

  | Select (c, a, b) -> Select (simtfy_expr c, simtfy_expr a, simtfy_expr b)

  | Load (t, buf, idx) -> Load (t, buf, simtfy_expr idx)

  | Call (t, name, args) -> Call (t, name, List.map simtfy_expr args)

  | Let (name, a, b) -> Let (name, simtfy_expr a, simtfy_expr b)

  | Debug (e, str, exprs) -> Debug (simtfy_expr e, str, List.map simtfy_expr exprs)
  
  (* We actually shouldn't expect vectors to make it into SIMT code usually... *)
  | MakeVector (exprs) -> MakeVector (List.map simtfy_expr exprs)
  | Broadcast (e, w) -> Broadcast (simtfy_expr e, w)
  | Ramp (a, b, w) -> Ramp (simtfy_expr a, simtfy_expr b, w)
  | ExtractElement (v, i) -> ExtractElement (simtfy_expr v, simtfy_expr i)
  
  (* Pass through *)
  | e -> e
  

let rec simtfy_stmt = function
  | For (name, base, width, ordered, body) when is_simt_var name ->
      Printf.printf "Dropping %s loop on %s (%s..%s)\n"
        (if ordered then "serial" else "parallel") name (string_of_expr base) (string_of_expr width);
      assert (not ordered);
      (* Drop this explicit loop, and just SIMTfy variable references in its body *)
      simtfy_stmt body

  (* Recurse *)
  | For (name, base, width, ordered, body) ->
      For (name, simtfy_expr base, simtfy_expr width, ordered, simtfy_stmt body)
  | Block (stmts) -> Block (List.map simtfy_stmt stmts)
  | Store (e, buf, idx) -> Store (simtfy_expr e, buf, simtfy_expr idx)
  | Pipeline (name, t, e, produce, consume) ->
      Pipeline (name, t, simtfy_expr e, simtfy_stmt produce, simtfy_stmt consume)
  | Print (str, exprs) -> Print (str, List.map simtfy_expr exprs)
