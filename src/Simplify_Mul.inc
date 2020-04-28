((a.node_type() == IRNodeType::Add) && (
 rewrite(((x + select(y, c0, c1))*c2), (select(y, c1, c0) - x), (((c1*c2) == c0) && (((c1 + -1)*c2) == (c0 + 1)))) ||
 rewrite(((select(x, c0, c1) + y)*c2), (select(x, c1, c0) - y), (((c1*c2) == c0) && (((c1 + -1)*c2) == (c0 + 1))))))
