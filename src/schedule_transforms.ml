
open Ir
open Schedule
open Analysis
open Util

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
        | Call (_, name, args) -> name :: (List.concat (List.map find_calls_expr args))
        | x -> fold_children_in_expr find_calls_expr (@) [] x
      in

      let rec find_calls_stmt stmt =
        fold_children_in_stmt find_calls_expr find_calls_stmt (@) stmt 
      in

      match body with 
        | Pure expr -> find_calls_expr expr
        | Impure stmt -> find_calls_stmt stmt            
    in

    (* Recursively find more calls in the called functions *)
    let calls = l @ (List.concat (List.map called_functions l)) in

    (* Prefix them all with this function name. *)
    List.map (fun x -> f ^ "." ^ x) calls 
  in

  List.fold_left (fun s f -> set_schedule s f Inline []) schedule (called_functions func)
    
(* Add a split to a schedule *)
let split_schedule (func: string) (var: string) (newouter: string) 
    (newinner: string) (factor: int) (schedule: schedule_tree) =
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
  
(* Vectorize a parallel for *)
let vectorize_schedule (func: string) (var: string) (schedule: schedule_tree) =
  let (call_sched, sched_list) = find_schedule schedule func in
  (* Find var in the sched_list *)
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

(* Unroll a parallel or serial for *)
let unroll_schedule (func: string) (var: string) (schedule: schedule_tree) =
  let (call_sched, sched_list) = find_schedule schedule func in
  (* Find var in the sched_list *)
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
    
(* Push one var to be outside another *)
let transpose_schedule (func: string) (outer: string) (inner: string) (schedule: schedule_tree) = 
  let (call_sched, sched_list) = find_schedule schedule func in
  (* Find var in the sched_list *)
  Printf.printf "Moving %s outside %s\n" outer inner;
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
