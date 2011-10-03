open Ir
open List
open Map
open Analysis
open Ir
open Printf
open Ir_printer

(* Generate a unique name for each call site *)
let name_counter = ref 0 
let mkname () = name_counter := (!name_counter)+1; string_of_int (!name_counter)

(* inline everything - replace all calls with inlined versions of the function in question *)
let rec inline_stmt (stmt : stmt) (env : environment) =
  (* Transform a statement using each call provided *)
  let rec xform stmt (calls : (string * val_type * (expr list)) list) = begin 
    (* TODO: factor this out into the subsitution part and the enclose part *)
    match calls with
      | (name, ty, args) :: rest ->
        let tmpname = "C" ^ (mkname ()) ^ "_" in
        (* Lookup the function type signature and body in the environment *)
        let (_, argnames, t, f_body) = Environment.find name env in
        printf "argnames: %s\n" (String.concat ", " argnames);
        printf "body: %s\n" (string_of_stmt f_body);
        (* prefix body var names with callsite prefix to prevent nonsense when inserting arguments *)        
        let prefixed = List.fold_left (fun s x -> subs_name_stmt x (tmpname ^ x) s) f_body argnames in
        printf "prefixed: %s\n" (string_of_stmt prefixed);
        (* subs body var names for args *)
        let substituted = List.fold_left2 (fun s x e -> subs_stmt (Var (i32, tmpname ^ x)) e s) prefixed argnames args in
        printf "substituted: %s\n" (string_of_stmt substituted);
        (* subs tmpname for result *)
        let result = subs_name_stmt "result" (tmpname ^ "result") substituted in
        Printf.printf "result renamed: %s\n%!" (Ir_printer.string_of_stmt result); 
        (* Recursively precompute the rest of the calls *)
        let recurse = xform stmt rest in
        (* Replace the call to this function in the current expressions with a load *)          
        let newstmt = subs_stmt (Call (name, ty, args)) (Load (ty, tmpname ^ "result", IntImm 0)) recurse in
        (* Return the statement wrapped in a pipeline *)
        Pipeline(tmpname ^ "result", ty, IntImm 1, inline_stmt result env, newstmt)
      | [] -> stmt end
  in 

  match stmt with 
    | Map (name, min, max, body) -> 
      let newbody = inline_stmt body env in
      let calls = (find_calls_in_expr min) @ (find_calls_in_expr max) in
      xform (Map(name, min, max, newbody)) calls
    | Store (e, buf, idx) ->
      let calls = (find_calls_in_expr e) @ (find_calls_in_expr idx) in
      xform stmt calls
    | Block l -> Block (List.map (fun x -> inline_stmt x env) l)
    | Pipeline (name, ty, size, produce, consume) ->
      let newproduce = inline_stmt produce env in
      let newconsume = inline_stmt consume env in
      let calls = find_calls_in_expr size in
      xform (Pipeline(name, ty, size, newproduce, newconsume)) calls

and find_calls_in_stmt = function 
  | Map (var, min, max, body) -> 
    (find_calls_in_expr min) @ (find_calls_in_expr max) @ (find_calls_in_stmt body)
  | Store (v, buf, idx) -> 
    (find_calls_in_expr v) @ (find_calls_in_expr idx)
  | Block l -> 
    List.concat (List.map find_calls_in_stmt l)
  | Pipeline (name, ty, size, produce, consume) -> 
    (find_calls_in_expr size) @ (find_calls_in_stmt produce) @ (find_calls_in_stmt consume)  

and find_calls_in_expr = function
  | Call (name, ty, args) -> 
    (name, ty, args) :: (List.concat (List.map find_calls_in_expr args))
  | Cast (_, a) | Not a | Load (_, _, a) | Broadcast (a, _) ->
    find_calls_in_expr a
  | Bop (_, a, b) | Cmp(_, a, b) | And (a, b) | Or (a, b) | ExtractElement (a, b) | Ramp (a, b, _) ->
    (find_calls_in_expr a) @ (find_calls_in_expr b)
  | Select (c, a, b) -> 
    (find_calls_in_expr c) @ (find_calls_in_expr a) @ (find_calls_in_expr b)
  | MakeVector l ->
    List.concat (List.map find_calls_in_expr l)        
  | x -> []


  
  
