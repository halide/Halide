(* An or operator for options *)
let option_either x y =
  match (x, y) with
    | (Some a, _) -> Some a
    | (_, b) -> b

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
(* module StringMap = BatMap.StringMap *) (* TODO: batteries *)

let string_map_merge ma mb = StringMap.merge (fun name a b -> option_either a b) ma mb

module StringIntSet = Set.Make (
  struct
    let compare = Pervasives.compare
    type t = string * int
  end
)

let string_int_set_concat (s: StringIntSet.t list) =
  List.fold_left StringIntSet.union StringIntSet.empty s

let string_starts_with a b =
  let len_a = String.length a in
  let len_b = String.length b in
  len_a > len_b && ((String.sub a 0 len_b) = b)

let string_ends_with a b =
  let len_a = String.length a in
  let len_b = String.length b in
  len_a > len_b && ((String.sub a (len_a - len_b) len_b) = b)

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

(* Sort according to a partial ordering. This sort is stable (in that
   it doesn't rearrange non-participating elements), and users of it
   assume this, so don't swap this for a non-stable sort *)
let rec partial_sort (lt: 'a -> 'a -> bool option) = function
  | [] -> []
  | first::rest ->
    (* Partition rest into (elems not lt first) @ (elem lt first :: unsorted elems) *)
    let rec partition = function
      | [] -> ([], [])
      | (x::xs) -> 
        match lt first x with
          | Some false -> ([], x::xs)
          | _ ->
            let (a, b) = partition xs in
            (x::a, b)
    in
    match partition rest with
      | (a, []) -> 
        (* first is in the right place *)
        first :: (partial_sort lt rest)
      | (a, (b::bs)) ->
        (* first should be swapped with b and the sort restarted from here *)
        partial_sort lt ((b::a) @ (first::bs))                       

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

let disable_bounds_checking = 
  let str = try Sys.getenv "HL_DISABLE_BOUNDS_CHECKING" with Not_found -> "0" in
  let num = try int_of_string str with Failure _ -> 0 in
  num > 0

let with_file_out filename func =
  let out = open_out filename in
  func out;
  close_out out
