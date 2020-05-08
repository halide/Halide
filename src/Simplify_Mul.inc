((b.node_type() == IRNodeType::Select) && (
 rewrite((x*select(y, c0, c1)), select(y, x, c1), ((c1 == 0) && (c0 == 1))))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + (y*c0))*c1), ((y*fold((c0*c1))) - x), (c1 == -1)) ||
 rewrite((((x*c0) + y)*c1), ((x*fold((c0*c1))) - y), (c1 == -1)))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite((((x*c0) - y)*c1), ((y + x)*fold((c1*-1))), ((c0 == -1) || (c1 == 0))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, c0, c1)*y), select(x, y, c1), ((c1 == 0) && (c0 == 1)))))
