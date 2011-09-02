open Ir
open Ir_printer
open Cg_llvm
open Llvm
open Llvm_executionengine
open Vectorize

let compilation_cache = 
  Hashtbl.create 16

let compile func =
  (* First check the compilation cache *)
  let (args, stmt) = func in
  let c = create_context() in
  if Hashtbl.mem compilation_cache func then begin
    (* Printf.printf "Found function in cache\n%!";  *)
    Hashtbl.find compilation_cache func
  end else begin 
    Printf.printf "Initializing native target\n%!"; 
    ignore (initialize_native_target());
    Printf.printf "Compiling %s to C callable\n%!" (string_of_stmt stmt);
    let (m, f) = codegen_to_c_callable c func in
    ignore(Llvm_bitwriter.write_bitcode_file m "generated.bc");
    Hashtbl.add compilation_cache func (m, f);
    (m, f)
  end
        
let _ = 
  Callback.register "makeIntImm" (fun a -> IntImm a);
  Callback.register "makeAdd" (fun a b -> Bop (Add, a, b));
  Callback.register "makeMul" (fun a b -> Bop (Mul, a, b));
  Callback.register "makeSub" (fun a b -> Bop (Sub, a, b));
  Callback.register "makeDiv" (fun a b -> Bop (Div, a, b));
  Callback.register "makeEQ" (fun a b -> Cmp (EQ, a, b));
  Callback.register "makeNE" (fun a b -> Cmp (NE, a, b));
  Callback.register "makeGT" (fun a b -> Cmp (GT, a, b));
  Callback.register "makeLT" (fun a b -> Cmp (LT, a, b));
  Callback.register "makeGE" (fun a b -> Cmp (GE, a, b));
  Callback.register "makeLE" (fun a b -> Cmp (LE, a, b));
  Callback.register "makeSelect" (fun c a b -> Select (c, a, b));
  Callback.register "doVectorize" (fun stmt var w -> vectorize_stmt stmt var w);
  Callback.register "makeVar" (fun a -> Var a);
  Callback.register "makeLoad" (fun buf idx -> Load (i32, {buf=buf; idx=idx}));
  Callback.register "makeStore" (fun a buf idx -> Store (a, {buf=buf; idx=idx}));
  Callback.register "makeFunction" (fun args stmt -> ((List.rev args), stmt));
  Callback.register "makeMap" (fun var min max stmt -> Map (var, min, max, stmt));
  Callback.register "doPrint" (fun a -> Printf.printf "%s\n%!" (string_of_stmt a));
  Callback.register "doCompile" compile;
  Callback.register "makeArgList" (fun _ -> []);
  Callback.register "makeBufferArg" (fun str -> Buffer str);
  Callback.register "addArgToList" (fun l x -> x::l);
  
  
