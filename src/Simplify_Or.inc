((b.node_type() == IRNodeType::Not) && (
 rewrite((x || !(x && y)), true) ||
 rewrite((x || !(y && x)), true))) ||
((a.node_type() == IRNodeType::And) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::NE) && (
 rewrite(((x && (y == z)) || (y != z)), (x || (y != z))) ||
 rewrite(((x && (y == z)) || (z != y)), (x || (y != z))) ||
 rewrite((((x == y) && z) || (x != y)), (z || (x != y))) ||
 rewrite((((x == y) && z) || (y != x)), (z || (x != y))))))) ||
((a.node_type() == IRNodeType::Or) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::LT) && (
 rewrite(((x || (y < z)) || (w < z)), (x || (min(y, w) < z))) ||
 rewrite(((x || (y < z)) || (y < (w + u))), (x || (y < max(z, (w + u))))) ||
 rewrite(((x || (y < (z + w))) || (y < u)), (x || (y < max((w + z), u)))) ||
 rewrite(((x || (y || (z < w))) || (u < w)), (x || (y || (min(z, u) < w)))) ||
 rewrite(((x || (y || (z || (w < u)))) || (v < u)), (x || (y || (z || (min(w, v) < u))))) ||
 rewrite(((x || (y || ((z < w) || u))) || (v < w)), ((x || ((min(z, v) < w) || u)) || y)) ||
 rewrite(((x || ((y < z) || w)) || (u < z)), (((min(y, u) < z) || w) || x)) ||
 rewrite(((x || ((y || (z < w)) || u)) || (v < w)), ((x || ((min(z, v) < w) || u)) || y)) ||
 rewrite(((x || (((y < z) || w) || u)) || (v < z)), (x || ((u || w) || (min(y, v) < z)))) ||
 rewrite((((x < y) || z) || (w < y)), (z || (min(w, x) < y))) ||
 rewrite((((x < y) || z) || (x < (w + u))), (z || (x < max(y, (w + u))))) ||
 rewrite((((x < (y + z)) || w) || (x < u)), ((x < max(u, (y + z))) || w)) ||
 rewrite((((x || (y < z)) || w) || (u < z)), (((min(y, u) < z) || w) || x)) ||
 rewrite((((x || (y || (z < w))) || u) || (v < w)), (((x || (min(z, v) < w)) || u) || y)) ||
 rewrite((((x || ((y < z) || w)) || u) || (v < z)), ((x || (u || w)) || (min(y, v) < z))) ||
 rewrite(((((x < y) || z) || w) || (u < y)), ((w || z) || (min(x, u) < y))) ||
 rewrite(((((x || (y < z)) || w) || u) || (v < z)), ((((min(y, v) < z) || w) || u) || x)) ||
 rewrite((((((x < y) || z) || w) || u) || (v < y)), (((u || z) || w) || (min(x, v) < y))))) ||
((b.node_type() == IRNodeType::LE) && (
 rewrite(((x || (y || (z <= w))) || (u <= w)), (x || (y || (min(z, u) <= w)))) ||
 rewrite(((x || ((y <= z) || w)) || (u <= z)), (((min(y, u) <= z) || w) || x)) ||
 rewrite((((x || (y <= z)) || w) || (u <= z)), (((min(y, u) <= z) || w) || x)) ||
 rewrite(((((x <= y) || z) || w) || (u <= y)), ((w || z) || (min(x, u) <= y))))))) ||
((a.node_type() == IRNodeType::Not) && EVAL_IN_LAMBDA(
 rewrite((!(x && y) || x), true) ||
 rewrite((!(x && y) || y), true) ||
((b.node_type() == IRNodeType::EQ) && (
 rewrite((!(x && (y == z)) || (z == y)), true) ||
 rewrite((!((x == y) && z) || (y == x)), true))) ||
((b.node_type() == IRNodeType::NE) && (
 rewrite((!(x && (y == z)) || (y != z)), !(x && (z == y))) ||
 rewrite((!(x && (y == z)) || (z != y)), !(x && (y == z))) ||
 rewrite((!((x == y) && z) || (x != y)), !((y == x) && z)) ||
 rewrite((!((x == y) && z) || (y != x)), !((x == y) && z))))))
