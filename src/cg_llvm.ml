open Ir
open Llvm
open List
open Util
open Ir_printer

let dbgprint = true

let entrypoint_name = "_im_main"
let caml_entrypoint_name = entrypoint_name ^ "_caml_runner"
let c_entrypoint_name = entrypoint_name ^ "_runner"
  
exception UnsupportedType of val_type
exception MissingEntrypoint
exception UnimplementedInstruction
exception UnalignedVectorMemref
exception CGFailed of string
exception ArgExprOfBufferArgument (* The Arg expr can't dereference a Buffer, only Scalars *)
exception ArgTypeMismatch of val_type * val_type

let buffer_t c = pointer_type (i8_type c)

(* Algebraic type wrapper for LLVM comparison ops *)
type cmp =
  | CmpInt of Icmp.t
  | CmpFloat of Fcmp.t

module ArgMap = Map.Make(String)
type argmap = (arg*int) ArgMap.t (* track args as name -> (Ir.arg,index) *)

let verify_cg m =
    (* verify the generated module *)
    match Llvm_analysis.verify_module m with
      | Some reason -> raise(CGFailed(reason))
      | None -> ()

let codegen (c:llcontext) (e:entrypoint) =

  let int_imm_t = i32_type c in
  let int32_imm_t = i32_type c in
  let float_imm_t = float_type c in
  let buffer_t = buffer_t c in

  let type_of_val_type t = match t with
    | UInt(1) | Int(1) -> i1_type c
    | UInt(8) | Int(8) -> i8_type c
    | UInt(16) | Int(16) -> i16_type c
    | UInt(32) | Int(32) -> i32_type c
    | UInt(64) | Int(64) -> i64_type c
    | Float(32) -> float_type c
    | Float(64) -> double_type c
    | IntVector( 1, n) | UIntVector( 1, n) -> vector_type (i1_type c) n
    | IntVector( 8, n) | UIntVector( 8, n) -> vector_type (i8_type c) n
    | IntVector(16, n) | UIntVector(16, n) -> vector_type (i16_type c) n
    | IntVector(32, n) | UIntVector(32, n) -> vector_type (i32_type c) n
    | IntVector(64, n) | UIntVector(64, n) -> vector_type (i64_type c) n
    | FloatVector(32, n) -> vector_type (float_type c) n
    | FloatVector(64, n) -> vector_type (double_type c) n
    | _ -> raise (UnsupportedType(t))
  in

  let elem_type_of_vector_val_type t = match t with
    | IntVector( 1, n) | UIntVector( 1, n) -> i1_type c
    | IntVector( 8, n) | UIntVector( 8, n) -> i8_type c
    | IntVector(16, n) | UIntVector(16, n) -> i16_type c
    | IntVector(32, n) | UIntVector(32, n) -> i32_type c
    | IntVector(64, n) | UIntVector(64, n) -> i64_type c
    | FloatVector(32, n) -> float_type c
    | FloatVector(64, n) -> double_type c
    | _ -> raise (UnsupportedType(t))
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

  (* create a new module for this cg result *)
  let m = create_module c "<fimage>" in
  
  (* unpack the entrypoint *)
  let arglist,s = e in

  let type_of_arg = function
    | Scalar (_, vt) -> type_of_val_type vt
    | Buffer _ -> buffer_t
  in
  let name_of_arg = function
    | Scalar (n,_) | Buffer n -> n
  in
  let index_of_arg a =
    let rec idx a l i = match l with
      | hd::_ when hd = a -> i
      | hd::rest -> idx a rest i+1
      | [] -> raise (Wtf "index_of_arg for arg not in arglist")
    in idx a arglist 0
  in

  let argtypes = List.map type_of_arg arglist in

  let argmap =
    List.fold_left
      (fun m arg -> ArgMap.add (name_of_arg arg) (arg,(index_of_arg arg)) m)
      ArgMap.empty
      arglist
  in

  (* define `void main(arg1, arg2, ...)` entrypoint*)
  let entrypoint_fn =
    define_function
      entrypoint_name
      (function_type (void_type c) (Array.of_list argtypes))
      m
  in

  (* assign names to args in LLVM *)
  Array.iteri
    (fun i v -> set_value_name (name_of_arg (List.nth arglist i)) v)
    (params entrypoint_fn);

  (* start codegen at entry block of main *)
  let b = builder_at_end c (entry_block entrypoint_fn) in
  
  let alloca_bb = append_block c ("alloca") (block_parent (insertion_block b)) in
  ignore(build_br alloca_bb b);
  let after_alloca_bb = append_block c ("after_alloca") (block_parent (insertion_block b)) in
  position_at_end alloca_bb b;  
  let alloca_end = build_br after_alloca_bb b in
  position_at_end after_alloca_bb b;  


  let arg_find name = 
    let arg,idx = ArgMap.find name argmap in
    match arg with Scalar (s,_) | Buffer s -> assert (s = name);
    (arg,idx)
  in
  let arg_get name = match arg_find name with (arg,_) -> arg in
  let arg_idx name = match arg_find name with (_,i) -> i in
  let arg_val name = param entrypoint_fn (arg_idx name) in

  let ptr_to_buffer buf =
    match arg_find buf with
      | (Buffer s), i ->
          assert (s = buf);
          param entrypoint_fn i
      | arg, _ -> raise (Wtf "ptr_to_buffer of non-Buffer argument name")
  in

  let rec cg_expr = function
    (* constants *)
    | IntImm(i) | UIntImm(i) -> const_int   (int_imm_t)   i
    | FloatImm(f)            -> const_float (float_imm_t) f

    (* cast *)
    | Cast(t,e) -> cg_cast t e

    (* TODO: coding style: use more whitespace, fewer parens in matches? *)

    (* Binary operators are generated from builders for int, uint, float types *)
    (* Arithmetic and comparison on vector types use the same build calls as 
     * the scalar versions *)

    (* arithmetic *)
    | Bop(Add, l, r) -> cg_binop build_add  build_add  build_fadd l r
    | Bop(Sub, l, r) -> cg_binop build_sub  build_sub  build_fsub l r
    | Bop(Mul, l, r) -> cg_binop build_mul  build_mul  build_fmul l r
    | Bop(Div, l, r) -> cg_binop build_sdiv build_udiv build_fdiv l r

    (* comparison *)
    | Cmp(EQ, l, r) -> cg_cmp Icmp.Eq  Icmp.Eq  Fcmp.Oeq l r
    | Cmp(NE, l, r) -> cg_cmp Icmp.Ne  Icmp.Ne  Fcmp.One l r
    | Cmp(LT, l, r) -> cg_cmp Icmp.Slt Icmp.Ult Fcmp.Olt l r
    | Cmp(LE, l, r) -> cg_cmp Icmp.Sle Icmp.Ule Fcmp.Ole l r
    | Cmp(GT, l, r) -> cg_cmp Icmp.Sgt Icmp.Ugt Fcmp.Ogt l r
    | Cmp(GE, l, r) -> cg_cmp Icmp.Sge Icmp.Uge Fcmp.Oge l r

    (* Select *)
    | Select(c, t, f) -> 
      begin
        match val_type_of_expr t with          
          | Int _ | UInt _ | Float _ -> 
            build_select (cg_expr c) (cg_expr t) (cg_expr f) "" b
          | IntVector(bits, n) | UIntVector(bits, n) | FloatVector(bits, n) ->
            let mask     = cg_expr c in
            let mask_ext = build_sext mask (type_of_val_type (val_type_of_expr t)) "" b in 
            let mask_t   = build_and mask_ext (cg_expr t) "" b in
            let all_ones = Broadcast(Cast(Int(bits), IntImm(-1)), n) in
            let inv_mask = build_xor mask_ext (cg_expr all_ones) "" b in
            let mask_f   = build_and inv_mask (cg_expr f) "" b in
            build_or mask_t mask_f "" b
      end

    (* memory TODO: handle vector loads and stores better *)
    | Load(t, mr) -> begin
      match (vector_elements t, vector_elements (val_type_of_expr mr.idx)) with 
        (* scalar load *)
        | (1, 1) -> build_load (cg_memref mr t) "" b 
          
        (* vector load of scalar address *)
        | (w, 1) -> begin match Analysis.reduce_expr_modulo mr.idx w with 
            | Some 0      -> build_load (cg_memref mr t) "" b
            | Some offset -> cg_unaligned_load t mr offset
            | None        -> cg_unknown_alignment_load t mr
        end
          
        (* general gather *)
        | (_, _) -> cg_gather t mr
    end
    (* Loop variables *)
    | Var(name) -> sym_get name

    | Arg(vt, name) ->
        (match arg_get name with
          | Scalar (_,avt) when vt <> avt -> raise (ArgTypeMismatch(vt,avt))
          | Buffer _ -> raise ArgExprOfBufferArgument
          | _ -> ());
        arg_val name

    (* Making vectors *)
    | MakeVector(l) -> cg_makevector(l, val_type_of_expr (MakeVector l), 0)

    | Broadcast(e, n) -> 
      let elem_type = val_type_of_expr e in
      let vec_type  = vector_of_val_type elem_type n in
      let expr      = cg_expr e in
      let rec rep = function
        | 0 -> undef (type_of_val_type vec_type)
        | i -> build_insertelement (rep (i-1)) expr (const_int int_imm_t (i-1)) "" b
      in rep n

    (* Unpacking vectors *)
    | ExtractElement(e, n) ->
      let v = cg_expr e in
      let idx = cg_expr (Cast(u32, n)) in
      build_extractelement v idx "" b

    (* TODO: fill out other ops *)
    | _ -> raise UnimplementedInstruction

  and cg_makevector = function
    | ([], t, _) -> undef (type_of_val_type t)
    | (first::rest, t, n) -> build_insertelement
          (cg_makevector (rest, t, n+1))
          (cg_expr first)
          (const_int int32_imm_t n)
          "" b

  and cg_binop iop uop fop l r =
    let build = match val_type_of_expr l with
      | Int _   | IntVector(_,_)   -> iop
      | UInt _  | UIntVector(_,_)  -> uop
      | Float _ | FloatVector(_,_) -> fop
    in
      build (cg_expr l) (cg_expr r) "" b

  and cg_cmp iop uop fop l r =
    cg_binop (build_icmp iop) (build_icmp uop) (build_fcmp fop) l r

  and cg_cast t e =
    (* shorthand for the common case *)
    let simple_cast build e t = build (cg_expr e) (type_of_val_type t) "" b in

    match (val_type_of_expr e, t) with

      (* TODO: cast vector types *)

      | UInt(fb), Int(tb) when fb > tb ->
          (* TODO: factor this truncate-then-zext pattern into a helper? *)
          (* truncate to t-1 bits, then zero-extend to t bits to avoid sign bit *)
          build_zext
            (build_trunc (cg_expr e) (integer_type c (tb-1)) "" b)
            (integer_type c tb) "" b

      | UInt(fb), Int(tb)
      | UInt(fb), UInt(tb) when fb < tb ->
          simple_cast build_zext e t

      (* TODO: what to do for negative sign in Int -> UInt? *)
      | Int(fb), UInt(tb) when fb > tb ->
          simple_cast build_trunc e t
      | Int(fb), UInt(tb) when fb < tb ->
          (* truncate to f-1 bits, then zero-extend to t bits to avoid sign bit *)
          build_zext
            (build_trunc (cg_expr e) (integer_type c (fb-1)) "" b)
            (integer_type c tb) "" b

      | UInt(fb), Int(tb)
      | Int(fb),  UInt(tb)
      | UInt(fb), UInt(tb)
      | Int(fb),  Int(tb) when fb = tb ->
          (* do nothing *)
          cg_expr e

      | UInt(fb), UInt(tb) when fb > tb -> simple_cast build_trunc e t

      (* int <--> float *)
      | Int(_),   Float(_) -> simple_cast build_sitofp e t
      | UInt(_),  Float(_) -> simple_cast build_uitofp e t
      | Float(_), Int(_)   -> simple_cast build_fptosi e t
      | Float(_), UInt(_)  -> simple_cast build_fptoui e t

      (* build_intcast in the C/OCaml interface assumes signed, so only
       * works for Int *)
      | Int(_), Int(_)       -> simple_cast build_intcast e t
      | Float(fb), Float(tb) -> simple_cast build_fpcast  e t

      (* TODO: remaining casts *)
      | _ -> raise UnimplementedInstruction

  and cg_for var_name min max body = 
      (* Emit the start code first, without 'variable' in scope. *)
      let start_val = min (* const_int int_imm_t min *) in

      (* Make the new basic block for the loop header, inserting after current
       * block. *)
      let preheader_bb = insertion_block b in
      let the_function = block_parent preheader_bb in
      let loop_bb = append_block c (var_name ^ "_loop") the_function in

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
      ignore (cg_stmt body);

      (* Emit the updated counter value. *)
      let next_var = build_add variable (const_int int_imm_t 1) (var_name ^ "_nextvar") b in

      (* Compute the end condition. *)
      let end_cond = build_icmp Icmp.Slt next_var max "" b in

      (* Create the "after loop" block and insert it. *)
      let loop_end_bb = insertion_block b in
      let after_bb = append_block c (var_name ^ "_afterloop") the_function in

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

  and cg_stmt = function
    (* TODO: unaligned vector store *)
    | Store(e, mr) -> cg_store e mr
    | Map(n, min, max, stmt) ->
      cg_for n (cg_expr min) (cg_expr max) stmt
    | Block (first::second::rest) ->
      ignore(cg_stmt first);
      cg_stmt (Block (second::rest))
    | Block(first::[]) ->
      cg_stmt first
    | Block _ -> raise (Wtf "cg_stmt of empty block")
    (* | _ -> raise UnimplementedInstruction *)

  and cg_store e mr =
    let elems = (vector_elements (val_type_of_expr mr.idx)) in
    if (elems > 1) then cg_scatter e mr
    else
      let ptr = cg_memref mr (val_type_of_expr e) in
      build_store (cg_expr e) ptr b

  and cg_unaligned_load t mr offset =
    let lower_addr = mr.idx -~ (IntImm(offset)) in
    let upper_addr = lower_addr +~ (IntImm(vector_elements t)) in
    let lower = Load(t, {buf = mr.buf; idx = lower_addr}) in
    let upper = Load(t, {buf = mr.buf; idx = upper_addr}) in
    let lower_indices = offset -- (vector_elements t) in
    let upper_indices = 0 -- offset in
    Printf.printf "lower: %d, upper: %d\n%!" (length lower_indices) (length upper_indices);
    let extract_lower = map (fun x -> ExtractElement(lower, (UIntImm(x)))) lower_indices in
    let extract_upper = map (fun x -> ExtractElement(upper, (UIntImm(x)))) upper_indices in
    let vec = MakeVector (extract_lower @ extract_upper) in
    cg_expr vec

  and cg_unknown_alignment_load t mr =
    let current = insertion_block b in
    position_before alloca_end b;
    let scratch = build_alloca (type_of_val_type t) "" b in      
    position_at_end current b;
    let memcpy     = declare_function "llvm.memcpy.p0i8.p0i8.i32"
      (function_type (void_type c) [|buffer_t; buffer_t; int32_imm_t; int32_imm_t; i1_type c|]) m in
    let stack_addr = build_pointercast scratch buffer_t "" b in
    let mem_addr   = build_pointercast (cg_memref mr t) buffer_t "" b in
    let length     = const_int int_imm_t ((bit_width t)/8) in
    let length64   = const_int (i64_type c) ((bit_width t)/8) in
    let alignment  = const_int int_imm_t ((bit_width (element_val_type t))/8) in
    let volatile   = const_int (i1_type c) 0 in
    let to_stack   = build_call memcpy [|stack_addr; mem_addr; length; alignment; volatile|] "" b in
    build_load (scratch) "" b

  and cg_gather t mr =
    let elem_type     = type_of_val_type (element_val_type t) in
    let base_ptr      = build_pointercast (ptr_to_buffer mr.buf) (pointer_type elem_type) "" b in
    let addr_vec      = cg_expr mr.idx in
    let get_idx i     = build_extractelement addr_vec (const_int int_imm_t i) "" b in
    let addr_of_idx i = build_gep base_ptr [| get_idx i |] "" b in
    let load_idx i    = build_load (addr_of_idx i) "" b in
    let rec insert_idx = function
      | -1 -> undef (type_of_val_type t)
      | i -> build_insertelement (insert_idx (i-1)) (load_idx i) (const_int int_imm_t i) "" b
    in insert_idx ((vector_elements t) - 1)

  and cg_scatter e mr =
    let elem_type     = type_of_val_type (element_val_type (val_type_of_expr e)) in
    let base_ptr      = build_pointercast (ptr_to_buffer mr.buf) (pointer_type elem_type) "" b in
    let addr_vec      = cg_expr mr.idx in
    let value_vec     = cg_expr e in
    let get_idx i     = build_extractelement addr_vec (const_int int_imm_t i) "" b in
    let addr_of_idx i = build_gep base_ptr [| get_idx i |] "" b in
    let get_elem i    = build_extractelement value_vec (const_int int_imm_t i) "" b in
    let store_idx i   = build_store (get_elem i) (addr_of_idx i) b in
    List.hd (List.map store_idx (0 -- vector_elements (val_type_of_expr e)))

  and cg_memref mr vt =
    (* load the global buffer** *)
    let base = ptr_to_buffer mr.buf in
    (* cast pointer to pointer-to-target-type *)
    (* TODO: fix address calculation to use inner type of vector type *)
    let elem_type = type_of_val_type (element_val_type vt) in
    let ptr = build_pointercast base (pointer_type (elem_type)) "memref_elem_ptr" b in
    (* build getelementpointer into buffer *)
    let gep = build_gep ptr [| cg_expr mr.idx |] "memref" b in
    build_pointercast gep (pointer_type (type_of_val_type vt)) "typed_memref" b


  and cg_storevector = function
    | ([], _, _, _) -> const_int int_imm_t 0
    | (first::rest, t, mr, stride) ->
        ignore(build_store first (cg_memref mr t) b);
        let nextaddr = {buf=mr.buf; idx= mr.idx +~ IntImm(stride)} in
        cg_storevector(rest, t, nextaddr, stride);
  in

    (* actually generate from the root statement *)
    ignore (cg_stmt s);

    (* return void from main *)
    ignore (build_ret_void b);

    if dbgprint then dump_module m;

    ignore (verify_cg m);

    (* return generated module and function *)
    (m,entrypoint_fn)
      
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

let codegen_to_ocaml_callable e =
  (* construct basic LLVM state *)
  let c = create_context () in

  (* codegen *)
  let (m,f) = codegen c e in

  (* codegen the wrapper *)
  let w = codegen_caml_wrapper c m f in

    (m,w)

(* C runner wrapper *)
let codegen_c_wrapper c m f =

  let buffer_t = buffer_t c in
  let i8_t = i8_type c in
  let i32_t = i32_type c in
  (* let i64_t = i64_type c in *)

  (* define wrapper entrypoint: void name(void* args[]) *)
  let wrapper = define_function
                  (c_entrypoint_name)
                  (function_type (void_type c) [|pointer_type buffer_t|])
                  m in

  let b = builder_at_end c (entry_block wrapper) in

  (* codegen load and cast from arg array slot i to lltype t *)
  let cg_load_arg i t =
    (* fetch object pointer = (( void* )args)[i] *)
    let args_array = param wrapper 0 in
    let arg_ptr = build_gep args_array [| const_int i32_t i |] "" b in
    (* deref arg pointer *)
    build_load (build_pointercast arg_ptr (pointer_type t) "" b) "" b
  in

  (* build inner function argument list from args array *)
  let args = Array.mapi 
               (fun i p -> cg_load_arg i (type_of p))
               (params f) in

    (* codegen the call *)
    ignore (build_call f args "" b);

    (* return *)
    ignore (build_ret_void b);

    if dbgprint then dump_module m;

    ignore (verify_cg m);

    (* return the wrapper function *)
    wrapper

let codegen_to_c_callable c e =
  (* codegen *)
  (*let (args,s) = e in*)
  let (m,f) = codegen c e in

  (* codegen the wrapper *)
  let w = codegen_c_wrapper c m f in

    (m,w)

exception BCWriteFailed of string

let codegen_to_file filename e =
  (* construct basic LLVM state *)
  let c = create_context () in

  (* codegen *)
  let (m,_) = codegen_to_c_callable c e in

    (* write to bitcode file *)
    match Llvm_bitwriter.write_bitcode_file m filename with
      | false -> raise(BCWriteFailed(filename))
      | true -> ();

    (* free memory *)
    dispose_module m
