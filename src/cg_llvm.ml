open Ir
open Llvm
open List
open Util
open Ir_printer
open Architecture

let dbgprint = false

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

let codegen (c:llcontext) (e:entrypoint) (arch:architecture) =

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

  (* The symbol table for loop variables *)
  let sym_table =
    Hashtbl.create 10 
  in

  let sym_add name llv =
    set_value_name name llv;
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
  and name_of_arg = function
    | Scalar (n, _) -> n
    | Buffer n -> n 
  in

  let argtypes = List.map type_of_arg arglist in
  
  (* define `void main(arg1, arg2, ...)` entrypoint*)
  let entrypoint_fn =
    define_function
      entrypoint_name
      (function_type (void_type c) (Array.of_list argtypes))
      m
  in


  (* Put args in the sym table *)
  Array.iteri
    (fun idx arg -> 
      let name = name_of_arg arg in
      let llval = param entrypoint_fn idx in
      sym_add name llval;
      (* And mark each buffer arg as Noalias *)
      match arg with 
        | Buffer b -> add_param_attr llval Attribute.Noalias
        | _ -> ()
    ) (Array.of_list arglist);


  (* start codegen at entry block of main *)
  let b = builder_at_end c (entry_block entrypoint_fn) in
  
  let alloca_bb = append_block c ("alloca") (block_parent (insertion_block b)) in
  ignore(build_br alloca_bb b);
  let after_alloca_bb = append_block c ("after_alloca") (block_parent (insertion_block b)) in
  position_at_end alloca_bb b;  
  let alloca_end = build_br after_alloca_bb b in
  position_at_end after_alloca_bb b;  

  let rec cg_expr e = arch.cg_expr c m b cg_expr_inner e
  and cg_expr_inner = function
    (* constants *)
    | IntImm i 
    | UIntImm i  -> const_int   (int_imm_t)   i
    | FloatImm f -> const_float (float_imm_t) f

    (* cast *)
    | Cast (t, e) -> cg_cast t e

    (* TODO: coding style: use more whitespace, fewer parens in matches? *)

    (* Binary operators are generated from builders for int, uint, float types *)
    (* Arithmetic and comparison on vector types use the same build calls as 
     * the scalar versions *)

    (* arithmetic *)

    (* llvm doesn't seem very good about converting common vector muls to vector shifts *)
        
    | Bop(Mul, Broadcast(IntImm 2, n), v) 
    | Bop(Mul, v, Broadcast(IntImm 2, n))
    | Bop(Mul, Broadcast(UIntImm 2, n), v) 
    | Bop(Mul, v, Broadcast(UIntImm 2, n)) -> 
        build_shl (cg_expr v) (cg_expr (Broadcast (IntImm 1, n))) "" b

    | Bop(Mul, Broadcast(Cast (Int x, IntImm 2), n), v)
    | Bop(Mul, v, Broadcast(Cast (Int x, IntImm 2), n))
    | Bop(Mul, Broadcast(Cast (Int x, UIntImm 2), n), v) 
    | Bop(Mul, v, Broadcast(Cast (Int x, UIntImm 2), n)) -> 
        build_shl (cg_expr v) (cg_expr (Broadcast (Cast (Int x, IntImm 1), n))) "" b

    | Bop(Mul, Broadcast(Cast (UInt x, IntImm 2), n), v)
    | Bop(Mul, v, Broadcast(Cast (UInt x, IntImm 2), n))
    | Bop(Mul, Broadcast(Cast (UInt x, UIntImm 2), n), v)
    | Bop(Mul, v, Broadcast(Cast (UInt x, UIntImm 2), n)) -> 
        build_shl (cg_expr v) (cg_expr (Broadcast (Cast (UInt x, UIntImm 1), n))) "" b

    | Bop(Div, v, Broadcast(IntImm 2, n)) ->
        build_ashr (cg_expr v) (cg_expr (Broadcast (IntImm 1, n))) "" b
    | Bop(Div, v, Broadcast(UIntImm 2, n)) -> 
        build_lshr (cg_expr v) (cg_expr (Broadcast (IntImm 1, n))) "" b

    | Bop(Div, v, Broadcast(Cast (Int x, IntImm 2), n)) 
    | Bop(Div, v, Broadcast(Cast (Int x, UIntImm 2), n)) -> 
        build_ashr (cg_expr v) (cg_expr (Broadcast (Cast (Int x, IntImm 1), n))) "" b

    | Bop(Div, v, Broadcast(Cast (UInt x, IntImm 2), n))
    | Bop(Div, v, Broadcast(Cast (UInt x, UIntImm 2), n)) -> 
        build_lshr (cg_expr v) (cg_expr (Broadcast (Cast (UInt x, UIntImm 1), n))) "" b

    | Bop(Div, v, Broadcast(IntImm 4, n)) ->
        build_ashr (cg_expr v) (cg_expr (Broadcast (IntImm 2, n))) "" b
    | Bop(Div, v, Broadcast(UIntImm 4, n)) -> 
        build_lshr (cg_expr v) (cg_expr (Broadcast (IntImm 2, n))) "" b

    | Bop(Div, v, Broadcast(Cast (Int x, IntImm 4), n)) 
    | Bop(Div, v, Broadcast(Cast (Int x, UIntImm 4), n)) -> 
        build_ashr (cg_expr v) (cg_expr (Broadcast (Cast (Int x, IntImm 2), n))) "" b

    | Bop(Div, v, Broadcast(Cast (UInt x, IntImm 4), n))
    | Bop(Div, v, Broadcast(Cast (UInt x, UIntImm 4), n)) -> 
        build_lshr (cg_expr v) (cg_expr (Broadcast (Cast (UInt x, UIntImm 2), n))) "" b
        

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

    | Load(t, buf, idx) -> cg_load t buf idx

    (* Loop variables *)
    | Var(vt, name) -> sym_get name

    (* Let expressions *)
    | Let(name, l, r) -> 
      sym_add name (cg_expr l);
      let result = cg_expr r in
      sym_remove name;
      result        

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

    | Ramp (base_expr, stride_expr, n) ->
      let elem_type = val_type_of_expr base_expr in
      let vec_type  = vector_of_val_type elem_type n in
      let base      = cg_expr base_expr in
      let stride    = cg_expr stride_expr in
      let rec rep = function
        (* build the empty vector and the first value to insert *)
        | 0 -> (undef (type_of_val_type vec_type)), base
        (* insert each value, and add to pass up the recursive chain *)
        | i -> let vec, value = rep (i-1) in               
               (build_insertelement vec value (const_int int_imm_t (i-1)) "" b,
                build_add value stride "" b)
      in fst (rep n)

    (* Unpacking vectors *)
    | ExtractElement(e, n) ->
        let v = cg_expr e in
        let idx = cg_expr (Cast(u32, n)) in
        build_extractelement v idx "" b

    | Debug(e, prefix, args) -> cg_debug e prefix args

    | e ->
      Printf.printf "Unimplemented: %s\n%!" (string_of_expr e);
      raise UnimplementedInstruction

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
          simple_cast build_trunc e t

      | UInt(fb), Int(tb)
      | UInt(fb), UInt(tb) when fb < tb ->
          simple_cast build_zext e t

      (* Some common casts can be done more efficiently by bitcasting
         and doing vector shuffles (assuming little-endianness) *)

      (* Narrowing ints by a factor of 2 *)
      | UIntVector(fb, fw), UIntVector(tb, tw)
      | IntVector(fb, fw), IntVector(tb, tw)
      | UIntVector(fb, fw), IntVector(tb, tw)
      | IntVector(fb, fw), UIntVector(tb, tw) when fw = tw && fb = tb*2 ->
          (* Bitcast to split hi and lo halves of each int *)
          let intermediate_type = type_of_val_type (IntVector (tb, tw*2)) in
          let split_hi_lo = build_bitcast (cg_expr e) intermediate_type "" b in
          let indices = const_vector (Array.of_list (List.map (fun x -> const_int int_imm_t (x*2)) (0 -- tw))) in
          (* Shuffle vector to grab the low halves *)
          build_shufflevector split_hi_lo (undef intermediate_type) indices "" b

      (* Widening unsigned ints by a factor of 2 *)
      | UIntVector(fb, fw), UIntVector(tb, tw)
      | UIntVector(fb, fw), IntVector(tb, tw) ->
          (* Make a zero vector of the same size *)
          let zero_vector = const_null (type_of_val_type (IntVector (fb, fw))) in
          let rec indices = function
            | 0 -> []
            | x -> (const_int int_imm_t (fw-x))::(const_int int_imm_t (2*fw-x))::(indices (x-1)) in                
          let shuffle = build_shufflevector (cg_expr e) zero_vector (const_vector (Array.of_list (indices fw))) "" b in
          build_bitcast shuffle (type_of_val_type t) "" b
          
      (* For signed ints we need to do sign extension 
      | IntVector(fb, fw), UIntVector(tb, tw) 
      | IntVector(fb, fw), UIntVector(tb, tw) when fw = tw && fb*2 = tb ->             *)          

      (* Narrowing integer casts always truncate *)
      | UIntVector(fb, fw), UIntVector(tb, tw)
      | IntVector(fb, fw), IntVector(tb, tw)
      | UIntVector(fb, fw), IntVector(tb, tw)
      | IntVector(fb, fw), UIntVector(tb, tw) when fw = tw && fb > tb ->
          simple_cast build_trunc e t            
      | UInt(fb), UInt(tb)
      | UInt(fb), Int(tb)
      | Int(fb), UInt(tb)
      | Int(fb), Int(tb) when fb > tb ->
          simple_cast build_trunc e t

      (* Widening integer casts from signed types sign extend then bitcast (like C does) *)
      | IntVector(fb, fw), IntVector(tb, tw)
      | IntVector(fb, fw), UIntVector(tb, tw) when fw = tw && fb < tb ->
          simple_cast build_sext e t
      | Int(fb), Int(tb)
      | Int(fb), UInt(tb) when fb < tb ->
          simple_cast build_sext e t
            
      (* Widening integer casts from unsigned types zero extend then bitcast *)
      | UIntVector(fb, fw), IntVector(tb, tw)
      | UIntVector(fb, fw), UIntVector(tb, tw) when fw = tw && fb < tb ->
          simple_cast build_zext e t
      | UInt(fb), Int(tb)
      | UInt(fb), UInt(tb) when fb < tb ->
          simple_cast build_zext e t

      (* If the bit counts match, just bitcast, but llvm treats ints
         and uints the same, so do nothing. *)
      | UIntVector(fb, fw), IntVector(tb, tw)
      | IntVector(fb, fw),  UIntVector(tb, tw) 
      | UIntVector(fb, fw), UIntVector(tb, tw) 
      | IntVector(fb, fw),  IntVector(tb, tw) when fw = tw && fb = tb -> 
          cg_expr e 
      | UInt(fb), Int(tb)
      | Int(fb),  UInt(tb) 
      | UInt(fb), UInt(tb) 
      | Int(fb),  Int(tb) when fb = tb -> 
          cg_expr e 
            
      (* int <--> float *)
      | IntVector _, FloatVector _
      | Int(_),   Float(_) -> simple_cast build_sitofp e t
      | UIntVector _, FloatVector _
      | UInt(_),  Float(_) -> simple_cast build_uitofp e t
      | FloatVector _, IntVector _
      | Float(_), Int(_)   -> simple_cast build_fptosi e t
      | FloatVector _, UIntVector _
      | Float(_), UInt(_)  -> simple_cast build_fptoui e t

      (* float widening or narrowing *)
      | Float(fb), Float(tb) -> simple_cast build_fpcast  e t

      (* TODO: remaining casts *)
      | f,t ->
        Printf.printf "Unimplemented cast: %s -> %s (of %s)\n%!" (string_of_val_type f) (string_of_val_type t) (string_of_expr e);
        raise UnimplementedInstruction

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

  and cg_stmt stmt = arch.cg_stmt c m b cg_stmt_inner stmt
  and cg_stmt_inner = function
    (* TODO: unaligned vector store *)
    | Store(e, buf, idx) -> cg_store e buf idx
    | For(name, min, n, _, stmt) ->
      cg_for name (cg_expr min) (cg_expr (min +~ n)) stmt
    | Block (first::second::rest) ->
      ignore(cg_stmt first);
      cg_stmt (Block (second::rest))
    | Block(first::[]) ->
      cg_stmt first
    | Block _ -> raise (Wtf "cg_stmt of empty block")
    (* | _ -> raise UnimplementedInstruction *)
    | Pipeline (name, ty, size, produce, consume) ->

        (* Force the alignment to be a multiple of 128 bits. This is
           platform specific, but we're planning to remove alloca at some
           stage anyway *)
        let width_multiplier = 128 / (bit_width ty) in
        let upgraded_type = 
          if width_multiplier > 1 then
            vector_of_val_type ty width_multiplier 
          else
            ty
        in

        let upgraded_size = 
          if width_multiplier > 1 then
            (size /~ (IntImm width_multiplier)) +~ (IntImm 1)
          else
            size
        in

        let current = insertion_block b in
        position_before alloca_end b;
        let scratch = build_array_alloca (type_of_val_type upgraded_type) (cg_expr upgraded_size) "" b in
        position_at_end current b;

        (* push the symbol environment *)
        sym_add name scratch;

        ignore (cg_stmt produce);
        let res = cg_stmt consume in

        (* pop the symbol environment *)
        sym_remove name;

        res

  and cg_store e buf idx =
    (* ignore(cg_debug e ("Store to " ^ buf ^ " ") [idx; e]); *)
    match (is_vector e, is_vector idx) with
      | (_, true) ->
        begin match idx with
          (* Aligned dense vector store: ramp stride 1 && idx base % vec width = 0 *)
          | Ramp(b, IntImm(1), n) when Analysis.reduce_expr_modulo b n = Some 0 ->            
            cg_aligned_store e buf b
          (* All other vector stores become scatters (even if dense) *)
          | _ -> cg_scatter e buf idx
        end

      | (false, false) -> build_store (cg_expr e) (cg_memref (val_type_of_expr e) buf idx) b

      | (true, false)  -> raise (Wtf "Can't store a vector to a scalar address")

  and cg_aligned_store e buf idx =
    let w = vector_elements (val_type_of_expr idx) in
    (* Handle aligned dense vector store of scalar by broadcasting first *)
    let expr = if is_scalar e then Broadcast(e, w) else e in
    build_store (cg_expr expr) (cg_memref (val_type_of_expr expr) buf idx) b

  and cg_scatter e buf idx =
    let elem_type     = type_of_val_type (element_val_type (val_type_of_expr e)) in
    let base_ptr      = build_pointercast (sym_get buf) (pointer_type elem_type) "" b in
    let addr_vec      = cg_expr idx in
    let value         = cg_expr e in
    let get_idx i     = build_extractelement addr_vec (const_int int_imm_t i) "" b in
    let addr_of_idx i = build_gep base_ptr [| get_idx i |] "" b in
    let get_elem i    = build_extractelement value (const_int int_imm_t i) "" b in
    let store_idx i   = build_store (if is_vector e then (get_elem i) else value) (addr_of_idx i) b in
    List.hd (List.map store_idx (0 -- vector_elements (val_type_of_expr e)))


  and cg_load t buf idx =
    (* ignore(cg_debug idx ("Load " ^ (string_of_val_type t) ^ " from " ^ buf ^ " ") [idx]); *)
    match (vector_elements t, is_vector idx) with 
        (* scalar load *)
      | (1, false) -> build_load (cg_memref t buf idx) "" b 
        
        (* vector load of scalar address *)
      | (_, false) ->
        Printf.printf "scalar expr %s loaded as vector type %s\n%!" (string_of_expr idx) (string_of_val_type t);
        raise (Wtf "Vector load from a scalar address")

      | (w, true) ->
        begin match idx with 
          (* dense vector load *)
          | Ramp(b, IntImm(1), _) ->
            begin match Analysis.reduce_expr_modulo b w with
              | Some 0      -> cg_aligned_load t buf b
              | Some offset -> cg_unaligned_load t buf b offset
              | None        -> cg_unknown_alignment_load t buf b
            end
          (* TODO: consider strided loads *)
          (* gather *)
          | _ -> cg_gather t buf idx
        end          
          
  and cg_aligned_load t buf idx =
    build_load (cg_memref t buf idx) "" b

  and cg_unaligned_load t buf idx offset =
    let vec_width = vector_elements t in
    let lower_addr = idx -~ (IntImm(offset)) in
    let upper_addr = lower_addr +~ (IntImm(vec_width)) in
    let lower = Load(t, buf, Ramp(lower_addr, IntImm 1, vec_width)) in
    let upper = Load(t, buf, Ramp(upper_addr, IntImm 1, vec_width)) in
    let lower_indices = offset -- (vector_elements t) in
    let upper_indices = 0 -- offset in
    let extract_lower = map (fun x -> ExtractElement(lower, (UIntImm(x)))) lower_indices in
    let extract_upper = map (fun x -> ExtractElement(upper, (UIntImm(x)))) upper_indices in
    let vec = MakeVector (extract_lower @ extract_upper) in
    cg_expr vec

  and cg_unknown_alignment_load t buf idx =
    let current = insertion_block b in
    position_before alloca_end b;
    let scratch = build_alloca (type_of_val_type t) "" b in      
    position_at_end current b;
    let memcpy     = declare_function "llvm.memcpy.p0i8.p0i8.i32"
      (function_type (void_type c) [|buffer_t; buffer_t; int32_imm_t; int32_imm_t; i1_type c|]) m in
    let stack_addr = build_pointercast scratch buffer_t "" b in
    let mem_addr   = build_pointercast (cg_memref t buf idx) buffer_t "" b in
    let length     = const_int int_imm_t ((bit_width t)/8) in
    let alignment  = const_int int_imm_t ((bit_width (element_val_type t))/8) in
    let volatile   = const_int (i1_type c) 0 in
    ignore(build_call memcpy [|stack_addr; mem_addr; length; alignment; volatile|] "" b);
    build_load (scratch) "" b 

  and cg_gather t buf idx =
    let elem_type     = type_of_val_type (element_val_type t) in
    let base_ptr      = build_pointercast (sym_get buf) (pointer_type elem_type) "" b in
    let addr_vec      = cg_expr idx in
    let get_idx i     = build_extractelement addr_vec (const_int int_imm_t i) "" b in
    let addr_of_idx i = build_gep base_ptr [| get_idx i |] "" b in
    let load_idx i    = build_load (addr_of_idx i) "" b in
    let rec insert_idx = function
      | -1 -> undef (type_of_val_type t)
      | i -> build_insertelement (insert_idx (i-1)) (load_idx i) (const_int int_imm_t i) "" b
    in insert_idx ((vector_elements t) - 1)

  and cg_memref vt buf idx =
    (* load the global buffer** *)
    let base = sym_get buf in
    (* cast pointer to pointer-to-target-type *)
    let elem_type = type_of_val_type (element_val_type vt) in
    let ptr = build_pointercast base (pointer_type (elem_type)) "memref_elem_ptr" b in
    (* build getelementpointer into buffer *)
    let gep = build_gep ptr [| cg_expr idx |] "memref" b in
    build_pointercast gep (pointer_type (type_of_val_type vt)) "typed_memref" b
      
  and cg_debug e prefix args =
    let ll_e = cg_expr e in

    (* Generate a format string and values to print for a printf *)
    let rec fmt_string x = match val_type_of_expr x with
      | Int _ -> ("%d", [Cast (i32, x)])
      | Float _ -> ("%3.3f", [Cast (f32, x)])
      | UInt _ -> ("%u", [Cast (u32, x)])
      | IntVector (_, n) | UIntVector (_, n) | FloatVector (_, n) -> 
          let elements = List.map (fun idx -> ExtractElement (x, IntImm idx)) (0 -- n) in
          let subformats = List.map fmt_string elements in
          ("[" ^ (String.concat ", " (List.map fst subformats)) ^ "]",
           List.concat (List.map snd subformats))
    in
    
    let fmts = List.map fmt_string args in
    let fmt = String.concat ", " (List.map fst fmts) in
    let args = List.concat (List.map snd fmts) in

    let ll_fmt = const_stringz c (prefix ^ fmt ^ "\n") in    
    let ll_args = List.map cg_expr args in
    
    let global_fmt = define_global "debug_fmt" ll_fmt m in
    let global_fmt = build_pointercast global_fmt (pointer_type (i8_type c)) "" b in

    (*Printf.printf "cg_debug: %s %s %s\n" (string_of_expr e) (prefix^fmt) (String.concat ", " (List.map string_of_expr args));

    Printf.printf "%s\n%!" (string_of_lltype (type_of global_fmt)); *)

    let ll_printf = declare_function "printf" 
      (var_arg_function_type (i32_type c) [|pointer_type (i8_type c)|]) m in
    ignore(build_call ll_printf (Array.of_list (global_fmt::ll_args)) "" b);

    ll_e
      
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
  let (m,f) = codegen c e (Architecture.host) in

  (* codegen the wrapper *)
  let w = codegen_caml_wrapper c m f in

    (m,w)

(* C runner wrapper *)
let codegen_c_wrapper c m f =

  let buffer_t = buffer_t c in
  let i32_t = i32_type c in

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
  let (m,f) = codegen c e Architecture.host in

  (* codegen the wrapper *)
  let w = codegen_c_wrapper c m f in

    (m,w)

exception BCWriteFailed of string

let codegen_to_file filename e =
  (* construct basic LLVM state *)
  let c = create_context () in

  (* codegen *)
  let (m,_) = codegen_to_c_callable c e in

  (* Set the target triple and target data for our dev machines *)
  set_target_triple "x86_64-apple-darwin11.1.0" m;
  set_data_layout "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64" m;

  (* write to bitcode file *)
  begin match Llvm_bitwriter.write_bitcode_file m filename with
    | false -> raise(BCWriteFailed(filename))
    | true -> ()
  end;
        
  (* free memory *)
  dispose_module m
