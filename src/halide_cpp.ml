open Ir
open Ir_printer
open Cg_llvm
open Llvm
open Llvm_executionengine
open Vectorize
open Unroll
open Split
open Constant_fold
open Schedule
open Lower
open Schedule_transforms
open Util

let compilation_cache = 
  Hashtbl.create 16

let codegen_to_c_callable e =
  let module Cg = Cg_for_target in
  let (c,m,f) = Cg.codegen_entry e in
  let w = Cg.codegen_c_wrapper c m f in
  (c,m,w)

let lower (f:string) (env:environment) (sched: schedule_tree) =
  (* Printexc.record_backtrace true; *)

  lower_function f env sched

  (* with x -> begin
    Printf.printf "Compilation failed. Backtrace:\n%s\n%!" (Printexc.get_backtrace ());
    raise x
  end *)

let serializeEntry name args stmt = Sexplib.Sexp.to_string
                                      (sexp_of_entrypoint (name, args, stmt))

let compile name args stmt =

  (* Printexc.record_backtrace true; *)


  (* TODO: Canonicalize names. Variables get renamed to v0 v1
     v2... depending on the order in which they're found, arguments to
     a0, a1, a2, depending on the order that they occur in the
     arguments list. This is done to assist hashing. *)

  try begin
    let func = (name, args, stmt) in
    if Hashtbl.mem compilation_cache func then begin
      (* Printf.printf "Found function in cache\n%!";  *)
      Hashtbl.find compilation_cache func
    end else begin 
      dbg 2 "Initializing native target\n%!"; 
      ignore (initialize_native_target());
      dbg 2 "Compiling:\n%s to C callable\n%!" (string_of_toplevel func);
      let (c, m, f) = codegen_to_c_callable func in
      (* ignore(Llvm_bitwriter.write_bitcode_file m "generated.bc"); *)
      Hashtbl.add compilation_cache func (m, f);

      (* Log the lowered entrypoint *)
      if verbosity > 1 then begin
        let out = open_out (name ^ ".sexp") in
        Printf.fprintf out "%s%!" (serializeEntry name args stmt);
        close_out out
      end;

      (* TODO: this leaks the llcontext, and will leak the module if the cache
       * doesn't free it eventually. *)
      (m, f)
    end
  end
  with x -> begin
    Printf.printf "Compilation failed.\n%!" (* "Backtrace:\n%s\n%!" (Printexc.get_backtrace ()) *);
    raise x
  end

let compile_to_file name args stmt =
  (* Printexc.record_backtrace true; *)
  
  let backend = try
    Sys.getenv "HL_BACKEND"
  with Not_found ->
    Printf.eprintf "HL_BACKEND not set - defaulting to LLVM\n";
    "llvm"
  in

  try begin match backend with
    | "llvm" ->
        (* ignore (initialize_native_target()); *)
        let module Cg = Cg_for_target in
        Cg.codegen_to_bitcode_and_header (name, args, stmt)
    | "c" ->
        Cg_c.codegen_to_file (name, args, stmt)
    | _ -> failwith ("Unrecognized HL_BACKEND: " ^ backend)
  end
  with x -> begin
    Printf.printf "Compilation failed.\n%!" (* "Backtrace:\n%s\n%!" (Printexc.get_backtrace ()) *);
    raise x
  end

let cast_to_int x = match val_type_of_expr x with
  | Int 32 -> x
  | _ -> Cast (Int 32, x)


let _ = 
  (* Make IR nodes *)
  Callback.register "makeIntImm" (fun a -> IntImm a);
  Callback.register "makeFloatImm" (fun a -> FloatImm a);
  Callback.register "makeCast" (fun t x -> Cast(t, x));
  Callback.register "makeAdd" (fun a b -> a +~ b);
  Callback.register "makeMul" (fun a b -> a *~ b);
  Callback.register "makeSub" (fun a b -> a -~ b);
  Callback.register "makeDiv" (fun a b -> a /~ b);
  Callback.register "makeMod" (fun a b -> a %~ b);
  Callback.register "makeMax" (fun a b -> Bop (Max, a, b));
  Callback.register "makeMin" (fun a b -> Bop (Min, a, b));
  Callback.register "makeEQ" (fun a b -> Cmp (EQ, a, b));
  Callback.register "makeNE" (fun a b -> Cmp (NE, a, b));
  Callback.register "makeGT" (fun a b -> Cmp (GT, a, b));
  Callback.register "makeLT" (fun a b -> Cmp (LT, a, b));
  Callback.register "makeGE" (fun a b -> Cmp (GE, a, b));
  Callback.register "makeLE" (fun a b -> Cmp (LE, a, b));
  Callback.register "makeAnd" (fun a b -> And (a, b));
  Callback.register "makeOr" (fun a b -> Or (a, b));
  Callback.register "makeNot" (fun a -> Not (a));
  Callback.register "makeSelect" (fun c a b -> Select (c, a, b));
  Callback.register "makeVar" (fun a -> Var (i32, a));
  Callback.register "makeFloatType" (fun a -> Float a);
  Callback.register "makeIntType" (fun a -> Int a);
  Callback.register "makeUIntType" (fun a -> UInt a);
  Callback.register "makeUniform" (fun t n -> Var (t, "." ^ n));
  Callback.register "makeFuncCall" (fun t name args -> Call (Func, t, name, args));
  Callback.register "makeExternCall" (fun t name args -> Call (Extern, t, "." ^ name, args));
  Callback.register "makeImageCall" (fun t name args -> Call (Image, t, "." ^ name, args));
  Callback.register "makeDebug" (fun e prefix args -> Debug (e, prefix, args));
  Callback.register "makeDefinition" (fun name argnames body -> (name, List.map (fun x -> (i32, x)) argnames, val_type_of_expr body, Pure body));
  Callback.register "makeEnv" (fun _ -> Environment.empty);
  Callback.register "addDefinitionToEnv" (fun env def -> 
    add_function def env
  );
  
  Callback.register "addScatterToDefinition" (fun env name update_name update_args update_var reduction_domain ->
    let (args, return_type, body) = find_function name env in
    let init_expr = match body with
      | Pure e -> e
      | _ -> failwith ("Can't add multiple reduction update steps to " ^ name)
    in

    let init_dims = List.length args and update_dims = List.length update_args in
    if List.length args <> List.length update_args then
      failwith (Printf.sprintf 
                  "Initial value of %s has %d dimensions, but updated value uses %d dimensions" 
                  name init_dims update_dims) 
    else ();

    (* The pure args are the naked vars in the update_args list that aren't in the reduction domain *)
    let rec get_pure_args = function
      | [] -> []          
      | (Var (t, n))::rest ->
          if (List.exists (fun (x, _, _) -> x = n) reduction_domain) then get_pure_args rest
          else (t, n)::(get_pure_args rest)
      | first::rest -> get_pure_args rest
    in
    let update_pure_args = get_pure_args update_args in
    let update_func = (update_name, update_pure_args, return_type, Pure update_var) in
    let env = Environment.add update_name update_func env in
    let reduce_body =  Reduce (init_expr, update_args, update_name, reduction_domain) in
    let reduce_func =  (name, args, return_type, reduce_body) in
    Environment.add name reduce_func env
  );

  Callback.register "makeList" (fun _ -> []);
  Callback.register "addToList" (fun l x -> x::l);  
  Callback.register "makePair" (fun x y -> (x, y));
  Callback.register "makeTriple" (fun x y z -> (x, y, z));
  
  Callback.register "makeBufferArg" (fun str -> Buffer ("." ^ str));
  Callback.register "makeScalarArg" (fun n t -> Scalar ("." ^ n, t));
  
  (* Debugging, compilation *)
  Callback.register "printStmt" (fun a -> Printf.printf "%s\n%!" (string_of_stmt a));
  
  Callback.register "printSchedule" (fun s -> print_schedule s; Printf.printf "%!");
  
  Callback.register "makeNoviceGuru" (fun _ -> novice);

  Callback.register "loadGuruFromFile" (fun filename -> load_guru_from_file filename);
  Callback.register "saveGuruToFile" (fun guru filename -> save_guru_to_file guru filename);

  Callback.register "makeSchedule" (fun (f: string) (sizes: expr list) (env: environment) (guru: scheduling_guru) ->
    dbg 2 "Guru:\n%s\n%!" (String.concat "\n" guru.serialized);
    dbg 2 "About to make default schedule...\n%!";    
    generate_schedule f env guru      
  );
  
  Callback.register "doLower" lower;  
  Callback.register "doCompile" compile;
  Callback.register "doCompileToFile" compile_to_file;
  
  (* Guru transformations. These partially apply the various ml
     functions to return an unary function that will transform a guru
     in the specified way. *)
  Callback.register "makeVectorizeTransform" (fun func var -> vectorize_schedule func var);
  Callback.register "makeUnrollTransform" (fun func var -> unroll_schedule func var);
  Callback.register "makeBoundTransform" (fun func var min size -> bound_schedule func var min size);
  Callback.register "makeSplitTransform" (fun func var outer inner n -> split_schedule func var outer inner n);
  Callback.register "makeTransposeTransform" (fun func var1 var2 -> transpose_schedule func var1 var2);
  Callback.register "makeChunkTransform" (fun func var -> chunk_schedule func var);
  Callback.register "makeRootTransform" (fun func -> root_schedule func);
  Callback.register "makeParallelTransform" (fun func var -> parallel_schedule func var);  
  Callback.register "makeRandomTransform" (fun func seed -> random_schedule func seed);

  Callback.register "serializeExpr" (fun e -> Sexplib.Sexp.to_string (sexp_of_expr e));
  Callback.register "serializeStmt" (fun s -> Sexplib.Sexp.to_string (sexp_of_stmt s));
  Callback.register "serializeEntry" serializeEntry;
