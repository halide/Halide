open Llvm
open Ir
open Util
open Cg_llvm_util

(* Function calling conventions for PTX, from <llvm/CallingConv.h>
 * For whatever reason, the official OCaml binding only exposes a few
 * CallingConv constants. *)
let ptx_kernel = 71
let ptx_device = 72

let cg_expr (c:llcontext) (m:llmodule) (b:llbuilder) (cg_expr : expr -> llvalue) (expr : expr) =
  cg_expr expr

let cg_stmt (c:llcontext) (m:llmodule) (b:llbuilder) (cg_stmt : stmt -> llvalue) (stmt : stmt) =
  cg_stmt stmt

let postprocess_function (f:llvalue) =
  set_function_call_conv ptx_kernel f

let rec codegen_entry host_ctx host_mod cg_entry entry =
  (* create separate device module *)
  let dev_ctx = create_context () in
  let dev_mod = create_module dev_ctx "<halide-device-ptx>" in

  (* set up module templates *)
  Stdlib.init_module_ptx_dev dev_mod;

  (* TODO: run simtfy here. It should extract bounds expressions for thread ID x,y,z,w
   * and use those for kernel launch *)

  (* codegen the main kernel *)
  let f = cg_entry dev_ctx dev_mod entry in

  (* set calling convention to __global__ for PTX *)
  set_function_call_conv ptx_kernel f;

  let ptx_src = Llutil.compile_module_to_string dev_mod in
  Printf.printf "PTX:\n%s\n%!" ptx_src;

  (* free memory *)
  dispose_module dev_mod;
  dispose_context dev_ctx;

  (*
   * Codegen the host calling module
   *)
  let c = host_ctx in
  let m = host_mod in
  Stdlib.init_module_ptx host_mod;

  (* unpack entrypoint *)
  let (entrypoint_name, arglist, _) = entry in

  let i32_t = i32_type c in

  let get_function = get_function m in

  let init = get_function "init" in
  let dev_malloc_if_missing = get_function "dev_malloc_if_missing" in
  let copy_to_host = get_function "copy_to_host" in
  let copy_to_dev = get_function "copy_to_dev" in
  let dev_run = get_function "dev_run" in
  let buffer_struct_type = buffer_t m in

  let type_of_arg = function
    | Scalar (_, vt) -> type_of_val_type c vt
    | Buffer _ -> pointer_type (buffer_struct_type)
  in

  (* build function *)
  let name = entrypoint_name ^ "_wrapper" in
  let f = define_function
            name
            (function_type
               (void_type c)
               (Array.of_list (List.map type_of_arg arglist)))
            m in

  let b = builder_at_end c (entry_block f) in

  (* build a const data item of the compiled ptx source *)
  let ptx_src_str = build_global_stringptr ptx_src "ptx_src" b in
  let entry_name_str = build_global_stringptr entrypoint_name "entry_name" b in

  (* init CUDA *)
  ignore (build_call init [| ptx_src_str; entry_name_str |] "" b);

  (* args array for kernel launch *)
  let arg_t = pointer_type (i8_type c) in (* void* *)
  let cuArgs = build_alloca (array_type arg_t (List.length arglist)) "cuArgs" b in

  let paramlist = Array.to_list (params f) in
  let args = List.combine arglist paramlist in

  (*
   * build llvalue dev_run args array list
   * for each arg:
   *    if buffer
   *        ; getelementptr to field 1 of buffer_t = CUdeviceptr:
   *        %arg.stack = build_struct_gep %arg (const_int i32_t 1) "<arg>.stack" b 
   *        dev_malloc_if_missing
   *        if non-result
   *            copy_to_dev
   *    else
   *        %arg.stack = alloca t       (align 4?)
   *        store t %arg, t* %arg.stack (align 4?)
   *    ; store to cuArgs:
   *    build_store (build_bitcast %arg.stack arg_t "" b) (build_gep cuArgs [|(i64 0) (i64 arg_idx)|])
   *)
  Array.iteri
    begin fun i (a, p) ->
      let cuArgval =
        match a with
          | Buffer nm ->
              set_value_name nm p;
              (* malloc if missing *)
              ignore (build_call dev_malloc_if_missing [| p |] "" b);
              (* copy to device for all but .result *)
              if nm <> ".result" then ignore (build_call copy_to_dev [| p |] "" b);
              (* return GEP: &(p->dev) *)
              build_struct_gep p 1 (nm ^ ".devptr") b
          | Scalar (nm, _) ->
              set_value_name nm p;
              (* store arg to local stack for passing by reference to cuArgs *)
              let stackArg = build_alloca (type_of p) (nm ^ ".stack") b in
              ignore (build_store p stackArg b);
              stackArg
      in
      Printf.printf "preprocessed arg %s\n%!" (match a with Buffer nm -> nm | Scalar (nm, _) -> nm);
      ignore(
        build_store
          (build_bitcast cuArgval arg_t "" b)
          (build_gep cuArgs [| (const_int i32_t 0); (const_int i32_t i) |] "cuArg" b)
          b
      )
    end
    (Array.of_list args);

  (*    
   * call dev_run(threadsX, threadsY, threadsZ, blocksX, blocksY, blocksZ, localmembytes, cuArgsPtr)
   *)
  let cuArgsPtr = build_gep cuArgs [| (const_int i32_t 0); (const_int i32_t 0) |] "cuArgsPtr" b in
  let threadsX = const_int i32_t 256 in
  let threadsY = const_int i32_t  1 in
  let threadsZ = const_int i32_t  1 in
  let blocksX  = const_int i32_t 64 in
  let blocksY  = const_int i32_t  1 in
  let blocksZ  = const_int i32_t  1 in
  let sharedMem = const_int i32_t 0 in
  ignore (
    build_call
      dev_run
      [| blocksX; blocksY; blocksZ; threadsX; threadsY; threadsZ; sharedMem; cuArgsPtr |]
      ""
      b
  );

  (* find arg named ".result"
   * call copy_to_host %result *)
  let (_, result_buffer) =
    List.hd
      (List.filter
         (function (Buffer nm, _) when nm = ".result" -> true | _ -> false)
         args)
  in
  ignore (build_call copy_to_host [| result_buffer |] "" b);

  ignore (build_ret_void b);

  (* return the generated function *)
  f

let malloc = (fun _ _ _ _ _ -> raise (Wtf "No malloc for PTX yet"))
let free = (fun _ _ _ _ -> raise (Wtf "No free for PTX yet"))

let env =
  let ntid_decl   = (".llvm.ptx.read.ntid.x", [], i32, Extern) in
  let nctaid_decl = (".llvm.ptx.read.nctaid.x", [], i32, Extern) in
  
  let e = Environment.empty in
  let e = Environment.add "llvm.ptx.read.nctaid.x" nctaid_decl e in
  let e = Environment.add "llvm.ptx.read.ntid.x" ntid_decl e in
  
  e
