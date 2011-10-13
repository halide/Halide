val string_of_val_type : Ir.val_type -> string
val string_of_op : Ir.binop -> string
val string_of_cmp : Ir.cmpop -> string
val string_of_expr : Ir.expr -> string
val string_of_stmt : Ir.stmt -> string
val string_of_buffer : Ir.buffer -> Ir.buffer
val string_of_toplevel : Ir.arg list * Ir.stmt -> string
val string_of_arg : Ir.arg -> string
