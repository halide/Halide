open Ir
open Llvm

(* TODO idea: could we encapsulate the recursive codegen functions in a local
 * scope within the main codegen function which was implicitly closed over the
 * context/module pointers? *)

let dbgprint = true

type context = llcontext * llmodule * llbuilder

let int_imm_t ctx = match ctx with (c,_,_) -> i32_type c
let float_imm_t ctx = match ctx with (c,_,_) -> float_type c
let buffer_t ctx = match ctx with (c,_,_) -> pointer_type (i8_type c)

exception UnsupportedType of val_type
let type_of_val_type ctx t = match ctx with (c,_,_) ->
  match t with
    | UInt(1) | Int(1) -> i1_type c
    | UInt(8) | Int(8) -> i8_type c
    | UInt(16) | Int(16) -> i16_type c
    | UInt(32) | Int(32) -> i32_type c
    | UInt(64) | Int(64) -> i1_type c
    | Float(32) -> float_type c
    | Float(64) -> double_type c
    | _ -> raise (UnsupportedType(t))

let type_of_expr ctx e = type_of_val_type ctx (val_type_of_expr e)

let buffer_name (b:buffer) = "buf" ^ string_of_int b

let ptr_to_buffer (ctx:context) (b:buffer) = match ctx with (c,m,_) ->
  (* TODO: put buffers in their own memory spaces *)
  let g = lookup_global (buffer_name b) m in match g with
    | Some(decl) -> decl
    | None -> declare_global (buffer_t ctx) (buffer_name b) m

(* Convention: codegen functions unpack context into c[ontext], m[odule],
 * b[uffer], if they need them, with pattern-matching. *)

let rec codegen_expr (ctx:context) e = match ctx with (_,_,b) ->
  match e with
    (* constants *)
    | IntImm(i) | UIntImm(i) -> const_int (int_imm_t ctx) i
    | FloatImm(f) -> const_float (float_imm_t ctx) f

    (* TODO: codegen Cast *)

    (* arithmetic *)
    | Add(_, (l, r)) -> build_add (codegen_expr ctx l) (codegen_expr ctx r) "" b

    (* memory *)
    | Load(t, mr) -> build_load (codegen_memref ctx mr t) "" b

    | _ -> build_ret_void b (* TODO: this is our simplest NOP *)

and codegen_stmt (ctx:context) s = match ctx with (_,_,b) ->
  match s with
    | Store(e, mr) ->
        let ptr = codegen_memref ctx mr (val_type_of_expr e) in
          build_store (codegen_expr ctx e) ptr b
    | _ -> build_ret_void b (* TODO: this is our simplest NOP *)

and codegen_memref (ctx:context) mr vt = match ctx with (_,_,b) ->
  let base = build_load (ptr_to_buffer ctx mr.buf) "" b in
  let ptr = build_gep base [| codegen_expr ctx mr.idx |] "" b in
    build_pointercast ptr (pointer_type (type_of_val_type ctx vt)) "" b

let codegen s =
  let c = create_context () in
  let m = create_module c "<fimage>" in
  let main = define_function "main" (function_type (void_type c) [| |]) m in
  let b = builder_at_end c (entry_block main) in
    ignore (codegen_stmt (c,m,b) s);
    ignore (build_ret_void b);
    if dbgprint then dump_module m;
    m

exception BCWriteFailed of string

let codegen_to_file filename s =
  let m = codegen s in
    if not (Llvm_bitwriter.write_bitcode_file m filename) then
      raise(BCWriteFailed(filename));
    dispose_module m
