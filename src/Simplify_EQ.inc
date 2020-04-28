((a.node_type() == IRNodeType::Mul) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite(((x*c0) == 0), (x == 0))))))
