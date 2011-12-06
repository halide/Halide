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
    
    (* Printf.printf "Looking up %s in the context %s\n" var context; *)
    
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
  (* Printf.printf "Looking up %s in the schedule\n" name; *)
  let (call_sched, sched_list) = find_schedule schedule name in

  (* Prefix all the stuff in the schedule_list *)
  (* Printf.printf "Prefixing schedule entry\n"; *)

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

let rec lower_stmt (stmt:stmt) (env:environment) (schedule:schedule_tree) (functions:string list) (precomp: stmt list) (debug:bool) =
  let bounds_buffer_name = "_size" in
  match functions with
    | [] -> 
        (* Nearly done lowering, now inject all the precomputations of buffer sizes at the start *)
        let lt a b =
          (* does the values consumed by a require the value stored by b? *)
          let load_b = match b with
            | Store (expr, buf, idx) -> Load (i32, buf, idx)
            | _ -> raise (Wtf "found a statement in the precomputations list that isn't a store\n")                
          and load_a = match a with
            | Store (expr, buf, idx) -> Load (i32, buf, idx)
            | _ -> raise (Wtf "found a statement in the precomputations list that isn't a store\n")
          in 
          match (stmt_contains_expr load_b a, stmt_contains_expr load_a b) with
            | (false, false) -> None
            | (true, false) -> Some false
            | (false, true) -> Some true
            | (true, true) -> raise (Wtf "circular dependence found in precomputation list\n")
        in

        (* Print out all the sizes  *)
        let print_sizes = if debug then List.map
            (fun x -> Print ("_size ", [IntImm x; Load (i32, bounds_buffer_name, IntImm x)])) 
            (0 -- (List.length precomp))
          else []
        in

        let precomp = partial_sort lt precomp in

        let produce = Block (precomp @ print_sizes) in

        begin match produce with 
          | (Block []) -> stmt
          | _ ->
              Pipeline (bounds_buffer_name, Int 32, IntImm (List.length precomp), produce, stmt)
        end

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

        let rec fix_bounds get_bounds = function
          | [] -> []
          | (first::rest) -> begin match first with 
              | Serial (name, IntImm 0, IntImm 0) -> begin
                match get_bounds name with
                  | Bounds.Unbounded -> raise (Wtf ("Could not infer size of dimension " ^ name))
                  | Bounds.Range (min, max) -> Serial (name, min, Constant_fold.constant_fold_expr (max -~ min +~ IntImm 1))
              end :: (fix_bounds get_bounds rest)
              | Parallel (name, IntImm 0, IntImm 0) -> begin
                match get_bounds name with
                  | Bounds.Unbounded -> raise (Wtf ("Could not infer size of dimension " ^ name))
                  | Bounds.Range (min, max) -> Parallel (name, min, Constant_fold.constant_fold_expr (max -~ min +~ IntImm 1))
              end :: (fix_bounds get_bounds rest)
              | Split (name, outer, inner, IntImm 0) -> begin
                let needs_inference = 
                  match stride_for_dim outer sched_list with
                    | (IntImm 0, IntImm 0) -> true
                    | _ -> false
                in if needs_inference then
                    match get_bounds name with 
                      | Bounds.Unbounded -> raise (Wtf ("Could not infer size of dimension " ^ name))
                      | Bounds.Range (min, max) -> 
                          (* Inner should have an explicit bound, and we only need to infer outer *)
                          let (_, inner_size) = stride_for_dim inner sched_list in
                          assert (inner_size <> IntImm 0);
                          let one = IntImm 1 in
                          (* We'll stuff min into the offset
                             term. What is the maximum value of outer
                             we need to compute?. It looks like there
                             are +/- ones missing in this expression
                             but there aren't. They all cancel. *)
                          let outer_max = (max -~ min) /~ inner_size in
                          let new_get_bounds x =
                            if x = outer then 
                              (* Round outwards to nearest multiples of
                                 inner_size. To be robust to negative values,
                                 these expressions get a little hairy *)
                              Bounds.Range (IntImm 0, outer_max)
                            else get_bounds x
                          in
                          Split(name, outer, inner, min) :: (fix_bounds new_get_bounds rest)
                  else first :: (fix_bounds get_bounds rest)
              end
              | _ -> first :: (fix_bounds get_bounds rest)
          end 
        in

        let sched_list = fix_bounds (fun x -> snd (range_of_dim x)) sched_list in

        (* Pull nasty terms in the sched_list out so that they don't cascade when doing boundary inference *)
        let rec precompute_bounds (old_list: schedule list) (new_list: schedule list) (precomp: stmt list) (count: int) =
          let load1 = Load (Int 32, bounds_buffer_name, IntImm count) in
          let store1 x = Store (x, bounds_buffer_name, IntImm count) in
          let load2 = Load (Int 32, bounds_buffer_name, IntImm (count+1)) in
          let store2 x = Store (x, bounds_buffer_name, IntImm (count+1)) in
          let recurse1 rest s p = precompute_bounds rest (s::new_list) (p::precomp) (count+1) in
          let recurse2 rest s p1 p2 = precompute_bounds rest (s::new_list) (p1::p2::precomp) (count+2) in
          let should_not_lift = function
            | IntImm _ -> true
            | Load (_, _, IntImm _) -> true
            | expr -> 
                let rec only_globals = function
                  | Call (_, name, args) -> (List.for_all only_globals args) && ((String.get name 0) = '.')
                  | Var (_, name) -> (String.get name 0) = '.'
                  | expr -> fold_children_in_expr only_globals (&&) true expr
                in
                not (only_globals expr)
          in
          (* TODO: this will break if we have bounds that depend on a loop variable *)
          match old_list with
            (* Constant terms don't need lifting *)
            | [] -> (List.rev new_list, List.rev precomp)
            | (Unrolled (_, m, _))::rest
            | (Vectorized (_, m, _))::rest 
            | (Split (_, _, _, m))::rest when should_not_lift m ->
                precompute_bounds rest ((List.hd old_list)::new_list) precomp count                
            | (Parallel (_, m, s))::rest
            | (Serial (_, m, s))::rest when should_not_lift m && should_not_lift s ->
                precompute_bounds rest ((List.hd old_list)::new_list) precomp count                
            (* One term to lift *)
            | (Unrolled (n, m, s))::rest ->
                recurse1 rest (Unrolled (n, load1, s)) (store1 m)
            | (Vectorized (n, m, s))::rest ->
                recurse1 rest (Vectorized (n, load1, s)) (store1 m)
            | (Serial (n, m, s))::rest when should_not_lift s->
                recurse1 rest (Serial (n, load1, s)) (store1 m)
            | (Parallel (n, m, s))::rest when should_not_lift s->
                recurse1 rest (Parallel (n, load1, s)) (store1 m)
            | (Serial (n, m, s))::rest when should_not_lift m ->
                recurse1 rest (Serial (n, m, load1)) (store1 s)
            | (Parallel (n, m, s))::rest when should_not_lift m ->
                recurse1 rest (Parallel (n, m, load1)) (store1 s)
            | (Split (a, b, c, m))::rest ->
                recurse1 rest (Split (a, b, c, load1)) (store1 m)
            (* Two terms to lift *)
            | (Parallel (n, m, s))::rest ->
                recurse2 rest (Parallel (n, load1, load2)) (store1 m) (store2 s)
            | (Serial (n, m, s))::rest ->
                recurse2 rest (Serial (n, load1, load2)) (store1 m) (store2 s)
        in
        let (sched_list, precomp) = precompute_bounds sched_list [] precomp (List.length precomp) in 

        Printf.printf "Lowering function %s with call_sched %s\n%!" name (string_of_call_schedule call_sched);

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

          (* Wrap the pair into a pipeline object *)
          let pipeline = Pipeline (buffer_name, return_type, buffer_size, produce, consume) in

          (* Do a constant-folding pass 
          let pipeline = Constant_fold.constant_fold_stmt pipeline in *)
                      
          (* Maybe wrap the pipeline in some debugging code *)
          let pipeline = if debug then
              let sizes = List.fold_left 
                (fun l (a, b) -> a::b::l)     
                [] (List.rev strides) in   
              Block [Print ("### Allocating " ^ name ^ " over ", sizes); 
                     pipeline; 
                     Print ("### Discarding " ^ name ^ " over ", sizes)]
            else
              pipeline 
          in 
          
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
        Printf.printf "\n-------\nResulting statement: %s\n-------\n" (string_of_stmt scheduled_call);  
        lower_stmt scheduled_call env schedule rest precomp debug



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
  (* Printf.printf "Realization order: %s\n" (String.concat "\n" functions); *)

  Printf.printf "Making root function body\n%!";
  let (args, return_type, body) = make_function_body func env debug in
  Printf.printf "Making root schedule\n%!";
  let (_, sched_list) = make_schedule func schedule in
  Printf.printf "Making root stride list\n%!";
  let strides = stride_list sched_list (List.map snd args) in
  Printf.printf "Realizing root statement\n%!";
  let core = (realize (func, args, return_type, body) sched_list ".result" strides debug) in
  Printf.printf "Recursively lowering function calls\n%!";
  let stmt = lower_stmt core env schedule (List.filter (fun x -> x <> func) functions) [] debug in

  stmt

  (*
    TODO: move this into lower where bounds are inferred 
    
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
  *)




