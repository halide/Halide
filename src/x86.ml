open Ir
open Llvm
open Cg_llvm_util
open Util
open Analysis

type state = int (* dummy - this Arch carries no extra state around during codegen *)
type context = state cg_context
let start_state () = 0

let pointer_size = 8

(* The target triple is different on os x and linux, so we'd better
   just let it use the native host. This makes it hard to generate x86
   code from ARM. What a shame. *)
(* "x86_64-unknown-linux-gnu" *)
let target_triple = "" 

let codegen_entry c m cg_entry _ e =
  (* set up module *)
  Stdlib.init_module_x86 m;

  (* build the inner kernel, which takes raw byte*s *)
  let inner = cg_entry c m e in

  (* return the wrapper which takes buffer_t*s *)
  cg_wrapper c m e inner

let rec cg_expr (con:context) (expr:expr) =
  let c = con.c and m = con.m and b = con.b in
  let cg_expr = cg_expr con in

  let ptr_t = pointer_type (i8_type c) in
  let i16x8_t = vector_type (i16_type c) 8 in
  let i8x16_t = vector_type (i8_type c) 16 in
  let i8x32_t = vector_type (i8_type c) 32 in
  let i32_t = i32_type c in

  (* Peephole optimizations for x86 *)
  match expr with 
    (* x86 doesn't do 16 bit vector division, but for constants you can do multiplication instead. *)
    | Bop (Div, x, Broadcast (Cast (UInt 16, IntImm y), 8)) ->
        let pmulhw = declare_function "llvm.x86.sse2.pmulh.w"
          (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in
        let z = (65536/y + 1) in
        let lhs = cg_expr x in
        let rhs = cg_expr (Broadcast (Cast (UInt 16, IntImm z), 8)) in        
        build_call pmulhw [|lhs; rhs|] "" b
          
    (* unaligned dense 128-bit loads use movups *)
    | Load (t, buf, Ramp(base, IntImm 1, n)) when (bit_width t = 128) ->
        begin match (Analysis.reduce_expr_modulo base n) with 
          | Some _ -> con.cg_expr expr
          | _ ->              
              let unaligned_load_128 = declare_function "unaligned_load_128"
                (function_type (i8x16_t) [|ptr_t|]) m in
              let addr = build_pointercast (con.cg_memref t buf base) ptr_t "" b in
              let value = build_call unaligned_load_128 [|addr|] "" b in
              build_bitcast value (type_of_val_type c t) "" b        
        end

    (* unaligned dense 256-bit loads use vmovups if available *)
    | Load (t, buf, Ramp(base, IntImm 1, n)) when (bit_width t = 256) ->
        begin match (Analysis.reduce_expr_modulo base n) with 
          | Some _ -> con.cg_expr expr
          | _ ->              
              let unaligned_load_256 = declare_function "unaligned_load_256"
                (function_type (i8x32_t) [|ptr_t|]) m in
              let addr = build_pointercast (con.cg_memref t buf base) ptr_t "" b in
              let value = build_call unaligned_load_256 [|addr|] "" b in
              build_bitcast value (type_of_val_type c t) "" b        
        end

    (* Strided loads with stride 2 should load two vectors and then shuffle *)
    | Load (t, buf, Ramp(base, IntImm 2, n)) when (bit_width t = 128 or bit_width t = 256) ->
        let v1 = cg_expr (Load (t, buf, Ramp(base, IntImm 1, n))) in
        let v2 = cg_expr (Load (t, buf, Ramp(base +~ IntImm n, IntImm 1, n))) in
        let mask = List.map (fun x -> const_int i32_t (x*2)) (0 -- n) in
        build_shufflevector v1 v2 (const_vector (Array.of_list mask)) "" b

    (* Loads with stride one half should do one load and then shuffle *)
    (* TODO: this is buggy 
    | Load (t, buf, idx) when 
        (bit_width t = 128 && duplicated_lanes idx && false) ->
        let newidx = Constant_fold.constant_fold_expr (deduplicate_lanes idx) in
        let vec = match newidx with
          | Ramp (base, IntImm 1, w) ->
              cg_expr (Load (t, buf, Ramp (base, IntImm 1, w*2)))
          | _ -> 
              cg_expr (Load (t, buf, newidx))
        in
        (* Duplicate each lane *)
        let w = vector_elements (val_type_of_expr newidx) in
        let mask = List.map (fun x -> const_int i32_t (x/2)) (0 -- (w*2)) in
        build_shufflevector vec vec (const_vector (Array.of_list mask)) "" b
    *)

    (* We don't have any special tricks up our sleeve for this case, just use the default cg_expr *)
    | _ -> con.cg_expr expr 
        
let rec cg_stmt (con:context) (stmt:stmt) =
  let c = con.c and m = con.m and b = con.b in
  let cg_expr = cg_expr con in
  (* let cg_stmt = cg_stmt con in *)
  let ptr_t = pointer_type (i8_type c) in
  let i8x16_t = vector_type (i8_type c) 16 in
  let i8x32_t = vector_type (i8_type c) 32 in

  (* Peephole optimizations for x86 *)
  match stmt with
    (* unaligned 128-bit dense stores use movups *)
    | Store (e, buf, Ramp(base, IntImm 1, n)) when (bit_width (val_type_of_expr e)) = 128 ->
        begin match (Analysis.reduce_expr_modulo base n) with
          | Some 0 -> con.cg_stmt stmt
          | _ ->
              let t = val_type_of_expr e in
              let unaligned_store_128 = declare_function "unaligned_store_128"
                (function_type (void_type c) [|i8x16_t; ptr_t|]) m in
              let addr = build_pointercast (con.cg_memref t buf base) ptr_t "" b in
              let value = build_bitcast (cg_expr e) i8x16_t "" b in
              build_call unaligned_store_128 [|value; addr|] "" b        
        end
    (* unaligned 256-bit dense stores use vmovups *)
    | Store (e, buf, Ramp(base, IntImm 1, n)) when (bit_width (val_type_of_expr e)) = 256 ->
        begin match (Analysis.reduce_expr_modulo base n) with
          | Some 0 -> con.cg_stmt stmt
          | _ ->
              let t = val_type_of_expr e in
              let unaligned_store_256 = declare_function "unaligned_store_256"
                (function_type (void_type c) [|i8x32_t; ptr_t|]) m in
              let addr = build_pointercast (con.cg_memref t buf base) ptr_t "" b in
              let value = build_bitcast (cg_expr e) i8x32_t "" b in
              build_call unaligned_store_256 [|value; addr|] "" b        
        end
    (* Fall back to the default cg_stmt *)
    | _ -> con.cg_stmt stmt

(* Free some memory. Not called directly, but rather malloc below uses
   this to build a cleanup closure *)
let free (con:context) (name:string) (address:llvalue) =
  let c = con.c and m = con.m and b = con.b in
  let free = declare_function "fast_free" (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
  ignore (build_call free [|address|] "" b)

(* Allocate some memory. Returns an llval representing the address,
   and also a closure that emits the cleanup code when you call it given
   a context *)
let malloc (con:context) (name:string) (elems:expr) (elem_size:expr) =
  let c = con.c and b = con.b and m = con.m in
  let size = Constant_fold.constant_fold_expr (Cast (Int 32, elems *~ elem_size)) in
  match size with
    (* Constant-sized allocations go on the stack *)
    | IntImm bytes ->
        let chunks = ((bytes + 15)/16) in (* 16-byte aligned stack *)
        (* Get the position at the top of the function *)
        let pos = instr_begin (entry_block (block_parent (insertion_block b))) in
        (* Make a builder at the start of the entry block *)
        let b = builder_at c pos in
        (* Inject an alloca *)
        let ptr = build_array_alloca (vector_type (i32_type c) 4) (const_int (i32_type c) chunks) "" b in
        let ptr = build_pointercast ptr (pointer_type (i8_type c)) "" b in
        (ptr, fun _ -> ())
    | _ -> 
        let malloc = declare_function "fast_malloc" (function_type (pointer_type (i8_type c)) [|i32_type c|]) m in  
        let size = Constant_fold.constant_fold_expr (Cast (Int 32, elems *~ elem_size)) in
        let addr = build_call malloc [|con.cg_expr size|] name b in
        (addr, fun con -> free con name addr)

let env = Environment.empty
  
