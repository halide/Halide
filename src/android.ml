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

let target_triple = "arm-linux-android-eabi"

let cg_entry c m codegen_entry _ e opts =
  (* set up module *)
  Stdlib.init_module_android m;

  (* build the inner kernel, which takes raw byte*s *)
  let inner = codegen_entry opts c m e in

  (* return the wrapper which takes buffer_t*s *)
  cg_wrapper c m e inner

let rec cg_expr (con : context) (expr : expr) =
  Arm.cg_expr con expr

let rec cg_stmt (con : context) (stmt : stmt) =
  let c = con.c and b = con.b and m = con.m in
  let int_imm_t = i32_type c in
  match stmt with 
    | Print (prefix, args) ->
        (* Generate a format string and values to print for a printf *)
        let rec fmt_string x = match val_type_of_expr x with
          | Int _ -> ("%d", [Cast (i32, x)])
          | Float _ -> ("%3.3f", [Cast (f64, x)])
          | UInt _ -> ("%u", [Cast (u32, x)])
          | IntVector (_, n) | UIntVector (_, n) | FloatVector (_, n) -> 
              let elements = List.map (fun idx -> ExtractElement (x, IntImm idx)) (0 -- n) in
              let subformats = List.map fmt_string elements in
              ("[" ^ (String.concat ", " (List.map fst subformats)) ^ "]",
               List.concat (List.map snd subformats))
        in
        
        let fmts = List.map fmt_string args in
        let fmt = String.concat " " (List.map fst fmts) in
        let args = List.concat (List.map snd fmts) in

        let ll_fmt = const_stringz c (prefix ^ fmt ^ "\n") in    
        let ll_args = List.map (cg_expr con) args in
        
        let global_fmt = define_global "debug_fmt" ll_fmt m in
        set_linkage Llvm.Linkage.Internal global_fmt;
        let global_fmt = build_pointercast global_fmt (pointer_type (i8_type c)) "" b in

        let halide_tag = const_stringz c "halide" in
        let halide_tag = define_global "halide_tag" halide_tag m in
        set_linkage Llvm.Linkage.Internal halide_tag;
        let halide_tag = build_pointercast halide_tag (pointer_type (i8_type c)) "" b in

        let ll_printf = declare_function "__android_log_print" 
          (var_arg_function_type (i32_type c) [|int_imm_t; pointer_type (i8_type c); pointer_type (i8_type c)|]) m in
        let level = const_int int_imm_t 3 in (* 3 == debug in android *)
        build_call ll_printf (Array.of_list (level::halide_tag::global_fmt::ll_args)) "" b 
        
    | stmt -> Arm.cg_stmt con stmt

(* Same as X86 code for malloc / free. TODO: make a posix/cpu arch module? *)
let free (con : context) (name:string) (address:llvalue) =
  Arm.free con name address

let malloc (con : context) (name : string) (elems : expr) (elem_size : expr) =
  Arm.malloc con name elems elem_size

let env = Environment.empty
