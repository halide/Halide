
open Ir
open Schedule
open Analysis
open Util
open Ir_printer

(* A function definition: (name, args, return type, body) *)

(* Make a schedule which generates a basic legal schedule for the evaluation of a function over a region *)
let make_default_schedule (func: string) (env: environment) (region : (string * expr * expr) list) =
  (* Make an empty schedule *)
  let schedule = empty_schedule in

  (* Start with a parallel for over the function args over the region *)
  let f_schedule = List.map (fun (v, m, s) -> Parallel (v, m, s)) region in
  let schedule = set_schedule schedule func Root f_schedule in

  (* Find all sub-functions and mark them as inline *)
  let rec called_functions f =
    let (_, _, _, body) = Environment.find f env in

    let l = 

      let rec find_calls_expr = function
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

      match body with 
        | Extern -> StringSet.empty
        | Pure expr -> find_calls_expr expr
        | Impure (initial_value, modified_args, modified_value) ->
            let s = StringSet.union (find_calls_expr initial_value) (find_calls_expr modified_value) in
            string_set_concat (s::(List.map find_calls_expr modified_args))
    in

    (* Recursively find more calls in the called functions *)
    let calls = string_set_concat (l::(List.map called_functions (StringSet.elements l))) in

    (* Prefix them all with this function name. *)
    string_set_map (fun x -> f ^ "." ^ x) calls 
  in

  StringSet.fold (fun f s -> set_schedule s f Inline []) (called_functions func) schedule
    
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
          assert (Analysis.reduce_expr_modulo size factor = Some 0);
          [Split (var, newouter, newinner, min);
           Parallel (newinner, IntImm 0, IntImm factor);
           Parallel (newouter, IntImm 0, Constant_fold.constant_fold_expr (size /~ (IntImm factor)))]
      | Serial (v, min, size) when v = var ->
          assert (Analysis.reduce_expr_modulo size factor = Some 0);
          [Split (var, newouter, newinner, min);
           Serial (newinner, IntImm 0, IntImm factor);
           Serial (newouter, IntImm 0, Constant_fold.constant_fold_expr (size /~ (IntImm factor)))]
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
  (* Make a sub-schedule using the arg names and the region. We're
     assuming all the args are region args for now *)
  let make_sched name (min, size) = Parallel (name, min, size) in
  let sched_list = List.map2 make_sched args region in

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
  root_or_chunk_schedule func (Chunk var) args region schedule

let root_schedule func args region schedule = 
  root_or_chunk_schedule func Root args region schedule

let parallel_schedule (func: string) (var: string) (min: expr) (size: expr) (schedule: schedule_tree) =

  let calls = find_all_schedule schedule func in
  
  let set schedule func =
    let (call_sched, sched_list) = find_schedule schedule func in
    set_schedule schedule func call_sched ((Parallel (var, min, size))::sched_list)
  in
  
  List.fold_left set schedule calls

let serial_schedule (func: string) (var: string) (min: expr) (size: expr) (schedule: schedule_tree) =

  let calls = find_all_schedule schedule func in
  
  let set schedule func =
    let (call_sched, sched_list) = find_schedule schedule func in
    set_schedule schedule func call_sched ((Serial (var, min, size))::sched_list)
  in
  
  List.fold_left set schedule calls

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
