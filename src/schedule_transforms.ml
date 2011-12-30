
open Ir
open Schedule
open Analysis
open Util
open Ir_printer


type scheduling_guru = {
  (* maps from fully qualified function name, environment, schedule so
     far, list of legal call schedules, to a schedule tree entry *)
  decide : string -> environment -> schedule_tree -> call_schedule list -> (call_schedule * schedule list)
}

let novice = {
  decide = fun func env sched options ->
    Printf.printf
      "Guru options for %s:\n  %s\n%!"
      func
      (String.concat "\n  " (List.map string_of_call_schedule options));
    
    (* Find the pure arguments *)
    let (args, _, body) = find_function func env in

    (* Also grab any reduction domain args *)
    let (reduction_args, is_update) = 
      if String.contains func '.' then
        match find_function (parent_name func) env with 
          | (_, _, Reduce (_, _, _, domain)) ->
              (List.map (fun (n, _, _) -> (Int 32, n)) domain, true)
          | _ -> ([], false)
      else ([], false)
    in

    let args = reduction_args @ args in

    (* let prefix = if is_update then "" else ((base_name func) ^ ".") in *)
    let prefix = (base_name func) ^ "." in

    (Root,
     List.map
       (fun (t,nm) ->
         Serial (nm, Var (t, prefix ^ nm ^ ".min"), Var (t, prefix ^ nm ^ ".extent")))
       args)
}

(* Make a schedule which evaluates a function over a region *)
let generate_schedule (func: string) (env: environment) (guru: scheduling_guru) =
  
  (* func: fully qualified function name we're making a decision
     for. Refers to a specific call-site (or group thereof). *)
  (* env: all relevant function bodies *)
  (* vars_in_scope: The loop variables for containing loops of this call-site *)
  (* bufs_in_scope: maps from unqualified function names to a list of
     loop vars that contain a realization of that function. If that var
     is in scope then so is the buffer. *)
  (* sched: the schedule so far. We return an updated copy. *)
  let rec inner (func: string) (env: environment) (vars_in_scope: string list) (bufs_in_scope: ((string*string) list) StringMap.t) (sched: schedule_tree) =

    (* determine legal call schedules *)
    (*
      Inline:
      - only things with parent
      - not reductions (if they want to be inline-like they should instead chunk over innermost)
      
      Root:
      - always legal

      Chunk:
      - legal over any vars in scope

      Coiterate:
      - legal over any *serial* vars in scope

      Reuse:
      - legal as long as a realization of the same function is in scope
    *)
    (* analyze the function *)
    let _,_,body = find_function func env in
    assert (body <> Extern);

    let is_reduce fname = match find_function fname env with _,_,Reduce _ -> true | _ -> false in

    let is_reduction = is_reduce func in
    let has_parent = String.contains func '.' in

    let is_reduction_update =
      if has_parent then
        is_reduce (parent_name func)
      else
        false
    in    

    (* enumerate all options *)
    let call_sched_options =
      let inline_options =
        if has_parent && not is_reduction && not is_reduction_update then
          [Inline]
        else
          []
      in

      let root_options = [Root] in

      let chunk_options = List.map (fun v -> Chunk v) vars_in_scope in

      (* TODO: coiterate options *)
      let coiterate_options = [] in

      let reuse_options =
        let realizations =
          try
            StringMap.find (base_name func) bufs_in_scope
          with Not_found ->
            []
        in
        let options =
          List.filter
          (* realization is in scope here (including root = "") *)
            (fun (var,_) -> List.mem var vars_in_scope || var = "")
            realizations
        in
        List.map
          (fun (_,realization) -> Reuse realization)
          options
      in
      inline_options @ root_options @ chunk_options @ coiterate_options @ reuse_options
    in

    let call_sched_options = 
      if is_reduction_update then begin
        let (parent_call_sched, _) = find_schedule sched (parent_name func) in
        [parent_call_sched]
      end else call_sched_options
    in

    (*
      Variable decisions (made in guru subcomponent)
      - pick arg from pending list to schedule next
      - pick any legal schedule for arg
    *)
    
    let call_sched,sched_list = guru.decide func env sched call_sched_options in

    (* Update sched using the decisions made *)
    let sched = set_schedule sched func call_sched sched_list in

    (* Update vars_in_scope according to the decision made *)
    (* prune stuff we're outside *)
    let vars_in_scope = match call_sched with
      | Root -> []
      | Coiterate (var,_,_)
      | Chunk var ->
          (list_take_while
             (fun x -> x <> var)
             vars_in_scope)
          @ [var]
      | Reuse _ -> vars_in_scope (* doesn't matter - never used because it has no children *)
      | Inline -> vars_in_scope
    in
    
    (* add new vars *)
    let vars_in_scope = vars_in_scope @ (
      let rec find_vars = function
        | (Serial (v,_,_))::rest
        | (Parallel (v,_,_))::rest -> (func ^ "." ^ v) :: find_vars rest
        | _::rest -> find_vars rest
        | [] -> []
      in
      find_vars sched_list
    ) in

    let add_realization var bufs_in_scope =
      let existing =
        try
          StringMap.find (base_name func) bufs_in_scope
        with Not_found -> []
      in
      StringMap.add (base_name func) ((var,func)::existing) bufs_in_scope
    in
      
    let bufs_in_scope = match call_sched with
      | Root -> add_realization "" bufs_in_scope
      | Chunk var
      | Coiterate (var,_,_) -> add_realization var bufs_in_scope
      | Reuse _
      | Inline -> bufs_in_scope
    in

    (* Find called functions (that aren't extern) and recurse *)
    let rec find_calls_expr = function
      (* skip externs *)
      | Call (_, name, args) when name.[0] = '.' ->
          (string_set_concat (List.map find_calls_expr args))
      | Call (_, name, args) when List.mem name (split_name func) ->
          (string_set_concat (List.map find_calls_expr args))
      | Call (_, name, args) -> 
          let rest = (string_set_concat (List.map find_calls_expr args)) in
          StringSet.add (func ^ "." ^ name) rest
      | x -> fold_children_in_expr find_calls_expr StringSet.union (StringSet.empty) x
    in
    
    let new_found_calls = 
      match body with 
        | Extern -> raise (Wtf "enumerating schedules for extern function")
        | Pure expr -> find_calls_expr expr
        | Reduce (init_expr, update_args, update_func, bounds) ->
            let s = StringSet.add (func ^ "." ^ update_func) (find_calls_expr init_expr) in
            string_set_concat (s::(List.map find_calls_expr update_args))
    in

    Printf.printf " new_found_calls -> %s\n%!" (String.concat ", " (StringSet.elements new_found_calls));

    let bufs_in_scope,sched =
      StringSet.fold
        (fun nm (bufs_in_scope,sched) ->
          inner nm env vars_in_scope bufs_in_scope sched)
        new_found_calls
        (bufs_in_scope,sched)
    in

    (bufs_in_scope,sched)

  in

  let _,sched = inner func env [] StringMap.empty empty_schedule in
  
  (* Do any post-processing of the schedule *)

  sched
  

(* A function definition: (name, args, return type, body) *)

(* Make a schedule which generates a basic legal schedule for the evaluation of a function over a region *)
let make_default_schedule (func: string) (env: environment) (region : (string * expr * expr) list) =
  (* Make an empty schedule *)
  let schedule = empty_schedule in

  (* Start with a for over the function args over the region *)
  let f_schedule = List.map (fun (v, m, s) -> Serial (v, m, s)) region in
  let schedule = set_schedule schedule func Root f_schedule in

  (* Find all sub-functions and mark them as inline *)
  let rec called_functions f found_calls =

    Printf.printf "-> %s\n%!" f;

    let (_, _, body) = find_function f env in

    Printf.printf " found_calls -> %s\n%!" (String.concat ", " (StringSet.elements found_calls));

    let rec find_calls_expr = function
      | Call (_, name, args) when name.[0] = '.' ->
          (string_set_concat (List.map find_calls_expr args))
      | Call (_, name, args) when List.mem name (split_name f) ->
          (string_set_concat (List.map find_calls_expr args))
      | Call (_, name, args) -> 
          let rest = (string_set_concat (List.map find_calls_expr args)) in
          StringSet.add name rest
      | x -> fold_children_in_expr find_calls_expr StringSet.union (StringSet.empty) x
    in
    
    let rec find_calls_stmt stmt =
      fold_children_in_stmt find_calls_expr find_calls_stmt StringSet.union stmt 
    in
    
    let new_found_calls = 
      match body with 
        | Extern -> StringSet.empty
        | Pure expr -> find_calls_expr expr
        | Reduce (init_expr, update_args, update_func, bounds) ->
            let s = StringSet.add update_func (find_calls_expr init_expr) in
            string_set_concat (s::(List.map find_calls_expr update_args))
    in

    let new_found_calls = string_set_map (fun x -> f ^ "." ^ x) new_found_calls in

    Printf.printf " new_found_calls -> %s\n%!" (String.concat ", " (StringSet.elements new_found_calls));

    let new_found_calls = StringSet.diff new_found_calls found_calls in

    Printf.printf " after exclusion -> %s\n%!" (String.concat ", " (StringSet.elements new_found_calls));

    (* Recursively find more calls in the called functions *)
    let found_calls = StringSet.union new_found_calls found_calls in
    let found_calls = List.fold_right called_functions (StringSet.elements new_found_calls) found_calls in

    (* Prefix them all with this function name. *)
    found_calls
  in

  let rec choose_schedule f s = 
    (* If there's no dot in our name, we're the root and have already been scheduled *)
    if not (String.contains f '.') then s else
      let (args, _, body) = find_function f env in
      let (call_sched, sched_list ) = begin match body with
        (* I'm a reduction *)
        | Reduce (_, _, _, _) -> 
            let f = base_name f in
            (Root, List.map (fun (t, n) -> Serial (n, Var (t, f ^ "." ^ n ^ ".min"),
                                                   Var (t, f ^ "." ^ n ^ ".extent"))) args)            
        | _ ->            
            let parent = parent_name f in
            let (parent_args, _, parent_body) = find_function parent env in
            match parent_body with 
              (* I'm the update step of a reduction *)
              | Reduce (_, update_args, update_func, domain) when update_func = base_name f ->
                  let s = choose_schedule parent s in
                  let (parent_call_sched, parent_sched_list) = find_schedule s parent in
                  
                  let rec get_gather_args = function
                    | (Var (t, n)::rest) when List.mem (t, n) parent_args -> 
                        (Serial (n, Var (t, n ^ ".min"), Var(t, n ^ ".extent")))::(get_gather_args rest)
                    | _::rest -> get_gather_args rest
                    | [] -> []
                  in
                  let reduce_args = (List.map (fun (n, m, s) -> Serial (n, m, s)) domain) in
                  let gather_args = get_gather_args update_args in
                  (Inline, reduce_args @ gather_args)
                    
              (* I'm not a reduction or the update step of a reduction *)
              | _ -> (Inline, [])
      end in set_schedule s f call_sched sched_list 
  in
  
  let schedule = StringSet.fold choose_schedule (called_functions func StringSet.empty) schedule in  

  schedule
    
(* Add a split to a schedule *)
let split_schedule (func: string) (var: string) (newouter: string) 
    (newinner: string) (factor: int) (schedule: schedule_tree) =

  Printf.printf "Splitting %s into %s * %d + %s in %s\n" var newouter factor newinner func;

  (* Find all the calls to func in the schedule *)
  let calls = find_all_schedule schedule func in

  let set schedule func = 
    let (call_sched, sched_list) = find_schedule schedule func in
    (* Find var in the sched_list *)
    let fix = function
      | Parallel (v, min, size) when v = var ->
          Printf.printf "%s %s %s\n" v (string_of_expr min) (string_of_expr size);
          (* assert (Analysis.reduce_expr_modulo size factor = Some 0); *)
          [Split (var, newouter, newinner, min);
           Parallel (newinner, IntImm 0, IntImm factor);
           Parallel (newouter, IntImm 0, Constant_fold.constant_fold_expr ((size +~ (IntImm (factor-1))) /~ (IntImm factor)))]
      | Serial (v, min, size) when v = var ->
          (* assert (Analysis.reduce_expr_modulo size factor = Some 0); *)
          [Split (var, newouter, newinner, min);
           Serial (newinner, IntImm 0, IntImm factor);
           Serial (newouter, IntImm 0, Constant_fold.constant_fold_expr ((size +~ (IntImm (factor-1))) /~ (IntImm factor)))]
      | x -> [x]
    in
    let sched_list = List.concat (List.map fix sched_list) in
    set_schedule schedule func call_sched sched_list
  in
  List.fold_left set schedule calls

(* Vectorize a parallel for *)
let vectorize_schedule (func: string) (var: string) (schedule: schedule_tree) =

  (* Find all the calls to func in the schedule *)
  let calls = find_all_schedule schedule func in

  let set schedule func = 
    let (call_sched, sched_list) = find_schedule schedule func in
    (* Find var in the sched_list *)
    Printf.printf "Vectorizing %s over %s\n%!" func var;
    let fix = function
      | Serial (v, min, size) 
      | Parallel (v, min, size) when v = var ->
          begin match size with 
            | IntImm x -> Vectorized (v, min, x)
            | _ -> raise (Wtf "Can't vectorize a var with non-const bounds")
          end
      | x -> x
    in    
    let sched_list = List.map fix sched_list in
    set_schedule schedule func call_sched sched_list
  in
  List.fold_left set schedule calls

(* Unroll a parallel or serial for *)
let unroll_schedule (func: string) (var: string) (schedule: schedule_tree) =

  (* Find all the calls to func in the schedule *)
  let calls = find_all_schedule schedule func in
  
  let set schedule func =
    let (call_sched, sched_list) = find_schedule schedule func in
    (* Find var in the sched_list *)
    Printf.printf "Unrolling %s over %s\n%!" func var;
    let fix = function
      | Serial (v, min, size) 
      | Parallel (v, min, size) when v = var ->
          begin match size with 
            | IntImm x -> Unrolled (v, min, x)
            | _ -> raise (Wtf "Can't unroll a var with non-const bounds")
          end
      | x -> x
    in
    let sched_list = List.map fix sched_list in
    set_schedule schedule func call_sched sched_list
  in
  List.fold_left set schedule calls

(* Push one var to be outside another *)
let transpose_schedule (func: string) (outer: string) (inner: string) (schedule: schedule_tree) = 
  (* Find all the calls to func in the schedule *)
  let calls = find_all_schedule schedule func in

  let set schedule func =
    let (call_sched, sched_list) = find_schedule schedule func in
    (* Find var in the sched_list *)
    Printf.printf "Moving %s outside %s in %s\n%!" outer inner func;
    let rec fix l x = match l with
      | [] -> raise (Wtf (inner ^ " does not exist in this schedule"))
      | ((Serial (v, _, _))::rest)
      | ((Parallel (v, _, _))::rest)
      | ((Vectorized (v, _, _))::rest) 
      | ((Unrolled (v, _, _))::rest) ->
          if v = outer then begin
            Printf.printf "Found %s\n" outer;
            fix rest (Some (List.hd l))
          end else if v = inner then begin
            Printf.printf "Found %s\n" inner;
            begin match x with 
              | Some x -> (List.hd l) :: (x :: rest)
              | None -> raise (Wtf (outer ^ "is already outside" ^ inner ^ "\n"))
            end
          end else begin
            (List.hd l)::(fix rest x)
          end
      | (first::rest) -> first :: (fix rest x)
    in
    let sched_list = fix sched_list None in
    set_schedule schedule func call_sched sched_list  
  in
  List.fold_left set schedule calls

let root_or_chunk_schedule (func: string) (call_sched: call_schedule)
    (args: string list) (region: (expr * expr) list) (schedule: schedule_tree) = 

  Printf.printf "root or chunk %s with region %s\n" func 
    (String.concat ", " (List.map (fun (x, y) -> "[" ^ string_of_expr x ^ ", " ^ string_of_expr y ^ "]") region));

  (* Make a sub-schedule using the arg names and the region. We're
     assuming all the args are region args for now *)
  let make_sched name (min, size) = Serial (name, min, size) in
  let sched_list = match region with
    (* To be inferred later *)
    | [] -> List.map (fun n -> make_sched n (Var (Int 32, func ^ "." ^ n ^ ".min"),
                                             Var (Int 32, func ^ "." ^ n ^ ".extent"))) args
    | _ -> List.map2 make_sched args region 
  in

  (* Find all the calls to func in the schedule. Sort them
     lexicographically so we can always mark the first one returned as
     the chunk provider and the others as reuse. Otherwise we risk
     marking a node as reuse when it has chunk providers as
     children. *)
  let string_cmp s1 s2 = if s1 < s2 then -1 else 1 in
  Printf.printf "Looking up %s in schedule\n%!" func;
  let unsorted = (find_all_schedule schedule func) in
  let (first, rest) = match (List.sort string_cmp unsorted) with
    | (a::b) -> (a, b)
    | [] -> raise (Wtf ("Could not find " ^ func ^ " in schedule\n"))
  in

  (* Set one to be chunked over var with the given region. Tell the
     others to reuse this chunk. *)

  (* Set each one to be chunked over var with the given region. Note
     that this doesn't allow for different callsites to be chunked
     differently! Maybe the argument to this function should be a
     fully qualified call? *)
  let set sched call =
    set_schedule sched call (Reuse first) []
  in

  Printf.printf "Done chunking\n%!";

  let chunk_first = set_schedule schedule first call_sched sched_list in

  List.fold_left set chunk_first rest

let chunk_schedule func var args region schedule = 
  (* Find all the possible vars that chunk could refer to, and call
     root_or_chunk_schedule once within each context *)

  (* Figure out all the possible vars this could refer to *)
  let rec descend enclosing_var context f (call_sched, sched_list, sched) found_vars =
    if f = func then 
      enclosing_var :: found_vars
    else

      let subcontext = if context = "" then f else (context ^ "." ^ f) in

      let rec var_in_sched_list = function
        | [] -> false
        | (Serial (v, _, _)::rest)
        | (Parallel (v, _, _)::rest)
        | (Vectorized (v, _, _)::rest)
        | (Unrolled (v, _, _)::rest)
        | (Split (v, _, _, _)::rest) when v = var -> true
        | (_::rest) -> var_in_sched_list rest
      in

      let enclosing_var =
        if var_in_sched_list sched_list then
          (* The enclosing var falls out of scope because a new var by the same name hides it *)
          Some subcontext 
        else match call_sched with
          (* The enclosing function is also an enclosing scope, so it's still valid *)
          | Inline -> enclosing_var 
          (* The calling function is not an enclosing scope, so the enclosing var gets nuked *)
          | Root -> None

          (* The var may or may not have fallen out of scope *)
          | Chunk chunk_dim -> 
              let idx_after_last_dot =
                try (String.rindex chunk_dim '.' + 1)
                with Not_found -> raise (Wtf ("chunk dimensions must be fully qualified: " ^ chunk_dim))
              in
              let chunk_var = String.sub chunk_dim idx_after_last_dot (String.length chunk_dim - idx_after_last_dot)
              and chunk_parent = String.sub chunk_dim 0 (idx_after_last_dot-1) in

              if chunk_var = var then enclosing_var else                
                let (_, chunk_sched_list) = find_schedule schedule chunk_parent in
                (* enclosing var is still in scope if we hit a var of
                   the same name earlier in the sched list of the
                   parent *)
                let rec var_occurs_before_chunk_var = function
                  (* If we don't find chunk_var something is wrong *)
                  | [] -> raise (Wtf ("Bad chunk dimension detected: " ^ chunk_dim))
                  (* We can't split on a chunk var *)
                  | (Split (v, _, _, _)::rest) ->
                      if v = chunk_var then
                        raise (Wtf "Can't split on a chunk var")
                      else
                        var_occurs_before_chunk_var rest
                  | (Serial (v, _, _)::rest)
                  | (Parallel (v, _, _)::rest)
                  | (Vectorized (v, _, _)::rest)
                  | (Unrolled (v, _, _)::rest) ->
                      if v = var then
                        true
                      else if v = chunk_var then
                        false 
                      else
                        var_occurs_before_chunk_var rest
                in
                if var_occurs_before_chunk_var chunk_sched_list then enclosing_var else None
              
          (* Reuse has no children, so it doesn't matter what we
             return here. Let's say that the enclosing var is still
             valid. *)
          | Reuse _ -> enclosing_var 
      in

      let Tree map = sched in
      StringMap.fold (descend enclosing_var subcontext) map found_vars
  in

  let Tree map = schedule in
  let contexts = StringMap.fold (descend None "") map [] in

  let string_of_context = function
    | None -> "None"
    | Some v -> "Some " ^ v
  in

  Printf.printf "Contexts in which I want to chunk %s over %s: %s\n%!"
      func var (String.concat ", " (List.map string_of_context contexts));

  (* TODO: allow different meanings in different places *)
  match contexts with 
      (*
    | [Some first] ->
        root_or_chunk_schedule func (Chunk (first ^ "." ^ var)) args region schedule        
      *)  
    | (Some first)::rest when List.for_all (fun x -> x = (Some first)) rest ->  (* single unambiguous meaning *)
        root_or_chunk_schedule func (Chunk (first ^ "." ^ var)) args region schedule 
    | _ -> 
        (* Ambiguous or ill-defined. Fall back to Root *)
        root_or_chunk_schedule func Root args region schedule

(* For a fully qualified function, what are all the variables
let all_vars_in_scope func *)

let root_schedule func args region schedule = 
  root_or_chunk_schedule func Root args region schedule

let parallel_schedule (func: string) (var: string) (schedule: schedule_tree) =

  let calls = find_all_schedule schedule func in
  
  let rec fix = function
    | [] -> []
    | (Serial (n, min, size))::rest when n = var ->
        (Parallel (n, min, size))::rest
    | first::rest -> first::(fix rest)
  in

  let set schedule func =
    let (call_sched, sched_list) = find_schedule schedule func in
    set_schedule schedule func call_sched (fix sched_list)
  in
  
  List.fold_left set schedule calls

(*
let serial_schedule (func: string) (var: string) (min: expr) (size: expr) (schedule: schedule_tree) =

  let calls = find_all_schedule schedule func in
  
  let set schedule func =
    let (call_sched, sched_list) = find_schedule schedule func in
    set_schedule schedule func call_sched ((Serial (var, min, size))::sched_list)
  in
  
  List.fold_left set schedule calls
*)

(*
  let infer_regions (env: environment) (schedule: schedule_tree) =
  let rec infer (func: string) (schedule: schedule_tree) (region: (expr * expr) list) =
  let (Tree map) = schedule in

(* Grab the body of the function in question *)
    let (_, args, return_type, body) = Environment.find func env in

    (* Compute and update the region for a callee given a region for the caller *)
    let update_region callee value map =
      (* Unpack the current schedule for the callee *)
      let (call_sched, sched_list, sub_sched) = value in

      (* Create the binding from variable names to ranges *)
      let add_binding map (arg_type, arg_name) (min, max) = StringMap.add arg_name (min, max) map
      let bindings = List.fold_left2 add_binding StringMap.empty args region in

      (* Given that binding, inspect the body of the caller to see what callee regions are used *)
      let callee_region = 
        match body with 
          | Pure expr -> required_of_expr callee bindings expr 
          | Impure (init_val, update_loc, update_val) ->
              let init_region = required_of_expr callee bindings init_val in
              (* Make a new binding for the iteration domain of the update step *)
              let new_bindings = 
in



      (* Update the sched_list to cover this region *)
      let new_sched_list = some function of callee_region in
      

      (* Set it, and recursively descend the subtree *)
      StringMap.add key (call_sched, new_sched_list, infer key sub_sched callee_region)
    in

    (* Update all keys (recursively descending) *)
    Tree (StringMap.fold update_region map)

(* Compute the region over which a (fully-qualified) function should be realized to satisfy everyone who uses it *)
let required_region (func: string) (env: environment) (schedule: schedule_tree) =


  (* Find the schedule for this function, and all other schedules that are marked to reuse it *)  
  let is_instances name call_sched _ = ((name = func) || (call_sched = Reuse func)) in
  let instances = filter_schedule schedule is_user in
  let users = List.map (fun name call_sched sched_list

  (* Find the region over which each will be computed (which may require some recursion) *)
  let user_regions = 
    let user_region name call_sched sched_list =
      match call_sched with 
        | Chunk _ | Coiterate | Root ->
        | Inline | Reuse
    in
    List.map user_region users
  in

  (* Convert that information to the region that each requires of this function *)
  let required_regions = in

  (* Take the union of all the regions and update the schedule tree *)
  let union = in

  set_schedule ...
*)
