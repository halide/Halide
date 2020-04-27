((b.node_type() == IRNodeType::Select) && (
 rewrite((x <= select(y, x, z)), ((x <= z) || y))))
