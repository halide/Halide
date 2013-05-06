val constant_fold_expr : Ir.expr -> Ir.expr
val constant_fold_stmt : Ir.stmt -> Ir.stmt
val remove_dead_lets_in_stmt : Ir.stmt -> Ir.stmt
