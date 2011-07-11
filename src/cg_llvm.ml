open Ir
open Llvm

let dbgprint = true

let entrypoint_name = "_im_main"
let caml_entrypoint_name = entrypoint_name ^ "_caml_runner"

exception UnsupportedType of val_type
exception MissingEntrypoint
exception UnimplementedInstruction

(* An exception for debugging *)
exception WTF

let buffer_t c = pointer_type (i8_type c)

(* Function to encapsulate shared state for primary codegen *)
let codegen_root (c:llcontext) (m:llmodule) (b:llbuilder) (s:stmt) =

  let int_imm_t = i32_type c in
  let float_imm_t = float_type c in

  let rec type_of_val_type t = match t with
    | UInt(1) | Int(1) -> i1_type c
    | UInt(8) | Int(8) -> i8_type c
    | UInt(16) | Int(16) -> i16_type c
    | UInt(32) | Int(32) -> i32_type c
    | UInt(64) | Int(64) -> i64_type c
    | Float(32) -> float_type c
    | Float(64) -> double_type c
    | Vector(t, n) -> vector_type (type_of_val_type t) n
    | _ -> raise (UnsupportedType(t))
  in

  let ptr_to_buffer buf =
    (* TODO: put buffers in their own LLVM memory spaces *)
    match lookup_function entrypoint_name m with
      | Some(f) -> param f (buf-1)
      | None -> raise (MissingEntrypoint)
  in

  (* The symbol table for loop variables *)
  let sym_table =
    Hashtbl.create 10 
  in

  let sym_add name llv =
    Hashtbl.add sym_table name llv
  and sym_remove name =
    Hashtbl.remove sym_table name
  and sym_get name =
    Hashtbl.find sym_table name
  in

  let rec codegen_expr = function
    (* constants *)
    | IntImm(i) | UIntImm(i) -> const_int   (int_imm_t)   i
    | FloatImm(f)            -> const_float (float_imm_t) f

    (* cast *)
    | Cast(t,e) -> codegen_cast t e

    (* TODO: coding style: use more whitespace, fewer parens in matches? *)

    (* arithmetic *)
    (* Arithmetic on vector types uses the same build calls as the scalar versions *)
    (* TODO: refactor into common build_binop? *)
    (* Float *)
    | Add(Float(_), (l, r))
    | Add(Vector(Float(_),_), (l, r)) -> build_fadd (codegen_expr l) (codegen_expr r) "" b

    | Sub(Float(_), (l, r))
    | Sub(Vector(Float(_),_), (l, r)) -> build_fsub (codegen_expr l) (codegen_expr r) "" b

    | Mul(Float(_), (l, r))
    | Mul(Vector(Float(_),_), (l, r)) -> build_fmul (codegen_expr l) (codegen_expr r) "" b

    | Div(Float(_), (l, r))
    | Div(Vector(Float(_),_), (l, r)) -> build_fdiv (codegen_expr l) (codegen_expr r) "" b

    (* Int/UInt *)
    | Add(_, (l, r))       -> build_add  (codegen_expr l) (codegen_expr r) "" b
    | Sub(_, (l, r))       -> build_sub  (codegen_expr l) (codegen_expr r) "" b
    | Mul(_, (l, r))       -> build_mul  (codegen_expr l) (codegen_expr r) "" b
    | Div(Int(_), (l, r))  -> build_sdiv (codegen_expr l) (codegen_expr r) "" b
    | Div(UInt(_), (l, r)) -> build_udiv (codegen_expr l) (codegen_expr r) "" b

    (* memory *)
    | Load(t, mr) -> build_load (codegen_memref mr t) "" b

    (* Loop variables *)
    | Var(name) -> sym_get name

    (* TODO: fill out other ops *)
    | _ -> raise UnimplementedInstruction

  and codegen_cast t e =
    match (val_type_of_expr e, t) with

      | (UInt(fbits),Int(tbits)) when fbits > tbits ->
          (* truncate to t-1 bits, then zero-extend to t bits to avoid sign bit *)
          build_zext
            (build_trunc (codegen_expr e) (integer_type c (tbits-1)) "" b)
            (integer_type c tbits) "" b

      | (UInt(fbits),Int(tbits)) when fbits < tbits ->
          build_zext (codegen_expr e) (type_of_val_type t) "" b

      | (Int(fbits),UInt(tbits)) when fbits > tbits ->
          build_trunc (codegen_expr e) (type_of_val_type t) "" b

      | (Int(fbits),UInt(tbits)) when fbits < tbits ->
          (* truncate to f-1 bits, then zero-extend to t bits to avoid sign bit *)
          build_zext
            (build_trunc (codegen_expr e) (integer_type c (fbits-1)) "" b)
            (integer_type c tbits) "" b

      | (UInt(fbits),Int(tbits)) | (Int(fbits),UInt(tbits)) when fbits < tbits ->
          (* do nothing *)
          codegen_expr e

      | (UInt(fbits),UInt(tbits)) when fbits > tbits ->
          build_trunc (codegen_expr e) (type_of_val_type t) "" b

      | (UInt(fbits),UInt(tbits)) ->
          codegen_expr e
          

      (*
      LLVM Reference:
       Instruction::CastOps opcode =
          (SrcBits == DstBits ? Instruction::BitCast :
           (SrcBits > DstBits ? Instruction::Trunc :
            (isSigned ? Instruction::SExt : Instruction::ZExt)));
       *)
   
      (* build_intcast in the C/OCaml interface assumes signed, so only
       * works for Int *)
      | (Int(_),Int(_)) ->
          build_intcast (codegen_expr e) (type_of_val_type t) "" b

      | (Float(fbits),Float(tbits)) ->
          build_fpcast(codegen_expr e) (type_of_val_type t) "" b

      (* TODO: remaining casts *)
      | _ -> raise UnimplementedInstruction



  and codegen_for var_name min max body = 
      (* Emit the start code first, without 'variable' in scope. *)
      let start_val = const_int int_imm_t min in

      (* Make the new basic block for the loop header, inserting after current
       * block. *)
      let preheader_bb = insertion_block b in
      let the_function = block_parent preheader_bb in
      let loop_bb = append_block c "loop" the_function in

      (* Insert an explicit fall through from the current block to the
       * loop_bb. *)
      ignore (build_br loop_bb b);

      (* Start insertion in loop_bb. *)
      position_at_end loop_bb b;

      (* Start the PHI node with an entry for start. *)
      let variable = build_phi [(start_val, preheader_bb)] var_name b in

      (* Within the loop, the variable is defined equal to the PHI node. *)
      sym_add var_name variable;

      (* Emit the body of the loop.  This, like any other expr, can change the
       * current BB.  Note that we ignore the value computed by the body, but
       * don't allow an error *)
      ignore (codegen_stmt body);

      (* Emit the updated counter value. *)
      let next_var = build_add variable (const_int int_imm_t 1) "nextvar" b in

      (* Compute the end condition. *)
      let end_cond = build_icmp Icmp.Slt next_var (const_int int_imm_t max) "" b in

      (* Create the "after loop" block and insert it. *)
      let loop_end_bb = insertion_block b in
      let after_bb = append_block c "afterloop" the_function in

      (* Insert the conditional branch into the end of loop_end_bb. *)
      ignore (build_cond_br end_cond loop_bb after_bb b);

      (* Any new code will be inserted in after_bb. *)
      position_at_end after_bb b;

      (* Add a new entry to the PHI node for the backedge. *)
      add_incoming (next_var, loop_end_bb) variable;

      (* Remove the variable binding *)
      sym_remove var_name;      

      (* Return an ignorable llvalue *)
      const_int int_imm_t 0

  and codegen_stmt = function
    | Store(e, mr) ->
        let ptr = codegen_memref mr (val_type_of_expr e) in
          build_store (codegen_expr e) ptr b
    | Map( { name=n; range=(min, max) }, stmt) ->
        codegen_for n min max stmt
    | Block (first::second::rest) ->
        ignore(codegen_stmt first);
        codegen_stmt (Block (second::rest))
    | Block(first::[]) ->
        codegen_stmt first
    | Block _ -> raise WTF
    | _ -> raise UnimplementedInstruction

  and codegen_memref mr vt =
    (* load the global buffer** *)
    let base = ptr_to_buffer mr.buf in
    (* cast pointer to pointer-to-target-type *)
    let ptr = build_pointercast base (pointer_type (type_of_val_type vt)) "" b in
    (* build getelementpointer into buffer *)
    build_gep ptr [| codegen_expr mr.idx |] "" b


  in

    (* actually generate from the root statement, returning the result *)
    codegen_stmt s


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
    ignore (codegen_root c m b s);

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

  (* ba is an llvalue of the pointer generated by:
   *   GenericValue.of_pointer some_bigarray_object *)
  let codegen_bigarray_to_buffer (ba:llvalue) =
    (* fetch object pointer = ((void* )val)+1 *)
    let field_ptr = build_gep ba [| const_int (i32_type c) 1 |] "" b in
    (* deref object pointer *)
    let ptr = build_load field_ptr "" b in
      (* cast to buffer_t for passing into im function *)
      build_pointercast ptr (buffer_t c) "" b
  in

  let args = Array.mapi 
               (fun i p ->
                  if is_buffer p then
                    codegen_bigarray_to_buffer (param wrapper i)
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
