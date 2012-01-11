open Ir
open Ir_printer
open Util

(*
                                
f(x, y) = g(A(x, y), B(x, y)) + g(C(x, y), D(x, y))

The fully specified way to schedule f/g is to put it inside some maps and lets - i.e. wrap it up in
imperative code.
  
Any externally imposed schedule is a recipe to help us do this.

How can we specify such a schedule more tersely from the outside while losing a minimum of
generality?

*)

(* Every (transitive) function has a schedule. Every callsite has a call_schedule. *)

(* How can we schedule a single function - i.e. what kind of Maps do we insert. The following
 * options create a perfectly nested set of maps. You get a list of them, where each dimension must
 * be handled. *) 
type dimension = string

type schedule = 
  (* dimension, names of dimensions introduced, min of old dimension *)
  | Split of dimension * dimension * dimension * expr
  (* Serialize across a dimension between the specified bounds *)
  | Serial     of dimension * expr * expr
  | Parallel   of dimension * expr * expr
  | Unrolled   of dimension * expr * int
  | Vectorized of dimension * expr * int

(* How should a sub-function be called - i.e. what lets do we introduce? A sufficient
 * representation is (with reference to the schedule of the caller), to what caller dimension
 * should I hoist this out to, and should I fuse with other calls to the same callee *)
type call_schedule =
  | Chunk of dimension (* of caller *)
  (* level in callee where dynamic/static transition happens - chunk granularity *)
  | Dynamic of dimension
  (* dimension of caller always pairs with outermost dimension of callee *)
  | Coiterate of dimension * int (* offset *) * int (* modulus *)
  | Inline (* block over nothing - just do in place *)
  | Root (* There is no calling context *)
  | Reuse of string (* Just do what some other function does, using the same data structure *)

(* Schedule for this function, schedule for sub-functions *)
type schedule_tree = 
  | Tree of ((call_schedule * (schedule list) * schedule_tree) StringMap.t) 

(* What is the extent of a schedule over a given dimension *)
let rec stride_for_dim dim = function
  | [] -> failwith ("failed to find schedule for dimension " ^ dim)
  | hd::rest ->
      begin match hd with
        | Serial (d, min, n)
        | Parallel (d, min, n) when d = dim -> (min,n)
        | Unrolled (d, min, n)
        | Vectorized (d, min, n) when d = dim -> (min, IntImm n)
        | Split (d, outer, inner, offset) when d = dim ->
            (* search for new dimensions on rest of the sched list -
             they are only allowed after defined by the split *)
            let (min_outer, size_outer) = stride_for_dim outer rest in
            let (_,         size_inner) = stride_for_dim inner rest in
            ((min_outer *~ size_inner) +~ offset, size_outer *~ size_inner)
        (* recurse if not found *)
        | _ -> stride_for_dim dim rest
      end

(* Return a list of expressions for computing the stride of each dimension of a function given the
 * schedule*)
let stride_list (sched:schedule list) (args:string list) =
  List.map (fun arg -> stride_for_dim arg sched) args

let find_schedule (tree:schedule_tree) (name:string) =
  let rec find (tree:schedule_tree) = function
    | [] -> failwith "find_schedule of empty list"
    | (first::rest) -> 
        let (Tree map) = tree in
        let (cs, sl, subtree) = StringMap.find first map in
        if rest = [] then (cs, sl) else find subtree rest
  in
  let name_parts = split_name name in
  try 
    find tree name_parts 
  with
    | Not_found -> failwith (name ^ " not found in schedule tree")
    


let rec set_schedule
      (tree: schedule_tree) (call: string) (call_sched: call_schedule) (sched_list: schedule list) =
  let rec set tree = function
    | [] -> failwith "set_schedule of empty list"
    | (first::rest) ->
        let (Tree map) = tree in
        let (old_cs, old_sl, old_tree) = 
          if StringMap.mem first map then
            StringMap.find first map 
          else
            (Root, [], empty_schedule) 
        in
        if (rest = []) then
          begin match call_sched with
            (* A reuse node has no children *)
            | Reuse _ -> Tree (StringMap.add first (call_sched, sched_list, Tree StringMap.empty) map)
            | _ -> Tree (StringMap.add first (call_sched, sched_list, old_tree) map)
          end
        else        
          Tree (StringMap.add first (old_cs, old_sl, set old_tree rest) map)
  in
  set tree (split_name call)

and empty_schedule = Tree StringMap.empty

let find_all_schedule (tree:schedule_tree) (name:string) =
  let rec find prefix = function
    | Tree tree ->
        let process_key key (_, _, subtree) list = 
          (* Printf.printf "Prefix: %s Key: %s\n%!" prefix key; *)
          let prefixed_key = prefix ^ key in
          let subresults = (find (prefixed_key ^ ".") subtree) @ list in
          if name = key then
            prefixed_key :: subresults
          else
            subresults
        in
        StringMap.fold process_key tree []
  in
  find "" (tree)

let list_of_schedule (tree:schedule_tree) =
  let rec find prefix = function
    | Tree tree ->
        let process_key key (_, _, subtree) list = 
          let prefixed_key = prefix ^ key in
          let subresults = (find (prefixed_key ^ ".") subtree) @ list in
          prefixed_key :: subresults
        in
        StringMap.fold process_key tree []
  in
  find "" (tree)  

let string_of_call_schedule = function
  | Chunk d -> "Chunk " ^ d 
  | Dynamic d -> "Dynamic " ^ d
  | Coiterate (d, offset, modulus) -> 
      "Coiterate " ^ d ^ " " ^ (string_of_int offset) ^ " " ^ (string_of_int modulus)
  | Inline -> "Inline"
  | Root -> "Root"      
  | Reuse s -> "Reuse " ^ s

let string_of_schedule = function
  | Split (d, d_o, d_i, offset) ->
      "Split " ^ d ^ " " ^ d_o ^ " " ^ d_i ^ " " ^ (string_of_expr offset)
  | Serial (d, min, n) ->
      "Serial "     ^ d ^ " " ^ (string_of_expr min) ^ " " ^ (string_of_expr n)
  | Parallel (d, min, n) ->
      "Parallel "   ^ d ^ " " ^ (string_of_expr min) ^ " " ^ (string_of_expr n)
  | Unrolled (d, min, n) ->
      "Unrolled "   ^ d ^ " " ^ (string_of_expr min) ^ " " ^ (string_of_int n)
  | Vectorized (d, min, n) ->
      "Vectorized " ^ d ^ " " ^ (string_of_expr min) ^ " " ^ (string_of_int n)

let print_schedule (tree : schedule_tree) = 
  let rec inner tree prefix = 
    let Tree map = tree in
    StringMap.iter (fun k (cs, sl, st) -> 
      let call_sched_string = string_of_call_schedule cs in
      let sched_list_string = "[" ^ (String.concat ", " (List.map string_of_schedule sl)) ^ "]" in
      let newprefix = prefix ^ "." ^ k in
      Printf.printf "%s -> %s %s\n" newprefix call_sched_string sched_list_string;
      inner st newprefix) map
  in inner tree ""

