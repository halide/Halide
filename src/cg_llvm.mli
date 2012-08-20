open Llvm
open Ir
open Cg_llvm_util

exception MissingEntrypoint
exception UnimplementedInstruction
exception UnalignedVectorMemref
exception ArgExprOfBufferArgument
exception ArgTypeMismatch of Ir.val_type * Ir.val_type

(* These are not parallel, because Architecture overrides module/context used by cg_entry
 * TODO: make parallel? *)
type cg_entry = llcontext -> llmodule -> entrypoint -> llvalue
type 'a make_cg_context = llcontext -> llmodule -> llbuilder ->
                          (string, llvalue) Hashtbl.t -> 'a -> 'a cg_context
(*
type cg_expr = expr -> llvalue
type cg_stmt = stmt -> llvalue
*)


module type Architecture = sig
  type state
  type context = state cg_context

  val target_triple : string
  val start_state : unit -> state

  (* TODO: rename codegen_entry to cg_entry -- internal codegen becomes codegen_entry *)
  val codegen_entry : llcontext -> llmodule -> cg_entry -> state make_cg_context ->
                      entrypoint -> llvalue
  val cg_expr : context -> expr -> llvalue
  val cg_stmt : context -> stmt -> llvalue
  val malloc  : context -> string -> expr -> expr -> (llvalue * (context -> unit))
  val env : environment
  val pointer_size : int
end

module type Codegen = sig
  type arch_state
  type context = arch_state cg_context

  (* make_cg_context ctx module builder symtab -> cg_context *)
  val make_cg_context : arch_state make_cg_context

  (* codegen_entry entry -> ctx, module, function *)
  val codegen_entry : entrypoint -> llcontext * llmodule * llvalue

  (* codegen_c_wrapper ctx mod func -> wrapper_func *)
  val codegen_c_wrapper : llcontext -> llmodule -> llvalue -> llvalue

  (* codegen_to_bitcode_and_header entry -> () - infers filenames from entrypoint name *)
  val codegen_to_bitcode_and_header : entrypoint -> unit

  (* codegen_to_file entry filename -> () *)
  val codegen_to_file : entrypoint -> string -> unit
end

module CodegenForArch : functor ( Arch : Architecture ) ->
  ( Codegen with type arch_state = Arch.state )
