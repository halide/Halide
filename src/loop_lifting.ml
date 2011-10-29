open Ir
open Analysis

(* Convert this structure:
   For (i, min, size,
      Pipeline (buf, ty, size, produce, consume))

   to this:

   Pipeline(buf, ty, size, produce, 
      For (i, min, size, consume))

   in cases where 'size' and 'produce' don't depend on 'i' 

   TODO: what if the pipeline is hidden inside a block?
*)

let rec loop_lifting (stmt: stmt) =  
  let newstmt = mutate_children_in_stmt (fun x -> x) loop_lifting stmt in
  match newstmt with
    | For (i, min, range, order, Pipeline (buf, ty, size, produce, consume)) 
        when (not (expr_contains_expr (Var (i32, i)) size)) &&
          (not (stmt_contains_expr (Var (i32, i)) produce)) ->
        Pipeline (buf, ty, size, produce, 
                  loop_lifting (For (i, min, range, order, consume)))
    | _ -> newstmt
