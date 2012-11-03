val vector_subs_expr : Ir.expr Util.StringMap.t -> Ir.expr -> Ir.expr
val vectorize_expr : string -> Ir.expr -> int -> Ir.expr -> Ir.expr
val vectorize_stmt : string -> Ir.expr -> int -> Ir.stmt -> Ir.stmt
