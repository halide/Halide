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

let lower (f:string) (env:environment) (sched: schedule_tree) (debug: int) =
  (* Printexc.record_backtrace true; *)

  begin
    Printf.printf "Lowering function\n";
    let lowered = lower_function f env sched (if (debug = 1) then true else false) in
    (* Printf.printf "Breaking false dependences\n";
     let lowered = Break_false_dependence.break_false_dependence_stmt lowered in 
     Printf.printf "Constant folding\n";
     let lowered = Constant_fold.constant_fold_stmt lowered in
     Printf.printf "Breaking false dependences\n";
     let lowered = Break_false_dependence.break_false_dependence_stmt lowered in  *)
    Printf.printf "Constant folding\n";
    let lowered = Constant_fold.constant_fold_stmt lowered in 
    Printf.printf "Resulting stmt:\n%s\n" (string_of_stmt lowered);
    lowered
  end

  (* with x -> begin
    Printf.printf "Compilation failed. Backtrace:\n%s\n%!" (Printexc.get_backtrace ());
    raise x
  end *)

let compile name args stmt =

  Printexc.record_backtrace true;


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
      Printf.printf "Initializing native target\n%!"; 
      ignore (initialize_native_target());
      Printf.printf "Compiling:\n%s to C callable\n%!" (string_of_toplevel func);
      let (c, m, f) = codegen_to_c_callable func in
      ignore(Llvm_bitwriter.write_bitcode_file m "generated.bc");
      Hashtbl.add compilation_cache func (m, f);
      (* TODO: this leaks the llcontext, and will leak the module if the cache
       * doesn't free it eventually. *)
      (m, f)
    end
  end
  with x -> begin
    Printf.printf "Compilation failed. Backtrace:\n%s\n%!" (Printexc.get_backtrace ());
    raise x
  end

let compile_to_file name args stmt =
  Printexc.record_backtrace true;
  
  try begin
    ignore (initialize_native_target());
    let module Cg = Cg_for_target in
    Cg.codegen_to_bitcode_and_header (name, args, stmt)
  end
  with x -> begin
    Printf.printf "Compilation failed. Backtrace:\n%s\n%!" (Printexc.get_backtrace ());
    raise x
  end

let _ = 
  (* Make IR nodes *)
  Callback.register "makeIntImm" (fun a -> IntImm a);
  Callback.register "makeUIntImm" (fun a -> IntImm a);
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
  Callback.register "makeLoad" (fun t buf idx -> Load (t, "." ^ buf, idx));
  Callback.register "makeUniform" (fun t n -> Var (t, "." ^ n));
  Callback.register "makeStore" (fun a buf idx -> Store (a, "." ^ buf, idx));
  Callback.register "makeFor" (fun var min n stmt -> For (var, min, n, true, stmt));
  Callback.register "inferType" (fun expr -> val_type_of_expr expr);
  Callback.register "makeCall" (fun t name args -> Call (t, name, args));
  Callback.register "makeDebug" (fun e prefix args -> Debug (e, prefix, args));
  Callback.register "makeDefinition" (fun name argnames body -> (name, List.map (fun x -> (i32, x)) argnames, val_type_of_expr body, Pure body));
  Callback.register "makeEnv" (fun _ -> Environment.empty);
  Callback.register "addDefinitionToEnv" (fun env def -> 
    let (n, _, _, _) = def in 
    Environment.add n def env
  );
  
  Callback.register "addScatterToDefinition" (fun env name update_name update_args update_var reduction_domain ->
    let (_, args, return_type, Pure init_expr) = Environment.find name env in
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
    let (_, args, _, _) = Environment.find f env in
    let region = List.map2 (fun (t, v) x -> (v, IntImm 0, x)) args sizes in
    Printf.printf "Guru:\n%s\n%!" (String.concat "\n" guru.serialized);
    Printf.printf "About to make default schedule...\n%!";    
    generate_schedule f env guru
      
  );
  
  Callback.register "doLower" lower;  
  Callback.register "doCompile" (compile);
  Callback.register "doCompileToFile" compile_to_file;
  
  (* Guru transformations. These partially apply the various ml
     functions to return an unary function that will transform a guru
     in the specified way. *)
  Callback.register "makeVectorizeTransform" (fun func var -> vectorize_schedule func var);
  Callback.register "makeUnrollTransform" (fun func var -> unroll_schedule func var);
  Callback.register "makeSplitTransform" (fun func var outer inner n -> split_schedule func var outer inner n);
  Callback.register "makeTransposeTransform" (fun func var1 var2 -> transpose_schedule func var1 var2);
  Callback.register "makeChunkTransform" (fun func var -> chunk_schedule func var);
  Callback.register "makeRootTransform" (fun func -> root_schedule func);
  Callback.register "makeParallelTransform" (fun func var -> parallel_schedule func var);  
  Callback.register "makeRandomTransform" (fun func seed -> random_schedule func seed);
