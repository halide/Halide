open Ir
open Ir_printer
open Schedule
open Analysis
open Util

let make_function_body (name:string) (env:environment) =
  let idx_after_last_dot =
    try (String.rindex name '.' + 1)
    with Not_found -> 0 in
  let fname = String.sub name idx_after_last_dot (String.length name - idx_after_last_dot) in 
  Printf.printf "Looking up %s in the environment\n%!" fname;
  let (_, args, return_type, body) = Environment.find fname env in
  Printf.printf "Found it\n%!";
  let prefix = name ^ "." in
  let renamed_args = List.map (fun (t, n) -> (t, prefix ^ n)) args in
  let renamed_body = match body with
    | Pure expr -> Pure (prefix_name_expr prefix expr)
    | Impure stmt -> Impure (prefix_name_stmt prefix stmt)
  in
  (renamed_args, return_type, renamed_body)

let make_schedule (name:string) (schedule:schedule_tree) =
  Printf.printf "Looking up %s in the schedule\n" name;
  let (call_sched, sched_list) = find_schedule schedule name in

  (* Prefix all the stuff in the schedule_list *)
  Printf.printf "Prefixing schedule entry\n";

  (* The prefix for this function *)
  let prefix = (name ^ ".") in

  (* The prefix for the calling context *)
  let caller = 
    if (String.contains name '.') then
      let last_dot = String.rindex name '.' in
      String.sub name 0 (last_dot+1) 
    else
      "" 
  in

  let prefix_expr = prefix_name_expr caller in
  let prefix_schedule = function      
    | Serial     (name, min, size) -> Serial     (prefix ^ name, prefix_expr min, prefix_expr size)
    | Parallel   (name, min, size) -> Parallel   (prefix ^ name, prefix_expr min, prefix_expr size)
    | Unrolled   (name, min, size) -> Unrolled   (prefix ^ name, prefix_expr min, size)
    | Vectorized (name, min, size) -> Vectorized (prefix ^ name, prefix_expr min, size)
    | Split (old_dim, new_dim_1, new_dim_2, offset) -> 
        Split (prefix ^ old_dim, prefix ^ new_dim_1, prefix ^ new_dim_2, prefix_expr offset)
  in
  let prefix_call_sched = function
    (* Refers to a var in the calling context *)
    | Chunk v -> 
        Chunk (caller ^ v)
    | x -> x
  in
  (prefix_call_sched call_sched, List.map prefix_schedule sched_list)

let rec lower_stmt (stmt:stmt) (env:environment) (schedule:schedule_tree) =

  let recurse s = lower_stmt s env schedule in

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

  (* Find the name of any function referred to in a statement *)
  let rec find_callname_stmt stmt = 
    let combiner op1 op2 = match (op1, op2) with
      | (Some a, _) -> Some a
      | (None, b) -> b
    in
    let rec find_callname_expr = function
      | Call (_, name, _) -> Some name
      | x -> fold_children_in_expr find_callname_expr combiner None x
    in
    fold_children_in_stmt find_callname_expr find_callname_stmt combiner stmt
  in

  match find_callname_stmt stmt with
    | None -> stmt (* No more function calls. We're done lowering. *)
    | Some name -> (* Found a function call to pull in *)

        Printf.printf "Found a call to %s\n" name;

        (* Grab the body *)
        let (args, return_type, body) = make_function_body name env in
        
        Printf.printf "Found a function: %s = %s\n" name 
          (match body with 
            | Pure expr -> string_of_expr expr 
            | Impure stmt -> string_of_stmt stmt);
        
        print_schedule schedule;

        let (call_sched, sched_list) = make_schedule name schedule in

        let scheduled_call =
          match call_sched with
            | Chunk chunk_dim -> begin
              (* A buffer to dump the intermediate result into *)
              let buffer_name = name ^ ".result" in          

              (* Make the strides list for indexing the buffer *)
              let strides = stride_list sched_list (List.map snd args) in

              (* Use the strides list to figure out the size of the intermediate buffer *)
              let sizes = List.map snd strides in
              let buffer_size = List.fold_left ( *~ ) (List.hd sizes) (List.tl sizes) in

              (* Generate a statement that evaluates the function over its schedule *)
              let produce = realize (args, return_type, body) sched_list buffer_name strides in

              Printf.printf "Realized chunk: %s\n" (string_of_stmt produce);

              Printf.printf "Looking for For over %s in %s\n" chunk_dim (string_of_stmt stmt);
              
              (* Recursively descend the statement until we get to the loop in question *)
              let rec inner substmt = match substmt with
                | For (for_dim, min, size, order, body) when for_dim = chunk_dim -> begin
                  (* Replace calls to function with loads from buffer in body *)
                  let rec replace_calls_in_expr = function
                    | Call (ty, func_name, args) when func_name = name ->
                        let index = List.fold_right2 
                          (fun arg (min,size) subindex -> size *~ subindex +~ arg -~ min) 
                          args strides (IntImm 0) in
                        Load (ty, buffer_name, index)
                    | x -> mutate_children_in_expr replace_calls_in_expr x
                  and replace_calls_in_stmt stmt = 
                    mutate_children_in_stmt replace_calls_in_expr replace_calls_in_stmt stmt in

                  let consume = replace_calls_in_stmt substmt in

                  (* If there are no calls to the function inside
                     body, we shouldn't be introducing a useless let *)
                  if consume = body then body else              
                    Pipeline (buffer_name, return_type, buffer_size, produce, consume)
                end
                | For (for_dim, min, size, order, body) -> 
                    For (for_dim, min, size, order, inner body)
                | Block l ->
                    Block (List.map inner l)
                | Pipeline (name, ty, size, produce, consume) ->
                    Pipeline (name, ty, size, inner produce, inner consume)
                | x -> x
              in inner stmt
            end

            | Root ->
                (* Make the strides list for indexing the output buffer *)
                let strides = stride_list sched_list (List.map snd args) in

                realize (args, return_type, body) sched_list "result" strides

            | Inline ->
                begin match body with 
                  | Pure expr ->
                      let rec inline_calls_in_expr = function
                        | Call (t, n, call_args) when n = name -> 
                          (* TODO: Check the types match (TODO: vectorize the function if necessary) *)
                            List.fold_left2 
                            (* Replace an argument with its value in e *)
                              (fun e (t, var) value -> subs_expr (Var (t, var)) value e)
                              expr args call_args 
                              
                        | x -> mutate_children_in_expr inline_calls_in_expr x
                      and inline_calls_in_stmt s =
                        mutate_children_in_stmt inline_calls_in_expr inline_calls_in_stmt s
                      in
                      inline_calls_in_stmt stmt
                  | Impure stmt -> raise (Wtf "I don't know how to inline impure functions yet")
                end
            | _ -> raise (Wtf "I don't know how to schedule this yet")
        in
        Printf.printf "\n-------\nResulting statement: %s\n-------\n" (string_of_stmt scheduled_call);
        recurse scheduled_call


(* Evaluate a function according to a schedule and put the results in
   the output_buf_name using the given strides *)
and realize (args, return_type, body) sched_list buffer_name strides = 
  match body with
    | Pure body ->
        (* Make the store index *)
        (* TODO, vars and function type signatures should have matching order *)
        let index = List.fold_right2 
          (fun arg (min,size) subindex -> (size *~ subindex) +~ (Var (i32, arg)) -~ min)
          (List.map snd args) 
          strides (IntImm 0) in

        (* Make the innermost store *)
        let inner_stmt = Store (body, buffer_name, index) in     
        
        (* Wrap it in for loops *)
        let wrap (stmt:stmt) = function 
          | Serial     (name, min, size) -> 
              For (name, min, size, true, stmt)
          | Parallel   (name, min, size) -> 
              For (name, min, size, false, stmt)
          | Unrolled   (name, min, size) -> 
              Unroll.unroll_stmt name (For (name, min, IntImm size, false, stmt))
          | Vectorized (name, min, size) -> 
              Vectorize.vectorize_stmt name (For (name, min, IntImm size, false, stmt))
          | Split (old_dim, new_dim_outer, new_dim_inner, offset) -> 
              let (_, size_new_dim_inner) = stride_for_dim new_dim_inner sched_list in
              let rec expand_old_dim_expr = function
                | Var (i32, dim) when dim = old_dim -> 
                    ((Var (i32, new_dim_outer)) *~ size_new_dim_inner) +~ 
                      (Var (i32, new_dim_inner)) +~ offset
                | x -> mutate_children_in_expr expand_old_dim_expr x 
              and expand_old_dim_stmt stmt = 
                mutate_children_in_stmt expand_old_dim_expr expand_old_dim_stmt stmt in
              expand_old_dim_stmt stmt in
        
        List.fold_left wrap inner_stmt sched_list

    | Impure body -> raise (Wtf "I don't know how to realize impure functions yet")            

and lower_function (func:string) (env:environment) (schedule:schedule_tree) =
  Printf.printf "Making function body\n%!";
  let (args, return_type, body) = make_function_body func env in
  Printf.printf "Making schedule\n%!";
  let (_, sched_list) = make_schedule func schedule in
  Printf.printf "Making stride list\n%!";
  let strides = stride_list sched_list (List.map snd args) in
  Printf.printf "Realizing initial statement\n%!";
  let core = (realize (args, return_type, body) sched_list ".result" strides) in
  Printf.printf "Recursively lowering function calls\n%!";
  lower_stmt core env schedule

