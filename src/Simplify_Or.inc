((a.node_type() == IRNodeType::Or) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::LT) && (
 rewrite(((x || (y < z)) || (y < (w + u))), (x || (y < max(z, (w + u))))) ||
 rewrite(((x || (y < (z + w))) || (y < u)), (x || (y < max((z + w), u)))) ||
 rewrite((((x < y) || z) || (x < (w + u))), (z || (x < max(y, (w + u))))) ||
 rewrite((((x < (y + z)) || w) || (x < u)), ((x < max(u, (y + z))) || w))))))
