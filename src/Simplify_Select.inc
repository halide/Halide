 rewrite(select(x, y, (u + select(x, z, w))), select(x, y, (u + w))) ||
 rewrite(select(x, y, (select(x, z, w) + u)), select(x, y, (w + u))) ||
 rewrite(select(x, y, (y - select(x, z, w))), (y - select(x, 0, w))) ||
 rewrite(select(x, y, (z - select(x, w, y))), select(x, y, (z - y))) ||
 rewrite(select(x, y, (z - select(x, y, w))), select(x, y, (z - w))) ||
 rewrite(select(x, y, (select(x, z, w) - u)), select(x, y, (w - u))) ||
 rewrite(select(x, y, (u*select(x, z, w))), select(x, y, (u*w))) ||
 rewrite(select(x, y, (select(x, z, w)*u)), select(x, y, (w*u))) ||
 rewrite(select(x, y, min(u, select(x, z, w))), select(x, y, min(u, w))) ||
 rewrite(select(x, y, min(select(x, z, w), u)), select(x, y, min(w, u))) ||
 rewrite(select(x, y, max(u, select(x, z, w))), select(x, y, max(u, w))) ||
 rewrite(select(x, y, max(select(x, z, w), u)), select(x, y, max(w, u))) ||
 rewrite(select(x, x, (y == select(x, z, w))), (x || (w == y))) ||
 rewrite(select(x, x, (select(x, z, w) == y)), (x || (w == y))) ||
 rewrite(select(x, x, (y < select(x, z, y))), x) ||
 rewrite(select(x, x, (select(x, y, z) < z)), x) ||
 rewrite(select(x, x, (x && y)), x) ||
 rewrite(select(x, x, (y && x)), x) ||
 rewrite(select(x, x, (x || y)), (x || y)) ||
 rewrite(select(x, x, (y || x)), (y || x)) ||
 rewrite(select(x, x, (y || !x)), true) ||
 rewrite(select(x, x, (!x || y)), true) ||
 rewrite(select(x, x, !(y || !x)), x) ||
 rewrite(select(x, x, !(!x || y)), x) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(select(x, (w + select(x, y, z)), u), select(x, (w + y), u)) ||
 rewrite(select(x, (select(x, y, z) + w), u), select(x, (y + w), u)) ||
 rewrite(select(x, (u + (y + (z + w))), (v + (w + z))), (select(x, (u + y), v) + (w + z))) ||
 rewrite(select(x, (u + (y + (z + w))), ((w + z) + v)), (select(x, (u + y), v) + (w + z))) ||
 rewrite(select(x, (u + ((z + w) + y)), (v + (w + z))), (select(x, (u + y), v) + (w + z))) ||
 rewrite(select(x, (u + ((z + w) + y)), ((w + z) + v)), (select(x, (u + y), v) + (w + z))) ||
 rewrite(select(x, (w + (y + z)), (v + (u + z))), (select(x, (w + y), (v + u)) + z)) ||
 rewrite(select(x, (w + (y + z)), (v + (z + u))), (select(x, (w + y), (v + u)) + z)) ||
 rewrite(select(x, (w + (y + z)), ((u + z) + v)), (select(x, (w + y), (u + v)) + z)) ||
 rewrite(select(x, (w + (y + z)), ((z + u) + v)), (select(x, (w + y), (v + u)) + z)) ||
 rewrite(select(x, (w + (z + y)), (u + y)), (select(x, (w + z), u) + y)) ||
 rewrite(select(x, (w + (z + y)), (u + z)), (select(x, (w + y), u) + z)) ||
 rewrite(select(x, (w + (z + y)), (v + (u + z))), (select(x, (w + y), (v + u)) + z)) ||
 rewrite(select(x, (w + (z + y)), (v + (z + u))), (select(x, (w + y), (v + u)) + z)) ||
 rewrite(select(x, (w + (z + y)), (y + u)), (select(x, (w + z), u) + y)) ||
 rewrite(select(x, (w + (z + y)), (z + u)), (select(x, (w + y), u) + z)) ||
 rewrite(select(x, (w + (z + y)), ((u + z) + v)), (select(x, (w + y), (u + v)) + z)) ||
 rewrite(select(x, (w + (z + y)), ((z + u) + v)), (select(x, (w + y), (v + u)) + z)) ||
 rewrite(select(x, (y + z), (u + (w + z))), (select(x, y, (u + w)) + z)) ||
 rewrite(select(x, (y + z), (u + (z + w))), (select(x, y, (u + w)) + z)) ||
 rewrite(select(x, (y + z), ((w + z) + u)), (select(x, y, (w + u)) + z)) ||
 rewrite(select(x, (y + z), ((z + w) + u)), (select(x, y, (u + w)) + z)) ||
 rewrite(select(x, (z + y), (u + (w + z))), (select(x, y, (u + w)) + z)) ||
 rewrite(select(x, (z + y), (u + (z + w))), (select(x, y, (u + w)) + z)) ||
 rewrite(select(x, (z + y), ((w + z) + u)), (select(x, y, (w + u)) + z)) ||
 rewrite(select(x, (z + y), ((z + w) + u)), (select(x, y, (u + w)) + z)) ||
 rewrite(select(x, ((y + z) + w), (v + (u + z))), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, ((y + z) + w), (v + (z + u))), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, ((y + z) + w), ((u + z) + v)), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, ((y + z) + w), ((z + u) + v)), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, ((y + (z + w)) + u), (v + (w + z))), (select(x, (y + u), v) + (w + z))) ||
 rewrite(select(x, ((y + (z + w)) + u), ((w + z) + v)), (select(x, (y + u), v) + (w + z))) ||
 rewrite(select(x, ((z + y) + w), (u + y)), (select(x, (z + w), u) + y)) ||
 rewrite(select(x, ((z + y) + w), (u + z)), (select(x, (y + w), u) + z)) ||
 rewrite(select(x, ((z + y) + w), (v + (u + z))), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, ((z + y) + w), (v + (z + u))), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, ((z + y) + w), (y + u)), (select(x, (z + w), u) + y)) ||
 rewrite(select(x, ((z + y) + w), (z + u)), (select(x, (y + w), u) + z)) ||
 rewrite(select(x, ((z + y) + w), ((u + z) + v)), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, ((z + y) + w), ((z + u) + v)), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, (((z + w) + y) + u), (v + (w + z))), (select(x, (y + u), v) + (w + z))) ||
 rewrite(select(x, (((z + w) + y) + u), ((w + z) + v)), (select(x, (y + u), v) + (w + z))) ||
 rewrite(select(x, (z + y), (y*2)), (select(x, z, y) + y)) ||
 rewrite(select(x, (z + y), (z*2)), (select(x, y, z) + z)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(select(x, (y - select(x, z, w)), u), select(x, (y - z), u)) ||
 rewrite(select(x, (select(x, y, z) - w), u), select(x, (y - w), u)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(select(x, (w*select(x, y, z)), u), select(x, (w*y), u)) ||
 rewrite(select(x, (select(x, y, z)*w), u), select(x, (y*w), u)) ||
 rewrite(select(x, (y*2), (y + z)), (select(x, y, z) + y)) ||
 rewrite(select(x, (y*2), (z + y)), (select(x, y, z) + y)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(select(x, min(w, select(x, y, z)), u), select(x, min(w, y), u)) ||
 rewrite(select(x, min(select(x, y, z), w), u), select(x, min(y, w), u)) ||
 rewrite(select(x, min(z, y), min(w, y)), min(select(x, z, w), y)) ||
 rewrite(select(x, min(z, y), min(w, z)), min(select(x, y, w), z)) ||
 rewrite(select(x, min(z, y), min(y, w)), min(select(x, z, w), y)) ||
 rewrite(select(x, min(z, y), min(z, w)), min(select(x, y, w), z)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(select(x, max(w, select(x, y, z)), w), select(x, max(w, y), w)) ||
 rewrite(select(x, max(w, select(x, y, z)), y), select(x, max(w, y), y)) ||
 rewrite(select(x, max(w, select(x, y, z)), z), select(x, max(w, y), z)) ||
 rewrite(select(x, max(y, select(x, y, z)), w), select(x, y, w)) ||
 rewrite(select(x, max(z, select(x, y, z)), w), select(x, max(z, y), w)) ||
 rewrite(select(x, max(select(x, y, z), w), w), select(x, max(y, w), w)) ||
 rewrite(select(x, max(select(x, y, z), w), y), select(x, max(y, w), y)) ||
 rewrite(select(x, max(select(x, y, z), w), z), select(x, max(y, w), z)) ||
 rewrite(select(x, max(select(x, y, z), y), w), select(x, y, w)) ||
 rewrite(select(x, max(select(x, y, z), z), w), select(x, max(y, z), w)) ||
 rewrite(select(x, max(z, y), max(w, y)), max(select(x, z, w), y)) ||
 rewrite(select(x, max(z, y), max(w, z)), max(select(x, y, w), z)) ||
 rewrite(select(x, max(z, y), max(y, w)), max(select(x, z, w), y)) ||
 rewrite(select(x, max(z, y), max(z, w)), max(select(x, y, w), z)))) ||
((b.node_type() == IRNodeType::EQ) && (
 rewrite(select(x, (y == z), (w == y)), (y == select(x, z, w))) ||
 rewrite(select(x, (y == z), (y == w)), (y == select(x, z, w))) ||
 rewrite(select(x, (z == y), (w == y)), (y == select(x, z, w))) ||
 rewrite(select(x, (z == y), (y == w)), (y == select(x, z, w))))) ||
((b.node_type() == IRNodeType::NE) && (
 rewrite(select(x, (y != select(x, z, w)), x), !((y == z) || !x)) ||
 rewrite(select(x, (select(x, z, w) != y), x), !((y == z) || !x)) ||
 rewrite(select(x, (y != z), (w != y)), (select(x, z, w) != y)) ||
 rewrite(select(x, (y != z), (y != w)), (select(x, z, w) != y)) ||
 rewrite(select(x, (z != y), (w != y)), (y != select(x, z, w))) ||
 rewrite(select(x, (z != y), (y != w)), (y != select(x, z, w))))) ||
((b.node_type() == IRNodeType::LT) && (
 rewrite(select(x, (y < select(x, y, z)), x), false) ||
 rewrite(select(x, (y < select(x, z, y)), x), (y < select(x, z, y))) ||
 rewrite(select(x, (select(x, y, z) < y), x), false) ||
 rewrite(select(x, (select(x, y, z) < z), x), (select(x, y, z) < z)) ||
 rewrite(select(x, (y < z), (w < z)), (select(x, y, w) < z)) ||
 rewrite(select(x, (y < z), (y < w)), (y < select(x, z, w))))) ||
((b.node_type() == IRNodeType::And) && (
 rewrite(select(x, (x && y), x), (x && y)) ||
 rewrite(select(x, (y && x), x), (y && x)))) ||
((b.node_type() == IRNodeType::Or) && (
 rewrite(select(x, (x || y), x), x) ||
 rewrite(select(x, (y || x), x), x) ||
 rewrite(select(x, (y || !x), x), (y && x)) ||
 rewrite(select(x, (!x || y), x), (x && y)))) ||
((b.node_type() == IRNodeType::Not) && (
 rewrite(select(x, !(x || y), x), false) ||
 rewrite(select(x, !(y || x), x), false))) ||
((a.node_type() == IRNodeType::EQ) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite(select((y == x), 0, (x - y)), (x - y)) ||
 rewrite(select((y == x), 0, (y - x)), (y - x)))) ||
 rewrite(select((y == x), x, y), y) ||
 rewrite(select((y == x), y, x), x) ||
 rewrite(select((y == x), x, min(x, y)), min(x, y)) ||
 rewrite(select((y == x), x, min(y, x)), min(y, x)) ||
 rewrite(select((y == x), y, min(x, y)), min(x, y)) ||
 rewrite(select((y == x), y, min(y, x)), min(y, x)) ||
 rewrite(select((y == x), x, max(x, y)), max(x, y)) ||
 rewrite(select((y == x), x, max(y, x)), max(y, x)) ||
 rewrite(select((y == x), y, max(x, y)), max(x, y)) ||
 rewrite(select((y == x), y, max(y, x)), max(y, x)) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(select((y == x), (x - y), 0), 0) ||
 rewrite(select((y == x), (y - x), 0), 0))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(select((y == x), min(x, y), x), x) ||
 rewrite(select((y == x), min(x, y), y), y) ||
 rewrite(select((y == x), min(y, x), x), x) ||
 rewrite(select((y == x), min(y, x), y), y))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(select((y == x), max(x, y), x), x) ||
 rewrite(select((y == x), max(x, y), y), y) ||
 rewrite(select((y == x), max(y, x), x), x) ||
 rewrite(select((y == x), max(y, x), y), y))) ||
((b.node_type() == IRNodeType::NE) && (
 rewrite(select((y == x), (x != y), z), !((x == y) || !z)) ||
 rewrite(select((y == x), (y != x), z), !((y == x) || !z)))) ||
((b.node_type() == IRNodeType::LT) && (
 rewrite(select((x == y), (x < y), z), !((y == x) || !z)) ||
 rewrite(select((y == x), (x < y), z), !((x == y) || !z)))))) ||
((a.node_type() == IRNodeType::LT) && (
 rewrite(select((x < select(y, x, z)), x, z), select(y, z, min(x, z))) ||
 rewrite(select((x < select(y, x, z)), z, x), select(y, x, max(z, x))) ||
 rewrite(select((x < select(y, z, x)), x, z), select(y, min(z, x), z)) ||
 rewrite(select((x < select(y, z, x)), z, x), select(y, max(z, x), x)) ||
 rewrite(select((select(x, y, z) < y), y, z), select(x, z, max(z, y))) ||
 rewrite(select((select(x, y, z) < y), z, y), select(x, y, min(y, z))) ||
 rewrite(select((select(x, y, z) < z), y, z), select(x, min(z, y), z)) ||
 rewrite(select((select(x, y, z) < z), z, y), select(x, max(y, z), y)) ||
 rewrite(select((x < y), x, min(x, y)), min(x, y)) ||
 rewrite(select((x < y), x, min(y, x)), min(y, x)) ||
 rewrite(select((x < y), y, min(x, y)), y) ||
 rewrite(select((x < y), y, min(y, x)), y) ||
 rewrite(select((x < y), x, max(x, y)), x) ||
 rewrite(select((x < y), x, max(y, x)), x) ||
 rewrite(select((x < y), y, max(x, y)), max(x, y)) ||
 rewrite(select((x < y), y, max(y, x)), max(y, x)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(select((c0 < min(x, y)), (z + min(y, 0)), z), z, (-1 <= c0)) ||
 rewrite(select((c0 < min(x, y)), (min(y, 0) + z), z), z, (-1 <= c0)) ||
 rewrite(select((c0 < min(y, x)), (z + min(y, 0)), z), z, (-1 <= c0)) ||
 rewrite(select((c0 < min(y, x)), (min(y, 0) + z), z), z, (-1 <= c0)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(select((c0 < x), (x*c1), min((x*c1), c2)), (x*c1), ((((0 <= c2) || (c0 <= -1)) || (c1 <= -1)) && ((1 <= c1) && ((c0*c1) <= c2)))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(select((x < y), min(x, y), x), x) ||
 rewrite(select((x < y), min(x, y), y), min(x, y)) ||
 rewrite(select((x < y), min(x, (y + c0)), x), x, (-1 <= c0)) ||
 rewrite(select((x < y), min(y, x), x), x) ||
 rewrite(select((x < y), min(y, x), y), min(y, x)) ||
 rewrite(select((x < y), min((y + c0), x), x), x, (-1 <= c0)) ||
 rewrite(select((x < y), min(((y - x)*c0), c1), c1), c1, (max(c1, 1) <= c0)) ||
 rewrite(select((x < (y + c0)), min(x, y), x), x, (c0 <= 1)) ||
 rewrite(select((x < (y + c0)), min(x, (y + c1)), x), x, ((c0 + -1) <= c1)) ||
 rewrite(select((x < (y + c0)), min(y, x), x), x, (c0 <= 1)) ||
 rewrite(select((x < (y + c0)), min((y + c1), x), x), x, ((c0 + -1) <= c1)) ||
 rewrite(select((x < (y + c0)), min((y - x), c0), c0), c0, (c0 <= 0)) ||
 rewrite(select((x < (y + c0)), min((y - x), c1), c1), c1, ((c0 + c1) <= 1)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(select((c0 < x), max(x, c1), x), x, ((c1 + -1) <= c0)) ||
 rewrite(select((x < y), max(x, y), x), max(y, x)) ||
 rewrite(select((x < y), max(x, y), y), y) ||
 rewrite(select((x < y), max(y, x), x), max(y, x)) ||
 rewrite(select((x < y), max(y, x), y), y))))) ||
((a.node_type() == IRNodeType::And) && (
 rewrite(select((z && (y == x)), x, y), y) ||
 rewrite(select((z && (y == x)), y, x), x) ||
 rewrite(select((z && (y != x)), x, y), select(z, x, y)) ||
 rewrite(select((z && (y != x)), y, x), select(z, y, x)) ||
 rewrite(select((z && (x < y)), x, y), select(z, min(x, y), y)) ||
 rewrite(select((z && (x < y)), y, x), select(z, max(x, y), x)) ||
 rewrite(select(((z == y) && x), y, z), z) ||
 rewrite(select(((z == y) && x), z, y), y) ||
 rewrite(select(((y != x) && z), x, y), select(z, x, y)) ||
 rewrite(select(((y != x) && z), y, x), select(z, y, x)) ||
 rewrite(select(((y < z) && x), y, z), select(x, min(z, y), z)) ||
 rewrite(select(((y < z) && x), z, y), max(select(x, z, y), y)))) ||
((a.node_type() == IRNodeType::Or) && (
 rewrite(select((z || (y == x)), x, y), select(z, x, y)) ||
 rewrite(select((z || (y == x)), y, x), select(z, y, x)) ||
 rewrite(select((z || (y != x)), x, y), x) ||
 rewrite(select((z || (y != x)), y, x), y) ||
 rewrite(select((z || (x < y)), x, y), select(z, x, min(x, y))) ||
 rewrite(select((z || (x < y)), y, x), select(z, y, max(y, x))) ||
 rewrite(select(((z == y) || x), y, z), select(x, y, z)) ||
 rewrite(select(((z == y) || x), z, y), select(x, z, y)) ||
 rewrite(select(((y != x) || z), x, y), x) ||
 rewrite(select(((y != x) || z), y, x), y) ||
 rewrite(select(((y < z) || x), y, z), select(x, y, min(z, y))) ||
 rewrite(select(((y < z) || x), z, y), select(x, z, max(z, y))))) ||
((a.node_type() == IRNodeType::Not) && (
 rewrite(select(!(z || (y == x)), x, y), select(z, y, x)) ||
 rewrite(select(!(z || (y == x)), y, x), select(z, x, y)) ||
 rewrite(select(!((z == y) || x), y, z), select(x, z, y)) ||
 rewrite(select(!((z == y) || x), z, y), select(x, y, z)) ||
 rewrite(select(!x, y, min(y, z)), select(x, min(y, z), y)) ||
 rewrite(select(!x, y, min(z, y)), select(x, min(z, y), y)) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(select(!x, min(z, y), y), select(x, y, min(z, y))) ||
 rewrite(select(!x, min(z, y), z), select(x, z, min(z, y))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(select(!x, max(z, y), y), select(x, y, max(z, y))) ||
 rewrite(select(!x, max(z, y), z), select(x, z, max(z, y))))))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite(select(select(x, x, (z == y)), y, z), select(x, y, z)) ||
 rewrite(select(select(x, x, (z == y)), z, y), select(x, z, y)) ||
 rewrite(select(select(x, x, (z != y)), y, z), y) ||
 rewrite(select(select(x, x, (z != y)), z, y), z) ||
 rewrite(select(select(x, x, (y < z)), y, z), select(x, y, min(z, y))) ||
 rewrite(select(select(x, x, (y < z)), z, y), select(x, z, max(z, y))) ||
 rewrite(select(select(x, (z == y), x), y, z), z) ||
 rewrite(select(select(x, (z == y), x), z, y), y) ||
 rewrite(select(select(x, (z != y), x), y, z), select(x, y, z)) ||
 rewrite(select(select(x, (z != y), x), z, y), select(x, z, y)) ||
 rewrite(select(select(x, (y < z), x), z, y), select(x, max(z, y), y))))