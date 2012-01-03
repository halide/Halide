open Llvm
open Ir
open Util
open Cg_llvm_util

(* Function calling conventions for PTX, from <llvm/CallingConv.h>
 * For whatever reason, the official OCaml binding only exposes a few
 * CallingConv constants. *)
let ptx_kernel = 71
let ptx_device = 72

let cg_expr (con:cg_context) (expr:expr) = 
  con.cg_expr expr

let cg_stmt (con:cg_context) (stmt:stmt) = 
  con.cg_stmt stmt

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

  let init = get_function "__init" in
  let dev_malloc_if_missing = get_function "__dev_malloc_if_missing" in
  let copy_to_host = get_function "__copy_to_host" in
  let copy_to_dev = get_function "__copy_to_dev" in
  let dev_run = get_function "__dev_run" in
  let buffer_struct_type = buffer_t m in

  let type_of_arg = function
    | Scalar (_, vt) -> type_of_val_type c vt
    | Buffer _ -> pointer_type (buffer_struct_type)
  in

  (* build function *)
  let name = entrypoint_name in
  let f = define_function
            name
            (function_type
               (void_type c)
               (Array.of_list (List.map type_of_arg arglist)))
            m in

  assert (name = (value_name f));

  let b = builder_at_end c (entry_block f) in

  (* build a const data item of the compiled ptx source *)
  let ptx_src_str = build_global_stringptr ptx_src "__ptx_src" b in
  let entry_name_str = build_global_stringptr entrypoint_name "__entry_name" b in

  (* init CUDA *)
  ignore (build_call init [| ptx_src_str; entry_name_str |] "" b);

  (* args array for kernel launch *)
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
   
  let toi32 x = build_intcast x (i32_type c) "" b in
  let ci x = const_int (i32_type c) x in

  let cuArgs =
    List.map
      begin function
        | Buffer name, param ->
            set_value_name name param;
            (* malloc if missing *)
            ignore (build_call dev_malloc_if_missing [| param |] "" b);
            (* copy to device for all but .result *)
            if name <> ".result" then ignore (build_call copy_to_dev [| param |] "" b);
            Printf.printf "preprocessed buffer arg %s\n%!" name;
            (* return GEP: &(p->dev) *)
            let devptr = build_struct_gep param 1 (name ^ ".devptr") b in
            let dims = List.map
              begin fun i ->
                let stackArg = build_alloca (i32_type c) (name ^ ".dim." ^ (string_of_int i)) b in
                let dimVal = toi32 (build_load (build_gep param [|ci 0; ci 4; ci i|] "" b) "" b) in
                ignore (build_store dimVal stackArg b);
                stackArg
              end
              (0--4)
            in devptr::dims
        | Scalar (name, _), param ->
            set_value_name name param;
            (* store arg to local stack for passing by reference to cuArgs *)
            let stackArg = build_alloca (type_of param) (name ^ ".stack") b in
            ignore (build_store param stackArg b);
            Printf.printf "preprocessed scalar arg %s\n%!" name;
            [stackArg]
      end
      args
  in
  let cuArgs = List.flatten cuArgs in
  
  let arg_t = pointer_type (i8_type c) in (* void* *)
  let cuArgsArr = build_alloca (array_type arg_t (List.length cuArgs)) "cuArgs" b in

  Array.iteri
    begin fun i cuArgval ->
      ignore (build_store
        (build_bitcast cuArgval arg_t "" b)
        (build_gep cuArgsArr [| (ci 0); (ci i) |] "cuArg" b)
        b)
    end
    (Array.of_list cuArgs);

  (*    
   * call dev_run(threadsX, threadsY, threadsZ, blocksX, blocksY, blocksZ, localmembytes, cuArgsPtr)
   *)
  let cuArgsPtr = build_gep cuArgsArr [| (const_int i32_t 0); (const_int i32_t 0) |] "cuArgsPtr" b in
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
  assert ((value_name result_buffer) = ".result");
  Printf.printf "Result name: %s\n%!" (value_name result_buffer);
  ignore (build_call copy_to_host [| result_buffer |] "" b);

  ignore (build_ret_void b);

  (* return the generated function *)
  f

let malloc = (fun _ _ -> raise (Wtf "No malloc for PTX yet"))
let free = (fun _ _ -> raise (Wtf "No free for PTX yet"))

let env =
  let ntid_decl   = (".llvm.ptx.read.ntid.x", [], i32, Extern) in
  let nctaid_decl = (".llvm.ptx.read.nctaid.x", [], i32, Extern) in
  
  let e = Environment.empty in
  let e = Environment.add "llvm.ptx.read.nctaid.x" nctaid_decl e in
  let e = Environment.add "llvm.ptx.read.ntid.x" ntid_decl e in
  
  e
