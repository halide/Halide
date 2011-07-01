open Ir
open Llvm

(* TODO idea: could we encapsulate the recursive codegen functions in a local
 * scope within the main codegen function which was implicitly closed over the
 * context/module pointers? *)

let dbgprint = true

let entrypoint_name = "_im_main"
let caml_entrypoint_name = entrypoint_name ^ "_caml_runner"

type context = llcontext * llmodule * llbuilder

let int_imm_t c = i32_type c
let float_imm_t c = float_type c
let buffer_t c = pointer_type (i8_type c)

exception UnsupportedType of val_type
let type_of_val_type ctx t = match ctx with (c,_,_) ->
  match t with
    | UInt(1) | Int(1) -> i1_type c
    | UInt(8) | Int(8) -> i8_type c
    | UInt(16) | Int(16) -> i16_type c
    | UInt(32) | Int(32) -> i32_type c
    | UInt(64) | Int(64) -> i64_type c
    | Float(32) -> float_type c
    | Float(64) -> double_type c
    | _ -> raise (UnsupportedType(t))

let type_of_expr ctx e = type_of_val_type ctx (val_type_of_expr e)

let buffer_name b = "buf" ^ string_of_int b

exception MissingEntrypoint
let ptr_to_buffer ctx b = match ctx with (c,m,_) ->
  (* TODO: put buffers in their own memory spaces *)
  match lookup_function entrypoint_name m with
    | Some(f) -> param f (b-1)
    | None -> raise (MissingEntrypoint)

(* this should throw errors if used *)
(* TODO: remove when codegen is ~complete *)
let nop ctx = match ctx with (_,_,b) ->
  build_ret_void b

(* Convention: codegen functions unpack context into c[ontext], m[odule],
 * b[uffer], if they need them, with pattern-matching.
 * TODO: cleaner way to carry and match this context state? It may grow... *)

let rec codegen_expr ctx e = match ctx with (c,_,b) ->
  match e with
    (* constants *)
    | IntImm(i) | UIntImm(i) -> const_int (int_imm_t c) i
    | FloatImm(f) -> const_float (float_imm_t c) f

    (* TODO: codegen Cast *)
    | Cast(t,e) -> codegen_cast ctx t e

    (* arithmetic *)
    | Add(_, (l, r)) -> build_add (codegen_expr ctx l) (codegen_expr ctx r) "" b

    (* memory *)
    | Load(t, mr) -> build_load (codegen_memref ctx mr t) "" b

    (* TODO: fill out other ops *)
    | _ -> nop ctx

and codegen_cast ctx t e = match ctx with (c,_,b) ->
  match (val_type_of_expr e, t) with

    | (UInt(fbits),Int(tbits)) when fbits > tbits ->
        (* truncate to t-1 bits, then zero-extend to t bits to avoid sign bit *)
        build_zext
          (build_trunc (codegen_expr ctx e) (integer_type c (tbits-1)) "" b)
          (integer_type c tbits) "" b

    | (UInt(fbits),Int(tbits)) when fbits < tbits ->
        build_zext (codegen_expr ctx e) (type_of_val_type ctx t) "" b

    | (Int(fbits),UInt(tbits)) when fbits > tbits ->
        build_trunc (codegen_expr ctx e) (type_of_val_type ctx t) "" b

    | (Int(fbits),UInt(tbits)) when fbits < tbits ->
        (* truncate to f-1 bits, then zero-extend to t bits to avoid sign bit *)
        build_zext
          (build_trunc (codegen_expr ctx e) (integer_type c (fbits-1)) "" b)
          (integer_type c tbits) "" b

    | (UInt(fbits),Int(tbits)) | (Int(fbits),UInt(tbits)) when fbits < tbits ->
        (* do nothing *)
        codegen_expr ctx e
(*
LLVM Reference:
 Instruction::CastOps opcode =
    (SrcBits == DstBits ? Instruction::BitCast :
     (SrcBits > DstBits ? Instruction::Trunc :
      (isSigned ? Instruction::SExt : Instruction::ZExt)));
 *)
 
    (* build_intcast in the C/OCaml interface assumes signed, so only
     * works for Int *)
    | (Int(_),Int(_)) -> build_intcast (codegen_expr ctx e) (type_of_val_type ctx t) "" b

    (* TODO: remaining casts *)
    | _ -> nop ctx

and codegen_stmt ctx s = match ctx with (_,_,b) ->
  match s with
    | Store(e, mr) ->
        let ptr = codegen_memref ctx mr (val_type_of_expr e) in
          build_store (codegen_expr ctx e) ptr b
    | _ -> build_ret_void b (* TODO: this is our simplest NOP *)

and codegen_memref ctx mr vt = match ctx with (_,_,b) ->
  (* load the global buffer** *)
  let base = ptr_to_buffer ctx mr.buf in
  (* build getelementpointer into buffer *)
  let ptr = build_gep base [| codegen_expr ctx mr.idx |] "" b in
    (* cast pointer to pointer-to-target-type *)
    build_pointercast ptr (pointer_type (type_of_val_type ctx vt)) "" b

module BufferSet = Set.Make (
struct
  type t = int
  let compare = Pervasives.compare
end)
(*module BufferMap = Map.Make( BufferOrder )*)

let rec buffers_in_stmt = function
  | If(e, s) -> BufferSet.union (buffers_in_expr e) (buffers_in_stmt s)
  | IfElse(e, st, sf) ->
      BufferSet.union (buffers_in_expr e) (
        BufferSet.union (buffers_in_stmt st) (buffers_in_stmt sf))
  | Map(_, s) -> buffers_in_stmt s
  | Block stmts ->
      List.fold_left BufferSet.union BufferSet.empty (List.map buffers_in_stmt stmts)
  | Reduce (_, e, mr) | Store (e, mr) -> BufferSet.add mr.buf (buffers_in_expr e)

and buffers_in_expr = function
  (* immediates, vars *)
  | IntImm _ | UIntImm _ | FloatImm _ | Var _ -> BufferSet.empty

  (* binary ops *)
  | Add(_, (l,r)) | Sub(_, (l,r)) | Mul(_, (l,r)) | Div(_, (l,r)) | EQ(l,r)
  | NEQ(l,r) | LT(l,r) | LTE(l,r) | GT(l,r) | GTE(l,r) | And(l,r) | Or(l,r) ->
      BufferSet.union (buffers_in_expr l) (buffers_in_expr r)

  (* unary ops *)
  | Not e | Cast (_,e) -> buffers_in_expr e

  (* memory ops *)
  | Load (_, mr) -> BufferSet.singleton mr.buf

exception CGFailed of string
let verify_cg m =
    (* verify the generated module *)
    match Llvm_analysis.verify_module m with
      | Some reason -> raise(CGFailed(reason))
      | None -> ()

let codegen c s =
  (* create a new module for this cg result *)
  let m = create_module c "<fimage>" in

  (* enumerate all referenced buffers *)
  let buffers = buffers_in_stmt s in

    (* TODO: assert that all buffer IDs are represented in ordinal positions in list? *)
    (* TODO: build and carry buffer ID -> param Llvm.value map *)
    (* TODO: set readonly attributes on buffer args which aren't written *)

  (* define `void main(buf1, buf2, ...)` entrypoint*)
  let buf_args =
    Array.map (fun b -> buffer_t c) (Array.of_list (BufferSet.elements buffers)) in
  let main = define_function entrypoint_name (function_type (void_type c) buf_args) m in

    (* iterate over args and assign name "bufXXX" with `set_value_name s v` *)
    Array.iteri (fun i v -> set_value_name ("buf" ^ string_of_int (i+1)) v) (params main);

  (* start codegen at entry block of main *)
  let b = builder_at_end c (entry_block main) in

    (* codegen body *)
    ignore (codegen_stmt (c,m,b) s);

    (* return void from main *)
    ignore (build_ret_void b);

    if dbgprint then dump_module m;

    ignore (verify_cg m);

    (* return generated module and function *)
    (m,main)

exception BCWriteFailed of string

let codegen_to_file filename s =
  (* construct basic LLVM state *)
  let c = create_context () in

  (* codegen *)
  let (m,_) = codegen c s in

    (* write to bitcode file *)
    match Llvm_bitwriter.write_bitcode_file m filename with
      | false -> raise(BCWriteFailed(filename))
      | true -> ();

    (* free memory *)
    dispose_module m

(*
 * Wrappers
 *)
(* ba is an llvalue of the pointer generated by:
 *   GenericValue.of_pointer some_bigarray_object *)
let codegen_bigarray_to_buffer ctx (ba:llvalue) = match ctx with (c,_,b) ->
  (* fetch object pointer = ((void* )val)+1 *)
  let field_ptr = build_gep ba [| const_int (i32_type c) 1 |] "" b in
  (* deref object pointer *)
  let ptr = build_load field_ptr "" b in
    (* cast to buffer_t for passing into im function *)
    build_pointercast ptr (buffer_t c) "" b

let codegen_caml_wrapper c m f =

  let is_buffer p = type_of p = buffer_t c in

  let wrapper_args = Array.map
                       (fun p ->
                          if is_buffer p then pointer_type (buffer_t c)
                          else type_of p)
                       (params f) in

  let wrapper = define_function
                  (caml_entrypoint_name)
                  (function_type (void_type c) wrapper_args)
                  m in

  let b = builder_at_end c (entry_block wrapper) in

  let args = Array.mapi 
               (fun i p ->
                  if is_buffer p then
                    codegen_bigarray_to_buffer (c,m,b) (param wrapper i)
                  else
                    param wrapper i)
               (params f) in

    (* codegen the call *)
    ignore (build_call f args "" b);

    (* return *)
    ignore (build_ret_void b);

    if dbgprint then dump_module m;

    ignore (verify_cg m);

    (* return the wrapper function *)
    wrapper

let codegen_to_ocaml_callable s =
  (* construct basic LLVM state *)
  let c = create_context () in

  (* codegen *)
  let (m,f) = codegen c s in

  (* codegen the wrapper *)
  let w = codegen_caml_wrapper c m f in

    (m,w)
