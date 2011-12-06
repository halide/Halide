open Llvm
open Ir

exception UnsupportedType of Ir.val_type
exception MissingEntrypoint
exception UnimplementedInstruction
exception UnalignedVectorMemref
exception CGFailed of string
exception ArgExprOfBufferArgument
exception ArgTypeMismatch of Ir.val_type * Ir.val_type
exception BCWriteFailed of string

(* These are not parallel, because Architecture overrides module/context used by cg_entry
 * TODO: make parallel? *)
type cg_entry = llcontext -> llmodule -> entrypoint -> llvalue
type cg_expr = expr -> llvalue
type cg_stmt = stmt -> llvalue

module type Architecture = sig
  (* TODO: rename codegen_entry to cg_entry -- internal codegen becomes codegen_entry *)
  val codegen_entry : llcontext -> llmodule -> cg_entry -> entrypoint -> llvalue
  val cg_expr : llcontext -> llmodule -> llbuilder -> cg_expr -> expr -> llvalue
  val cg_stmt : llcontext -> llmodule -> llbuilder -> cg_stmt -> stmt -> llvalue
  val malloc  : llcontext -> llmodule -> llbuilder -> cg_expr -> expr -> llvalue
  val free    : llcontext -> llmodule -> llbuilder -> llvalue -> llvalue
  val env : environment
end

module type Codegen = sig
  (* codegen_entry entry -> ctx, module, function *)
  val codegen_entry : entrypoint -> llcontext * llmodule * llvalue

  (* codegen_c_wrapper ctx mod func -> wrapper_func *)
  val codegen_c_wrapper : llcontext -> llmodule -> llvalue -> llvalue

  (* save_bc_to_file mod filename -> () *)
  val save_bc_to_file : llmodule -> string -> unit

  (* codegen_c_header entry filename -> () *)
  val codegen_c_header : entrypoint -> string -> unit

  (* codegen_to_bitcode_and_header entry -> () - infers filenames from entrypoint name *)
  val codegen_to_bitcode_and_header : entrypoint -> unit

  (* codegen_to_file entry filename -> () *)
  val codegen_to_file : entrypoint -> string -> unit
end

module CodegenForArch : functor ( Arch : Architecture ) -> Codegen

module CodegenForHost : Codegen
