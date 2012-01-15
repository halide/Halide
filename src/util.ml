module StringSet = Set.Make (
  struct
    let compare = Pervasives.compare
    type t = string
  end
)

let string_set_concat (s: StringSet.t list) =
  List.fold_left StringSet.union StringSet.empty s

let string_set_map (f: string -> string) (s: StringSet.t) =
  StringSet.fold (fun x s -> StringSet.add (f x) s) s StringSet.empty

module StringMap = Map.Make(String)

module StringIntSet = Set.Make (
  struct
    let compare = Pervasives.compare
    type t = string * int
  end
)

let string_int_set_concat (s: StringIntSet.t list) =
  List.fold_left StringIntSet.union StringIntSet.empty s

(* An or operator for options *)
let option_either x y =
  match (x, y) with
    | (Some a, _) -> Some a
    | (_, b) -> b

(* A range operator - TODO: exclusive, while Batteries equivalent is inclusive *)
let (--) i j = 
  let rec aux n acc =
    if n < i then acc else aux (n-1) (n :: acc)
  in aux (j-1) []

let rec split_name n =
  try
    let i = (String.index n '.') in
    (String.sub n 0 i) :: (split_name (String.sub n (i+1) ((String.length n)-(i+1))))
  with Not_found -> [n]

let list_zip a b = List.map2 (fun x y -> (x, y)) a b

let list_zip3 a b c = List.map2 (fun (x, y) z -> (x, y, z)) (list_zip a b) c

let rec list_take_while pred = function
  | first::rest when not (pred first) -> []
  | first::rest -> first::(list_take_while pred rest)
  | [] -> []

let rec list_drop_while pred = function
  | first::rest when pred first -> list_drop_while pred rest
  | l -> l

(* Sort a list using a partial order *)
let rec partial_sort (lt: 'a -> 'a -> bool option) (l : 'a list) =
  let rec select_smallest before elem after =
    let lt_or_none b = match lt elem b with
      | None -> true
      | Some true -> true
      | _ -> false
    in
    if ((List.fold_left (&&) true (List.map lt_or_none before)) &&
           (List.fold_left (&&) true (List.map lt_or_none after))) then begin
      elem::(partial_sort lt (before @ after))
    end else begin
      match after with
        | [] -> failwith "Invalid partial ordering"
        | first::rest -> select_smallest (elem::before) first rest
    end
  in
  match l with
    | [] -> []
    | (first::rest) -> select_smallest [] first rest
 

(* TODO: set local debug options and flags per-module *)
let verbosity = 
  let str = try Sys.getenv "HL_DEBUG_CODEGEN" with Not_found -> "0" in
  let num = try int_of_string str with Failure _ -> 0 in
  num

(* 0 -> print nothing
   1 -> print top-level important stuff
   2 -> print way too much *)
let dbg level = 
  if (level < verbosity) then 
    Printf.printf 
  else 
    Printf.ifprintf stdout 
