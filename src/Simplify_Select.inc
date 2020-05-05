((b.node_type() == IRNodeType::Add) && (
 rewrite(select(x, (y + (z + w)), (u + (v + w))), (select(x, (y + z), (u + v)) + w)) ||
 rewrite(select(x, (y + (z + w)), (u + (v + z))), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, (y + (z + w)), (u + (w + v))), (select(x, (y + z), (u + v)) + w)) ||
 rewrite(select(x, (y + (z + w)), (u + (z + v))), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, (y + (z + w)), ((u + w) + v)), (select(x, (y + z), (u + v)) + w)) ||
 rewrite(select(x, (y + (z + w)), ((u + z) + v)), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, (y + (z + w)), ((w + u) + v)), (select(x, (y + z), (v + u)) + w)) ||
 rewrite(select(x, (y + (z + w)), ((z + u) + v)), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, (y + (z + (w + u))), (u + w)), (select(x, (y + (z + u)), u) + w)) ||
 rewrite(select(x, (y + (z + (w + u))), (w + u)), (select(x, (y + (z + w)), w) + u)) ||
 rewrite(select(x, (y + ((z + w) + u)), (w + z)), (select(x, (y + (z + u)), z) + w)) ||
 rewrite(select(x, (y + ((z + w) + u)), (z + w)), (select(x, (y + (w + u)), w) + z)) ||
 rewrite(select(x, ((y + z) + w), (u + (v + y))), (select(x, (z + w), (u + v)) + y)) ||
 rewrite(select(x, ((y + z) + w), (u + (v + z))), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, ((y + z) + w), (u + (y + v))), (select(x, (z + w), (u + v)) + y)) ||
 rewrite(select(x, ((y + z) + w), (u + (z + v))), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, ((y + z) + w), ((u + y) + v)), (select(x, (z + w), (u + v)) + y)) ||
 rewrite(select(x, ((y + z) + w), ((u + z) + v)), (select(x, (y + w), (u + v)) + z)) ||
 rewrite(select(x, ((y + z) + w), ((y + u) + v)), (select(x, (z + w), (v + u)) + y)) ||
 rewrite(select(x, ((y + z) + w), ((z + u) + v)), (select(x, (y + w), (v + u)) + z)) ||
 rewrite(select(x, ((y + (z + w)) + u), (w + z)), (select(x, ((y + w) + u), w) + z)) ||
 rewrite(select(x, ((y + (z + w)) + u), (z + w)), (select(x, ((y + z) + u), z) + w)) ||
 rewrite(select(x, (((y + z) + w) + u), (y + z)), (select(x, ((y + w) + u), y) + z)) ||
 rewrite(select(x, (((y + z) + w) + u), (z + y)), (select(x, ((y + w) + u), y) + z)) ||
 rewrite(select(x, (y + (z - w)), (u - w)), (select(x, (y + z), u) - w)) ||
 rewrite(select(x, ((y - z) + w), (u - z)), (select(x, (y + w), u) - z)) ||
 rewrite(select(x, ((y/c0) + c1), ((y + z)/c0)), ((select(x, c0, z) + y)/c0), prove_me((c1 == 1))) ||
 rewrite(select(x, ((y/c0) + c1), ((z + y)/c0)), ((select(x, c0, z) + y)/c0), prove_me((c1 == 1))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(select(x, min(y, z), min(w, y)), min(select(x, z, w), y)) ||
 rewrite(select(x, min(y, z), min(w, z)), min(select(x, y, w), z)) ||
 rewrite(select(x, min(y, z), min(y, w)), min(select(x, z, w), y)) ||
 rewrite(select(x, min(y, z), min(z, w)), min(select(x, y, w), z)))) ||
((a.node_type() == IRNodeType::LT) && EVAL_IN_LAMBDA(
 rewrite(select(((x % c0) < select(y, c1, c2)), c3, c4), select(y, c4, c3), (((((c1 <= 0) && (1 < c2)) && ((2 % c0) < c2)) && ((-1 % c0) < c2)) || (c3 == c4))) ||
 rewrite(select(((x % c0) < select(y, c1, c2)), z, w), select(y, w, z), ((c1 <= 0) && (max(max(max((-1 % c0), (1 % c0)), (3 % c0)), (7721 % c0)) < c2)))))
