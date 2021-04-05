((a.node_type() == IRNodeType::And) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::IntImm) && (
 rewrite(!(x && ((y + z) <= (y*c0))), (!x || (y < z)), ((2 <= c0) && (c0 < 3))) ||
 rewrite(!(x && ((y + z) <= (z*c0))), (!x || (z < y)), ((2 <= c0) && (c0 < 3))) ||
 rewrite(!(((x + y) <= (x*c0)) && z), (!z || (x < y)), ((2 <= c0) && (c0 < 3))) ||
 rewrite(!(((x + y) <= (y*c0)) && z), (!z || (y < x)), ((2 <= c0) && (c0 < 3)))))))
