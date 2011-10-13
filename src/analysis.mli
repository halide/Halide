exception ModulusOfNonInteger
exception ModulusOfMakeVector
exception ModulusOfBroadcast
exception ModulusOfRamp

val reduce_expr_modulo : Ir.expr -> int -> int option

val fold_children_in_expr :
  (Ir.expr -> 'a) -> ('a -> 'a -> 'a) -> 'a -> Ir.expr -> 'a
val fold_children_in_stmt :
  (Ir.expr -> 'a) -> (Ir.stmt -> 'a) -> ('a -> 'a -> 'a) -> Ir.stmt -> 'a
val expr_contains_expr : Ir.expr -> Ir.expr -> bool
val mutate_children_in_expr : (Ir.expr -> Ir.expr) -> Ir.expr -> Ir.expr
val mutate_children_in_stmt :
  (Ir.expr -> Ir.expr) -> (Ir.stmt -> Ir.stmt) -> Ir.stmt -> Ir.stmt
val subs_stmt : Ir.expr -> Ir.expr -> Ir.stmt -> Ir.stmt
val subs_expr : Ir.expr -> Ir.expr -> Ir.expr -> Ir.expr
val subs_name_stmt : Ir.buffer -> Ir.buffer -> Ir.stmt -> Ir.stmt
val subs_name_expr : Ir.buffer -> Ir.buffer -> Ir.expr -> Ir.expr
val prefix_name_expr : string -> Ir.expr -> Ir.expr
val prefix_name_stmt : string -> Ir.stmt -> Ir.stmt

val hash_int : int -> int * int * int * int
val hash_float : float -> int * int * int * int
val hash_str : string -> int * int * int * int
val hash_combine2 :
  'a * 'b * 'c * 'd -> 'e * 'f * 'g * 'h -> int * int * int * int
val hash_combine3 :
  'a * 'b * 'c * 'd ->
  'e * 'f * 'g * 'h -> 'i * 'j * 'k * 'l -> int * int * int * int
val hash_combine4 :
  'a * 'b * 'c * 'd ->
  'e * 'f * 'g * 'h ->
  'i * 'j * 'k * 'l -> 'm * 'n * 'o * 'p -> int * int * int * int
val hash_expand : 'a -> int * int * int * int
val hash_type : Ir.val_type -> int * int * int * int
val hash_expr : Ir.expr -> int * int * int * int
