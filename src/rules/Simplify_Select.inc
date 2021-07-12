((a.node_type() == IRNodeType::LT) && EVAL_IN_LAMBDA(
 rewrite(select(c0 < x, c0, c1), min(select(c0 < x, c0, c1), c0), (0 <= c0) && (c1 < 0)) ||
 rewrite(select(c0 < x, c0, c1), max(select(c0 < x, c0, c1), c1), (0 <= c0) && (c1 < 0)) ||
 rewrite(select(c0 < x, c1, c0), min(select(c0 < x, c1, c0), c0), (0 <= c0) && (c1 < 0)) ||
 rewrite(select(c0 < x, c1, c0), max(select(c0 < x, c1, c0), c1), (0 <= c0) && (c1 < 0)) ||
 rewrite(select(c0 < x, c1, c2), min(select(c0 < x, c1, c2), max(c1, c2)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(select(c0 < x, c1, c2), max(select(c0 < x, c1, c2), min(c1, c2)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(select(c0 < x, c1, x), min(select(c0 < x, c1, x), max(c0, c1)), 0 <= min(c0, c1)) ||
 rewrite(select(c0 < x, c1, min(x + y, c2)), min(select(c0 < x, c1, min(x + y, c2)), max(c1, c2)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(select(min(x, c0) < y, c1, min(y, c0)), min(select(min(x, c0) < y, c1, min(y, c0)), max(c0, c1)), 0 <= min(c0, c1)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(select(c0 < x, max(x*c1, c2) + c3, c4), max(select(c0 < x, max(x*c1, c2) + c3, c4), min(c2 + c3, c4)), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (c4 < 0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(select(c0 < x, c1 - max(x*c2, c1), c0), min(select(c0 < x, c1 - max(x*c2, c1), c0), c0), 0 <= min(min(c0, c1), c2)))) ||
((b.node_type() == IRNodeType::Mod) && (
 rewrite(select(c0 < x, (((y*c1) + ((z*c2) + w)) + x) % c1, c3), min(select(c0 < x, (((y*c1) + ((z*c2) + w)) + x) % c1, c3), max(c1 - c3, c3)*max(c1 - c3, c3)), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(select(c0 < x, (((y*c1) + ((z*c2) + w)) + x) % c1, c3), max(select(c0 < x, (((y*c1) + ((z*c2) + w)) + x) % c1, c3), 0), 0 <= min(min(min(c0, c1), c2), c3)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(select(c0 < x, min(x, c1), x), min(select(c0 < x, min(x, c1), x), max(c0, c1)), 0 <= min(c0, c1)) ||
 rewrite(select(c0 < x, min(x*c1, c2), x*c1), min(select(c0 < x, min(x*c1, c2), x*c1), (c0*c1) + c2), 0 <= min(min(c0, c1), c2)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(select(c0 < x, max(min(y - (x*c1), c1), c0), c1), min(select(c0 < x, max(min(y - (x*c1), c1), c0), c1), c0 + c1), 0 <= min(c0, c1)) ||
 rewrite(select(c0 < x, max(min(y - (x*c1), c1), c0), c1), min(select(c0 < x, max(min(y - (x*c1), c1), c0), c1), max(c0, c1)), 0 <= min(c0, c1)) ||
 rewrite(select(c0 < x, max(min(y - (x*c1), c1), c0), c1), max(select(c0 < x, max(min(y - (x*c1), c1), c0), c1), 0), 0 <= min(c0, c1)) ||
 rewrite(select(c0 < x, max(min(y - (x*c1), c1), c0), c1), max(select(c0 < x, max(min(y - (x*c1), c1), c0), c1), min(c1, 0)), 0 <= min(c0, c1)) ||
 rewrite(select(c0 < x, max(min(y - (x*c1), c1), c0), c2), min(select(c0 < x, max(min(y - (x*c1), c1), c0), c2), max(max(c0, c2), c1)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(select(c0 < x, max(min(y - (x*c1), c1), c0), c2), max(select(c0 < x, max(min(y - (x*c1), c1), c0), c2), 0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(select(c0 < x, max(min(y - ((x + z)*c1), c1), c0), c1), min(select(c0 < x, max(min(y - ((x + z)*c1), c1), c0), c1), max(c0, c1)), 0 <= min(c0, c1)) ||
 rewrite(select(c0 < x, max(min(y - ((x + z)*c1), c1), c0), c1), max(select(c0 < x, max(min(y - ((x + z)*c1), c1), c0), c1), 0), 0 <= min(c0, c1)) ||
 rewrite(select(c0 < x, max(min((x*c1) - y, c0), c2), c2), min(select(c0 < x, max(min((x*c1) - y, c0), c2), c2), c0), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(select(c0 < x, max(min((x*c1) - y, c0), c2), c2), max(select(c0 < x, max(min((x*c1) - y, c0), c2), c2), c2), (0 <= min(c0, c1)) && (c2 < 0))))))
