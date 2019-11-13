((b.node_type() == IRNodeType::Add) && (
 rewrite(min(x, (y + ((((x - y) + c0)/c1)*c1))), x, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
 rewrite(min(x, (((((x - y)/c0)*c0) + y) + c1)), x, ((-1 <= (c0 + c1)) && ((c0 + -1) <= c1))) ||
 rewrite(min(x, ((y - select((z < (y + c0)), c1, c2)) + c3)), min(x, (y + select((z < (y + c0)), fold((c3 - c1)), fold((c3 - c2)))))) ||
 rewrite(min(x, (((((x - y) + c0)/c1)*c1) + y)), x, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
 rewrite(min(x, ((min((x/c0), y)*c0) + c1)), min(x, ((y*c0) + c1)), ((1 <= c0) && ((max(c0, 1) + -1) <= c1))) ||
 rewrite(min(x, (min(y, (x + c0)) + c1)), min(x, (y + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(x, (min(y, (x + c0)) + c1)), (min(y, (x + c0)) + c1), ((c0 + c1) <= 0)) ||
 rewrite(min(x, (min((x + c0), y) + c1)), min(x, (y + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(x, (min((x + c0), y) + c1)), (min((x + c0), y) + c1), ((c0 + c1) <= 0)) ||
 rewrite(min(x, (min(min(y, (x + c0)), z) + c1)), min(x, (min(y, z) + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(x, (min(min((x + c0), y), z) + c1)), min(x, (min(y, z) + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(x, (max(min(x, c0), c1) + c2)), min(x, fold((max(c0, c1) + c2))), (((((-1 <= c2) || (c0 <= c1)) || ((c1 + -1) <= c0)) && ((((-1 <= c2) || (c0 <= c1)) || (c0 <= (c1 + c2))) && (((0 <= c2) || (c0 <= c1)) && (c1 <= c0)))) || ((c0 + 1) <= c1))) ||
 rewrite(min(x, (max(min(x, y), c0) + c0)), min(x, (max(y, c0) + c0)), (0 <= c0)) ||
 rewrite(min(y, (min(x, (y + c0)) + c1)), (min(y, (fold(max((-1 - c0), (c1 - max(min(min((c1 - max((0 - c0), c1)), (c0 + c1)), 0), (c1 - max((0 - c0), c1)))))) + x)) + fold(max(min(min((c1 - max((0 - c0), c1)), (c0 + c1)), 0), (c1 - max((0 - c0), c1)))))) ||
 rewrite(min(y, (min(x, (y + c0)) + c1)), min(y, (x + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(y, (min(min(x, y), z) + c0)), min(y, (min(x, z) + c0)), (0 <= c0)) ||
 rewrite(min(y, (min(min((x + c0), y), c0) + c1)), min(y, min((fold((max(c1, 0) + c0)) + x), fold((max(c1, 0) + c0)))), (0 <= c1)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(min(x, (min(((x + c0)/c1), y)*c1)), min(x, (y*c1)), ((1 <= c1) && ((c1 + -1) <= c0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(w, min(z, select((x < y), z, max(w, c0)))), min(z, w)) ||
 rewrite(min(w, min(select((x < y), z, max(w, c0)), z)), min(z, w)) ||
 rewrite(min(x, min(w, select((x < y), max(x, z), w))), min(x, w)) ||
 rewrite(min(x, min(w, select((x < y), max(z, x), w))), min(w, x)) ||
 rewrite(min(x, min(x, y)), min(x, y)) ||
 rewrite(min(x, min(y, x)), min(x, y)) ||
 rewrite(min(x, min(y, (x + c0))), min(x, y), (0 <= c0)) ||
 rewrite(min(x, min(y, (x + c0))), min(y, (x + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(x, c0) + c1))), min(x, min(y, fold((c0 + c1)))), (0 <= c1)) ||
 rewrite(min(x, min(y, max(x, c0))), min(x, y)) ||
 rewrite(min(x, min(z, (min((x + c0), y) + c1))), min(x, min(z, (y + c1))), (0 <= (c0 + c1))) ||
 rewrite(min(x, min(z, min(y, (x + c0)))), min(x, min(z, y)), (0 <= c0)) ||
 rewrite(min(x, min(z, min(y, (x + c0)))), min(z, min(y, (x + c0))), (c0 <= 0)) ||
 rewrite(min(x, min(z, min((x + c0), y))), min(z, min(x, y)), (0 <= c0)) ||
 rewrite(min(x, min(z, min((x + c0), y))), min(z, min((x + c0), y)), (c0 <= 0)) ||
 rewrite(min(x, min((x + c0), y)), min(x, y), (0 <= c0)) ||
 rewrite(min(x, min((x + c0), y)), min((x + c0), y), (c0 <= 0)) ||
 rewrite(min(x, min((min(x, c0) + c1), y)), min(x, min(fold((c0 + c1)), y)), (0 <= c1)) ||
 rewrite(min(x, min((min((x + c0), y) + c1), z)), min(x, min((y + c1), z)), (0 <= (c0 + c1))) ||
 rewrite(min(x, min(min(y, (x + c0)), z)), min(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, (x + c0)), z)), min(min(y, (x + c0)), z), (c0 <= 0)) ||
 rewrite(min(x, min(min((x + c0), y), z)), min(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(x, min(min((x + c0), y), z)), min(min((x + c0), y), z), (c0 <= 0)) ||
 rewrite(min(x, min(max(x, c0), y)), min(x, y)) ||
 rewrite(min(x, min(max(x, y), min(x, z))), min(max(x, y), min(x, z))) ||
 rewrite(min(x, min(max(x, y), min(z, x))), min(x, min(max(x, y), z))) ||
 rewrite(min(x, min(max(y, x), min(x, z))), min(max(y, x), min(x, z))) ||
 rewrite(min(x, min(max(y, x), min(z, x))), min(x, min(max(y, x), z))) ||
 rewrite(min(x, min(select((x < y), max(x, z), w), w)), min(x, w)) ||
 rewrite(min(x, min(select((x < y), max(z, x), w), w)), min(x, w)) ||
 rewrite(min(y, min(x, (y + c0))), min(y, x), (0 <= c0)) ||
 rewrite(min(y, min(x, (y + c0))), min(x, (y + c0)), (c0 <= 0)) ||
 rewrite(min(y, min(x, (z + min(x, y)))), (min(0, z) + min(x, y))) ||
 rewrite(min(y, min(x, (z + min(y, x)))), (min(0, z) + min(y, x))) ||
 rewrite(min(y, min(x, (min(x, y) + z))), (min(0, z) + min(x, y))) ||
 rewrite(min(y, min(x, (min(y, x) + z))), (min(z, 0) + min(y, x))) ||
 rewrite(min(y, min(x, (min(y, (z + c0)) + c1))), min(y, min(x, (fold((c0 + c1)) + z))), (0 <= c1)) ||
 rewrite(min(y, min(x, (min((y + c0), z) + c1))), min(y, min(x, (z + c1))), (0 <= (c0 + c1))) ||
 rewrite(min(y, min(z, (y + min(x, c0)))), min((y + min(x, fold(min(c0, 0)))), z)) ||
 rewrite(min(y, min(z, (min(x, c0) + y))), min(z, (min(x, fold(min(c0, 0))) + y))) ||
 rewrite(min(y, min(z, (min(x, y) + c0))), min(y, min(z, (x + c0))), (0 <= c0)) ||
 rewrite(min(y, min(z, min(x, (y + c0)))), min(z, min(x, (y + c0))), (c0 <= 0)) ||
 rewrite(min(y, min(z, min((y + c0), x))), min(z, min((y + c0), x)), (c0 <= 0)) ||
 rewrite(min(y, min(z, max(x, y))), min(y, z)) ||
 rewrite(min(y, min(z, max(y, x))), min(z, y)) ||
 rewrite(min(y, min(z, select(x, y, (y + c0)))), min(y, z), (0 <= c0)) ||
 rewrite(min(y, min(z, select(x, (y + c0), y))), min(y, z), (0 <= c0)) ||
 rewrite(min(y, min((y + c0), x)), min(y, x), (0 <= c0)) ||
 rewrite(min(y, min((y + c0), x)), min((y + c0), x), (c0 <= 0)) ||
 rewrite(min(y, min((y + min(x, c0)), z)), min(z, (min(x, fold(min(c0, 0))) + y))) ||
 rewrite(min(y, min((z + min(x, y)), x)), (min(0, z) + min(y, x))) ||
 rewrite(min(y, min((z + min(y, x)), x)), (min(0, z) + min(y, x))) ||
 rewrite(min(y, min((min(x, c0) + y), z)), min(z, (min(fold(min(c0, 0)), x) + y))) ||
 rewrite(min(y, min((min(x, y) + c0), z)), min(y, min((x + c0), z)), (0 <= c0)) ||
 rewrite(min(y, min((min(x, y) + z), x)), (min(0, z) + min(y, x))) ||
 rewrite(min(y, min((min(y, x) + z), x)), (min(y, x) + min(0, z))) ||
 rewrite(min(y, min((min(y, (z + c0)) + c1), x)), min(y, min((fold((c0 + c1)) + z), x)), (0 <= c1)) ||
 rewrite(min(y, min((min((y + c0), z) + c1), x)), min(y, min((z + c1), x)), (0 <= (c0 + c1))) ||
 rewrite(min(y, min(min(x, (y + c0)), z)), min(min(x, (y + c0)), z), (c0 <= 0)) ||
 rewrite(min(y, min(min((y + c0), x), z)), min(min((y + c0), x), z), (c0 <= 0)) ||
 rewrite(min(y, min(max(x, y), z)), min(y, z)) ||
 rewrite(min(y, min(max(y, x), z)), min(y, z)) ||
 rewrite(min(y, min(select(x, y, (y + c0)), z)), min(y, z), (0 <= c0)) ||
 rewrite(min(y, min(select(x, (y + c0), y), z)), min(y, z), (0 <= c0)) ||
 rewrite(min(z, min(x, (min(y, z) + c0))), min(z, min(x, (y + c0))), (0 <= c0)) ||
 rewrite(min(z, min(x, (min((y + c0), z) + c1))), min(z, min(x, (fold((max(c1, 0) + c0)) + y))), (0 <= c1)) ||
 rewrite(min(z, min(y, (x + min(y, z)))), (min(0, x) + min(y, z))) ||
 rewrite(min(z, min(y, (x + min(z, y)))), (min(x, 0) + min(z, y))) ||
 rewrite(min(z, min(y, (min(y, z) + x))), (min(z, y) + min(0, x))) ||
 rewrite(min(z, min(y, (min(z, y) + x))), (min(z, y) + min(0, x))) ||
 rewrite(min(z, min((x + min(y, z)), y)), (min(0, x) + min(y, z))) ||
 rewrite(min(z, min((x + min(z, y)), y)), (min(0, x) + min(z, y))) ||
 rewrite(min(z, min((min(y, z) + c0), x)), min(z, min((y + c0), x)), (0 <= c0)) ||
 rewrite(min(z, min((min(y, z) + x), y)), (min(z, y) + min(0, x))) ||
 rewrite(min(z, min((min(z, y) + x), y)), (min(z, y) + min(0, x))) ||
 rewrite(min(z, min((min((y + c0), z) + c1), x)), min(z, min((fold((max(c1, 0) + c0)) + y), x)), (0 <= c1)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(min(y, select(x, y, (y + c0))), y, (0 <= c0)) ||
 rewrite(min(y, select(x, (y + c0), y)), y, (0 <= c0)) ||
 rewrite(min(y, select(x, (min(y, c0) + c1), y)), select(x, min(y, fold((c0 + c1))), y), (0 <= c1)) ||
 rewrite(min(y, select(x, max(y, z), y)), y) ||
 rewrite(min(y, select(x, max(z, y), y)), y) ||
 rewrite(min(z, select(x, min(y, z), z)), select(x, min(y, z), z)) ||
 rewrite(min(z, select(x, min(z, y), z)), select(x, min(z, y), z)))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(min((y + ((((x - y) + c0)/c1)*c1)), x), x, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
 rewrite(min((((((x - y)/c0)*c0) + y) + c1), x), x, ((-1 <= (c0 + c1)) && ((c0 + -1) <= c1))) ||
 rewrite(min(((y - select((z < (y + c0)), c1, c2)) + c3), x), min((y + select((z < (y + c0)), fold((c3 - c1)), fold((c3 - c2)))), x)) ||
 rewrite(min((((((x - y) + c0)/c1)*c1) + y), x), x, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
 rewrite(min(((min((x/c0), y)*c0) + c1), x), min(((y*c0) + c1), x), ((1 <= c0) && ((max(c0, 1) + -1) <= c1))) ||
 rewrite(min((min(x, (y + c0)) + c1), y), (min((fold(max((-1 - c0), (c1 - max(min(min((c1 - max((0 - c0), c1)), (c0 + c1)), 0), (c1 - max((0 - c0), c1)))))) + x), y) + fold(max(min(min((c1 - max((0 - c0), c1)), (c0 + c1)), 0), (c1 - max((0 - c0), c1)))))) ||
 rewrite(min((min((x + c0), y) + c1), x), min((y + c1), x), (0 <= (c0 + c1))) ||
 rewrite(min((min((x + c0), y) + c1), x), (min((x + c0), y) + c1), ((c0 + c1) <= 0)) ||
 rewrite(min((min(min(x, y), z) + c0), y), min((min(x, z) + c0), y), (0 <= c0)) ||
 rewrite(min((min(min(y, (x + c0)), z) + c1), x), min((min(y, z) + c1), x), (0 <= (c0 + c1))) ||
 rewrite(min((min(min((x + c0), y), c0) + c1), y), min(min((fold((max(c1, 0) + c0)) + x), fold((max(c1, 0) + c0))), y), (0 <= c1)) ||
 rewrite(min((min(min((x + c0), y), z) + c1), x), min((min(y, z) + c1), x), (0 <= (c0 + c1))) ||
 rewrite(min((max(min(x, c0), c1) + c2), x), min(fold((max(c0, c1) + c2)), x), (((((-1 <= c2) || (c0 <= c1)) || ((c1 + -1) <= c0)) && ((((-1 <= c2) || (c0 <= c1)) || (c0 <= (c1 + c2))) && (((0 <= c2) || (c0 <= c1)) && (c1 <= c0)))) || ((c0 + 1) <= c1))) ||
 rewrite(min((max(min(x, y), c0) + c0), x), min((max(y, c0) + c0), x), (0 <= c0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((w + (x + (y + z))), (u + z)), (min((w + (x + y)), u) + z)) ||
 rewrite(min((w + (x + (y + z))), (z + u)), (min((w + (x + y)), u) + z)) ||
 rewrite(min((w + (x + (z + y))), (u + z)), (min((w + (x + y)), u) + z)) ||
 rewrite(min((w + (x + (z + y))), (z + u)), (min((w + (x + y)), u) + z)) ||
 rewrite(min((w + ((y + z) + x)), (u + z)), (min((w + (y + x)), u) + z)) ||
 rewrite(min((w + ((y + z) + x)), (z + u)), (min((w + (y + x)), u) + z)) ||
 rewrite(min((w + ((z + y) + x)), (u + z)), (min((w + (y + x)), u) + z)) ||
 rewrite(min((w + ((z + y) + x)), (z + u)), (min((w + (y + x)), u) + z)) ||
 rewrite(min((x + y), (w + (y - z))), (min((w - z), x) + y)) ||
 rewrite(min((x + y), (z + (w + y))), (min(x, (z + w)) + y)) ||
 rewrite(min((x + y), (z + (y + w))), (min((w + z), x) + y)) ||
 rewrite(min((x + y), ((w + y) + z)), (min(x, (w + z)) + y)) ||
 rewrite(min((x + y), ((y + w) + z)), (min((w + z), x) + y)) ||
 rewrite(min((x + y), ((y - z) + w)), (min(x, (w - z)) + y)) ||
 rewrite(min((x + y), (min(y, c0) + c1)), min(fold((c0 + c1)), (min(x, c1) + y))) ||
 rewrite(min((x + (y + z)), (w + z)), (min((x + y), w) + z)) ||
 rewrite(min((x + (y + z)), (z + w)), (min((x + y), w) + z)) ||
 rewrite(min((x + (z + y)), (w + z)), (min((x + y), w) + z)) ||
 rewrite(min((x + (z + y)), (z + w)), (min((x + y), w) + z)) ||
 rewrite(min((y + x), (w + (y - z))), (y + min(x, (w - z)))) ||
 rewrite(min((y + x), (z + (w + y))), (y + min(x, (w + z)))) ||
 rewrite(min((y + x), (z + (y + w))), (y + min(x, (z + w)))) ||
 rewrite(min((y + x), ((w + y) + z)), (y + min((w + z), x))) ||
 rewrite(min((y + x), ((y + w) + z)), (y + min((z + w), x))) ||
 rewrite(min((y + x), ((y - z) + w)), (min(x, (w - z)) + y)) ||
 rewrite(min((y + x), (min(y, c0) + c1)), min(fold((c0 + c1)), (min(x, c1) + y))) ||
 rewrite(min(((x + (y + z)) + w), (u + z)), (min(((x + y) + w), u) + z)) ||
 rewrite(min(((x + (y + z)) + w), (z + u)), (min(((x + y) + w), u) + z)) ||
 rewrite(min(((x + (z + y)) + w), (u + z)), (min(((x + y) + w), u) + z)) ||
 rewrite(min(((x + (z + y)) + w), (z + u)), (min(((x + y) + w), u) + z)) ||
 rewrite(min(((y + z) + x), (w + z)), (min((y + x), w) + z)) ||
 rewrite(min(((y + z) + x), (z + w)), (min((y + x), w) + z)) ||
 rewrite(min(((z + y) + x), (w + z)), (min((y + x), w) + z)) ||
 rewrite(min(((z + y) + x), (z + w)), (min((y + x), w) + z)) ||
 rewrite(min((((y + z) + x) + w), (u + z)), (min(((y + x) + w), u) + z)) ||
 rewrite(min((((y + z) + x) + w), (z + u)), (min(((y + x) + w), u) + z)) ||
 rewrite(min((((z + y) + x) + w), (u + z)), (min(((y + x) + w), u) + z)) ||
 rewrite(min((((z + y) + x) + w), (z + u)), (min(((y + x) + w), u) + z)) ||
 rewrite(min(((x - y) + c0), (x + z)), (min((c0 - y), z) + x)) ||
 rewrite(min(((x - y) + c0), (z + x)), (min((c0 - y), z) + x)) ||
 rewrite(min((min(x, c0) + c1), (x + y)), min((min(y, c1) + x), fold((c0 + c1)))) ||
 rewrite(min((min(x, c0) + c1), (y + x)), min((min(y, c1) + x), fold((c0 + c1)))) ||
 rewrite(min((min(x, (y + z)) + c0), (y + z)), min((x + c0), (y + z)), (0 <= c0)) ||
 rewrite(min((min(x, (y + z)) + c0), (z + y)), min((x + c0), (z + y)), (0 <= c0)) ||
 rewrite(min((min((x + y), (z + c0)) + c1), (x + y)), min((fold((max(c1, -1) + c0)) + z), (x + y)), (0 <= c1)) ||
 rewrite(min((min((x + y), (z + c0)) + c1), (y + x)), min((fold((max(c1, -1) + c0)) + z), (y + x)), (0 <= c1)))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite(min((x - y), (w + (z - y))), (min(x, (w + z)) - y)) ||
 rewrite(min((x - y), ((z - y) + w)), (min((w + z), x) - y)))))) ||
((a.node_type() == IRNodeType::Mul) && (
 rewrite(min((min(((x + c0)/c1), y)*c1), x), min((y*c1), x), ((1 <= c1) && ((c1 + -1) <= c0))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((x*c0), ((min(x, y)*c0) + c1)), min((x*c0), ((y*c0) + c1)), ((0 <= c1) && (1 <= c0))) ||
 rewrite(min((x*c0), ((min(x, y)*c0) + c1)), ((min(x, y)*c0) + c1), ((1 <= c0) && (c1 <= 0))) ||
 rewrite(min((x*c0), ((min(y, x)*c0) + c1)), min((x*c0), ((y*c0) + c1)), ((0 <= c1) && (1 <= c0))) ||
 rewrite(min((x*c0), ((min(y, x)*c0) + c1)), ((min(y, x)*c0) + c1), ((1 <= c0) && (c1 <= 0))) ||
 rewrite(min((x*c0), ((min((x + c0), y)*c0) + c1)), min((x*c0), ((y*c0) + c1)), ((0 <= ((c0*c0) + c1)) && (1 <= c0))) ||
 rewrite(min(((x + y)*c0), ((y*c0) + c1)), (min((x*c0), c1) + (y*c0))) ||
 rewrite(min(((x/c0)*c0), (x + c1)), (x + c1), (((c1 <= 0) && ((c1 + -1) <= c0)) && ((c0 + c1) <= 1))) ||
 rewrite(min((((x + c0)/c1)*c1), (x + c2)), (x + c2), ((0 <= c1) && ((c1 + c2) < (c0 + 2)))) ||
 rewrite(min((((x + c0)/c1)*c1), (min(x, y) + c2)), (min(x, y) + c2), ((0 <= c1) && ((c1 + c2) < (c0 + 2)))) ||
 rewrite(min((min(x, y)*c0), ((x*c0) + c1)), (min(x, y)*c0), ((0 <= c1) && (1 <= c0))) ||
 rewrite(min((min(x, y)*c0), ((y*c0) + c1)), (min(x, y)*c0), ((0 <= c1) && (1 <= c0))))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min((((c0 - x)/c1)*c1), (c2 - x)), (c2 - x), (((c2 + -1) <= (c0 + c1)) && (((c1 + c2) + -1) <= c0))))))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite(min(min(w, select((x < y), max(x, z), w)), x), min(w, x)) ||
 rewrite(min(min(w, select((x < y), max(z, x), w)), x), min(x, w)) ||
 rewrite(min(min(x, y), x), min(x, y)) ||
 rewrite(min(min(x, (y + c0)), y), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, (y + c0)), y), min(x, (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (z + min(x, y))), y), (min(0, z) + min(x, y))) ||
 rewrite(min(min(x, (z + min(y, c0))), z), min(min(x, (z + y)), z), (0 <= c0)) ||
 rewrite(min(min(x, (z + min(y, x))), y), (min(0, z) + min(y, x))) ||
 rewrite(min(min(x, (min(x, y) + z)), y), (min(0, z) + min(x, y))) ||
 rewrite(min(min(x, (min(y, c0) + z)), z), min(min(x, (y + z)), z), (0 <= c0)) ||
 rewrite(min(min(x, (min(y, x) + z)), y), (min(y, x) + min(z, 0))) ||
 rewrite(min(min(x, (min(y, z) + c0)), z), min(min(x, (y + c0)), z), (0 <= c0)) ||
 rewrite(min(min(x, (min(y, (z + c0)) + c1)), y), min(min(x, (fold((c0 + c1)) + z)), y), (0 <= c1)) ||
 rewrite(min(min(x, (min((y + c0), z) + c1)), y), min(min(x, (z + c1)), y), (0 <= (c0 + c1))) ||
 rewrite(min(min(x, (min((y + c0), z) + c1)), z), min(min(x, (fold((max(c1, 0) + c0)) + y)), z), (0 <= c1)) ||
 rewrite(min(min(y, x), x), min(y, x)) ||
 rewrite(min(min(y, (x + c0)), x), min(y, x), (0 <= c0)) ||
 rewrite(min(min(y, (x + c0)), x), min(y, (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, (x + min(y, z))), z), (min(0, x) + min(y, z))) ||
 rewrite(min(min(y, (x + min(z, y))), z), (min(x, 0) + min(z, y))) ||
 rewrite(min(min(y, (min(x, c0) + c1)), x), min(min(y, fold((c0 + c1))), x), (0 <= c1)) ||
 rewrite(min(min(y, (min(y, z) + x)), z), (min(y, z) + min(x, 0))) ||
 rewrite(min(min(y, (min(z, y) + x)), z), (min(y, z) + min(0, x))) ||
 rewrite(min(min(y, max(x, c0)), x), min(y, x)) ||
 rewrite(min(min(z, (y + min(x, c0))), y), min((y + min(x, fold(min(c0, 0)))), z)) ||
 rewrite(min(min(z, (min(x, c0) + y)), y), min((min(x, fold(min(c0, 0))) + y), z)) ||
 rewrite(min(min(z, (min(x, y) + c0)), y), min(min(z, (x + c0)), y), (0 <= c0)) ||
 rewrite(min(min(z, (min((x + c0), y) + c1)), x), min(min(z, (y + c1)), x), (0 <= (c0 + c1))) ||
 rewrite(min(min(z, min(x, (y + c0))), y), min(z, min(x, (y + c0))), (c0 <= 0)) ||
 rewrite(min(min(z, min(y, (x + c0))), x), min(z, min(y, x)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, (x + c0))), x), min(z, min(y, (x + c0))), (c0 <= 0)) ||
 rewrite(min(min(z, min((x + c0), y)), x), min(z, min(x, y)), (0 <= c0)) ||
 rewrite(min(min(z, min((x + c0), y)), x), min(z, min((x + c0), y)), (c0 <= 0)) ||
 rewrite(min(min(z, min((y + c0), x)), y), min(z, min((y + c0), x)), (c0 <= 0)) ||
 rewrite(min(min(z, max(x, y)), y), min(z, y)) ||
 rewrite(min(min(z, max(y, x)), y), min(z, y)) ||
 rewrite(min(min(z, select(x, y, (y + c0))), y), min(z, y), (0 <= c0)) ||
 rewrite(min(min(z, select(x, (y + c0), y)), y), min(z, y), (0 <= c0)) ||
 rewrite(min(min(z, select((x < y), z, max(w, c0))), w), min(z, w)) ||
 rewrite(min(min((x + c0), y), x), min(x, y), (0 <= c0)) ||
 rewrite(min(min((x + c0), y), x), min((x + c0), y), (c0 <= 0)) ||
 rewrite(min(min((x + min(y, z)), y), z), (min(x, 0) + min(y, z))) ||
 rewrite(min(min((x + min(z, y)), y), z), (min(0, x) + min(z, y))) ||
 rewrite(min(min((y + c0), x), y), min(y, x), (0 <= c0)) ||
 rewrite(min(min((y + c0), x), y), min((y + c0), x), (c0 <= 0)) ||
 rewrite(min(min((y + min(x, c0)), z), y), min(z, (min(fold(min(c0, 0)), x) + y))) ||
 rewrite(min(min((z + min(x, y)), x), y), (min(z, 0) + min(y, x))) ||
 rewrite(min(min((z + min(y, x)), x), y), (min(0, z) + min(y, x))) ||
 rewrite(min(min(((min(x, c0) + y) + c1), x), c0), ((min(fold((0 - c1)), y) + c1) + min(x, c0))) ||
 rewrite(min(min((min(x, c0) + c1), y), x), min(min(fold((c0 + c1)), y), x), (0 <= c1)) ||
 rewrite(min(min((min(x, c0) + y), z), y), min((min(fold(min(c0, 0)), x) + y), z)) ||
 rewrite(min(min((min(x, y) + c0), z), y), min(min((x + c0), z), y), (0 <= c0)) ||
 rewrite(min(min((min(x, y) + z), x), y), (min(x, y) + min(0, z))) ||
 rewrite(min(min((min(y, x) + z), x), y), (min(y, x) + min(0, z))) ||
 rewrite(min(min((min(y, z) + c0), x), z), min(min((y + c0), x), z), (0 <= c0)) ||
 rewrite(min(min((min(y, z) + x), y), z), (min(x, 0) + min(y, z))) ||
 rewrite(min(min((min(y, (z + c0)) + c1), x), y), min(min((fold((c0 + c1)) + z), x), y), (0 <= c1)) ||
 rewrite(min(min((min(z, y) + x), y), z), (min(z, y) + min(x, 0))) ||
 rewrite(min(min((min((x + c0), y) + c1), z), x), min(min((y + c1), z), x), (0 <= (c0 + c1))) ||
 rewrite(min(min((min((y + c0), z) + c1), x), y), min(min((z + c1), x), y), (0 <= (c0 + c1))) ||
 rewrite(min(min((min((y + c0), z) + c1), x), z), min(min((fold((max(c1, 0) + c0)) + y), x), z), (0 <= c1)) ||
 rewrite(min(min(min(x, (y + c0)), z), y), min(min(x, (y + c0)), z), (c0 <= 0)) ||
 rewrite(min(min(min(y, (x + c0)), z), x), min(min(y, x), z), (0 <= c0)) ||
 rewrite(min(min(min(y, (x + c0)), z), x), min(min(y, (x + c0)), z), (c0 <= 0)) ||
 rewrite(min(min(min((x + c0), y), z), x), min(min(y, z), x), (0 <= c0)) ||
 rewrite(min(min(min((x + c0), y), z), x), min(min((x + c0), y), z), (c0 <= 0)) ||
 rewrite(min(min(min((y + c0), x), z), y), min(min((y + c0), x), z), (c0 <= 0)) ||
 rewrite(min(min(max(x, c0), y), x), min(x, y)) ||
 rewrite(min(min(max(x, y), z), y), min(z, y)) ||
 rewrite(min(min(max(x, y), min(x, z)), x), min(max(x, y), min(x, z))) ||
 rewrite(min(min(max(x, y), min(z, x)), x), min(max(x, y), min(z, x))) ||
 rewrite(min(min(max(y, x), z), y), min(y, z)) ||
 rewrite(min(min(select(x, y, (y + c0)), z), y), min(y, z), (0 <= c0)) ||
 rewrite(min(min(select(x, (y + c0), y), z), y), min(y, z), (0 <= c0)) ||
 rewrite(min(min(select((x < y), z, max(w, c0)), z), w), min(z, w)) ||
 rewrite(min(min(select((x < y), max(x, z), w), w), x), min(w, x)) ||
 rewrite(min(min(select((x < y), max(z, x), w), w), x), min(x, w)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(min(w, (z + min(x, y))), ((z + x) + c0)), min(w, (z + min(x, y))), (0 <= c0)) ||
 rewrite(min(min(w, (z + min(y, x))), ((z + x) + c0)), min(w, (z + min(y, x))), (0 <= c0)) ||
 rewrite(min(min(w, (min(x, y) + z)), ((z + x) + c0)), min(w, (min(x, y) + z)), (0 <= c0)) ||
 rewrite(min(min(w, (min(y, x) + z)), ((z + x) + c0)), min(w, (min(y, x) + z)), (0 <= c0)) ||
 rewrite(min(min(w, min(z, min(x, y))), (x + c0)), min(w, min(z, min(x, y))), (0 <= c0)) ||
 rewrite(min(min(w, min(z, min(y, x))), (x + c0)), min(w, min(z, min(y, x))), (0 <= c0)) ||
 rewrite(min(min(w, min(z, min(y, (x + c0)))), (x + c1)), min(min(w, min(z, y)), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(w, min(z, min((x + c0), y))), (x + c1)), min(min(w, min(z, y)), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(w, min(min(x, y), z)), (x + c0)), min(w, min(min(y, z), x)), (0 <= c0)) ||
 rewrite(min(min(w, min(min(y, x), z)), (x + c0)), min(w, min(min(y, z), x)), (0 <= c0)) ||
 rewrite(min(min(w, min(min(y, (x + c0)), z)), (x + c1)), min(min(w, min(y, z)), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(w, min(min((x + c0), y), z)), (x + c1)), min(min(w, min(y, z)), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(x, y), (x + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, y), (x + c0)), min(y, (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (y + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, y), (y + c0)), min(x, (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (z + min(x, y))), (min(x, y) + min(0, z))) ||
 rewrite(min(min(x, y), (z + min(y, x))), (min(x, y) + min(0, z))) ||
 rewrite(min(min(x, y), (min(x, y) + z)), (min(x, y) + min(0, z))) ||
 rewrite(min(min(x, y), (min(x, z) + c0)), min(y, (min(x, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(y, x) + z)), (min(x, y) + min(0, z))) ||
 rewrite(min(min(x, y), (min(y, z) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(y, z) + c0)), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(z, x) + c0)), min(y, (min(z, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(z, y) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, y) + c0)), min(x, (min(z, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min((x + c0), z) + c1)), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(min(x, y), (min(min(x, z), w) + c0)), min(y, (min(min(x, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(min((y + c0), x), z) + c1)), min(y, (min(x, z) + c1)), (((-1 <= c0) && (0 <= (c0 + c1))) && (c1 <= 0))) ||
 rewrite(min(min(x, (w + min(y, z))), ((w + z) + c0)), min(x, (w + min(y, z))), (0 <= c0)) ||
 rewrite(min(min(x, (w + min(z, y))), ((w + z) + c0)), min(x, (w + min(y, z))), (0 <= c0)) ||
 rewrite(min(min(x, (y + c0)), (min(x, (y + c1)) + c2)), min(x, (y + fold(min((c1 + c2), c0)))), (0 <= c2)) ||
 rewrite(min(min(x, (y + c0)), (min(x, (y + c1)) + c2)), (min(x, (y + fold(min((c0 - c2), c1)))) + c2), (c2 <= 0)) ||
 rewrite(min(min(x, (y + c0)), (min(x, (y + c1)) + c2)), min(x, (y + c0)), ((0 <= c2) && (c0 <= (c1 + c2)))) ||
 rewrite(min(min(x, (y + c0)), (min(z, y) + c1)), min(x, (min(z, y) + c1)), (c1 <= c0)) ||
 rewrite(min(min(x, ((y + z) + c0)), (y + min(w, z))), min(x, (y + min(w, z))), (0 <= c0)) ||
 rewrite(min(min(x, ((y + z) + c0)), (y + min(z, w))), min(x, (y + min(z, w))), (0 <= c0)) ||
 rewrite(min(min(x, ((y + z) + c0)), (min(w, z) + y)), min(x, (min(w, z) + y)), (0 <= c0)) ||
 rewrite(min(min(x, ((y + z) + c0)), (min(z, w) + y)), min(x, (min(z, w) + y)), (0 <= c0)) ||
 rewrite(min(min(x, (min(y, c0) + c1)), (min(y, c2) + c3)), min(x, (min(y, c0) + c1)), ((c1 <= c3) && ((c0 + c1) <= (c2 + c3)))) ||
 rewrite(min(min(x, (min(y, z) + c0)), (z + c1)), min(x, (min(y, z) + c0)), (c0 <= c1)) ||
 rewrite(min(min(x, (min(y, z) + w)), ((w + z) + c0)), min(x, (min(y, z) + w)), (0 <= c0)) ||
 rewrite(min(min(x, (min(z, y) + w)), ((w + z) + c0)), min(x, (min(z, y) + w)), (0 <= c0)) ||
 rewrite(min(min(y, x), (x + c0)), min(y, x), (0 <= c0)) ||
 rewrite(min(min(y, x), (x + c0)), min(y, (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (y + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(y, x), (y + c0)), min(x, (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (z + min(x, y))), (min(y, x) + min(0, z))) ||
 rewrite(min(min(y, x), (z + min(y, x))), (min(y, x) + min(0, z))) ||
 rewrite(min(min(y, x), (min(x, y) + z)), (min(y, x) + min(0, z))) ||
 rewrite(min(min(y, x), (min(x, z) + c0)), min(y, (min(x, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(y, x) + z)), (min(y, x) + min(0, z))) ||
 rewrite(min(min(y, x), (min(y, z) + c0)), min(min(y, x), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(y, x), (min(y, z) + c0)), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(z, x) + c0)), min(y, (min(z, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(z, y) + c0)), min(min(y, x), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(y, x), (min(z, y) + c0)), min(x, (min(z, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min((x + c0), z) + c1)), min(min(y, x), (z + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(min(y, x), (min(min(x, z), w) + c0)), min(y, (min(min(x, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(min((y + c0), x), z) + c1)), min(y, (min(x, z) + c1)), (((-1 <= c0) && (0 <= (c0 + c1))) && (c1 <= 0))) ||
 rewrite(min(min(y, (x + c0)), (min(x, z) + c1)), min(y, (min(x, z) + c1)), (c1 <= c0)) ||
 rewrite(min(min(y, (x + c0)), (min(z, x) + c1)), min(y, (min(z, x) + c1)), (c1 <= c0)) ||
 rewrite(min(min(y, (x + c0)), (min((x + c1), y) + c2)), (min(y, (x + fold(min((c0 - c2), c1)))) + c2), (c2 <= 0)) ||
 rewrite(min(min(z, (x + y)), ((y + x) + c0)), min(z, ((y + x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, ((x + y) + c0)), (x + min(w, y))), min(z, (x + min(w, y))), (0 <= c0)) ||
 rewrite(min(min(z, ((x + y) + c0)), (x + min(y, w))), min(z, (x + min(y, w))), (0 <= c0)) ||
 rewrite(min(min(z, ((x + y) + c0)), (y + x)), min(z, (y + x)), (0 <= c0)) ||
 rewrite(min(min(z, ((x + y) + c0)), (min(w, y) + x)), min(z, (min(w, y) + x)), (0 <= c0)) ||
 rewrite(min(min(z, ((x + y) + c0)), (min(y, w) + x)), min(z, (x + min(y, w))), (0 <= c0)) ||
 rewrite(min(min(z, min(x, y)), (x + c0)), min(z, min(x, y)), (0 <= c0)) ||
 rewrite(min(min(z, min(x, y)), (x + c0)), min(min(z, y), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, min(x, y)), (y + c0)), min(z, min(x, y)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, x)), (x + c0)), min(z, min(y, x)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, x)), (x + c0)), min(min(z, y), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, min(y, x)), (y + c0)), min(z, min(x, y)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, (x + c0))), (x + c1)), min(min(z, y), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(z, min((x + c0), y)), (x + c1)), min(min(z, y), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(z, select(x, y, (y + c0))), (y + c0)), min(z, (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, select(x, (y + c0), y)), (y + c0)), min(z, (y + c0)), (c0 <= 0)) ||
 rewrite(min(min((w + min(y, z)), x), ((w + z) + c0)), min((w + min(y, z)), x), (0 <= c0)) ||
 rewrite(min(min((w + min(z, y)), x), ((w + z) + c0)), min((w + min(z, y)), x), (0 <= c0)) ||
 rewrite(min(min((x + c0), y), (min(x, z) + c1)), min(y, (min(x, z) + c1)), (c1 <= c0)) ||
 rewrite(min(min((x + c0), y), (min(z, x) + c1)), min(y, (min(z, x) + c1)), (c1 <= c0)) ||
 rewrite(min(min((x + c0), y), (min((x + c1), y) + c2)), (min((x + fold(min((c0 - c2), c1))), y) + c2), (c2 <= 0)) ||
 rewrite(min(min((x + y), z), ((y + x) + c0)), min(z, ((y + x) + c0)), (c0 <= 0)) ||
 rewrite(min(min((y + c0), x), (min(x, (y + c1)) + c2)), min(x, (y + fold(min((c1 + c2), c0)))), (0 <= c2)) ||
 rewrite(min(min((y + c0), x), (min(x, (y + c1)) + c2)), (min(x, (y + fold(min((c0 - c2), c1)))) + c2), (c2 <= 0)) ||
 rewrite(min(min((y + c0), x), (min(x, (y + c1)) + c2)), min((y + c0), x), ((0 <= c2) && (c0 <= (c1 + c2)))) ||
 rewrite(min(min((y + c0), x), (min(z, y) + c1)), min(x, (min(z, y) + c1)), (c1 <= c0)) ||
 rewrite(min(min((z + min(x, y)), w), ((z + x) + c0)), min((z + min(x, y)), w), (0 <= c0)) ||
 rewrite(min(min((z + min(y, x)), w), ((z + x) + c0)), min((z + min(y, x)), w), (0 <= c0)) ||
 rewrite(min(min(((x + y) + c0), z), (x + min(w, y))), min(z, (x + min(w, y))), (0 <= c0)) ||
 rewrite(min(min(((x + y) + c0), z), (x + min(y, w))), min(z, (x + min(y, w))), (0 <= c0)) ||
 rewrite(min(min(((x + y) + c0), z), (y + x)), min((x + y), z), (0 <= c0)) ||
 rewrite(min(min(((x + y) + c0), z), (min(w, y) + x)), min(z, (min(w, y) + x)), (0 <= c0)) ||
 rewrite(min(min(((x + y) + c0), z), (min(y, w) + x)), min(z, (min(y, w) + x)), (0 <= c0)) ||
 rewrite(min(min(((y + z) + c0), x), (y + min(w, z))), min(x, (y + min(w, z))), (0 <= c0)) ||
 rewrite(min(min(((y + z) + c0), x), (y + min(z, w))), min(x, (y + min(z, w))), (0 <= c0)) ||
 rewrite(min(min(((y + z) + c0), x), (min(w, z) + y)), min(x, (min(w, z) + y)), (0 <= c0)) ||
 rewrite(min(min(((y + z) + c0), x), (min(z, w) + y)), min(x, (min(z, w) + y)), (0 <= c0)) ||
 rewrite(min(min((min(x, y) + z), w), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
 rewrite(min(min((min(y, c0) + c1), x), (min(y, c2) + c3)), min((min(y, c0) + c1), x), ((c1 <= c3) && ((c0 + c1) <= (c2 + c3)))) ||
 rewrite(min(min((min(y, x) + z), w), ((z + x) + c0)), min((min(y, x) + z), w), (0 <= c0)) ||
 rewrite(min(min((min(y, z) + c0), x), (z + c1)), min((min(y, z) + c0), x), (c0 <= c1)) ||
 rewrite(min(min((min(y, z) + w), x), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
 rewrite(min(min((min(z, y) + w), x), ((w + z) + c0)), min((min(z, y) + w), x), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (x + c0)), min(min(y, z), x), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (x + c0)), min(min(y, z), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (y + c0)), min(min(x, y), z), (0 <= c0)) ||
 rewrite(min(min(min(y, x), z), (x + c0)), min(min(y, x), z), (0 <= c0)) ||
 rewrite(min(min(min(y, x), z), (x + c0)), min(min(y, z), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(y, x), z), (y + c0)), min(min(x, z), y), (0 <= c0)) ||
 rewrite(min(min(min(y, (x + c0)), z), (x + c1)), min(min(y, z), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(min(z, min(x, y)), w), (x + c0)), min(min(min(z, y), w), x), (0 <= c0)) ||
 rewrite(min(min(min(z, min(y, x)), w), (x + c0)), min(min(z, min(y, x)), w), (0 <= c0)) ||
 rewrite(min(min(min(z, min(y, (x + c0))), w), (x + c1)), min(min(min(z, y), w), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(min(z, min((x + c0), y)), w), (x + c1)), min(min(min(z, y), w), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(min((x + c0), y), z), (x + c1)), min(min(y, z), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (x + c0)), min(min(min(x, y), z), w), (0 <= c0)) ||
 rewrite(min(min(min(min(y, x), z), w), (x + c0)), min(min(min(y, z), w), x), (0 <= c0)) ||
 rewrite(min(min(min(min(y, (x + c0)), z), w), (x + c1)), min(min(min(y, z), w), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(min(min((x + c0), y), z), w), (x + c1)), min(min(min(y, z), w), (x + c1)), (c1 <= c0)) ||
 rewrite(min(min(select(x, y, (y + c0)), z), (y + c0)), min((y + c0), z), (c0 <= 0)) ||
 rewrite(min(min(select(x, (y + c0), y), z), (y + c0)), min((y + c0), z), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(min(z, (x - y)), (w - y)), min((min(x, w) - y), z)) ||
 rewrite(min(min((x - y), z), (w - y)), min((min(x, w) - y), z)))))) ||
((a.node_type() == IRNodeType::Max) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(max(y, (x + c0)), (max((y + c1), x) + c0)), max(y, (c0 + x)), (0 <= (c0 + c1))) ||
 rewrite(min(max(y, min(x, c0)), (min(x, c1) + c2)), (min(x, c1) + c2), ((c2 <= 0) && ((c1 + c2) <= c0))) ||
 rewrite(min(max((x + c0), y), (max((y + c1), x) + c0)), max(y, (x + c0)), (0 <= (c0 + c1))) ||
 rewrite(min(max(min(x, c0), c1), (x + c2)), (min(fold((max(c0, c1) - c2)), x) + c2), ((((c1 <= c0) && (c2 <= 1)) || (c0 <= c1)) && (((c0 <= c1) || (c2 <= 0)) || (((c0 + c2) + -1) <= c1)))) ||
 rewrite(min(max(min(x, c0), c1), (min(x, y) + c2)), (min(fold((max(c0, c1) - c2)), min(x, y)) + c2), (((c2 <= 0) && (c1 <= c0)) || (c0 <= c1))) ||
 rewrite(min(max(min(x, c0), c1), (min(y, x) + c2)), (min(fold((max(c0, c1) - c2)), min(y, x)) + c2), (((c2 <= 0) && (c1 <= c0)) || (c0 <= c1))) ||
 rewrite(min(max(min(x, c0), y), (min(x, c1) + c2)), (min(x, c1) + c2), ((c2 <= 0) && ((c1 + c2) <= c0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(max(x, c0), min(x, c1)), min(x, c1)) ||
 rewrite(min(max(x, y), min(w, max(y, z))), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(x, y), min(w, max(z, y))), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(x, y), min(max(y, z), w)), min(max(y, min(x, z)), w)) ||
 rewrite(min(max(x, y), min(max(z, y), w)), min(max(y, min(x, z)), w)) ||
 rewrite(min(max(y, x), min(w, max(y, z))), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(y, x), min(w, max(z, y))), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(y, x), min(max(y, z), w)), min(max(y, min(x, z)), w)) ||
 rewrite(min(max(y, x), min(max(z, y), w)), min(max(y, min(x, z)), w)) ||
 rewrite(min(max(min(x, c0), c1), min(y, c0)), min(max(min(x, c0), c1), y), (c1 <= c0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(min(max(min(x, c0), c1), max(y, min(x, c2))), max(min(x, c0), min(y, c1)), ((((c1 <= c0) || (c2 <= c1)) || ((c2 + -1) <= c0)) && (((c0 <= c2) && (min(c1, c2) <= c0)) && ((min((c2 + 1), c1) + -2) <= c0)))) ||
 rewrite(min(max(min(x, c0), c1), max(min(x, c2), y)), max(min(x, c0), min(c1, y)), ((((c1 <= c0) || (c2 <= c1)) || ((c2 + -1) <= c0)) && (((c0 <= c2) && (min(c1, c2) <= c0)) && ((min((c2 + 1), c1) + -2) <= c0)))) ||
 rewrite(min(max(min(x, y), c0), max(min(x, c1), c2)), max(c0, min(min(x, c1), y)), (((((c0 <= c1) || (c2 <= c0)) && (min((c2 + -2), c1) <= c0)) || ((c2 + -1) <= c1)) && (((((c1 <= c0) || (c2 <= c1)) && (min((c2 + -2), c0) <= c1)) || ((c2 + -1) <= c0)) && ((c0 <= c2) && (c2 <= max(c0, c1)))))))))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite(min(select(x, y, (y + c0)), y), y, (0 <= c0)) ||
 rewrite(min(select(x, (y + c0), y), y), y, (0 <= c0)) ||
 rewrite(min(select(x, (min(y, c0) + c1), y), y), select(x, min(fold((c0 + c1)), y), y), (0 <= c1)) ||
 rewrite(min(select(x, min(y, z), z), z), select(x, min(y, z), z)) ||
 rewrite(min(select(x, min(z, y), z), z), select(x, min(z, y), z)) ||
 rewrite(min(select(x, max(y, z), y), y), y) ||
 rewrite(min(select(x, max(z, y), y), y), y) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(select(x, y, (y + c0)), (y + c0)), (y + c0), (c0 <= 0)) ||
 rewrite(min(select(x, (y + c0), y), (y + c0)), (y + c0), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(min(select((x < c0), c1, c2), select((x < c3), c1, c2)), select((x < c3), c1, c2), (((c0 <= c3) || (c2 <= c1)) && ((c1 <= c2) || (c3 <= c0))))))))