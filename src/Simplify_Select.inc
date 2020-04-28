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
 rewrite(select(x, ((y + z) + w), ((z + u) + v)), (select(x, (y + w), (v + u)) + z))))
