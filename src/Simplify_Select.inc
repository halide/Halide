((b.node_type() == IRNodeType::Add) && (
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
 rewrite(select(x, (((z + w) + y) + u), ((w + z) + v)), (select(x, (y + u), v) + (w + z))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(select(x, max(y, z), max(w, y)), max(select(x, z, w), y)) ||
 rewrite(select(x, max(y, z), max(y, w)), max(select(x, z, w), y)) ||
 rewrite(select(x, max(z, y), max(w, y)), max(select(x, z, w), y)) ||
 rewrite(select(x, max(z, y), max(y, w)), max(select(x, z, w), y)))) ||
((a.node_type() == IRNodeType::LT) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(select((c0 < min(x, y)), (z + min(y, 0)), z), z, (-1 <= c0)) ||
 rewrite(select((c0 < min(x, y)), (min(y, 0) + z), z), z, (-1 <= c0)) ||
 rewrite(select((c0 < min(y, x)), (z + min(y, 0)), z), z, (-1 <= c0)) ||
 rewrite(select((c0 < min(y, x)), (min(y, 0) + z), z), z, (-1 <= c0)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(select((c0 < x), (x*c1), min((x*c1), c2)), (x*c1), ((((0 <= c2) || (c0 <= -1)) || (c1 <= -1)) && ((1 <= c1) && ((c0*c1) <= c2)))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(select((x < y), min(x, (y + c0)), x), x, (-1 <= c0)) ||
 rewrite(select((x < y), min((y + c0), x), x), x, (-1 <= c0)) ||
 rewrite(select((x < y), min(((y - x)*c0), c1), c1), c1, (max(c1, 1) <= c0)) ||
 rewrite(select((x < (y + c0)), min(x, y), x), x, (c0 <= 1)) ||
 rewrite(select((x < (y + c0)), min(x, (y + c1)), x), x, ((c0 + -1) <= c1)) ||
 rewrite(select((x < (y + c0)), min(y, x), x), x, (c0 <= 1)) ||
 rewrite(select((x < (y + c0)), min((y + c1), x), x), x, ((c0 + -1) <= c1)) ||
 rewrite(select((x < (y + c0)), min((y - x), c0), c0), c0, (c0 <= 0)) ||
 rewrite(select((x < (y + c0)), min((y - x), c1), c1), c1, ((c0 + c1) <= 1)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(select((c0 < x), max(x, c1), x), x, ((c1 + -1) <= c0))))))