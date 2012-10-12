exception ModulusOfNonInteger
exception ModulusOfMakeVector
exception ModulusOfBroadcast
exception ModulusOfRamp

val reduce_expr_modulo : Ir.expr -> int -> int option
val compute_remainder_modulus : Ir.expr -> int * int

val fold_children_in_expr :
  (Ir.expr -> 'a) -> ('a -> 'a -> 'a) -> 'a -> Ir.expr -> 'a
val fold_children_in_stmt :
  (Ir.expr -> 'a) -> (Ir.stmt -> 'a) -> ('a -> 'a -> 'a) -> Ir.stmt -> 'a
val expr_contains_expr : Ir.expr -> Ir.expr -> bool
val stmt_contains_expr : Ir.expr -> Ir.stmt -> bool
val mutate_children_in_expr : (Ir.expr -> Ir.expr) -> Ir.expr -> Ir.expr
val mutate_children_in_stmt :
  (Ir.expr -> Ir.expr) -> (Ir.stmt -> Ir.stmt) -> Ir.stmt -> Ir.stmt
(* val subs_stmt : Ir.stmt -> Ir.stmt -> Ir.stmt -> Ir.stmt *)
val subs_expr_in_stmt : Ir.expr -> Ir.expr -> Ir.stmt -> Ir.stmt
val subs_expr : Ir.expr -> Ir.expr -> Ir.expr -> Ir.expr
val subs_name_stmt : Ir.buffer -> Ir.buffer -> Ir.stmt -> Ir.stmt
val subs_name_expr : Ir.buffer -> Ir.buffer -> Ir.expr -> Ir.expr
val prefix_name_expr : string -> Ir.expr -> Ir.expr
val prefix_name_stmt : string -> Ir.stmt -> Ir.stmt
val find_names_in_stmt : Util.StringSet.t -> int -> Ir.stmt -> Util.StringIntSet.t
val find_names_in_expr : ?exclude_bufs:bool -> ?exclude_vars:bool -> Util.StringSet.t -> int -> Ir.expr -> Util.StringIntSet.t

val find_vars_in_expr : Ir.expr -> Ir.val_type Util.StringMap.t
val find_calls_in_expr : Ir.expr -> (Ir.call_type * Ir.val_type) Util.StringMap.t

val find_loads_in_expr : Ir.expr -> Util.StringSet.t
val find_loads_in_stmt : Ir.stmt -> Util.StringSet.t
val find_stores_in_stmt : Ir.stmt -> Util.StringSet.t

val duplicated_lanes : Ir.expr -> bool
val deduplicate_lanes : Ir.expr -> Ir.expr
