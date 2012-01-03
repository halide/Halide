open Ir
open Llvm
open Cg_llvm_util

type state = int (* dummy - we don't use anything in this Arch for now *)
type context = state cg_context
let start_state () = 0

let codegen_entry c m cg_entry e =
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

    (* We don't have any special tricks up our sleeve for this case *)
    | _ -> con.cg_expr expr 
        
let rec cg_stmt (con:context) (stmt:stmt) =
  let c = con.c and m = con.m and b = con.b in
  let cg_expr = cg_expr con in
  let cg_stmt = cg_stmt con in
  let ptr_t = pointer_type (i8_type c) in
  let i8x16_t = vector_type (i8_type c) 16 in

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
    | _ -> con.cg_stmt stmt

let malloc (con:context) (size:expr) = 
  let c = con.c and m = con.m and b = con.b in  
  let malloc = declare_function "malloc" (function_type (pointer_type (i8_type c)) [|i64_type c|]) m in  
  build_call malloc [|cg_expr con (Cast (Int 64, size))|] "" b 

let free (con:context) (address:llvalue) =
  let c = con.c and m = con.m and b = con.b in
  let free = declare_function "free" (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
  build_call free [|address|] "" b   

let env = Environment.empty
  
