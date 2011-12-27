open Ir
open Llvm
open Cg_llvm_util

let buffer_t m =
  get_type m "struct.buffer_t"

let codegen_entry c m cg_entry e =
  (* set up module *)
  Stdlib.init_module_x86 m;

  let inner = cg_entry c m e in

  (*
   * Build wrapper
   *)
  (* TODO: move this to Cg_llvm_util *)

  (* unpack entrypoint *)
  let (entrypoint_name, arglist, _) = e in

  (* rename the inner function *)
  ignore (set_value_name (entrypoint_name ^ "_inner") inner);

  let buffer_struct_type = buffer_t m in

  let type_of_arg = function
    | Scalar (_, vt) -> type_of_val_type c vt
    | Buffer _ -> pointer_type (buffer_struct_type)
  in

  let f = define_function
            entrypoint_name
            (function_type
               (void_type c)
               (Array.of_list (List.map type_of_arg arglist)))
            m in
  let b = builder_at_end c (entry_block f) in
  
  let args = Array.of_list
               (List.combine
                  arglist
                  (Array.to_list (params f))) in

  Printf.printf "Building wrapper\n%!";
  Printf.printf "  %d args\n" (List.length arglist);
  Printf.printf "  %d params\n%!" (Array.length (params f));
  Printf.printf "  %d inner params\n%!" (Array.length (params inner));

  ignore (
    build_call
      inner
      (Array.map
         begin function
           | Buffer nm, param ->
               build_load (build_struct_gep param 0 (nm ^ ".host") b) "" b
           | _, param -> param
         end
         args
      )
      ""
      b
  );
  ignore (build_ret_void b);

  Printf.printf "Built wrapper\n%!";
  dump_value f;

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
