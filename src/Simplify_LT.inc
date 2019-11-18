((a.node_type() == IRNodeType::IntImm) && (
((b.node_type() == IRNodeType::Mul) && (
 rewrite((0 < (x*x)), (x != 0)))))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((x < (y + min(x, 0))), (max(0, x) < y)) ||
 rewrite((x < (y + max(x, 0))), (min(0, x) < y)) ||
 rewrite((x < (min(x, 0) + y)), (max(x, 0) < y)) ||
 rewrite((x < (min(z, (x - y)) + min(x, y))), false) ||
 rewrite((x < (min(z, (x - y)) + min(y, x))), false) ||
 rewrite((x < (min((x - y), z) + min(x, y))), false) ||
 rewrite((x < (min((x - y), z) + min(y, x))), false) ||
 rewrite((x < (min(min((x + c0), y), z) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((x < (max(x, 0) + y)), (min(x, 0) < y)) ||
 rewrite((x < (max(min((x + c0), y), z) + c1)), ((x + fold(max((1 - c1), (c0 + 1)))) <= z), ((c0 + c1) <= 0)) ||
 rewrite((x < (select(y, x, z) + 1)), (x <= select(y, x, z))) ||
 rewrite((y < (x + min(y, 1))), (max(1, y) <= x)) ||
 rewrite((y < (z + (x + y))), (0 < (z + x))) ||
 rewrite((y < (z + (y + x))), (0 < (z + x))) ||
 rewrite((y < ((x + y) + z)), (0 < (x + z))) ||
 rewrite((y < ((y + x) + z)), (0 < (x + z))) ||
 rewrite((y < (min(y, 1) + x)), (max(y, 1) <= x)) ||
 rewrite((y < (min((min(x, c0) + y), z) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((y < (min(min(x, (y + c0)), z) + c1)), false, ((c0 + c1) <= 0)) ||
 rewrite((y < (max(x, min(y, c0)) + c1)), ((y + fold((1 - min(c1, 0)))) <= x), (c1 <= 0)) ||
 rewrite((y < (max(x, min(y, z)) + c0)), (y < (x + c0)), (c0 <= 0)) ||
 rewrite((y < (max(min(x, y), z) + c0)), ((y + fold((1 - min(c0, 0)))) <= z), (c0 <= 0)) ||
 rewrite((y < (max(min(x, (y + c0)), z) + c1)), ((fold(max((1 - c1), (c0 + 1))) + y) <= z), ((c0 + c1) <= 0)) ||
 rewrite((z < (min(y, x) + min(w, (z - x)))), false) ||
 rewrite((z < (min(y, x) + min(w, (z - y)))), false) ||
 rewrite((z < (min(y, x) + min((z - x), w))), false) ||
 rewrite((z < (min(y, x) + min((z - y), w))), false) ||
 rewrite((z < (min(min(x, (y + c0)), c1) + c2)), (z < min(min(y, (x + c2)), c0)), (((c0 + c2) == 0) && ((c1 + c2) == c0))) ||
 rewrite((z < (max(x, (min(y, z) + c0)) + c1)), (z < (x + c1)), ((c0 + c1) <= 0)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((c0 < (x*x)), true, (c0 <= -1)) ||
 rewrite((x < (x*c0)), (1 <= x), (2 <= c0)) ||
 rewrite((x < (x*c0)), (x <= -1), (c0 <= -1)) ||
 rewrite((x < (x*(y + 1))), (1 <= (x*y))) ||
 rewrite((x < (x*(1 - y))), ((x*y) <= -1)) ||
 rewrite((x < (x*(c0 - x))), false, ((0 <= c0) && (c0 <= 2))) ||
 rewrite((x < ((y + 1)*x)), (1 <= (y*x))) ||
 rewrite((x < ((1 - y)*x)), ((y*x) <= -1)) ||
 rewrite((x < ((c0 - x)*x)), false, ((0 <= c0) && (c0 <= 2))) ||
 rewrite((x < ((x*x)*c0)), (x != 0), (2 <= c0)))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((x < (x/c0)), (x < 0), (c0 <= -1)) ||
 rewrite((x < ((min((x*c0), y) + c1)/c0)), false, ((max(c1, 0) + 1) <= c0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((c0 < min(y, ((max(x, c1)*c2) + c2))), (c0 < y), (((((1 <= c1) || (c0 <= 1)) || (c1 <= -1)) || ((c0 + 1) <= (c2*2))) && ((((c0 <= 0) || ((c0 + 1) <= c2)) || (((1 <= c1) || (c1 <= -1)) && (0 <= c1))) && ((((1 <= c2) && ((c0 + 1) <= ((c1 + 1)*c2))) && ((c0 + 1) <= ((c1 + 2)*c2))) && ((c0 + 1) <= ((c1 + 3)*c2)))))) ||
 rewrite((c0 < min(((max(x, c1)*c2) + c2), y)), (c0 < y), (((((1 <= c1) || (c0 <= 1)) || (c1 <= -1)) || ((c0 + 1) <= (c2*2))) && ((((c0 <= 0) || ((c0 + 1) <= c2)) || (((1 <= c1) || (c1 <= -1)) && (0 <= c1))) && ((((1 <= c2) && ((c0 + 1) <= ((c1 + 1)*c2))) && ((c0 + 1) <= ((c1 + 2)*c2))) && ((c0 + 1) <= ((c1 + 3)*c2)))))) ||
 rewrite((x < min(z, max(x, y))), (x < min(z, y))) ||
 rewrite((x < min(z, max(y, x))), (x < min(z, y))) ||
 rewrite((x < min(z, select(y, x, z))), (select(y, z, x) < z)) ||
 rewrite((x < min(z, select(y, z, x))), (x < select(y, z, x))) ||
 rewrite((x < min(max(x, y), z)), (x < min(y, z))) ||
 rewrite((x < min(max(y, x), z)), (x < min(y, z))) ||
 rewrite((x < min(select(y, x, z), z)), (select(y, z, x) < z)) ||
 rewrite((x < min(select(y, z, x), z)), (x < select(y, z, x))) ||
 rewrite((y < min(z, min(x, y))), false) ||
 rewrite((y < min(z, min(y, x))), false) ||
 rewrite((y < min(min(x, y), z)), false) ||
 rewrite((y < min(min(y, x), z)), false) ||
 rewrite((z < min(x, ((min(y, c0) + z) + c1))), false, ((c0 + c1) <= 0)) ||
 rewrite((z < min(((min(y, c0) + z) + c1), x)), false, ((c0 + c1) <= 0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((c1 < max(min(x, c0), (y + c0))), (fold((c1 - c0)) < y), (c0 <= c1)) ||
 rewrite((c2 < max(x, (min(y, c0) + c1))), (fold((c2 + 1)) <= x), ((c0 + c1) <= c2)) ||
 rewrite((c2 < max((min(y, c0) + c1), x)), (fold((c2 + 1)) <= x), ((c0 + c1) <= c2)) ||
 rewrite((c3 < max(x, ((min(y, c0)*c1) + c2))), (fold((c3 + 1)) <= x), ((((1 <= c0) || (c0 <= -1)) || (c2 <= c3)) && ((1 <= c1) && (((c0*c1) + c2) <= ((min(c1, 0)*2) + c3))))) ||
 rewrite((c3 < max(((min(y, c0)*c1) + c2), x)), (fold((c3 + 1)) <= x), ((((1 <= c0) || (c0 <= -1)) || (c2 <= c3)) && ((1 <= c1) && (((c0*c1) + c2) <= ((min(c1, 0)*2) + c3))))) ||
 rewrite((x < max(z, max(x, y))), (x < max(z, y))) ||
 rewrite((x < max(z, max(y, x))), (x < max(z, y))) ||
 rewrite((x < max(z, select(y, x, z))), (x < z)) ||
 rewrite((x < max(z, select(y, z, x))), (x < z)) ||
 rewrite((x < max(max(x, y), z)), (x < max(y, z))) ||
 rewrite((x < max(max(y, x), z)), (x < max(y, z))) ||
 rewrite((x < max(select(y, x, z), z)), (x < z)) ||
 rewrite((x < max(select(y, z, x), z)), (x < z)) ||
 rewrite((y < max(w, max(z, min(x, y)))), (y < max(w, z))) ||
 rewrite((y < max(w, max(z, min(y, x)))), (y < max(w, z))) ||
 rewrite((y < max(w, max(min(x, y), z))), (y < max(w, z))) ||
 rewrite((y < max(w, max(min(y, x), z))), (y < max(w, z))) ||
 rewrite((y < max(z, min(x, y))), (y < z)) ||
 rewrite((y < max(z, min(y, x))), (y < z)) ||
 rewrite((y < max(min(x, y), z)), (y < z)) ||
 rewrite((y < max(min(y, x), z)), (y < z)) ||
 rewrite((y < max(max(z, min(x, y)), w)), (y < max(z, w))) ||
 rewrite((y < max(max(z, min(y, x)), w)), (y < max(z, w))) ||
 rewrite((y < max(max(min(x, y), z), w)), (y < max(z, w))) ||
 rewrite((y < max(max(min(y, x), z), w)), (y < max(z, w))) ||
 rewrite((z < max(x, (min(y, (z + c0)) + c1))), (z < x), ((c0 + c1) <= 0)) ||
 rewrite((z < max(x, min(y, (z + c0)))), (z < x), (c0 <= 0)) ||
 rewrite((z < max(x, min((z + c0), y))), (z < x), (c0 <= 0)) ||
 rewrite((z < max((min(y, (z + c0)) + c1), x)), (z < x), ((c0 + c1) <= 0)) ||
 rewrite((z < max(min(y, (z + c0)), x)), (z < x), (c0 <= 0)) ||
 rewrite((z < max(min((z + c0), y), x)), (z < x), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((x < select(y, x, (x - z))), (select(y, 0, z) <= -1)) ||
 rewrite((x < select(y, x, (x*c0))), (1 <= select(y, 0, x)), (2 <= c0)) ||
 rewrite((x < select(y, z, (x + z))), (select(y, x, 0) < z)) ||
 rewrite((x < select(y, z, (z + x))), (select(y, x, 0) < z)) ||
 rewrite((x < select(y, z, min(x, z))), (x < select(y, z, x))) ||
 rewrite((x < select(y, z, min(z, x))), (x < select(y, z, x))) ||
 rewrite((x < select(y, z, max(w, x))), (x < select(y, z, w))) ||
 rewrite((x < select(y, z, max(x, w))), (x < select(y, z, w))) ||
 rewrite((x < select(y, (x + z), x)), (1 <= select(y, z, 0))) ||
 rewrite((x < select(y, (x + z), z)), (select(y, 0, x) < z)) ||
 rewrite((x < select(y, (z + x), x)), (0 < select(y, z, 0))) ||
 rewrite((x < select(y, (z + x), z)), (select(y, 0, x) < z)) ||
 rewrite((x < select(y, (x - z), x)), (select(y, z, 0) < 0)) ||
 rewrite((x < select(y, (x*c0), x)), (1 <= select(y, x, 0)), (2 <= c0)) ||
 rewrite((x < select(y, min(x, z), x)), false) ||
 rewrite((x < select(y, min(x, z), z)), (select(y, z, x) < z)) ||
 rewrite((x < select(y, min(z, x), x)), false) ||
 rewrite((x < select(y, min(z, x), z)), (x < select(y, x, z))) ||
 rewrite((x < select(y, max(x, z), w)), (x < select(y, z, w))) ||
 rewrite((x < select(y, max(z, x), w)), (x < select(y, z, w))) ||
 rewrite((x < select((x < y), y, z)), (x < max(y, z))) ||
 rewrite((x < select((x < y), z, x)), (x < min(y, z))) ||
 rewrite((x < select((x < y), z, y)), (x < min(z, y))) ||
 rewrite((y < select(x, y, min(y, z))), false) ||
 rewrite((y < select(x, y, min(z, y))), false))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(((y + min(x, 0)) < x), (y < max(x, 0))) ||
 rewrite(((y + max(x, 0)) < x), (y < min(x, 0))) ||
 rewrite(((z + (y + x)) < x), ((z + y) < 0)) ||
 rewrite(((z + (y + x)) < y), ((z + x) < 0)) ||
 rewrite((((y + x) + z) < x), ((y + z) < 0)) ||
 rewrite((((y + x) + z) < y), ((x + z) < 0)) ||
 rewrite(((min(x, 0) + y) < x), (y < max(x, 0))) ||
 rewrite(((max(x, 0) + y) < x), (y < min(x, 0))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((u + z) < (w + (x + (y + z)))), (u < (w + (x + y)))) ||
 rewrite(((u + z) < (w + (x + (z + y)))), (u < (w + (x + y)))) ||
 rewrite(((u + z) < (w + ((y + z) + x))), (u < (w + (y + x)))) ||
 rewrite(((u + z) < (w + ((z + y) + x))), (u < (w + (y + x)))) ||
 rewrite(((u + z) < ((x + (y + z)) + w)), (u < ((x + y) + w))) ||
 rewrite(((u + z) < ((x + (z + y)) + w)), (u < ((x + y) + w))) ||
 rewrite(((u + z) < (((y + z) + x) + w)), (u < ((y + x) + w))) ||
 rewrite(((u + z) < (((z + y) + x) + w)), (u < ((y + x) + w))) ||
 rewrite(((z + u) < (w + (x + (y + z)))), (u < (w + (x + y)))) ||
 rewrite(((z + u) < (w + (x + (z + y)))), (u < (w + (x + y)))) ||
 rewrite(((z + u) < (w + ((y + z) + x))), (u < (w + (y + x)))) ||
 rewrite(((z + u) < (w + ((z + y) + x))), (u < (w + (y + x)))) ||
 rewrite(((z + u) < ((x + (y + z)) + w)), (u < ((x + y) + w))) ||
 rewrite(((z + u) < ((x + (z + y)) + w)), (u < ((x + y) + w))) ||
 rewrite(((z + u) < (((y + z) + x) + w)), (u < ((y + x) + w))) ||
 rewrite(((z + u) < (((z + y) + x) + w)), (u < ((y + x) + w))) ||
 rewrite(((z + min(y, x)) < (x + y)), (z < max(y, x))) ||
 rewrite(((z + min(y, x)) < (y + x)), (z < max(y, x))) ||
 rewrite(((z + select(y, c1, c2)) < (x + c0)), (z < (x + select(y, fold((c0 - c1)), fold((c0 - c2)))))) ||
 rewrite(((min(y, x) + z) < (x + y)), (z < max(x, y))) ||
 rewrite(((min(y, x) + z) < (y + x)), (z < max(y, x))) ||
 rewrite(((select(y, c1, c2) + z) < (x + c0)), ((z + select(y, fold((c1 - c0)), fold((c2 - c0)))) < x)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((w + (y*z)) < (z*(x + y))), (w < (z*x))) ||
 rewrite(((w + (y*z)) < (z*(y + x))), (w < (z*x))) ||
 rewrite(((w + (y*z)) < ((x + y)*z)), (w < (x*z))) ||
 rewrite(((w + (y*z)) < ((y + x)*z)), (w < (x*z))) ||
 rewrite(((w + (z*y)) < (z*(x + y))), (w < (z*x))) ||
 rewrite(((w + (z*y)) < (z*(y + x))), (w < (z*x))) ||
 rewrite(((w + (z*y)) < ((x + y)*z)), (w < (x*z))) ||
 rewrite(((w + (z*y)) < ((y + x)*z)), (w < (x*z))) ||
 rewrite(((y + x) < (x*2)), (y < x)) ||
 rewrite(((y + x) < (y*2)), (x < y)) ||
 rewrite((((y*z) + w) < (z*(x + y))), (w < (z*x))) ||
 rewrite((((y*z) + w) < (z*(y + x))), (w < (z*x))) ||
 rewrite((((y*z) + w) < ((x + y)*z)), (w < (x*z))) ||
 rewrite((((y*z) + w) < ((y + x)*z)), (w < (x*z))) ||
 rewrite((((z*y) + w) < (z*(x + y))), (w < (z*x))) ||
 rewrite((((z*y) + w) < (z*(y + x))), (w < (z*x))) ||
 rewrite((((z*y) + w) < ((x + y)*z)), (w < (x*z))) ||
 rewrite((((z*y) + w) < ((y + x)*z)), (w < (x*z))) ||
 rewrite(((min((x*c0), c1) + (y*c0)) < (x*c0)), (y <= max(-1, x)), (((0 <= (c0 + c1)) && (1 <= c0)) && (c1 <= -1))) ||
 rewrite(((select(y, 2, c1) + (z*2)) < (x*2)), ((2 + z) <= x), ((2 <= c1) && (c1 <= 3))) ||
 rewrite(((select(y, c0, c1) + (z*c0)) < (x*c0)), (z <= (select(y, -2, 0) + x)), (((0 <= (min((c0*2), c0) + c1)) && (1 <= c0)) && (c1 <= -1))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((y + x) < min(x, y)), (max(y, x) <= -1)) ||
 rewrite(((y + x) < min(y, x)), (max(y, x) <= -1)) ||
 rewrite(((y + select(z, c0, c1)) < min(x, y)), false, (0 <= min(c0, c1))) ||
 rewrite(((y + select(z, c0, c1)) < min(y, x)), false, (0 <= min(c0, c1))) ||
 rewrite(((y + select(z, c1, c2)) < min(x, (y + c0))), false, (c0 <= min(c1, c2))) ||
 rewrite(((y + select(z, c1, c2)) < min((y + c0), x)), false, (c0 <= min(c1, c2))) ||
 rewrite(((z + w) < min(x, (w + min(y, z)))), false) ||
 rewrite(((z + w) < min(x, (w + min(z, y)))), false) ||
 rewrite(((z + w) < min(x, (min(y, z) + w))), false) ||
 rewrite(((z + w) < min(x, (min(z, y) + w))), false) ||
 rewrite(((z + w) < min((w + min(y, z)), x)), false) ||
 rewrite(((z + w) < min((w + min(z, y)), x)), false) ||
 rewrite(((z + w) < min((min(y, z) + w), x)), false) ||
 rewrite(((z + w) < min((min(z, y) + w), x)), false) ||
 rewrite(((z + y) < min(w, (z + min(x, y)))), false) ||
 rewrite(((z + y) < min(w, (z + min(y, x)))), false) ||
 rewrite(((z + y) < min(w, (min(x, y) + z))), false) ||
 rewrite(((z + y) < min(w, (min(y, x) + z))), false) ||
 rewrite(((z + y) < min((z + min(x, y)), w)), false) ||
 rewrite(((z + y) < min((z + min(y, x)), w)), false) ||
 rewrite(((z + y) < min((min(x, y) + z), w)), false) ||
 rewrite(((z + y) < min((min(y, x) + z), w)), false) ||
 rewrite(((select(z, c0, c1) + y) < min(x, y)), false, (0 <= min(c0, c1))) ||
 rewrite(((select(z, c0, c1) + y) < min(y, x)), false, (0 <= min(c0, c1))) ||
 rewrite(((select(z, c1, c2) + y) < min(x, (y + c0))), false, (c0 <= min(c1, c2))) ||
 rewrite(((select(z, c1, c2) + y) < min((y + c0), x)), false, (c0 <= min(c1, c2))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(((y + x) < max(x, y)), (min(y, x) <= -1)) ||
 rewrite(((y + x) < max(y, x)), (min(y, x) <= -1)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(((x + y) < select(z, y, x)), (select(z, x, y) < 0)) ||
 rewrite(((y + x) < select(z, y, x)), (select(z, x, y) <= -1)) ||
 rewrite(((z + select(w, c0, c1)) < select(x, min(y, z), z)), false, (0 <= min(c0, c1))) ||
 rewrite(((z + select(w, c0, c1)) < select(x, min(z, y), z)), false, (0 <= min(c0, c1))) ||
 rewrite(((select(w, c0, c1) + z) < select(x, min(y, z), z)), false, (0 <= min(c0, c1))) ||
 rewrite(((select(w, c0, c1) + z) < select(x, min(z, y), z)), false, (0 <= min(c0, c1))))))) ||
((a.node_type() == IRNodeType::Mul) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite(((x*x) < 1), (x == 0)))) ||
 rewrite(((x*c0) < x), (x <= -1), (2 <= c0)) ||
 rewrite(((x*c0) < x), (1 <= x), (c0 <= -1)) ||
 rewrite(((x*x) < c0), false, (c0 <= 0)) ||
 rewrite(((x*x) < x), false) ||
 rewrite(((x*(x + -1)) < x), (x == 1)) ||
 rewrite(((x*(x + 0)) < c0), false, (c0 <= 0)) ||
 rewrite(((x*(x + 0)) < c1), false, (c1 <= 0)) ||
 rewrite(((x*(x + c0)) < x), false, ((0 <= c0) && (c0 <= 2))) ||
 rewrite(((y*(x + 1)) < y), ((y*x) <= -1)) ||
 rewrite(((y*(1 - x)) < y), (1 <= (y*x))) ||
 rewrite((((x + -1)*x) < x), (x == 1)) ||
 rewrite((((x + 0)*x) < c0), false, (c0 <= 0)) ||
 rewrite((((x + 0)*x) < c1), false, (c1 <= 0)) ||
 rewrite((((x + 1)*y) < y), ((x*y) <= -1)) ||
 rewrite((((x + c0)*x) < x), false, ((0 <= c0) && (c0 <= 2))) ||
 rewrite((((1 - x)*y) < y), (1 <= (x*y))) ||
 rewrite((((x*x)*c0) < x), false, (1 <= c0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((w*min(z, y)) < (u + (x + (w*min(y, z))))), (0 < (u + x))) ||
 rewrite(((w*min(z, y)) < (u + (x + (min(y, z)*w)))), (0 < (u + x))) ||
 rewrite(((w*min(z, y)) < (u + (x + (min(z, y)*w)))), (0 < (u + x))) ||
 rewrite(((w*min(z, y)) < (u + ((w*min(y, z)) + x))), (0 < (u + x))) ||
 rewrite(((w*min(z, y)) < (u + ((min(y, z)*w) + x))), (0 < (u + x))) ||
 rewrite(((w*min(z, y)) < (u + ((min(z, y)*w) + x))), (0 < (u + x))) ||
 rewrite(((w*min(z, y)) < ((x + (w*min(y, z))) + u)), (0 < (x + u))) ||
 rewrite(((w*min(z, y)) < ((x + (min(y, z)*w)) + u)), (0 < (x + u))) ||
 rewrite(((w*min(z, y)) < ((x + (min(z, y)*w)) + u)), (0 < (x + u))) ||
 rewrite(((w*min(z, y)) < (((w*min(y, z)) + x) + u)), (0 < (x + u))) ||
 rewrite(((w*min(z, y)) < (((min(y, z)*w) + x) + u)), (0 < (x + u))) ||
 rewrite(((w*min(z, y)) < (((min(z, y)*w) + x) + u)), (0 < (x + u))) ||
 rewrite(((x*2) < (x + y)), (x < y)) ||
 rewrite(((x*2) < (y + x)), (x < y)) ||
 rewrite(((y*z) < (w + (min(x, y) + (z*y)))), (0 < (w + min(x, y)))) ||
 rewrite(((y*z) < (w + (min(y, x) + (z*y)))), (0 < (w + min(y, x)))) ||
 rewrite(((y*z) < (z + (x + (z*y)))), (0 < (z + x))) ||
 rewrite(((y*z) < (z + ((z*y) + x))), (0 < (z + x))) ||
 rewrite(((y*z) < ((x + (z*y)) + z)), (0 < (x + z))) ||
 rewrite(((y*z) < (((z*y) + x) + z)), (0 < (z + x))) ||
 rewrite(((y*z) < ((min(x, y) + (z*y)) + w)), (0 < (min(x, y) + w))) ||
 rewrite(((y*z) < ((min(y, x) + (z*y)) + w)), (0 < (min(y, x) + w))) ||
 rewrite(((z*y) < (w + (min(x, y) + (y*z)))), (0 < (w + min(x, y)))) ||
 rewrite(((z*y) < (w + (min(y, x) + (y*z)))), (0 < (w + min(y, x)))) ||
 rewrite(((z*y) < (z + (x + (y*z)))), (0 < (z + x))) ||
 rewrite(((z*y) < (z + ((y*z) + x))), (0 < (z + x))) ||
 rewrite(((z*y) < ((x + (y*z)) + z)), (0 < (x + z))) ||
 rewrite(((z*y) < (((y*z) + x) + z)), (0 < (z + x))) ||
 rewrite(((z*y) < ((min(x, y) + (y*z)) + w)), (0 < (min(x, y) + w))) ||
 rewrite(((z*y) < ((min(y, x) + (y*z)) + w)), (0 < (min(y, x) + w))) ||
 rewrite(((z*(w + y)) < (x + (y*z))), ((z*w) < x)) ||
 rewrite(((z*(w + y)) < (x + (z*y))), ((z*w) < x)) ||
 rewrite(((z*(w + y)) < ((y*z) + x)), ((z*w) < x)) ||
 rewrite(((z*(w + y)) < ((z*y) + x)), ((z*w) < x)) ||
 rewrite(((z*(y + w)) < (x + (y*z))), ((z*w) < x)) ||
 rewrite(((z*(y + w)) < (x + (z*y))), ((z*w) < x)) ||
 rewrite(((z*(y + w)) < ((y*z) + x)), ((z*w) < x)) ||
 rewrite(((z*(y + w)) < ((z*y) + x)), ((z*w) < x)) ||
 rewrite(((z*(y/c0)) < (w + (x + ((y/c0)*z)))), (0 < (w + x))) ||
 rewrite(((z*(y/c0)) < (w + (((y/c0)*z) + x))), (0 < (w + x))) ||
 rewrite(((z*(y/c0)) < ((x + ((y/c0)*z)) + w)), (0 < (x + w))) ||
 rewrite(((z*(y/c0)) < ((((y/c0)*z) + x) + w)), (0 < (x + w))) ||
 rewrite((((w + y)*z) < (x + (y*z))), ((w*z) < x)) ||
 rewrite((((w + y)*z) < (x + (z*y))), ((w*z) < x)) ||
 rewrite((((w + y)*z) < ((y*z) + x)), ((w*z) < x)) ||
 rewrite((((w + y)*z) < ((z*y) + x)), ((w*z) < x)) ||
 rewrite((((y + w)*z) < (x + (y*z))), ((w*z) < x)) ||
 rewrite((((y + w)*z) < (x + (z*y))), ((w*z) < x)) ||
 rewrite((((y + w)*z) < ((y*z) + x)), ((w*z) < x)) ||
 rewrite((((y + w)*z) < ((z*y) + x)), ((w*z) < x)) ||
 rewrite((((y/c0)*z) < (w + (x + (z*(y/c0))))), (0 < (w + x))) ||
 rewrite((((y/c0)*z) < (w + ((z*(y/c0)) + x))), (0 < (w + x))) ||
 rewrite((((y/c0)*z) < ((x + (z*(y/c0))) + w)), (0 < (x + w))) ||
 rewrite((((y/c0)*z) < (((z*(y/c0)) + x) + w)), (0 < (x + w))) ||
 rewrite(((min(z, y)*w) < (u + (x + (w*min(y, z))))), (0 < (u + x))) ||
 rewrite(((min(z, y)*w) < (u + (x + (w*min(z, y))))), (0 < (u + x))) ||
 rewrite(((min(z, y)*w) < (u + (x + (min(y, z)*w)))), (0 < (u + x))) ||
 rewrite(((min(z, y)*w) < (u + ((w*min(y, z)) + x))), (0 < (u + x))) ||
 rewrite(((min(z, y)*w) < (u + ((w*min(z, y)) + x))), (0 < (u + x))) ||
 rewrite(((min(z, y)*w) < (u + ((min(y, z)*w) + x))), (0 < (u + x))) ||
 rewrite(((min(z, y)*w) < ((x + (w*min(y, z))) + u)), (0 < (x + u))) ||
 rewrite(((min(z, y)*w) < ((x + (w*min(z, y))) + u)), (0 < (x + u))) ||
 rewrite(((min(z, y)*w) < ((x + (min(y, z)*w)) + u)), (0 < (x + u))) ||
 rewrite(((min(z, y)*w) < (((w*min(y, z)) + x) + u)), (0 < (x + u))) ||
 rewrite(((min(z, y)*w) < (((w*min(z, y)) + x) + u)), (0 < (x + u))) ||
 rewrite(((min(z, y)*w) < (((min(y, z)*w) + x) + u)), (0 < (x + u))))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((x*x) < (x*c0)), (x == fold((max(max((min(c0, 0) + 2), c0), 0) + -1))), (((2 <= c0) || (c0 <= -1)) && (((1 <= c0) || (c0 <= -2)) && ((-2 <= c0) && (c0 <= 2))))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((x*c0) < min(z, (min(x, y)*c0))), false, (1 <= c0)) ||
 rewrite(((x*c0) < min((min(x, y)*c0), z)), false, (1 <= c0)) ||
 rewrite(((x*x) < min(x, y)), false) ||
 rewrite(((x*x) < min(y, x)), false) ||
 rewrite(((z*c0) < min(x, (min(y, z)*c0))), false, (1 <= c0)) ||
 rewrite(((z*c0) < min((min(y, z)*c0), x)), false, (1 <= c0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(((x*x) < max(x, y)), ((x*x) < y)) ||
 rewrite(((x*x) < max(y, x)), ((x*x) < y)))))) ||
((a.node_type() == IRNodeType::Div) && (
 rewrite(((x/c0) < x), (1 <= x), (c0 <= -1)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((((y + c0)/c1) < (x + c0)), ((y + c1) <= (x*c1)), (((1 <= c1) && ((c0 + 1) <= ((c0 + 1)*c1))) && (((c0 + 1) <= ((c0 + 1)*c1)) && ((((c0 + 1)*c1) + -1) <= c0)))) ||
 rewrite((((y + c1)/c0) < (x + c0)), (y <= (x*c0)), (((1 <= c0) && ((c1 + 1) <= (c0*c0))) && (((c0*c0) + -1) <= c1))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((((c2 - x)/c1) < ((c0 - x)/c1)), false, (((1 <= c1) || (c2 <= c0)) && ((c0 <= c2) || (c1 <= -1)))))))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite((min(y, (x*x)) < x), (y < x)) ||
 rewrite((min(y, select(x, y, z)) < z), (y < z)) ||
 rewrite((min(z, min(y, x)) < x), (min(z, y) < x)) ||
 rewrite((min(z, min(y, x)) < y), (min(z, x) < y)) ||
 rewrite((min(z, max(y, x)) < x), (z < x)) ||
 rewrite((min(z, max(y, x)) < y), (z < y)) ||
 rewrite((min(z, select(x, y, z)) < y), (z < y)) ||
 rewrite((min((x*x), y) < x), (y < x)) ||
 rewrite((min(min(y, x), z) < x), (min(y, z) < x)) ||
 rewrite((min(min(y, x), z) < y), (min(x, z) < y)) ||
 rewrite((min(max(y, x), z) < x), (z < x)) ||
 rewrite((min(max(y, x), z) < y), (z < y)) ||
 rewrite((min(select(x, y, z), y) < z), (y < z)) ||
 rewrite((min(select(x, y, z), z) < y), (z < y)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(y, c2) < (min(x, c0) + 1)), (y <= min(x, c0)), ((c0 + 1) <= c2)) ||
 rewrite((min(y, c2) < (min(x, c0) + c1)), (y < (min(x, c0) + c1)), ((c0 + c1) <= c2)) ||
 rewrite((min(y, x) < (x + y)), (1 <= max(x, y))) ||
 rewrite((min(y, x) < (y + x)), (1 <= max(y, x))) ||
 rewrite((min(y, z) < (min(x, y) + 1)), (min(y, z) <= x)) ||
 rewrite((min(y, (z + c0)) < (min(x, y) + c0)), (z < min(x, y)), (c0 <= 0)) ||
 rewrite((min(y, min(x, z)) < (x + c0)), (min(y, z) < (x + c0)), (c0 <= 0)) ||
 rewrite((min(y, min(z, x)) < (x + c0)), (min(y, z) < (x + c0)), (c0 <= 0)) ||
 rewrite((min(z, y) < (w + (x + min(y, z)))), (0 < (w + x))) ||
 rewrite((min(z, y) < (w + (min(y, z) + x))), (0 < (w + x))) ||
 rewrite((min(z, y) < ((x + min(y, z)) + w)), (0 < (x + w))) ||
 rewrite((min(z, y) < ((min(y, z) + x) + w)), (0 < (x + w))) ||
 rewrite((min(z, y) < (min(x, y) + 1)), (min(z, y) <= x)) ||
 rewrite((min(z, (x + 1)) < (min(x, y) + 1)), (z <= min(x, y))) ||
 rewrite((min(z, (y + c1)) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 <= min(c1, 1)))) ||
 rewrite((min(z, (y + x)) < (x + y)), (z < (x + y))) ||
 rewrite((min(z, (y + x)) < (y + x)), (z < (y + x))) ||
 rewrite((min(z, (y*x)) < (u + (w + min(z, (x*y))))), (0 < (u + w))) ||
 rewrite((min(z, (y*x)) < (u + (w + min((x*y), z)))), (0 < (u + w))) ||
 rewrite((min(z, (y*x)) < (u + (min(z, (x*y)) + w))), (0 < (u + w))) ||
 rewrite((min(z, (y*x)) < (u + (min((x*y), z) + w))), (0 < (u + w))) ||
 rewrite((min(z, (y*x)) < ((w + min(z, (x*y))) + u)), (0 < (w + u))) ||
 rewrite((min(z, (y*x)) < ((w + min((x*y), z)) + u)), (0 < (w + u))) ||
 rewrite((min(z, (y*x)) < ((min(z, (x*y)) + w) + u)), (0 < (w + u))) ||
 rewrite((min(z, (y*x)) < ((min((x*y), z) + w) + u)), (0 < (w + u))) ||
 rewrite((min(z, select(y, x, (x + c1))) < (x + c0)), (z < (x + c0)), (c0 <= min(c1, 0))) ||
 rewrite((min(z, select(y, (x + c1), x)) < (x + c0)), (z < (x + c0)), (c0 <= min(c1, 0))) ||
 rewrite((min((x + 1), z) < (min(x, y) + 1)), (z <= min(x, y))) ||
 rewrite((min((y + c1), z) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 <= min(c1, 1)))) ||
 rewrite((min((y + x), z) < (x + y)), (z < (x + y))) ||
 rewrite((min((y + x), z) < (y + x)), (z < (y + x))) ||
 rewrite((min((z + c0), y) < (min(x, y) + c0)), (z < min(x, y)), (c0 <= 0)) ||
 rewrite((min((y*c0), c2) < ((x*c0) + c1)), (min(0, y) < x), ((c1 <= min(c2, 0)) && ((max(c2, 0) + 1) <= (c0 + c1)))) ||
 rewrite((min((y*x), z) < (u + (w + min(z, (x*y))))), (0 < (u + w))) ||
 rewrite((min((y*x), z) < (u + (w + min((x*y), z)))), (0 < (u + w))) ||
 rewrite((min((y*x), z) < (u + (min(z, (x*y)) + w))), (0 < (u + w))) ||
 rewrite((min((y*x), z) < (u + (min((x*y), z) + w))), (0 < (u + w))) ||
 rewrite((min((y*x), z) < ((w + min(z, (x*y))) + u)), (0 < (w + u))) ||
 rewrite((min((y*x), z) < ((w + min((x*y), z)) + u)), (0 < (w + u))) ||
 rewrite((min((y*x), z) < ((min(z, (x*y)) + w) + u)), (0 < (w + u))) ||
 rewrite((min((y*x), z) < ((min((x*y), z) + w) + u)), (0 < (w + u))) ||
 rewrite((min(min(x, z), y) < (x + c0)), (min(z, y) < (x + c0)), (c0 <= 0)) ||
 rewrite((min(min(z, x), y) < (x + c0)), (min(z, y) < (x + c0)), (c0 <= 0)) ||
 rewrite((min(select(y, x, (x + c1)), z) < (x + c0)), (z < (x + c0)), (c0 <= min(c1, 0))) ||
 rewrite((min(select(y, (x + c1), x), z) < (x + c0)), (z < (x + c0)), (c0 <= min(c1, 0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((min(w, min(z, (y + c0))) < min(x, y)), (min(w, z) < min(x, y)), (0 <= c0)) ||
 rewrite((min(w, min(z, (y + c0))) < min(y, x)), (min(w, z) < min(y, x)), (0 <= c0)) ||
 rewrite((min(w, min((y + c0), z)) < min(x, y)), (min(w, z) < min(x, y)), (0 <= c0)) ||
 rewrite((min(w, min((y + c0), z)) < min(y, x)), (min(w, z) < min(y, x)), (0 <= c0)) ||
 rewrite((min(x, c1) < min(min(min(x, y), z), c0)), false, (c0 <= c1)) ||
 rewrite((min(y, c0) < min(z, min(x, y))), (fold((c0 + 1)) <= min(z, min(x, y)))) ||
 rewrite((min(y, c0) < min(z, min(y, x))), (fold((c0 + 1)) <= min(z, min(y, x)))) ||
 rewrite((min(y, c0) < min(min(x, y), z)), (fold((c0 + 1)) <= min(min(x, y), z))) ||
 rewrite((min(y, c0) < min(min(y, x), z)), (fold((c0 + 1)) <= min(min(y, x), z))) ||
 rewrite((min(y, c1) < min(min(min(x, y), z), c0)), false, (c0 <= c1)) ||
 rewrite((min(y, z) < min(x, y)), (z < min(x, y))) ||
 rewrite((min(y, z) < min(x, (y + c0))), (min(y, z) < x), (1 <= c0)) ||
 rewrite((min(y, z) < min(y, x)), (z < min(y, x))) ||
 rewrite((min(y, z) < min((y + c0), x)), (min(y, z) < x), (1 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(z, min(x, y))), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(z, min(y, x))), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(min(x, y), z)), false, (0 <= c0)) ||
 rewrite((min(y, (x + c0)) < min(min(y, x), z)), false, (0 <= c0)) ||
 rewrite((min(y, (z + w)) < min(min(x, y), (w + z))), false) ||
 rewrite((min(y, (z + w)) < min(min(y, x), (w + z))), false) ||
 rewrite((min(z, y) < min(x, y)), (z < min(x, y))) ||
 rewrite((min(z, y) < min(x, (y + c0))), (min(z, y) < x), (1 <= c0)) ||
 rewrite((min(z, y) < min(y, x)), (z < min(y, x))) ||
 rewrite((min(z, y) < min((y + c0), x)), (min(z, y) < x), (1 <= c0)) ||
 rewrite((min(z, (y + c0)) < min(x, y)), (z < min(x, y)), (0 <= c0)) ||
 rewrite((min(z, (y + c0)) < min(y, x)), (z < min(y, x)), (0 <= c0)) ||
 rewrite((min(z, min(y, x)) < min(x, y)), (z < min(x, y))) ||
 rewrite((min(z, min(y, x)) < min(y, x)), (z < min(y, x))) ||
 rewrite((min((y + c0), x) < min(z, min(x, y))), false, (0 <= c0)) ||
 rewrite((min((y + c0), x) < min(z, min(y, x))), false, (0 <= c0)) ||
 rewrite((min((y + c0), x) < min(min(x, y), z)), false, (0 <= c0)) ||
 rewrite((min((y + c0), x) < min(min(y, x), z)), false, (0 <= c0)) ||
 rewrite((min((y + c0), z) < min(x, y)), (z < min(x, y)), (0 <= c0)) ||
 rewrite((min((y + c0), z) < min(y, x)), (z < min(y, x)), (0 <= c0)) ||
 rewrite((min((z + w), y) < min(min(x, y), (w + z))), false) ||
 rewrite((min((z + w), y) < min(min(y, x), (w + z))), false) ||
 rewrite((min(min(y, x), z) < min(x, y)), (z < min(x, y))) ||
 rewrite((min(min(y, x), z) < min(y, x)), (z < min(y, x))) ||
 rewrite((min(min(z, (y + c0)), w) < min(x, y)), (min(z, w) < min(x, y)), (0 <= c0)) ||
 rewrite((min(min(z, (y + c0)), w) < min(y, x)), (min(z, w) < min(y, x)), (0 <= c0)) ||
 rewrite((min(min((y + c0), z), w) < min(x, y)), (min(z, w) < min(x, y)), (0 <= c0)) ||
 rewrite((min(min((y + c0), z), w) < min(y, x)), (min(z, w) < min(y, x)), (0 <= c0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((min(y, x) < max(x, y)), (x != y)) ||
 rewrite((min(y, x) < max(y, x)), (x != y)))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max(y, (x*x)) < x), false) ||
 rewrite((max(y, select(x, y, z)) < z), (y < select(x, z, y))) ||
 rewrite((max(z, min(x, y)) < y), (max(z, x) < y)) ||
 rewrite((max(z, min(y, x)) < y), (max(z, x) < y)) ||
 rewrite((max(z, max(y, x)) < x), false) ||
 rewrite((max(z, max(y, x)) < y), false) ||
 rewrite((max(z, select(x, y, z)) < y), (select(x, y, z) < y)) ||
 rewrite((max((x*x), y) < x), false) ||
 rewrite((max(min(y, x), z) < y), (max(x, z) < y)) ||
 rewrite((max(min(z, x), y) < x), (max(z, y) < x)) ||
 rewrite((max(max(y, x), z) < x), false) ||
 rewrite((max(max(y, x), z) < y), false) ||
 rewrite((max(max(y, z), (max(w, z) + c0)) < x), (max(y, (max(w, z) + c0)) < x), (0 <= c0)) ||
 rewrite((max(max(z, y), (max(w, z) + c0)) < x), (max(y, (max(w, z) + c0)) < x), (0 <= c0)) ||
 rewrite((max(select(x, y, z), y) < z), (y < select(x, z, y))) ||
 rewrite((max(select(x, y, z), z) < y), (select(x, y, z) < y)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((max(u, max(w, (z + (y*x)))) < (z + (x*y))), false) ||
 rewrite((max(u, max(w, (z + (y*x)))) < ((x*y) + z)), false) ||
 rewrite((max(u, max(w, (z + (y*x)))) < ((y*x) + z)), false) ||
 rewrite((max(u, max(w, (z + min(y, x)))) < (z + min(x, y))), false) ||
 rewrite((max(u, max(w, (z + min(y, x)))) < (min(x, y) + z)), false) ||
 rewrite((max(u, max(w, (z + min(y, x)))) < (min(y, x) + z)), false) ||
 rewrite((max(u, max(w, ((y*x) + z))) < (z + (x*y))), false) ||
 rewrite((max(u, max(w, ((y*x) + z))) < (z + (y*x))), false) ||
 rewrite((max(u, max(w, ((y*x) + z))) < ((x*y) + z)), false) ||
 rewrite((max(u, max(w, (min(y, x) + z))) < (z + min(x, y))), false) ||
 rewrite((max(u, max(w, (min(y, x) + z))) < (z + min(y, x))), false) ||
 rewrite((max(u, max(w, (min(y, x) + z))) < (min(x, y) + z)), false) ||
 rewrite((max(u, max((z + (y*x)), w)) < (z + (x*y))), false) ||
 rewrite((max(u, max((z + (y*x)), w)) < ((x*y) + z)), false) ||
 rewrite((max(u, max((z + (y*x)), w)) < ((y*x) + z)), false) ||
 rewrite((max(u, max((z + min(y, x)), w)) < (z + min(x, y))), false) ||
 rewrite((max(u, max((z + min(y, x)), w)) < (min(x, y) + z)), false) ||
 rewrite((max(u, max((z + min(y, x)), w)) < (min(y, x) + z)), false) ||
 rewrite((max(u, max(((y*x) + z), w)) < (z + (x*y))), false) ||
 rewrite((max(u, max(((y*x) + z), w)) < (z + (y*x))), false) ||
 rewrite((max(u, max(((y*x) + z), w)) < ((x*y) + z)), false) ||
 rewrite((max(u, max((min(y, x) + z), w)) < (z + min(x, y))), false) ||
 rewrite((max(u, max((min(y, x) + z), w)) < (z + min(y, x))), false) ||
 rewrite((max(u, max((min(y, x) + z), w)) < (min(x, y) + z)), false) ||
 rewrite((max(u, max((z*w), (y + x))) < (x + y)), false) ||
 rewrite((max(u, max(min(z, w), (y + x))) < (x + y)), false) ||
 rewrite((max(w, (y + z)) < (z + min(x, y))), false) ||
 rewrite((max(w, (y + z)) < (z + min(y, x))), false) ||
 rewrite((max(w, (y + z)) < (min(x, y) + z)), false) ||
 rewrite((max(w, (y + z)) < (min(y, x) + z)), false) ||
 rewrite((max(w, (z + y)) < (z + min(x, y))), false) ||
 rewrite((max(w, (z + y)) < (z + min(y, x))), false) ||
 rewrite((max(w, (z + y)) < (min(x, y) + z)), false) ||
 rewrite((max(w, (z + y)) < (min(y, x) + z)), false) ||
 rewrite((max(w, min(z, min(x, y))) < (x + c0)), ((fold((1 - c0)) + w) <= x), (1 <= c0)) ||
 rewrite((max(w, min(z, min(y, x))) < (x + c0)), ((fold((1 - c0)) + w) <= x), (1 <= c0)) ||
 rewrite((max(w, min(min(x, y), z)) < (x + c0)), ((w + fold((1 - c0))) <= x), (1 <= c0)) ||
 rewrite((max(w, min(min(y, x), z)) < (x + c0)), ((w + fold((1 - c0))) <= x), (1 <= c0)) ||
 rewrite((max(y, c2) < (max(x, c0) + 1)), (max(y, c2) <= x), ((c0 + 1) <= c2)) ||
 rewrite((max(y, c2) < (max(x, c0) + c1)), (max(y, c2) < (x + c1)), ((c0 + c1) <= c2)) ||
 rewrite((max(y, x) < (x + y)), (1 <= min(x, y))) ||
 rewrite((max(y, x) < (y + x)), (1 <= min(y, x))) ||
 rewrite((max(y, (x*c0)) < (w + (z + max((x*c0), y)))), (0 < (w + z))) ||
 rewrite((max(y, (x*c0)) < (w + (max((x*c0), y) + z))), (0 < (w + z))) ||
 rewrite((max(y, (x*c0)) < ((z + max((x*c0), y)) + w)), (0 < (z + w))) ||
 rewrite((max(y, (x*c0)) < ((max((x*c0), y) + z) + w)), (0 < (z + w))) ||
 rewrite((max(z, (y + max(w, x))) < (x + y)), false) ||
 rewrite((max(z, (y + max(w, x))) < (y + x)), false) ||
 rewrite((max(z, (y + max(x, w))) < (x + y)), false) ||
 rewrite((max(z, (y + max(x, w))) < (y + x)), false) ||
 rewrite((max(z, (max(w, y) + x)) < (x + y)), false) ||
 rewrite((max(z, (max(w, y) + x)) < (y + x)), false) ||
 rewrite((max(z, (max(y, w) + x)) < (x + y)), false) ||
 rewrite((max(z, (max(y, w) + x)) < (y + x)), false) ||
 rewrite((max(z, (x - y)) < (u + (w + max((x - y), z)))), (0 < (u + w))) ||
 rewrite((max(z, (x - y)) < (u + (max((x - y), z) + w))), (0 < (u + w))) ||
 rewrite((max(z, (x - y)) < ((w + max((x - y), z)) + u)), (0 < (w + u))) ||
 rewrite((max(z, (x - y)) < ((max((x - y), z) + w) + u)), (0 < (w + u))) ||
 rewrite((max((y + z), w) < (z + min(x, y))), false) ||
 rewrite((max((y + z), w) < (z + min(y, x))), false) ||
 rewrite((max((y + z), w) < (min(x, y) + z)), false) ||
 rewrite((max((y + z), w) < (min(y, x) + z)), false) ||
 rewrite((max((y + max(x, z)), w) < (x + y)), false) ||
 rewrite((max((y + max(x, z)), w) < (y + x)), false) ||
 rewrite((max((y + max(z, x)), w) < (x + y)), false) ||
 rewrite((max((y + max(z, x)), w) < (y + x)), false) ||
 rewrite((max((z + y), w) < (z + min(x, y))), false) ||
 rewrite((max((z + y), w) < (z + min(y, x))), false) ||
 rewrite((max((z + y), w) < (min(x, y) + z)), false) ||
 rewrite((max((z + y), w) < (min(y, x) + z)), false) ||
 rewrite((max((max(y, z) + x), w) < (x + y)), false) ||
 rewrite((max((max(y, z) + x), w) < (y + x)), false) ||
 rewrite((max((max(z, y) + x), w) < (x + y)), false) ||
 rewrite((max((max(z, y) + x), w) < (y + x)), false) ||
 rewrite((max((x - y), z) < (u + (w + max(z, (x - y))))), (0 < (u + w))) ||
 rewrite((max((x - y), z) < (u + (max(z, (x - y)) + w))), (0 < (u + w))) ||
 rewrite((max((x - y), z) < ((w + max(z, (x - y))) + u)), (0 < (w + u))) ||
 rewrite((max((x - y), z) < ((max(z, (x - y)) + w) + u)), (0 < (w + u))) ||
 rewrite((max((x*c0), y) < (w + (z + max(y, (x*c0))))), (0 < (w + z))) ||
 rewrite((max((x*c0), y) < (w + (max(y, (x*c0)) + z))), (0 < (w + z))) ||
 rewrite((max((x*c0), y) < ((z + max(y, (x*c0))) + w)), (0 < (z + w))) ||
 rewrite((max((x*c0), y) < ((max(y, (x*c0)) + z) + w)), (0 < (z + w))) ||
 rewrite((max(min(z, min(x, y)), w) < (x + c0)), ((fold((1 - c0)) + w) <= x), (1 <= c0)) ||
 rewrite((max(min(z, min(y, x)), w) < (x + c0)), ((fold((1 - c0)) + w) <= x), (1 <= c0)) ||
 rewrite((max(min(min(x, y), z), w) < (x + c0)), ((fold((1 - c0)) + w) <= x), (1 <= c0)) ||
 rewrite((max(min(min(y, x), z), w) < (x + c0)), ((fold((1 - c0)) + w) <= x), (1 <= c0)) ||
 rewrite((max(max(w, (z + (y*x))), u) < (z + (x*y))), false) ||
 rewrite((max(max(w, (z + (y*x))), u) < ((x*y) + z)), false) ||
 rewrite((max(max(w, (z + (y*x))), u) < ((y*x) + z)), false) ||
 rewrite((max(max(w, (z + min(y, x))), u) < (z + min(x, y))), false) ||
 rewrite((max(max(w, (z + min(y, x))), u) < (min(x, y) + z)), false) ||
 rewrite((max(max(w, (z + min(y, x))), u) < (min(y, x) + z)), false) ||
 rewrite((max(max(w, ((y*x) + z)), u) < (z + (x*y))), false) ||
 rewrite((max(max(w, ((y*x) + z)), u) < (z + (y*x))), false) ||
 rewrite((max(max(w, ((y*x) + z)), u) < ((x*y) + z)), false) ||
 rewrite((max(max(w, (min(y, x) + z)), u) < (z + min(x, y))), false) ||
 rewrite((max(max(w, (min(y, x) + z)), u) < (z + min(y, x))), false) ||
 rewrite((max(max(w, (min(y, x) + z)), u) < (min(x, y) + z)), false) ||
 rewrite((max(max(z, (y + x)), (w + u)) < (x + y)), false) ||
 rewrite((max(max((y + x), z), (w + u)) < (x + y)), false) ||
 rewrite((max(max((z + (y*x)), w), u) < (z + (x*y))), false) ||
 rewrite((max(max((z + (y*x)), w), u) < ((x*y) + z)), false) ||
 rewrite((max(max((z + (y*x)), w), u) < ((y*x) + z)), false) ||
 rewrite((max(max((z + min(y, x)), w), u) < (z + min(x, y))), false) ||
 rewrite((max(max((z + min(y, x)), w), u) < (min(x, y) + z)), false) ||
 rewrite((max(max((z + min(y, x)), w), u) < (min(y, x) + z)), false) ||
 rewrite((max(max(((y*x) + z), w), u) < (z + (x*y))), false) ||
 rewrite((max(max(((y*x) + z), w), u) < (z + (y*x))), false) ||
 rewrite((max(max(((y*x) + z), w), u) < ((x*y) + z)), false) ||
 rewrite((max(max((min(y, x) + z), w), u) < (z + min(x, y))), false) ||
 rewrite((max(max((min(y, x) + z), w), u) < (z + min(y, x))), false) ||
 rewrite((max(max((min(y, x) + z), w), u) < (min(x, y) + z)), false) ||
 rewrite((max(max((z*w), (y + x)), u) < (x + y)), false) ||
 rewrite((max(max(min(z, w), (y + x)), u) < (x + y)), false))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((max((y*c0), c1) < (x*c0)), (max(0, y) < x), (((0 <= c1) && ((c1 + 1) <= (c0*2))) && ((max(c1, 0) + 1) <= c0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((max(w, max(y, z)) < min(x, y)), false) ||
 rewrite((max(w, max(y, z)) < min(y, x)), false) ||
 rewrite((max(w, max(z, y)) < min(x, y)), false) ||
 rewrite((max(w, max(z, y)) < min(y, x)), false) ||
 rewrite((max(y, z) < min(x, y)), false) ||
 rewrite((max(y, z) < min(y, x)), false) ||
 rewrite((max(z, y) < min(x, y)), false) ||
 rewrite((max(z, y) < min(y, x)), false) ||
 rewrite((max(z, min(y, x)) < min(x, y)), false) ||
 rewrite((max(z, min(y, x)) < min(y, x)), false) ||
 rewrite((max((w + u), (z + y)) < min(x, (y + z))), false) ||
 rewrite((max((w + u), (z + y)) < min((y + z), x)), false) ||
 rewrite((max(min(y, x), z) < min(x, y)), false) ||
 rewrite((max(min(y, x), z) < min(y, x)), false) ||
 rewrite((max(max(y, z), w) < min(x, y)), false) ||
 rewrite((max(max(y, z), w) < min(y, x)), false) ||
 rewrite((max(max(z, y), w) < min(x, y)), false) ||
 rewrite((max(max(z, y), w) < min(y, x)), false))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((max(y, z) < max(x, y)), (max(y, z) < x)) ||
 rewrite((max(y, z) < max(y, x)), (max(y, z) < x)) ||
 rewrite((max(z, y) < max(x, y)), (max(z, y) < x)) ||
 rewrite((max(z, y) < max(y, x)), (max(z, y) < x)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((max(y, x) < select(z, x, y)), false) ||
 rewrite((max(y, x) < select(z, y, x)), false) ||
 rewrite((max(z, y) < select(x, max(y, z), y)), false) ||
 rewrite((max(z, y) < select(x, max(y, z), z)), false) ||
 rewrite((max(z, y) < select(x, max(z, y), y)), false) ||
 rewrite((max(z, y) < select(x, max(z, y), z)), false))))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite((select(x, 0, (y - z)) < y), (1 <= select(x, y, z))) ||
 rewrite((select(x, y, (y + z)) < y), (select(x, 0, z) < 0)) ||
 rewrite((select(x, y, (y + z)) < z), (y < select(x, z, 0))) ||
 rewrite((select(x, y, (z + y)) < y), (select(x, 0, z) < 0)) ||
 rewrite((select(x, y, (z + y)) < z), (y < select(x, z, 0))) ||
 rewrite((select(x, y, (y - z)) < y), (0 < select(x, 0, z))) ||
 rewrite((select(x, y, (y*c0)) < y), (y < select(x, y, 0)), (2 <= c0)) ||
 rewrite((select(x, y, (z*z)) < z), (select(x, y, z) < z)) ||
 rewrite((select(x, y, min(w, z)) < w), (select(x, y, z) < w)) ||
 rewrite((select(x, y, min(z, w)) < w), (select(x, y, z) < w)) ||
 rewrite((select(x, y, max(z, w)) < w), (select(x, y, w) < w)) ||
 rewrite((select(x, y, max(z, w)) < z), (select(x, y, z) < z)) ||
 rewrite((select(x, (z + y), y) < z), (y < select(x, 0, z))) ||
 rewrite((select(x, (z + y), z) < y), (z < select(x, 0, y))) ||
 rewrite((select(x, (y - z), 0) < y), (0 < select(x, z, y))) ||
 rewrite((select(x, (y - z), y) < y), (1 <= select(x, z, 0))) ||
 rewrite((select(x, (y*c0), y) < y), (select(x, y, 0) < 0), (2 <= c0)) ||
 rewrite((select(x, (y*y), y) < y), false) ||
 rewrite((select(x, min(y, z), w) < y), (select(x, z, w) < y)) ||
 rewrite((select(x, min(z, y), w) < y), (select(x, z, w) < y)) ||
 rewrite((select(x, max(z, y), w) < z), (select(x, z, w) < z)) ||
 rewrite((select(y, (x + z), x) < x), (select(y, z, 0) <= -1)) ||
 rewrite((select(y, (z + x), x) < x), (select(y, z, 0) <= -1)) ||
 rewrite((select(y, ((z + x) + c0), x) < x), (select(y, z, fold((0 - c0))) < fold((0 - c0)))) ||
 rewrite((select((x < y), x, z) < y), (min(x, z) < y)) ||
 rewrite((select((x < y), x, z) < z), (x < min(y, z))) ||
 rewrite((select((x < y), z, y) < y), (max(x, z) < y)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((select(x, y, z) < (y + z)), (0 < select(x, z, y))) ||
 rewrite((select(x, y, z) < (z + y)), (0 < select(x, z, y))) ||
 rewrite((select(z, y, (w + y)) < (x + y)), (select(z, 0, w) < x)) ||
 rewrite((select(z, y, (w + y)) < (y + x)), (select(z, 0, w) < x)) ||
 rewrite((select(z, y, (y + w)) < (x + y)), (select(z, 0, w) < x)) ||
 rewrite((select(z, y, (y + w)) < (y + x)), (select(z, 0, w) < x)) ||
 rewrite((select(z, (u + (w + (y + x))), (x + y)) < (x + y)), (select(z, (u + (w + x)), x) < x)) ||
 rewrite((select(z, (u + (w + (y + x))), (x + y)) < (y + x)), (select(z, (u + (w + x)), x) < x)) ||
 rewrite((select(z, (u + (w + (y + x))), (y + x)) < (x + y)), (select(z, (u + (w + x)), x) < x)) ||
 rewrite((select(z, (u + (w + (y + x))), (y + x)) < (y + x)), (select(z, (u + (w + x)), x) < x)) ||
 rewrite((select(z, (u + ((y + x) + w)), (x + y)) < (x + y)), (select(z, (u + (x + w)), x) < x)) ||
 rewrite((select(z, (u + ((y + x) + w)), (x + y)) < (y + x)), (select(z, (u + (x + w)), x) < x)) ||
 rewrite((select(z, (u + ((y + x) + w)), (y + x)) < (x + y)), (select(z, (u + (x + w)), x) < x)) ||
 rewrite((select(z, (u + ((y + x) + w)), (y + x)) < (y + x)), (select(z, (u + (x + w)), x) < x)) ||
 rewrite((select(z, (w + x), x) < (x + y)), (select(z, w, 0) < y)) ||
 rewrite((select(z, (w + x), x) < (y + x)), (select(z, w, 0) < y)) ||
 rewrite((select(z, (x + w), x) < (x + y)), (select(z, w, 0) < y)) ||
 rewrite((select(z, (x + w), x) < (y + x)), (select(z, w, 0) < y)) ||
 rewrite((select(z, ((w + (y + x)) + u), (x + y)) < (x + y)), (select(z, ((w + x) + u), x) < x)) ||
 rewrite((select(z, ((w + (y + x)) + u), (x + y)) < (y + x)), (select(z, ((w + x) + u), x) < x)) ||
 rewrite((select(z, ((w + (y + x)) + u), (y + x)) < (x + y)), (select(z, ((w + x) + u), x) < x)) ||
 rewrite((select(z, ((w + (y + x)) + u), (y + x)) < (y + x)), (select(z, ((w + x) + u), x) < x)) ||
 rewrite((select(z, (((y + x) + w) + u), (x + y)) < (x + y)), (select(z, ((x + w) + u), x) < x)) ||
 rewrite((select(z, (((y + x) + w) + u), (x + y)) < (y + x)), (select(z, ((x + w) + u), x) < x)) ||
 rewrite((select(z, (((y + x) + w) + u), (y + x)) < (x + y)), (select(z, ((x + w) + u), x) < x)) ||
 rewrite((select(z, (((y + x) + w) + u), (y + x)) < (y + x)), (select(z, ((x + w) + u), x) < x)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((select(x, y, z) < min(y, z)), false) ||
 rewrite((select(x, y, z) < min(z, y)), false) ||
 rewrite((select(y, c1, min(x, c0)) < min(x, c0)), false, (c0 <= c1)) ||
 rewrite((select(z, min(y, x), x) < min(x, y)), false) ||
 rewrite((select(z, min(y, x), x) < min(y, x)), false) ||
 rewrite((select(z, min(y, x), y) < min(x, y)), false) ||
 rewrite((select(z, min(y, x), y) < min(y, x)), false) ||
 rewrite((select(z, max(w, x), min(x, y)) < min(x, y)), false) ||
 rewrite((select(z, max(w, x), min(x, y)) < min(y, x)), false) ||
 rewrite((select(z, max(w, x), min(y, x)) < min(x, y)), false) ||
 rewrite((select(z, max(w, x), min(y, x)) < min(y, x)), false) ||
 rewrite((select(z, max(x, w), min(x, y)) < min(x, y)), false) ||
 rewrite((select(z, max(x, w), min(x, y)) < min(y, x)), false) ||
 rewrite((select(z, max(x, w), min(y, x)) < min(x, y)), false) ||
 rewrite((select(z, max(x, w), min(y, x)) < min(y, x)), false))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((select((x && y), c0, c1) < select(x, c0, c1)), false, (c0 <= c1)) ||
 rewrite((select((y && x), c0, c1) < select(x, c0, c1)), false, (c0 <= c1))))))