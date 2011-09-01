open Ir
open Ir_printer
open Cg_llvm
open Llvm
open Llvm_executionengine

let runStmt stmt arg = 
  ignore (initialize_native_target ());
  Printf.printf "Compiling %s to C callable\n%!" (string_of_stmt stmt);
  let (m, f) = codegen_to_c_callable (create_context()) stmt in
  Printf.printf "Creating execution engine\n%!"; 
  let ee = ExecutionEngine.create m in
  Printf.printf "Running function\n%!";
  ignore (    
    ExecutionEngine.run_function f [| GenericValue.of_pointer arg |] ee
  )

let _ = 
  Callback.register "makeIntImm" (fun a -> IntImm a);
  Callback.register "makeAdd" (fun a b -> Bop (Add, a, b));
  Callback.register "makeVar" (fun a -> Var a);
  Callback.register "makeLoad" (fun buf idx -> Load (i32, {buf=buf; idx=idx}));
  Callback.register "makeStore" (fun a buf idx -> Store (a, {buf=buf; idx=idx}));
  Callback.register "doRun" (fun stmt arg -> runStmt stmt arg);
  Callback.register "doPrint" (fun a -> Printf.printf "%s\n%!" (string_of_expr a))




