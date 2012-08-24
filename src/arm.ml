open Llvm
open Ir
open Util
open Cg_llvm_util
open Analysis
open Ir_printer

type state = int (* dummy - we don't use anything in this Arch for now *)
type context = state cg_context
let start_state () = 0

let pointer_size = 4

let target_triple = "arm-linux-eabi"

let codegen_entry c m cg_entry _ e =
  (* set up module *)
  Stdlib.init_module_arm m;

  (* build the inner kernel, which takes raw byte*s *)
  let inner = cg_entry c m e in

  (* return the wrapper which takes buffer_t*s *)
  cg_wrapper c m e inner

let rec cg_expr (con : context) (expr : expr) =
  let c = con.c and b = con.b and m = con.m in

  let ptr_t = pointer_type (i8_type c) in
  let i32_t = i32_type c in
  (* let i8x16_t = vector_type (i8_type c) 16 in *)
  let i16x8_t = vector_type (i16_type c) 8 in 
  let f32x4_t = vector_type (float_type c) 4 in
  let i16x8x2_t = struct_type c [| i16x8_t; i16x8_t |] in
  let i16x4_t = vector_type (i16_type c) 4 in
  let i32x4_t = vector_type (i32_type c) 4 in
  (* let i16x4x2_t = struct_type c [| i16x4_t; i16x4_t |] in *)

  let cg_expr e = cg_expr con e in

  (* 
  let cg_select cond thenCase elseCase =
    let elts = vector_elements (val_type_of_expr cond) in
    let bits = element_width (val_type_of_expr thenCase) in
    let l = cg_expr thenCase in
    let r = cg_expr elseCase in
    let mask = cg_expr cond in
    let mask = build_sext mask (type_of_val_type c (IntVector (bits, elts))) "" b in
    let ones = const_all_ones (type_of mask) in
    let inv_mask = build_xor mask ones "" b in
    let t = type_of l in
    let l = build_bitcast l (type_of mask) "" b in
    let r = build_bitcast r (type_of mask) "" b in
    let result = build_or (build_and mask l "" b) (build_and inv_mask r "" b) "" b in
    build_bitcast result t "" b       
  in    
  *)
  
  let rec is_power_of_two = function
    | 1 -> true
    | x -> let y = x/2 in (is_power_of_two y) && (x = y*2)
  in

  let rec log2 = function
    | 1 -> 0
    | x -> 1 + (log2 (x/2))
  in

  (* Peephole optimizations for arm *)
  match expr with 
    | Bop (Min, l, r) when is_vector l -> 
        begin match (val_type_of_expr l) with
          | IntVector (16, 8) ->
              let op = declare_function "llvm.arm.neon.vmins.v8i16"
                (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          | IntVector (32, 4) ->
              let op = declare_function "llvm.arm.neon.vmins.v4i32"
                (function_type (i32x4_t) [|i32x4_t; i32x4_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          | FloatVector (32, 4) ->
              let op = declare_function "llvm.arm.neon.vmins.v4f32"
                (function_type (f32x4_t) [|f32x4_t; f32x4_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          (* TODO: other types *)
          | _ -> con.cg_expr (Select (l <~ r, l, r))
        end
    | Bop (Max, l, r) when is_vector l -> 
        begin match (val_type_of_expr l) with
          | IntVector (16, 8) ->
              let op = declare_function "llvm.arm.neon.vmaxs.v8i16"
                (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          | IntVector (32, 4) ->
              let op = declare_function "llvm.arm.neon.vmaxs.v4i32"
                (function_type (i32x4_t) [|i32x4_t; i32x4_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          | FloatVector (32, 4) ->
              let op = declare_function "llvm.arm.neon.vmaxs.v4f32"
                (function_type (f32x4_t) [|f32x4_t; f32x4_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          (* TODO: other types *)
          | _ -> con.cg_expr (Select (l >~ r, l, r))
        end

    (* use intrinsics for vector loads/stores *) 
    | Load (t, buf, Ramp (base, IntImm 1, w)) when (bit_width t = 128 || bit_width t = 64) && (t <> (FloatVector (64, 2))) ->
        let intrin = match t with
          | FloatVector (_, _) -> "4f32" (* there isn't a v2f64 vld *)
          | UIntVector (bits, w) 
          | IntVector (bits, w) -> (string_of_int w) ^ "i" ^ (string_of_int bits)
          | failwith -> ("ARM vector load of non-vector type " ^ (string_of_val_type t))
        in
        let llt = type_of_val_type c t in
        let ld = declare_function ("llvm.arm.neon.vld1.v" ^ intrin) 
          (function_type llt [|ptr_t; i32_t|]) m in        
        let addr = con.cg_memref t buf base in        
        let addr = build_pointercast addr ptr_t "" b in
        let result = build_call ld [|addr; const_int (i32_t) ((bit_width (element_val_type t))/8)|] "" b in
        result

    (* Strided loads can call vld2 *)
    | Load (IntVector (16, 8), buf, Ramp (base, IntImm 2, w)) 
    | Load (UIntVector (16, 8), buf, Ramp (base, IntImm 2, w)) ->
        let ld = declare_function "llvm.arm.neon.vld2.v8i16"
          (function_type (i16x8x2_t) [|ptr_t; i32_t|]) m in
        let (base, which, known) = match (Analysis.reduce_expr_modulo base 2) with
          | Some 0 -> (base, 0, true)
          | Some 1 -> (Constant_fold.constant_fold_expr (base -~ IntImm 1), 1, true)
          | None -> (base, 0, false)
          | _ -> failwith "Math no work"
        in
        if known then                 
          let addr = con.cg_memref (IntVector (16, 8)) buf base in        
          let addr = build_pointercast addr ptr_t "" b in
          let result = build_call ld [|addr; const_int (i32_t) 16|] "" b in
          build_extractvalue result which "" b 
        else
          failwith "Unknown alignment on vld2"

    (* Narrowing vector shifts intrinsics *)

    (* 32 -> 16 *)
    | Cast (IntVector(16, 4), Bop (Div, x, Broadcast(IntImm y, w))) when is_power_of_two y ->
        let intrin = declare_function "llvm.arm.neon.vshiftn.v4i16"
          (function_type (i16x4_t) [|i32x4_t; i32x4_t|]) m in
        let arg1 = cg_expr x in
        let shift = const_int i32_t (-(log2 y)) in
        let arg2 = const_vector [|shift; shift; shift; shift|] in
        build_call intrin [|arg1; arg2|] "" b                                      
        
    (* Non-narrowing vector shift  *)
    (* 
    | Bop (Div, x, Broadcast(IntImm y, w)) when is_power_of_two y ->
          *)

        

    (* Other integer types ... 
    | Bop (Div, x, Broadcast(Cast(Int 16, IntImm y), w)) when is_power_of_two y ->
    *)
        
            
            (* 
    | Load (IntVector (16, 4), buf, Ramp (base, IntImm 2, w)) 
    | Load (UIntVector (16, 4), buf, Ramp (base, IntImm 2, w)) ->
        let ld = declare_function "llvm.arm.neon.vld2.v4i16"
          (function_type (i16x4x2_t) [|ptr_t; i32_t|]) m in
        let addr = con.cg_memref (IntVector (16, 4)) buf base in        
        let addr = build_pointercast addr ptr_t "" b in
        let result = build_call ld [|addr; const_int (i32_t) 16|] "" b in
        build_extractvalue result 0 "" b 
            *)

    (* TODO: generalize strided loads to other types *)

          
    (* 

    (* absolute difference via the pattern x > y ? x - y : y - x *)
    | Select (Cmp (LT, x, y), Bop (Sub, y1, x1), Bop (Sub, x2, y2)) 
    | Select (Cmp (LE, x, y), Bop (Sub, y1, x1), Bop (Sub, x2, y2)) 
    | Select (Cmp (GT, x, y), Bop (Sub, x1, y1), Bop (Sub, y2, x2)) 
    | Select (Cmp (GE, x, y), Bop (Sub, x1, y1), Bop (Sub, y2, x2)) 
        when is_vector x && x = x1 && x = x2 && y = y1 && y = y2 ->
       TODO
    *)

     
    (* absolute value via the pattern (x > 0 ? x : 0-x) or similar *)
    | Select (Cmp (LT, x, zero), Bop (Sub, zero1, x1), x2) 
    | Select (Cmp (LE, x, zero), Bop (Sub, zero1, x1), x2) 
    | Select (Cmp (GT, x, zero), x1, Bop (Sub, zero1, x2)) 
    | Select (Cmp (GE, x, zero), x1, Bop (Sub, zero1, x2)) 
        when is_vector x && x = x1 && x = x2 && zero = zero1 &&
          zero = make_zero (val_type_of_expr x) ->
        (* May additionally be an absolute difference *)
        begin match x with
          | Bop (Sub, l, r) -> 
              (* abd *)
              begin match (val_type_of_expr l) with
                | IntVector (16, 8) ->
                    let op = declare_function "llvm.arm.neon.vabds.v8i16"
                      (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in              
                    build_call op [| cg_expr l; cg_expr r |] "" b
                (* TODO: other types *)
                | _ -> con.cg_expr expr
              end
              
          | _ -> 
              (* abs *)
              begin match (val_type_of_expr x) with
                | IntVector (16, 8) ->
                    let op = declare_function "llvm.arm.neon.vabs.v8i16"
                      (function_type (i16x8_t) [|i16x8_t|]) m in              
                    build_call op [| cg_expr x |] "" b
                (* TODO: other types *)
                | _ -> con.cg_expr expr
              end                                
        end
 
    (* halving add for integers *)
    | Bop (Div, Bop (Add, l, r), two)
        when two = make_const (val_type_of_expr l) 2 ->
        begin match (val_type_of_expr l) with
          | IntVector (16, 8) ->
              let op = declare_function "llvm.arm.neon.vhadds.v8i16"
                (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          (* TODO: other types *)
          | _ -> con.cg_expr expr
        end
      
    (* 
    (* Broadcast using vdup? *)
    | Broadcast (x, n) ->        

    (* TODO: halving subtract? *)

    (* TODO: fused multiply-accumulate? *)

    (* TODO: absolute difference and accumulate? *)
    
    *)

    (* Vector interleave: TODO: port this to x86 as well *)
    | Select (Cmp (EQ, Bop (Mod, Ramp (x, one, w), two), zero), l, r) 
        when ((Analysis.reduce_expr_modulo x 2 = Some 0) &&
              (duplicated_lanes l) &&
              (duplicated_lanes r) &&
              (zero = make_const (vector_of_val_type (val_type_of_expr x) w) 0) &&
              (one = make_const (val_type_of_expr x) 1) &&
              (two = make_const (vector_of_val_type (val_type_of_expr x) w) 2)) ->
        let l = deduplicate_lanes l in
        let r = deduplicate_lanes r in
        let l = Constant_fold.constant_fold_expr l in
        let r = Constant_fold.constant_fold_expr r in
        let l = cg_expr l in
        let r = cg_expr r in        
        let rec gen_mask w = function
          | 0 -> []
          | n -> (gen_mask w (n-1)) @ [const_int i32_t (n-1); const_int i32_t (n+w-1)] in
        let mask = const_vector (Array.of_list (gen_mask (w/2) (w/2))) in
        build_shufflevector l r mask "" b 
          
    | _ -> con.cg_expr expr

exception DeinterleaveFailed

let rec cg_stmt (con : context) (stmt : stmt) =
  let c = con.c and b = con.b and m = con.m in
  let cg_expr e = cg_expr con e in
  (* let cg_stmt s = cg_stmt con s in *)

  let ptr_t = pointer_type (i8_type c) in
  let i32_t = i32_type c in
  (* let i16x8_t = vector_type (i16_type c) 8 in *)
  let i8x16_t = vector_type (i8_type c) 16 in
  let i16x4_t = vector_type (i16_type c) 4 in

  (* Arm has an interleaving store, which can be important for
     performance. It's a pain to detect when we should use this *)
  let rec try_deinterleave expr = 
    let recurse = try_deinterleave in
    Printf.printf "try_deinterleave pondering %s\n%!" (string_of_expr expr);
    match expr with
      | Select (Cmp (EQ, Bop(Mod, Ramp (base, IntImm 1, _), Broadcast (IntImm 2, _)), Broadcast (IntImm 0, _)), l, r) 
          when Analysis.reduce_expr_modulo base 2 = Some 0 ->
          (* This expression is why we're here - it was flagged by should_deinterleave *)
          let (ll, _) = recurse l 
          and (_, rr) = recurse r in
          (ll, rr)              

      | Select (c, a, b) ->
          let (cl, cr) = recurse c 
          and (al, ar) = recurse a
          and (bl, br) = recurse b in
          (Select (cl, al, bl), Select (cr, ar, br))
      (* This occurs in the interleave pattern we expect the front-end to provide *)
      | Bop (Div, Ramp (base, IntImm 1, w), Broadcast (IntImm 2, _)) ->
          (Ramp (base /~ (IntImm 2), IntImm 1, w/2),
           Ramp (base /~ (IntImm 2), IntImm 1, w/2))
      | Ramp (base, stride, w) when w mod 2 = 0 ->
          (Ramp (base, stride *~ (IntImm 2), w/2),
           Ramp (base +~ stride, stride *~ (IntImm 2), w/2))
      | Broadcast (expr, w) when w mod 2 = 0 ->
          (Broadcast (expr, w/2), Broadcast (expr, w/2))
      | Cmp (op, l, r) -> 
          let (ll, lr) = recurse l and (rl, rr) = recurse r in
          (Cmp (op, ll, rl), Cmp (op, lr, rr))
      | Bop (op, l, r) -> 
          let (ll, lr) = recurse l and (rl, rr) = recurse r in
          (Bop (op, ll, rl), Bop (op, lr, rr))
      | And (l, r) -> 
          let (ll, lr) = recurse l and (rl, rr) = recurse r in
          (And (ll, rl), And (lr, rr))
      | Or (l, r) -> 
          let (ll, lr) = recurse l and (rl, rr) = recurse r in
          (Or (ll, rl), Or (lr, rr))
      | Not e -> 
          let (l, r) = recurse e in
          (Not l, Not r)
      | Let (n, l, r) when vector_elements (val_type_of_expr l) = 1 ->
          let (rl, rr) = recurse r in
          (Let (n, l, rl), Let (n, l, rr))
      | Load (t, buf, idx) when (vector_elements t) mod 2 = 0 ->
          let w = vector_elements t in
          let newt = vector_of_val_type (element_val_type t) (w/2) in
          let (il, ir) = recurse idx in
          (Load (newt, buf, il), Load (newt, buf, ir))
      | Cast (t, e) when (vector_elements t) mod 2 = 0->
          let w = vector_elements t in
          let newt = vector_of_val_type (element_val_type t) (w/2) in          
          let (l, r) = recurse e in
          (Cast (newt, l), Cast (newt, r))
      (* TODO: | MakeVector l ->  *)
          
      | Debug (e, str, args) -> 
          let (l, r) = recurse e in
          (Debug (l, str, args), Debug (r, str, args))
      (* Barf on anything else *)
      | _ -> raise DeinterleaveFailed
  in

  let rec should_deinterleave = function
    | Select (Cmp (EQ, Bop(Mod, Ramp (base, IntImm 1, _), Broadcast (IntImm 2, _)), Broadcast (IntImm 0, _)), _, _) -> 
        Analysis.reduce_expr_modulo base 2 = Some 0
    | expr -> fold_children_in_expr should_deinterleave (||) false expr
  in

  let cg_dense_store e buf base w =
    if (bit_width (val_type_of_expr e) = 128) then begin
        let alignment = match (Analysis.reduce_expr_modulo base w, w) with
          (* 16-byte-aligned *)
          | (Some 0, _) -> 16 
          (* 8-byte-aligned cases *)
          | (Some 1, 2) 
          | (Some 2, 4) 
          | (Some 4, 8) 
          | (Some 8, 16) -> 8 
          (* 4-byte-aligned cases *)
          | (Some _, 4) -> 4
          | (Some x, 8) when x mod 2 = 0 -> 4
          | (Some x, 16) when x mod 4 = 0 -> 4
          (* 2-byte-aligned cases *)
          | (Some _, 8) -> 2
          | (Some x, 16) when x mod 2 = 0 -> 2
          (* byte-aligned *)
          | _ -> 1
        in
        let st = declare_function "llvm.arm.neon.vst1.v16i8"
          (function_type (void_type c) [|ptr_t; i8x16_t; i32_t|]) m in 
        let addr = con.cg_memref (val_type_of_expr e) buf base in        
        let addr = build_pointercast addr ptr_t "" b in
        let value = build_bitcast (cg_expr e) i8x16_t "" b in
        build_call st [|addr; value; const_int (i32_t) alignment|] "" b         
    end else con.cg_stmt (Store (e, buf, Ramp (base, IntImm 1, w))) 
  in

  match stmt with
    | Store (e, buf, Ramp (base, IntImm 1, w)) ->
        if (should_deinterleave e) then begin
          (* Printf.printf "Considering deinterleaving %s\n" (string_of_expr e); *)
          let result = try (Some (try_deinterleave e)) with DeinterleaveFailed -> None in
          begin match (result, val_type_of_expr e, Analysis.reduce_expr_modulo base 2) with
            | (Some (l, r), UIntVector (16, 8), Some 0) 
            | (Some (l, r), IntVector (16, 8), Some 0)  -> 
                let l = Constant_fold.constant_fold_expr l in
                let r = Constant_fold.constant_fold_expr r in          
                (* Printf.printf "Let's do it!\n%!"; *)
                (* Printf.printf "Resulting children: %s %s\n" (string_of_expr l) (string_of_expr r); *)
                let l = cg_expr l in
                let r = cg_expr r in
                let st = declare_function "llvm.arm.neon.vst2.v4i16"
                  (function_type (void_type c) [|ptr_t; i16x4_t; i16x4_t; i32_t|]) m in 
                let addr = con.cg_memref (IntVector (16, 8)) buf base in        
                let addr = build_pointercast addr ptr_t "" b in
                build_call st [|addr; l; r; const_int (i32_t) 2|] "" b               
            | (None, _, _) -> 
                (* Printf.printf "Not doing it because deinterleave failed\n"; *)
                cg_dense_store e buf base w
            | (_, _, Some 0) ->
                (* Printf.printf "Not doing it because type didn't match a known vst2\n"; *)
                cg_dense_store e buf base w        
            | (_, _, _) ->
                (* Printf.printf "Not doing it because base of write not a multiple of two\n"; *)
                cg_dense_store e buf base w
          end 
        end else cg_dense_store e buf base w
    | _ -> con.cg_stmt stmt


(* Same as X86 code for malloc / free. TODO: make a posix/cpu arch module? *)
let free (con : context) (name:string) (address:llvalue) =
  let c = con.c and b = con.b and m = con.m in
  let free = declare_function "fast_free" (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
  ignore (build_call free [|address|] "" b)

let malloc (con : context) (name : string) (elems : expr) (elem_size : expr) =
  let c = con.c and b = con.b and m = con.m in
  let size = Constant_fold.constant_fold_expr (Cast (Int 32, elems *~ elem_size)) in
  match size with
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
