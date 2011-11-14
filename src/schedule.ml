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

(* min/size exprs *)
(*
type region = (dimension * expr * expr) list

let union_region ra rb = 
  (* TODO: this needs to tolerate nil regions for ease of code below *)
  (* TODO: simplify min/max of exprs *)

let provides (_,args,_,_):definition sched =
  let args = List.map fst args in
  let dim_region d =
    let (min, n) = stride_for_dim d in
    (d, min, n)
  in
  List.fold_left (fun r d -> r @ [dim_region d]) [] args

(* Point requirements, in terms of dimensions of def *)
let requires (_,_,_,body):definition (callee_name,callee_args,_,_):definition =
  let rec expr_requires = function
    | Cast (_, e)
    | Not e
    | Load (_, _, e)
    | Broadcast (e, _) -> expr_requires e

    | Bop (_, a, b)
    | Cmp (_, a, b)
    | And (a, b)
    | Or  (a, b)
    | Ramp (a, b, _)
    | ExtractElement (a, b)
    | Let (_, a, b) -> union_region (expr_requires a) (expr_requires b)

    | Select (a, b, c) ->
        union_region
          (expr_requires a)
          (union_region (expr_requires b) (expr_requires c))

    | MakeVector es ->
        List.fold_left (fun r e -> union_region r (expr_requires e)) [] es

    | Call (_, name, call_args) when name = callee_name ->
        List.fold_left2
          (* concat name, argval, size=1 onto list *)
          (fun r arg (_,nm) -> r @ [(nm, arg, IntImm(1))])
          [] call_args callee_args

    (* Immediates, vars, and other calls fall through *)
    | _ -> []
        *)

module StringMap = Map.Make(String)

(* Schedule for this function, schedule for sub-functions *)
type schedule_tree = 
  | Tree of ((call_schedule * (schedule list) * schedule_tree) StringMap.t) 

(* What is the extent of a schedule over a given dimension *)
let rec stride_for_dim dim = function
  | [] -> raise (Wtf ("failed to find schedule for dimension " ^ dim))
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
    | [] -> raise (Wtf "find_schedule of empty list")
    | (first::rest) -> 
        let (Tree map) = tree in
        let (cs, sl, subtree) = StringMap.find first map in
        if rest = [] then (cs, sl) else find subtree rest
  in
  let name_parts = split_name name in
  try 
    find tree name_parts 
  with
    | Not_found -> raise (Wtf (name ^ " not found in schedule tree"))
    


let rec set_schedule
      (tree: schedule_tree) (call: string) (call_sched: call_schedule) (sched_list: schedule list) =
  let rec set tree = function
    | [] -> raise (Wtf "set_schedule of empty list")
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
  let ends_with substr str =
    (* Printf.printf "ends_with %s %s\n%!" substr str; *)
    let strlen = String.length str in
    let substrlen = String.length substr in
    if substrlen > strlen then false else
      let suffix = String.sub str (strlen - substrlen) substrlen in
      substr = suffix      
  in
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

(* Take 
   
   g(x) = x*2
   f(x) = g(x-1) + g(x+1)

   schedule for f(fx):  
   [Split ("fx", 16, "fxo", "fxi");
   Serial ("fxi"); --> 0,16
   Serial ("fxo")] --> 0,10 - implied by realize f [0,100] and split by 16

   call_schedule for f.g is Chunk ("fxi", true)
   schedule for f.g(gx) 
   [Serial ("gx")]

   Realize f

   -> Initial statement
   result[fx] = f(fx)

   -> Initialize wrapper code using schedule for f

   Map "fxo" from 0 to 10
     Map "fxi" from 0 to 16       
       result[fxo*16 + fxi] = f(fxo*16 + fxi)

   -> Subs f function body
   
   Map "fxo" from 0 to 10
     Map "fxi" from 0 to 16   
       Let f_result[1]
         f_result[0] = g(fxo*16 + fxi - 1) + g(fxo*16 + fxi + 1) 
         result[fxo*16 + fxi] = f_result[0]

   -> Look for calls (two calls to g). Precompute accoring to call schedule

   Map "fxo" from 0 to 10
     Let G[fxo*16+17 - fxo*16-1]
       Map "gx" from fxo*16-1 to fxo*16+17
         G[gx-fxo*16-1] = g(gx)
       Map "fxi" from 0 to 16
         Let f_result[1] 
           f_result[0] = G[fxo*16 + fxi - 1 - (fxo*16-1)] + G[fxo*16 + fxi + 1 - (fxo*16+1)]
           result[fxo*16 + fxi] = f_result[0]

   -> Subs g function body
   
   Map "fxo" from 0 to 10
     Let G[fxo*16+17 - fxo*16-1]
       Map "gx" from fxo*16-1 to fxo*16+17
         Let g_result[1] 
           g_result[0] = gx*2  
           G[gx-fxo*16-1] = g_result[0]
       Map "fxi" from 0 to 16
         Let f_result[1] 
           f_result[0] = G[fxo*16 + fxi - 1 - (fxo*16-1)] + G[fxo*16 + fxi + 1 - (fxo*16+1)]
           result[fxo*16 + fxi] = f_result[0]

   -> Look for calls (none). Done.

   Another example (replace every pixel with how often it occurs)

   hist(im[x, y])++ (* Assume implemented as imperative blob for now *)
   f(x, y) = hist(im[x, y])

   schedule for f(fx, fy):
   [Serial ("fy", 0, 100); Serial ("fx", 0, 100)]

   call schedule for hist: (Chunk ("fy", true))

   schedule for f.hist: [Serial ("i", 0, 256)]

   -> Initialize wrapper code using schedule for f

   Map "fy" from 0 to 100
     Map "fx" from 0 to 100
       result[fy*100+fx] = f(x, y)

   -> Subs f function body
   Map "fy" from 0 to 100
     Map "fx" from 0 to 100
       Let f_result[1]
         f_result[0] = hist(im[fx, fy])
         result[fy*100+fx] = f_result[0]

   -> Look for calls (hist), and lift according to call schedule

   Let HIST[256]
     Map "i" from 0 to 256
       HIST[i] = hist(i)
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         Let f_result[1]
           f_result[0] = HIST[im[fx, fy]]
           result[fy*100+fx] = f_result[0]
   
   -> Subs hist function body
   
   Let HIST[256]
     Map "i" from 0 to 256
       Let hist_result[1]
         Let inner_result[256]
           Map "hx" from 0 to 100
             Map "hy" from 0 to 100
               inner_result[im[hx, hy]]++
           hist_result[0] = inner_result[i]
         HIST[i] = hist_result[0]
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         Let f_result[1]
           f_result[0] = HIST[im[fx, fy]]
           result[fy*100+fx] = f_result[0]
   
   -> Lift invariant pipeline roots outside maps
   
   Let HIST[256]
     Let inner_result[256]
       Map "hx" from 0 to 100
         Map "hy" from 0 to 100
           inner_result[im[hx, hy]]++
       Map "i" from 0 to 256
         Let hist_result[1]
           hist_result[0] = inner_result[i]
         HIST[i] = hist_result[0]
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         Let f_result[1]
           f_result[0] = HIST[im[fx, fy]]
           result[fy*100+fx] = f_result[0]

   -> Remove trivial scalar lets (assigned once, used once)

   Let HIST[256]
     Let inner_result[256]
       Map "hx" from 0 to 100
         Map "hy" from 0 to 100
           inner_result[im[hx, hy]]++
       Map "i" from 0 to 256
         HIST[i] = inner_result[i]
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         result[fy*100+fx] = HIST[im[fx, fy]]

   -> Remove identity pipeline stages
   
   Let inner_result[256]
     Map "hx" from 0 to 100
       Map "hy" from 0 to 100
         inner_result[im[hx, hy]]++
     Map "fy" from 0 to 100
       Map "fx" from 0 to 100
         result[fy*100+fx] = inner_result[im[fx, fy]]   

*)
   

