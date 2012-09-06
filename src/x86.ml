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

let codegen_entry c m cg_entry _ e opts =
  (* set up module *)
  if List.mem "avx" opts then
    Stdlib.init_module_x86_avx m
  else
    Stdlib.init_module_x86 m;

  (* build the inner kernel, which takes raw byte*s *)
  let inner = cg_entry c m e opts in

  (* return the wrapper which takes buffer_t*s *)
  cg_wrapper c m e inner

let rec cg_expr (con:context) (expr:expr) =
  let c = con.c and m = con.m and b = con.b in
  let use_avx = List.mem "avx" con.arch_opts in
  let cg_expr = cg_expr con in

  let ptr_t = pointer_type (i8_type c) in
  let i16x8_t = vector_type (i16_type c) 8 in
  let i8x16_t = vector_type (i8_type c) 16 in
  let i8x32_t = vector_type (i8_type c) 32 in
  let i32_t = i32_type c in

  let max_i8 = Cast (Int(8), IntImm 127) in
  let min_i8 = Cast (Int(8), IntImm (-128)) in
  let max_i16 = Cast (Int(16), IntImm 32767) in
  let min_i16 = Cast (Int(16), IntImm (-32768)) in
  let max_i32 = Cast (Int(32), IntImm 2147483647) in
  let min_i32 = Cast (Int(32), IntImm (-2147483648)) in  
  let max_u8 = Cast (UInt(8), IntImm 255) in
  let max_u16 = Cast (UInt(8), IntImm 65535) in

  let rec pow2 = function
    | 0 -> 1
    | x -> (pow2 (x-1)) * 2
  in

  let call_intrin name args = 
    let t x = type_of_val_type c (val_type_of_expr x) in
    let intrin = declare_function ("llvm.x86." ^ name) (function_type (t expr) (Array.map t args)) m in
    build_call intrin (Array.map cg_expr args) "" b
  in

  let cg_unsigned_integer_division x y intrin table bits elts =
    begin match Array.get table (y-2) with
      | (meth, mul, shift) -> 
          (* Codegen the argument *)
          let t0 = cg_expr x in 
        
          (* Possibly do a multiply-keep-high-half (methods 1 and 2) *)
          let t1 = if meth > 0 then 
              let mul = cg_expr (Broadcast (Cast (UInt bits, IntImm mul), elts)) in
              build_call intrin [|t0; mul|] "" b
            else 
              t0
          in
          
          (* Possibly add a correcting factor of (t0-t1)/2 (method 2 only) *)
          let t2 = if meth > 1 then           
              let sh = cg_expr (Broadcast (Cast (Int bits, IntImm 1), elts)) in
              let y = build_sub t0 t1 "" b in
              let y = build_lshr y sh "" b in
              build_add t1 y "" b
            else 
              t1 
          in

          (* Perform a final shift if necessary (all methods) *)
          let sh = cg_expr (Broadcast (Cast (Int bits, IntImm shift), elts)) in
          if shift != 0 then build_lshr t2 sh "" b else t2
    end
  in

  let cg_signed_integer_division x y intrin table bits elts =
    begin match Array.get table (y-2) with
      | (meth, mul, shift) -> 
          (* Codegen the argument *)
          let t0 = cg_expr x in 
        
          (* Do a multiply-keep-high-half *)
          let mul = cg_expr (Broadcast (Cast (Int bits, IntImm mul), elts)) in
          let t1 = build_call intrin [|t0; mul|] "" b in
          
          (* Possibly add a correcting factor of t0 (method 1 only) *)
          let t2 = if meth > 0 then           
              build_add t0 t1 "" b
            else 
              t1 
          in

          (* Perform a shift if necessary (all methods) *)
          let sh = cg_expr (Broadcast (Cast (Int bits, IntImm shift), elts)) in
          let t3 = if shift != 0 then build_ashr t2 sh "" b else t2 in

          (* Add one for negative numbers *)
          let sh = cg_expr (Broadcast (Cast (Int bits, IntImm (bits-1)), elts)) in
          let sign_bit = build_lshr t0 sh "" b in
          build_add sign_bit t3 "" b

    end
  in

  (* Peephole optimizations for x86. 

     We either produce specially-crafted ll that we know will codegen
     cleanly, call intrinsics directly, or call wrappers around
     intrinsics that we've put in the stdlib for x86 *)
  match expr with 

    (* Averaging unsigned bytes or words *)
    | Cast (UIntVector(narrow, elts), 
            Bop (Div, 
                 Bop (Add, 
                      Bop (Add, 
                           Cast(UIntVector(wide, _), l), 
                           Cast(UIntVector(_, _), r)),
                      one),
                 two))
        when (wide > narrow) && 
          (narrow = 8 || narrow = 16) &&
          (elts * narrow = 128) &&
          one = make_const (UIntVector (wide, elts)) 1 && 
          two = make_const (UIntVector (wide, elts)) 2 -> 
      let intrin = if narrow = 8 then "sse2.pavg.b" else "sse2.pavg.w" in
      call_intrin intrin [|l; r|]

    (* x86 doesn't do 16 bit vector division, but for small constants you can do multiplication instead. *)
    | Bop (Div, x, Broadcast (Cast (Int 16, IntImm y), 8)) when y > 1 && y < 64 ->
      let intrin = declare_function "llvm.x86.sse2.pmulh.w" (function_type i16x8_t [|i16x8_t; i16x8_t|]) m in 
      let table = Integer_division.table_s16 in
      cg_signed_integer_division x y intrin table 16 8  
        
    | Bop (Div, x, Broadcast (Cast (UInt 16, IntImm y), 8)) when y > 1 && y < 64 ->
      let intrin = declare_function "llvm.x86.sse2.pmulhu.w" (function_type i16x8_t [|i16x8_t; i16x8_t|]) m in 
      let table = Integer_division.table_u16 in
      cg_unsigned_integer_division x y intrin table 16 8
        
    (* Also use mulh.w and mulhu.w when explicity requested using a widening mul followed by a shift *)
    | Cast (IntVector (16, 8), 
            Bop (Div, 
                 Bop (Mul, 
                      Cast (IntVector (32, 8), l),
                      Cast (IntVector (32, 8), r)), 
                 Broadcast (IntImm 65536, 8))) ->
      call_intrin "sse2.pmulh.w" [|l; r|]

    | Cast (UIntVector (16, 8), 
            Bop (Div, 
                 Bop (Mul, 
                      Cast (UIntVector (32, 8), l),
                      Cast (UIntVector (32, 8), r)), 
                 Broadcast (Cast (UInt 32, IntImm 65536), 8))) ->
      call_intrin "sse2.pmulhu.w" [|l; r|]

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

    (* Mins and maxes *)
    | Bop (op, l, r) when op = Min or op = Max ->
        let t = val_type_of_expr l in
        let intrin = match (op, t) with
          | (Min, IntVector   (8, 16)) -> "sse41.pminsb"
          | (Min, UIntVector  (8, 16)) -> "sse2.pminu.b"
          | (Min, IntVector   (16, 8)) -> "sse2.pmins.w"
          | (Min, UIntVector  (16, 8)) -> "sse41.pminuw"
          | (Min, IntVector   (32, 4)) -> "sse41.pminsd"
          | (Min, UIntVector  (32, 4)) -> "sse41.pminud"
          | (Min, FloatVector (32, 4)) -> "sse.min.ps"
          | (Min, FloatVector (64, 2)) -> "sse2.min.pd"
          | (Max, IntVector   (8, 16)) -> "sse41.pmaxsb"
          | (Max, UIntVector  (8, 16)) -> "sse2.pmaxu.b"
          | (Max, IntVector   (16, 8)) -> "sse2.pmaxs.w"
          | (Max, UIntVector  (16, 8)) -> "sse41.pmaxuw"
          | (Max, IntVector   (32, 4)) -> "sse41.pmaxsd"
          | (Max, UIntVector  (32, 4)) -> "sse41.pmaxud"
          | (Max, FloatVector (32, 4)) -> "sse.max.ps"
          | (Max, FloatVector (64, 2)) -> "sse2.max.pd"
          | _ -> ""
        in
        begin match intrin with
          | "" -> con.cg_expr expr (* there's no intrinsic for this *)
          | _ -> call_intrin intrin [|l; r|]
        end

    (* Saturating adds and subs for 8 and 16 bit ints *)
    | Cast (IntVector(8, 16), 
           Bop (Max, 
                Bop (Min, 
                     Bop (op, Cast(IntVector(wide, _), l), Cast(IntVector(_, _), r)),
                     Broadcast (Cast (_, IntImm 127), _)),
                Broadcast (Cast (_, IntImm (-128)), _)))
        when (wide > 8) && (op = Add or op = Sub) -> 
      begin match op with
        | Add -> call_intrin "sse2.padds.b" [|l; r|]
        | Sub -> call_intrin "sse2.psubs.b" [|l; r|]
      end
    | Cast (UIntVector(8, 16), 
            Bop (Min, 
                 Bop (op, Cast(UIntVector(wide, _), l), Cast(UIntVector(_, _), r)),
                 Broadcast (Cast (_, IntImm 255), _)))
        when (wide > 8) && (op = Add or op = Sub) -> 
      begin match op with
        | Add -> call_intrin "sse2.paddus.b" [|l; r|]
        | Sub -> call_intrin "sse2.psubus.b" [|l; r|]
      end
    | Cast (IntVector(16, 8), 
           Bop (Max, 
                Bop (Min, 
                     Bop (op, Cast(IntVector(wide, _), l), Cast(IntVector(_, _), r)),
                     Broadcast (IntImm 32767, _)),
                Broadcast (IntImm (-32768), _)))
        when (wide > 16) && (op = Add or op = Sub) -> 
      begin match op with
        | Add -> call_intrin "sse2.padds.w" [|l; r|]
        | Sub -> call_intrin "sse2.psubs.w" [|l; r|]
      end
    | Cast (UIntVector(16, 8), 
            Bop (Min, 
                 Bop (op, Cast(UIntVector(wide, _), l), Cast(UIntVector(_, _), r)),
                 Broadcast (Cast (_, IntImm 65535), _)))
        when (wide > 16) && (op = Add or op = Sub) -> 
      begin match op with
        | Add -> call_intrin "sse2.paddus.w" [|l; r|]
        | Sub -> call_intrin "sse2.psubus.w" [|l; r|]
      end

    (* Saturating narrowing casts. We recognize them here, and implement them in architecture.x86.stdlib.ll *)
    | Cast (IntVector(8, 16),
            Bop (Max, 
                 Bop (Min, arg, 
                      Broadcast (Cast (_, IntImm 127), _)),
                 Broadcast (Cast (_, IntImm (-128)), _))) 
        when val_type_of_expr arg = IntVector(16, 16) ->
      con.cg_expr (Call (Extern, IntVector(8, 16), "packsswb", [arg]))
    | Cast (UIntVector(8, 16),
            Bop (Max, 
                 Bop (Min, arg, 
                      Broadcast (Cast (_, IntImm 255), _)),
                 Broadcast (Cast (_, IntImm 0), _))) 
        when val_type_of_expr arg = IntVector(16, 16) ->
      con.cg_expr (Call (Extern, UIntVector(8, 16), "packuswb", [arg]))
    | Cast (IntVector(16, 8),
            Bop (Max, 
                 Bop (Min, arg, 
                      Broadcast (IntImm 32767, _)),
                 Broadcast (IntImm (-32768), _))) 
        when val_type_of_expr arg = IntVector(32, 8) ->
      con.cg_expr (Call (Extern, IntVector(16, 8), "packssdw", [arg]))
    | Cast (UIntVector(16, 8),
            Bop (Max, 
                 Bop (Min, arg, 
                      Broadcast (IntImm 65535, _)),
                 Broadcast (IntImm 0, _))) 
        when val_type_of_expr arg = IntVector(32, 8) ->
      con.cg_expr (Call (Extern, UIntVector(16, 8), "packusdw", [arg]))

    (* Inverse square root *)
    | Bop (Div, Broadcast(FloatImm 1.0, 4), Call (Extern, FloatVector(32, 4), ".sqrt_f32", [arg])) ->
      call_intrin "sse.rsqrt.ps" [|arg|]

    | Bop (Div, Broadcast(FloatImm 1.0, 8), Call (Extern, FloatVector(32, 8), ".sqrt_f32", [arg])) when use_avx ->
      call_intrin "avx.rsqrt.ps.256" [|arg|]

    (* Inverse *)
    | Bop (Div, Broadcast(FloatImm 1.0, 4), arg) ->
      call_intrin "sse.rcp.ps" [|arg|]  
        
    | Bop (Div, Broadcast(FloatImm 1.0, 8), arg) when use_avx ->
      call_intrin "avx.rcp.ps.256" [|arg|]

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
  
