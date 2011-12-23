open Ir
open Llvm

let codegen_entry c m cg_entry e =
  (* set up module *)
  Stdlib.init_module_x86 m;

  let f = cg_entry c m e in

  (* TODO: build wrappers *)

  f

let cg_expr (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =

  (* let i8x16_t = vector_type (i8_type c) 16 in *)

  (* let paddb_16 = declare_function "paddb_16"
    (function_type (i8x16_t) [|i8x16_t; i8x16_t|]) m in *)

  let i16x8_t = vector_type (i16_type c) 8 in
  let pmulhw = declare_function "llvm.x86.sse2.pmulh.w"
    (function_type (i16x8_t) [|i16x8_t; i16x8_t|]) m in

  match expr with 
    (* x86 doesn't do 16 bit vector division, but for constants you can do multiplication instead. *)
    | Bop (Div, x, Broadcast (Cast (UInt 16, IntImm y), 8)) ->
        Printf.printf "MOO!\n%!";
        let z = (65536/y + 1) in
        let lhs = cg_expr x in
        let rhs = cg_expr (Broadcast (Cast (UInt 16, IntImm z), 8)) in        
        build_call pmulhw [|lhs; rhs|] "" b

    (* We don't have any special tricks up our sleeve for this case *)
    | _ -> cg_expr expr 

let cg_stmt (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let malloc (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (size : expr) =
  let malloc = declare_function "safe_malloc" (function_type (pointer_type (i8_type c)) [|i64_type c|]) m in  
  build_call malloc [|cg_expr (Cast (Int 64, size))|] "" b 

let free (c:llcontext) (m:llmodule) (b:llbuilder) (address:llvalue) =
  let free = declare_function "safe_free" (function_type (void_type c) [|pointer_type (i8_type c)|]) m in
  build_call free [|address|] "" b   

let env = Environment.empty
