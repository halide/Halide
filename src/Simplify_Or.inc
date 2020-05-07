((b.node_type() == IRNodeType::Not) && (
 rewrite((x || !(x && y)), true) ||
 rewrite((x || !(y && x)), true))) ||
((a.node_type() == IRNodeType::And) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::NE) && (
 rewrite(((x && (y == z)) || (y != z)), (x || (z != y))) ||
 rewrite(((x && (y == z)) || (z != y)), (x || (z != y))) ||
 rewrite((((x == y) && z) || (x != y)), (z || (y != x))) ||
 rewrite((((x == y) && z) || (y != x)), ((x != y) || z)))))) ||
((a.node_type() == IRNodeType::Or) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::LT) && (
 rewrite(((x || (y < z)) || (w < z)), (x || (min(y, w) < z))) ||
 rewrite(((x || (y < z)) || (y < (w + u))), (x || (y < max(z, (w + u))))) ||
 rewrite(((x || (y < (z + w))) || (y < u)), (x || (y < max((z + w), u)))) ||
 rewrite(((x || (y || (z < w))) || (u < w)), (x || ((min(z, u) < w) || y))) ||
 rewrite(((x || (y || (z || (w < u)))) || (v < u)), (x || (y || (z || (min(w, v) < u))))) ||
 rewrite(((x || (y || ((z < w) || u))) || (v < w)), ((x || (y || (min(z, v) < w))) || u)) ||
 rewrite(((x || ((y < z) || w)) || (u < z)), ((x || (min(y, u) < z)) || w)) ||
 rewrite(((x || ((y || (z < w)) || u)) || (v < w)), ((x || (min(z, v) < w)) || (u || y))) ||
 rewrite(((x || (((y < z) || w) || u)) || (v < z)), ((x || ((min(y, v) < z) || u)) || w)) ||
 rewrite((((x < y) || z) || (w < y)), (z || (min(x, w) < y))) ||
 rewrite((((x < y) || z) || (x < (w + u))), (z || (x < max(y, (w + u))))) ||
 rewrite((((x < (y + z)) || w) || (x < u)), (w || (x < max((z + y), u)))) ||
 rewrite((((x || (y < z)) || w) || (u < z)), (((min(y, u) < z) || w) || x)) ||
 rewrite((((x || (y || (z < w))) || u) || (v < w)), (y || (x || ((min(z, v) < w) || u)))) ||
 rewrite((((x || ((y < z) || w)) || u) || (v < z)), (((x || (min(y, v) < z)) || u) || w)) ||
 rewrite(((((x < y) || z) || w) || (u < y)), (((min(x, u) < y) || w) || z)) ||
 rewrite(((((x || (y < z)) || w) || u) || (v < z)), (((min(y, v) < z) || u) || (w || x))) ||
 rewrite((((((x < y) || z) || w) || u) || (v < y)), ((((min(x, v) < y) || w) || u) || z)))) ||
((b.node_type() == IRNodeType::LE) && (
 rewrite(((x || (y || (z <= w))) || (u <= w)), (x || ((min(z, u) <= w) || y))) ||
 rewrite(((x || ((y <= z) || w)) || (u <= z)), ((x || (min(y, u) <= z)) || w)) ||
 rewrite((((x || (y <= z)) || w) || (u <= z)), (((min(y, u) <= z) || w) || x)) ||
 rewrite(((((x <= y) || z) || w) || (u <= y)), (((min(x, u) <= y) || w) || z)))))) ||
((a.node_type() == IRNodeType::Not) && EVAL_IN_LAMBDA(
 rewrite((!(x && y) || x), true) ||
 rewrite((!(x && y) || y), true) ||
((b.node_type() == IRNodeType::EQ) && (
 rewrite((!(x && (y == z)) || (z == y)), true) ||
 rewrite((!((x == y) && z) || (y == x)), true))) ||
((b.node_type() == IRNodeType::NE) && (
 rewrite((!(x && (y == z)) || (y != z)), !(x && (z == y))) ||
 rewrite((!(x && (y == z)) || (z != y)), !(x && (z == y))) ||
 rewrite((!((x == y) && z) || (x != y)), !((y == x) && z)) ||
 rewrite((!((x == y) && z) || (y != x)), !((y == x) && z))))))
