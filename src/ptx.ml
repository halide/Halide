open Llvm
open Ir
open Ir_printer
open Util
open Cg_llvm_util
open Ptx_dev
open Analysis

let dbgprint = true
let dump_mod = dbgprint && false

(* Function calling conventions for PTX, from <llvm/CallingConv.h>
 * For whatever reason, the official OCaml binding only exposes a few
 * CallingConv constants. *)
let ptx_kernel = 71
let ptx_device = 72

(*
type alloc = {
  count : expr;
  elem_size : expr;
  buf : llvalue; (* buffer_t* which holds this allocation *)
}
 *)

type state = {
  host_state : X86.state;
  buf_add : string -> llvalue -> unit;
  buf_remove : string -> unit;
  buf_get : string -> llvalue;
  buf_get_name : llvalue -> string;
  buf_dump : unit -> unit;
  dev_mod : llmodule;
  dev_state : Ptx_dev.state;
  (*
  (* map raw buffer host byte pointers to alloc descriptors *)
  (* alloc_info : (llvalue, alloc) Hashtbl.t; *)
  (* map buffer names - Pipeline or input Buffer args - to raw buffer host byte pointers *)
  bufs : (string, llvalue) Hashtbl.t;
  (* map buffer_t*'s to their names *)
  buf_names : (llvalue, string) Hashtbl.t;
  (* map raw host byte pointers (e.g. returned by Ptx.malloc) to buffer_t*s *)
  hostptr_buf : (llvalue, llvalue) Hashtbl.t;
   *)
}
type context = state cg_context

let pointer_size = 8

let start_state () =
  (* create separate device module *)
  let dev_ctx = create_context () in
  let dev_mod = create_module dev_ctx "<halide-device-ptx>" in
  (* set up module template *)
  Stdlib.init_module_ptx_dev dev_mod;

  (* create state to track buffer_ts *)
  let bufs = Hashtbl.create 10 in
  let buf_names = Hashtbl.create 10 in
  let buf_add n v =
    set_value_name (n ^ "_buf") v;
    Hashtbl.add bufs n v;
    Hashtbl.add buf_names v n
  and buf_get n =
    try Hashtbl.find bufs n
    with Not_found -> raise (Wtf ("buffer " ^ n ^ " not found"))
  and buf_get_name v =
    try Hashtbl.find buf_names v
    with Not_found -> raise (Wtf ("buffer " ^ (value_name v) ^ " not found"))
  in
  let buf_remove n =
    let v = buf_get n in
    Hashtbl.remove bufs n;
    Hashtbl.remove buf_names v
  in
  let buf_dump () =
    Printf.eprintf "Ptx.state.buf_dump:\n%!";
    Hashtbl.iter (fun n v -> Printf.eprintf "  %s -> %!" n; dump_value v) bufs;
    Printf.eprintf "reverse map:\n%!";
    Hashtbl.iter (fun v n -> Printf.eprintf "  %!"; dump_value v; Printf.eprintf "    -> %s\n%!" n) buf_names;
  in
  
  {
    host_state = X86.start_state ();
    (* alloc_info = Hashtbl.create 10; *)
    buf_add = buf_add;
    buf_remove = buf_remove;
    buf_get = buf_get;
    buf_get_name = buf_get_name;
    buf_dump = buf_dump;
    dev_mod = dev_mod;
    dev_state = Ptx_dev.start_state ();
  }

(* Package up a context for the X86 codegen module.
 * The Cg_llvm closures (cg_expr, etc.) will still call back to *this* module,
 * with our own context, since they are closed over it at construction. *)
let host_context (con:context) = {
  c = con.c;
  m = con.m;
  b = con.b;
  cg_expr = con.cg_expr;
  cg_stmt = con.cg_stmt;
  cg_memref = con.cg_memref;
  sym_get = con.sym_get;
  sym_add = con.sym_add;
  sym_remove = con.sym_remove;
  dump_syms = con.dump_syms;
  arch_state = con.arch_state.host_state;
}

let init con = get_function con.m "__init"
let release con = get_function con.m "__release"
let dev_malloc_if_missing con = get_function con.m "__dev_malloc_if_missing"
let copy_to_host con = get_function con.m "__copy_to_host"
let copy_to_dev con = get_function con.m "__copy_to_dev"
let dev_run con = get_function con.m "__dev_run"
let buffer_struct_type con = buffer_t con.m

let cg_expr (con:context) (expr:expr) =
  (* map base to x86 *)
  X86.cg_expr (host_context con) expr

let cg_dev_kernel con stmt =
  
  let const_zero = const_zero con.c in
  
  (* reset ptx_dev arch_state *)
  con.arch_state.dev_state.shared_mem_bytes := 0;
  con.arch_state.dev_state.first_block := true;

  (* extract bounds for block/thread loops for kernel launch *)
  let default_bounds = IntImm 1 in
  let rec extract_bounds var = function
    | For (name, base, width, ordered, body) when (base_name name) = var ->
        width
    | stmt -> fold_children_in_stmt
                (fun e -> IntImm 1)
                (extract_bounds var)
                (fun a b ->
                   if a = default_bounds then
                     b
                   else if b = default_bounds then
                     a
                   else
                     Bop(Max, a, b))
                stmt
  in

  let n_tid_x   = extract_bounds "threadidx" stmt in
  let n_tid_y   = extract_bounds "threadidy" stmt in
  let n_tid_z   = extract_bounds "threadidz" stmt in
  let n_blkid_x = extract_bounds "blockidx" stmt in
  let n_blkid_y = extract_bounds "blockidy" stmt in
  let n_blkid_z = extract_bounds "blockidz" stmt in

  Printf.printf "\ndev kernel root:\n%s\n\n%!" (string_of_stmt stmt);

  Printf.printf " PTX bounds: (%sx%sx%s) threads, (%sx%sx%s) blocks\n%!"
    (string_of_expr n_tid_x) (string_of_expr n_tid_y) (string_of_expr n_tid_z)
    (string_of_expr n_blkid_x) (string_of_expr n_blkid_y) (string_of_expr n_blkid_z);

  (*
  TODO:
   build closure args to kernel func
   create memcopies for all buffer args
   store closure args in CUDA args array
   set up closure symbols for kernel:
     arg vals from ordered params - closure args iteri
   compile kernel to PTX string
   launch kernel
     ntid, nctaid grid size
     args void* array --> = host-style closure structure?
   copy back? copy nothing back? what about .result?
  *)

  (*
   * Build the kernel for the device
   *)

  (* extract the symbols we need to package up and pass to the kernel *)
  let names_in_stmt = find_names_in_stmt StringSet.empty pointer_size stmt in
  let closure_vars = List.map fst (StringIntSet.elements names_in_stmt) in
  let closure_vals = List.map con.sym_get closure_vars in
  let closure_types = List.map type_of closure_vals in
  let closure_reads =
    List.filter
      (fun v -> StringSet.mem v (find_loads_in_stmt stmt))
      closure_vars
  in
  let closure_writes =
    List.filter
      (fun v -> StringSet.mem v (find_stores_in_stmt stmt))
      closure_vars
  in

  if dbgprint then begin
    Printf.eprintf "Closure args:\n%!";
    List.iter
      (fun (n,v) -> Printf.eprintf "  %s := %!" n; dump_value v)
      (List.combine closure_vars closure_vals);

    List.iter (Printf.eprintf "  - Load:  %s\n%!") closure_reads;
    List.iter (Printf.eprintf "  - Store: %s\n%!") closure_writes;
  end;

  (* grab ptx_dev state state *)
  let dev_mod = con.arch_state.dev_mod in
  let dev_ctx = module_context dev_mod in
  
  let dev_closure_types =
    List.map
      begin fun t ->
        match classify_type t with
          | TypeKind.Float   -> float_type dev_ctx
          | TypeKind.Integer -> integer_type dev_ctx (integer_bitwidth t)
          | TypeKind.Pointer -> raw_buffer_t dev_ctx
          | _ -> raise (Wtf "Trying to build PTX device closure with unsupported type")
      end
      closure_types
  in

  (* build kernel function *)
  let k = define_function
            "kernel"
            (function_type (void_type dev_ctx) (Array.of_list dev_closure_types))
            dev_mod
  in

  (* set value names for the kernel function *)
  List.iter
    (fun (n, v) -> set_value_name n v)
    (List.combine closure_vars (param_list k));

  let dev_b = builder_at_end dev_ctx (entry_block k) in

  (* TODO: create entry function *)
  (* TODO: build dev_syms symtab for closure args *)
  let dev_syms = Hashtbl.create 10 in (* TODO! *)
  List.iter
    (fun (n, v) -> Hashtbl.add dev_syms n v)
    (List.combine closure_vars (param_list k));
  
  (* TODO: just use Ptx_dev's codegen_entry, with Buffer and Scalar args as needed? Makes it harder to patch in arbitrary context/state info *)
  let module DevCg = Cg_llvm.CodegenForArch(Ptx_dev) in
  let dev_con = DevCg.make_cg_context dev_ctx dev_mod dev_b dev_syms con.arch_state.dev_state in
  
  if dbgprint then dev_con.dump_syms ();
  
  Printf.eprintf "--- Starting PTX body codegen ---\n%!";

  (* TODO: codegen the main kernel *)
  ignore (Ptx_dev.cg_stmt dev_con stmt);
  ignore (build_ret_void dev_b);

  (* set calling convention to __global__ for PTX *)
  set_function_call_conv ptx_kernel k;

  (*
   * Set up the buffer args
   *)
  let closure_read_bufs = List.map con.arch_state.buf_get closure_reads in
  let closure_write_bufs = List.map con.arch_state.buf_get closure_writes in

  (* dev_malloc_if_missing *)
  List.iter
    begin fun buf -> ignore (
      build_call
        (dev_malloc_if_missing con)
        [| buf |]
        ""
        con.b
    ) end
    (closure_read_bufs @ closure_write_bufs);
  
  (* copy_to_dev *)
  List.iter
    begin fun buf -> ignore (
      build_call
        (copy_to_dev con)
        [| buf |]
        ""
        con.b
    ) end
    closure_read_bufs;

  (*
   * Build the call from the host
   *)
  (* track this entrypoint in the PTX module by its uniquified function name *)
  let entry_name = value_name k in
  let entry_name_str = build_global_stringptr entry_name "__entry_name" con.b in
  
  (* build the void* kernel args array *)
  let arg_t = pointer_type (i8_type con.c) in (* void* *)
  let cuArgsArr = build_alloca
                    (array_type arg_t (List.length closure_vars))
                    "cuArgs"
                    con.b in

  (* save closure args to cuArgsArr *)
  let closure_stack_vals =
    List.map
      begin fun (n,v) ->
        let v =
          (* grab the buffer_t if this is a buffer host ptr.
           * otherwise, pass the llvalue straight through. *)
          if (type_of v) = (raw_buffer_t con.c) then
            let buf = con.arch_state.buf_get n in
            cg_buffer_field buf DevPtr con.b
          else
            v
        in
        let ptr = build_alloca (type_of v) ((value_name v) ^ ".stack") con.b in
        ignore (build_store v ptr con.b);
        ptr
      end
      (List.combine closure_vars closure_vals)
  in
  Array.iteri
    begin fun i v ->
      let bits = build_bitcast v arg_t "" con.b in
      let ptr = build_gep cuArgsArr [| (ci con.c 0); (ci con.c i) |] "" con.b in
      ignore (build_store bits ptr con.b)
    end
    (Array.of_list closure_stack_vals);

  (* the basic launch args *)
  let launch_args = [
    entry_name_str;
    cg_expr con n_blkid_x; cg_expr con n_blkid_y; cg_expr con n_blkid_z;
    cg_expr con n_tid_x; cg_expr con n_tid_y; cg_expr con n_tid_z;
    const_int (i32_type con.c) !(con.arch_state.dev_state.shared_mem_bytes);
    build_gep cuArgsArr [| const_zero; const_zero |] "cuArgsArr" con.b;
  ] in

  let dev_run = dev_run con in
  Printf.eprintf "Building call to dev_run (%s) with args:\n%!" (string_of_lltype (type_of dev_run));
  List.iter (fun a -> Printf.eprintf "  %s\n%!" (string_of_lltype (type_of a))) launch_args;

  ignore (build_call dev_run (Array.of_list launch_args) "" con.b);

  (* copy_to_host *)
  List.iter
    begin fun buf -> ignore (
      build_call
        (copy_to_host con)
        [| buf |]
        ""
        con.b
    ) end
    closure_write_bufs;

  (* Return an ignorable llvalue *)
  const_zero

let cg_stmt (con:context) stmt = match stmt with
  (* map topmost ParFor into PTX kernel invocation *)
  | For (name, base, width, ordered, body) when is_simt_var name ->
      cg_dev_kernel con stmt

  (* map base to x86 *)
  | stmt -> X86.cg_stmt (host_context con) stmt

(*
 * Codegen the host calling module
 *)
let rec codegen_entry c m cg_entry make_cg_context e =
  (* load the template PTX host module *)
  Stdlib.init_module_ptx m;

  (* TODO: store functions strings, kernels, etc. in null-terminated global array,
   * which init() can walk at runtime?
   * Or just increment a global ref in this module? *)

  (* unpack entrypoint *)
  let (name, arglist, body) = e in

  (* build the buffer_t calling convention ("wrapper") directly, so we can 
   * access buffer_t params in our generated code *)
  let f = define_wrapper c m e in
  let init_bb = entry_block f in
  let body_bb = append_block c "body" f in
  let b = builder_at_end c body_bb in

  (* initialize our codegen state *)
  let state = start_state () in

  (* hang on to the buffer_ts from the wrapper *)
  List.iter
    (function
       | (Buffer nm), param ->
           state.buf_add nm param
       | _ -> ())
    (List.combine arglist (param_list f));

  (* initialize a symbol table with the params *)
  let param_syms = arg_symtab arglist (arg_var_vals arglist b) in

  (* DEBUG: log current context *)
  if dbgprint then begin
    state.buf_dump ();
    dump_syms param_syms;
  end;

  (* codegen the actual function *)
  let con = make_cg_context c m b param_syms state in
  ignore (cg_stmt con body);

  (* release runtime resources before returning *)
  ignore (build_call (release con) [| |] "" b);

  (* return void from main *)
  ignore (build_ret_void b);

  (* dig up the device module, into which all the PTX codegen should have happened *)
  let dev_mod = con.arch_state.dev_mod in
  let dev_ctx = module_context dev_mod in

  dump_module dev_mod;
  let ptx_src = Llutil.compile_module_to_string dev_mod in
  Printf.printf "PTX:\n%s\n%!" ptx_src;

  (* free memory *)
  dispose_module dev_mod;
  dispose_context dev_ctx;

  (* compile PTX source to a global string constant *)
  let ptx_src_str = build_global_stringptr ptx_src "__ptx_src" b in

  (* jump back to the start; init CUDA *)
  let b = builder_at_end c init_bb in
  ignore (build_call (init con) [| ptx_src_str |] "" b);
  ignore (build_br body_bb b);

  (* check on our result *)
  ignore (verify_cg m);
  if dump_mod then dump_module m;

  (* return the function we've built *)
  assert (name = (value_name f));
  f

let malloc con name count elem_size =
  Printf.eprintf "Ptx.malloc %s (%sx%s)\n%!" name (string_of_expr count) (string_of_expr elem_size);
  (* TODO: track malloc llvalue -> size (dynamic llvalue) mapping for cuda memcpy *)
  let hostptr = X86.malloc (host_context con) name count elem_size in
  
  (* build a buffer_t to track this allocation *)
  let buf = build_alloca (buffer_struct_type con) ("malloc_" ^ name) con.b in
  let set_field f v =
    Printf.eprintf "  set_field %s :=%!" (string_of_buffer_field f);
    dump_value v;
    let fptr = cg_buffer_field_ref buf f con.b in
    let fty = element_type (type_of fptr) in
    let v = match classify_type fty with
      | TypeKind.Integer ->
          Printf.eprintf "  building const_intcast to %s of%!" (string_of_lltype fty);
          dump_value v;
          build_intcast v fty (name ^ "_field_type_cast") con.b
      | _ -> v
    in
    Printf.eprintf "   ->%!"; dump_value fptr;
    ignore (build_store v fptr con.b) in

  let zero = const_zero con.c
  and one = ci con.c 1 in
  
  set_field HostPtr   hostptr;
  set_field DevPtr    zero;
  set_field HostDirty one;
  set_field DevDirty  zero;
  set_field (Dim 0)   (cg_expr con count);
  set_field (Dim 1)   one;
  set_field (Dim 2)   one;
  set_field (Dim 3)   one;
  set_field ElemSize  (cg_expr con elem_size);

  con.arch_state.buf_add name buf;

  hostptr

let free con ptr =
  X86.free (host_context con) ptr

let env =
  let ntid_decl   = (".llvm.ptx.read.ntid.x", [], i32, Extern) in
  let nctaid_decl = (".llvm.ptx.read.nctaid.x", [], i32, Extern) in
  
  let e = Environment.empty in
  let e = Environment.add "llvm.ptx.read.nctaid.x" nctaid_decl e in
  let e = Environment.add "llvm.ptx.read.ntid.x" ntid_decl e in
  
  e
