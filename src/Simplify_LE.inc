((b.node_type() == IRNodeType::Select) && (
 rewrite((x <= select(y, x, z)), ((x <= z) || y)))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, y, z) <= y), (x || (z <= y)))))
