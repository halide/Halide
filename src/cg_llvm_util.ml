open Llvm
open Ir
open Util

exception UnsupportedType of val_type
exception CGFailed of string
exception BCWriteFailed of string

type cg_context = {
  c : llcontext;
  m : llmodule;
  b : llbuilder;
  cg_expr : expr -> llvalue;
  cg_stmt : stmt -> llvalue;  
  cg_memref : val_type -> string -> expr -> llvalue;
  sym_get : string -> llvalue;
}

let raw_buffer_t c = pointer_type (i8_type c)

let verify_cg m =
    (* verify the generated module *)
    match Llvm_analysis.verify_module m with
      | Some reason -> raise(CGFailed(reason))
      | None -> ()

let type_of_val_type c t = match t with
  | UInt(1) | Int(1) -> i1_type c
  | UInt(8) | Int(8) -> i8_type c
  | UInt(16) | Int(16) -> i16_type c
  | UInt(32) | Int(32) -> i32_type c
  | UInt(64) | Int(64) -> i64_type c
  | Float(32) -> float_type c
  | Float(64) -> double_type c
  | IntVector( 1, n) | UIntVector( 1, n) -> vector_type (i1_type c) n
  | IntVector( 8, n) | UIntVector( 8, n) -> vector_type (i8_type c) n
  | IntVector(16, n) | UIntVector(16, n) -> vector_type (i16_type c) n
  | IntVector(32, n) | UIntVector(32, n) -> vector_type (i32_type c) n
  | IntVector(64, n) | UIntVector(64, n) -> vector_type (i64_type c) n
  | FloatVector(32, n) -> vector_type (float_type c) n
  | FloatVector(64, n) -> vector_type (double_type c) n
  | _ -> raise (UnsupportedType(t))


(* get references to the support functions *)
let get f name = match f name with
  | Some v -> v
  | None -> raise (Wtf ("Couldn't find " ^ name ^ " in module"))

let get_function m = get (fun nm -> lookup_function nm m)

let get_type m = get (type_by_name m)

let buffer_t m =
  get_type m "struct.buffer_t"

(*
 * Build wrapper
 *)
let cg_wrapper c m e inner =
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
  
  let args = (List.combine arglist (Array.to_list (params f))) in

  Printf.printf "Building wrapper\n%!";
  Printf.printf "  %d args\n" (List.length arglist);
  Printf.printf "  %d params\n%!" (Array.length (params f));
  Printf.printf "  %d inner params\n%!" (Array.length (params inner));

  let toi32 x = build_intcast x (i32_type c) "" b in

  let c x = const_int (i32_type c) x in

  let make_call_arg = function
    | Buffer nm, param ->
        [build_load (build_gep param [|c 0; c 0|] (nm ^ ".host") b) "" b;
         toi32 (build_load (build_gep param [|c 0; c 4; c 0|] (nm ^ ".dim.0") b) "" b);
         toi32 (build_load (build_gep param [|c 0; c 4; c 1|] (nm ^ ".dim.1") b) "" b);
         toi32 (build_load (build_gep param [|c 0; c 4; c 2|] (nm ^ ".dim.2") b) "" b);
         toi32 (build_load (build_gep param [|c 0; c 4; c 3|] (nm ^ ".dim.3") b) "" b)]
    | _, param -> [param]
  in
  
  let call_args = Array.of_list (List.flatten (List.map make_call_arg args)) in

  ignore (build_call inner call_args "" b);
  ignore (build_ret_void b);

  f
