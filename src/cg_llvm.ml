open Ir
open Llvm

(* TODO idea: could we encapsulate the recursive codegen functions in a local
 * scope within the main codegen function which was implicitly closed over the
 * context/module pointers? *)

let dbgprint = true

let entrypoint_name = "_im_main"

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
    | UInt(64) | Int(64) -> i1_type c
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

(* Convention: codegen functions unpack context into c[ontext], m[odule],
 * b[uffer], if they need them, with pattern-matching.
 * TODO: cleaner way to carry and match this context state? It may grow... *)

let rec codegen_expr ctx e = match ctx with (c,_,b) ->
  match e with
    (* constants *)
    | IntImm(i) | UIntImm(i) -> const_int (int_imm_t c) i
    | FloatImm(f) -> const_float (float_imm_t c) f

    (* TODO: codegen Cast *)

    (* arithmetic *)
    | Add(_, (l, r)) -> build_add (codegen_expr ctx l) (codegen_expr ctx r) "" b

    (* memory *)
    | Load(t, mr) -> build_load (codegen_memref ctx mr t) "" b

    | _ -> build_ret_void b (* TODO: this is our simplest NOP *)

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

let codegen s =
  (* construct basic LLVM state *)
  let c = create_context () in
  let m = create_module c "<fimage>" in

  (* enumerate all referenced buffers *)
  let buffers = buffers_in_stmt s in

    (* TODO: assert that all buffer IDs are represented in ordinal positions in list? *)
    (* TODO: build and carry buffer ID -> param Llvm.value map *)
    (* TODO: set readonly attributes on buffer args which aren't written *)

  (* define `void main(buf1, buf2, ...)` entrypoint*)
  let buf_args =
    Array.of_list (List.map (fun b -> buffer_t c) (BufferSet.elements buffers)) in
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

    (* return generated module *)
    m

exception BCWriteFailed of string
exception CGFailed of string

let codegen_to_file filename s =
  let m = codegen s in
    (* verify the generated module *)
    match Llvm_analysis.verify_module m with
      | Some reason -> raise(CGFailed(reason))
      | None -> ();
    match Llvm_bitwriter.write_bitcode_file m filename with
      | false -> raise(BCWriteFailed(filename))
      | true -> ();
    dispose_module m
