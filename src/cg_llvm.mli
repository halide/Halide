exception UnsupportedType of Ir.val_type
exception MissingEntrypoint
exception UnimplementedInstruction
exception UnalignedVectorMemref
exception CGFailed of string
exception ArgExprOfBufferArgument
exception ArgTypeMismatch of Ir.val_type * Ir.val_type
exception BCWriteFailed of string

val codegen_to_c_callable :
  Llvm.llcontext -> Ir.entrypoint -> Architecture.architecture -> Llvm.llmodule * Llvm.llvalue
val codegen_to_bitcode_and_header : Ir.entrypoint -> Architecture.architecture -> unit
val codegen_to_file : string -> Ir.entrypoint -> Architecture.architecture -> unit

