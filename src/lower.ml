open Ir
open Ir_printer
open Schedule
open Analysis

type 'a option =
  | Option of 'a
  | None

let rec lower (stmt:stmt) (env:environment) (schedule:schedule_tree) =
  (* Find a function called in the statement. If none, terminate.
     Look up its schedule. 
       If Chunk:
         Allocate some storage
         producer = realize the function according to its schedule
         Inject producer at the appropriate place according to the call_schedule.
         Replace calls in consumer with loads
       If Inline:
         If Impure:
           For each call to this function
             Wrap the statement containing it in a Pipeline that produces then consumes it
         If Pure:
           Lookup the function body
           wrap it in lets to bind the arguments
           Subs it into the call site
       If dynamic:
         punt for now, seems possible
       If coiterate:         
         Generate scratch storage
         Split loop into unordered startup, ordered steady state, unordered wind down that use the scratch node


     To lookup a function body:
     Search the environment for the last element of the function name
     prefix all names in the body with (fully qualified callee function name + '.')
  *)

  let make_function_body (name:string) =
    let idx_after_last_dot =
      try (String.rindex name '.' + 1)
      with Not_found -> 0 in
    let fname = String.sub name idx_after_last_dot (String.length name - idx_after_last_dot) in 
    let (name, args, return_type, body) = Environment.find fname env in
    let prefix = name ^ "." in
    let renamed_args = List.map (fun (n, t) -> (prefix ^ n, t)) args in
    let renamed_body = match body with
        | Pure expr -> Pure (prefix_name_expr prefix expr)
        | Impure stmt -> Impure (prefix_name_stmt prefix stmt)
    in
    (renamed_args, return_type, renamed_body)
  in

  (* Find the name of any function referred to in a statement *)
  let rec find_callname_stmt stmt = 
    let combiner op1 op2 = match (op1, op2) with
      | (Option a, _) -> Option a
      | (None, b) -> b
    in
    let rec find_callname_expr = function
      | Call (name, _, _) -> Option name
      | x -> fold_children_in_expr find_callname_expr combiner None x
    in
    fold_children_in_stmt find_callname_expr find_callname_stmt combiner stmt
  in

  match find_callname_stmt stmt with
    | None -> stmt (* No more function calls. We're done lowering. *)
    | Option name -> (* Found a function call to pull in *)
      (* Grab the body *)
      let (args, return_type, body) = make_function_body name in
      
      Printf.printf "Found a function: %s = %s\n" name 
        (match body with 
          | Pure expr -> string_of_expr expr 
          | Impure stmt -> string_of_stmt stmt);

      let (call_sched, sched_list) = find_schedule schedule name in
      
      let call_sched_string = string_of_call_schedule call_sched in
      let sched_list_string = "[" ^ (String.concat ", " (List.map string_of_schedule sched_list)) ^ "]" in
      Printf.printf "It has schedule: %s %s\n" call_sched_string sched_list_string;

      match call_sched with
        | Chunk dim true -> 
          
        | _ -> raise (Wtf "I don't know how to schedule this yet")

      

(*
and realize (func:string) (env:environment) (schedule:schedule_tree) (output_buf_name:string) = 
  (* Wrap a call to the function in the appropriate schedule for the function *)
*)

