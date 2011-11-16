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

let compile args stmt =

  (* TODO: Canonicalize names. Variables get renamed to v0 v1
     v2... depending on the order in which they're found, arguments to
     a0, a1, a2, depending on the order that they occur in the
     arguments list. This is done to assist hashing. *)

  let func = (args, stmt) in
  let c = create_context() in
  if Hashtbl.mem compilation_cache func then begin
    (* Printf.printf "Found function in cache\n%!";  *)
    Hashtbl.find compilation_cache func
  end else begin 
    Printf.printf "Initializing native target\n%!"; 
    ignore (initialize_native_target());
    Printf.printf "Compiling:\n%s to C callable\n%!" (string_of_toplevel func);
    let (m, f) = codegen_to_c_callable c func Architecture.host in
    ignore(Llvm_bitwriter.write_bitcode_file m "generated.bc");
    Hashtbl.add compilation_cache func (m, f);
    (m, f)
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
  Callback.register "makeEQ" (fun a b -> Cmp (EQ, a, b));
  Callback.register "makeNE" (fun a b -> Cmp (NE, a, b));
  Callback.register "makeGT" (fun a b -> Cmp (GT, a, b));
  Callback.register "makeLT" (fun a b -> Cmp (LT, a, b));
  Callback.register "makeGE" (fun a b -> Cmp (GE, a, b));
  Callback.register "makeLE" (fun a b -> Cmp (LE, a, b));
  Callback.register "makeSelect" (fun c a b -> Select (c, a, b));
  Callback.register "makeVar" (fun a -> Var (i32, a));
  Callback.register "makeFloatType" (fun a -> Float a);
  Callback.register "makeIntType" (fun a -> Int a);
  Callback.register "makeUIntType" (fun a -> UInt a);
  Callback.register "makeLoad" (fun t buf idx -> Load (t, "." ^ buf, idx));
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
  
  Callback.register "addScatterToDefinition" (fun env name args rhs ->
    let (n, a, r, gather) = match Environment.find name env with
      | (n, a, r, Pure gather) -> (n, a, r, gather)
      | _ -> raise (Wtf "Can only add a scatter definition to a function already defined as a gather")
    in
    Environment.add n (n, a, r, Impure (gather, args, rhs)) env
  );

  Callback.register "makeList" (fun _ -> []);
  Callback.register "addToList" (fun l x -> x::l);  
  Callback.register "makePair" (fun x y -> (x, y));
  Callback.register "makeTriple" (fun x y z -> (x, y, z));
  
  Callback.register "makeBufferArg" (fun str -> Buffer ("." ^ str));

  (* Debugging, compilation *)
  Callback.register "printStmt" (fun a -> Printf.printf "%s\n%!" (string_of_stmt a));

  Callback.register "printSchedule" (fun s -> print_schedule s; Printf.printf "%!");

  Callback.register "makeSchedule" (fun (f: string) (sizes: int list) (env: environment) ->
    let (_, args, _, _) = Environment.find f env in
    let region = List.map2 (fun (t, v) x -> Printf.printf "making default schedule: %s\n" v; (v, IntImm 0, IntImm x)) args sizes in
    Printf.printf("About to make default schedule...\n%!");
    make_default_schedule f env region
  );

  Callback.register "doLower" (fun (f:string) (env:environment) (sched: schedule_tree) (debug: int)-> 
    Printf.printf "Lowering function\n";
    let lowered = lower_function f env sched (if (debug = 1) then true else false) in
    Printf.printf "Breaking false dependences\n";
    (* let lowered = Break_false_dependence.break_false_dependence_stmt lowered in 
    Printf.printf "Constant folding\n";
    let lowered = Constant_fold.constant_fold_stmt lowered in
    Printf.printf "Breaking false dependences\n";
    let lowered = Break_false_dependence.break_false_dependence_stmt lowered in  *)
    Printf.printf "Constant folding\n";
    let lowered = Constant_fold.constant_fold_stmt lowered in 
    lowered
  );

  Callback.register "doCompile" (fun a s -> compile a s);

  (* Schedule transformations. These partially apply the various ml
     functions to return an unary function that will transform a schedule
     in the specified way. *)
  Callback.register "makeVectorizeTransform" (fun func var -> vectorize_schedule func var);
  Callback.register "makeUnrollTransform" (fun func var -> unroll_schedule func var);
  Callback.register "makeSplitTransform" (fun func var outer inner n -> split_schedule func var outer inner n);
  Callback.register "makeTransposeTransform" (fun func var1 var2 -> transpose_schedule func var1 var2);
  Callback.register "makeChunkTransform" (fun func var args region -> chunk_schedule func var args region);
  Callback.register "makeParallelTransform" (fun func var min size -> parallel_schedule func var min size);
  Callback.register "makeSerialTransform" (fun func var min size -> serial_schedule func var min size);
  
