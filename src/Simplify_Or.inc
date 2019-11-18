((b.node_type() == IRNodeType::EQ) && (
 rewrite((y || (x == select(y, z, w))), (y || (w == x))) ||
 rewrite((y || (select(y, z, w) == x)), (y || (w == x))))) ||
((b.node_type() == IRNodeType::LT) && (
 rewrite((x || (select(x, y, z) < z)), x) ||
 rewrite((y || (x < select(y, z, x))), y))) ||
((b.node_type() == IRNodeType::Or) && (
 rewrite((x || (y || !x)), true) ||
 rewrite((x || (!x || y)), true))) ||
((b.node_type() == IRNodeType::Not) && (
 rewrite((x || !(y || !x)), x) ||
 rewrite((x || !(!x || y)), x))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((x || select(x, x, y)), (x || y)) ||
 rewrite((x || select(x, y, x)), x))) ||
((a.node_type() == IRNodeType::EQ) && (
 rewrite(((x == select(y, z, w)) || y), ((w == x) || y)) ||
 rewrite(((select(y, z, w) == x) || y), ((w == x) || y)) ||
((b.node_type() == IRNodeType::EQ) && (
 rewrite(((y == x) || (x == y)), (x == y)) ||
 rewrite(((y == x) || (y == x)), (x == y)) ||
 rewrite((((x*x) == c0) || (x == 0)), ((x*x) <= 0), (c0 <= 0)))))) ||
((a.node_type() == IRNodeType::NE) && (
((b.node_type() == IRNodeType::NE) && (
 rewrite(((y != x) || (x != y)), (y != x)) ||
 rewrite(((y != x) || (y != x)), (y != x)))))) ||
((a.node_type() == IRNodeType::LT) && (
 rewrite(((x < select(y, z, x)) || y), y) ||
 rewrite(((select(x, y, z) < z) || x), x) ||
((b.node_type() == IRNodeType::EQ) && (
 rewrite(((x < y) || (x == y)), (x <= y)) ||
 rewrite(((x < y) || (y == x)), (x <= y)))) ||
((b.node_type() == IRNodeType::NE) && (
 rewrite(((x < y) || (x != y)), (y != x)) ||
 rewrite(((x < y) || (y != x)), (y != x)))))) ||
((a.node_type() == IRNodeType::Or) && (
 rewrite(((y || !x) || x), true) ||
 rewrite(((!x || y) || x), true))) ||
((a.node_type() == IRNodeType::Not) && (
 rewrite((!(y || !x) || x), x) ||
 rewrite((!(!x || y) || x), x))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite((select(x, x, y) || x), (y || x)) ||
 rewrite((select(x, y, x) || x), x)))