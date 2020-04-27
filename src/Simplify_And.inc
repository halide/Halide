((a.node_type() == IRNodeType::And) && (
((b.node_type() == IRNodeType::LE) && (
 rewrite(((x && (y <= z)) && (y <= w)), (x && (y <= min(z, w)))) ||
 rewrite((((x <= y) && z) && (x <= w)), ((x <= min(y, w)) && z))))))
