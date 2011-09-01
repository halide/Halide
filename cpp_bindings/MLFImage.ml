open Ir
open Ir_printer
open Cg_llvm
open Llvm
open Llvm_executionengine

let compilation_cache = 
  Hashtbl.create 16

let compile stmt =
  (* First check the compilation cache *)
  let c = create_context() in
  if Hashtbl.mem compilation_cache stmt then begin
    Hashtbl.find compilation_cache stmt
  end else begin 
    ignore (initialize_native_target());
    Printf.printf "Compiling %s to C callable\n%!" (string_of_stmt stmt);
    let (m, f) = codegen_to_c_callable c stmt in
    ignore(Llvm_bitwriter.write_bitcode_file m "generated.bc");
    Hashtbl.add compilation_cache stmt (m, f);
    (m, f)
  end
        
let _ = 
  Callback.register "makeIntImm" (fun a -> IntImm a);
  Callback.register "makeAdd" (fun a b -> Bop (Add, a, b));
  Callback.register "makeVar" (fun a -> Var a);
  Callback.register "makeLoad" (fun buf idx -> Load (i32, {buf=buf; idx=idx}));
  Callback.register "makeStore" (fun a buf idx -> Store (a, {buf=buf; idx=idx}));
  Callback.register "doPrint" (fun a -> Printf.printf "%s\n%!" (string_of_stmt a));
  Callback.register "doCompile" compile


