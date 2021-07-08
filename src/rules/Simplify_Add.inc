((b.node_type() == IRNodeType::Add) && (
 rewrite(u + ((min(v, c0) - (v + (u + z))) + z), min(u + ((min(v, c0) - (v + (u + z))) + z), c0), 0 <= c0))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(u + (min((v - u) + c0, z) - v), min(u + (min((v - u) + c0, z) - v), c0), 0 <= c0) ||
 rewrite(u + (min(((y*c0) - u) + c0, z) - (max(y, c1)*c0)), min(u + (min(((y*c0) - u) + c0, z) - (max(y, c1)*c0)), c0), 0 <= min(c0, c1)) ||
 rewrite(u + (min(y - (z*c0), (v - u) + c0) - v), min(u + (min(y - (z*c0), (v - u) + c0) - v), c0), 0 <= c0))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite((u + (min((v - u) + c0, z) - v)) + c1, min((u + (min((v - u) + c0, z) - v)) + c1, c0), (0 <= c0) && (c1 < 0)) ||
 rewrite(((u*c0) + ((z*c0) - max((u + z)*c0, (u*c0) + c1))) + c0, min(((u*c0) + ((z*c0) - max((u + z)*c0, (u*c0) + c1))) + c0, c0), (0 <= c0) && (c1 < 0)) ||
 rewrite((min((u - v) + c0, z) + (v - u)) + c1, min((min((u - v) + c0, z) + (v - u)) + c1, c0), (0 <= c0) && (c1 < 0)) ||
 rewrite((max(x, c0) + min((min(y, c0)*c1) - x, c0)) + c1, min((max(x, c0) + min((min(y, c0)*c1) - x, c0)) + c1, (((c0 + 1)*c1) + c0) + c0), 0 <= min(c0, c1)) ||
 rewrite((max(x, c0) + min((min(y, c0)*c1) - x, c0)) + c1, min((max(x, c0) + min((min(y, c0)*c1) - x, c0)) + c1, (c0*c1) + ((c0 + c1) + c0)), 0 <= min(c0, c1)) ||
 rewrite((max(x, c0) + min((min(y, c0)*c1) - x, c0)) + c1, min((max(x, c0) + min((min(y, c0)*c1) - x, c0)) + c1, max(c0*2, c1) + (c0*c1)), 0 <= min(c0, c1)))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(((min(u, c0) + (y + z)) - ((u + y) + z)) + c1, min(((min(u, c0) + (y + z)) - ((u + y) + z)) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(((((min((u + v) + c0, z) + w) - u) - v) - w) + c1, min(((((min((u + v) + c0, z) + w) - u) - v) - w) + c1, c0 + c1), 0 <= min(c0, c1)) ||
 rewrite(((min((min(u, c0) + v) + c0, w) - u) - v) + c1, min(((min((min(u, c0) + v) + c0, w) - u) - v) + c1, c0 + c1), 0 <= min(c0, c1)) ||
 rewrite(((min((min(u, c0) + v) + c1, w) - u) - v) + c1, min(c1*2, ((min((min(u, c0) + v) + c1, w) - u) - v) + c1), 0 <= min(c0, c1)) ||
 rewrite(((min((min(u, c0) + v) + c1, w) - u) - v) + c2, min(((min((min(u, c0) + v) + c1, w) - u) - v) + c2, c1 + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min((min(v, c0) + w) + c0, u) - v) - w) + c1, min(((min((min(v, c0) + w) + c0, u) - v) - w) + c1, c0), (0 <= c0) && (c1 < 0)) ||
 rewrite(((min((min(v, c0) + w) + c1, u) - v) - w) + c2, min(((min((min(v, c0) + w) + c1, u) - v) - w) + c2, c1), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(((min(x, c0)*c1) - min(x*c1, c2)) + c1, min(((min(x, c0)*c1) - min(x*c1, c2)) + c1, max(c0*c1, c2) + (c1 - c2)), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0)*c1) - min(x*c1, c2)) + c1, max(((min(x, c0)*c1) - min(x*c1, c2)) + c1, min((c1*2) - c2, 0) + c1), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0)*c1) - min(x*c1, c2)) + c2, min(max(c0*c1, c2), ((min(x, c0)*c1) - min(x*c1, c2)) + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0)*c1) - min(x*c1, c2)) + c2, max(min(c0*c1, c2), ((min(x, c0)*c1) - min(x*c1, c2)) + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0)*c1) - min(x*c1, c2)) + c3, min(((min(x, c0)*c1) - min(x*c1, c2)) + c3, cast(Int(32), intrin(Call::abs, c3))), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(((min(x, c0)*c1) - min(x*c1, c2)) + c3, max((c3 - max(c1 - c2, c0)) - max(c1 - c2, c0), ((min(x, c0)*c1) - min(x*c1, c2)) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(((min(x, c0)*c1) - min(x*c1, c2)) + c3, max(select(c1 < c3, 0, c3 - c2), ((min(x, c0)*c1) - min(x*c1, c2)) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(((max(x, c0)*c1) - select(c2 < x, c0, c2)) + c3, min(((max(x, c0)*c1) - select(c2 < x, c0, c2)) + c3, c3), (((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (0 <= c3)) ||
 rewrite(((max(x, c0)*c1) - select(c2 < x, c2, c3)) + c4, min(((max(x, c0)*c1) - select(c2 < x, c2, c3)) + c4, ((c0*c1) - c3) + c4), ((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite(((max(x, c0)*c1) - select(c2 < x, c3, c4)) + c5, min(((max(x, c0)*c1) - select(c2 < x, c3, c4)) + c5, (c0*c1) + (c5 - min(c3, c4))), (((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (c3 < 0)) && (c4 < 0)) && (0 <= c5)) ||
 rewrite(((max(x, c0)*c1) - select(c2 < x, c3, c4)) + c5, min(((max(x, c0)*c1) - select(c2 < x, c3, c4)) + c5, (c0*c1) + (c5 - c4)), (((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (0 <= c3)) && (c4 < 0)) && (0 <= c5)) ||
 rewrite(((max(x, c0)*c1) - select(c2 < x, c3, c4)) + c5, min(((c0*c1) + c5) - c4, ((max(x, c0)*c1) - select(c2 < x, c3, c4)) + c5), (((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (0 <= c3)) && (c4 < 0)) && (0 <= c5)) ||
 // rewrite(((max(x*c0, c1)*c2) - select(c3 < x, c4, c5)) + c6, min(((max(x*c0, c1)*c2) - select(c3 < x, c4, c5)) + c6, ((c1*c2) - c5) + c6), (((((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) && (0 <= c4)) && (c5 < 0)) && (0 <= c6)) ||
 rewrite((min(x, c0) - select(c0 < x, min(x, c1), x)) + c1, min((min(x, c0) - select(c0 < x, min(x, c1), x)) + c1, c0 + c1), 0 <= min(c0, c1)) ||
 rewrite((min(x, c0) - select(c0 < x, min(x, c1), x)) + c1, max((min(x, c0) - select(c0 < x, min(x, c1), x)) + c1, 0), 0 <= min(c0, c1)) ||
 rewrite((min(x, c0) - select(c1 < x, min(x, c2), x)) + c0, min((min(x, c0) - select(c1 < x, min(x, c2), x)) + c0, select(c0 < c2, 0, c0) + c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite((min(x, c0) - select(c1 < x, min(x, c2), x)) + c0, max(c0 - max(c2 - c0, c1), (min(x, c0) - select(c1 < x, min(x, c2), x)) + c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite((min(x, c0) - select(c1 < x, min(x, c2), x)) + c3, min((min(x, c0) - select(c1 < x, min(x, c2), x)) + c3, (c3 - min(c0, c2)) + c0), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite((min(x, c0) - select(c1 < x, min(x, c2), x)) + c3, min((c0 + c3) - min(c0, c2), (min(x, c0) - select(c1 < x, min(x, c2), x)) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite((min(x, c0) - select(c1 < x, min(x, c2), x)) + c3, max(c3 - max(c2 - c0, c1), (min(x, c0) - select(c1 < x, min(x, c2), x)) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite((min(u + c0, z) - min(min(u + c1, z) + c2, u)) + c3, max((min(u + c0, z) - min(min(u + c1, z) + c2, u)) + c3, c2), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite((min(u + c0, z) - max(u, w)) + c1, min((min(u + c0, z) - max(u, w)) + c1, c1), (0 <= c1) && (c0 < 0)) ||
 rewrite((min(x + c0, y) - max(z, x)) + c1, min((min(x + c0, y) - max(z, x)) + c1, c0), (0 <= c0) && (c1 < 0)) ||
 rewrite((min(x + c0, y) - max(z, x)) + c1, min((min(x + c0, y) - max(z, x)) + c1, c1), (0 <= c1) && (c0 < 0)) ||
 rewrite((min((u + w) + x, v + c0) - ((w + u) + x)) + c1, min((min((u + w) + x, v + c0) - ((w + u) + x)) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite((min((v + w) + x, u + c0) - ((w + v) + x)) + c1, min((min((v + w) + x, u + c0) - ((w + v) + x)) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite((min(min(u, (z*c0) + c1) + c2, w) - u) + c3, min((min(min(u, (z*c0) + c1) + c2, w) - u) + c3, c2 + c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite((min(min(v, z + c0) + c1, w + u) - v) + c2, min((min(min(v, z + c0) + c1, w + u) - v) + c2, c1 + c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite((min(min(x + c0, y) + c1, z + w) - y) + c2, min((min(min(x + c0, y) + c1, z + w) - y) + c2, c1 + c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite((min(x*c0, c1) - (select(c2 < x, c3, c2) + (x*c0))) + c4, min(max(c4, 0), (min(x*c0, c1) - (select(c2 < x, c3, c2) + (x*c0))) + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite((min(x*c0, c1) - (select(c2 < x, c3, c4) + (x*c0))) + c0, min(c0 - c4, (min(x*c0, c1) - (select(c2 < x, c3, c4) + (x*c0))) + c0), (0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) ||
 rewrite((min(x*c0, c1) - (select(c2 < x, c3, c4) + (x*c0))) + c5, min(c5 - c4, (min(x*c0, c1) - (select(c2 < x, c3, c4) + (x*c0))) + c5), ((0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) && (0 <= c5)) ||
 rewrite((max(x, y) - min(min(y, z), x)) + c0, max((max(x, y) - min(min(y, z), x)) + c0, c0), 0 <= c0) ||
 rewrite((max(u + c0, z) - min(u, w)) + c1, max((max(u + c0, z) - min(u, w)) + c1, c0), 0 <= min(c0, c1)) ||
 rewrite((max(x + c0, u) - min(u, x)) + c1, max((max(x + c0, u) - min(u, x)) + c1, c0), 0 <= min(c0, c1)) ||
 rewrite((max(min(x + y, c0), c1) - max(min(y, c0), c1)) + c2, min((max(min(x + y, c0), c1) - max(min(y, c0), c1)) + c2, c0), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite((max(min(x + y, c0), c1) - max(min(y, c0), c1)) + c2, max(c2 - c0, (max(min(x + y, c0), c1) - max(min(y, c0), c1)) + c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite((max(max(u, w), c0) - min(min(u, w), c0)) + c1, max((max(max(u, w), c0) - min(min(u, w), c0)) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite((max(max(x + c0, y), c1) - x) + c2, max((max(max(x + c0, y), c1) - x) + c2, c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite((select(c0 < x, c0, c1) - max(x, c1)) + c1, min((select(c0 < x, c0, c1) - max(x, c1)) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite((select(c0 < x, c0, c1) - max(x, c1)) + c2, min((select(c0 < x, c0, c1) - max(x, c1)) + c2, c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite((select(c0 < x, c1, c2) - max(x, c2)) + c3, min((select(c0 < x, c1, c2) - max(x, c2)) + c3, max(c1 - c2, 0) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite((select(c0 < x, c1, c2) - max(x, c2)) + c3, min(c3 - min(c2 - c1, 0), (select(c0 < x, c1, c2) - max(x, c2)) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite((select(c0 < x, c1, c2) - max(x, c3)) + c4, min((select(c0 < x, c1, c2) - max(x, c3)) + c4, max(c1, c2) + (c4 - c3)), (0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) ||
 rewrite((select(c0 < x, c1, c2) - max(x, c3)) + c4, min(max(c2 + c4, c1) - c3, (select(c0 < x, c1, c2) - max(x, c3)) + c4), (0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) ||
 rewrite((select(c0 < x, c1, c2) - max((y*c3) + x, c4)) + c5, min((select(c0 < x, c1, c2) - max((y*c3) + x, c4)) + c5, max(c1, c2) + (c5 - c4)), (0 <= min(min(min(min(c0, c1), c2), c3), c4)) && (c5 < 0)) ||
 rewrite((select(c0 < x, c1, c2) - max((y*c3) + x, c4)) + c5, min((max(c1, c2) + c5) - c4, (select(c0 < x, c1, c2) - max((y*c3) + x, c4)) + c5), (0 <= min(min(min(min(c0, c1), c2), c3), c4)) && (c5 < 0)) ||
 rewrite((select(c0 < x, c1, c2) - max(x*c3, c4)) + c5, min((select(c0 < x, c1, c2) - max(x*c3, c4)) + c5, max(c1, c2) + (c5 - c4)), 0 <= min(min(min(min(min(c0, c1), c2), c3), c4), c5)))) ||
((a.node_type() == IRNodeType::Mod) && EVAL_IN_LAMBDA(
 rewrite(((select(c0 < x, c1, c2) + y) % c3) + c4, max(((select(c0 < x, c1, c2) + y) % c3) + c4, c4), (((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) && (0 <= c4)))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
 rewrite((((min((min(u, c0) + v) + c0, w) - u) - v)*c1) + c0, min((c1 + 1)*c0, (((min((min(u, c0) + v) + c0, w) - u) - v)*c1) + c0), 0 <= min(c0, c1)) ||
 rewrite((((min((min(u, c0) + v) + c0, w) - u) - v)*c1) + c2, min((((min((min(u, c0) + v) + c0, w) - u) - v)*c1) + c2, (c0*c1) + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite((((min((min(u, c0) + v) + c1, w) - u) - v)*c1) + c2, min((((min((min(u, c0) + v) + c1, w) - u) - v)*c1) + c2, (c1*c1) + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite((((min((min(u, c0) + v) + c1, w) - u) - v)*c2) + c1, min((c2 + 1)*c1, (((min((min(u, c0) + v) + c1, w) - u) - v)*c2) + c1), 0 <= min(min(c0, c1), c2)) ||
 rewrite((((min((min(u, c0) + v) + c1, w) - u) - v)*c2) + c3, min((((min((min(u, c0) + v) + c1, w) - u) - v)*c2) + c3, (c1*c2) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(((min(x, c0) - select(c0 < x, min(x, c1), x))*c2) + c2, min((c0 + 1)*c2, ((min(x, c0) - select(c0 < x, min(x, c1), x))*c2) + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0) - select(c0 < x, min(x, c1), x))*c2) + c2, max(((min(x, c0) - select(c0 < x, min(x, c1), x))*c2) + c2, 0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0) - select(c1 < x, min(x, c2), x))*c2) + c0, min(c0*c0, ((min(x, c0) - select(c1 < x, min(x, c2), x))*c2) + c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0) - select(c1 < x, min(x, c2), x))*c2) + c0, max(c0 - (max(c2 - c0, c1)*c2), ((min(x, c0) - select(c1 < x, min(x, c2), x))*c2) + c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0) - select(c1 < x, min(x, c2), x))*c2) + c2, min(max(c0*c2, c2), ((min(x, c0) - select(c1 < x, min(x, c2), x))*c2) + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0) - select(c1 < x, min(x, c2), x))*c2) + c2, max((2 - c2)*c2, ((min(x, c0) - select(c1 < x, min(x, c2), x))*c2) + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(((min(x, c0) - select(c1 < x, min(x, c2), x))*c3) + c3, min((1 - min(c2 - c0, 0))*c3, ((min(x, c0) - select(c1 < x, min(x, c2), x))*c3) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(((min(x, c0) - select(c1 < x, min(x, c2), x))*c3) + c4, max(c4 - c3, ((min(x, c0) - select(c1 < x, min(x, c2), x))*c3) + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite((min(x - y, c0)*c1) + c2, min((min(x - y, c0)*c1) + c2, (c0*c1) + c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite((min(x - ((y*c0) + z), c1)*c2) + c3, min(c3 - c2, (min(x - ((y*c0) + z), c1)*c2) + c3), (((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (0 <= c3)) ||
 rewrite((max(x, c0)*c1) + c2, max((max(x, c0)*c1) + c2, c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite((max(x, c0)*c1) + c2, min((max(x, c0)*c1) + c2, c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite((max((x*c0) + (min(y, c1) + (z*c2)), c3)*c4) + c5, min((max((x*c0) + (min(y, c1) + (z*c2)), c3)*c4) + c5, c5), ((0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) && (0 <= c5)) ||
 rewrite((max(x - max(min(x, c0), c1), c2)*c3) + c4, min((max(x - max(min(x, c0), c1), c2)*c3) + c4, c0), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (c4 < 0)) ||
 rewrite((max(max(u, w) - min(u, w), c0)*c1) + c2, max((max(max(u, w) - min(u, w), c0)*c1) + c2, c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite((max(max(u, z) - min(u, z), c0)*c1) + c1, max((max(max(u, z) - min(u, z), c0)*c1) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite((max(min(u, c0), min(u, c1) + c0)*c2) + c2, min(((c0 + c1) + 1)*c2, (max(min(u, c0), min(u, c1) + c0)*c2) + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite((max(min(x, c0), c1)*c2) + c3, min((max(min(x, c0), c1)*c2) + c3, (max(c0, c1)*c2) + c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite((max(min(x, c0), c1)*c2) + c3, min(c3 - (max(c0, c1)*(0 - c2)), (max(min(x, c0), c1)*c2) + c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite((max(min(x, c0), c1)*c2) + c3, max((max(min(x, c0), c1)*c2) + c3, c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite((max(min(x, c0), c1)*c2) + c3, min((max(min(x, c0), c1)*c2) + c3, c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite((max(min(x, c0), c1)*c2) + c3, max((max(min(x, c0), c1)*c2) + c3, (max(c0, c1)*c2) + c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite((max(min((min(x, c0)*c1) - y, c2), c3)*c4) + c5, min((max(min((min(x, c0)*c1) - y, c2), c3)*c4) + c5, c5), ((((0 <= min(c0, c1)) && (c2 < 0)) && (c3 < 0)) && (0 <= c4)) && (0 <= c5)) ||
 rewrite((max(min((min(x, c0)*c1) - y, c2), c3)*c4) + c5, max((max(min((min(x, c0)*c1) - y, c2), c3)*c4) + c5, (c3*c4) + c5), ((((0 <= min(c0, c1)) && (c2 < 0)) && (c3 < 0)) && (0 <= c4)) && (0 <= c5)) ||
 rewrite((max(min(x*c0, c1), c2)*c3) + c4, min((max(min(x*c0, c1), c2)*c3) + c4, c4), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite((max(min(x*c0, c1), c2)*c3) + c4, max((max(min(x*c0, c1), c2)*c3) + c4, (max(c1, c2)*c3) + c4), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite((max(min(select(c0 < x, c1, c2), x), c2)*c3) + c4, min((max(min(select(c0 < x, c1, c2), x), c2)*c3) + c4, c4), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite((max(min(select(c0 < x, c1, c2), x), c2)*c3) + c4, max((max(min(select(c0 < x, c1, c2), x), c2)*c3) + c4, (max(c1, c2)*c3) + c4), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite((max(max(u, c0 - u), c1)*c2) + c2, max((max(max(u, c0 - u), c1)*c2) + c2, c2), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
 rewrite((select(c0 < x, c1, c2)*c3) + c4, min((select(c0 < x, c1, c2)*c3) + c4, (c2*c3) + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite((select(c0 < x, c1, c2)*c3) + c4, max((select(c0 < x, c1, c2)*c3) + c4, c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite((select(c0 < x, c1, c2)*c3) + c4, max((select(c0 < x, c1, c2)*c3) + c4, (min(c1, c2)*c3) + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite((select(c0 < x, min(x, c1), x)*c2) + c1, max((select(c0 < x, min(x, c1), x)*c2) + c1, ((c0 + c1)*c2) + c1), (0 <= min(c0, c1)) && (c2 < 0)))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite(min((x*c0) + (y*c1), c2) + c0, min(min((x*c0) + (y*c1), c2) + c0, c0 + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(c0 - (min(x, c1)*c2), c3) + c2, min(min(c0 - (min(x, c1)*c2), c3) + c2, c2 + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min(c0 - (min(x, c1)*c2), c3) + c2, max((c0 + c2) - max(c1*c2, c0), min(c0 - (min(x, c1)*c2), c3) + c2), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min(c0 - (min(x, c1)*c2), c3) + c4, min(min(c0 - (min(x, c1)*c2), c3) + c4, c3 + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(min(u - (max(u, c0) + (y*c1)), c2) + c3, min(min(u - (max(u, c0) + (y*c1)), c2) + c3, c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(min(u - (max(u, c0) + (y*c1)), c2) + c3, min(min(u - (max(u, c0) + (y*c1)), c2) + c3, c3), (((0 <= c1) && (c0 < 0)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(min(x - (min(x, c0) + (y*c0)), c1) + c2, min(min(x - (min(x, c0) + (y*c0)), c1) + c2, c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(min(x - (min(x, c0) + (y*c1)), c2) + c3, min(min(x - (min(x, c0) + (y*c1)), c2) + c3, c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(min(x - (min(y*c0, c1) + (z*c2)), c3) + c3, min(c3*2, min(x - (min(y*c0, c1) + (z*c2)), c3) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min(x - (min(y*c0, c1) + (z*c2)), c3) + c4, min(min(x - (min(y*c0, c1) + (z*c2)), c3) + c4, c3 + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(min(x - (min(y*c0, c1) + (z*c2)), c3) + c4, min(min(x - (min(y*c0, c1) + (z*c2)), c3) + c4, c4), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite(min(x - (y*c0), c0) + c1, min(min(x - (y*c0), c0) + c1, c0 + c1), 0 <= min(c0, c1)) ||
 rewrite(min(x - (y*c0), c1) + c2, min(min(x - (y*c0), c1) + c2, c1 + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x - (((y*c0) + z)*c1), c0) + c2, min(min(x - (((y*c0) + z)*c1), c0) + c2, c0 + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x - (((y*c0) + z)*c1), c1) + c2, min(min(x - (((y*c0) + z)*c1), c1) + c2, c1 + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(x - (((y*c0) + z)*c1), c2) + c3, min(min(x - (((y*c0) + z)*c1), c2) + c3, c2 + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min(x - (((y*c0) + z)*c1), c2) + c3, min(min(x - (((y*c0) + z)*c1), c2) + c3, c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(min(x - (((y*c0) + ((z*c1) + w))*c2), c3) + c1, min(min(x - (((y*c0) + ((z*c1) + w))*c2), c3) + c1, c1 + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min((u + y) - (max(u, c0) + z), c1) + c2, min(min((u + y) - (max(u, c0) + z), c1) + c2, c2), (0 <= c2) && (max(c0, c1) < 0)) ||
 rewrite(min((u + y) - (max(u, c0) + z), c1) + c2, min(min((u + y) - (max(u, c0) + z), c1) + c2, c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(min((c0 - (x*c1)) - (y*c2), c3) + c4, min(min((c0 - (x*c1)) - (y*c2), c3) + c4, c4), ((0 <= min(min(c0, c1), c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite(min((c0 - (x*c1)) - (((y*c2) + z)*c3), c4) + c5, min(min((c0 - (x*c1)) - (((y*c2) + z)*c3), c4) + c5, c5), ((0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) && (0 <= c5)) ||
 rewrite(min((c0 - (x*c1)) - min(y*c2, c3), c4) + c5, min(min((c0 - (x*c1)) - min(y*c2, c3), c4) + c5, c5), ((0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) && (0 <= c5)) ||
 rewrite(min((c0 - max(x*c1, c2)) - (y*c3), c4) + c5, min(min((c0 - max(x*c1, c2)) - (y*c3), c4) + c5, c5), ((0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0)) && (0 <= c5)) ||
 rewrite(min((x - (y*c0)) - ((z*c1) + w), c2) + c3, min(min((x - (y*c0)) - ((z*c1) + w), c2) + c3, c2 + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min(((x + y) - (z*c0)) - ((w*c1) + x), c2) + c0, min(min(((x + y) - (z*c0)) - ((w*c1) + x), c2) + c0, c0 + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(((x + y) - (z*c0)) - ((w*c1) + y), c0) + c2, min(min(((x + y) - (z*c0)) - ((w*c1) + y), c0) + c2, c0 + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(min(((x + y) - (z*c0)) - ((w*c1) + y), c2) + c3, min(min(((x + y) - (z*c0)) - ((w*c1) + y), c2) + c3, c2 + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min(((x + y) - (z*c0)) - ((w*c1) + y), c2) + c3, min(min(((x + y) - (z*c0)) - ((w*c1) + y), c2) + c3, c2 + c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(min(((x*c0) - (y*c1)) - z, c2) + c3, min(min(((x*c0) - (y*c1)) - z, c2) + c3, c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(min((max(x, c0)*c1) - (y*c2), c3) + c4, min(min((max(x, c0)*c1) - (y*c2), c3) + c4, c4), ((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite(min((max(x, c0)*c1) - max(x*c2, c3), c4) + c5, min(min((max(x, c0)*c1) - max(x*c2, c3), c4) + c5, c5), (((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (0 <= c3)) && (c4 < 0)) && (0 <= c5)) ||
 rewrite(min(min(u, c0) - (u + y), c1) + c2, min(min(min(u, c0) - (u + y), c1) + c2, c1 + c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(min(min(u, c0) - (u + y), c1) + c2, min(min(min(u, c0) - (u + y), c1) + c2, c1 + c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(min(min(u, c0 - (y*c1)) - min(u, c2), c3) + c4, min(min(min(u, c0 - (y*c1)) - min(u, c2), c3) + c4, c3 + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(min(min(u + c0, z) - ((w*c1) + u), c2) + c3, min(min(min(u + c0, z) - ((w*c1) + u), c2) + c3, c2 + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(min(min(x - (y*c0), z + c1) - ((w*c2) + z), c3) + c4, min(min(min(x - (y*c0), z + c1) - ((w*c2) + z), c3) + c4, c3 + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(min(min(x*c0, c1) - (y*c0), c1) + c2, min(min(min(x*c0, c1) - (y*c0), c1) + c2, c1 + c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(min(min(x*c0, c1) - (y*c0), c1) + c2, min(min(min(x*c0, c1) - (y*c0), c1) + c2, c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(min(min(x*c0, c1) - (y*c2), c3) + c2, min(min(min(x*c0, c1) - (y*c2), c3) + c2, c2 + c3), (((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (c3 < 0)) ||
 rewrite(min(min(x*c0, c1) - (y*c2), c3) + c4, min(min(min(x*c0, c1) - (y*c2), c3) + c4, c4), ((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite(min(min(x*c0, c1) - (y*c2), c3) + c4, min(min(min(x*c0, c1) - (y*c2), c3) + c4, c3 + c4), ((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite(min(min(x*c0, c1) - ((y + x)*c0), c2) + c3, min(min(min(x*c0, c1) - ((y + x)*c0), c2) + c3, c2 + c3), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(min(select(c0 < x, c1, c0) - (y*c2), c3) + c4, min(min(select(c0 < x, c1, c0) - (y*c2), c3) + c4, c4), ((((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (c3 < 0)) && (0 <= c4)) ||
 rewrite(min(x*c0, c1) + c2, min(min(x*c0, c1) + c2, c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(min(min(x, c0)*c1, (x*c1) + c2) + c3, min(min(min(x, c0)*c1, (x*c1) + c2) + c3, (c0*c1) + c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(((x*c0) - u) + c0, z) + (u - (max(x, c1)*c0)), min(min(((x*c0) - u) + c0, z) + (u - (max(x, c1)*c0)), c0), 0 <= min(c0, c1)))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite(max(x, c0) + c1, max(max(x, c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(max(max(x + y, z), w), c0) + c0, w) + c1, max(max(max(max(max(x + y, z), w), c0) + c0, w) + c1, c1), (0 <= c0) && (c1 < 0)) ||
 rewrite(max(c0 - (u*c1), (max(u, c2)*c1) + c1) + c3, max(max(c0 - (u*c1), (max(u, c2)*c1) + c1) + c3, c0), (((0 <= c1) && (c0 < 0)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(max(x - y, c0) + c1, max(max(x - y, c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(x - max(min(x, c0), c1), c2) + c0, max(max(x - max(min(x, c0), c1), c2) + c0, c0), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(max(u, v) - min(u, v), c0) + c1, max(max(max(u, v) - min(u, v), c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(u, w) - min(u, w), c0) + c1, max(max(max(u, w) - min(u, w), c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(u, z) - min(u, w), c0) + c1, max(max(max(u, z) - min(u, w), c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(u, z) - min(u, z), c0) + c1, max(max(max(u, z) - min(u, z), c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(v, u) - min(v, u), c0) + c1, max(max(max(v, u) - min(v, u), c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(max(x % y, z) - min(x % y, z), c0) + c1, max(max(max(x % y, z) - min(x % y, z), c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(u % c0, (u + z) % c0) + c1, min(max(max(c0, c1), c0*c1), max(u % c0, (u + z) % c0) + c1), 0 <= min(c0, c1)) ||
 rewrite(max(u % c0, (u + z) % c0) + c1, max(max(u % c0, (u + z) % c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(x % c0, (x + y) % c0) + c1, min(max(max(c0, c1), c0*c1), max(x % c0, (x + y) % c0) + c1), 0 <= min(c0, c1)) ||
 rewrite(max(x % c0, (x + y) % c0) + c1, max(max(x % c0, (x + y) % c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(x % c0, (y + x) % c0) + c1, min(max(max(c0, c1), c0*c1), max(x % c0, (y + x) % c0) + c1), 0 <= min(c0, c1)) ||
 rewrite(max(x % c0, (y + x) % c0) + c1, max(max(x % c0, (y + x) % c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max((u + y) % c0, ((u + z) + y) % c0) + c1, min(max(max(c0, c1), max(c0, c1)*c1), max((u + y) % c0, ((u + z) + y) % c0) + c1), 0 <= min(c0, c1)) ||
 rewrite(max((u + y) % c0, ((u + z) + y) % c0) + c1, max(max((u + y) % c0, ((u + z) + y) % c0) + c1, c1), 0 <= min(c0, c1)) ||
 rewrite(max(x*c0, c1) + c2, max(max(x*c0, c1) + c2, c2), (0 <= min(c0, c1)) && (c2 < 0)) ||
 rewrite(max(((x*c0) + y)*c1, c2) + c3, max(max(((x*c0) + y)*c1, c2) + c3, c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite(max(min(x - ((y*c0) + z), c1)*c2, c3) + c4, min(max(min(x - ((y*c0) + z), c1)*c2, c3) + c4, (c1 + c4) + (c2 + c3)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(max(min(x - ((y*c0) + z), c1)*c2, c3) + c4, min(max(min(x - ((y*c0) + z), c1)*c2, c3) + c4, max(c1*2, c3) + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(max(min(x - ((y*c0) + z), c1)*c2, c3) + c4, min(max(min(x - ((y*c0) + z), c1)*c2, c3) + c4, max(c1*c2, c3) + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(max(min(x - ((y*c0) + z), c1)*c2, c3) + c4, max(max(min(x - ((y*c0) + z), c1)*c2, c3) + c4, c3 + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(max(max(x, c0)*c1, c2) + c3, max(max(max(x, c0)*c1, c2) + c3, c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite(max(min(c0 - (min(x, c1)*c2), c3), c3) + c2, min(max(min(c0 - (min(x, c1)*c2), c3), c3) + c2, c2 + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(max(min(c0 - (min(x, c1)*c2), c3), c3) + c2, max(max(min(c0 - (min(x, c1)*c2), c3), c3) + c2, c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(max(min(x - y, c0), c1) + c2, min(max(min(x - y, c0), c1) + c2, c0 + c2), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(min(x - y, c0), c1) + c2, max(max(min(x - y, c0), c1) + c2, c1), ((0 <= c0) && (c1 < 0)) && (0 <= c2)) ||
 rewrite(max(min(x - ((y*c0) + z), c1), c2) + c3, min(max(min(x - ((y*c0) + z), c1), c2) + c3, max(c1, c2) + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(max(min(x - ((y*c0) + z), c1), c2) + c3, max(max(min(x - ((y*c0) + z), c1), c2) + c3, c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
 rewrite(max(min(x - ((y*c0) + z), c1), c2) + c3, min(max(min(x - ((y*c0) + z), c1), c2) + c3, c3), (((0 <= c0) && (c1 < 0)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(max(min(x - ((y*c0) + z), c1), c2) + c3, max(max(min(x - ((y*c0) + z), c1), c2) + c3, c2), (((0 <= c0) && (c1 < 0)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(max(min(x - ((y*c0) + ((z*c1) + w)), c2), c3) + c4, min(max(min(x - ((y*c0) + ((z*c1) + w)), c2), c3) + c4, max(c2, c3) + c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(max(min(x - ((y*c0) + ((z*c1) + w)), c2), c3) + c4, max(max(min(x - ((y*c0) + ((z*c1) + w)), c2), c3) + c4, c4), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(max(min(x - (((y*c0) + z)*c1), c2), c3) + c0, min(max(min(x - (((y*c0) + z)*c1), c2), c3) + c0, c0 + c2), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite(max(min(x - (((y*c0) + z)*c1), c2), c3) + c0, max(max(min(x - (((y*c0) + z)*c1), c2), c3) + c0, c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite(max(min((u + y) - (min(u, c0) + z), c1), c2) + c3, max(max(min((u + y) - (min(u, c0) + z), c1), c2) + c3, c2), ((0 <= min(c0, c1)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(max(min(x*c0, c1), c2) + c3, min(max(min(x*c0, c1), c2) + c3, max(c1, c2) + c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite(max(min(x*c0, c1), c2) + c3, max(max(min(x*c0, c1), c2) + c3, c3), (0 <= min(min(c0, c1), c2)) && (c3 < 0)) ||
 rewrite(max(min(x*c0, c1), c2) + c3, min(max(min(x*c0, c1), c2) + c3, c3), (((0 <= c0) && (c1 < 0)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(max(min(x*c0, c1), c2) + c3, max(max(min(x*c0, c1), c2) + c3, c2), (((0 <= c0) && (c1 < 0)) && (c2 < 0)) && (0 <= c3)) ||
 rewrite(max(max(c0 - u, u + c1), c2) + c3, max(max(max(c0 - u, u + c1), c2) + c3, c1), (((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (0 <= c3)) ||
 rewrite(max(max(c0 - u, u - c0), c1) + c2, max(max(max(c0 - u, u - c0), c1) + c2, c1), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(max(c0 - x, x + c1), c2) + c3, max(max(max(c0 - x, x + c1), c2) + c3, c1), (((0 <= c0) && (c1 < 0)) && (0 <= c2)) && (0 <= c3)) ||
 rewrite(max(max(c0 - x, x - c0), c1) + c2, max(max(max(c0 - x, x - c0), c1) + c2, c1), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(max(c0 - x, x - c1), c2) + c0, max(max(max(c0 - x, x - c1), c2) + c0, c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(max(max(x - y, (y - x) + c0), c1) + c2, max(max(max(x - y, (y - x) + c0), c1) + c2, c2), ((0 <= c1) && (c0 < 0)) && (0 <= c2)) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(max(c0 - x, x + c1), c2) + min(x, c0), max(max(max(c0 - x, x + c1), c2) + min(x, c0), c0), ((0 <= c0) && (c1 < 0)) && (0 <= c2)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite(select(c0 < x, c1 - max(x*c2, c1), c0) + c2, min(select(c0 < x, c1 - max(x*c2, c1), c0) + c2, c0 + c2), 0 <= min(min(c0, c1), c2)) ||
 rewrite(select(c0 < x, c1 - max(x*c2, c1), c0) + c3, min(select(c0 < x, c1 - max(x*c2, c1), c0) + c3, c0 + c3), 0 <= min(min(min(c0, c1), c2), c3)) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(select(c0 < x, c1, c2) + (min((c3 - (y*c4)) - x, c0)*c1), min(select(c0 < x, c1, c2) + (min((c3 - (y*c4)) - x, c0)*c1), max(c1, c2) + (c0*c1)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(select(c0 < x, c1, c2) + (min((c3 - (y*c4)) - x, c0)*c2), min(select(c0 < x, c1, c2) + (min((c3 - (y*c4)) - x, c0)*c2), max(c1, c2) + (c0*c2)), 0 <= min(min(min(min(c0, c1), c2), c3), c4)) ||
 rewrite(select(c0 < x, c1, c2) + (max(x, c3)*c4), min(select(c0 < x, c1, c2) + (max(x, c3)*c4), max(c1, c2) + (c3*c4)), (0 <= min(min(min(c0, c1), c2), c3)) && (c4 < 0))))))
