((b.node_type() == IRNodeType::Add) && (
 rewrite(select(x, (y + (z + w)), (u + (v + w))), (select(x, (y + z), (u + v)) + w)) ||
 rewrite(select(x, (y + (z + w)), (u + (v + z))), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, (y + (z + w)), (u + (w + v))), (select(x, (y + z), (u + v)) + w)) ||
 rewrite(select(x, (y + (z + w)), (u + (z + v))), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, (y + (z + w)), ((u + w) + v)), (select(x, (y + z), (u + v)) + w)) ||
 rewrite(select(x, (y + (z + w)), ((u + z) + v)), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, (y + (z + w)), ((w + u) + v)), (select(x, (y + z), (v + u)) + w)) ||
 rewrite(select(x, (y + (z + w)), ((z + u) + v)), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, ((y + z) + w), (u + (v + y))), (select(x, (z + w), (u + v)) + y)) ||
 rewrite(select(x, ((y + z) + w), (u + (v + z))), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, ((y + z) + w), (u + (y + v))), (select(x, (z + w), (u + v)) + y)) ||
 rewrite(select(x, ((y + z) + w), (u + (z + v))), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, ((y + z) + w), ((u + y) + v)), (select(x, (z + w), (u + v)) + y)) ||
 rewrite(select(x, ((y + z) + w), ((u + z) + v)), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, ((y + z) + w), ((y + u) + v)), (select(x, (z + w), (v + u)) + y)) ||
 rewrite(select(x, ((y + z) + w), ((z + u) + v)), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, ((y/c0) + c1), ((y + z)/c0)), ((select(x, c0, z) + y)/c0), prove_me((select((c0 == 0), 0, 1) == c1))) ||
 rewrite(select(x, ((y/c0) + c1), ((z + y)/c0)), ((select(x, c0, z) + y)/c0), prove_me((select((c0 == 0), 0, 1) == c1))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(select(x, min(y, z), min(w, y)), min(select(x, z, w), y)) ||
 rewrite(select(x, min(y, z), min(w, z)), min(select(x, y, w), z)) ||
 rewrite(select(x, min(y, z), min(y, w)), min(select(x, z, w), y)) ||
 rewrite(select(x, min(y, z), min(z, w)), min(select(x, y, w), z))))
