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

(* A general-purpose exception *)
exception Wtf of string

let list_zip a b = List.map2 (fun x y -> (x, y)) a b

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
        | [] -> raise (Wtf "Invalid partial ordering")
        | first::rest -> select_smallest (elem::before) first rest
    end
  in
  match l with
    | [] -> []
    | (first::rest) -> select_smallest [] first rest
 
