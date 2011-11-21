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

(* An or operator for options *)
let option_either x y =
  match (x, y) with
    | (Some a, _) -> Some a
    | (_, b) -> b

(* A range operator *)
let (--) i j = 
  let rec aux n acc =
    if n < i then acc else aux (n-1) (n :: acc)
  in aux (j-1) []

let rec split_name n =
  try
    let i = (String.index n '.') in
    (String.sub n 0 i) :: (split_name (String.sub n (i+1) ((String.length n)-(i+1))))
  with Not_found -> [n]

exception Wtf of string
