open Ir
open Ir_printer
open Cg_llvm
open Llvm
open Llvm_executionengine
open Vectorize
open Unroll
open Split
open Constant_fold
open Inline

let compilation_cache = 
  Hashtbl.create 16

let compile f =

  (* Canonicalize names. Variables get renamed to v0 v1
     v2... depending on the order in which they're found, arguments to
     a0, a1, a2, depending on the order that they occur in the
     arguments list. This is done to assist hashing. *)

  (* TODO *)
  let func = 
    let canonicalize (args, stmt) = 
      let rec canonicalize_stmt = function
        | x -> x
      and canonicalize_expr = function
        | x -> x        
      in (args, canonicalize_stmt stmt)
    in (canonicalize f) in
  
  (* Next check the compilation cache *)

  let (args, stmt) = func in 
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
  Callback.register "makeVar" (fun a -> Var a);
  Callback.register "makeLoad" (fun buf idx -> Load (f32, buf, idx));
  Callback.register "makeStore" (fun a buf idx -> Store (a, buf, idx));
  Callback.register "makeFunction" (fun args stmt -> ((List.rev args), stmt));
  Callback.register "makeMap" (fun var min max stmt -> Map (var, min, max, stmt));
  Callback.register "makeLet" (fun name size produce consume -> Let (name, f32, size, produce, consume));
  Callback.register "makeCall" (fun name args -> Call (name, f32, args));
  Callback.register "makeDefinition" (fun name argnames body -> Printf.printf "I got the name %s\n%!" name; (name, argnames, f32, body));
  Callback.register "makeEnv" (fun _ -> Environment.empty);
  Callback.register "addDefinitionToEnv" (fun env def -> 
    let (n1, a1, t1, b1) = def in 
    let newenv = Environment.add n1 def env in 
    let (n2, a2, t2, b2) = Environment.find n1 newenv in 
    Printf.printf "definition: %s\n%!" n2; 
    newenv);
  Callback.register "makeStringList" (fun _ -> []);
  Callback.register "addStringToList" (fun l s -> s::l);
  Callback.register "makeExprList" (fun _ -> []);
  Callback.register "addExprToList" (fun l e -> e::l);

  Callback.register "makeArgList" (fun _ -> []);
  Callback.register "makeBufferArg" (fun str -> Buffer str);
  Callback.register "addArgToList" (fun l x -> x::l);


  (* Debugging, compilation *)
  Callback.register "doPrint" (fun a -> Printf.printf "%s\n%!" (string_of_stmt a));
  Callback.register "doCompile" compile;

  (* Transformations *)
  Callback.register "doVectorize" (fun var stmt -> vectorize_stmt var stmt);
  Callback.register "doUnroll" (fun var stmt -> unroll_stmt var stmt);
  Callback.register "doSplit" (fun var outer inner n stmt -> split_stmt var outer inner n stmt);
  Callback.register "doConstantFold" (fun stmt -> constant_fold_stmt stmt);
  (* Callback.register "doShift" (fun var expr stmt -> shift var expr stmt); *)
  Callback.register "doInline" (fun stmt env -> inline_stmt stmt env);
