open Llvm
open Ir
open Ir_printer
open Util
open Cg_llvm_util
open Analysis

type state = int (* dummy - we don't use anything in this Arch for now *)
type context = state cg_context
let start_state () = 0
(* TODO: track current surrounding thread/block nest scope.
 * should allow malloc to compute correct offsets. *)

let is_simt_var name =
  let name = base_name name in
  List.exists
    (fun v -> name = v)
    ["threadidx"; "threadidy"; "threadidz"; "threadidw"; "blockidx"; "blockidy"; "blockidz"; "blockidz"]

exception Unknown_intrinsic of string

let pointer_size = 8

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

let cg_expr con e = con.cg_expr e

let rec cg_stmt con = function
  | For (name, base, width, ordered, body) when is_simt_var name ->
      (* TODO: loop needs to be turned into If (which we don't have in our IR), not dropped *)
      Printf.eprintf "Dropping %s loop on %s (%s..%s)\n%!"
        (if ordered then "serial" else "parallel") name (string_of_expr base) (string_of_expr width);
      assert (not ordered);

      let b = con.b
      and c = con.c
      and cg_expr = cg_expr con
      and cg_stmt = cg_stmt con
      and sym_add = con.sym_add
      and sym_remove = con.sym_remove in

      (* Drop this explicit loop, and just SIMTfy variable references in its body *)
      (* Add base to all references to name inside loop, since CTA and Thread IDs start from 0 *)
      let simtvar = (Call (i32, simt_intrinsic name, [])) in
      let loopvar = simtvar +~ base in

      (* create the basic blocks for the (start of the) loop body, and continuing after the loop *)
      let the_function = block_parent (insertion_block b) in
      let loop_bb = append_block c (name ^ "_simt_loop") the_function in
      let after_bb = append_block c (name ^ "_simt_afterloop") the_function in
      
      (* conditionally jump into the loop, if our thread corresponds to a valid iteration *)
      let cond = Cmp(LT, simtvar, width) in
      Printf.eprintf " for -> if (%s)\n%!" (string_of_expr cond);
      (* dump_module con.m; *)

      ignore (build_cond_br (cg_expr cond) loop_bb after_bb b);
      (* ignore (build_br loop_bb b); *)

      (* Start insertion in loop_bb. *)
      position_at_end loop_bb b;

      (* push the loop variable into scope *)
      sym_add name (cg_expr loopvar);

      (* codegen the body *)
      cg_stmt body;

      (* pop the loop variable *)
      sym_remove name;

      (* Insert branch out of if. *)
      ignore (build_br after_bb b);

      (* Any new code will be inserted in after_bb. *)
      position_at_end after_bb b;

      (* Return an ignorable llvalue *)
      const_int (i32_type c) 0

  | stmt -> con.cg_stmt stmt

let rec codegen_entry dev_ctx dev_mod cg_entry entry =
  raise (Wtf "Direct use of Ptx_dev.codegen_entry is not supported")

let malloc = (fun _ _ _ _ -> raise (Wtf "No malloc for PTX yet"))
let free = (fun _ _ -> raise (Wtf "No free for PTX yet"))

let env =
  let ntid_decl   = (".llvm.ptx.read.ntid.x", [], i32, Extern) in
  let nctaid_decl = (".llvm.ptx.read.nctaid.x", [], i32, Extern) in

  let e = Environment.empty in
  let e = Environment.add "llvm.ptx.read.nctaid.x" nctaid_decl e in
  let e = Environment.add "llvm.ptx.read.ntid.x" ntid_decl e in

  e
