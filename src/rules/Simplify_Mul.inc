((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite((x + c0)*(x + c1), max((x + c0)*(x + c1), (c0 - c1)*(c1 - c0)), 0 <= min(c0, c1)))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite((min(x, c0) - select(c1 < x, min(x, c2), x))*c3, min((min(x, c0) - select(c1 < x, min(x, c2), x))*c3, select(c0 < c2, c1, c0)*c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite((min(x, c0) - select(c1 < x, min(x, c2), x))*c3, max((min(x, c0) - select(c1 < x, min(x, c2), x))*c3, (min(c0, c2) - max(c1, c2))*c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite((min(min(u, c0) + v, c1) - (v + u))*c2, min((min(min(u, c0) + v, c1) - (v + u))*c2, c1), 0 <= min(min(c0, c1), c2))))
