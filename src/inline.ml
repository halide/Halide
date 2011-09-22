open Ir
open List
open Map

module DefMap = Map.Make(String)
type defmap = definition DefMap.t

(* inline everything - replace all calls with inlined versions of the function in question *)

let rec inline_stmt (stmt : stmt) (env : defmap) =
  match stmt with
    | Map (var, min, max, body) ->
      (* Extract calls in min and max, compute them, and put myself inside a let *)
      
      Map var ? ? (inline_stmt body env)
    | Block list -> List.map (fun x -> inline_stmt x env) list
    | Store (v, buf, idx) ->
      let calls = (findcalls v) @ (findcalls idx) in
      let rec xform = function
        | (name, arg) :: rest ->
          let tmpname = mkname () in
          let (_, argnames, f_body) = DefMap.find env name in
          (* subs body argnames for args *)
          (* subs tmpname for result *)
          Let(mkname (), ?ty?, 1, , xform rest)
        | [] -> Store (v, buf, idx)
      (* Extract calls in v and idx, compute them, and put the statement inside a let *)
    | Let (buf, ty, size, produce, consume) -> Let (buf, size, inline_stmt produce env, inline_stmt consume env)
      

and inline_definition =


(* Find all the call-sites in an expression, replace them with loads,
   and return a function that will wrap the statement in question in an
   appropriate amount of support stuff (allocs, etc) *)

(* inline_expr : expr -> defmap -> (expr, stmt -> stmt) *)
and inline_expr (expr : expr) (env : defmap) =
  let rec findcalls = function
    | Call (name, args) -> 
      (name, args) :: (List.concat (List.map findcalls args))
    | Cast (_, a) | Not a | Load (_, _, a) | Broadcast (a, _) ->
      findcalls a
    | Bop (_, a, b) | Cmp(_, a, b) | And (a, b) | Or (a, b) | ExtractElement (a, b) ->
      (findcalls a) @ (findcalls b)
    | Select (c, a, b) -> 
      (findcalls c) @ (findcalls a) @ (findcalls b)
    | MakeVector l ->
      List.concat (List.map findcalls l)        
    | x -> []
      
  and calls = findcalls expr in
  
  
