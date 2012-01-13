open Ir
open Ir_printer
open Schedule
open Analysis
open Util
open Vectorize
open Bounds

let trace_verbosity = 
  let str = try Sys.getenv "HL_TRACE" with Not_found -> "0" in
  try int_of_string str with Failure _ -> begin
    Printf.printf "Could not understand tracing level %s\n" str;
    0
  end

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
        with Not_found -> failwith ("Could not resolve name " ^ var ^ " in " ^ context)
      in    
      let parent = String.sub context 0 last_dot in
      resolve_name parent var schedule      
  end
        
let make_function_body (name:string) (env:environment) =
  dbg 0 "make_function_body %s\n%!" name;
  
  let (args, return_type, body) = find_function name env in
  let prefix = name ^ "." in
  let pre = prefix_name_expr prefix in
  let renamed_args = List.map (fun (t, n) -> (t, prefix ^ n)) args in
  let renamed_body = match body with
    | Extern -> failwith ("Can't lookup body of extern function: " ^ name)
    | Pure expr -> Pure (pre expr)
    | Reduce (init_expr, modified_args, update_func, reduction_domain) -> 
        let prefix_update = prefix_name_expr (prefix ^ update_func ^ ".") in
        Reduce (pre init_expr,
                List.map (prefix_name_expr (prefix ^ update_func ^ ".")) modified_args, 
                prefix ^ update_func,
                List.map (fun (n, x, y) -> 
                  (prefix ^ update_func ^ "." ^ n,
                   prefix_update x, prefix_update y)) reduction_domain)

  in
  (renamed_args, return_type, renamed_body)

let simplify_range = function
  | Unbounded -> Unbounded
  | Range (a, b) -> Range (Constant_fold.constant_fold_expr a, Constant_fold.constant_fold_expr b)
let simplify_region r = List.map simplify_range r 
let string_of_range = function  
  | Unbounded -> "[Unbounded]"
  | Range (a, b) -> "[" ^ string_of_expr a ^ ", " ^ string_of_expr b ^ "]" 
let bounds f stmt = simplify_region (required_of_stmt f (StringMap.empty) stmt) 

let rec lower_stmt (func:string) (stmt:stmt) (env:environment) (schedule:schedule_tree) =
  (* Grab the schedule for the next function call to lower *)
  let (call_sched, sched_list) = find_schedule schedule func in

  dbg 0 "Lowering function %s with schedule %s [%s]\n%!" func 
    (string_of_call_schedule call_sched)
    (String.concat ", " (List.map string_of_schedule sched_list));
  
  let scheduled_call = 
    match call_sched with
      | Chunk chunk_dim -> begin
        (* Recursively descend the statement until we get to the loop in question *)
        let rec inner = function
          | For (for_dim, min, size, order, for_body) when for_dim = chunk_dim -> 
              For (for_dim, min, size, order, realize func for_body env schedule)
          | stmt -> mutate_children_in_stmt (fun x -> x) inner stmt
        in inner stmt
      end
      | Root -> realize func stmt env schedule
      | Inline ->
          (* grab the body of the function *)
          let (args, _, body) = make_function_body func env in
          (* Just replace all calls to the function with the body of the function *)
          begin match body with 
            | Extern -> failwith ("Can't inline extern function " ^ func)
            | Reduce _ -> failwith ("Can't inline a reduction " ^ func)
            | Pure expr ->
                let rec inline_calls_in_expr = function
                  | Call (t, n, call_args) when n = func -> 
                      let call_args = List.map inline_calls_in_expr call_args in
                      
                      (* If the args have vector type, promote
                         the function body to also have vector type
                         in its args *)
                      
                      let widths = List.map (fun x -> vector_elements (val_type_of_expr x)) call_args in
                      let width = List.hd widths in (* Do we have pure functions with no arguments? *)
                      assert (List.for_all (fun x -> x = 1 || x = width) widths);                            

                      let expr = 
                        if width > 1 then begin
                          let arg_names = List.map snd args in
                          let promoted_args = List.map (fun (t, n) -> Var (vector_of_val_type t width, n)) args in
                          let env = List.fold_right2 StringMap.add arg_names promoted_args StringMap.empty in
                          let expr = vector_subs_expr env expr in
                          expr
                        end else expr
                      in 

                      (* Generate functions that wrap an expression in a let that defines the argument *)
                      let wrap_arg name arg x = Let (name, arg, x) in
                      let wrappers = List.map2 (fun arg (t, name) -> wrap_arg name arg) call_args args in

                      (* Apply all those wrappers to the function body *)
                      List.fold_right (fun f arg -> f arg) wrappers expr

                  | x -> mutate_children_in_expr inline_calls_in_expr x
                and inline_calls_in_stmt s =
                  mutate_children_in_stmt inline_calls_in_expr inline_calls_in_stmt s
                in
                inline_calls_in_stmt stmt                        
          end
      | Reuse s -> begin
        (* Replace calls to name with calls to s in stmt *)
        let rec fix_expr = function
          | Call (ty, n, args) when n = func -> 
              let args = List.map (mutate_children_in_expr fix_expr) args in
              Call (ty, s, args)
          | expr -> mutate_children_in_expr fix_expr expr
        in
        let rec fix_stmt stmt = mutate_children_in_stmt fix_expr fix_stmt stmt in
        fix_stmt stmt
      end
      | _ -> failwith "I don't know how to schedule this yet"
  in
  (* Printf.printf "\n-------\nResulting statement: %s\n-------\n" (string_of_stmt scheduled_call);   *)
  scheduled_call



(* Evaluate a function according to a schedule and wrap the stmt consuming it in a pipeline *)
and realize func consume env schedule =

  let (_, sched_list) = find_schedule schedule func in

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
  
  let (args, return_type, body) = make_function_body func env in
  let arg_vars = List.map (fun (t, n) -> Var (t, n)) args in
  let arg_names = List.map snd args in

  let strides = stride_list sched_list arg_names in
  let buffer_size =
    List.fold_right2
      (fun (min, size) nm old_size ->
         (* these debug prints are useful, but break constant folding, particularly
            essential for CUDA shmem *)
         (* Debug(size, Printf.sprintf "  dim %s = " nm, [size]) *~ old_size) *)
         size *~ old_size)
      strides
      arg_names
      (IntImm 1) in

  match body with
    | Extern -> failwith ("Can't lower extern function call " ^ func)
    | Pure body ->
        let inner_stmt = Provide (body, func, arg_vars) in
        let produce = List.fold_left wrap inner_stmt sched_list in
        let rec flatten = function
          | (x, y)::rest -> x::y::(flatten rest)
          | [] -> []
        in
        let produce = if (trace_verbosity > 0) then            
            Block [Print ("Time ", [Call (Int(32), ".currentTime", [])]); 
                   Print ("Realizing " ^ func ^ " over ", flatten strides);
                   produce] 
          else produce in
        Pipeline (func, return_type, buffer_size, produce, consume)
    | Reduce (init_expr, update_args, update_func, reduction_domain) ->

        let init_stmt = Provide (init_expr, func, arg_vars) in

        let init_stmt = if (trace_verbosity > 1) then 
            Block [Print ("Initializing " ^ func ^ " at ", arg_vars @ [init_expr]); init_stmt]
          else init_stmt
        in

        let initialize = List.fold_left wrap init_stmt sched_list in
               
        let (pure_update_args, _, update_body) = make_function_body update_func env in
        let update_expr = match update_body with
          | Pure expr -> expr
          | _ -> failwith "The update step of a reduction must be pure"
        in
        
        (* remove recursion in the update expr *)
        let update_expr = subs_name_expr (update_func ^ "." ^ (base_name func)) func update_expr in

        let update_stmt = Provide (update_expr, func, update_args) in

        let update_stmt = if (trace_verbosity > 1) then 
            Block [Print ("Updating " ^ func ^ " at ", update_args @ [update_expr]); update_stmt]
          else update_stmt
        in

        let (_, update_sched_list) = find_schedule schedule update_func in
        let update = List.fold_left wrap update_stmt update_sched_list in

        let pure_domain = List.map 
          (fun (t, n) -> 
            let parent_n = (parent_name update_func) ^ "." ^ (base_name n) in
            (n, Var (t, parent_n ^ ".min"), Var (t, parent_n ^ ".extent")))
          pure_update_args 
        in

        (* Wrap it in some let statements that set the bounds of the reduction *)
        let update = List.fold_left
          (fun stmt (var, min, size) ->
            let stmt = LetStmt (var ^ ".min", min, stmt) in
            let stmt = LetStmt (var ^ ".extent", size, stmt) in
            stmt
          )
          update
          (reduction_domain @ pure_domain)
        in

        let rec flatten = function
          | (x, y)::rest -> x::y::(flatten rest)
          | [] -> []
        in


        let produce = 
          if (trace_verbosity > 0) then 
            let initialize = Block [Print ("Time ", [Call (Int(32), ".currentTime", [])]); 
                                    Print ("Initializing " ^ func ^ " over ", flatten strides);
                                    initialize] in             
            let update = Block [Print ("Time ", [Call (Int(32), ".currentTime", [])]); 
                                Print ("Updating " ^ func, []);
                                update] in 
            Block [initialize; update]
          else
            Block [initialize; update]
        in

        (* Put the whole thing in a pipeline that exposes the updated
           result to the consumer *)
        Pipeline (func, return_type, buffer_size, produce, consume)


(* Figure out interdependent expressions that give the bounds required
   by all the functions defined in some block. *)
(* bounds is a list of (func, var, min, max) *)
let rec extract_bounds_soup env var_env bounds = function
  | Pipeline (func, ty, size, produce, consume) -> 
      (* What function am I producing? *)
      let bounds = try          
        (* Get the args list of the function *)
        let (args, _, body) = find_function func env in
        let args = List.map snd args in
       

        (* Compute the extent of that function used in the consume side *)
        let region = required_of_stmt func var_env consume in

        (* If func is a reduction, also consider the bounds being written to *)
        let region = match (body, produce) with
          | (Reduce _, Block [init; update]) ->
              let update_region = required_of_stmt func var_env update in
              let region = region_union region update_region in
              (* fixup any recursive bounds (TODO: this is a giant hack) *)
              let fix range var =
                match range with
                  | Unbounded -> failwith ("Reduction writes to unbounded region of " ^ func)
                  | Range (min, max) -> begin
                    let rec bad_expr = function
                      | Var (t, v) when v = func ^ "." ^ var ^ ".extent" -> true
                      | Var (t, v) when v = func ^ "." ^ var ^ ".min" -> true
                      | expr -> fold_children_in_expr bad_expr (||) false expr
                    in
                    let min = match min with
                      | Bop (Min, x, v) when bad_expr v -> x
                      | _ -> min
                    and max = match max with
                      | Bop (Max, x, v) when bad_expr v -> x
                      | _ -> max
                    in Range (min, max)
                  end                      
              in List.map2 fix region args
                
          | (Reduce _, _) -> failwith "Could not understand the produce side of a reduction"
          | _ -> region
        in

        if region = [] then bounds else begin
                
          let string_of_bound = function
            | Unbounded -> "Unbounded"
            | Range (min, max) -> "Range (" ^ string_of_expr min ^ ", " ^ string_of_expr max ^ ")"
          in
          (* Printf.printf "%s: %s %s\n" func (String.concat ", " args) (String.concat ", " (List.map string_of_bound region)); *)
        
          (* Add the extent of those vars to the bounds soup *)
          let add_bound bounds arg = function
            | Range (min, max) ->              
                (func, arg, min, max)::bounds
            | _ -> failwith ("Could not compute bounds of " ^ func ^ "." ^ arg)
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
      let var_env = StringMap.add n (Range (min, size +~ min -~ IntImm 1)) var_env in
      extract_bounds_soup env var_env bounds body   
  | x -> bounds
 


let rec bounds_inference env schedule = function
  | For (var, min, size, order, body) ->
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

            let lift_var precomp var value = 
              let value = Constant_fold.constant_fold_expr value in
              (* let value = Break_false_dependence.break_false_dependence_expr value in
              let value = Constant_fold.constant_fold_expr value in *)
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

let lower_function_calls (stmt:stmt) (env:environment) (schedule:schedule_tree) =
  let rec replace_calls_with_loads_in_expr func strides expr = 
    let recurse = replace_calls_with_loads_in_expr func strides in
    match expr with 
      (* Match calls to f from someone else, or recursive calls from f to itself *)
      | Call (ty, f, args) when f = func || f = (func ^ "." ^ (base_name func)) ->
          let args = List.map recurse args in 
          let index = List.fold_right2 
            (fun arg (min,size) subindex -> size *~ subindex +~ arg -~ min) 
            args strides (IntImm 0) in
          let load = Load (ty, func, index) in
          if (trace_verbosity > 1) then
            Debug (load, "Loading " ^ func ^ " at ", args)
          else
            load
      | x -> mutate_children_in_expr recurse x
  in
  let rec replace_calls_with_loads_in_stmt func strides stmt = 
    let recurse_stmt = replace_calls_with_loads_in_stmt func strides in
    let recurse_expr = replace_calls_with_loads_in_expr func strides in
    match stmt with
      | Provide (e, f, args) when f = func ->
          let args = List.map recurse_expr args in
          let index = List.fold_right2 
            (fun arg (min,size) subindex -> size *~ subindex +~ arg -~ min) 
            args strides (IntImm 0) in          
          Store (recurse_expr e, f, index)
      | _ -> mutate_children_in_stmt recurse_expr recurse_stmt stmt
  in

  let functions = list_of_schedule schedule in
  let update stmt f =
    let (args, _, _) = find_function f env in
    let (_, sched_list) = find_schedule schedule f in
    if sched_list = [] then stmt else
      let strides = stride_list sched_list (List.map (fun (_, n) -> f ^ "." ^ n) args) in
      replace_calls_with_loads_in_stmt f strides stmt
  in
  List.fold_left update stmt functions 

let lower_function (func:string) (env:environment) (schedule:schedule_tree) =
  let starts_with a b =
    String.length a > String.length b &&
      String.sub a 0 ((String.length b)+1) = b ^ "."
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

  (* Get all the functions to lower *)
  let functions = list_of_schedule schedule in

  (* Filter out the reduction update steps *)
  let functions = List.filter 
    (fun func ->
      if String.contains func '.' then
        match find_function (parent_name func) env with
          | (_, _, Reduce (_, _, update_func, _)) ->
              (base_name func) <> update_func
          | _ -> true
      else
        true
    )
    functions 
  in
    

  (* Compute the order in which to realize them *)
  let functions = partial_sort lt functions in
  dbg 0 "Realization order: %s\n" (String.concat ", " functions);

  dbg 0 "Fully qualifying symbols in the schedule\n%!";
  (* if 0 < verbosity then print_schedule schedule; *)
  let schedule = qualify_schedule schedule in
  (* if 0 < verbosity then print_schedule schedule; *)

  dbg 0 "Realizing root statement\n%!";
  let (Pipeline (_, _, _, core, _)) = realize func (Block []) env schedule in

  dbg 0 "Recursively lowering function calls\n%!";
  let functions = List.filter (fun x -> x <> func) functions in  
  let stmt = List.fold_left (fun stmt f -> lower_stmt f stmt env schedule) core functions in

  dbg 0 "Performing bounds inference\n%!";
  (* Updates stmt and schedule *)
  let (For (_, _, _, _, stmt), schedule) =
    bounds_inference env schedule (For ("", IntImm 0, IntImm 0, true, stmt)) in

  (* if 0 < verbosity then print_schedule schedule; *)

  dbg 0 "Replacing function references with loads and stores\n%!";
  let stmt = lower_function_calls stmt env schedule in

  dbg 0 "Replacing stores to %s to stores to .result\n%!" func;
  let rec rewrite_loads_from_result = function
    | Load (e, f, idx) when f = func ->
        Load (e, ".result", idx)
    | expr -> mutate_children_in_expr rewrite_loads_from_result expr
  in
  let rec rewrite_references_to_result = function
    | Store (e, f, idx) when f = func ->
        Store (rewrite_loads_from_result e, ".result", rewrite_loads_from_result idx)
    | stmt -> mutate_children_in_stmt rewrite_loads_from_result rewrite_references_to_result stmt 
  in
  let stmt = rewrite_references_to_result stmt in

  let args,_,_ = find_function func env in
  let (stmt,_) =
    List.fold_left
      (fun (stmt,i) (t,nm) ->
        let stmt =
          LetStmt (
            ("." ^ func ^ "." ^ nm ^ ".min"),
            IntImm 0,
            stmt)
        in
        (LetStmt (
          ("." ^ func ^ "." ^ nm ^ ".extent"),
          Var (t, ".result.dim." ^ (string_of_int i)),
          stmt), i+1))
      (stmt,0)
      args
  in
  stmt





