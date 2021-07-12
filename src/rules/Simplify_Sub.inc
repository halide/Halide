((b.node_type() == IRNodeType::Mul) && (
 rewrite(c0 - (max(x, c0)*c1), min(c0 - (max(x, c0)*c1), c0), 0 <= min(c0, c1)) ||
 rewrite(c0 - (max(x, c1)*c2), min(c0 - (max(x, c1)*c2), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - (max((x*c1) + y, c2)*c1), min(c0 - (max((x*c1) + y, c2)*c1), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - (max((x*c1) + y, c2)*c3), min(c0 - (max((x*c1) + y, c2)*c3), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - (max((x*c1) + ((y*c2) + z), c3)*c3), min(c0 - (max((x*c1) + ((y*c2) + z), c3)*c3), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - (max((x*c1) + ((y*c2) + z), c3)*c4), min(c0 - (max((x*c1) + ((y*c2) + z), c3)*c4), c0), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - (max(min(x, c1), c2)*c3), min(c0 - (max(min(x, c1), c2)*c3), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - (max(min(x, c1), c2)*c3), max(c0 - (max(min(x, c1), c2)*c3), (max(c1, c2)*(0 - c3)) + c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - (max(min(x, c1), c2)*c3), max(c0 - (max(min(x, c1), c2)*c3), c0 - (max(c1, c2)*c3)), 0 <= min(min(min(c0, c1), c2), c3)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(c0 - min(max((x*c1) + ((y*c2) + z), c3)*c4, c5), min(c0 - min(max((x*c1) + ((y*c2) + z), c3)*c4, c5), c0), 0 <= min(min(min(min(min(c0, c1), c2), c3), c4), c5)) ||
 rewrite(c0 - min(max((x*c1) + ((y*c2) + z), c3)*c4, c5), max(c0 - min(max((x*c1) + ((y*c2) + z), c3)*c4, c5), c0 - c5), 0 <= min(min(min(min(min(c0, c1), c2), c3), c4), c5)) ||
 rewrite(c0 - min(max(x*c1, c2)*c3, c4), min(c0 - min(max(x*c1, c2)*c3, c4), c0), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - min(max(x*c1, c2)*c3, c4), max(c0 - min(max(x*c1, c2)*c3, c4), c0 - c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(x - min(min(x, c0) + c1, y), max(x - min(min(x, c0) + c1, y), c1), (0 <= c0) && (c1 < 0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(c0 - max(x, c0), min(c0 - max(x, c0), c0), 0 <= c0) ||
 rewrite(c0 - max(x, c1), min(c0 - max(x, c1), c0), 0 <= min(c0, c1)) ||
 rewrite(c0 - max(x, c1), min(c0 - max(x, c1), c0 - c1), (0 <= c0) && (c1 < 0)) ||
 rewrite(c0 - max((x*c1) + y, c0), min(c0 - max((x*c1) + y, c0), c0), 0 <= min(c0, c1)) ||
 rewrite(c0 - max((x*c1) + y, c2), min(c0 - max((x*c1) + y, c2), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max((x*c1) + (min(y, c2) + (z*c3)), c4), min(c0 - max((x*c1) + (min(y, c2) + (z*c3)), c4), c0), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max((x*c1) + (min(y*c2, c3) + (z*c4)), c5), min(c0 - max((x*c1) + (min(y*c2, c3) + (z*c4)), c5), c0), 0 <= min(min(min(min(min(c0, c1), c2), c3), c4), c5)) ||
 rewrite(c0 - max((x*c1) + (y*c2), c0), min(c0 - max((x*c1) + (y*c2), c0), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max((x*c1) + (y*c2), c3), min(c0 - max((x*c1) + (y*c2), c3), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max((((x*c1) + y)*c2) + (z*c3), c0), min(c0 - max((((x*c1) + y)*c2) + (z*c3), c0), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max((((x*c1) + y)*c2) + (z*c3), c4), min(c0 - max((((x*c1) + y)*c2) + (z*c3), c4), c0), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max(min(x*c1, c2) + (y*c3), c0), min(c0 - max(min(x*c1, c2) + (y*c3), c0), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(x*c1, c2) + (y*c3), c4), min(c0 - max(min(x*c1, c2) + (y*c3), c4), c0), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max(x*c1, c0), min(c0 - max(x*c1, c0), c0), 0 <= min(c0, c1)) ||
 rewrite(c0 - max(x*c1, c0), min(c0 - max(x*c1, c0), c1), 0 <= min(c0, c1)) ||
 rewrite(c0 - max(x*c1, c0), min(c0 - max(x*c1, c0), c1), (0 <= c1) && (c0 < 0)) ||
 rewrite(c0 - max(x*c1, c2), min(c0 - max(x*c1, c2), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(((x*c1) + y)*c2, c0), min(c0 - max(((x*c1) + y)*c2, c0), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(((x*c1) + y)*c2, c3), min(c0 - max(((x*c1) + y)*c2, c3), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(((x*c1) + ((y*c2) + z))*c3, c0), min(c0 - max(((x*c1) + ((y*c2) + z))*c3, c0), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(((x*c1) + ((y*c2) + z))*c3, c4), min(c0 - max(((x*c1) + ((y*c2) + z))*c3, c4), c0), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max(min((x*c1) + (y*c2), c3), c0), min(c0 - max(min((x*c1) + (y*c2), c3), c0), c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min((x*c1) + (y*c2), c3), c0), max(min(c0 - c3, 0), c0 - max(min((x*c1) + (y*c2), c3), c0)), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min((x*c1) + (y*c2), c3), c4), min(c0 - max(min((x*c1) + (y*c2), c3), c4), c0), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max(min((x*c1) + (y*c2), c3), c4), max(c0 - max(min((x*c1) + (y*c2), c3), c4), c0 - max(c3, c4)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max(min((x*c1) + (max(y, c2)*c3), c4), c5), max(c0 - max(min((x*c1) + (max(y, c2)*c3), c4), c5), c0 - max(c4, c5)), 0 <= min(min(min(min(min(c0, c1), c2), c3), c4), c5)) ||
 rewrite(c0 - max(min(x*c1, c0), c2), min(c0 - max(min(x*c1, c0), c2), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(x*c1, c0), c2), max(min(c0 - c2, 0), c0 - max(min(x*c1, c0), c2)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(x*c1, c2), c0), min(c0 - max(min(x*c1, c2), c0), c1), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(x*c1, c2), c0), min(c0 - max(min(x*c1, c2), c0), c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(x*c1, c2), c0), max(c0 - max(min(x*c1, c2), c0), 0 - c1), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(x*c1, c2), c0), max(min(c0 - c2, 0), c0 - max(min(x*c1, c2), c0)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(x*c1, c2), c0), max(min(c2 - c1, 0), c0 - max(min(x*c1, c2), c0)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(x*c1, c2), c3), min(c0 - max(min(x*c1, c2), c3), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(x*c1, c2), c3), min(c0 - max(min(x*c1, c2), c3), c0 - c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(x*c1, c2), c3), max(c0 - max(min(x*c1, c2), c3), c0 - max(c2, c3)), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(((x*c1) + y)*c2, c0), c3), min(c0 - max(min(((x*c1) + y)*c2, c0), c3), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(((x*c1) + y)*c2, c0), c3), max(min(c0 - c3, 0), c0 - max(min(((x*c1) + y)*c2, c0), c3)), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(((x*c1) + y)*c2, c3), c0), min(c0 - max(min(((x*c1) + y)*c2, c3), c0), c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(((x*c1) + y)*c2, c3), c0), max(min(c0 - c3, 0), c0 - max(min(((x*c1) + y)*c2, c3), c0)), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(((x*c1) + y)*c2, c3), c4), min(c0 - max(min(((x*c1) + y)*c2, c3), c4), c0), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max(min(((x*c1) + y)*c2, c3), c4), max(c0 - max(min(((x*c1) + y)*c2, c3), c4), c0 - max(c3, c4)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max(min(select(c1 < x, c0, c2), x), c2), min(c0 - max(min(select(c1 < x, c0, c2), x), c2), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(select(c1 < x, c0, c2), x), c2), max(min(c0 - c2, 0), c0 - max(min(select(c1 < x, c0, c2), x), c2)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(select(c1 < x, c0, c2), x*c3), c2), min(c0 - max(min(select(c1 < x, c0, c2), x*c3), c2), c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(select(c1 < x, c0, c2), x*c3), c2), max(min(c0 - c2, 0), c0 - max(min(select(c1 < x, c0, c2), x*c3), c2)), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(c0 - max(min(select(c1 < x, c2, c0), x), c0), min(c0 - max(min(select(c1 < x, c2, c0), x), c0), c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(select(c1 < x, c2, c0), x), c0), max(c0 - max(min(select(c1 < x, c2, c0), x), c0), 0 - c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(select(c1 < x, c2, c0), x), c0), max(min(c0 - c2, 0), c0 - max(min(select(c1 < x, c2, c0), x), c0)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(c0 - max(min(select(c1 < x, c2, c3), x*c4), c3), min(c0 - max(min(select(c1 < x, c2, c3), x*c4), c3), c0 - c3), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(c0 - max(min(select(c1 < x, c2, c3), x*c4), c3), max(c0 - max(min(select(c1 < x, c2, c3), x*c4), c3), c0 - max(c2, c3)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(c0 - select(x, max(y*c1, c2) + c3, c4), min(c0 - select(x, max(y*c1, c2) + c3, c4), c0 - min(c2 + c3, c4)), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (c4 < 0)) ||
 rewrite(c0 - select(c0 < x, (y + x) % c1, c2), min(c0 - select(c0 < x, (y + x) % c1, c2), c0 - c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(c0 - select(c0 < x, (y + x) % c1, c2), max(min((c1 + -1)*c2, 0), c0 - select(c0 < x, (y + x) % c1, c2)), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(c0 - select(c1 < x, max(x*c2, c3) + c4, c5), min(c0 - select(c1 < x, max(x*c2, c3) + c4, c5), c0), 0 <= min(min(min(min(min(c0, c1), c2), c3), c4), c5)) ||
 rewrite(c0 - select(c1 < x, max(x*c2, c3) + c4, c5), min(c0 - select(c1 < x, max(x*c2, c3) + c4, c5), c0 - min(c3 + c4, c5)), ((0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) && (c5 < 0)) ||
 rewrite(c0 - select(c1 < x, max(x*c2, c3) + c4, c5), min(c0 - select(c1 < x, max(x*c2, c3) + c4, c5), c0 - min(c3 + c4, c5)), (((((0 <= c1) && (c0 < 0)) && (0 <= c2)) && (0 <= c3)) && (c4 < 0)) && (c5 < 0)))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite((min(x - (y*c0), (u - v) + c0) + v) - u, min((min(x - (y*c0), (u - v) + c0) + v) - u, c0), 0 <= c0))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(((max(x, c0)*c1) - min(((x*c1) - u) + c1, z)) - u, max(((max(x, c0)*c1) - min(((x*c1) - u) + c1, z)) - u, 0 - c1), 0 <= min(c0, c1)) ||
 rewrite((min((min(u, c0) + v) + c1, w) - u) - v, min((min((min(u, c0) + v) + c1, w) - u) - v, c1), 0 <= min(c0, c1)) ||
 rewrite((min((min(u, c0) + v) + c1, z) - u) - v, min((min((min(u, c0) + v) + c1, z) - u) - v, c1), 0 <= min(c0, c1)) ||
 rewrite((min(min(u, c0) + v, (v - (w*c1)) + c2) - u) - v, min((min(min(u, c0) + v, (v - (w*c1)) + c2) - u) - v, c2), 0 <= min(min(c0, c1), c2)) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((min(x*c0, c1) - select(c2 < x, c3, c4)) - (x*c0), min((min(x*c0, c1) - select(c2 < x, c3, c4)) - (x*c0), 0 - c4), (0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite(min(x - (y*c0), min(u, c1) + c2) - u, min(min(x - (y*c0), min(u, c1) + c2) - u, c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(min(u, z + c0), min(u, z + c1) + c2) - u, min(min(min(u, z + c0), min(u, z + c1) + c2) - u, c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(min(u + c0, z), min(u, z + c1) + c2) - z, min(min(min(u + c0, z), min(u, z + c1) + c2) - z, c1), (0 <= min(c0, c1)) && (c2 < 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(u, c0) - (select(y, c1, c2) + u), min(min(u, c0) - (select(y, c1, c2) + u), cast(Int(32), intrin(Call::abs, min(c1, c2)))), ((0 <= c0) && (c1 < 0)) && (c2 < 0)) ||
 rewrite(min(u, c0) - (select(y, c1, c2) + u), min(min(u, c0) - (select(y, c1, c2) + u), 0 - min(c1, c2)), ((0 <= c0) && (c1 < 0)) && (c2 < 0)) ||
 rewrite(min(u + v, c0) - (v + u), min(min(u + v, c0) - (v + u), c0), 0 <= c0) ||
 rewrite(min((v + w) + x, u + c0) - ((w + v) + x), min(min((v + w) + x, u + c0) - ((w + v) + x), c0), 0 <= c0) ||
 rewrite(min((min(v, c0)*c1) + w, u) - ((v*c1) + w), min(min((min(v, c0)*c1) + w, u) - ((v*c1) + w), c0), 0 <= min(c0, c1)) ||
 rewrite(min((min(v, c0)*c1) + w, u + c2) - ((v*c1) + w), min(min((min(v, c0)*c1) + w, u + c2) - ((v*c1) + w), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(min(u, c0) + v, c1) - (v + u), min(min(min(u, c0) + v, c1) - (v + u), c1), 0 <= min(c0, c1)) ||
 rewrite(min(min(u, c0) + v, z) - (v + u), min(min(min(u, c0) + v, z) - (v + u), c0), 0 <= c0) ||
 rewrite(min(min(u + y, c0) + v, c1) - (u + (v + y)), min(min(min(u + y, c0) + v, c1) - (u + (v + y)), c1), 0 <= min(c0, c1)) ||
 rewrite(min(x*c0, c1) - (select(c2 < x, c3, c4) + (x*c0)), min(min(x*c0, c1) - (select(c2 < x, c3, c4) + (x*c0)), cast(Int(32), intrin(Call::abs, c4))), (0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) ||
 rewrite(min(x*c0, c1) - (select(c2 < x, c3, c4) + (x*c0)), min(min(x*c0, c1) - (select(c2 < x, c3, c4) + (x*c0)), cast(Int(32), intrin(Call::abs, min(c3, c4)))), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (c4 < 0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(u + c0, y) - min(u, min(u + c1, y) + c2), min(min(u + c0, y) - min(u, min(u + c1, y) + c2), max(c0, c1) - min(c1 + c2, 0)), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(min(u + c0, y) - min(u, min(u + c1, y) + c2), max(min(u + c0, y) - min(u, min(u + c1, y) + c2), c2), (0 <= min(c0, c1)) && (c2 < 0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(min(u + c0, z) - max(u, w), min(min(u + c0, z) - max(u, w), c0), 0 <= c0) ||
 rewrite(min(u + y, z + c0) - max(v, max(v, y) + u), min(min(u + y, z + c0) - max(v, max(v, y) + u), c0), 0 <= c0) ||
 rewrite(min(x + y, c0) - max(min(x + y, c1) + c2, y), min(min(x + y, c0) - max(min(x + y, c1) + c2, y), (c0 - c2) - min(c0, c1)), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(min(x + y, c0) - max(min(x + y, c1) + c2, y), min(min(x + y, c0) - max(min(x + y, c1) + c2, y), max(c0, c1) - (c1 + c2)), (0 <= min(c0, c1)) && (c2 < 0)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(min(x, c0) - select(y, min(x, c1), x), min(min(x, c0) - select(y, min(x, c1), x), c0), 0 <= min(c0, c1)) ||
 rewrite(min(x, c0) - select(c0 < x, min(x, c1), x), max(min(x, c0) - select(c0 < x, min(x, c1), x), 0 - c1), 0 <= min(c0, c1)) ||
 rewrite(min(x, c0) - select(c0 < x, min(x, c1), x), min(min(x, c0) - select(c0 < x, min(x, c1), x), c0), 0 <= min(c0, c1)) ||
 rewrite(min(x, c0) - select(c1 < x, min(x, c2), x), max(min(c0 - max(c1, c2), 0), min(x, c0) - select(c1 < x, min(x, c2), x)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x, c0) - select(c1 < x, min(x, c2), x), min(select(c2 < c0, c0, 0), min(x, c0) - select(c1 < x, min(x, c2), x)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x, c0) - select(c1 < x, min(x, c2), x), max(min(x, c0) - select(c1 < x, min(x, c2), x), (min(c0, c2) - c1) - c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x, c0) - select(c1 < x, min(x, c2), x), max(min(x, c0) - select(c1 < x, min(x, c2), x), 0 - max(c2 - c0, c1)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x, c0) - select(c1 < x, min(x, c2), x), min(max(c0 - c2, 0), min(x, c0) - select(c1 < x, min(x, c2), x)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x, c0) - select(c1 < x, min(x, c2), x), min(max(c0 - c2, c1), min(x, c0) - select(c1 < x, min(x, c2), x)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x + c0, y) - select(c1 < z, min(y + c2, x), x), min(min(x + c0, y) - select(c1 < z, min(y + c2, x), x), c0), 0 <= min(min(c0, c1), c2)))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Mul) && (
 rewrite(max(min(x*c0, c1), min(x*c2, c3)*c4) - (x*c0), min(select(c4 < c1, 0, c0), max(min(x*c0, c1), min(x*c2, c3)*c4) - (x*c0)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(u + c0, w) - min(u, c1), max(max(u + c0, w) - min(u, c1), c0), 0 <= min(c0, c1)) ||
 rewrite(max(v + c0, u) - min(v, c1), max(max(v + c0, u) - min(v, c1), c0), 0 <= min(c0, c1)) ||
 rewrite(max(x + y, c0) - min(y, c1), max(max(x + y, c0) - min(y, c1), c0 - c1), 0 <= min(c0, c1)) ||
 rewrite(max(min(x + y, z + c0), c1) - min(z, c2), max(max(min(x + y, z + c0), c1) - min(z, c2), c1 - c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(max(u + c0, w), c1) - min(u, c2), max(max(max(u + c0, w), c1) - min(u, c2), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(max(v + c0, u), c1) - min(v, c2), max(max(max(v + c0, u), c1) - min(v, c2), c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(max(min(x + y, z + c0), w), c1) - min(z, c2), max(max(max(min(x + y, z + c0), w), c1) - min(z, c2), c1 - c2), 0 <= min(min(c0, c1), c2)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(max(min(u, v), c0) - max(min(v, u + c0), c1), max(max(min(u, v), c0) - max(min(v, u + c0), c1), min(c1 - c0, c0) - c1), 0 <= min(c0, c1)) ||
 rewrite(max(min(u, v), c0) - max(min(v, u + c0), c1), min(max(min(u, v), c0) - max(min(v, u + c0), c1), c0), 0 <= min(c0, c1)) ||
 rewrite(max(min(u, v), c0) - max(min(v, u + c1), c2), max(c1*c2, max(min(u, v), c0) - max(min(v, u + c1), c2)), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, v), c0) - max(min(v, u + c1), c2), min(max(c1*c1, c0), max(min(u, v), c0) - max(min(v, u + c1), c2)), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, v), c0) - max(min(v, u + c1), c2), min(max(0 - c1, c0), max(min(u, v), c0) - max(min(v, u + c1), c2)), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, y), c0) - max(min(u + c1, y), c2), min(max(0 - c1, c0), max(min(u, y), c0) - max(min(u + c1, y), c2)), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, y), c0) - max(min(u + c1, y), c2), max(c1*c2, max(min(u, y), c0) - max(min(u + c1, y), c2)), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, y), c0) - max(min(u + c1, y), c2), min(max(cast(Int(32), intrin(Call::abs, c1)), c0), max(min(u, y), c0) - max(min(u + c1, y), c2)), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, v + c0), c1) - max(min(v, u), c0), max(min(c1 - c0, 0), max(min(u, v + c0), c1) - max(min(v, u), c0)), 0 <= min(c0, c1)) ||
 rewrite(max(min(u, v + c0), c1) - max(min(v, u), c0), min(max(c1 - c0, c0), max(min(u, v + c0), c1) - max(min(v, u), c0)), 0 <= min(c0, c1)) ||
 rewrite(max(min(u, v + c0), c1) - max(min(v, u), c2), max(min(c0*c2, c0), max(min(u, v + c0), c1) - max(min(v, u), c2)), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, v + c0), c1) - max(min(v, u), c2), max(min(0 - c2, c0), max(min(u, v + c0), c1) - max(min(v, u), c2)), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, v + c0), c1) - max(min(v, u), c2), min(c1*c1, max(min(u, v + c0), c1) - max(min(v, u), c2)), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, v + c0), c1) - max(min(v, u), c2), min(max(min(u, v + c0), c1) - max(min(v, u), c2), c1), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u, v + c0), c1) - max(min(v, u + c2), c3), min(max(c1 - c2, c0), max(min(u, v + c0), c1) - max(min(v, u + c2), c3)), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(max(min(u, v + c0), c1) - max(min(v, u + c2), c3), max(min(min(c1 - c3, cast(Int(32), intrin(Call::abs, c2))), c0), max(min(u, v + c0), c1) - max(min(v, u + c2), c3)), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(max(min(u + c0, v), c1) - max(min(u, v), c2), min(max(c0, c1), max(min(u + c0, v), c1) - max(min(u, v), c2)), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u + c0, v), c1) - max(min(u, v), c2), min(max(min(u + c0, v), c1) - max(min(u, v), c2), c1), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
 rewrite(max(min(u + c0, v), c1) - max(min(u, v), c2), max(min(c0*c2, c0), max(min(u + c0, v), c1) - max(min(u, v), c2)), ((0 <= c1) && (c0 < 0)) && (0 <= c2)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Max) && (
 rewrite(select(c0 < x, c1, c2) - max(x, c1), min(select(c0 < x, c1, c2) - max(x, c1), c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(select(c0 < x, c1, c2) - max(x, c3), min(select(c0 < x, c1, c2) - max(x, c3), max(c1, c2) - c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(select(c0 < x, c1, c2) - max((y*c3) + x, c1), min(select(c0 < x, c1, c2) - max((y*c3) + x, c1), c2), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(select(c0 < x, c1, c2) - max((y*c3) + x, c4), min(select(c0 < x, c1, c2) - max((y*c3) + x, c4), max(c1, c2) - c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(select(c0 < x, c1, c2) - max(x*c3, c4), min(select(c0 < x, c1, c2) - max(x*c3, c4), max(c1, c2) - c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4))))))
