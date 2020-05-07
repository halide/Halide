((b.node_type() == IRNodeType::Select) && (
 rewrite((x <= select(y, x, z)), (y || (x <= z))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, y, z) <= y), ((z <= y) || x))))
