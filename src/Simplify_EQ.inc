((a.node_type() == IRNodeType::Max) && (
((b.node_type() == IRNodeType::Min) && (
 rewrite((max(x, y) == min(x, y)), (x == y)) ||
 rewrite((max(x, y) == min(y, x)), (y == x)) ||
 rewrite((max(x, (y*z)) == min(x, (z*y))), ((y*z) == x)) ||
 rewrite((max(x, (y*z)) == min((z*y), x)), ((y*z) == x)) ||
 rewrite((max((x*y), z) == min(z, (y*x))), ((x*y) == z)) ||
 rewrite((max((x*y), z) == min((y*x), z)), ((x*y) == z))))))
