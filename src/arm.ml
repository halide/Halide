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
  let i8x16_t = vector_type (i8_type c) 16 in
  let i16x8_t = vector_type (i16_type c) 8 in
  let f32x4_t = vector_type (float_type c) 4 in
  let i16x8x2_t = struct_type c [| i16x8_t; i16x8_t |] in
  let i16x4_t = vector_type (i16_type c) 4 in
  let i16x4x2_t = struct_type c [| i16x4_t; i16x4_t |] in

  let cg_expr e = cg_expr con e in

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
  
  match expr with 
    | Bop (Min, l, r) when is_vector l -> 
        begin match (val_type_of_expr l) with
          | IntVector (16, 8) ->
              let op = declare_function "llvm.arm.neon.vmins.v8i16"
                (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          | FloatVector (32, 4) ->
              let op = declare_function "llvm.arm.neon.vmins.v4f32"
                (function_type (f32x4_t) [|f32x4_t; f32x4_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          (* TODO: other types *)
          | _ -> cg_select (l <~ r) l r
        end
    | Bop (Max, l, r) when is_vector l -> 
        begin match (val_type_of_expr l) with
          | IntVector (16, 8) ->
              let op = declare_function "llvm.arm.neon.vmaxs.v8i16"
                (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          | FloatVector (32, 4) ->
              let op = declare_function "llvm.arm.neon.vmaxs.v4f32"
                (function_type (f32x4_t) [|f32x4_t; f32x4_t|]) m in              
              build_call op [| cg_expr l; cg_expr r |] "" b
          (* TODO: other types *)
          | _ -> cg_select (l >~ r) l r
        end

    (* use intrinsics for vector loads/stores *) 
    | Load (IntVector (16, 8), buf, Ramp (base, IntImm 1, w)) 
    | Load (UIntVector (16, 8), buf, Ramp (base, IntImm 1, w)) ->
        let ld = declare_function "llvm.arm.neon.vld1.v8i16"
          (function_type (i16x8_t) [|ptr_t; i32_t|]) m in
        let addr = con.cg_memref (IntVector (16, 8)) buf base in        
        let addr = build_pointercast addr ptr_t "" b in
        let result = build_call ld [|addr; const_int (i32_t) 16|] "" b in
        result

    | Load (IntVector (16, 4), buf, Ramp (base, IntImm 1, w)) 
    | Load (UIntVector (16, 4), buf, Ramp (base, IntImm 1, w)) ->
        let ld = declare_function "llvm.arm.neon.vld1.v4i16"
          (function_type (i16x4_t) [|ptr_t; i32_t|]) m in
        let addr = con.cg_memref (IntVector (16, 4)) buf base in        
        let addr = build_pointercast addr ptr_t "" b in
        let result = build_call ld [|addr; const_int (i32_t) 16|] "" b in
        result

    | Load (IntVector (16, 8), buf, Ramp (base, IntImm 2, w)) 
    | Load (UIntVector (16, 8), buf, Ramp (base, IntImm 2, w)) ->
        let ld = declare_function "llvm.arm.neon.vld2.v8i16"
          (function_type (i16x8x2_t) [|ptr_t; i32_t|]) m in
        let addr = con.cg_memref (IntVector (16, 8)) buf base in        
        let addr = build_pointercast addr ptr_t "" b in
        let result = build_call ld [|addr; const_int (i32_t) 16|] "" b in
        build_extractvalue result 0 "" b 

    | Load (IntVector (16, 4), buf, Ramp (base, IntImm 2, w)) 
    | Load (UIntVector (16, 4), buf, Ramp (base, IntImm 2, w)) ->
        let ld = declare_function "llvm.arm.neon.vld2.v4i16"
          (function_type (i16x4x2_t) [|ptr_t; i32_t|]) m in
        let addr = con.cg_memref (IntVector (16, 4)) buf base in        
        let addr = build_pointercast addr ptr_t "" b in
        let result = build_call ld [|addr; const_int (i32_t) 16|] "" b in
        build_extractvalue result 0 "" b 



    (* 

    (* absolute difference via the pattern x > y ? x - y : y - x *)
    | Select (Cmp (LT, x, y), Bop (Sub, y1, x1), Bop (Sub, x2, y2)) 
    | Select (Cmp (LE, x, y), Bop (Sub, y1, x1), Bop (Sub, x2, y2)) 
    | Select (Cmp (GT, x, y), Bop (Sub, x1, y1), Bop (Sub, y2, x2)) 
    | Select (Cmp (GE, x, y), Bop (Sub, x1, y1), Bop (Sub, y2, x2)) 
        when is_vector x && x = x1 && x = x2 && y = y1 && y = y2 ->
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
    (* Broadcast using vdup *)
    | Broadcast (x, n) ->        

    (* TODO: halving subtract? *)

    (* TODO: fused multiply-accumulate? *)

    (* TODO: absolute difference and accumulate? *)
    
    *)



          
    (* vector select doesn't work right on arm yet *)          
    | Select (cond, thenCase, elseCase) when is_vector cond -> 
        cg_select cond thenCase elseCase

    | _ -> con.cg_expr expr

let rec cg_stmt (con : context) (stmt : stmt) =
  let c = con.c and b = con.b and m = con.m in
  let cg_expr e = cg_expr con e in
  let cg_stmt s = cg_stmt con s in

  let ptr_t = pointer_type (i8_type c) in
  let i32_t = i32_type c in
  let i16x8_t = vector_type (i16_type c) 8 in
  let i8x16_t = vector_type (i8_type c) 16 in
  let i16x4_t = vector_type (i16_type c) 4 in

  let rec duplicated_lanes expr = match expr with
    | Broadcast _ -> true
    | Bop (Div, Ramp (base, one, n), two) 
        when (one = make_const (val_type_of_expr base) 1 &&
            two = make_const (val_type_of_expr expr) 2) -> true
    | MakeVector _ -> false
    | Ramp _ -> false
    | expr -> fold_children_in_expr duplicated_lanes (&&) true expr
  in
  
  let rec deduplicate_lanes expr = match expr with
    | Broadcast (e, n) -> Broadcast (e, n/2)
    | Bop (Div, Ramp (base, one, n), two) 
        when (one = make_const (val_type_of_expr base) 1 &&
            two = make_const (val_type_of_expr expr) 2) -> 
        Ramp (base /~ (make_const (val_type_of_expr base) 2), one, n/2)
    | Load (t, buf, idx) when is_vector idx ->
        Load (vector_of_val_type (element_val_type t) ((vector_elements t)/2), 
              buf, deduplicate_lanes idx)
    | MakeVector _ -> raise (Wtf "Can't deduplicate the lanes of a MakeVector")
    | Ramp _ -> raise (Wtf "Can't deduplicate the lanes of a generic Ramp")
    | expr -> mutate_children_in_expr deduplicate_lanes expr
  in

  match stmt with
    (* Look for the vector interleaved store pattern. It should be
       expressed like so: out(x) = Select(x%2==0, a, b) where a and b
       don't depend on (x%2) (e.g. they contain only x/2) *)
    | Store (Select (Cmp (EQ, Bop (Mod, Ramp (x, one, w), two), zero), l, r), buf, Ramp (base, one1, _)) 
        when (duplicated_lanes l &&
              duplicated_lanes r &&
              zero = make_const (vector_of_val_type (val_type_of_expr x) w) 0 &&
              one = make_const (val_type_of_expr x) 1 && 
              one1 = make_const (val_type_of_expr base) 1 &&
              two = make_const (vector_of_val_type (val_type_of_expr x) w) 2) ->
        
        let l = deduplicate_lanes l in
        let r = deduplicate_lanes r in
        let l = Constant_fold.constant_fold_expr l in
        let r = Constant_fold.constant_fold_expr r in
        let t = val_type_of_expr l in
        let l = cg_expr l in
        let r = cg_expr r in

        begin match t with 
          | IntVector (16, 4) 
          | UIntVector (16, 4) ->
              let st = declare_function "llvm.arm.neon.vst2.v4i16"
                (function_type (void_type c) [|ptr_t; i16x4_t; i16x4_t; i32_t|]) m in 
              let addr = con.cg_memref (IntVector (16, 8)) buf base in        
              let addr = build_pointercast addr ptr_t "" b in
              build_call st [|addr; l; r; const_int (i32_t) 2|] "" b 
          | _ -> con.cg_stmt stmt (* TODO: more types *)
        end

    (* 128-bit dense stores should always use vst1 *)
    | Store (e, buf, Ramp(base, IntImm 1, w)) when (bit_width (val_type_of_expr e) = 128) ->
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
        let addr = con.cg_memref (IntVector (8, 16)) buf base in        
        let addr = build_pointercast addr ptr_t "" b in
        let value = build_bitcast (cg_expr e) i8x16_t "" b in
        build_call st [|addr; value; const_int (i32_t) alignment|] "" b         

        
        

    | _ -> con.cg_stmt stmt


let malloc (con : context) (name : string) (elems : expr) (elem_size : expr) =
  let c = con.c and b = con.b and m = con.m in
  let malloc = declare_function "malloc" (function_type (pointer_type (i8_type c)) [|i64_type c|]) m in  
  build_call malloc [|con.cg_expr (Cast (Int 64, elems *~ elem_size))|] name b

let free (con : context) (address:llvalue) =
  let c = con.c and b = con.b and m = con.m in
  let free = declare_function "free" (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
  build_call free [|address|] "" b   

let env = Environment.empty
