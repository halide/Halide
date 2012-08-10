
open Ir
open Schedule
open Analysis
open Util
open Ir_printer


type scheduling_guru = {
  (* maps from fully qualified function name, environment, list of
     legal call schedules, to a schedule tree entry *)
  decide : string -> environment -> call_schedule list -> (call_schedule * schedule list);
  (* A sequence of strings describing this schedule *)
  serialized : string list
}

let novice = {
  serialized = ["novice"];
  decide = fun func env options ->

    (* Find the pure arguments *)
    let (args, _, body) = find_function func env in

    (* Also grab any reduction domain args *)
    let (reduction_args, is_update) = 
      if String.contains func '.' then
        match find_function (parent_name func) env with 
          | (_, _, Reduce (_, _, update_func, domain)) when update_func = (base_name func) ->
              (List.map (fun (n, _, _) -> (Int 32, n)) domain, true)
          | _ -> ([], false)
      else ([], false)
    in

    let args = args @ reduction_args in

    let prefix = (base_name func) ^ "." in

    (* Pick the first option *)
    let call_sched = List.hd options in

    let sched_list = 
      match call_sched with
        (* If it's inline or reuse, the sched_list is empty *)
        | Reuse _ | Inline -> []
        (* Otherwise just make a basic serial schedule *)
        | _ -> 
            List.map
              (fun (t,nm) ->
                Serial (nm, Var (t, prefix ^ nm ^ ".min"), Var (t, prefix ^ nm ^ ".extent")))
              args
    in

    (call_sched, sched_list)
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

      Reuse:
      - legal as long as a realization of the same function is in scope
    *)
    (* analyze the function *)
    let _,_,body = find_function func env in

    let is_reduce fname = match find_function fname env with _,_,Reduce _ -> true | _ -> false in

    let is_reduction = is_reduce func in
    let has_parent = String.contains func '.' in

    let is_reduction_update =
      if has_parent then begin
        match find_function (parent_name func) env with
          | (_, _, Reduce (_, _, update_name, _)) when update_name = base_name func -> true
          | _ -> false
      end else
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
      reuse_options @ inline_options @ root_options @ coiterate_options @ chunk_options
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

    dbg 2 "Asking guru to decide for %s from these options: %s\n%!"
      func
      (String.concat ", " (List.map string_of_call_schedule call_sched_options));
    
    let (call_sched, sched_list) = guru.decide func env call_sched_options in

    dbg 2 "Decision made for %s: %s %s\n%!"
      func
      (string_of_call_schedule call_sched)
      (String.concat ", " (List.map string_of_schedule sched_list));
      

    (* Update sched using the decisions made *)
    let sched = set_schedule sched func call_sched sched_list in

    (* Update vars_in_scope according to the decision made *)
    (* prune stuff we're outside *)
    let vars_in_scope = match call_sched with
      | Root -> []
      | Chunk var -> list_drop_while (fun x -> x <> var) vars_in_scope
      | Reuse _ (* doesn't matter - never used because it has no children *)
      | Inline -> vars_in_scope
    in
    
    (* add new vars *)
    let vars_in_scope = (
      let rec find_vars = function
        | (Serial (v,_,_))::rest
        | (Parallel (v,_,_))::rest -> (func ^ "." ^ v) :: find_vars rest
        | _::rest -> find_vars rest
        | [] -> vars_in_scope
      in
      find_vars sched_list
    ) in

    dbg 2 "Vars_in_scope after deciding fate of %s: %s\n%!"
      func
      (String.concat ", " vars_in_scope);

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
      | Chunk var -> add_realization var bufs_in_scope
      | Reuse _
      | Inline -> bufs_in_scope
    in

    let should_recurse = match call_sched with
      | Reuse _ -> false
      | _ -> true
    in

    if should_recurse then begin
      (* Find called functions (that aren't extern) and recurse *)
      let rec find_calls_expr = function
        | Call (Func, _, name, args) when List.mem name (split_name func) ->
            (* Call to self *)
            (string_set_concat (List.map find_calls_expr args))
        | Call (Func, _, name, args) -> 
            let rest = (string_set_concat (List.map find_calls_expr args)) in
            StringSet.add (func ^ "." ^ name) rest
        | x -> fold_children_in_expr find_calls_expr StringSet.union (StringSet.empty) x
      in
    
      let new_found_calls = 
        match body with 
          | Pure expr -> find_calls_expr expr
          | Reduce (init_expr, update_args, update_func, bounds) ->
              let s = StringSet.add (func ^ "." ^ update_func) (find_calls_expr init_expr) in
              string_set_concat (s::(List.map find_calls_expr update_args))
      in

      StringSet.fold
        (fun nm (bufs_in_scope,sched) ->
          inner nm env vars_in_scope bufs_in_scope sched)
        new_found_calls
        (bufs_in_scope,sched)
    end else (bufs_in_scope, sched)
      

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

    (* Printf.printf "-> %s\n%!" f; *)

    let (_, _, body) = find_function f env in

    dbg 2 " found_calls -> %s\n%!" (String.concat ", " (StringSet.elements found_calls));

    let rec find_calls_expr = function
      | Call (Func, _, name, args) when List.mem name (split_name f) ->
          (string_set_concat (List.map find_calls_expr args))
      | Call (Func, _, name, args) -> 
          let rest = (string_set_concat (List.map find_calls_expr args)) in
          StringSet.add name rest
      | x -> fold_children_in_expr find_calls_expr StringSet.union (StringSet.empty) x
    in
    
    let rec find_calls_stmt stmt =
      fold_children_in_stmt find_calls_expr find_calls_stmt StringSet.union stmt 
    in
    
    let new_found_calls = 
      match body with 
        | Pure expr -> find_calls_expr expr
        | Reduce (init_expr, update_args, update_func, bounds) ->
            let s = StringSet.add update_func (find_calls_expr init_expr) in
            string_set_concat (s::(List.map find_calls_expr update_args))
    in

    let new_found_calls = string_set_map (fun x -> f ^ "." ^ x) new_found_calls in

    dbg 2 " new_found_calls -> %s\n%!" (String.concat ", " (StringSet.elements new_found_calls));

    let new_found_calls = StringSet.diff new_found_calls found_calls in

    dbg 2 " after exclusion -> %s\n%!" (String.concat ", " (StringSet.elements new_found_calls));

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
    
(*
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
*)

(* A guru that uses a sub-guru, then mutates the resulting schedule list *)
let mutate_sched_list_guru (func: string) (mutator: schedule list -> schedule list) (serialized: string) (guru: scheduling_guru) = {
  serialized = guru.serialized @ [serialized];
  decide = fun f env legal_call_scheds ->
    let (call_sched, sched_list) = guru.decide f env legal_call_scheds in
    if (base_name f = func && (sched_list <> [])) then begin
      dbg 2 "Mutating schedule list for %s: %s -> %!"
        func
        (String.concat ", " (List.map string_of_schedule sched_list));
      let new_sched_list = mutator sched_list in
      dbg 2 "%s\n%!"
        (String.concat ", " (List.map string_of_schedule new_sched_list));
      (call_sched, new_sched_list)
    end else (call_sched, sched_list)
}

(* A guru that uses a sub-guru, mutating the legal call schedules *)
let mutate_legal_call_schedules_guru (func: string) (mutator: call_schedule list -> call_schedule list) (serialized: string) (guru: scheduling_guru) = {
  serialized = guru.serialized @ [serialized];
  decide = fun f env options ->
    if (base_name f = func) then begin
      dbg 2 "Winnowing call schedule options for %s: %s -> %!"
        func
        (String.concat ", " (List.map string_of_call_schedule options));
      let new_options = mutator options in    
      dbg 2 "%s\n%!"
        (String.concat ", " (List.map string_of_call_schedule new_options));
      guru.decide f env new_options
    end else
      guru.decide f env options
}  

(* Vectorize a parallel for *)
let vectorize_schedule (func: string) (var: string) (guru: scheduling_guru) =
  let serialized = Printf.sprintf "vectorize %s %s" func var in
  let mutate = function
    | Serial (v, min, size) 
    | Parallel (v, min, size) when v = var ->
        begin match size with 
          | IntImm x -> Vectorized (v, min, x)
          | _ -> failwith "Can't vectorize a var with non-const bounds"
        end
    | x -> x
  in mutate_sched_list_guru func (List.map mutate) serialized guru


(* Unroll a for *)
let unroll_schedule (func: string) (var: string) (guru: scheduling_guru) =
  let serialized = Printf.sprintf "unroll %s %s" func var in
  let mutate = function
    | Serial (v, min, size) 
    | Parallel (v, min, size) when v = var ->
        begin match size with 
          | IntImm x -> Unrolled (v, min, x)
          | _ -> failwith "Can't unroll a var with non-const bounds"
        end
    | x -> x
  in mutate_sched_list_guru func (List.map mutate) serialized guru

(* Mark explicit bounds on a var *)
let bound_schedule (func: string) (var: string) (min: expr) (size: expr) (guru: scheduling_guru) =
  let serialized = Printf.sprintf "bound %s %s %s %s" func var (string_of_expr min) (string_of_expr size) in
  let mutate = function
    (* TODO: old_min and old_size will be dynamically evaluated to the
       area used. We should inject a runtime assert that these fall
       within min and size (the area we're telling it to compute) *)
    | Serial (v, old_min, old_size) when v = var ->
        Serial (v, min, size) 
    | Parallel (v, old_min, old_size) when v = var ->
        Parallel (v, min, size)
    | x -> x
  in mutate_sched_list_guru func (List.map mutate) serialized guru

(* Unroll a for *)
let parallel_schedule (func: string) (var: string) (guru: scheduling_guru) =
  let serialized = Printf.sprintf "parallel %s %s" func var in
  let mutate = function
    | Serial (v, min, size) when v = var -> Parallel (v, min, size)
    | x -> x
  in mutate_sched_list_guru func (List.map mutate) serialized guru


let split_schedule (func: string) (var: string) (outer: string) (inner: string) (n: expr) (guru: scheduling_guru) =
  let int_n = match n with
    | IntImm x -> x
    | _ -> failwith "Can only handle const integer splits for now"
  in
  let serialized = Printf.sprintf "split %s %s %s %s %d" func var outer inner int_n in
  let rec mutate = function
    | (Parallel (v, min, size))::rest when v = var ->
        (Split (v, outer, inner, min))::
          (Parallel (inner, IntImm 0, n))::
          (Parallel (outer, IntImm 0, (size +~ n -~ (IntImm 1)) /~ n))::
          rest
    | (Serial (v, min, size))::rest when v = var -> 
        (Split (v, outer, inner, min))::
          (Serial (inner, IntImm 0, n))::
          (Serial (outer, IntImm 0, (size +~ n -~ (IntImm 1)) /~ n))::
          rest
    | first::rest -> first::(mutate rest)
    | [] -> failwith ("Did not find variable " ^ var ^ " in the schedule for " ^ func ^ "\n%!")
  in mutate_sched_list_guru func mutate serialized guru

(* Push one var to be outside another *)
let transpose_schedule (func: string) (outer: string) (inner: string) (guru: scheduling_guru) = 
  let serialized = Printf.sprintf "transpose %s %s %s" func outer inner in
  let rec mutate x l = match l with
    | [] -> failwith (inner ^ " does not exist in this schedule")
    | ((Serial (v, _, _))::rest)
    | ((Parallel (v, _, _))::rest)
    | ((Vectorized (v, _, _))::rest) 
    | ((Unrolled (v, _, _))::rest) ->
        if v = outer then mutate (Some (List.hd l)) rest
        else if v = inner then match x with 
          | Some x -> (List.hd l) :: (x :: rest)
          | None -> failwith (outer ^ " is already outside " ^ inner)
        else (List.hd l)::(mutate x rest)
    | (first::rest) -> first :: (mutate x rest)  
  in mutate_sched_list_guru func (mutate None) serialized guru

(* Set a function to be evaluated at the root (or reuse it) *)
let root_schedule (func: string) (guru: scheduling_guru) =
  let serialized = Printf.sprintf "root %s" func in
  (* Best so far, remainder of list *)
  let rec mutate x l = match (x, l) with
    (* Accept root over nothing, but keep looking *)
    | (None, Root::rest) -> mutate (Some Root) rest
    (* Take the first reuse, if there is one *)
    | (_, (Reuse buf)::rest) -> [Reuse buf]
    (* Skip past uninteresting things *)
    | (_, first::rest) -> mutate x rest
    (* If we found something acceptable, return it *)
    | (Some x, []) -> [x] 
    (* Otherwise freak out *)
    | _ -> failwith ("Could not schedule " ^ func ^ " as root")
  in mutate_legal_call_schedules_guru func (mutate None) serialized guru

let chunk_schedule (func: string) (var: string) (guru: scheduling_guru) = 
  let serialized = Printf.sprintf "chunk %s %s" func var in
  (* Best so far, remainder of list *)
  let rec mutate x l = match (x, l) with
    (* Accept chunk over nothing, but keep looking *)
    | (None, (Chunk v)::rest) when base_name v = var -> mutate (Some (Chunk v)) rest
    (* Take the first reuse, if there is one *)
    | (_, (Reuse buf)::rest) -> [Reuse buf]
    (* Skip past uninteresting things *)
    | (_, first::rest) -> mutate x rest
    (* If we found something acceptable, return it *)
    | (Some x, []) -> [x]
    (* Otherwise freak out *)
    | _ -> failwith ("Could not schedule " ^ func ^ " as chunked over " ^ var)
  in mutate_legal_call_schedules_guru func (mutate None) serialized guru

let random_schedule (func: string) (seed: int) (guru: scheduling_guru) = {
  serialized = (Printf.sprintf "random %s %d" func seed)::(guru.serialized);
  decide = fun f env legal_call_scheds ->
    let random_choice list = List.nth list (Random.int (List.length list)) in
    if (base_name f = func) then begin
      Random.init seed;
      if ((Random.int 3) = 0) then begin
        (* A mutation of the legal call schedules: pick a few at random *)
        let choice1 = random_choice legal_call_scheds in
        let choice2 = random_choice legal_call_scheds in
        let choice3 = random_choice legal_call_scheds in
        guru.decide f env [choice1; choice2; choice3]
      end else begin
        (* A mutation of the resulting sched_list *)

        (* Make some unique var names *)
        let n1 = Printf.sprintf "rand_%s_%d_a" func seed in
        let n2 = Printf.sprintf "rand_%s_%d_b" func seed in

        let (call_sched, sched_list) = guru.decide f env legal_call_scheds in
        let vectorize = 
          (* 0: Vectorize first serial var by a small factor *)
          let factor = random_choice [4; 8; 16] in
          let rec mutate = function
            | (Serial (v, min, size))::rest -> (* TODO: this may not be legal for reduction vars *)
                (Split (v, n2, n1, min))::
                  (Vectorized (n1, IntImm 0, factor))::
                  (Serial (n2, IntImm 0, (size +~ (IntImm (factor-1))) /~ (IntImm factor)))::
                  rest
            | (Vectorized (v, m, s))::rest -> (Vectorized (v, m, s))::rest (* Already vectorized *)
            | first::rest -> first::(mutate rest)
            | x -> x
          in mutate sched_list 
        and unroll = 
          (* 1: Unroll first serial var by a small factor *)
          let factor = random_choice [2; 3; 4] in
          let rec mutate = function
            | (Serial (v, min, size))::rest -> 
                (Split (v, n2, n1, min))::
                  (Unrolled (n1, IntImm 0, factor))::
                  (Serial (n2, IntImm 0, (size +~ (IntImm (factor-1))) /~ (IntImm factor)))::
                  rest
            | first::rest -> first::(mutate rest)
            | x -> x
          in mutate sched_list 
        and parallel = 
          (* 2: Parallelize last var *)
          match (List.rev sched_list) with 
            | [] -> []
            | (Serial (v, min, size))::rest -> 
                List.rev ((Parallel (v, min, size))::rest)
            | _ -> sched_list 
        and transpose = 
          (* 3: Transpose two vars *)
          match sched_list with
            | [] -> []                
            | [x] -> [x]
            | l ->
                (* Push the entry at position src to position dst *)
                let dst = (Random.int ((List.length l) - 1)) + 1 in
                let src = Random.int dst in 
                let rec bury idx item l = match (idx, item, l) with
                  | (_, item, []) -> [item]
                  | (0, item, l) -> item::l
                  | (x, item, (first::rest)) -> first::(bury (x-1) item rest)
                in                      
                let rec push src dst l = match (src, dst, l) with
                  | (0, _, (Split _)::rest) -> l (* Don't bury splits *)
                  | (_, _, []) -> l 
                  | (0, _, first::rest) -> (bury dst first rest)
                  | (_, _, first::rest) -> first::(push (src-1) (dst-1) rest)
                in
                push src dst sched_list                  
        and split = 
          (* 4: Split a var using a moderate power of two *)
          let factor = random_choice [16; 32; 64; 128] in
          match sched_list with 
            | [] -> []
            | l -> 
                let idx = Random.int (List.length l) in
                let rec make_split idx l = match (idx, l) with
                  | (_, []) -> []
                  | (0, (Serial (v, min, size))::rest) -> 
                      (Split (v, n2, n1, min))::
                        (Serial (n1, IntImm 0, IntImm factor))::
                        (Serial (n2, IntImm 0, (size +~ (IntImm (factor-1))) /~ (IntImm factor)))::
                        rest
                  | (0, (Parallel (v, min, size))::rest) -> 
                      (Split (v, n2, n1, min))::
                        (Parallel (n1, IntImm 0, IntImm factor))::
                        (Parallel (n2, IntImm 0, (size +~ (IntImm (factor-1))) /~ (IntImm factor)))::
                        rest                      
                  | (x, first::rest) -> first::(make_split (x-1) rest)
                in make_split idx sched_list
        in
        let choice = random_choice [vectorize; vectorize; unroll; parallel; transpose; split] in
        (call_sched, choice)          
      end        
    end else guru.decide f env legal_call_scheds
}

let parse_guru (str: string list) =
  let parse_next guru str = 
    let first_space = String.index str ' ' in
    let guru_type = String.sub str 0 first_space in
    match guru_type with
      | "novice"    -> novice
      | "root"      -> (Scanf.sscanf str "root %s" root_schedule) guru
      | "split"     -> Scanf.sscanf str "split %s %s %s %s %d" 
          (fun func var outer inner n ->
            split_schedule func var outer inner (IntImm n) guru)
      | "chunk"     -> (Scanf.sscanf str "chunk %s %s" chunk_schedule) guru
      | "transpose" -> (Scanf.sscanf str "transpose %s %s %s" transpose_schedule) guru
      | "vectorize" -> (Scanf.sscanf str "vectorize %s %s" vectorize_schedule) guru
      | "unroll"    -> (Scanf.sscanf str "unroll %s %s" unroll_schedule) guru
      | "parallel"  -> (Scanf.sscanf str "parallel %s %s" parallel_schedule) guru
      | "random"    -> (Scanf.sscanf str "random %s %d" random_schedule) guru
      | _ -> failwith ("Unrecognized guru type: " ^ str)
  in
  List.fold_left parse_next novice str 

let save_guru_to_file (guru: scheduling_guru) (filename: string) =
  let out_channel = open_out filename in
  List.iter (fun x -> output_string out_channel (x ^ "\n")) guru.serialized;
  close_out out_channel    

let load_guru_from_file (filename: string) = 
  let in_channel = open_in filename in
  let lines = ref [] in
  let lines = try begin
    while true; do
      lines := input_line in_channel :: !lines
    done; []
  end with End_of_file -> begin
    close_in in_channel;
    List.rev !lines
  end in
  parse_guru lines


    
