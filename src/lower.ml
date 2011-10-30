open Ir
open Ir_printer
open Schedule
open Analysis
open Util
open Vectorize

module StringSet = Set.Make (
  struct
    let compare = Pervasives.compare
    type t = string
  end
)

(* Lowering produces references to names either in the context of the
   callee or caller. The ones in the context of the caller may in fact
   refer to ones in the caller of the caller, or so on up the
   chain. E.g. f.g.x actually refers to f.x if g does not provide an x
   in its schedule. *)
let rec resolve_name (context: string) (var: string) (schedule: schedule_tree) =
  
  (* names starting with . are global, and so are already resolved *)
  if (String.get var 0 = '.') then var else begin
    
    Printf.printf "Looking up %s in the context %s\n" var context;
    
    let (call_sched, sched_list) = find_schedule schedule context in
    
    let provides_name = function
      | Serial (n, _, _)
      | Parallel (n, _, _)
      | Vectorized (n, _, _)
      | Split (n, _, _, _)
      | Unrolled (n, _, _) when n = var -> true
      | _ -> false
    in
    
    let found = List.fold_left (or) false (List.map provides_name sched_list) in
    
    if found then
      (context ^ "." ^ var)
    else 
      (* Cut the last item off the context and recurse *)
      let last_dot =
        try (String.rindex context '.')
        with Not_found -> raise (Wtf ("Could not resolve name " ^ var ^ " in " ^ context))
      in    
      let parent = String.sub context 0 last_dot in
      resolve_name parent var schedule      
  end
        
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
    | Impure (expr, modified_args, modified_val) -> 
        Impure (prefix_name_expr prefix expr,
                List.map (prefix_name_expr prefix) modified_args, 
                prefix_name_expr prefix modified_val)
  in
  (renamed_args, return_type, renamed_body)

let make_schedule (name:string) (schedule:schedule_tree) =
  Printf.printf "Looking up %s in the schedule\n" name;
  let (call_sched, sched_list) = find_schedule schedule name in

  (* Prefix all the stuff in the schedule_list *)
  Printf.printf "Prefixing schedule entry\n";

  (* The prefix for this function *)
  let prefix x = (name ^ "." ^ x) in

  (* The prefix for the calling context *)
  let caller = 
    if (String.contains name '.') then
      let last_dot = String.rindex name '.' in
      String.sub name 0 last_dot 
    else
      "" 
  in

  let rec prefix_expr = function
    | Var (t, n) -> Var (t, resolve_name caller n schedule)
    | x -> mutate_children_in_expr prefix_expr x
  in

  let prefix_schedule = function      
    | Serial     (name, min, size) -> Serial     (prefix name, prefix_expr min, prefix_expr size)
    | Parallel   (name, min, size) -> Parallel   (prefix name, prefix_expr min, prefix_expr size)
    | Unrolled   (name, min, size) -> Unrolled   (prefix name, prefix_expr min, size)
    | Vectorized (name, min, size) -> Vectorized (prefix name, prefix_expr min, size)
    | Split (old_dim, new_dim_1, new_dim_2, offset) -> 
        Split (prefix old_dim, prefix new_dim_1, prefix new_dim_2, prefix_expr offset)
  in
  let prefix_call_sched = function
    (* Refers to a var in the calling context *)
    | Chunk v -> 
        Chunk (resolve_name caller v schedule)
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
            | Impure (initial_value, modified_args, modified_value) ->
                (string_of_expr initial_value) ^ "\n[" ^ 
                  (String.concat ", " (List.map string_of_expr modified_args)) ^ "] <- " ^
                  (string_of_expr modified_value));
        
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
              let produce = realize (name, args, return_type, body) sched_list buffer_name strides in

              Printf.printf "Realized chunk: %s\n" (string_of_stmt produce);

              Printf.printf "Looking for For over %s in %s\n" chunk_dim (string_of_stmt stmt);
              (* TODO: what if the chunk dimension has been split? Is
                 this allowed? If so we should chunk over the outer
                 variable produced *)
              
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

                realize (name, args, return_type, body) sched_list "result" strides

            | Inline ->
                begin match body with 
                  | Pure expr ->
                      let rec inline_calls_in_expr = function
                        | Call (t, n, call_args) when n = name -> 
                            (* TODO: Check the types match *)
                            List.fold_left2 
                              (* Replace an argument with its value in e, possibly vectorizing as we go for i32 args *)
                              (fun e (t, var) value -> 
                                if (t = i32) then 
                                  vector_subs_expr var value e 
                                else
                                  subs_expr (Var (t, var)) value e)
                              expr args call_args 
                              
                        | x -> mutate_children_in_expr inline_calls_in_expr x
                      and inline_calls_in_stmt s =
                        mutate_children_in_stmt inline_calls_in_expr inline_calls_in_stmt s
                      in
                      inline_calls_in_stmt stmt
                  | Impure _ -> raise (Wtf "I don't know how to inline impure functions yet")
                end
            | Reuse s -> begin
                let (call_sched, sched_list) = make_schedule s schedule in
                match call_sched with
                  | Chunk dim -> begin
                    (* Replace calls to name in stmt with loads from s.result *)

                    (* Grab the arg names of the parent in order to make the strides list *)
                    let (args, _, _) = make_function_body s env in

                    (* Make the strides list for indexing the buffer *)
                    let strides = stride_list sched_list (List.map snd args) in
                    let buffer_name = s ^ ".result" in
                    let rec replace_calls_in_expr = function
                      | Call (ty, func_name, args) when func_name = name ->
                          let index = List.fold_right2 
                            (fun arg (min,size) subindex -> size *~ subindex +~ arg -~ min) 
                            args strides (IntImm 0) in
                          Load (ty, buffer_name, index)
                      | x -> mutate_children_in_expr replace_calls_in_expr x
                    and replace_calls_in_stmt stmt = 
                      mutate_children_in_stmt replace_calls_in_expr replace_calls_in_stmt stmt in                    
                    replace_calls_in_stmt stmt

                  end
                  | _ -> raise (Wtf ("Can't reuse something with call schedule " ^ (string_of_call_schedule call_sched)))
            end
            | _ -> raise (Wtf "I don't know how to schedule this yet")
        in
        Printf.printf "\n-------\nResulting statement: %s\n-------\n" (string_of_stmt scheduled_call);
        recurse scheduled_call

(* Evaluate a function according to a schedule and put the results in
   the output_buf_name using the given strides *)
and realize (name, args, return_type, body) sched_list buffer_name strides = 
  (* Make the store index *)
  (* TODO, vars and function type signatures should have matching order *)
  let make_buffer_index args =
    List.fold_right2 
      (fun arg (min,size) subindex -> (size *~ subindex) +~ arg -~ min)
      args strides (IntImm 0) in
  let index = make_buffer_index (List.map (fun (t, v) -> Var (t, v)) args) in

  (* Wrap a statement in for loops using a schedule *)
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
        expand_old_dim_stmt stmt 
  in
  
  (* Deal with recursion by replacing calls to the same
     function in the realized body with loads from the buffer *)
  let rec remove_recursion = function
    | Call (ty, n, args) when n = name ->
        Load (ty, buffer_name, make_buffer_index args)
    | x -> mutate_children_in_expr remove_recursion x
  in

  match body with
    | Pure body ->
        let inner_stmt = Store (remove_recursion body, buffer_name, index) in
        List.fold_left wrap inner_stmt sched_list
    | Impure (initial_value, modified_args, modified_val) ->
        (* Remove recursive references *)
        let initial_value = remove_recursion initial_value in
        let modified_idx = make_buffer_index (List.map remove_recursion modified_args) in
        let modified_val = remove_recursion modified_val in

        (* Split the sched_list into terms refering to the initial
           value, and terms refering to free vars *)

        (* Args are: Var names refering to function args, sched_list
           for function args, sched_list for free vars, sched_list
           remaining to be processed *)
        let rec partition_sched_list = function
          | (_, arg_sched_list, free_sched_list, []) -> (List.rev arg_sched_list, List.rev free_sched_list)
          | (arg_set, arg_sched_list, free_sched_list, first::rest) ->
              begin match first with 
                | Serial (name, _, _)
                | Parallel (name, _, _) 
                | Unrolled (name, _, _) 
                | Vectorized (name, _, _) ->
                    Printf.printf "Looking up %s in arg set\n%!" name;
                    if StringSet.mem name arg_set then
                      partition_sched_list (arg_set, first::arg_sched_list, free_sched_list, rest)
                    else
                      partition_sched_list (arg_set, arg_sched_list, first::free_sched_list, rest)
                | Split (name, outer, inner, _) ->
                    Printf.printf "Looking up %s in arg set\n%!" name;
                    if StringSet.mem name arg_set then
                      let arg_set = StringSet.add outer arg_set in
                      let arg_set = StringSet.add inner arg_set in
                      partition_sched_list (arg_set, first::arg_sched_list, free_sched_list, rest)
                    else
                      partition_sched_list (arg_set, arg_sched_list, first::free_sched_list, rest)
              end
        in 

        let arg_set = List.fold_right (StringSet.add) (List.map snd args) StringSet.empty in
        let (arg_sched_list, free_sched_list) = partition_sched_list (arg_set, [], [], sched_list) in

        (* Wrap the initial value in for loops corresponding to the function args *)
        let inner_init_stmt = Store (initial_value, buffer_name, index) in
        let init_wrapped = List.fold_left wrap inner_init_stmt arg_sched_list in
        
        (* Wrap the modifier in for loops corresponding to the free vars in modified_idx and modified_val *)
        let inner_modifier_stmt = Store (modified_val, buffer_name, modified_idx) in
        let modifier_wrapped = List.fold_left wrap inner_modifier_stmt free_sched_list in
        
        (* Put them in a block *)
        Block [init_wrapped; modifier_wrapped]

let lower_function (func:string) (env:environment) (schedule:schedule_tree) =
  Printf.printf "Making function body\n%!";
  let (args, return_type, body) = make_function_body func env in
  Printf.printf "Making schedule\n%!";
  let (_, sched_list) = make_schedule func schedule in
  Printf.printf "Making stride list\n%!";
  let strides = stride_list sched_list (List.map snd args) in
  Printf.printf "Realizing initial statement\n%!";
  let core = (realize (func, args, return_type, body) sched_list ".result" strides) in
  Printf.printf "Recursively lowering function calls\n%!";
  lower_stmt core env schedule    
