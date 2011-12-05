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

module type Architecture = sig
  (* val codegen : llcontext -> llmodule -> entrypoint -> llvalue *)
  val cg_expr : llcontext -> llmodule -> llbuilder -> (expr -> llvalue) -> expr -> llvalue
  val cg_stmt : llcontext -> llmodule -> llbuilder -> (stmt -> llvalue) -> stmt -> llvalue
  val malloc : llcontext -> llmodule -> llbuilder -> (expr -> llvalue) -> expr -> llvalue
  val free : llcontext -> llmodule -> llbuilder -> llvalue -> llvalue
  val initial_module : llcontext -> llmodule
  (* val env : environment *)
end

module type Codegen = sig
  val codegen_to_c_callable :
    Llvm.llcontext -> Ir.entrypoint -> Llvm.llmodule * Llvm.llvalue
  val codegen_to_bitcode_and_header : Ir.entrypoint -> unit
  val codegen_to_file : string -> Ir.entrypoint -> unit
end

module CodegenForArch : functor ( Arch : Architecture ) -> Codegen

module CodegenForHost : Codegen