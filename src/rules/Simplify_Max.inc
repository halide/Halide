((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(max(c0 - x, x), max(max(c0 - x, x), 0), 0 <= c0) ||
 rewrite(max(x - (y*c0), c0), max(max(x - (y*c0), c0), c0), 0 <= c0) ||
 rewrite(max(((u*c0) + ((min(u, c1)*c0) + ((y*c0) + z))) - w, c1), max(max(((u*c0) + ((min(u, c1)*c0) + ((y*c0) + z))) - w, c1), c1), 0 <= min(c0, c1)) ||
 rewrite(max(min(x + y, c0) - max(min(x + y, c1) + c2, y), c1), max(max(min(x + y, c0) - max(min(x + y, c1) + c2, y), c1), c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(max(min(x + y, c0) - max(min(x + y, c1) + c2, y), c1), min(max(min(x + y, c0) - max(min(x + y, c1) + c2, y), c1), max(c0 - c1, c1) - c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(max(min((min(u, c0) + v) + c1, z) - (u + v), c2), max(max(min((min(u, c0) + v) + c1, z) - (u + v), c2), c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(min((min(u, c0) + v) + c1, z) - (u + v), c2), min(max(min((min(u, c0) + v) + c1, z) - (u + v), c2), max(c1, c2)), 0 <= min(min(c0, c1), c2)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max(c0 - x, x + c1), max(max(c0 - x, x + c1), c0), (0 <= c1) && (c0 < 0)) ||
 rewrite(max(c0 - x, max(x, c1) + c0), max(max(c0 - x, max(x, c1) + c0), c0), (0 <= c1) && (c0 < 0)) ||
 rewrite(max(c0 - (u*c1), (max(u, c2)*c1) + c1), max(max(c0 - (u*c1), (max(u, c2)*c1) + c1), c0), ((0 <= c1) && (c0 < 0)) && (c2 < 0)) ||
 rewrite(max(x - y, max(max(y - x, (x - y) + c0), c1) + c2), max(max(x - y, max(max(y - x, (x - y) + c0), c1) + c2), c2), ((0 <= c1) && (c0 < 0)) && (0 <= c2)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(max(c0 - max(u, c1), c1 - max(u, c2)), min(max(c0 - max(u, c1), c1 - max(u, c2)), max(c1 - c2, c0 - c1)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(c0 - max(u, c1), c1 - max(u, c2)), min(max(c0 - max(u, c1), c1 - max(u, c2)), max(c0 - c1, c1 - c2)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(c0 - max(u, c1), c2 - max(u, c3)), min(max(c0 - max(u, c1), c2 - max(u, c3)), max(c0 - c1, c2 - c3)), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(max(c0 - max(x, c1), c2 - max(min(x, c2), c3)), min(max(c0 - max(x, c1), c2 - max(min(x, c2), c3)), max(c2 - c3, c0 - c1)), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite(max(c0 - max(x, c1), c2 - max(min(x, c2), c3)), max(max(c0 - max(x, c1), c2 - max(min(x, c2), c3)), c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite(max(min(x - ((y*c0) + ((z*c1) + w)), c0), c2), min(max(min(x - ((y*c0) + ((z*c1) + w)), c0), c2), max(c0, c2)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(min(x - ((y*c0) + ((z*c1) + w)), c0), c2), max(max(min(x - ((y*c0) + ((z*c1) + w)), c0), c2), c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(min(x - (y*c0), c0), c1), min(max(min(x - (y*c0), c0), c1), max(c0, c1)), 0 <= min(c0, c1)) ||
 rewrite(max(min(x - (y*c0), c0), c1), max(max(min(x - (y*c0), c0), c1), c1), 0 <= min(c0, c1)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max(min(u, c0), min(u, c1) + c0), min(max(min(u, c0), min(u, c1) + c0), c0 + c1), 0 <= min(c0, c1)) ||
 rewrite(max(min(u, c0), min(u, c1) + c2), min(max(min(u, c0), min(u, c1) + c2), max(c1 + c2, c0)), (0 <= min(c0, c1)) && (c2 < 0)))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite(max(max(c0 - u, u - c0), c1), max(max(max(c0 - u, u - c0), c1), c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(c0 - x, x), c1), max(max(max(c0 - x, x), c1), c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(c0 - x, x + c1), c2), max(max(max(c0 - x, x + c1), c2), c1), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(max(c0 - x, x - c0), c1), max(max(max(c0 - x, x - c0), c1), c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(c0 - x, x - c1), c2), max(max(max(c0 - x, x - c1), c2), c2), 0 <= min(min(c0, c1), c2)))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Select) && (
 rewrite(max(select(u < z, c0, c1), select(u < (z + c0), c0, c1)), min(max(select(u < z, c0, c1), select(u < (z + c0), c0, c1)), max(c0, c1)), 0 <= min(c0, c1)) ||
 rewrite(max(select(u < z, c0, c1), select(u < (z + c0), c0, c1)), max(max(select(u < z, c0, c1), select(u < (z + c0), c0, c1)), min(c0, c1)), 0 <= min(c0, c1))))))
