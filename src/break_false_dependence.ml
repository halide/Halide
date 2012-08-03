open Ir
open Analysis
open Hash
open Ir_printer

module ExprSet = Set.Make (
  struct
    let compare = Pervasives.compare
    type t = expr
  end
)

let break_false_dependence_expr expr =

  (* Find the set of all vars, calls, and loads in the expression *)
  let rec find_unknowns expr = match expr with
    | Var _ -> ExprSet.add expr ExprSet.empty
    | Load (t, b, idx) -> ExprSet.add expr (find_unknowns idx)
    | Call (ct, rt, f, args) -> 
        let unknowns_in_args = List.map find_unknowns args in
        let unknowns_in_args = List.fold_left ExprSet.union ExprSet.empty unknowns_in_args in
        ExprSet.add expr unknowns_in_args
    | x -> fold_children_in_expr find_unknowns ExprSet.union ExprSet.empty x            
  in
  (* For each one, subs it with zero of the appropriate type and check
     if the hash of the expression changed or not *)
  let break_dependence unknown expr =
    let zero = make_zero (val_type_of_expr unknown) in
    let newexpr = (subs_expr unknown zero expr) in
    if (hash_expr expr) = (hash_expr newexpr) then begin
      newexpr 
    end else begin
      expr
    end
  in
  
  ExprSet.fold break_dependence (find_unknowns expr) expr
        
let rec break_false_dependence_stmt stmt =
  mutate_children_in_stmt break_false_dependence_expr break_false_dependence_stmt stmt
