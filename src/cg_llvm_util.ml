open Llvm
open Ir
open Util

exception UnsupportedType of val_type
exception CGFailed of string
exception BCWriteFailed of string

let raw_buffer_t c = pointer_type (i8_type c)

let verify_cg m =
    (* verify the generated module *)
    match Llvm_analysis.verify_module m with
      | Some reason -> raise(CGFailed(reason))
      | None -> ()

type 'a cg_context = {
  c : llcontext;
  m : llmodule;
  b : llbuilder;
  cg_expr : expr -> llvalue;
  cg_stmt : stmt -> llvalue;
  cg_memref : val_type -> string -> expr -> llvalue;
  sym_get : string -> llvalue;
  sym_add : string -> llvalue -> unit;
  sym_remove : string -> unit;
  dump_syms : unit -> unit;
  arch_state : 'a;
}

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

let dump_syms symtab =
  Printf.eprintf "dump_syms:\n%!";
  Hashtbl.iter (fun k _ -> Printf.eprintf "  %s\n%!" k) symtab

(* misc helpers *)
let context_of_val v = type_context (type_of v)
let toi32 x b = build_intcast x (i32_type (context_of_val x)) "" b
let ci c x = const_int (i32_type c) x
let param_list f = Array.to_list (params f)

(* codegen an llvalue which loads buf->dim[i] *)
let cg_buffer_dim bufptr dim b =
  let ci = ci (context_of_val bufptr) in
  toi32 (build_load (build_gep
                       bufptr
                       [|ci 0; ci 4; ci dim|]
                       ((value_name bufptr) ^ ".dim." ^ string_of_int dim) b) "" b) b

(* codegen an llvalue which loads buf->host *)
let cg_buffer_host_ptr bufptr b =
  let ci = ci (context_of_val bufptr) in
  build_load (build_gep bufptr [|ci 0; ci 0|] ((value_name bufptr) ^ ".host") b) "" b

(* map an Ir.arg to an ordered list of types for its constituent Var parts *)
let types_of_arg_vars c = function
  | Scalar (_, vt) -> [type_of_val_type c vt]
  | Buffer _ -> [raw_buffer_t c; i32_type c; i32_type c; i32_type c; i32_type c]

(* map an Ir.arg to an ordered list of names for its constituent Var parts *)
let names_of_arg_vars = function
  | Scalar (n, _) -> [n]
  | Buffer n -> [n; n ^ ".dim.0"; n ^ ".dim.1"; n ^ ".dim.2"; n ^ ".dim.3"] 

let arg_var_types c arglist = List.flatten (List.map (types_of_arg_vars c) arglist)
let arg_var_names arglist = List.flatten (List.map names_of_arg_vars arglist)

(* map an (Ir.arg, llvalue) pair to an ordered list of values for the 
 * exploded arg Var values *)
let vals_of_arg_vars b = function
  | Buffer _, param ->
      [cg_buffer_host_ptr param b;
       cg_buffer_dim param 0 b;
       cg_buffer_dim param 1 b;
       cg_buffer_dim param 2 b;
       cg_buffer_dim param 3 b]
  | _, param -> [param]

(*
 * return an ordered list of llvalues for the exploded arg Var values in arglist
 *
 * NOTE: Requires that builder `b` be in the function whose params we want
 *       to map to values!
 *)
let arg_var_vals arglist b =
  let f = block_parent (insertion_block b) in
  let args_params = (List.combine arglist (param_list f)) in
  List.flatten (List.map (vals_of_arg_vars b) args_params)

(* initialize a symbol table from an entrypoint arglist and a matching list of llvalues *)
let arg_symtab arglist argvals =
  let symtab = Hashtbl.create 10 in
  List.iter
    (fun (n, v) -> Hashtbl.add symtab n v)
    (List.combine (arg_var_names arglist) argvals);
  symtab

(* set llvalue names on ordered list vals corresponding to (packed OR unpacked arglist) *)
let set_arg_names arglist vals =
  let names =
    if List.length arglist = List.length vals then
      (* assume we want wrapper names, not expanded var names *)
      List.map (function Scalar (n,_) -> n | Buffer n -> n) arglist
    else
      (* use the expanded var names *)
      arg_var_names arglist
  in
  assert (List.length names = List.length vals);
  List.iter
    (fun (n, v) -> set_value_name n v)
    (List.combine names vals)

let define_entry c m e =
  (* unpack the entrypoint *)
  let (entrypoint_name, arglist, _) = e in

  (* define `void main(arg1, arg2, ...)` entrypoint*)
  let f = define_function
            entrypoint_name
            (function_type (void_type c) (Array.of_list (arg_var_types c arglist)))
            m
  in
  
  (* set the llvalue names for the entry params *)
  set_arg_names arglist (param_list f);
  
  f

(* Mark each buffer arg as Noalias *)
let make_buffer_params_noalias f =
  iter_params
    (fun p ->
       if type_of p = (buffer_t (global_parent f)) then
         add_param_attr p Attribute.Noalias else ())
    f

(* define a buffer_t wrapper function for the signature in entrypoint `e` *)
let define_wrapper c m e =
  (* unpack entrypoint *)
  let (entrypoint_name, arglist, _) = e in

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

  f

(*
 * Build a buffer_t wrapper function
 *)
let cg_wrapper c m e inner =
  (* unpack entrypoint *)
  let (entrypoint_name, arglist, _) = e in

  (* rename the inner function *)
  ignore (set_value_name (entrypoint_name ^ "_inner") inner);

  (* define a buffer_t wrapper *)
  let f = define_wrapper c m e in
  let b = builder_at_end c (entry_block f) in

  Printf.printf "Building wrapper\n%!";
  Printf.printf "  %d args\n" (List.length arglist);
  Printf.printf "  %d params\n%!" (Array.length (params f));
  Printf.printf "  %d inner params\n%!" (Array.length (params inner));

  (* construct the argument list from the fields of the params of the wrapper *)
  let call_args = arg_var_vals arglist b in
  set_arg_names arglist call_args;

  (* call the inner function, and return void *)
  ignore (build_call inner (Array.of_list call_args) "" b);
  ignore (build_ret_void b);

  f
