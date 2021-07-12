((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(min(max(min(x + y, c0), c1) + c2, y), min(min(max(min(x + y, c0), c1) + c2, y), max(c0, c1) + c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(u + y, min(u + z, c0) + c1), min(min(u + y, min(u + z, c0) + c1), c0 + c1), 0 <= min(c0, c1)) ||
 rewrite(min(u + (v + z), min(u + (v + w), c0) + c1), min(min(u + (v + z), min(u + (v + w), c0) + c1), c0 + c1), 0 <= min(c0, c1)) ||
 rewrite(min(u + (min((v - u) + c0, z) - v), w + c1), min(min(u + (min((v - u) + c0, z) - v), w + c1), c0), 0 <= min(c0, c1)) ||
 rewrite(min(min(u, c0) + c1, u + y), min(min(min(u, c0) + c1, u + y), c0 + c1), 0 <= min(c0, c1)) ||
 rewrite(min(min(x, c0) + (y*c1), max(min(z + x, c2), c3) + c4), min(min(min(x, c0) + (y*c1), max(min(z + x, c2), c3) + c4), max(c2, c3) + c4), (0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(min(x - (y*c0), c1), min(min(x - (y*c0), c1), c1), 0 <= min(c0, c1)) ||
 rewrite(min(x - (((y*c0) + z)*c1), c1), min(min(x - (((y*c0) + z)*c1), c1), c1), 0 <= min(c0, c1)) ||
 rewrite(min((x - (y*c0)) - ((z*c1) + w), c0), min(min((x - (y*c0)) - ((z*c1) + w), c0), c0), 0 <= min(c0, c1)) ||
 rewrite(min(((x + y) - (z*c0)) - ((w*c1) + y), c0), min(min(((x + y) - (z*c0)) - ((w*c1) + y), c0), c0), 0 <= min(c0, c1)) ||
 rewrite(min(((x + y) - (z*c0)) - ((w*c1) + y), c1), min(min(((x + y) - (z*c0)) - ((w*c1) + y), c1), c1), 0 <= min(c0, c1)) ||
 rewrite(min((max(x + y, c0) - (z*c1)) - min(y, c2), c1), min(min((max(x + y, c0) - (z*c1)) - min(y, c2), c1), c1), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(min(u, z) - max(u, w), c0), min(min(min(u, z) - max(u, w), c0), c0), 0 <= c0) ||
 rewrite(min(min(u, z + c0) - max(u, w), c1), min(min(min(u, z + c0) - max(u, w), c1), c0), 0 <= min(c0, c1)) ||
 rewrite(min(min(select(x < y, min(y + c0, x), x), y) - x, c1), min(min(min(select(x < y, min(y + c0, x), x), y) - x, c1), c0), 0 <= min(c0, c1)) ||
 rewrite(min(min(select(x < (y + c0), z, x), x) - w, c1), min(min(min(select(x < (y + c0), z, x), x) - w, c1), c1), (0 <= c1) && (c0 < 0)) ||
 rewrite(min(max(x + y, c0) - (min(y, c1) + (z*c2)), c3), min(min(max(x + y, c0) - (min(y, c1) + (z*c2)), c3), c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min(select(c0 < x, c1, min(x + y, c2)) - (x + y), c0), min(min(select(c0 < x, c1, min(x + y, c2)) - (x + y), c0), c0), 0 <= min(min(c0, c1), c2)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(x - (y*c0), min((y*c1) + x, c2) + c3), min(min(x - (y*c0), min((y*c1) + x, c2) + c3), c2 + c3), (((0 <= c0) && (c1 < 0)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(min((x - (y*c0)) - z, min(w - z, c1) + c2), min(min((x - (y*c0)) - z, min(w - z, c1) + c2), c1 + c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(min(u, c0)*c1, min(u*c1, c2) + c3), min(min(min(u, c0)*c1, min(u*c1, c2) + c3), c2 + c3), 0 <= min(min(min(c0, c1), c2), c3)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite(min(min(x + c0, y), c1), min(min(min(x + c0, y), c1), c1), (0 <= c1) && (c0 < 0)) ||
 rewrite(min(min((min(x, c0) + y) + c1, x), c0), min(min(min((min(x, c0) + y) + c1, x), c0), c0), (0 <= c0) && (c1 < 0)) ||
 rewrite(min(min(max(x + y, c0) + c1, y), c2), min(min(min(max(x + y, c0) + c1, y), c2), c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(min(min((x - (y*c0)) - z, min(w - z, c1) + c2), c1), min(min(min((x - (y*c0)) - z, min(w - z, c1) + c2), c1), cast(Int(32), intrin(Call::abs, c1))), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(min(min(x + c0, y), z), c1), min(min(min(min(x + c0, y), z), c1), c1), (0 <= c1) && (c0 < 0)) ||
 rewrite(min(min(select(c0 < x, c1, min(y, c2)), min(y, c2)), y), min(min(min(select(c0 < x, c1, min(y, c2)), min(y, c2)), y), c2), 0 <= min(min(c0, c1), c2)))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(max(x, c0), max(y, c0) + c1), max(min(max(x, c0), max(y, c0) + c1), c1), (0 <= c0) && (c1 < 0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(min(max(min(u, v + c0), c1), max(min(v, u), c2)), max(min(max(min(u, v + c0), c1), max(min(v, u), c2)), c0), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
 rewrite(min(max(min(u + c0, v), c1), max(min(u, v), c2)), max(min(max(min(u + c0, v), c1), max(min(u, v), c2)), c0), ((0 <= c1) && (c0 < 0)) && (0 <= c2)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite(min(select(c0 < x, c1, c2), y), min(max(c1, c2), min(select(c0 < x, c1, c2), y)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(select(x < (y + c0), min(y + c1, x) - z, c2), c2), min(max(c0, c2), min(select(x < (y + c0), min(y + c1, x) - z, c2), c2)), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(min(select(x, c0, c1), select(x, c2, c3)*c4), min(max(c0, c1), min(select(x, c0, c1), select(x, c2, c3)*c4)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(min(select(x, c0, c1), select(x, c2, c3)*c4), max(min(select(x, c0, c1), select(x, c2, c3)*c4), min(c2*c4, c0)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(min(select(c0 < x, c1, c2), x*c3), min(max(c1, c2), min(select(c0 < x, c1, c2), x*c3)), 0 <= min(min(min(c0, c1), c2), c3)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(select(c0 < x, max(y, c1), min(y, c2)), min(y, c2)), min(max(c2, 0), min(select(c0 < x, max(y, c1), min(y, c2)), min(y, c2))), 0 <= min(min(c0, c1), c2))))))
