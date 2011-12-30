open Ir
open Llvm
open List
open Util
open Ir_printer
open Cg_llvm_util
open Analysis

exception MissingEntrypoint
exception UnimplementedInstruction
exception UnalignedVectorMemref
exception ArgExprOfBufferArgument (* The Arg expr can't dereference a Buffer, only Scalars *)
exception ArgTypeMismatch of val_type * val_type

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
  val codegen_entry : entrypoint -> llcontext * llmodule * llvalue
  val codegen_c_wrapper : llcontext -> llmodule -> llvalue -> llvalue
  val save_bc_to_file : llmodule -> string -> unit
  val codegen_c_header : entrypoint -> string -> unit
  val codegen_to_bitcode_and_header : entrypoint -> unit
  val codegen_to_file : entrypoint -> string -> unit
end

module CodegenForArch ( Arch : Architecture ) = struct

let dbgprint = false

(* Algebraic type wrapper for LLVM comparison ops *)
type cmp =
  | CmpInt of Icmp.t
  | CmpFloat of Fcmp.t

module ArgMap = Map.Make(String)
type argmap = (arg*int) ArgMap.t (* track args as name -> (Ir.arg,index) *) 

let cg_entry c m e =

  let int_imm_t = i32_type c in
  let int32_imm_t = i32_type c in
  let float_imm_t = float_type c in
  let buffer_t = raw_buffer_t c in

  let type_of_val_type = type_of_val_type c in

  (* The symbol table for loop variables *)
  let sym_table =
    Hashtbl.create 10 
  in

  let sym_add name llv =
    set_value_name name llv;
    Hashtbl.add sym_table name llv
  and sym_add_no_rename name llv =
    Hashtbl.add sym_table name llv
  and sym_remove name =
    Hashtbl.remove sym_table name
  and sym_get name =
    try Hashtbl.find sym_table name
    with Not_found -> raise (Wtf ("symbol " ^ name ^ " not found"))
  in

  (* unpack the entrypoint *)
  let (entrypoint_name, arglist, s) = e in
  let type_of_arg = function
    | Scalar (_, vt) -> [type_of_val_type vt]
    | Buffer _ -> [buffer_t; int_imm_t; int_imm_t; int_imm_t; int_imm_t]
  and name_of_arg = function
    | Scalar (n, _) -> [n]
    | Buffer n -> [n; n ^ ".dim.0"; n ^ ".dim.1"; n ^ ".dim.2"; n ^ ".dim.3"] 
  in

  let argtypes = List.flatten (List.map type_of_arg arglist) in
  
  (* define `void main(arg1, arg2, ...)` entrypoint*)
  let entrypoint_fn =
    define_function
      entrypoint_name
      (function_type (void_type c) (Array.of_list argtypes))
      m
  in

  let argnames = List.flatten (List.map name_of_arg arglist) in
  let argvals = List.map (param entrypoint_fn) (0 -- (List.length argnames)) in

  (* Put args in the sym table *)
  List.iter
    (fun (n, t, llval) ->
      sym_add n llval;
      (* And mark each buffer arg as Noalias *)
      if t = buffer_t then add_param_attr llval Attribute.Noalias else ()
    ) (list_zip3 argnames argtypes argvals);

  (* start codegen at entry block of main *)
  let b = builder_at_end c (entry_block entrypoint_fn) in
  
  (* A place for allocas
  let alloca_bb = append_block c ("alloca") (block_parent (insertion_block b)) in
  ignore(build_br alloca_bb b);
  let after_alloca_bb = append_block c ("after_alloca") (block_parent (insertion_block b)) in
  position_at_end alloca_bb b;  
  let alloca_end = build_br after_alloca_bb b in
  position_at_end after_alloca_bb b;  
  *)

  let rec cg_expr e = 
    if dbgprint then Printf.printf "begin cg_expr %s\n%!" (string_of_expr e);
    let result = Arch.cg_expr c m b cg_expr_inner e in
    if dbgprint then Printf.printf "end cg_expr %s -> %s\n%!" (string_of_expr e) (string_of_lltype (type_of result));
    result
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

    | Bop(Add, l, r) -> cg_binop build_add  build_add  build_fadd l r
    | Bop(Sub, l, r) -> cg_binop build_sub  build_sub  build_fsub l r
    | Bop(Mul, l, r) -> cg_binop build_mul  build_mul  build_fmul l r
    | Bop(Div, l, r) -> cg_binop build_sdiv build_udiv build_fdiv l r
    | Bop(Min, l, r) -> cg_minmax Min l r
    | Bop(Max, l, r) -> cg_minmax Max l r
    | Bop(Mod, l, r) -> cg_mod l r

    (* comparison *)
    | Cmp(EQ, l, r) -> cg_cmp Icmp.Eq  Icmp.Eq  Fcmp.Oeq l r
    | Cmp(NE, l, r) -> cg_cmp Icmp.Ne  Icmp.Ne  Fcmp.One l r
    | Cmp(LT, l, r) -> cg_cmp Icmp.Slt Icmp.Ult Fcmp.Olt l r
    | Cmp(LE, l, r) -> cg_cmp Icmp.Sle Icmp.Ule Fcmp.Ole l r
    | Cmp(GT, l, r) -> cg_cmp Icmp.Sgt Icmp.Ugt Fcmp.Ogt l r
    | Cmp(GE, l, r) -> cg_cmp Icmp.Sge Icmp.Uge Fcmp.Oge l r

    (* logical *)
    | And (l, r) -> build_and (cg_expr l) (cg_expr r) "" b
    | Or (l, r) -> build_or (cg_expr l) (cg_expr r) "" b
    | Not l -> build_not (cg_expr l) "" b

    (* Select *)
    | Select(c, t, f) -> 
        begin
          match val_type_of_expr c with
            | Int _ | UInt _ | Float _ -> 
                build_select (cg_expr c) (cg_expr t) (cg_expr f) "" b
            | IntVector(_, n) | UIntVector(_, n) | FloatVector(_, n) ->
                let bits = element_width (val_type_of_expr t) in
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

    (* Extern calls *)
    | Call(t, name, args) ->
        (* declare the extern function *)
        let arg_types = List.map (fun arg -> type_of_val_type (val_type_of_expr arg)) args in
        let name = base_name name in
        let llfunc = declare_function name
          (function_type (type_of_val_type t) (Array.of_list arg_types)) m in

        (* codegen args and call *)
        let llargs = List.map (fun arg -> cg_expr arg) args in
        build_call llfunc (Array.of_list (llargs)) ("extern_" ^ name) b

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
        in      
        let result = rep n in
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

  and cg_minmax op l r =
    let cmp = match op with Min -> LT | Max -> GT | _ -> raise (Wtf "cg_minmax with non-min/max op") in
    cg_expr (Select (Cmp (cmp, l, r), l, r))
      
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
      | UIntVector(fb, fw), IntVector(tb, tw) when fw = tw && fb*2 = tb ->
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
      let next_var = build_add variable (const_int int_imm_t 1) (var_name ^ "_nextvar") b in 

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
    (* Find all references in body to things outside of it *)
    let rec find_names_in_stmt internal = function
      | Store (e, buf, idx) ->
          let inner = StringIntSet.union (find_names_in_expr internal e) (find_names_in_expr internal idx) in
          if (StringSet.mem buf internal) then inner else (StringIntSet.add (buf, 8) inner)
      | Pipeline (name, ty, size, produce, consume) ->
          let internal = StringSet.add name internal in
          string_int_set_concat [
            find_names_in_expr internal size;
            find_names_in_stmt internal produce;
            find_names_in_stmt internal consume]
      | LetStmt (name, value, stmt) ->
          let internal = StringSet.add name internal in
          StringIntSet.union (find_names_in_expr internal value) (find_names_in_stmt internal stmt)
      | Block l ->
          string_int_set_concat (List.map (find_names_in_stmt internal) l)
      | For (var_name, min, size, order, body) ->
          let internal = StringSet.add var_name internal in
          string_int_set_concat [
            find_names_in_expr internal min;
            find_names_in_expr internal size;
            find_names_in_stmt internal body]
      | Print (fmt, args) ->
          string_int_set_concat (List.map (find_names_in_expr internal) args)
      | Provide (fn, _, _) ->
          raise (Wtf "Encountered a provide during cg. These should have been lowered away.")
    and find_names_in_expr internal = function
      | Load (_, buf, idx) ->
          let inner = find_names_in_expr internal idx in
          if (StringSet.mem buf internal) then inner else (StringIntSet.add (buf, 8) inner)
      | Var (t, n) ->
          let size = (bit_width t)/8 in
          if (StringSet.mem n internal) then StringIntSet.empty else (StringIntSet.add (n, size) StringIntSet.empty)
      | Let (n, a, b) ->
          let internal = StringSet.add n internal in
          StringIntSet.union (find_names_in_expr internal a) (find_names_in_expr internal b)
      | x -> fold_children_in_expr (find_names_in_expr internal) StringIntSet.union StringIntSet.empty x
    in

    (* Dump everything relevant in the symbol table into a closure *)
    let syms = find_names_in_stmt (StringSet.add var_name StringSet.empty) body in
    
    (* Compute an offset in bytes into a closure for each symbol name, and put
       that offset into a string map *)
    let (total_bytes, offset_map) = 
      StringIntSet.fold (fun (name, size) (offset, offset_map) ->
        let current_val = sym_get name in
        (* round size up to the nearest power of two for alignment *)
        let rec roundup x y = if (x >= y) then x else roundup (x*2) y in
        let size = roundup 1 size in
        (* bump offset up to be a multiple of size for alignment *)
        let offset = ((offset+size-1)/size)*size in
        Printf.printf "Putting %s at offset %d\n" name offset;
        (offset + size, StringMap.add name offset offset_map)
      ) syms (0, StringMap.empty) 
    in

    (* Allocate a closure *)
    let closure = Arch.malloc c m b cg_expr (IntImm total_bytes) in

    (* Store everything in the closure *)
    StringIntSet.iter (fun (name, size) ->      
      let current_val = sym_get name in
      let offset = StringMap.find name offset_map in
      let addr = build_bitcast closure (pointer_type (type_of current_val)) "" b in      
      let addr = build_gep addr [| const_int int_imm_t (offset/size) |] "" b in
      ignore (build_store current_val addr b);
    ) syms;

    (* Make a function that represents the body *)
    let body_fn_type = (function_type (void_type c) [|int_imm_t; buffer_t|]) in
    let body_fn = define_function (var_name ^ ".body") body_fn_type m in

    (* Save the old position of the builder *)
    let current = insertion_block b in

    (* Move the builder inside the function *)
    position_at_end (entry_block body_fn) b;

    (* Load everything from the closure *)
    StringIntSet.iter (fun (name, size) ->      
      let closure = param body_fn 1 in
      let current_val = sym_get name in
      let offset = StringMap.find name offset_map in
      let addr = build_bitcast closure (pointer_type (type_of current_val)) "" b in      
      let addr = build_gep addr [| const_int int_imm_t (offset/size) |] "" b in
      sym_add name (build_load addr name b);
    ) syms;

    (* Make the var name refer to the first argument *)
    Printf.printf "%s = %s\n%!" var_name "param body_fn 0";
    sym_add var_name (param body_fn 0);    

    (* Generate the function body *)
    ignore (cg_stmt body);
    ignore (build_ret_void b);

    sym_remove var_name;

    (* Pop all the symbols we used in the function body *)
    StringIntSet.iter (fun (name, _) -> sym_remove name);

    (* Move the builder back *)
    position_at_end current b;

    (* Call do_par_for *)
    let do_par_for = declare_function "do_par_for"
      (function_type (void_type c) [|pointer_type body_fn_type; int_imm_t; int_imm_t; buffer_t|]) m in
    ignore(build_call do_par_for [|body_fn; min; size; closure|] "" b);

    (* Free the closure *)
    Arch.free c m b closure;
    
    (* Return an ignorable llvalue *)
    const_int int_imm_t 0

  and cg_stmt stmt = Arch.cg_stmt c m b cg_stmt_inner stmt
  and cg_stmt_inner = function
    (* TODO: unaligned vector store *)
    | Store(e, buf, idx) -> cg_store e buf idx
    | For(name, min, n, order, stmt) ->
        (if order then cg_for else cg_par_for) name (cg_expr min) (cg_expr n) stmt
    | Block (first::second::rest) ->
        ignore(cg_stmt first);
        cg_stmt (Block (second::rest))
    | Block(first::[]) ->
        cg_stmt first
    | Block _ -> raise (Wtf "cg_stmt of empty block")

    | LetStmt (name, value, stmt) ->
        sym_add name (cg_expr value);
        let result = cg_stmt stmt in
        sym_remove name;
        result    

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
        
        (* See if we can't reduce it to a constant... *)
        let upgraded_size = Constant_fold.constant_fold_expr upgraded_size in
        
        let scratch = 
          let bytes = size *~ (IntImm ((bit_width ty)/8)) in 
          Arch.malloc c m b cg_expr bytes
        in
        
        (* push the symbol environment *)
        sym_add name scratch;

        ignore (cg_stmt produce);
        let res = cg_stmt consume in

        (* pop the symbol environment *)
        sym_remove name;

        (* free the scratch *)
        ignore (Arch.free c m b scratch);

        res
    | Print (fmt, args) -> cg_print fmt args

  and cg_store e buf idx =
    (* ignore(cg_debug e ("Store to " ^ buf ^ " ") [idx; e]); *)
    match (is_vector e, is_vector idx) with
      | (_, true) ->
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

      | (true, false)  -> raise (Wtf "Can't store a vector to a scalar address")

  and cg_aligned_store e buf idx =
    let w = vector_elements (val_type_of_expr idx) in
    (* Handle aligned dense vector store of scalar by broadcasting first *)
    let expr = if is_scalar e then Broadcast(e, w) else e in
    build_store (cg_expr expr) (cg_memref (val_type_of_expr expr) buf idx) b

  and cg_unaligned_store e buf idx =
    let t = val_type_of_expr e in

    (* We special-case a few common sizes *)
    if (bit_width t = 128) then begin
      let i8x16_t = vector_type (i8_type c) 16 in
      let unaligned_store_128 = declare_function "unaligned_store_128"
        (function_type (void_type c) [|i8x16_t; buffer_t|]) m in
      let addr = build_pointercast (cg_memref t buf idx) buffer_t "" b in
      let value = cg_expr e in
      let value = build_bitcast value i8x16_t "" b in
      build_call unaligned_store_128 [|value; addr|] "" b
    end else cg_scatter e buf (Ramp(idx, IntImm 1, vector_elements t))

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
    (* We special-case a few common sizes *)
    if (bit_width t = 128) then begin
      let i8x16_t = vector_type (i8_type c) 16 in
      let unaligned_load_128 = declare_function "unaligned_load_128"
        (function_type (i8x16_t) [|buffer_t|]) m in
      let addr = build_pointercast (cg_memref t buf idx) buffer_t "" b in
      let value = build_call unaligned_load_128 [|addr|] "" b in
      build_bitcast value (type_of_val_type t) "" b
    end else cg_gather t buf (Ramp(idx, IntImm 1, vector_elements t))

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
    in 
    let result = insert_idx ((vector_elements t) - 1) in
    result
      

  and cg_memref vt buf idx =
    (* load the global buffer** *)
    let base = sym_get buf in
    (* cast pointer to pointer-to-target-type *)
    let elem_type = type_of_val_type (element_val_type vt) in
    let ptr = build_pointercast base (pointer_type (elem_type)) "memref_elem_ptr" b in
    (* build getelementpointer into buffer *)
    let gep = build_gep ptr [| cg_expr idx |] "memref" b in
    build_pointercast gep (pointer_type (type_of_val_type vt)) "typed_memref" b
      
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
    let global_fmt = build_pointercast global_fmt (pointer_type (i8_type c)) "" b in

    (*Printf.printf "cg_debug: %s %s %s\n" (string_of_expr e) (prefix^fmt) (String.concat ", " (List.map string_of_expr args));

    Printf.printf "%s\n%!" (string_of_lltype (type_of global_fmt)); *)

    let ll_printf = declare_function "printf" 
      (var_arg_function_type (i32_type c) [|pointer_type (i8_type c)|]) m in
    build_call ll_printf (Array.of_list (global_fmt::ll_args)) "" b    

  and cg_debug e prefix args =
    let ll_e = cg_expr e in
    ignore(cg_print prefix args);
    ll_e
      
  in

  (* actually generate from the root statement *)
  ignore (cg_stmt s);
  
  (* return void from main *)
  ignore (build_ret_void b);
  
  if dbgprint then dump_module m;
  
  ignore (verify_cg m);
  
  (* return generated module and function *)
  entrypoint_fn


(*
 * Wrappers
 *)

(* C runner wrapper *)
let codegen_c_wrapper c m f =

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
          Printf.printf "Wrapping buffer arg type %s\n%!" typename;
          assert (typename = "struct.buffer_t");
          build_pointercast arg_ptr t "" b
      | _ ->
          Printf.printf "Wrapping non-buffer arg type %s\n%!" (string_of_lltype t);
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

let save_bc_to_file m fname =
  begin match Llvm_bitwriter.write_bitcode_file m fname with
    | false -> raise (BCWriteFailed fname)
    | true -> ()
  end

let codegen_c_header e header_file =
  let (object_name, args, _) = e in

  (* Produce a header *)
  let string_of_type = function
    | Int bits -> "int" ^ (string_of_int bits) ^ "_t"
    | UInt bits -> "uint" ^ (string_of_int bits) ^ "_t"
    | Float 32 -> "float"
    | Float 64 -> "double"
    | _ -> raise (Wtf "Bad type for toplevel argument")
  in
  let string_of_arg = function
    | Scalar (n, t) -> (string_of_type t) ^ " " ^ (String.sub n 1 ((String.length n)-1))
    | Buffer n -> "buffer_t *" ^ (String.sub n 1 ((String.length n)-1))
  in
  let arg_string = String.concat ", " (List.map string_of_arg args) in
  let lines = 
    ["#ifndef " ^ object_name ^ "_h";
     "#define " ^ object_name ^ "_h";
     "";
     "#include <stdint.h>";
     "";
     "typedef struct buffer_t {";
     "  uint8_t* host;";
     "  uint64_t dev;";
     "  bool host_dirty;";
     "  bool dev_dirty;";
     "  size_t dims[4];";
     "  size_t elem_size;";
     "} buffer_t;";
     "";
     "void " ^ object_name ^ "(" ^ arg_string ^ ");";
     "";
     "#endif"]
  in

  let out = open_out header_file in
  output_string out (String.concat "\n" lines);
  close_out out

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
let codegen_entry (e:entrypoint) =
  let (name, _, _) = e in

  (* construct basic LLVM state *)
  let c = create_context () in
  let m = create_module c ("<" ^ name ^ "_halide_host>") in

  (* codegen *)
  let f = Arch.codegen_entry c m cg_entry e in

  (c,m,f)

(* TODO: this is fairly redundant with codegen_to_file *)
let codegen_to_bitcode_and_header (e:entrypoint) =
  let (object_name, _, _) = e in

  let bitcode_file = object_name ^ ".bc" in
  let header_file = object_name ^ ".h" in

  (* codegen *)
  let (c,m,f) = codegen_entry e in

  (* build the convenience wrapper *)
  ignore (codegen_c_wrapper c m f);

  (* write to bitcode file *)
  save_bc_to_file m bitcode_file;

  (* write the header *)
  codegen_c_header e header_file;

  (* free memory *)
  dispose_module m;
  dispose_context c

(* TODO: drop this - it's redundant with codegen_to_bitcode_and_header, and can
 * be trivially reconstituted from the rest of the interfaces available *)
let codegen_to_file e filename =

  let (c,m,f) = codegen_entry e in

  (* build the convenience wrapper *)
  ignore (codegen_c_wrapper c m f);

  save_bc_to_file m filename;

  (* free memory *)
  dispose_module m;
  dispose_context c

end

module CodegenForHost = struct
  let hosttype = 
    try
      Sys.getenv "HOSTTYPE" 
    with Not_found ->
      Printf.printf "Could not detect host architecture (HOSTTYPE not set). Assuming x86_64.\n";
      "x86_64"
  
  (* TODO: this is ugly. Not sure there's a better way. *)
  let codegen_entry e = 
    if hosttype = "arm" then
      let module Cg = CodegenForArch(Arm) in
      Cg.codegen_entry e
    else (* if hosttype = "x86_64" then *)
      let module Cg = CodegenForArch(X86) in
      Cg.codegen_entry e

  let codegen_c_wrapper c e = 
    if hosttype = "arm" then
      let module Cg = CodegenForArch(Arm) in
      Cg.codegen_c_wrapper c e
    else (* if hosttype = "x86_64" then *)
      let module Cg = CodegenForArch(X86) in
      Cg.codegen_c_wrapper c e

  let save_bc_to_file c e = 
    if hosttype = "arm" then
      let module Cg = CodegenForArch(Arm) in
      Cg.save_bc_to_file c e
    else (* if hosttype = "x86_64" then *)
      let module Cg = CodegenForArch(X86) in
      Cg.save_bc_to_file c e

  let codegen_c_header c e = 
    if hosttype = "arm" then
      let module Cg = CodegenForArch(Arm) in
      Cg.codegen_c_header c e
    else (* if hosttype = "x86_64" then *)
      let module Cg = CodegenForArch(X86) in
      Cg.codegen_c_header c e

  let codegen_to_bitcode_and_header e = 
    if hosttype = "arm" then
      let module Cg = CodegenForArch(Arm) in
      Cg.codegen_to_bitcode_and_header e
    else (* if hosttype = "x86_64" then *)
      let module Cg = CodegenForArch(X86) in
      Cg.codegen_to_bitcode_and_header e
  
  let codegen_to_file e f = 
    if hosttype = "arm" then
      let module Cg = CodegenForArch(Arm) in
      Cg.codegen_to_file e f
    else (* if hosttype = "x86_64" then *)
      let module Cg = CodegenForArch(X86) in
      Cg.codegen_to_file e f
end
