open Ir
open Ir_printer
open Schedule
open Analysis
open Util
open Vectorize



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
        
let make_function_body (name:string) (env:environment) (debug:bool) =
  let (args, return_type, body) = find_function name env in
  let prefix = name ^ "." in
  let renamed_args = List.map (fun (t, n) -> (t, prefix ^ n)) args in
  let renamed_body = match body with
    | Extern -> raise (Wtf ("Can't lookup body of extern function: " ^ name))
    | Pure expr -> 
        let expr = if debug then
            Debug (expr, "### Evaluating " ^ name ^ " at ", List.map (fun (t, n) -> Var (t, n)) renamed_args) 
          else expr in
        Pure (prefix_name_expr prefix expr)
    | Impure (expr, modified_args, modified_val) -> 
        let expr = if debug then
            Debug (expr, "### Initializing " ^ name ^ " at ", List.map (fun (t, n) -> Var (t, n)) renamed_args) 
          else expr in
        let modified_val = if debug then 
            Debug (modified_val, "### Modifying " ^ name ^ " at ", modified_args) 
          else modified_val in
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


let simplify_range = function
  | Bounds.Unbounded -> Bounds.Unbounded
  | Bounds.Range (a, b) -> Bounds.Range (Constant_fold.constant_fold_expr a, Constant_fold.constant_fold_expr b)
let simplify_region r = List.map simplify_range r 
let string_of_range = function  
  | Bounds.Unbounded -> "[Unbounded]"
  | Bounds.Range (a, b) -> "[" ^ string_of_expr a ^ ", " ^ string_of_expr b ^ "]" 
let bounds f stmt = simplify_region (Bounds.required_of_stmt f (StringMap.empty) stmt) 

let rec replace_calls_with_loads_in_expr function_name buffer_name strides debug = function
  | Call (ty, func_name, args) when func_name = function_name ->
      let index = List.fold_right2 
        (fun arg (min,size) subindex -> size *~ subindex +~ arg -~ min) 
        args strides (IntImm 0) in
      let load = Load (ty, buffer_name, index) in
      if debug then
        Debug (load, "### Loading " ^ function_name ^ " at ", args)
      else
        load
  | x -> mutate_children_in_expr (replace_calls_with_loads_in_expr function_name buffer_name strides debug) x

let rec replace_calls_with_loads_in_stmt function_name buffer_name strides debug stmt = 
  mutate_children_in_stmt 
    (replace_calls_with_loads_in_expr function_name buffer_name strides debug)
    (replace_calls_with_loads_in_stmt function_name buffer_name strides debug) 
    stmt

let rec lower_stmt (stmt:stmt) (env:environment) (schedule:schedule_tree) (functions:string list) (debug:bool) =
  match functions with
    | [] -> stmt (* Done lowering *)
    | (name::rest) ->

        (* Grab the body of the function call *)
        let (args, return_type, body) = make_function_body name env debug in
        
        (* Grab the schedule for function call *)
        let (call_sched, sched_list) = make_schedule name schedule in

        (* Complete the sched_list (it may not contain explicit bounds for some dimensions) *)
        let region_used = bounds name stmt in
        let range_of_dim d =
          List.find (fun (dim, _) -> d = dim) (list_zip (List.map snd args) region_used)
        in

        let fix_bounds = function
          | Serial (name, IntImm 0, IntImm 0) -> begin
            match snd (range_of_dim name) with
              | Bounds.Unbounded -> raise (Wtf ("Could not infer size of dimension " ^ name))
              | Bounds.Range (min, max) -> Serial (name, min, max -~ min +~ IntImm 1)
          end
          | Parallel (name, IntImm 0, IntImm 0) -> begin
            match snd (range_of_dim name) with
              | Bounds.Unbounded -> raise (Wtf ("Could not infer size of dimension " ^ name))
              | Bounds.Range (min, max) -> Parallel (name, min, max -~ min +~ IntImm 1)
          end
          | x -> x
        in

        let sched_list = List.map fix_bounds sched_list in

        Printf.printf "Lowering function %s with call_sched %s\n" name (string_of_call_schedule call_sched);

        (* Make a function to use for scheduling chunks and roots *)
        let make_pipeline stmt = 
          (* Make a name for the output buffer *)
          let buffer_name = name ^ ".result" in
          (* Figure out how to index it *)
          let strides = stride_list sched_list (List.map snd args) in
          (* Figure out how much storage to allocate for it *)
          let sizes = List.map snd strides in
          let buffer_size = List.fold_left ( *~ ) (IntImm 1) sizes in                      
          (* Generate a statement that evaluates the function over its schedule *)
          let produce = realize (name, args, return_type, body) sched_list buffer_name strides debug in
          (* Generate the code that consumes the result by replacing calls with loads *)
          let consume = replace_calls_with_loads_in_stmt name buffer_name strides debug stmt in 
          (* let consume = stmt in *)
          (* Wrap the pair into a pipeline object *)
          let pipeline = Pipeline (buffer_name, return_type, buffer_size, produce, consume) in
                      
          (* Maybe wrap the pipeline in some debugging code *)
          if false then
            let sizes = List.fold_left
              (fun l (a, b) -> a::b::l)    
              [] (List.rev strides) in  
            Block [pipeline; Print ("### Discarding " ^ name ^ " over ", sizes)]
          else
            pipeline
        in

        let scheduled_call = 
          match call_sched with
            | Chunk chunk_dim -> begin
              (* Recursively descend the statement until we get to the loop in question *)
              let rec inner = function
                | For (for_dim, min, size, order, for_body) when for_dim = chunk_dim -> 
                    For (for_dim, min, size, order, make_pipeline for_body)
                | stmt -> mutate_children_in_stmt (fun x -> x) inner stmt
              in inner stmt
            end
            | Root -> make_pipeline stmt
            | Inline ->
                (* Just replace all calls to the function with the body of the function *)
                begin match body with 
                  | Extern -> raise (Wtf ("Can't inline extern function " ^ name))
                  | Impure _ -> raise (Wtf ("Can't inline impure function " ^ name))
                  | Pure expr ->
                      let rec inline_calls_in_expr = function
                        | Call (t, n, call_args) when n = name -> 
                            (* TODO: Check the types match *)
                            List.fold_left2 
                              (* Replace an argument with its value in e, possibly vectorizing as we go for i32 args *)
                              (fun e (t, var) value -> 
                                if (t = i32) then vector_subs_expr var value e 
                                else subs_expr (Var (t, var)) value e)
                              expr args call_args                               
                        | x -> mutate_children_in_expr inline_calls_in_expr x
                      and inline_calls_in_stmt s =
                        mutate_children_in_stmt inline_calls_in_expr inline_calls_in_stmt s
                      in
                      inline_calls_in_stmt stmt                        
                end
            | Reuse s -> begin
              (* Replace calls to name with calls to s in stmt *)
              let rec fix_expr = function
                | Call (ty, n, args) when n = name -> 
                    Call (ty, s, args)
                | expr -> mutate_children_in_expr fix_expr expr
              in
              let rec fix_stmt stmt = mutate_children_in_stmt fix_expr fix_stmt stmt in
              fix_stmt stmt
            end
            | _ -> raise (Wtf "I don't know how to schedule this yet")
        in
        (* Printf.printf "\n-------\nResulting statement: %s\n-------\n" (string_of_stmt scheduled_call); *)
        lower_stmt scheduled_call env schedule rest debug



(* Evaluate a function according to a schedule and put the results in
   the output_buf_name using the given strides *)
and realize (name, args, return_type, body) sched_list buffer_name strides debug = 
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
    | Call (ty, n, args) ->
        begin match List.rev (split_name n) with 
          | a::b::_ when a = b ->
              Load (ty, buffer_name, make_buffer_index args)        
          | x -> Call (ty, n, args)
        end
    | x -> mutate_children_in_expr remove_recursion x
  in
  
  let produce =
    match body with
      | Extern -> raise (Wtf ("Can't lower extern function call " ^ name))          
      | Pure body ->
          let inner_stmt = Store (remove_recursion body, buffer_name, index) in
          List.fold_left wrap inner_stmt sched_list
      | Impure (initial_value, modified_args, modified_val) ->
          (* Remove recursive references *)
          let initial_value = remove_recursion initial_value in
          let modified_idx = make_buffer_index (List.map remove_recursion modified_args) in
          Printf.printf "index: %s\n%!" (string_of_expr modified_idx);
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
                      if StringSet.mem name arg_set then
                        partition_sched_list (arg_set, first::arg_sched_list, free_sched_list, rest)
                      else
                        partition_sched_list (arg_set, arg_sched_list, first::free_sched_list, rest)
                  | Split (name, outer, inner, _) ->
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
  in 

  if debug then 
    let sizes = List.fold_left
      (fun l (a, b) -> a::b::l)    
      [] (List.rev strides) in  
    Block [Print ("### Realizing " ^ name ^ " over ", sizes); produce]
  else
    produce

let rec topological_sort = function
  | Pipeline (n1, t1, s1, produce, consume) as pipeline ->
      (* Pull a nested chain of pipelines into a list *)
      let rec listify_pipeline_chain = function
        | Pipeline (n, t, s, p, c) ->
            let (list, tail) = listify_pipeline_chain c in
            ((n, t, s, p) :: list, tail)
        | x -> ([], x)
      in
      let (chain, tail) = listify_pipeline_chain pipeline in

      (* Do a selection-sort by repeatedly looking for an element that
         does not depend on anything else in the todo list *)

      let depends (_, _, s1, p1) (n2, _, _, _) = 
        (* Does a reference to n2 appear in p1 or s1? *)
        let rec expr_mutator = function
          | Load (_, buf, _) when buf = n2 -> true
          | x -> fold_children_in_expr expr_mutator (or) false x
        and stmt_mutator x = fold_children_in_stmt expr_mutator stmt_mutator (or) x
        in (stmt_mutator p1) or (expr_mutator s1)
      in

      let rec selection_sort = function
        | (first::rest) ->
            let rec find_first elems_before elem elems_after = 
              let dep_before = List.fold_left (fun x y -> x or (depends elem y)) false elems_before in
              let dep_after = List.fold_left (fun x y -> x or (depends elem y)) false elems_after in                  
              if dep_before or dep_after then
                match elems_after with
                  | (first::rest) -> find_first (elem::elems_before) first rest
                  | [] -> raise (Wtf "Circular dependency detected")
              else
                let (n, t, s, p) = elem in
                Pipeline (n, t, s, p, selection_sort (elems_before @ elems_after))
            in find_first [] first rest 
        | [] -> tail
      in

      selection_sort chain
  | x -> mutate_children_in_stmt (fun x -> x) topological_sort x


(* TODO: @jrk Make debug an optional arg? *)
let lower_function (func:string) (env:environment) (schedule:schedule_tree) (debug:bool) =
  let starts_with a b =    
    String.length a >= String.length b &&
      String.sub a 0 (String.length b) = b
  in
  (* A partial order on realization order of functions *)        
  let lt a b =
    (* If a requires b directly, a should be realized first *)
    if (starts_with b a) then (Some true)
    else if (starts_with a b) then (Some false)
    else
      let (call_sched_a, _) = (find_schedule schedule a) in
      let (call_sched_b, _) = (find_schedule schedule b) in
      (* If a reuses the computation of b, a should be 'realized' first (which just rewrites calls to a into calls to b) *)
      if (call_sched_a = Reuse b) then (Some true)
      else if (call_sched_b = Reuse a) then (Some false)
      else None
  in

  let functions = partial_sort lt (list_of_schedule schedule) in
  Printf.printf "Realization order: %s\n" (String.concat "\n" functions);

  Printf.printf "Making root function body\n%!";
  let (args, return_type, body) = make_function_body func env debug in
  Printf.printf "Making root schedule\n%!";
  let (_, sched_list) = make_schedule func schedule in
  Printf.printf "Making root stride list\n%!";
  let strides = stride_list sched_list (List.map snd args) in
  Printf.printf "Realizing root statement\n%!";
  let core = (realize (func, args, return_type, body) sched_list ".result" strides debug) in
  Printf.printf "Recursively lowering function calls\n%!";
  let stmt = lower_stmt core env schedule (List.filter (fun x -> x <> func) functions) debug in

  stmt

  (*
  Printf.printf "Static bounds checking\n%!";
  let simplify_range = function
    | Bounds.Unbounded -> Bounds.Unbounded
    | Bounds.Range (a, b) -> Bounds.Range (Constant_fold.constant_fold_expr a, Constant_fold.constant_fold_expr b)
  in
  let simplify_region r = List.map simplify_range r in 
  let string_of_range = function  
      | Bounds.Unbounded -> "[Unbounded]"
      | Bounds.Range (a, b) -> "[" ^ string_of_expr a ^ ", " ^ string_of_expr b ^ "]" 
  in  
  let bounds f = simplify_region (Bounds.required_of_stmt f (StringMap.empty) stmt) in
  let check f = function
    | [] -> ()
    | region_used ->
        Printf.printf "This region of %s is used: %s\n%!"  f
          (String.concat "*" (List.map string_of_range region_used))
  in

  List.iter (fun f -> check f (bounds f)) functions;
  Printf.printf "Replacing calls with loads\n%!";

  
  let lower_calls stmt f =
    (* TODO: somewhat inefficient to rebuild the function bodies and schedules here *)
    let (args, _, _) = make_function_body f env debug in
    let (_, sched_list) = make_schedule f schedule in 
    if sched_list = [] then 
      stmt 
    else begin
      let strides = stride_list sched_list (List.map snd args) in 
      Printf.printf "Replacing calls to %s with loads using strides %s\n" f (String.concat ", " (List.map (fun (x, y) -> ("[" ^ string_of_expr x ^ ", " ^ string_of_expr y ^ "]")) strides));
      replace_calls_with_loads_in_stmt f (f ^ ".result") strides debug stmt
    end
  in

  List.fold_left lower_calls stmt functions
  *)




