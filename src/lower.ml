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

let simplify_range = function
  | Bounds.Unbounded -> Bounds.Unbounded
  | Bounds.Range (a, b) -> Bounds.Range (Constant_fold.constant_fold_expr a, Constant_fold.constant_fold_expr b)
let simplify_region r = List.map simplify_range r 
let string_of_range = function  
  | Bounds.Unbounded -> "[Unbounded]"
  | Bounds.Range (a, b) -> "[" ^ string_of_expr a ^ ", " ^ string_of_expr b ^ "]" 
let bounds f stmt = simplify_region (Bounds.required_of_stmt f (StringMap.empty) stmt) 

let rec lower_stmt (stmt:stmt) (env:environment) (schedule:schedule_tree) (functions:string list) (debug:bool) =
  match functions with
    | [] -> stmt
    | (name::rest) ->        

        (* Grab the body of the function call *)
        let (args, return_type, body) = make_function_body name env debug in
        
        let arg_names = List.map snd args in

        (* Grab the schedule for function call *)
        let (call_sched, sched_list) = find_schedule schedule name in

        Printf.printf "Lowering function %s with schedule %s [%s]\n%!" name 
          (string_of_call_schedule call_sched)
          (String.concat ", " (List.map string_of_schedule sched_list));

        (* Make a function to use for scheduling chunks and roots *)
        let make_pipeline stmt = 
          (* Make a name for the output buffer *)
          let buffer_name = name ^ ".result" in
          (* Figure out how to index it *)
          let strides = stride_list sched_list arg_names in
          (* Figure out how much storage to allocate for it *)
          let sizes = List.map snd strides in
          let buffer_size = List.fold_left ( *~ ) (IntImm 1) sizes in                      
          (* Generate a statement that evaluates the function over its schedule *)
          let produce = realize (name, args, return_type, body) sched_list buffer_name strides debug in
          (* Generate the code that consumes the result by replacing calls with loads *)
          (* let consume = replace_calls_with_loads_in_stmt name buffer_name strides debug stmt in  *)
          let consume = stmt in

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
                            let call_args = List.map inline_calls_in_expr call_args in
 
                            (* If the args have vector type, promote
                               the function body to also have vector type
                               in its args *)

                            let widths = List.map (fun x -> vector_elements (val_type_of_expr x)) call_args in
                            let width = List.hd widths in (* Do we have pure functions with no arguments? *)
                            assert (List.for_all (fun x -> x = 1 || x = width) widths);                            

                            let expr = 
                              if width > 1 then begin
                                Printf.printf "promoting body of %s to vector with args: %s\n%!" 
                                  name (String.concat ", " (List.map string_of_expr call_args));
                                Printf.printf "Old body: %s\n%!" (string_of_expr expr);
                                let promoted_args = List.map (fun (t, n) -> Var (vector_of_val_type t width, n)) args in
                                let env = List.fold_right2 StringMap.add arg_names promoted_args StringMap.empty in
                                let expr = vector_subs_expr env expr in
                                Printf.printf "New body: %s\n%!" (string_of_expr expr);
                                expr
                              end else expr
                            in 

                            (* Generate functions that wrap an expression in a let that defines the argument *)
                            let wrap_arg name arg x = Let (name, arg, x) in
                            let wrappers = List.map2 (fun arg (t, name) -> wrap_arg name arg) call_args args in

                            (* Apply all those wrappers to the function body *)
                            List.fold_right (fun f arg -> f arg) wrappers expr

                              

                        (*
                        (* TODO: Check the types match *)
                            List.fold_left2 
                              (* Replace an argument with its value in e, possibly vectorizing as we go for i32 args *)
                              (fun e (t, var) value -> 
                                if (t = i32) then vector_subs_expr var value e 
                                else subs_expr (Var (t, var)) value e)
                              expr args call_args                               
                              *)
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
                    let args = List.map (mutate_children_in_expr fix_expr) args in
                    Call (ty, s, args)
                | expr -> mutate_children_in_expr fix_expr expr
              in
              let rec fix_stmt stmt = mutate_children_in_stmt fix_expr fix_stmt stmt in
              fix_stmt stmt
            end
            | _ -> raise (Wtf "I don't know how to schedule this yet")
        in
        Printf.printf "\n-------\nResulting statement: %s\n-------\n" (string_of_stmt scheduled_call);  
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

(* Figure out interdependent expressions that give the bounds required
   by all the functions defined in some block. *)
(* bounds is a list of (func, var, min, max) *)
let rec extract_bounds_soup env var_env bounds = function
  | Pipeline (buf, ty, size, produce, consume) -> 
      (* What function am I producing? *)
      let bounds = try
        let func = String.sub buf 0 (String.rindex buf '.') in        

        (* Compute the extent of that function used in the consume side *)
        let region = Bounds.required_of_stmt func var_env consume in

        if region = [] then bounds else begin

          (* Get the args list of the function *)
          let (args, _, _) = find_function func env in
          let args = List.map snd args in

        
          let string_of_bound = function
            | Bounds.Unbounded -> "Unbounded"
            | Bounds.Range (min, max) -> "Range (" ^ string_of_expr min ^ ", " ^ string_of_expr max ^ ")"
          in
          Printf.printf "%s: %s %s\n" func (String.concat ", " args) (String.concat ", " (List.map string_of_bound region));
        
          (* Add the extent of those vars to the bounds soup *)
          let add_bound bounds arg = function
            | Bounds.Range (min, max) ->              
                (func, arg, min, max)::bounds
            | _ -> raise (Wtf ("Could not compute bounds of " ^ func ^ "." ^ arg))
          in 
          List.fold_left2 add_bound bounds args region
        end
        with Not_found -> bounds
      in

      (* recurse into both sides *)
      let bounds = extract_bounds_soup env var_env bounds produce in
      extract_bounds_soup env var_env bounds consume
  | Block l -> List.fold_left (extract_bounds_soup env var_env) bounds l
  | For (n, min, size, order, body) -> 
      let var_env = StringMap.add n (Bounds.Range (min, size +~ min -~ IntImm 1)) var_env in
      extract_bounds_soup env var_env bounds body   
  | x -> bounds
 


let rec bounds_inference env schedule = function
  | For (var, min, size, order, body) ->
      let size_buffer = var ^ "_sizes" in
      (* Pull out the bounds of all function realizations within this body *)
      begin match extract_bounds_soup env StringMap.empty [] body with
        | [] -> 
            let (body, schedule) = bounds_inference env schedule body in 
            (For (var, min, size, order, body), schedule)              
        | bounds ->
            (* sort the list topologically *)
            let lt (f1, arg1, min1, max1) (f2, arg2, min2, max2) =
              let v1m = Var (Int 32, f1 ^ "." ^ arg1 ^ ".min") in
              let v2m = Var (Int 32, f2 ^ "." ^ arg2 ^ ".min") in
              let v1e = Var (Int 32, f1 ^ "." ^ arg1 ^ ".extent") in
              let v2e = Var (Int 32, f2 ^ "." ^ arg2 ^ ".extent") in
              if expr_contains_expr v1m min2 then Some false
              else if expr_contains_expr v1m max2 then Some false
              else if expr_contains_expr v1e min2 then Some false
              else if expr_contains_expr v1e max2 then Some false
              else if expr_contains_expr v2m min1 then Some true
              else if expr_contains_expr v2m max1 then Some true
              else if expr_contains_expr v2e min1 then Some true
              else if expr_contains_expr v2e max1 then Some true
              else None
            in
            let bounds = partial_sort lt bounds in

            let subs_schedule old_expr new_expr schedule =
              let subs_e e = Constant_fold.constant_fold_expr (subs_expr old_expr new_expr e) in
              let subs_sched = function
                | Parallel (n, m, s) -> Parallel (n, subs_e m, subs_e s)
                | Serial (n, m, s) -> Serial (n, subs_e m, subs_e s)
                | Vectorized (n, m, s) -> Vectorized (n, subs_e m, s)
                | Unrolled (n, m, s) -> Unrolled (n, subs_e m, s)
                | Split (n, no, ni, o) -> Split (n, no, ni, subs_e o)
              in
              let keys = list_of_schedule schedule in
              let update_schedule sched key =
                let (call_sched, sched_list) = find_schedule sched key in
                set_schedule sched key call_sched (List.map subs_sched sched_list)
              in

              List.fold_left update_schedule schedule keys
            in

            (*
            let lift_var (precomp, body, schedule) var value =
              let value = Constant_fold.constant_fold_expr value in
              let value = Break_false_dependence.break_false_dependence_expr value in
              let value = Constant_fold.constant_fold_expr value in
              let n = List.length precomp in
              let new_expr = Load (Int 32, size_buffer, IntImm n) in
              let assignment = Store (value, size_buffer, IntImm n) in
              (* let precomp x = LetStmt (var, value, precomp x) in *)
              let precomp = assignment::(List.map (subs_stmt var new_expr) precomp) in
              let body = subs_stmt var new_expr body in
              let schedule = subs_schedule var new_expr schedule in
              (precomp, body, schedule)
            in 
            let rewrite_bound (precomp, body, schedule) (func, arg, min, max) =
              (* Lift the storage of the min *)              
              let var = Var (Int 32, func ^ "." ^ arg ^ ".min") in
              let (precomp, body, schedule) = lift_var (precomp, body, schedule) var min in
              let var = Var (Int 32, func ^ "." ^ arg ^ ".extent") in
              lift_var (precomp, body, schedule) var (max -~ min +~ IntImm 1)
            in
            *)

            let lift_var precomp var value = 
              let value = Constant_fold.constant_fold_expr value in
              let value = Break_false_dependence.break_false_dependence_expr value in
              let value = Constant_fold.constant_fold_expr value in
              let precomp x = LetStmt (var, value, precomp x) in
              precomp
            in

            let rewrite_bound precomp (func, arg, min, max) =
              (* Lift the storage of the min *)              
              let precomp = lift_var precomp (func ^ "." ^ arg ^ ".min") min in
              let precomp = lift_var precomp (func ^ "." ^ arg ^ ".extent") (max -~ min +~ IntImm 1) in
              precomp
            in
            
            let precomp = List.fold_left rewrite_bound (fun x -> x) bounds in

            let (body, schedule) = bounds_inference env schedule body in               

            (For (var, min, size, order, precomp body), schedule)
      end
  | Block l ->
      let rec fix sched = function
        | Block [] -> (Block [], sched)
        | Block (first::rest) -> 
            let (Block fix_rest, fix_sched) = fix sched (Block rest) in
            let (fix_first, fix_sched) = bounds_inference env fix_sched first in
            (Block (fix_first::fix_rest), fix_sched)
      in fix schedule (Block l)
  | Pipeline (name, ty, size, produce, consume) ->
      let (produce, schedule) = bounds_inference env schedule produce in
      let (consume, schedule) = bounds_inference env schedule consume in
      (Pipeline (name, ty, size, produce, consume), schedule)
  | x -> (x, schedule)


let qualify_schedule (sched:schedule_tree) =
  let make_schedule (name:string) (schedule:schedule_tree) =
    (* Printf.printf "Looking up %s in the schedule\n" name; *)
    let (call_sched, sched_list) = find_schedule schedule name in

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
      | Var (t, n) -> Var (t, if (String.get n 0 = '.') then n else (caller ^ "." ^ n))
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

    (call_sched, List.map prefix_schedule sched_list)     
  in

  let keys = list_of_schedule sched in
  let update sched key =
    let (call_sched, sched_list) = make_schedule key sched in
    set_schedule sched key call_sched sched_list 
  in
  List.fold_left update sched keys    

let lower_function_calls (stmt:stmt) (env:environment) (schedule:schedule_tree) (debug:bool) =
  let rec replace_calls_with_loads_in_expr function_name buffer_name strides debug expr = 
    let recurse = replace_calls_with_loads_in_expr function_name buffer_name strides debug in
    match expr with 
      | Call (ty, func_name, args) when func_name = function_name ->
          let args = List.map recurse args in 
          let index = List.fold_right2 
            (fun arg (min,size) subindex -> size *~ subindex +~ arg -~ min) 
            args strides (IntImm 0) in
          let load = Load (ty, buffer_name, index) in
          if debug then
            Debug (load, "### Loading " ^ function_name ^ " at ", args)
          else
            load
      | x -> mutate_children_in_expr recurse x
  in
  let rec replace_calls_with_loads_in_stmt function_name buffer_name strides debug stmt = 
    mutate_children_in_stmt 
      (replace_calls_with_loads_in_expr function_name buffer_name strides debug)
      (replace_calls_with_loads_in_stmt function_name buffer_name strides debug) 
      stmt     
  in

  let functions = list_of_schedule schedule in
  let update stmt f =
    let (args, _, _) = find_function f env in
    let (_, sched_list) = find_schedule schedule f in
    if sched_list = [] then stmt else
      let strides = stride_list sched_list (List.map (fun (_, n) -> f ^ "." ^ n) args) in
      replace_calls_with_loads_in_stmt f (f ^ ".result") strides debug stmt
  in
  List.fold_left update stmt functions 

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

  Printf.printf "Fully qualifying symbols in the schedule\n%!";
  let schedule = qualify_schedule schedule in

  Printf.printf "Making root function body\n%!";
  let (args, return_type, body) = make_function_body func env debug in
  Printf.printf "Making root schedule\n%!";
  let (_, sched_list) = find_schedule schedule func in
  Printf.printf "Making root stride list\n%!";
  let strides = stride_list sched_list (List.map snd args) in
  Printf.printf "Realizing root statement\n%!";
  let core = (realize (func, args, return_type, body) sched_list ".result" strides debug) in
  Printf.printf "Recursively lowering function calls\n%!";
  let stmt = lower_stmt core env schedule (List.filter (fun x -> x <> func) functions) debug in

  Printf.printf "Performing bounds inference\n%!";
  (* Updates stmt and schedule *)
  let (For (_, _, _, _, stmt), schedule) =
    bounds_inference env schedule (For ("", IntImm 0, IntImm 0, true, stmt)) in

  print_schedule schedule;

  Printf.printf "Replacing function calls with loads\n%!";
  let stmt = lower_function_calls stmt env schedule debug in

  print_schedule schedule;
  
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




