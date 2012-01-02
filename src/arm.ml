open Llvm
open Ir
open Util
open Cg_llvm_util

let codegen_entry c m cg_entry e =
  (* set up module *)
  Stdlib.init_module_arm m;

  (* build the inner kernel, which takes raw byte*s *)
  let inner = cg_entry c m e in

  (* return the wrapper which takes buffer_t*s *)
  cg_wrapper c m e inner

let rec cg_expr (con : cg_context) (expr : expr) =
  let c = con.c and b = con.b and m = con.m in
  let ptr_t = pointer_type (i8_type c) in
  let i32_t = i32_type c in
  let i16x8_t = vector_type (i16_type c) 8 in
  let i16x8x2_t = struct_type c [| i16x8_t; i16x8_t |] in

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
    build_or (build_and mask l "" b) (build_and inv_mask r "" b) "" b
  in    

  match expr with 
    (* vector select doesn't work right on arm yet *)
    | Bop (Min, l, r) when is_vector l -> 
        begin match (val_type_of_expr l) with
          | IntVector (16, 8) ->
              let op = declare_function "llvm.arm.neon.vmins.v8i16"
                (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in              
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
          (* TODO: other types *)
          | _ -> cg_select (l >~ r) l r
        end

    (* intrinsics for strided vector loads/stores *)          

    | Load (IntVector (16, 8), buf, Ramp (base, IntImm 2, w)) 
    | Load (UIntVector (16, 8), buf, Ramp (base, IntImm 2, w)) ->
        let ld = declare_function "llvm.arm.neon.vld2.v8i16"
          (function_type (i16x8x2_t) [|ptr_t; i32_t|]) m in
        let addr = con.cg_memref (IntVector (16, 8)) buf base in        
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
    | Select (cond, thenCase, elseCase) when is_vector cond -> 
        cg_select cond thenCase elseCase

    | _ -> con.cg_expr expr

let rec cg_stmt (con : cg_context) (stmt : stmt) =
  let c = con.c and b = con.b and m = con.m in
  let cg_expr e = cg_expr con e in
  let cg_stmt s = cg_stmt con s in
  match stmt with
    | _ -> con.cg_stmt stmt


let malloc (con : cg_context) (size : expr) =
  let c = con.c and b = con.b and m = con.m in
  let malloc = declare_function "malloc" (function_type (pointer_type (i8_type c)) [|i64_type c|]) m in  
  build_call malloc [|con.cg_expr (Cast (Int 64, size))|] "" b 

let free (con : cg_context) (address:llvalue) =
  let c = con.c and b = con.b and m = con.m in
  let free = declare_function "free" (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
  build_call free [|address|] "" b   

let env = Environment.empty
