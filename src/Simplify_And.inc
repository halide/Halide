((b.node_type() == IRNodeType::NE) && (
 rewrite((y && (x != select(y, z, w))), !(!y || (z == x))) ||
 rewrite((y && (select(y, z, w) != x)), !(!y || (z == x))))) ||
((b.node_type() == IRNodeType::LT) && (
 rewrite((x && (select(x, y, z) < y)), false) ||
 rewrite((y && (x < select(y, x, z))), false))) ||
((b.node_type() == IRNodeType::LE) && (
 rewrite((x && ((w + (z + y)) <= y)), (x && ((w + z) <= 0))) ||
 rewrite((x && ((w + (z + y)) <= z)), (x && ((w + y) <= 0))) ||
 rewrite((x && (((z + y) + w) <= y)), (x && ((z + w) <= 0))) ||
 rewrite((x && (((z + y) + w) <= z)), (x && ((y + w) <= 0))) ||
 rewrite((x && (min(y, z) <= select(w, min(y, z), z))), x) ||
 rewrite((x && (min(y, z) <= select(w, min(z, y), z))), x) ||
 rewrite((x && (min(z, y) <= select(w, min(y, z), z))), x) ||
 rewrite((x && (min(z, y) <= select(w, min(z, y), z))), x) ||
 rewrite((x && (select(y, max(w, z), z) <= max(w, z))), x) ||
 rewrite((x && (select(y, max(w, z), z) <= max(z, w))), x) ||
 rewrite((x && (select(y, max(z, w), z) <= max(w, z))), x) ||
 rewrite((x && (select(y, max(z, w), z) <= max(z, w))), x))) ||
((b.node_type() == IRNodeType::Or) && (
 rewrite((x && (y || !x)), (x && y)) ||
 rewrite((x && (!x || y)), (x && y)))) ||
((b.node_type() == IRNodeType::Not) && (
 rewrite((x && !(y || !x)), !(y || !x)) ||
 rewrite((x && !(!x || y)), !(!x || y)) ||
 rewrite((y && !(x || y)), false) ||
 rewrite((y && !(y || x)), false))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((x && select(x, x, y)), x) ||
 rewrite((x && select(x, y, x)), (x && y)))) ||
((a.node_type() == IRNodeType::EQ) && (
((b.node_type() == IRNodeType::EQ) && (
 rewrite(((y == x) && (x == y)), (y == x)) ||
 rewrite(((y == x) && (y == x)), (y == x)))))) ||
((a.node_type() == IRNodeType::NE) && (
 rewrite(((x != select(y, z, w)) && y), !(!y || (z == x))) ||
 rewrite(((select(y, z, w) != x) && y), !((z == x) || !y)) ||
((b.node_type() == IRNodeType::NE) && (
 rewrite(((y != x) && (x != y)), (x != y)) ||
 rewrite(((y != x) && (y != x)), (x != y)))))) ||
((a.node_type() == IRNodeType::LT) && (
 rewrite(((x < select(y, x, z)) && y), false) ||
 rewrite(((select(x, y, z) < y) && x), false) ||
((b.node_type() == IRNodeType::EQ) && (
 rewrite(((x < y) && (x == y)), false) ||
 rewrite(((x < y) && (y == x)), false))) ||
((b.node_type() == IRNodeType::NE) && (
 rewrite(((x < y) && (x != y)), (x < y)) ||
 rewrite(((x < y) && (y != x)), (x < y)))) ||
((b.node_type() == IRNodeType::LT) && (
 rewrite(((x < y) && (y < x)), false) ||
 rewrite(((x < y) && (y < (x + c0))), false, (c0 <= 1)) ||
 rewrite(((x < (y + c0)) && (y < (x + c1))), false, ((c0 + c1) <= 1)))))) ||
((a.node_type() == IRNodeType::LE) && (
 rewrite((((z + (x + y)) <= y) && w), (((z + x) <= 0) && w)) ||
 rewrite((((z + (y + x)) <= y) && w), (((z + x) <= 0) && w)) ||
 rewrite(((((z + y) + w) <= y) && x), (((z + w) <= 0) && x)) ||
 rewrite(((((z + y) + w) <= z) && x), (((y + w) <= 0) && x)) ||
 rewrite(((min(y, z) <= select(w, min(y, z), z)) && x), x) ||
 rewrite(((min(y, z) <= select(w, min(z, y), z)) && x), x) ||
 rewrite(((min(z, y) <= select(w, min(y, z), z)) && x), x) ||
 rewrite(((min(z, y) <= select(w, min(z, y), z)) && x), x) ||
 rewrite(((select(y, max(w, z), z) <= max(w, z)) && x), x) ||
 rewrite(((select(y, max(w, z), z) <= max(z, w)) && x), x) ||
 rewrite(((select(y, max(z, w), z) <= max(w, z)) && x), x) ||
 rewrite(((select(y, max(z, w), z) <= max(z, w)) && x), x))) ||
((a.node_type() == IRNodeType::And) && (
((b.node_type() == IRNodeType::LT) && (
 rewrite(((w && (z && (x < y))) && (x < u)), (w && (z && (x < min(u, y))))) ||
 rewrite(((w && ((y < z) && x)) && (y < u)), (w && ((y < min(u, z)) && x))) ||
 rewrite(((z && (x < y)) && (x < w)), (z && (x < min(y, w)))) ||
 rewrite((((y < z) && x) && (y < w)), ((y < min(w, z)) && x)) ||
 rewrite((((z && (x < y)) && w) && (x < u)), (((x < min(u, y)) && w) && z)) ||
 rewrite(((((y < z) && x) && w) && (y < u)), (((y < min(u, z)) && w) && x)))))) ||
((a.node_type() == IRNodeType::Or) && (
 rewrite(((y || !x) && x), (y && x)) ||
 rewrite(((!x || y) && x), (y && x)))) ||
((a.node_type() == IRNodeType::Not) && (
 rewrite((!(x || y) && y), false) ||
 rewrite((!(y || x) && y), false) ||
 rewrite((!(y || !x) && x), !(y || !x)) ||
 rewrite((!(!x || y) && x), !(!x || y)))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite((select(x, x, y) && x), x) ||
 rewrite((select(x, y, x) && x), (y && x))))