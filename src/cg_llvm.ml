open Ir
open Llvm
open List
open Util
open Ir_printer
open Cg_util
open Cg_llvm_util
open Analysis

exception MissingEntrypoint
exception UnimplementedInstruction
exception UnalignedVectorMemref
exception ArgExprOfBufferArgument (* The Arg expr can't dereference a Buffer, only Scalars *)
exception ArgTypeMismatch of val_type * val_type

type cg_entry = string list -> llcontext -> llmodule -> entrypoint -> llvalue 
type 'a make_cg_context = llcontext -> llmodule -> llbuilder ->
                          (string, llvalue) Hashtbl.t -> 'a -> string list -> 'a cg_context
type cg_expr = expr -> llvalue
type cg_stmt = stmt -> llvalue

(* Codegen relies on an architecture module that tells us how to do
   architecture specific things. The way we implement this is by
   constructing a Codegen module parameterized on an architecture, then
   dispatching to the appropriate codegen module. This happens in
   cg_for_target.ml *)
module type Architecture = sig
  type state
  type context = state cg_context

  val target_triple : string

  val start_state : unit -> state

  val cg_entry : llcontext -> llmodule -> cg_entry -> state make_cg_context -> entrypoint -> string list -> llvalue
  val cg_expr : context -> expr -> llvalue
  val cg_stmt : context -> stmt -> llvalue
  val malloc  : context -> string -> expr -> expr -> (llvalue * (context -> unit))
  val env : environment
  val pointer_size : int
end


module type Codegen = sig
  type arch_state
  type context = arch_state cg_context

  val make_cg_context : arch_state make_cg_context
  val codegen_entry : string list -> entrypoint -> llcontext * llmodule * llvalue
  val codegen_c_wrapper : string list -> llcontext -> llmodule -> llvalue -> llvalue
  val codegen_to_bitcode_and_header : string list -> entrypoint -> unit
end


module CodegenForArch ( Arch : Architecture ) = struct

type arch_state = Arch.state
type context = arch_state cg_context

module ArgMap = Map.Make(String)
type argmap = (arg*int) ArgMap.t (* track args as name -> (Ir.arg,index) *) 

let rec make_cg_context c m b sym_table arch_state arch_opts =

  let int_imm_t = i32_type c in
  let int32_imm_t = i32_type c in
  let float_imm_t = float_type c in
  let buffer_t = raw_buffer_t c in

  let type_of_val_type = type_of_val_type c in

  let sym_add name llv =
    set_value_name name llv;
    Hashtbl.add sym_table name llv
  and sym_remove name =
    Hashtbl.remove sym_table name
  and sym_get name =
    try Hashtbl.find sym_table name
    with Not_found -> failwith ("symbol " ^ name ^ " not found")
  and dump_syms () = dump_syms sym_table
  in

  let rec cg_context = {
    c = c; m = m; b = b;
    cg_expr = cg_expr_inner;
    cg_stmt = cg_stmt_inner;
    cg_memref = cg_memref;
    sym_get = sym_get;
    sym_add = sym_add;
    sym_remove = sym_remove;
    dump_syms = dump_syms;
    arch_state = arch_state;
    arch_opts = arch_opts;
  }
  and cg_expr e = 
    dbg 2 "begin cg_expr %s\n%!" (string_of_expr e);
    let result = Arch.cg_expr cg_context e in
    dbg 2 "end cg_expr %s -> %s\n%!" (string_of_expr e) (string_of_lltype (type_of result));
    result
  and cg_expr_inner = function
    (* constants *)
    | IntImm i   -> const_int   (int_imm_t)   i
    | FloatImm f -> const_float (float_imm_t) f

    (* cast *)
    | Cast (t, e) -> cg_cast t e

    (* TODO: coding style: use more whitespace, fewer parens in matches? *)

    (* Binary operators are generated from builders for int, uint, float types *)
    (* Arithmetic and comparison on vector types use the same build calls as 
     * the scalar versions *)

    (* arithmetic *)

    | Bop (Add, l, r) -> cg_binop build_add  build_add  build_fadd l r
    | Bop (Sub, l, r) -> cg_binop build_sub  build_sub  build_fsub l r
    | Bop (Mul, l, r) -> cg_binop build_mul  build_mul  build_fmul l r
    | Bop (Div, l, r) -> cg_binop build_sdiv build_udiv build_fdiv l r
    (* In most cases architecture-specific code should pick up min and max and generate something better *)
    | Bop (Min, l, r) -> cg_expr (Select (l <~ r, l, r))
    | Bop (Max, l, r) -> cg_expr (Select (l <~ r, r, l))
    | Bop (Mod, l, r) -> cg_mod l r

    (* comparison *)
    | Cmp (EQ, l, r) -> cg_cmp Icmp.Eq  Icmp.Eq  Fcmp.Oeq l r
    | Cmp (NE, l, r) -> cg_cmp Icmp.Ne  Icmp.Ne  Fcmp.One l r
    | Cmp (LT, l, r) -> cg_cmp Icmp.Slt Icmp.Ult Fcmp.Olt l r
    | Cmp (LE, l, r) -> cg_cmp Icmp.Sle Icmp.Ule Fcmp.Ole l r
    | Cmp (GT, l, r) -> cg_cmp Icmp.Sgt Icmp.Ugt Fcmp.Ogt l r
    | Cmp (GE, l, r) -> cg_cmp Icmp.Sge Icmp.Uge Fcmp.Oge l r

    (* logical *)
    | And (l, r) -> build_and (cg_expr l) (cg_expr r) "" b
    | Or (l, r) -> build_or (cg_expr l) (cg_expr r) "" b
    | Not l -> build_not (cg_expr l) "" b

    (* Select *)
    | Select (c, t, f) ->
        build_select (cg_expr c) (cg_expr t) (cg_expr f) "" b 

    | Load (t, buf, idx) -> cg_load t buf idx

    (* Variables *)
    | Var (vt, name) -> sym_get name

    (* Extern calls *)
    | Call (Extern, t, name, args) ->
        (* If we're making a vectorized call to an extern scalar
           function, we need to do some extra work here *)
        (* First, codegen the args *)
        let llargs = Array.of_list (List.map cg_expr args) in      
        let elts = vector_elements t in
        (* Compure the scalar and vector names for this function *)
        let scalar_name = base_name name in
        let vector_name = if elts > 1 then (scalar_name ^ "x" ^ (string_of_int elts)) else scalar_name in
        (* Look up the scalar and vector forms *)
        let scalar_fn = lookup_function scalar_name m in
        let vector_fn = lookup_function vector_name m in
        (* If the scalar version doesn't exist declare it as extern *)
        let scalar_fn = 
          match scalar_fn with
            | None -> 
                dbg 2 "Did not find %s in initial module. Assuming it's extern.\n%!" scalar_name;
                let arg_types = List.map (fun arg -> type_of_val_type (element_val_type (val_type_of_expr arg))) args in
                declare_function scalar_name (function_type (type_of_val_type (element_val_type t)) (Array.of_list arg_types)) m
            | Some fn -> 
              dbg 2 "Found %s in initial module\n%!" scalar_name;
              fn
        in

        if elts = 1 then begin
          (* Scalar call *)
          build_call scalar_fn llargs ("extern_" ^ scalar_name) b
        end else begin
          (* Vector call *)
          match vector_fn with 
            | None ->         
                dbg 2 "Did not find %s in initial module. Calling scalar version.\n%!" vector_name;
                (* Couldn't find a vector version. Scalarize. *)
                let make_call i = 
                  let extract_elt a =
                    let idx = const_int int_imm_t i in
                    build_extractelement a idx "" b
                  in
                  let args = Array.map extract_elt llargs in
                  build_call scalar_fn args ("extern_" ^ scalar_name) b 
                in
                let rec assemble_result = function
                  | ([], _) -> undef (type_of_val_type t)
                  | (first::rest, n) -> 
                    build_insertelement 
                      (assemble_result (rest, n+1)) 
                      first 
                      (const_int int32_imm_t n) "" b
                in
                let calls = List.map make_call (0 -- elts) in
                assemble_result (calls, 0)              
            | Some fn ->
                dbg 2 "Found %s in initial module\n%!" vector_name;
                dbg 2 "Type is: %s\n%!" (string_of_lltype (type_of fn));
                let arg_types = Array.map type_of llargs in
                dbg 2 "Type should be: %s\n%!" (string_of_lltype (function_type (type_of_val_type t) arg_types));
                dbg 2 "Passing args of type: ";
                Array.iter (fun a -> dbg 2 "%s " (string_of_lltype (type_of a))) llargs;
                dbg 2 "\n%!";           
                (* Found a vector version *)
                build_call fn llargs ("extern_" ^ vector_name) b              
        end
    | Call (_, _, name, _) ->
        failwith ("Can't lower call to " ^ name ^ ". This should have been replaced with a Load during lowering\n")

    (* Let expressions *)
    | Let (name, l, r) -> 
      sym_add name (cg_expr l);
      let result = cg_expr r in
      sym_remove name;
      result

    (* Making vectors *)
    | MakeVector (l) -> cg_makevector(l, val_type_of_expr (MakeVector l), 0)

    | Broadcast (e, n) -> 
        let elem_type = val_type_of_expr e in
        let vec_type  = vector_of_val_type elem_type n in
        let expr      = cg_expr e in
        let blank = undef (type_of_val_type vec_type) in
        let result = build_insertelement blank expr (const_int int_imm_t 0) "" b in
        let rec indices = function
          | 0 -> []
          | n -> (const_int int_imm_t 0)::(indices (n-1)) 
        in
        let indices = const_vector (Array.of_list (indices n)) in
        let result = build_shufflevector result blank indices "" b in          
        result

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
    | ExtractElement (e, n) ->
        let v = cg_expr e in
        let idx = cg_expr (Cast(u32, n)) in
        build_extractelement v idx "" b

    | Debug(e, prefix, args) -> cg_debug e prefix args

  and cg_makevector = function
    | ([], t, _) -> undef (type_of_val_type t)
    | (first::rest, t, n) -> 
        build_insertelement
          (cg_makevector (rest, t, n+1))
          (cg_expr first)
          (const_int int32_imm_t n)
          "" b

  and cg_binop iop uop fop l r =
    let build = match val_type_of_expr l with
      | Int _   | IntVector(_,_)   -> iop
      | UInt _  | UIntVector(_,_)  -> uop
      | Float _ | FloatVector(_,_) -> fop
    in build (cg_expr l) (cg_expr r) "" b

  and cg_mod l r =
    match val_type_of_expr l with
      | Float _ | FloatVector (_, _) -> build_frem (cg_expr l) (cg_expr r) "" b
      | UInt _ | UIntVector (_, _) -> build_urem (cg_expr l) (cg_expr r) "" b
      | _ -> 
          (* l % r is not necessarily positive, but ((l % r) + r) % r
             should be. *)
          let r = cg_expr r and l = cg_expr l in
          let initial_mod = build_srem l r "" b in
          let made_positive = build_add initial_mod r "" b in
          build_srem made_positive r "" b

  and cg_cmp iop uop fop l r =
    cg_binop (build_icmp iop) (build_icmp uop) (build_fcmp fop) l r

  and cg_cast t e =
    (* shorthand for the common case *)
    let simple_cast build e t = build (cg_expr e) (type_of_val_type t) "" b in

    match (val_type_of_expr e, t) with

      (* Trivial cast *)
      | a, b when a = b -> cg_expr e

      (* Scalar int casts *)
      | UInt(fb), Int(tb) when fb > tb ->
          simple_cast build_trunc e t

      | UInt(fb), Int(tb)
      | UInt(fb), UInt(tb) when fb < tb ->
          simple_cast build_zext e t

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
      | Float(fb), Float(tb) -> simple_cast build_fpcast e t
      | FloatVector(fb, fw), FloatVector(tb, tw) when fw = tw -> 
          simple_cast build_fpcast e t

      (* TODO: remaining casts *)
      | f,t ->
        Printf.printf
          "Unimplemented cast: %s -> %s (of %s)\n%!"
          (string_of_val_type f) (string_of_val_type t) (string_of_expr e);
        raise UnimplementedInstruction

  and cg_for var_name min size body = 
      (* Emit the start code first, without 'variable' in scope. *)
      let max = build_add min size "" b in

      (* let counter_t = i64_type c in
      let min = build_intcast min counter_t "" b in
      let max = build_intcast max counter_t "" b in *)

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
      let variable = build_phi [(min, preheader_bb)] var_name b in

      (* Within the loop, the variable is defined equal to the PHI node. *)
      sym_add var_name variable;

      (* Emit the body of the loop.  This, like any other expr, can change the
       * current BB.  Note that we ignore the value computed by the body, but
       * don't allow an error *)
      ignore (cg_stmt body);

      (* Emit the updated counter value. *)
      (* let next_var = build_add variable (const_int int_imm_t 1) (var_name ^ "_nextvar") b in  *)
      let next_var = build_add variable (const_int (type_of variable) 1) (var_name ^ "_nextvar") b in       

      (* Compute the end condition. *)
      let end_cond = build_icmp Icmp.Ne next_var max "" b in

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

  and cg_par_for var_name min size body =
    (* Dump everything relevant in the symbol table into a closure *)
    let syms = find_names_in_stmt (StringSet.add var_name StringSet.empty) Arch.pointer_size body in

    (* Compute an offset in bytes into a closure for each symbol name, and put
       that offset into a string map *)
    let (total_bytes, offset_map) = 
      StringIntSet.fold (fun (name, size) (offset, offset_map) ->
        (* round size up to the nearest power of two for alignment *)
        let rec roundup x y = if (x >= y) then x else roundup (x*2) y in
        let size = roundup 1 size in
        (* bump offset up to be a multiple of size for alignment *)
        let offset = ((offset+size-1)/size)*size in
        (offset + size, StringMap.add name offset offset_map)
      ) syms (0, StringMap.empty) 
    in

    (* Allocate a closure *)
    let (closure, cleanup_closure) =
      Arch.malloc cg_context (var_name^"_closure")
        (IntImm total_bytes) (IntImm 1) in

    (* Store everything in the closure *)
    StringIntSet.iter (fun (name, size) ->      
      let current_val = sym_get name in
      let offset = StringMap.find name offset_map in
      dbg 2 "Storing %s of size %d at offset %d\n%!" name size offset;
      let addr = build_bitcast closure (pointer_type (type_of current_val)) "" b in      
      let addr = build_gep addr [| const_int int_imm_t (offset/size) |] "" b in
      ignore (build_store current_val addr b);
    ) syms;

    (* Make a function that represents the body *)
    let body_fn_type = (function_type (void_type c) [|int_imm_t; buffer_t|]) in
    let body_fn = define_function (var_name ^ ".body") body_fn_type m in

    (* make a new context *)
    let sub_builder = (builder_at_end c (entry_block body_fn)) in
    let sub_sym_table = Hashtbl.create 10 in
    let sub_context = make_cg_context c m sub_builder sub_sym_table arch_state arch_opts in

    (* Load everything from the closure into the new symbol table *)
    StringIntSet.iter (fun (name, size) ->      
      let closure = param body_fn 1 in
      let current_val = sym_get name in
      let offset = StringMap.find name offset_map in
      let addr = build_bitcast closure (pointer_type (type_of current_val)) "" sub_builder in      
      let addr = build_gep addr [| const_int int_imm_t (offset/size) |] "" sub_builder in
      let llv = build_load addr name sub_builder in
      set_value_name name llv;
      Hashtbl.add sub_sym_table name llv
    ) syms;

    (* Make the var name refer to the first argument *)
    dbg 2 "%s = %s\n%!" var_name "param body_fn 0";
    set_value_name var_name (param body_fn 0);
    Hashtbl.add sub_sym_table var_name (param body_fn 0);

    (* Generate the function body *)
    ignore (sub_context.cg_stmt body);
    ignore (build_ret_void sub_builder);

    (* Call do_par_for back in the main function *)
    let do_par_for = declare_function "do_par_for"
      (function_type (void_type c) [|pointer_type body_fn_type; int_imm_t; int_imm_t; buffer_t|]) m in
    let closure = build_pointercast closure buffer_t "" b in
    ignore(build_call do_par_for [|body_fn; min; size; closure|] "" b);

    (* Free the closure *)
    cleanup_closure cg_context;

    (* Return an ignorable llvalue *)
    const_int int_imm_t 0

  and cg_stmt stmt = 
    dbg 2 "begin cg_stmt %s\n%!" (string_of_stmt stmt);
    let result = Arch.cg_stmt cg_context stmt in
    dbg 2 "end cg_stmt %s\n%!" (string_of_stmt stmt);
    result
  and cg_stmt_inner = function
    | Store (e, buf, idx) -> cg_store e buf idx
    | For (name, min, n, order, stmt) ->
        (if order then cg_for else cg_par_for) name (cg_expr min) (cg_expr n) stmt
    | Block (first::second::rest) ->
        ignore(cg_stmt first);
        cg_stmt (Block (second::rest))
    | Block (first::[]) ->
        cg_stmt first
    | Block _ -> failwith "cg_stmt of empty block"

    | LetStmt (name, value, stmt) ->
        sym_add name (cg_expr value);
        let result = cg_stmt stmt in
        sym_remove name;
        result

    | Pipeline (name, ty, size, produce, consume) ->

        (* allocate buffer *)
        let elem_size = (IntImm ((bit_width ty)/8)) in 
        let (scratch, cleanup_scratch) = Arch.malloc cg_context name size elem_size in

        (* push the symbol environment *)
        sym_add name scratch;

        ignore (cg_stmt produce);
        let res = cg_stmt consume in

        (* pop the symbol environment *)
        sym_remove name;

        (* free the scratch *)
        cleanup_scratch cg_context;

        res
    | Print (fmt, args) -> cg_print fmt args
    | Assert (e, str) -> cg_assert e str
    | s -> failwith (Printf.sprintf "Can't codegen: %s" (Ir_printer.string_of_stmt s))

  and cg_store e buf idx =
    (* ignore(cg_debug e ("Store to " ^ buf ^ " ") [idx; e]); *)
    match (is_vector e, is_vector idx) with
      | (true, true) ->
        begin match idx with
          (* Aligned dense vector store: ramp stride 1 && idx base % vec width = 0 *)
          | Ramp(b, IntImm(1), n) when Analysis.reduce_expr_modulo b n = Some 0 ->            
              cg_aligned_store e buf b
          (* Unaligned dense vector store *)
          | Ramp(b, IntImm(1), n) ->
              cg_unaligned_store e buf b
          (* All other vector stores become scatters (even if dense) *)
          | _ -> cg_scatter e buf idx
        end

      | (false, false) -> build_store (cg_expr e) (cg_memref (val_type_of_expr e) buf idx) b
      | (false, true)  -> failwith "Can't store a scalar to a vector address"
      | (true, false)  -> failwith "Can't store a vector to a scalar address"

  and cg_aligned_store e buf idx =
    build_store (cg_expr e) (cg_memref (val_type_of_expr e) buf idx) b

  and cg_unaligned_store e buf idx =
    (* Architectures that can handle this better should *)
    cg_scatter e buf (Ramp(idx, IntImm 1, vector_elements (val_type_of_expr e)))

  and cg_scatter e buf idx =
    let base          = sym_get buf in
    let addrspace     = address_space (type_of base) in
    let elem_type     = type_of_val_type (element_val_type (val_type_of_expr e)) in
    let base_ptr      = build_pointercast base (qualified_pointer_type elem_type addrspace) "" b in
    let addr_vec      = cg_expr idx in
    let value         = cg_expr e in
    let get_idx i     = build_extractelement addr_vec (const_int int_imm_t i) "" b in
    let addr_of_idx i = build_gep base_ptr [| get_idx i |] "" b in
    let get_elem i    = build_extractelement value (const_int int_imm_t i) "" b in
    let store_idx i   = build_store (get_elem i) (addr_of_idx i) b in
    List.hd (List.map store_idx (0 -- vector_elements (val_type_of_expr e)))

  and cg_load t buf idx =
    (* ignore(cg_debug idx ("Load " ^ (string_of_val_type t) ^ " from " ^ buf ^ " ") [idx]); *)
    match (vector_elements t, is_vector idx) with 
        (* scalar load *)
      | (1, false) -> build_load (cg_memref t buf idx) "" b 

        (* vector load of scalar address *)
      | (_, false) ->
        failwith (Printf.sprintf "scalar expr %s loaded as vector type %s\n%!"
                    (string_of_expr idx) (string_of_val_type t))

      | (w, true) ->
        begin match idx with 
          (* dense vector load *)
          | Ramp(b, IntImm(1), _) ->
              begin match Analysis.reduce_expr_modulo b w with
                | Some 0      -> cg_aligned_load t buf b
                | Some offset -> cg_unaligned_load t buf b offset
                | None        -> cg_unknown_alignment_load t buf b
              end
          (* Reverse dense vector load *)             
          | Ramp (base, IntImm(-1), _) ->
              (* Load the right elements *)
              let vec = cg_expr (Load (t, buf, Ramp (base -~ (IntImm (w-1)), IntImm 1, w))) in         
              (* Reverse them *)
              let mask = cg_expr (MakeVector (List.map (fun x -> IntImm (w-1-x)) (0--w))) in
              build_shufflevector vec (undef (type_of vec)) mask "" b

          (* Match against clamped load pattern and generate an
             if-then-else that does the dense load when within
             bounds *)

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
    
    let extract_lower = map (fun x -> ExtractElement(lower, (IntImm(x)))) lower_indices in
    let extract_upper = map (fun x -> ExtractElement(upper, (IntImm(x)))) upper_indices in
    let vec = MakeVector (extract_lower @ extract_upper) in
    cg_expr vec

  and cg_unknown_alignment_load t buf idx =
    (* Architectures that can handle this better should *)
    cg_gather t buf (Ramp(idx, IntImm 1, vector_elements t))

  and cg_gather t buf idx =
    let base          = sym_get buf in
    let addrspace     = address_space (type_of base) in
    let elem_type     = type_of_val_type (element_val_type t) in
    let base_ptr      = build_pointercast (base) (qualified_pointer_type elem_type addrspace) "" b in
    let addr_vec      = cg_expr idx in
    let get_idx i     = build_extractelement addr_vec (const_int int_imm_t i) "" b in
    let addr_of_idx i = build_gep base_ptr [| get_idx i |] "" b in
    let load_idx i    = build_load (addr_of_idx i) "" b in
    let rec insert_idx = function
      | -1 -> undef (type_of_val_type t)
      | i -> build_insertelement (insert_idx (i-1)) (load_idx i) (const_int int_imm_t i) "" b
    in 
    let result = insert_idx ((vector_elements t) - 1) in
    result

  and cg_memref vt buf idx =
    (* load the global buffer** *)
    let base = sym_get buf in
    (* capture address space, since we need to propagate it to the ultimate element memref ptrs *)
    let addrspace = address_space (type_of base) in
    (* cast pointer to pointer-to-target-type *)
    let elem_type = type_of_val_type (element_val_type vt) in
    let ptr = build_pointercast base (qualified_pointer_type (elem_type) addrspace) "memref_elem_ptr" b in
    (* build getelementpointer into buffer *)
    let gep = build_gep ptr [| cg_expr idx |] "memref" b in
    build_pointercast gep (qualified_pointer_type (type_of_val_type vt) addrspace) "typed_memref" b

  and cg_print prefix args =
    (* Generate a format string and values to print for a printf *)
    let rec fmt_string x = match val_type_of_expr x with
      | Int _ -> ("%d", [Cast (i32, x)])
      | Float _ -> ("%3.3f", [Cast (f64, x)])
      | UInt _ -> ("%u", [Cast (u32, x)])
      | IntVector (_, n) | UIntVector (_, n) | FloatVector (_, n) -> 
          let elements = List.map (fun idx -> ExtractElement (x, IntImm idx)) (0 -- n) in
          let subformats = List.map fmt_string elements in
          ("[" ^ (String.concat ", " (List.map fst subformats)) ^ "]",
           List.concat (List.map snd subformats))
    in

    let fmts = List.map fmt_string args in
    let fmt = String.concat " " (List.map fst fmts) in
    let args = List.concat (List.map snd fmts) in

    let ll_fmt = const_stringz c (prefix ^ fmt ^ "\n") in    
    let ll_args = List.map cg_expr args in

    let global_fmt = define_global "debug_fmt" ll_fmt m in
    set_linkage Llvm.Linkage.Internal global_fmt;
    let global_fmt = build_pointercast global_fmt (pointer_type (i8_type c)) "" b in

    (*Printf.printf "cg_debug: %s %s %s\n" (string_of_expr e) (prefix^fmt) (String.concat ", " (List.map string_of_expr args));

    Printf.printf "%s\n%!" (string_of_lltype (type_of global_fmt)); *)

    let ll_printf = declare_function "printf" 
      (var_arg_function_type (i32_type c) [|pointer_type (i8_type c)|]) m in
    build_call ll_printf (Array.of_list (global_fmt::ll_args)) "" b    

  and cg_assert e str =
    let e = build_intcast (cg_expr e) (i1_type c) "" b in

    (* Make a new basic block *)
    let the_function = block_parent (insertion_block b) in
    let assert_body_bb = append_block c "assert" the_function in    
    let after_bb = append_block c "after_assert" the_function in

    (* If the condition fails, enter the assert body, otherwise, enter the block after *)
    ignore (build_cond_br e after_bb assert_body_bb b);

    (* Construct the assert body *)
    position_at_end assert_body_bb b;

    let ll_msg = const_stringz c str in
    let msg = define_global "assert_message" ll_msg m in
    set_linkage Llvm.Linkage.Internal msg;
    let msg = build_pointercast msg (pointer_type (i8_type c)) "" b in
    let ll_halide_error = declare_function "halide_error" 
      (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
    ignore(build_call ll_halide_error [|msg|] "" b);

    (* Right now all asserts are preconditions in the preamble to the
       function, so there are no allocations to clean up *)
    ignore (build_ret_void b);

    (* Position the builder at the start of the after assert block *)
    position_at_end after_bb b;

    const_int int_imm_t 0

  and cg_debug e prefix args =
    let ll_e = cg_expr e in
    ignore(cg_print prefix args);
    ll_e
  in

  cg_context

let cg_entry arch_opts c m e =
  let (_, args, stmt) = e in

  (* make an entrypoint function *)
  let f = define_entry c m e in

  (* Put params in a new symbol table *)
  let param_syms = arg_symtab args (param_list f) in

  (* flag all buffer arguments as NoAlias *)
  make_buffer_params_noalias f;

  (* start codegen at entry block of main *)
  let b = builder_at_end c (entry_block f) in

  (* actually generate from the root statement *)
  let ctx = make_cg_context c m b param_syms (Arch.start_state ()) arch_opts in
  ignore (Arch.cg_stmt ctx stmt);

  (* return void from main *)
  ignore (build_ret_void b);

  (* check on our result *)
  if 0 > verbosity then dump_module m;
  ignore (verify_cg m);

  (* return generated function *)
  f


(*
 * Wrappers
 *)

(* C runner wrapper *)
let codegen_c_wrapper arch_opts c m f =

  let raw_buffer_t = raw_buffer_t c in
  let i32_t = i32_type c in

  (* define wrapper entrypoint: void name(void **args) *)
  let wrapper = define_function
                  ((value_name f) ^ "_c_wrapper")
                  (function_type (void_type c) [|pointer_type (raw_buffer_t)|])
                  m in

  let b = builder_at_end c (entry_block wrapper) in

  (* codegen load and cast from arg array slot i to lltype t *)
  let cg_load_arg i t =
    (* fetch arg pointer = args[i] *)
    let args_array = param wrapper 0 in
    let arg_ptr = build_load (build_gep args_array [| const_int i32_t i |] "" b) "" b in
    (* Cast this pointer to the appropriate type and load from it to get the ith argument *)
    match classify_type t with
      | TypeKind.Pointer ->
          let typename = match struct_name (element_type t) with Some nm -> nm | None -> "<unnamed>" in
          dbg 2 "Wrapping buffer arg type %s\n%!" typename;
          assert (typename = "struct.buffer_t");
          build_pointercast arg_ptr t "" b
      | _ ->
          dbg 2 "Wrapping non-buffer arg type %s\n%!" (string_of_lltype t);
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

  if 0 > verbosity then dump_module m;
  
  ignore (verify_cg m);
  
  (* return the wrapper function *)
  wrapper

(* TODO refactor codegen_to_file into:
 *  caller creates/frees context, parent module
 *  Arch.codegen cg_func c m e
 *      set up module(s)
 *      cg_func into device mod
 *      postprocess_function (do whatever it currently does, then drop from Arch interface
 *      on GPU:
 *          compile dev module to PTX string (C++ support lib)
 *          add PTX string as global const_stringz in host ctx
 *      create host wrapper (Arch_support CPU or Ptx internal variant)
 *      Arch_support.cg_dynamic_wrapper into host mod
 *      dispose device context, module
 *  
 *  codegen_*wrapper* stuff moves into Arch_support and Ptx
 *)

(* codegen constructs a new context and module for code generation, which it
 * returns for the caller to eventually free *)
let codegen_entry (opts:string list) (e:entrypoint) =
  let (name, _, _) = e in

  (* construct basic LLVM state *)
  let c = create_context () in
  let m = create_module c ("<" ^ name ^ "_halide_host>") in

  if Arch.target_triple != "" then 
    set_target_triple Arch.target_triple m 
  else 
    ignore (Llvm_executionengine.initialize_native_target());

  (* codegen *)
  let f = Arch.cg_entry c m cg_entry make_cg_context e opts in

  (c,m,f)

let codegen_to_bitcode_and_header (opts:string list) (e:entrypoint) =
  let (object_name, _, _) = e in

  let bitcode_file = object_name ^ ".bc" in
  let header_file = object_name ^ ".h" in

  (* codegen *)
  let (c,m,f) = codegen_entry opts e in

  (* build the convenience wrapper *)
  ignore (codegen_c_wrapper opts c m f);

  (* write to bitcode file *)
  save_bc_to_file m bitcode_file;

  (* write the header *)
  codegen_c_header e header_file;

  (* free memory *)
  dispose_module m;
  dispose_context c

end
