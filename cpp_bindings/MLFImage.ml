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
    Printf.printf "Compiling %s to C callable\n%!" (string_of_toplevel func);
    let (m, f) = codegen_to_c_callable c func in
    ignore(Llvm_bitwriter.write_bitcode_file m "generated.bc");
    Hashtbl.add compilation_cache func (m, f);
    (m, f)
  end
        
let _ = 
  (* Make IR nodes *)
  Callback.register "makeIntImm" (fun a -> IntImm a);
  Callback.register "makeFloatImm" (fun a -> FloatImm a);
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
  Callback.register "makeVar" (fun a -> Var (i32, a));
  Callback.register "makeLoad" (fun buf idx -> Load (f32, "." ^ buf, idx));
  Callback.register "makeStore" (fun a buf idx -> Store (a, "." ^ buf, idx));
  Callback.register "makeFor" (fun var min n stmt -> For (var, min, n, true, stmt));
  Callback.register "makePipeline" (fun name size produce consume -> Pipeline (name, f32, size, produce, consume));
  Callback.register "makeCall" (fun name args -> Call (f32, name, args));
  Callback.register "makeDefinition" (fun name argnames body -> Printf.printf "I got the name %s\n%!" name; (name, List.map (fun x -> (i32, x)) argnames, f32, Pure body));
  Callback.register "makeEnv" (fun _ -> Environment.empty);
  Callback.register "addDefinitionToEnv" (fun env def -> 
    let (n1, a1, t1, b1) = def in 
    let newenv = Environment.add n1 def env in 
    let (n2, a2, t2, b2) = Environment.find n1 newenv in 
    Printf.printf "definition: %s\n%!" n2; 
    newenv);

  Callback.register "makeList" (fun _ -> []);
  Callback.register "addToList" (fun l x -> x::l);  
  Callback.register "makePair" (fun x y -> (x, y));
  Callback.register "makeTriple" (fun x y z -> (x, y, z));
  
  Callback.register "makeBufferArg" (fun str -> Buffer ("." ^ str));

  (* Debugging, compilation *)
  Callback.register "doPrint" (fun a -> Printf.printf "%s\n%!" (string_of_stmt a));
  Callback.register "doLower" (fun (f:string) (sizes:int list) (env:environment) -> 
    let (_, args, _, _) = Environment.find f env in
    let region = List.map2 (fun (t, v) x -> Printf.printf "%s\n" v; (v, IntImm 0, IntImm x)) args sizes in
    let lowered = lower_function f env (make_default_schedule f env region) in
    let lowered = Break_false_dependence.break_false_dependence_stmt lowered in
    let lowered = Constant_fold.constant_fold_stmt lowered in
    lowered
  );
  Callback.register "doCompile" (fun a s -> compile a s);

  (* Transformations *)
  Callback.register "doVectorize" (fun var stmt -> vectorize_stmt var stmt);
  Callback.register "doUnroll" (fun var stmt -> unroll_stmt var stmt);
  Callback.register "doSplit" (fun var outer inner n stmt -> split_stmt var outer inner n stmt);
  Callback.register "doConstantFold" (fun stmt -> constant_fold_stmt stmt);



