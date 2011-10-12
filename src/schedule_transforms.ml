
open Ir
open Schedule
open Analysis

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

  let sched = List.fold_left (fun s f -> set_schedule s f Inline []) schedule (called_functions func) in
  
  Printf.printf "Made a default schedule:\n";
  print_schedule sched;
  Printf.printf "-----\n";

  sched
