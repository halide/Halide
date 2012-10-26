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
    
    let found = List.fold_left (fun x y -> x or y) false (List.map provides_name sched_list) in
    
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
  dbg 2 "make_function_body %s\n%!" name;
  
  let (args, return_type, body) = find_function name env in
  let prefix = name ^ "." in
  let pre = prefix_name_expr prefix in
  let renamed_args = List.map (fun (t, n) -> (t, prefix ^ n)) args in
  let renamed_body = match body with
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

  dbg 2 "Lowering function %s with schedule %s [%s]\n%!" func 
    (string_of_call_schedule call_sched)
    (String.concat ", " (List.map string_of_schedule sched_list));

  (* grab the body of the function *)
  let (args, return_type, body) = make_function_body func env in
  
  dbg 2 "Lowering function %s with schedule %s [%s]\n%!" func 
    (string_of_call_schedule call_sched)
    (String.concat ", " (List.map string_of_schedule sched_list));

  let scheduled_call = 
    match call_sched with
      | Chunk (store_dim, compute_dim) -> begin        
        let rec inject_compute = function
          | For (for_dim, min, size, order, for_body) when for_dim = compute_dim -> 
            let for_body = realize func for_body env schedule in
            For (for_dim, min, size, order, for_body)
          | stmt -> mutate_children_in_stmt (fun x -> x) inject_compute stmt
        and inject_storage = function
          | For (for_dim, min, size, order, stmt) when for_dim = store_dim ->
            let dim_names = List.map snd args in
            let region = extent_computed_list sched_list dim_names in

            let stmt = if for_dim = compute_dim then
                (* If we're already at the right loop level for the
                   compute as well, inject the realization here *)
                realize func stmt env schedule
              else
                inject_compute stmt
            in
            let stmt = Realize (func, return_type, region, stmt) in
            For (for_dim, min, size, order, stmt)

          (* 
            let stmt = Allocate (func, return_type, alloc_size, stmt) in 

            let strides = List.map (fun dim -> Var (i32, dim ^ ".stride")) dim_names in
            let (min_computed, extent_computed) = List.split (extent_computed_list sched_list dim_names) in
            let alloc_size = Var (i32, func ^ ".alloc_size") in

            let stmt = 
              if strides = [] then 
                (* skip for zero-dimensional funcs *)
                LetStmt (func ^ ".alloc_size", IntImm 1, stmt) 
              else                
                (* lets for strides *)
                let lhs = 
                  (List.map (fun dim -> (dim ^ ".stride")) dim_names) @ 
                    [func ^ ".alloc_size"] in
                let rhs = (IntImm 1) :: (List.map2 ( *~ ) strides extent_computed) in
                (* lets for mins 
                   Currently uses compute mins at this loop level *)                
                let lhs = lhs @ (List.map (fun dim -> (dim ^ ".buf_min")) dim_names) in
                let rhs = rhs @ min_computed in
                let make_let_stmt l r stmt = LetStmt (l, r, stmt) in
                List.fold_right2 make_let_stmt lhs rhs stmt
            in For (for_dim, min, size, order, stmt)
          *)
          | stmt -> mutate_children_in_stmt (fun x -> x) inject_storage stmt
        in inject_storage stmt
      end
      | Inline ->
          (* Just replace all calls to the function with the body of the function *)
          begin match body with 
            | Reduce _ -> failwith ("Can't inline a reduction " ^ func)
            | Pure expr ->
                let rec inline_calls_in_expr = function
                  | Call (Func, t, n, call_args) when n = func -> 
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
          | Call (Func, ty, n, args) when n = func -> 
              let args = List.map (mutate_children_in_expr fix_expr) args in
              Call (Func, ty, s, args)
          | expr -> mutate_children_in_expr fix_expr expr
        in
        let rec fix_stmt stmt = mutate_children_in_stmt fix_expr fix_stmt stmt in
        fix_stmt stmt
      end
  in
  (* Printf.printf "\n-------\nResulting statement: %s\n-------\n" (string_of_stmt scheduled_call);   *)
  scheduled_call



(* Evaluate a function according to a schedule and wrap the stmt consuming it in a pipeline *)
and realize func consume env schedule =

  let (_, sched_list) = find_schedule schedule func in

  (* Wrap a statement in for loops using a schedule *)
  let wrap (sched_list: schedule list) (stmt:stmt) = function 
    | Serial     (name, min, size) -> 
        For (name, min, size, true, stmt)
    | Parallel   (name, min, size) -> 
        For (name, min, size, false, stmt)
    | Unrolled   (name, min, size) -> 
        Unroll.unroll_stmt name (For (name, min, IntImm size, false, stmt))
    | Vectorized (name, min, size) -> 
        Vectorize.vectorize_stmt name (For (name, min, IntImm size, false, stmt))
    | Split (old_dim, new_dim_outer, new_dim_inner, offset) -> 
        let (_, size_new_dim_inner) = extent_computed_for_dim new_dim_inner sched_list in
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

  (* Only used for tracing printfs *)
  let extent_computed = extent_computed_list sched_list arg_names in

  match body with
    | Pure body ->
        let inner_stmt = Provide (body, func, arg_vars) in

        let inner_stmt = if (trace_verbosity > 1) then
            Block [Print ("Storing " ^ func ^ " at ", arg_vars @ [body]); inner_stmt]
          else inner_stmt
        in

        let produce = List.fold_left (wrap sched_list) inner_stmt sched_list in
        let rec flatten = function
          | (x, y)::rest -> x::y::(flatten rest)
          | [] -> []
        in
        let produce = if (trace_verbosity > 0) then            
            Block [Print ("Time ", [Call (Extern, Int(32), ".currentTime", [])]); 
                   Print ("Realizing " ^ func ^ " over ", flatten extent_computed);
                   produce] 
          else produce in
        Pipeline (func, produce, consume)
    | Reduce (init_expr, update_args, update_func, reduction_domain) ->

        let init_stmt = Provide (init_expr, func, arg_vars) in

        let init_stmt = if (trace_verbosity > 1) then 
            Block [Print ("Initializing " ^ func ^ " at ", arg_vars @ [init_expr]); init_stmt]
          else init_stmt
        in

        let initialize = List.fold_left (wrap sched_list) init_stmt sched_list in

        dbg 2 "Making body of update function: %s\n%!" update_func;
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

        dbg 2 "Retrieving schedule of update func\n%!";
        let (_, update_sched_list) = find_schedule schedule update_func in
        let update = List.fold_left (wrap update_sched_list) update_stmt update_sched_list in

        dbg 2 "Computing pure domain\n%!";
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
            let initialize = Block [Print ("Time ", [Call (Extern, Int(32), ".currentTime", [])]); 
                                    Print ("Initializing " ^ func ^ " over ", flatten extent_computed);
                                    initialize] in             
            let update = Block [Print ("Time ", [Call (Extern, Int(32), ".currentTime", [])]); 
                                Print ("Updating " ^ func, []);
                                update] in 
            Block [initialize; update]
          else
            Block [initialize; update]
        in

        (* Put the whole thing in a pipeline that exposes the updated
           result to the consumer *)
        Pipeline (func, produce, consume)


(* Figure out interdependent expressions that give the bounds required
   by all the functions defined in some block. *)
(* bounds is a list of (func, var, min, max) *)
let rec extract_bounds_soup env var_env bounds = function
  | Pipeline (func, produce, consume) -> 
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
  | Realize (_, _, _, body) 
  | Allocate (_, _, _, body) ->     
      extract_bounds_soup env var_env bounds body
  | Block l -> List.fold_left (extract_bounds_soup env var_env) bounds l
  | For (name, min, size, order, body) -> 
      let var_env = StringMap.add name (Range (min, size +~ min -~ IntImm 1)) var_env in
      extract_bounds_soup env var_env bounds body           
  | LetStmt (name, value, body) ->
      dbg 2 "Entering %s\n" name;
      let var_env = StringMap.add name (bounds_of_expr_in_env var_env value) var_env in
      dbg 2 "Entering %s (2)\n" name;
      extract_bounds_soup env var_env bounds body
  | x -> bounds
 


let rec bounds_inference env schedule = function
  | For (var, min, size, order, body) ->
      dbg 2 "Performing bounds inference inside loop over %s\n" var;
      (* Pull out the bounds of all function realizations within this body *)
      begin match extract_bounds_soup env StringMap.empty [] body with
        | [] -> 
            dbg 2 "Got an empty list from bounds soup\n";
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
            
            dbg 2 "Wrapping for loop body in %d let statements\n" (List.length bounds);

            (For (var, min, size, order, precomp body), schedule)
      end
  | Block l ->
      let rec fix sched = function
        | [] -> ([], sched)
        | (first::rest) -> 
            let (fix_rest, fix_sched) = fix sched rest in
            let (fix_first, fix_sched) = bounds_inference env fix_sched first in
            ((fix_first::fix_rest), fix_sched)
      in let (l, schedule) = fix schedule l in (Block l, schedule)
  | Realize (name, ty, region, body) -> 
      let (body, schedule) = bounds_inference env schedule body in
      (Realize (name, ty, region, body), schedule)                                            
  | Allocate (name, ty, size, body) -> 
      let (body, schedule) = bounds_inference env schedule body in
      (Allocate (name, ty, size, body), schedule)
  | Pipeline (name, produce, consume) ->
      let (produce, schedule) = bounds_inference env schedule produce in
      let (consume, schedule) = bounds_inference env schedule consume in
      (Pipeline (name, produce, consume), schedule)
  | LetStmt (name, value, stmt) ->
      let (stmt, schedule) = bounds_inference env schedule stmt in
      (LetStmt (name, value, stmt), schedule)
  | x -> (x, schedule)

let rec sliding_window (stmt:stmt) (function_env:environment) =
  let rec process_dim name dims env serial_dim serial_dim_min stmt =
    match stmt with
      (* We're within an allocate over name, and a serial for loop over
         dim. Find bounds of realizations of name and rewrite them to take
         into account stuff computed so far *)
      | Pipeline (n, produce, consume) when n = name ->
        dbg 2 "process_dim %s %s\n" name serial_dim;
        (* Compute new bounds for the realization of n which are the
           exclusion of the bounds for this iteration and the bounds for
           the previous iteration. (This is only weakly inductive, we
           could also consider *all* previous iterations) *)
        
        let serial_dim_expr = Var (i32, serial_dim) in
        
        (* Find which dims depend on the serial_dim ... *)
        let rec find_matching_dim = function
          | [] -> []
          | (dim::dims) -> 
            let min = StringMap.find (name ^ "." ^ dim ^ ".min") env in
            let extent = StringMap.find (name ^ "." ^ dim ^ ".extent") env in
            let list = find_matching_dim dims in
            if expr_contains_expr serial_dim_expr (min +~ extent) then
              (dim, min, extent)::list
            else 
              list
        in
        
        (* ... hopefully just one does, and hopefully just in the min, not
         in the extent. If none do then this is a silly schedule. If
         multiple do then we don't handle that. *)
        begin match find_matching_dim dims with
          | ((dim, min, extent)::[]) when not (expr_contains_expr serial_dim_expr extent) ->          
            dbg 2 "Doing sliding window of %s over %s against %s\n" name serial_dim dim;
            let stride = min -~ (subs_expr serial_dim_expr (serial_dim_expr -~ (IntImm 1)) min) in
            let new_min = min +~ extent -~ stride in
            let new_extent = stride in
            let steady_state = (serial_dim_expr >~ serial_dim_min) in
            let new_min = Select (steady_state, new_min, min) in
            let new_extent = Select (steady_state, new_extent, extent) in
            
            let stmt = produce in
            let stmt = LetStmt (name ^ "." ^ dim ^ ".min", new_min, stmt) in
            let stmt = LetStmt (name ^ "." ^ dim ^ ".extent", new_extent, stmt) in
            let stmt = Pipeline (n, stmt, consume) in 
            stmt
          | _ -> 
            stmt 
        end
      | LetStmt (n, expr, stmt) -> 
        let new_env = StringMap.add n expr env in
        LetStmt (n, expr, process_dim name dims new_env serial_dim serial_dim_min stmt)
      | stmt -> mutate_children_in_stmt (fun x -> x) (process_dim name dims env serial_dim serial_dim_min) stmt
  in
  
  let rec process name dims = function
    | For (dim, min, extent, true, body) ->
      dbg 2 "Performing sliding window optimization for %s over %s\n" name dim;
      let new_body = process name dims body in
      let new_body = process_dim name dims StringMap.empty dim min new_body in
      For (dim, min, extent, true, new_body)
    | stmt -> mutate_children_in_stmt (fun x -> x) (process name dims) stmt
  in

  match stmt with
    | Realize (name, ty, region, body) ->
      let (args, _, _) = find_function name function_env in
      let dims = List.map snd args in
      let new_body = process name dims body in
      let new_body = sliding_window new_body function_env in
      Realize (name, ty, region, new_body) 
    | stmt -> mutate_children_in_stmt (fun x -> x) (fun s -> sliding_window s function_env) stmt
      
let rec storage_folding defs stmt = 

  (* Returns an expression that gives the permissible folding factor *)
  let rec process func env = function
    (* We're inside an allocate over func *)
    | For (for_dim, for_min, for_size, true, body) ->
      dbg 2 "Storage folding inside loop over %s\n" for_dim;
      (* Compute the region of func touched within this body *)
      let region = required_of_stmt func env body in

      (* Attempt to fold along each dimension of the region in turn *)
      let rec try_fold = function
        | ([], _) -> None
        | ((Unbounded::rest), i) -> try_fold (rest, (i+1))
        | ((Range (min, max))::rest, i) ->
          let extent = Constant_fold.constant_fold_expr ((max +~ (IntImm 1)) -~ min) in
          (* Find the maximum value of extent over the loop *)
          let env = StringMap.add for_dim (Range (for_min, for_min +~ for_size -~ (IntImm 1))) env in
          let max_extent = begin match bounds_of_expr_in_env env extent with
            | Unbounded -> begin
              dbg 2 "Not folding %s over dimension %d because unbounded extent: %s\n" func i (string_of_expr extent);
              try_fold (rest, (i+1))
            end
            | Range (_, IntImm k) -> begin
              (* Round up to the nearest power of two, so that we can just use masking *)
              let rec pow2 x = if (x < k) then pow2 (x*2) else x in
              let k = pow2 1 in
              let result = (i, IntImm k) in
              dbg 2 "Folding %s over dimension %d by %d\n" func i k;
              Some result
            end              
            | Range (_, max) -> begin
              dbg 2 "Folding factor for %s over dimension %d: %s\n" func i (string_of_expr max);              
              (* Some max *)
              (* For now we rule out dynamic folding factors, because we can't tell whether they're a good idea *)
              try_fold (rest, (i+1))
            end
          end in
          (* Now check that min is monotonic in for_dim *)
          let next_min = subs_expr (Var (i32, for_dim)) ((Var (i32, for_dim)) +~ (IntImm 1)) min in
          let check = Constant_fold.constant_fold_expr ((next_min -~ min) >~ (IntImm 0)) in
          if (check = bool_imm true || check = bool_imm false) then
            max_extent
          else begin
            dbg 2 "Not folding %s because min didn't simplify: %s\n" func (string_of_expr check);
            try_fold (rest, (i+1))
          end          
      in try_fold (region, 0)
    | LetStmt (n, e, stmt) -> 
      let env = StringMap.add n (bounds_of_expr_in_env env e) env in
      begin match process func env stmt with
        | None -> None
        | Some ((dim, factor)) ->
          Some ((dim, subs_expr (Var (val_type_of_expr e, n)) e factor))
      end
    | Realize (_, _, _, stmt) 
    | (Block [stmt]) -> process func env stmt
    | (Block ((Assert (_, _))::rest))
    | (Block ((Print (_, _))::rest)) -> process func env (Block rest)      
    | stmt -> begin
      dbg 2 "Not folding %s due to hitting an unsupported statement\n" func;
      None
    end
  in      

  let rec rewrite_args args factor = function
    | 0 -> ((List.hd args) %~ factor)::(List.tl args)
    | n -> (List.hd args)::(rewrite_args (List.tl args) factor (n-1))
  in

  let rec fold_loads func dim factor = function
    | Call (Func, ty, f, args) when f = func ->
      let args = List.map (fold_loads func dim factor) args in
      let args = rewrite_args args factor dim in
      Call (Func, ty, f, args)
    | expr -> mutate_children_in_expr (fold_loads func dim factor) expr
  in

  let rec fold_loads_and_stores func dim factor = function
    | Provide (e, f, args) when f = func -> 
      let e = fold_loads func dim factor e in
      let args = List.map (fold_loads func dim factor) args in
      let args = rewrite_args args factor dim in
      Provide (e, f, args)
    | stmt -> mutate_children_in_stmt
      (fold_loads func dim factor)
      (fold_loads_and_stores func dim factor) 
      stmt
  in
      
  match stmt with
    | Realize (func, ty, region, stmt) ->
      begin match process func StringMap.empty stmt with
        | None ->
          dbg 2 "No useful folds found for %s\n" func;
          Realize (func, ty, region, storage_folding defs stmt)
        | Some ((dim, factor)) -> 
          dbg 2 "Folding %s over dimension %d by %s\n" func dim (string_of_expr factor);
          let new_stmt = storage_folding defs stmt in 
          let new_stmt = fold_loads_and_stores func dim factor new_stmt in
          let rec fix_region idx = function
            | [] -> failwith "Reached end of list when modifying region during storage folding\n"
            | ((min, extent)::rest) ->
              if idx > 0 then 
                ((min, extent)::(fix_region (idx-1) rest)) 
              else
                (IntImm 0, factor)::rest  
          in
          let new_region = fix_region dim region in

          dbg 2 "After folding: %s\n" (string_of_stmt new_stmt);
          Realize (func, ty, new_region, new_stmt)
      end
      
    | Allocate (func, ty, size, stmt) -> 
      let (args, _, _) = find_function func defs in
      let args = List.map snd args in

      begin match process func StringMap.empty stmt with
        | None ->
          dbg 2 "No useful folds found for %s\n" func;
          Allocate (func, ty, size, storage_folding defs stmt)
        | Some ((dim, factor)) -> 
          dbg 2 "Folding %s over dimension %d by %s\n" func dim (string_of_expr factor);
          let new_stmt = storage_folding defs stmt in 
          let new_stmt = fold_loads_and_stores func dim factor new_stmt in
          
          dbg 2 "After folding: %s\n" (string_of_stmt new_stmt);

          let dim_name = List.nth args dim in
          let new_size = (size /~ (Var (i32, func ^ "." ^ dim_name ^ ".extent"))) *~ factor in
          Allocate (func, ty, new_size, new_stmt)
      end
    | _ -> mutate_children_in_stmt (fun x -> x) (storage_folding defs) stmt 
      
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
        String.sub name 0 (last_dot+1)
      else
        "" 
    in

    let rec prefix_expr = function
      | Var (t, n) -> Var (t, if (String.get n 0 = '.') then n else (caller ^ n))
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
  let rec flatten_args func args arg_names =
    match (args, arg_names) with 
      | [], [] -> 
          IntImm 0
      | (arg::args, name::names) -> 
          (* stride * (idx - min) *)
          let rest = flatten_args func args names in
          let buf_min = Var (i32, func ^ "." ^ name ^ ".buf_min") in
          let offset = arg -~ buf_min in
          let stride = Var (i32, func ^ "." ^ name ^ ".stride") in
          (stride *~ offset) +~ rest
      | _ -> failwith ("Wrong number of args in call to " ^ func)
  in
  let rec replace_calls_with_loads_in_expr func arg_names expr = 
    let recurse = replace_calls_with_loads_in_expr func arg_names in
    match expr with 
      (* Match calls to f from someone else, or recursive calls from f to itself *)
      | Call (Func, ty, f, args) when f = func || f = (func ^ "." ^ (base_name func)) ->
          dbg 2 "Flattening a call to %s\n" func;
          let args = List.map recurse args in 
          let index = flatten_args func args arg_names in
          let load = Load (ty, func, index) in
          if (trace_verbosity > 1) then
            Debug (load, "Loading " ^ func ^ " at ", args)
          else
            load
      | x -> mutate_children_in_expr recurse x
  in
  let rec replace_calls_with_loads_in_stmt func arg_names stmt = 
    let recurse_stmt = replace_calls_with_loads_in_stmt func arg_names in
    let recurse_expr = replace_calls_with_loads_in_expr func arg_names in
    let (dim_names, _, _) = find_function func env in
    let dim_names = List.map (fun (x, y) -> func ^ "." ^ y) dim_names in
    match stmt with
      | Provide (e, f, args) when f = func ->
          dbg 2 "Flattening a provide to %s\n" func;
          let args = List.map recurse_expr args in
          let index = flatten_args func args arg_names in
          Store (recurse_expr e, f, index)
      | Realize (f, ty, region, stmt) when f = func ->
          dbg 2 "Flattening a realize to an allocate %s\n" func;
          let region = List.map (fun (x, y) -> (recurse_expr x, recurse_expr y)) region in
          (* Inject a bunch of lets for the strides and buf_mins *)
          let strides = List.map (fun dim -> Var (i32, dim ^ ".stride")) dim_names in
          let (min_computed, extent_computed) = List.split region in
          let alloc_size = Var (i32, func ^ ".alloc_size") in

          let stmt = Allocate (f, ty, alloc_size, recurse_stmt stmt) in

          if strides = [] then 
            (* skip for zero-dimensional funcs *)
            LetStmt (func ^ ".alloc_size", IntImm 1, stmt) 
          else                
            (* lets for strides *)
            let lhs = 
              (List.map (fun dim -> (dim ^ ".stride")) dim_names) @ 
                    [func ^ ".alloc_size"] in
            let rhs = (IntImm 1) :: (List.map2 ( *~ ) strides extent_computed) in
            (* lets for mins 
               Currently uses compute mins at this loop level *)                
            let lhs = lhs @ (List.map (fun dim -> (dim ^ ".buf_min")) dim_names) in
            let rhs = rhs @ min_computed in
            let make_let_stmt l r stmt = LetStmt (l, r, stmt) in
            List.fold_right2 make_let_stmt lhs rhs stmt
      | _ -> mutate_children_in_stmt recurse_expr recurse_stmt stmt
  in

  let functions = list_of_schedule schedule in
  let update stmt f =
    let (args, _, _) = find_function f env in
    let (call_sched, sched_list) = find_schedule schedule f in
    match call_sched with
      | Inline          (* Inline calls have already been folded away *)
      | Reuse _ -> stmt (* Reuse calls have been rewritten to call another buffer *)
      | _ ->
          let arg_names = List.map snd args in
          replace_calls_with_loads_in_stmt f arg_names stmt
  in
  List.fold_left update stmt functions 

let lower_image_calls (stmt:stmt) =

  (* Replace calls to images with loads from image. Accumulate all names of images found as a side-effect *) 
  let images = ref StringSet.empty in
  let rec walk_expr = function
    | Call (Image, t, name, args) ->
      images := StringSet.add name !images; 
      let idx = List.fold_right2 (fun arg n idx ->
        let img_min = Var (i32, name ^ ".min." ^ (string_of_int n)) in
        let img_stride = Var (i32, name ^ ".stride." ^ (string_of_int n)) in
        idx +~ (((walk_expr arg) -~ img_min) *~ img_stride) 
      ) args (0 -- (List.length args)) (IntImm 0) in
      Load (t, name, idx)
    | expr -> mutate_children_in_expr walk_expr expr
  in
  let rec walk_stmt stmt = mutate_children_in_stmt walk_expr walk_stmt stmt in
  
  let new_stmt = walk_stmt stmt in

  (* Add an assert at the start to make sure we don't load each image out of bounds *)
  (* TODO: this is slow because it traverses the entire AST, and subs's in let statements *)
  let asserts = StringSet.fold (fun name asserts -> 
    let msg = ("Function may load image " ^ name ^ " out of bounds") in
    let region = required_of_stmt name StringMap.empty stmt in
    let new_asserts = List.fold_right2 (fun range n asserts ->
      match range with
        | Unbounded -> failwith ("Unbounded use of input image " ^ name)
        | Range (min, max) -> 
          let img_min = Var (i32, name ^ ".min." ^ (string_of_int n)) in
          let img_extent = Var (i32, name ^ ".extent." ^ (string_of_int n)) in
          let check = (min >=~ img_min) &&~ ((max -~ min) <~ img_extent) in
          (Assert (check, msg)) :: asserts
    ) region (0 -- (List.length region)) asserts in
    new_asserts 
  ) !images [] in

  match new_stmt with 
    | Block stmts -> Block (asserts @ stmts)
    | stmt -> Block (asserts @ [new_stmt])

let lower_function (func:string) (env:environment) (schedule:schedule_tree) =

  (* dump pre-lowered form *)
  if 0 < verbosity then begin
    let out = open_out (func ^ ".def") in
    Printf.fprintf out "%s%!" (string_of_environment env);
    close_out out;
  end;

  (* dump initial schedule *)
  if 0 < verbosity then begin
    let out = open_out (func ^ ".schedule") in
    Printf.fprintf out "%s%!" (string_of_schedule_tree schedule);
    close_out out;
  end;


  (* ----------------------------------------------- *)
  dbg 1 "Computing the order in which to realize functions\n%!";

  (* A partial order on realization order of functions *)        
  let lt a b =
    (* If a requires b directly, a should be realized first *)
    if (string_starts_with b (a ^ ".")) then (Some true)
    else if (string_starts_with a (b ^ ".")) then (Some false)
    else
      let (call_sched_a, _) = (find_schedule schedule a) in
      let (call_sched_b, _) = (find_schedule schedule b) in
      (* If a reuses the computation of b, a should be 'realized'
         first (which just rewrites calls to a into calls to b) *)
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

  (* Topologically sort them *)
  let functions = partial_sort lt functions in
  dbg 1 "Realization order: %s\n" (String.concat ", " functions);

  (* ----------------------------------------------- *)
  dbg 1 "Fully qualifying symbols in the schedule\n%!";
  let schedule = qualify_schedule schedule in

  let dump_stmt stmt pass pass_desc suffix verb = 
    if verbosity > verb then begin
      let out = open_out (func ^ ".lowered_pass_" ^ (string_of_int pass) ^ "_" ^ suffix) in
      Printf.fprintf out "# %s\n\n" pass_desc;
      Printf.fprintf out "%s%!" (string_of_stmt stmt);
      close_out out;
    end;
  in

  let pass = 0 in

  (* ----------------------------------------------- *)
  let pass_desc = "Realizing initial statement" in
  dbg 1 "%s\n%!" pass_desc;

  let stmt = match realize func (Block []) env schedule with 
    | Pipeline (_, p, _) -> p
    | _ -> failwith "Realize didn't return a pipeline"
  in

  dump_stmt stmt pass pass_desc "initial" 1;

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Inserting out-of-bounds checks for output image" in
  dbg 1 "%s\n%!" pass_desc;

  let region = required_of_stmt func StringMap.empty stmt in
  let true_val = bool_imm true in
  let false_val = bool_imm false in
  let (check, _) = List.fold_left (fun (expr, count) range ->    
    match range with
      | Unbounded -> (expr &&~ false_val, count+1)
      | Range (min, max) ->
        let out_extent = (Var (i32, ".result.extent." ^ (string_of_int count))) in
        let out_min = (Var (i32, ".result.min." ^ (string_of_int count))) in
        (expr &&~
           (min >=~ out_min) &&~
           ((max -~ min) <=~ out_extent), 
         count+1))
    (true_val, 0) region in 
  let oob_check = Assert (check, "Function may access output image out of bounds") in

  dump_stmt (Block [oob_check; stmt]) pass pass_desc "oob_check" 1;

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  dbg 1 "Lowering function calls\n%!";
  let functions = List.filter (fun x -> x <> func) functions in  

  (* Inject a loop over root to help us realize functions scheduled as
     root. This will get constant-folded away later *)
  let stmt = For ("<root>", IntImm 0, IntImm 1, true, stmt) in
  let stmt = List.fold_left (fun stmt f ->     
    let pass_desc = "Lowering " ^ f in
    dbg 1 "%s\n%!" pass_desc;
    
    let stmt = lower_stmt f stmt env schedule in
    
    dump_stmt stmt pass pass_desc f 1;
    
    stmt) stmt functions 
    
  in
  
  let stmt = match stmt with 
    | For ("<root>", _, _, _, stmt) -> stmt
    | _ -> failwith "Lowering function calls didn't return a for loop over root\n";        
  in

  (* let stmt = Constant_fold.constant_fold_stmt stmt in *)

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Replacing input image references with loads" in
  dbg 1 "%s\n%!" pass_desc;

  (* For a later pass we'll need a list of all external images referenced *)
  let rec find_input_images = function
    | Call (Image, ty, f, args) -> StringSet.add f (string_set_concat (List.map find_input_images args))
    | expr -> fold_children_in_expr find_input_images StringSet.union StringSet.empty expr
  in
  let rec find_input_images_in_stmt = function
    | stmt -> 
      fold_children_in_stmt find_input_images find_input_images_in_stmt StringSet.union stmt
  in
  let input_images = StringSet.elements (find_input_images_in_stmt stmt) in

  (* Add back in the oob_check on the output image too *)
  let stmt = Block ([oob_check; stmt]) in  
  let stmt = lower_image_calls stmt in

  dump_stmt stmt pass pass_desc "image_loads" 1;
  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Bounds inference" in
  dbg 1 "%s\n%!" pass_desc;
  (* Updates stmt and schedule *)
  let stmt = For ("<root>", IntImm 0, IntImm 1, true, stmt) in
  let (stmt,schedule) = bounds_inference env schedule stmt in

  (* let stmt = Constant_fold.constant_fold_stmt stmt in *)

  dump_stmt stmt pass pass_desc "bounds_inference" 1;

  let pass = pass + 1 in


  (* ----------------------------------------------- *)
  let pass_desc = "Sliding window optimization" in
  dbg 1 "%s\n%!" pass_desc;
  (* Updates stmt and schedule *)
  let stmt = sliding_window stmt env in

  dump_stmt stmt pass pass_desc "sliding_window" 1;

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Storage folding optimization" in
  dbg 1 "%s\n%!" pass_desc;
  
  let stmt = storage_folding env stmt in

  dump_stmt stmt pass pass_desc "storage_folding" 1;

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Replace function references with loads and stores" in 
  dbg 1 "%s\n%!" pass_desc;

  let stmt = lower_function_calls stmt env schedule in

  dump_stmt stmt pass pass_desc "loads_and_stores" 1;

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Replacing loads and stores to the output with loads and stores to .result" in
  dbg 1 "%s\n%!" pass_desc;
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

  dump_stmt stmt pass pass_desc "result_stores" 1;

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Replace references to bounds of output function with bounds of output buffer" in
  dbg 1 "%s\n%!" pass_desc;
  let args,_,_ = find_function func env in
  let dim_names = List.map snd args in
  let dimensions = List.length args in

  let lhs = [] in
  let rhs = [] in
  let lhs = lhs @ (List.map (fun dim -> (func ^ "." ^ dim ^ ".buf_min")) dim_names) in
  let rhs = rhs @ (List.map (fun i -> Var (i32, ".result.min." ^ (string_of_int i))) (0 -- dimensions)) in
  let lhs = lhs @ (List.map (fun dim -> (func ^ "." ^ dim ^ ".min")) dim_names) in
  let rhs = rhs @ (List.map (fun i -> Var (i32, ".result.min." ^ (string_of_int i))) (0 -- dimensions)) in
  let lhs = lhs @ (List.map (fun dim -> (func ^ "." ^ dim ^ ".stride")) dim_names) in
  let rhs = rhs @ (List.map (fun i -> Var (i32, ".result.stride." ^ (string_of_int i))) (0 -- dimensions)) in
  let lhs = lhs @ (List.map (fun dim -> (func ^ "." ^ dim ^ ".extent")) dim_names) in
  let rhs = rhs @ (List.map (fun i -> Var (i32, ".result.extent." ^ (string_of_int i))) (0 -- dimensions)) in

  let make_let_stmt name value stmt = LetStmt (name, value, stmt) in
  let stmt = List.fold_right2 make_let_stmt lhs rhs stmt in

  dump_stmt stmt pass pass_desc "result_bounds" 1;

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Forcing innermost stride to 1 and mins to 0 on input and output buffers" in
  dbg 1 "%s\n%!" pass_desc;

  let input_images = ".result"::input_images in

  let stride_checks = 
    List.map 
      (fun name -> Assert (Var (i32, name ^ ".stride.0") =~ IntImm 1, 
                           Printf.sprintf "Stride on innermost dimension of %s must be 1" name))
      input_images
  in
  let stmt = List.fold_left
    (fun stmt name ->
      LetStmt (name ^ ".stride.0", IntImm 1, stmt))
    stmt input_images
  in

  let min_checks = 
    List.map 
      (fun name -> [
        Assert (Var (i32, name ^ ".min.0") =~ IntImm 0, 
                Printf.sprintf "Min on dimension 0 of %s must be 0" name);
        Assert (Var (i32, name ^ ".min.1") =~ IntImm 0,
                Printf.sprintf "Min on dimension 1 of %s must be 0" name);
        Assert (Var (i32, name ^ ".min.2") =~ IntImm 0, 
                Printf.sprintf "Min on dimension 2 of %s must be 0" name);
        Assert (Var (i32, name ^ ".min.3") =~ IntImm 0, 
                Printf.sprintf "Min on dimension 3 of %s must be 0" name)])
      input_images
  in
  let min_checks = List.concat min_checks in

  let stmt = List.fold_left
    (fun stmt name ->
      LetStmt (name ^ ".min.0", IntImm 0, 
      LetStmt (name ^ ".min.1", IntImm 0, 
      LetStmt (name ^ ".min.2", IntImm 0, 
      LetStmt (name ^ ".min.3", IntImm 0, stmt)))))
    stmt input_images
  in

  let stmts = stride_checks @ min_checks @ [stmt] in
  let stmt = Block stmts in
    
  dump_stmt stmt pass pass_desc "innermost_strides" 1;

  let pass = pass + 1 in

  (* ----------------------------------------------- *)
  let pass_desc = "Constant folding" in
  dbg 1 "%s\n%!" pass_desc;
  let stmt = Constant_fold.constant_fold_stmt stmt in
  let stmt = Constant_fold.remove_dead_lets_in_stmt stmt  in

  dump_stmt stmt pass pass_desc "final" 1;

  stmt





