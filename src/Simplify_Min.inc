((b.node_type() == IRNodeType::Add) && (
 rewrite(min(x, ((max((x/c0), c1)*c0) + c2)), x, (((((1 <= c1) || (c1 <= -1)) && (c1 == 0)) || ((c0 + -1) <= c2)) && (((-1 <= (min(((c1 + 1)*c0), 0) + c2)) && (0 <= c2)) && (1 <= c0)))) ||
 rewrite(min(x, ((max((x/c0), y)*c0) + c1)), x, (((-1 <= (max(c1, 0) + c0)) && (1 <= c0)) && ((max(c0, 1) + -1) <= c1))) ||
 rewrite(min(x, (min(y, (x + c0)) + c1)), (min(y, (x + c0)) + c1), ((c0 + c1) <= 0)) ||
 rewrite(min(x, (min((x + c0), y) + c1)), min(x, (y + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(x, (min(min(x, y), z) + c0)), min(x, (min(y, z) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(min(x, y), z) + c0)), (min(min(x, y), z) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(x, y), z), w) + c0)), min(x, (min(min(y, z), w) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(min(min(x, y), z), w) + c0)), (min(min(min(x, y), z), w) + c0), (c0 <= 0)) ||
 rewrite(min(y, (((max(x, y)/c0)*c0) + c1)), y, ((-1 <= (c0 + c1)) && ((max(c0, 1) + -1) <= c1))) ||
 rewrite(min(y, (min(x, (y + c0)) + c0)), min(y, (x + c0)), (0 <= c0)) ||
 rewrite(min(y, (min(x, (y + c0)) + c1)), min(y, (x + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(y, (min(min(x, y), z) + c0)), min(y, (min(x, z) + c0)), (0 <= c0)) ||
 rewrite(min(y, (min(min(x, y), z) + c0)), (min(min(x, y), z) + c0), (c0 <= 0)) ||
 rewrite(min(y, (min(min(min(x, y), z), w) + c0)), (min(min(min(x, y), z), w) + c0), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(min(x, (max(((x + c0)/c1), c2)*c1)), x, (((((1 <= c2) || (c2 <= -1)) && (0 <= c2)) || ((c1 + -1) <= c0)) && ((1 <= c1) && (c2 <= 0)))) ||
 rewrite(min(x, (max(((x + c0)/c1), y)*c1)), x, ((1 <= c1) && ((c1 + -1) <= c0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(x, min(x, y)), min(x, y)) ||
 rewrite(min(x, min(y, x)), min(x, y)) ||
 rewrite(min(x, min(y, ((x/c0)*c0))), min(y, ((x/c0)*c0))) ||
 rewrite(min(x, min(y, (((x + c0)/c1)*c1))), min(x, y), ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(min(x, min(((x/c0)*c0), y)), min(((x/c0)*c0), y)) ||
 rewrite(min(x, min((((x + c0)/c1)*c1), y)), min(x, y), ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(min(y, min(w, min(z, (min(x, y) + c0)))), min(y, min(w, min(z, (x + c0)))), (0 <= c0)) ||
 rewrite(min(y, min(w, min(z, min(x, (y + c0))))), min(y, min(w, min(z, x))), (0 <= c0)) ||
 rewrite(min(y, min(w, min(z, min((y + c0), x)))), min(w, min(z, min(y, x))), (0 <= c0)) ||
 rewrite(min(y, min(w, min((min(x, y) + c0), z))), min(y, min(w, min((x + c0), z))), (0 <= c0)) ||
 rewrite(min(y, min(w, min(min(x, (y + c0)), z))), min(y, min(w, min(x, z))), (0 <= c0)) ||
 rewrite(min(y, min(w, min(min((y + c0), x), z))), min(y, min(w, min(x, z))), (0 <= c0)) ||
 rewrite(min(y, min(w, select(x, max(y, z), w))), min(w, y)) ||
 rewrite(min(y, min(w, select(x, max(z, y), w))), min(w, y)) ||
 rewrite(min(y, min(x, (y + c0))), min(x, y), (0 <= c0)) ||
 rewrite(min(y, min(x, (y + c0))), min(x, (y + c0)), (c0 <= 0)) ||
 rewrite(min(y, min(x, (min(y, z) + c0))), min(y, min(x, (z + c0))), (0 <= c0)) ||
 rewrite(min(y, min(x, (min(y, z) + c0))), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(y, min(x, (min((y + c0), z) + c1))), min(y, min(x, (z + c1))), (0 <= (c0 + c1))) ||
 rewrite(min(y, min(x, min(y, z))), min(y, min(x, z))) ||
 rewrite(min(y, min(x, min(z, y))), min(y, min(x, z))) ||
 rewrite(min(y, min(z, ((min(x, c0) + y) + c1))), min(z, (min(x, fold(min((0 - c1), c0))) + (y + c1)))) ||
 rewrite(min(y, min(z, min(x, (y + c0)))), min(y, min(z, x)), (0 <= c0)) ||
 rewrite(min(y, min(z, min(x, (y + c0)))), min(z, min(x, (y + c0))), (c0 <= 0)) ||
 rewrite(min(y, min(z, min((y + c0), x))), min(y, min(z, x)), (0 <= c0)) ||
 rewrite(min(y, min(z, min((y + c0), x))), min(z, min((y + c0), x)), (c0 <= 0)) ||
 rewrite(min(y, min((y + c0), x)), min(y, x), (0 <= c0)) ||
 rewrite(min(y, min((y + c0), x)), min((y + c0), x), (c0 <= 0)) ||
 rewrite(min(y, min(((min(x, c0) + y) + c1), z)), min(z, ((min(fold(min((0 - c1), c0)), x) + c1) + y))) ||
 rewrite(min(y, min((min(y, z) + c0), x)), min(y, min((z + c0), x)), (0 <= c0)) ||
 rewrite(min(y, min((min(y, z) + c0), x)), min((min(y, z) + c0), x), (c0 <= 0)) ||
 rewrite(min(y, min((min((y + c0), z) + c1), x)), min(y, min((z + c1), x)), (0 <= (c0 + c1))) ||
 rewrite(min(y, min(min(x, (y + c0)), z)), min(y, min(x, z)), (0 <= c0)) ||
 rewrite(min(y, min(min(x, (y + c0)), z)), min(min(x, (y + c0)), z), (c0 <= 0)) ||
 rewrite(min(y, min(min(y, z), x)), min(y, min(z, x))) ||
 rewrite(min(y, min(min(z, y), x)), min(y, min(z, x))) ||
 rewrite(min(y, min(min(z, (min(x, y) + c0)), w)), min(y, min(min(z, (x + c0)), w)), (0 <= c0)) ||
 rewrite(min(y, min(min(z, min(x, (y + c0))), w)), min(y, min(min(z, x), w)), (0 <= c0)) ||
 rewrite(min(y, min(min(z, min((y + c0), x)), w)), min(y, min(min(z, x), w)), (0 <= c0)) ||
 rewrite(min(y, min(min((y + c0), x), z)), min(y, min(x, z)), (0 <= c0)) ||
 rewrite(min(y, min(min((y + c0), x), z)), min(min((y + c0), x), z), (c0 <= 0)) ||
 rewrite(min(y, min(min((min(x, y) + c0), z), w)), min(y, min(min((x + c0), z), w)), (0 <= c0)) ||
 rewrite(min(y, min(min(min(x, (y + c0)), z), w)), min(y, min(min(x, z), w)), (0 <= c0)) ||
 rewrite(min(y, min(min(min((y + c0), x), z), w)), min(y, min(min(x, z), w)), (0 <= c0)) ||
 rewrite(min(y, min(select(x, max(y, z), w), w)), min(y, w)) ||
 rewrite(min(y, min(select(x, max(z, y), w), w)), min(y, w)) ||
 rewrite(min(z, min(x, (z + min(y, c0)))), min(x, (z + min(y, fold(min(c0, 0)))))) ||
 rewrite(min(z, min(x, (min(y, c0) + z))), min(x, (min(y, fold(min(c0, 0))) + z))) ||
 rewrite(min(z, min(x, (min(y, z) + c0))), min(z, min(x, (y + c0))), (0 <= c0)) ||
 rewrite(min(z, min(x, (min(y, z) + c0))), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(z, min(y, (w + (x + min(y, z))))), (min(0, (w + x)) + min(y, z))) ||
 rewrite(min(z, min(y, (w + (x + min(z, y))))), (min(0, (w + x)) + min(z, y))) ||
 rewrite(min(z, min(y, (w + (min(y, z) + x)))), (min(y, z) + min(0, (w + x)))) ||
 rewrite(min(z, min(y, (w + (min(z, y) + x)))), (min(y, z) + min(0, (w + x)))) ||
 rewrite(min(z, min(y, (x + min(y, z)))), (min(x, 0) + min(y, z))) ||
 rewrite(min(z, min(y, (x + min(z, y)))), (min(x, 0) + min(z, y))) ||
 rewrite(min(z, min(y, ((x + min(y, z)) + w))), (min(y, z) + min(0, (x + w)))) ||
 rewrite(min(z, min(y, ((x + min(z, y)) + w))), (min(y, z) + min(0, (x + w)))) ||
 rewrite(min(z, min(y, ((min(y, z) + x) + w))), (min(z, y) + min(0, (x + w)))) ||
 rewrite(min(z, min(y, ((min(z, y) + x) + w))), (min(z, y) + min(0, (x + w)))) ||
 rewrite(min(z, min(y, (min(y, z) + x))), (min(z, y) + min(0, x))) ||
 rewrite(min(z, min(y, (min(z, y) + x))), (min(z, y) + min(0, x))) ||
 rewrite(min(z, min(y, min(x, (w + min(y, z))))), min(x, (min(w, 0) + min(y, z)))) ||
 rewrite(min(z, min(y, min(x, (w + min(z, y))))), min(x, (min(w, 0) + min(z, y)))) ||
 rewrite(min(z, min(y, min(x, (min(y, z) + w)))), min(x, (min(y, z) + min(0, w)))) ||
 rewrite(min(z, min(y, min(x, (min(z, y) + w)))), min(x, (min(w, 0) + min(z, y)))) ||
 rewrite(min(z, min(y, min((w + min(y, z)), x))), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(z, min(y, min((w + min(z, y)), x))), min((min(w, 0) + min(z, y)), x)) ||
 rewrite(min(z, min(y, min((min(y, z) + w), x))), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(z, min(y, min((min(z, y) + w), x))), min((min(w, 0) + min(z, y)), x)) ||
 rewrite(min(z, min((w + (y + min(x, z))), x)), (min(0, (w + y)) + min(z, x))) ||
 rewrite(min(z, min((w + (y + min(z, x))), x)), (min(0, (w + y)) + min(z, x))) ||
 rewrite(min(z, min((w + (min(y, z) + x)), y)), (min(z, y) + min(0, (w + x)))) ||
 rewrite(min(z, min((w + (min(z, y) + x)), y)), (min(z, y) + min((w + x), 0))) ||
 rewrite(min(z, min((y + min(x, z)), x)), (min(y, 0) + min(x, z))) ||
 rewrite(min(z, min((y + min(z, x)), x)), (min(y, 0) + min(z, x))) ||
 rewrite(min(z, min((z + min(y, c0)), x)), min((z + min(y, fold(min(c0, 0)))), x)) ||
 rewrite(min(z, min(((y + min(x, z)) + w), x)), (min(z, x) + min(0, (y + w)))) ||
 rewrite(min(z, min(((y + min(z, x)) + w), x)), (min(z, x) + min((y + w), 0))) ||
 rewrite(min(z, min(((min(y, z) + x) + w), y)), (min(z, y) + min((x + w), 0))) ||
 rewrite(min(z, min(((min(z, y) + x) + w), y)), (min(z, y) + min((x + w), 0))) ||
 rewrite(min(z, min((min(y, c0) + z), x)), min(x, (min(fold(min(c0, 0)), y) + z))) ||
 rewrite(min(z, min((min(y, z) + c0), x)), min(z, min((y + c0), x)), (0 <= c0)) ||
 rewrite(min(z, min((min(y, z) + c0), x)), min((min(y, z) + c0), x), (c0 <= 0)) ||
 rewrite(min(z, min((min(y, z) + x), y)), (min(z, y) + min(x, 0))) ||
 rewrite(min(z, min((min(z, y) + x), y)), (min(z, y) + min(x, 0))) ||
 rewrite(min(z, min(min(x, (w + min(y, z))), y)), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(z, min(min(x, (w + min(z, y))), y)), min((min(w, 0) + min(z, y)), x)) ||
 rewrite(min(z, min(min(x, (min(y, z) + w)), y)), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(z, min(min(x, (min(z, y) + w)), y)), min(x, (min(z, y) + min(w, 0)))) ||
 rewrite(min(z, min(min((w + min(y, z)), x), y)), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(z, min(min((w + min(z, y)), x), y)), min((min(w, 0) + min(z, y)), x)) ||
 rewrite(min(z, min(min((min(y, z) + w), x), y)), min(x, (min(y, z) + min(w, 0)))) ||
 rewrite(min(z, min(min((min(z, y) + w), x), y)), min(x, (min(z, y) + min(w, 0)))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(min(y, select(x, (y + c0), y)), y, (0 <= c0)) ||
 rewrite(min(y, select(x, (min(y, c0) + c1), y)), select(x, min(y, fold((c0 + c1))), y), (0 <= c1)) ||
 rewrite(min(z, select(x, min(y, z), z)), select(x, min(y, z), z)) ||
 rewrite(min(z, select(x, min(z, y), z)), select(x, min(z, y), z)))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(min((((max(x, y)/c0)*c0) + c1), y), y, ((-1 <= (c0 + c1)) && ((max(c0, 1) + -1) <= c1))) ||
 rewrite(min(((max((x/c0), c1)*c0) + c2), x), x, (((((1 <= c1) || (c1 <= -1)) && (c1 == 0)) || ((c0 + -1) <= c2)) && (((-1 <= (min(((c1 + 1)*c0), 0) + c2)) && (0 <= c2)) && (1 <= c0)))) ||
 rewrite(min(((max((x/c0), y)*c0) + c1), x), x, (((-1 <= (max(c1, 0) + c0)) && (1 <= c0)) && ((max(c0, 1) + -1) <= c1))) ||
 rewrite(min((min(x, (y + c0)) + c0), y), min((x + c0), y), (0 <= c0)) ||
 rewrite(min((min(y, (x + c0)) + c1), x), min((y + c1), x), (0 <= (c0 + c1))) ||
 rewrite(min((min(y, (x + c0)) + c1), x), (min(y, (x + c0)) + c1), ((c0 + c1) <= 0)) ||
 rewrite(min((min((x + c0), y) + c1), x), min((y + c1), x), (0 <= (c0 + c1))) ||
 rewrite(min((min(min(x, y), z) + c0), x), min((min(y, z) + c0), x), (0 <= c0)) ||
 rewrite(min((min(min(x, y), z) + c0), x), (min(min(x, y), z) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(y, x), z) + c0), x), min((min(y, z) + c0), x), (0 <= c0)) ||
 rewrite(min((min(min(y, x), z) + c0), x), (min(min(y, x), z) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(min(x, y), z), w) + c0), x), min((min(min(y, z), w) + c0), x), (0 <= c0)) ||
 rewrite(min((min(min(min(x, y), z), w) + c0), x), (min(min(min(x, y), z), w) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(min(x, y), z), w) + c0), y), (min(min(min(x, y), z), w) + c0), (c0 <= 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((x + y), (z + (w + x))), (x + min(y, (w + z)))) ||
 rewrite(min((x + y), (z + (x + w))), (x + min(y, (z + w)))) ||
 rewrite(min((x + y), ((w + x) + z)), (min(y, (w + z)) + x)) ||
 rewrite(min((x + y), ((x + w) + z)), (x + min((z + w), y))) ||
 rewrite(min((x + (y + (w + z))), (u + w)), (min((x + (y + z)), u) + w)) ||
 rewrite(min((x + (y + (w + z))), (w + u)), (min((x + (y + z)), u) + w)) ||
 rewrite(min((x + (y + (z + w))), (u + w)), (min((x + (y + z)), u) + w)) ||
 rewrite(min((x + (y + (z + w))), (w + u)), (min((x + (y + z)), u) + w)) ||
 rewrite(min((x + (z + y)), (w + y)), (min((x + z), w) + y)) ||
 rewrite(min((x + (z + y)), (w + z)), (min((x + y), w) + z)) ||
 rewrite(min((x + (z + y)), (y + w)), (min((x + z), w) + y)) ||
 rewrite(min((x + (z + y)), (z + w)), (min((x + y), w) + z)) ||
 rewrite(min((x + ((w + z) + y)), (u + w)), (min((x + (z + y)), u) + w)) ||
 rewrite(min((x + ((w + z) + y)), (w + u)), (min((x + (z + y)), u) + w)) ||
 rewrite(min((x + ((z + w) + y)), (u + w)), (min((x + (z + y)), u) + w)) ||
 rewrite(min((x + ((z + w) + y)), (w + u)), (min((x + (z + y)), u) + w)) ||
 rewrite(min((y + x), (u + (z + (w + x)))), (x + min(y, (z + (w + u))))) ||
 rewrite(min((y + x), (u + (z + (w + y)))), (y + min(x, (z + (w + u))))) ||
 rewrite(min((y + x), (u + (z + (x + w)))), (x + min(y, (z + (u + w))))) ||
 rewrite(min((y + x), (u + (z + (y + w)))), (y + min(x, (z + (u + w))))) ||
 rewrite(min((y + x), (u + ((w + x) + z))), (x + min(y, ((w + u) + z)))) ||
 rewrite(min((y + x), (u + ((w + y) + z))), (y + min(x, ((w + u) + z)))) ||
 rewrite(min((y + x), (u + ((x + w) + z))), (x + min(y, ((u + w) + z)))) ||
 rewrite(min((y + x), (u + ((y + w) + z))), (y + min(x, ((u + w) + z)))) ||
 rewrite(min((y + x), (z + (w + x))), (min((w + z), y) + x)) ||
 rewrite(min((y + x), (z + (x + w))), (min((z + w), y) + x)) ||
 rewrite(min((y + x), ((w + x) + z)), (min(y, (w + z)) + x)) ||
 rewrite(min((y + x), ((x + w) + z)), (min((w + z), y) + x)) ||
 rewrite(min((y + x), ((z + (w + x)) + u)), (min(y, (z + (w + u))) + x)) ||
 rewrite(min((y + x), ((z + (w + y)) + u)), (min(x, (z + (w + u))) + y)) ||
 rewrite(min((y + x), ((z + (x + w)) + u)), (min(y, ((z + u) + w)) + x)) ||
 rewrite(min((y + x), ((z + (y + w)) + u)), (min(x, ((z + u) + w)) + y)) ||
 rewrite(min((y + x), (((w + x) + z) + u)), (min(y, ((w + u) + z)) + x)) ||
 rewrite(min((y + x), (((w + y) + z) + u)), (min(x, ((w + u) + z)) + y)) ||
 rewrite(min((y + x), (((x + w) + z) + u)), (min(y, ((u + w) + z)) + x)) ||
 rewrite(min((y + x), (((y + w) + z) + u)), (y + min(((u + w) + z), x))) ||
 rewrite(min((z + (x + y)), (w + (u + x))), (min((z + y), (w + u)) + x)) ||
 rewrite(min((z + (x + y)), (w + (x + u))), (min((z + y), (w + u)) + x)) ||
 rewrite(min((z + (x + y)), ((u + x) + w)), (min((z + y), (u + w)) + x)) ||
 rewrite(min((z + (x + y)), ((x + u) + w)), (min((z + y), (w + u)) + x)) ||
 rewrite(min((z + (y + x)), (w + (u + x))), (min((z + y), (w + u)) + x)) ||
 rewrite(min((z + (y + x)), (w + (x + u))), (min((z + y), (w + u)) + x)) ||
 rewrite(min((z + (y + x)), ((u + x) + w)), (min((z + y), (u + w)) + x)) ||
 rewrite(min((z + (y + x)), ((x + u) + w)), (min((z + y), (w + u)) + x)) ||
 rewrite(min(((y + (w + z)) + x), (u + w)), (min(((y + z) + x), u) + w)) ||
 rewrite(min(((y + (w + z)) + x), (w + u)), (min(((y + z) + x), u) + w)) ||
 rewrite(min(((y + (z + w)) + x), (u + w)), (min(((y + z) + x), u) + w)) ||
 rewrite(min(((y + (z + w)) + x), (w + u)), (min(((y + z) + x), u) + w)) ||
 rewrite(min(((z + y) + x), (u + (w + y))), (min((z + x), (u + w)) + y)) ||
 rewrite(min(((z + y) + x), (u + (w + z))), (min((y + x), (u + w)) + z)) ||
 rewrite(min(((z + y) + x), (u + (y + w))), (min((z + x), (u + w)) + y)) ||
 rewrite(min(((z + y) + x), (u + (z + w))), (min((y + x), (u + w)) + z)) ||
 rewrite(min(((z + y) + x), (w + y)), (min((z + x), w) + y)) ||
 rewrite(min(((z + y) + x), (w + z)), (min((y + x), w) + z)) ||
 rewrite(min(((z + y) + x), (y + w)), (min((z + x), w) + y)) ||
 rewrite(min(((z + y) + x), (z + w)), (min((y + x), w) + z)) ||
 rewrite(min(((z + y) + x), ((w + y) + u)), (min((z + x), (w + u)) + y)) ||
 rewrite(min(((z + y) + x), ((w + z) + u)), (min((y + x), (w + u)) + z)) ||
 rewrite(min(((z + y) + x), ((y + w) + u)), (min((z + x), (u + w)) + y)) ||
 rewrite(min(((z + y) + x), ((z + w) + u)), (min((y + x), (u + w)) + z)) ||
 rewrite(min((((z + y) + x) + w), (u + y)), (min(((z + x) + w), u) + y)) ||
 rewrite(min((((z + y) + x) + w), (u + z)), (min(((y + x) + w), u) + z)) ||
 rewrite(min((((z + y) + x) + w), (y + u)), (min(((z + x) + w), u) + y)) ||
 rewrite(min((((z + y) + x) + w), (z + u)), (min(((y + x) + w), u) + z)) ||
 rewrite(min((min(x, (y + z)) + c0), (y + z)), min((x + c0), (y + z)), (0 <= c0)) ||
 rewrite(min((min(x, (y + z)) + c0), (z + y)), min((x + c0), (z + y)), (0 <= c0)) ||
 rewrite(min((min((x + y), z) + c0), (x + y)), min((z + c0), (x + y)), (0 <= c0)) ||
 rewrite(min((min((x + y), z) + c0), (y + x)), min((z + c0), (y + x)), (0 <= c0)))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite(min((x - y), (w + (z - y))), (min(x, (w + z)) - y)) ||
 rewrite(min((x - y), ((z - y) + w)), (min((z + w), x) - y)))))) ||
((a.node_type() == IRNodeType::Mul) && (
 rewrite(min((min(x, c0)*c0), c1), min((x*c0), c1), ((1 <= c0) && (c1 <= (c0*c0)))) ||
 rewrite(min((min(x, c0)*c1), c2), min((x*c1), c2), ((((1 <= c0) || (c0 <= -1)) || (c2 <= 0)) && ((1 <= c1) && (c2 <= (c0*c1))))) ||
 rewrite(min((max(((x + c0)/c1), c2)*c1), x), x, (((((1 <= c2) || (c2 <= -1)) && (0 <= c2)) || ((c1 + -1) <= c0)) && ((1 <= c1) && (c2 <= 0)))) ||
 rewrite(min((max(((x + c0)/c1), y)*c1), x), x, ((1 <= c1) && ((c1 + -1) <= c0))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(((x/c0)*c0), (x + c1)), (x + c1), (((c1 <= 0) && ((c1 + -1) <= c0)) && ((c0 + c1) <= 1))) ||
 rewrite(min((((x + c0)/c1)*c1), (x + c2)), (x + c2), (((c2 + -1) <= (c0 + c1)) && (((c1 + c2) + -1) <= c0))))))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite(min(min(w, min(z, (min(x, y) + c0))), y), min(min(w, min(z, (x + c0))), y), (0 <= c0)) ||
 rewrite(min(min(w, min(z, min(y, (x + c0)))), x), min(w, min(z, min(y, x))), (0 <= c0)) ||
 rewrite(min(min(w, min(z, min((y + c0), x))), y), min(w, min(z, min(y, x))), (0 <= c0)) ||
 rewrite(min(min(w, min((min(x, y) + c0), z)), y), min(min(w, min((x + c0), z)), y), (0 <= c0)) ||
 rewrite(min(min(w, min(min(y, (x + c0)), z)), x), min(w, min(min(y, x), z)), (0 <= c0)) ||
 rewrite(min(min(w, min(min((y + c0), x), z)), y), min(w, min(min(y, x), z)), (0 <= c0)) ||
 rewrite(min(min(w, select(x, max(y, z), w)), y), min(w, y)) ||
 rewrite(min(min(w, select(x, max(z, y), w)), y), min(w, y)) ||
 rewrite(min(min(x, y), x), min(y, x)) ||
 rewrite(min(min(x, (z + min(y, c0))), z), min(x, (min(y, fold(min(c0, 0))) + z))) ||
 rewrite(min(min(x, (min(y, c0) + c1)), c0), min((min(y, fold((c0 - max(c1, 0)))) + c1), x)) ||
 rewrite(min(min(x, (min(y, c0) + c1)), c2), min((min(y, fold(min((c2 - c1), c0))) + c1), x), (((c2 + 1) <= (c0 + c1)) || ((c0 + c1) <= c2))) ||
 rewrite(min(min(x, (min(y, c0) + z)), z), min((min(y, fold(min(c0, 0))) + z), x)) ||
 rewrite(min(min(x, (min(y, z) + c0)), z), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min((y + c0), z) + c1)), y), min(min(x, (z + c1)), y), (0 <= (c0 + c1))) ||
 rewrite(min(min(x, min(y, z)), y), min(x, min(y, z))) ||
 rewrite(min(min(x, min(z, y)), y), min(x, min(z, y))) ||
 rewrite(min(min(y, x), x), min(y, x)) ||
 rewrite(min(min(y, (w + (x + min(y, z)))), z), (min(0, (w + x)) + min(y, z))) ||
 rewrite(min(min(y, (w + (x + min(z, y)))), z), (min(0, (w + x)) + min(z, y))) ||
 rewrite(min(min(y, (w + (min(y, z) + x))), z), (min(y, z) + min(0, (w + x)))) ||
 rewrite(min(min(y, (w + (min(z, y) + x))), z), (min(y, z) + min(0, (w + x)))) ||
 rewrite(min(min(y, (x + c0)), x), min(y, x), (0 <= c0)) ||
 rewrite(min(min(y, (x + c0)), x), min(y, (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, (x + min(y, z))), z), (min(x, 0) + min(y, z))) ||
 rewrite(min(min(y, (x + min(z, y))), z), (min(x, 0) + min(z, y))) ||
 rewrite(min(min(y, ((x + min(y, z)) + w)), z), (min(y, z) + min(0, (x + w)))) ||
 rewrite(min(min(y, ((x + min(z, y)) + w)), z), (min(y, z) + min(0, (x + w)))) ||
 rewrite(min(min(y, ((min(y, z) + x) + w)), z), (min(y, z) + min(0, (x + w)))) ||
 rewrite(min(min(y, ((min(z, y) + x) + w)), z), (min(y, z) + min(0, (x + w)))) ||
 rewrite(min(min(y, (min(y, z) + x)), z), (min(z, y) + min(0, x))) ||
 rewrite(min(min(y, (min(z, y) + x)), z), (min(y, z) + min(x, 0))) ||
 rewrite(min(min(y, ((x/c0)*c0)), x), min(y, ((x/c0)*c0))) ||
 rewrite(min(min(y, (((x + c0)/c1)*c1)), x), min(y, x), ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(min(min(y, min(x, (w + min(y, z)))), z), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(min(y, min(x, (w + min(z, y)))), z), min((min(w, 0) + min(z, y)), x)) ||
 rewrite(min(min(y, min(x, (min(y, z) + w))), z), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(min(y, min(x, (min(z, y) + w))), z), min((min(y, z) + min(0, w)), x)) ||
 rewrite(min(min(y, min((w + min(y, z)), x)), z), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(min(y, min((w + min(z, y)), x)), z), min((min(w, 0) + min(z, y)), x)) ||
 rewrite(min(min(y, min((min(y, z) + w), x)), z), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(min(y, min((min(z, y) + w), x)), z), min((min(w, 0) + min(z, y)), x)) ||
 rewrite(min(min(z, ((min(x, c0) + y) + c1)), y), min((min(x, fold(min((0 - c1), c0))) + (y + c1)), z)) ||
 rewrite(min(min(z, (min(x, y) + c0)), x), min(min(z, (y + c0)), x), (0 <= c0)) ||
 rewrite(min(min(z, (min(x, y) + c0)), x), min(z, (min(x, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, (min(x, y) + c0)), y), min(min(z, (x + c0)), y), (0 <= c0)) ||
 rewrite(min(min(z, min(y, (x + c0))), x), min(z, min(y, x)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, (x + c0))), x), min(z, min(y, (x + c0))), (c0 <= 0)) ||
 rewrite(min(min(z, min((y + c0), x)), y), min(z, min(y, x)), (0 <= c0)) ||
 rewrite(min(min(z, min((y + c0), x)), y), min(z, min((y + c0), x)), (c0 <= 0)) ||
 rewrite(min(min((w + (z + min(x, y))), x), y), (min(0, (w + z)) + min(x, y))) ||
 rewrite(min(min((w + (z + min(y, x))), x), y), (min(0, (w + z)) + min(y, x))) ||
 rewrite(min(min((w + (min(y, z) + x)), y), z), (min((w + x), 0) + min(y, z))) ||
 rewrite(min(min((w + (min(z, y) + x)), y), z), (min((w + x), 0) + min(y, z))) ||
 rewrite(min(min((y + c0), x), y), min(x, y), (0 <= c0)) ||
 rewrite(min(min((y + c0), x), y), min((y + c0), x), (c0 <= 0)) ||
 rewrite(min(min((z + min(x, y)), x), y), (min(z, 0) + min(x, y))) ||
 rewrite(min(min((z + min(y, c0)), x), z), min(x, (min(fold(min(c0, 0)), y) + z))) ||
 rewrite(min(min((z + min(y, x)), x), y), (min(z, 0) + min(y, x))) ||
 rewrite(min(min(((z + min(x, y)) + w), x), y), (min((z + w), 0) + min(x, y))) ||
 rewrite(min(min(((z + min(y, x)) + w), x), y), (min((z + w), 0) + min(x, y))) ||
 rewrite(min(min(((min(x, c0) + y) + c1), z), y), min(((min(fold(min((0 - c1), c0)), x) + c1) + y), z)) ||
 rewrite(min(min(((min(y, z) + x) + w), y), z), (min(0, (w + x)) + min(y, z))) ||
 rewrite(min(min(((min(z, y) + x) + w), y), z), (min(z, y) + min((x + w), 0))) ||
 rewrite(min(min((min(x, c0) + c1), y), c0), min(y, (min(x, fold((c0 - max(c1, 0)))) + c1))) ||
 rewrite(min(min((min(x, c0) + c1), y), c2), min((min(fold(min((c2 - c1), c0)), x) + c1), y), (((c2 + 1) <= (c0 + c1)) || ((c0 + c1) <= c2))) ||
 rewrite(min(min((min(y, c0) + z), x), z), min((min(fold(min(c0, 0)), y) + z), x)) ||
 rewrite(min(min((min(y, z) + c0), x), y), min(min((z + c0), x), y), (0 <= c0)) ||
 rewrite(min(min((min(y, z) + c0), x), y), min((min(y, z) + c0), x), (c0 <= 0)) ||
 rewrite(min(min((min(y, z) + c0), x), z), min(min((y + c0), x), z), (0 <= c0)) ||
 rewrite(min(min((min(y, z) + c0), x), z), min((min(y, z) + c0), x), (c0 <= 0)) ||
 rewrite(min(min((min(y, z) + x), y), z), (min(y, z) + min(x, 0))) ||
 rewrite(min(min((min(z, y) + x), y), z), (min(z, y) + min(x, 0))) ||
 rewrite(min(min((min((y + c0), z) + c1), x), y), min(min((z + c1), x), y), (0 <= (c0 + c1))) ||
 rewrite(min(min(((x/c0)*c0), y), x), min(((x/c0)*c0), y)) ||
 rewrite(min(min((((x + c0)/c1)*c1), y), x), min(x, y), ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(min(min(min(x, (w + min(y, z))), y), z), min((min(w, 0) + min(y, z)), x)) ||
 rewrite(min(min(min(x, (w + min(z, y))), y), z), min((min(w, 0) + min(z, y)), x)) ||
 rewrite(min(min(min(x, (min(y, z) + w)), y), z), min(x, (min(y, z) + min(w, 0)))) ||
 rewrite(min(min(min(x, (min(z, y) + w)), y), z), min(x, (min(z, y) + min(w, 0)))) ||
 rewrite(min(min(min(y, z), x), y), min(min(y, z), x)) ||
 rewrite(min(min(min(y, (x + c0)), z), x), min(min(y, x), z), (0 <= c0)) ||
 rewrite(min(min(min(y, (x + c0)), z), x), min(min(y, (x + c0)), z), (c0 <= 0)) ||
 rewrite(min(min(min(z, y), x), y), min(min(z, y), x)) ||
 rewrite(min(min(min(z, (min(x, y) + c0)), w), y), min(min(min(z, (x + c0)), w), y), (0 <= c0)) ||
 rewrite(min(min(min(z, min(y, (x + c0))), w), x), min(min(min(z, y), w), x), (0 <= c0)) ||
 rewrite(min(min(min(z, min((y + c0), x)), w), y), min(min(z, min(y, x)), w), (0 <= c0)) ||
 rewrite(min(min(min((y + c0), x), z), y), min(min(x, z), y), (0 <= c0)) ||
 rewrite(min(min(min((y + c0), x), z), y), min(min((y + c0), x), z), (c0 <= 0)) ||
 rewrite(min(min(min((z + min(x, y)), w), x), y), min((min(z, 0) + min(x, y)), w)) ||
 rewrite(min(min(min((z + min(y, x)), w), x), y), min((min(z, 0) + min(y, x)), w)) ||
 rewrite(min(min(min((min(x, y) + c0), z), w), y), min(min(min((x + c0), z), w), y), (0 <= c0)) ||
 rewrite(min(min(min((min(y, z) + w), x), y), z), min((min(y, z) + min(w, 0)), x)) ||
 rewrite(min(min(min((min(z, y) + w), x), y), z), min((min(z, y) + min(w, 0)), x)) ||
 rewrite(min(min(min((x*c0), y), (z*c0)), c1), min(min((min(x, z)*c0), y), c1), (1 <= c0)) ||
 rewrite(min(min(min(min(y, (x + c0)), z), w), x), min(min(min(y, x), z), w), (0 <= c0)) ||
 rewrite(min(min(min(min((y + c0), x), z), w), y), min(min(min(y, x), z), w), (0 <= c0)) ||
 rewrite(min(min(min(max((x + y), c0), z), y), c1), min(min(y, z), c1), (c1 <= c0)) ||
 rewrite(min(min(select(x, max(y, z), w), w), y), min(w, y)) ||
 rewrite(min(min(select(x, max(z, y), w), w), y), min(w, y)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(min(w, min(z, (y + x))), (u + x)), min(min(w, (min(u, y) + x)), z)) ||
 rewrite(min(min(w, min(z, (y + x))), (u + y)), min(min(w, (min(u, x) + y)), z)) ||
 rewrite(min(min(w, min(z, (y + x))), (x + u)), min(min(w, (min(u, y) + x)), z)) ||
 rewrite(min(min(w, min(z, (y + x))), (y + u)), min(min(w, (min(x, u) + y)), z)) ||
 rewrite(min(min(w, min(z, min(x, y))), (y + c0)), min(min(w, min(z, x)), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(w, min(z, min(y, x))), (x + c0)), min(w, min(z, min(y, x))), (0 <= c0)) ||
 rewrite(min(min(w, min(z, min(y, x))), (y + c0)), min(w, min(z, min(y, x))), (0 <= c0)) ||
 rewrite(min(min(w, min(z, min(y, x))), (y + c0)), min(min(w, min(z, x)), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(w, min((z + y), x)), (u + y)), min(min((min(u, z) + y), x), w)) ||
 rewrite(min(min(w, min((z + y), x)), (u + z)), min(min(w, x), (min(y, u) + z))) ||
 rewrite(min(min(w, min((z + y), x)), (y + u)), min(min(w, (min(z, u) + y)), x)) ||
 rewrite(min(min(w, min((z + y), x)), (z + u)), min(min(w, (min(u, y) + z)), x)) ||
 rewrite(min(min(w, min(min(x, y), z)), (y + c0)), min(min(w, min(x, z)), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(w, min(min(y, x), z)), (x + c0)), min(w, min(min(y, z), x)), (0 <= c0)) ||
 rewrite(min(min(w, min(min(y, x), z)), (y + c0)), min(w, min(min(x, z), y)), (0 <= c0)) ||
 rewrite(min(min(w, min(min(y, x), z)), (y + c0)), min(min(w, min(x, z)), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), ((min(z, c0) + x) + c1)), min(y, ((min(z, c0) + x) + c1)), ((c0 + c1) <= 0)) ||
 rewrite(min(min(x, y), (min(x, (z + c0)) + c1)), min(y, (min(x, (z + c0)) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(z, (y + c0)) + c1)), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(min(x, y), (min((x + c0), z) + c1)), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(min(x, y), (min(min(z, y), w) + c0)), min(x, (min(min(z, y), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (y + (w + z))), (u + z)), min(x, (min((w + y), u) + z))) ||
 rewrite(min(min(x, (y + (w + z))), (z + u)), min(x, (min((w + y), u) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (u + z)), min(x, (min((y + w), u) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (z + u)), min(x, (min((y + w), u) + z))) ||
 rewrite(min(min(x, ((z + y) + w)), (u + y)), min(x, (min((z + w), u) + y))) ||
 rewrite(min(min(x, ((z + y) + w)), (u + z)), min(x, (min((y + w), u) + z))) ||
 rewrite(min(min(x, ((z + y) + w)), (y + u)), min((min(u, (z + w)) + y), x)) ||
 rewrite(min(min(x, ((z + y) + w)), (z + u)), min((min(u, (w + y)) + z), x)) ||
 rewrite(min(min(x, (min(y, (z + w)) + c0)), (w + z)), min(min(x, (y + c0)), (w + z)), (0 <= c0)) ||
 rewrite(min(min(y, x), (x + c0)), min(y, x), (0 <= c0)) ||
 rewrite(min(min(y, x), (x + c0)), min(y, (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (y + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(y, x), (y + c0)), min(x, (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (z + min(x, y))), (min(z, 0) + min(x, y))) ||
 rewrite(min(min(y, x), (z + min(y, x))), (min(z, 0) + min(y, x))) ||
 rewrite(min(min(y, x), ((min(z, c0) + x) + c1)), min(y, ((min(z, c0) + x) + c1)), ((c0 + c1) <= 0)) ||
 rewrite(min(min(y, x), (min(x, y) + z)), (min(y, x) + min(0, z))) ||
 rewrite(min(min(y, x), (min(x, z) + c0)), min(min(y, x), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(y, x), (min(x, z) + c0)), min(y, (min(x, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(x, (z + c0)) + c1)), min(y, (min(x, (z + c0)) + c1)), (c1 <= 0)) ||
 rewrite(min(min(y, x), (min(y, x) + z)), (min(y, x) + min(0, z))) ||
 rewrite(min(min(y, x), (min(y, z) + c0)), min(min(y, x), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(y, x), (min(y, z) + c0)), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(z, x) + c0)), min(min(y, x), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(y, x), (min(z, x) + c0)), min(y, (min(z, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(z, y) + c0)), min(min(y, x), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(y, x), (min(z, y) + c0)), min(x, (min(z, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(z, (y + c0)) + c1)), min(min(y, x), (z + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(min(y, x), (min((x + c0), z) + c1)), min(min(y, x), (z + c1)), (0 <= (c0 + c1))) ||
 rewrite(min(min(y, x), (min(min(x, z), w) + c0)), min(y, (min(min(x, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(min(y, z), w) + c0)), min(x, (min(min(y, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, x), (min(min(z, x), w) + c0)), min(min(y, x), (min(z, w) + c0)), (0 <= c0)) ||
 rewrite(min(min(y, x), (min(min(z, y), w) + c0)), min(min(y, x), (min(z, w) + c0)), (0 <= c0)) ||
 rewrite(min(min(y, x), (min(min(z, y), w) + c0)), min(x, (min(min(z, y), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(y, (x + c0)), (min(x, z) + c1)), min((min(x, (fold(max((c1 - c0), -1)) + z)) + c0), y), (c0 <= c1)) ||
 rewrite(min(min(y, (x + c0)), (min(z, x) + c1)), min((min((fold(max((c1 - c0), -1)) + z), x) + c0), y), (c0 <= c1)) ||
 rewrite(min(min(y, (x*c0)), ((z*c0) + c0)), min((min(x, (z + 1))*c0), y), (1 <= c0)) ||
 rewrite(min(min(z, (y + x)), (u + (w + x))), min(z, (x + min((u + w), y)))) ||
 rewrite(min(min(z, (y + x)), (u + (w + y))), min(z, (y + min((u + w), x)))) ||
 rewrite(min(min(z, (y + x)), (u + (x + w))), min((min(y, (u + w)) + x), z)) ||
 rewrite(min(min(z, (y + x)), (u + (y + w))), min(z, (y + min((u + w), x)))) ||
 rewrite(min(min(z, (y + x)), ((w + x) + u)), min((min(y, (u + w)) + x), z)) ||
 rewrite(min(min(z, (y + x)), ((w + y) + u)), min((min(x, (w + u)) + y), z)) ||
 rewrite(min(min(z, (y + x)), ((x + w) + u)), min((min(y, (w + u)) + x), z)) ||
 rewrite(min(min(z, (y + x)), ((y + w) + u)), min((min(x, (w + u)) + y), z)) ||
 rewrite(min(min(z, min(x, y)), (min(w, y) + c0)), min(min(z, min(x, y)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(z, min(x, y)), (min(w, y) + c0)), min(min(z, x), (min(w, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, min(y, x)), (x + c0)), min(z, min(y, x)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, x)), (x + c0)), min(min(z, y), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, min(y, x)), (y + c0)), min(z, min(x, y)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, x)), (y + c0)), min(min(z, x), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, min(y, x)), (min(w, y) + c0)), min(min(z, min(y, x)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, x)), (min(w, y) + c0)), min(min(z, x), (min(w, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, min(y, x)), (min(x, w) + c0)), min(min(z, min(y, x)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, x)), (min(x, w) + c0)), min(min(z, y), (min(x, w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(z, min(y, x)), (min(y, w) + c0)), min(min(z, min(y, x)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(z, min(y, x)), (min(y, w) + c0)), min(min(z, x), (min(y, w) + c0)), (c0 <= 0)) ||
 rewrite(min(min((y + c0), x), (min(y, z) + c1)), min((min((z + fold(max((c1 - c0), -1))), y) + c0), x), (c0 <= c1)) ||
 rewrite(min(min((y + c0), x), (min(z, y) + c1)), min((min((z + fold(max((c1 - c0), -1))), y) + c0), x), (c0 <= c1)) ||
 rewrite(min(min((z + y), x), (u + (w + y))), min((min((u + w), z) + y), x)) ||
 rewrite(min(min((z + y), x), (u + (w + z))), min((min((w + u), y) + z), x)) ||
 rewrite(min(min((z + y), x), (u + (y + w))), min((min((u + w), z) + y), x)) ||
 rewrite(min(min((z + y), x), (u + (z + w))), min((min((w + u), y) + z), x)) ||
 rewrite(min(min((z + y), x), ((w + y) + u)), min((min((w + u), z) + y), x)) ||
 rewrite(min(min((z + y), x), ((w + z) + u)), min((min((w + u), y) + z), x)) ||
 rewrite(min(min((z + y), x), ((y + w) + u)), min((min((w + u), z) + y), x)) ||
 rewrite(min(min((z + y), x), ((z + w) + u)), min((min((w + u), y) + z), x)) ||
 rewrite(min(min((z + (y + x)), w), (u + x)), min((min((z + y), u) + x), w)) ||
 rewrite(min(min((z + (y + x)), w), (u + y)), min((min((z + x), u) + y), w)) ||
 rewrite(min(min((z + (y + x)), w), (x + u)), min((min(u, (y + z)) + x), w)) ||
 rewrite(min(min((z + (y + x)), w), (y + u)), min((min(u, (z + x)) + y), w)) ||
 rewrite(min(min(((z + y) + w), x), (u + z)), min((min((y + w), u) + z), x)) ||
 rewrite(min(min(((z + y) + w), x), (z + u)), min((min((w + y), u) + z), x)) ||
 rewrite(min(min(((z + y) + x), w), (u + y)), min(w, (min((z + x), u) + y))) ||
 rewrite(min(min(((z + y) + x), w), (y + u)), min((min((z + x), u) + y), w)) ||
 rewrite(min(min((min(y, (z + w)) + c0), x), (w + z)), min(min((y + c0), x), (w + z)), (0 <= c0)) ||
 rewrite(min(min((y*c0), x), ((z*c0) + c0)), min((min((z + 1), y)*c0), x), (1 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(w, y) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(w, y) + c0)), min(min(x, z), (min(w, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(y, x), z), (x + c0)), min(min(y, x), z), (0 <= c0)) ||
 rewrite(min(min(min(y, x), z), (x + c0)), min(min(y, z), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(y, x), z), (y + c0)), min(min(y, x), z), (0 <= c0)) ||
 rewrite(min(min(min(y, x), z), (y + c0)), min(min(x, z), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(y, x), z), (min(w, y) + c0)), min(min(min(y, x), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(y, x), z), (min(w, y) + c0)), min(min(x, z), (min(w, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(y, x), z), (min(x, w) + c0)), min(min(min(y, x), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(y, x), z), (min(x, w) + c0)), min(min(y, z), (min(x, w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(y, x), z), (min(y, w) + c0)), min(min(min(y, x), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(y, x), z), (min(y, w) + c0)), min(min(x, z), (min(y, w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(z, (y + x)), w), (u + x)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min(z, (y + x)), w), (u + y)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min(z, (y + x)), w), (x + u)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min(z, (y + x)), w), (y + u)), min(min((min(u, x) + y), w), z)) ||
 rewrite(min(min(min(z, min(x, y)), w), (y + c0)), min(min(min(z, x), w), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(z, min(y, x)), w), (x + c0)), min(min(z, min(y, x)), w), (0 <= c0)) ||
 rewrite(min(min(min(z, min(y, x)), w), (y + c0)), min(min(z, min(y, x)), w), (0 <= c0)) ||
 rewrite(min(min(min(z, min(y, x)), w), (y + c0)), min(min(min(z, x), w), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min((z + y), x), w), (u + y)), min(min((min(u, z) + y), x), w)) ||
 rewrite(min(min(min((z + y), x), w), (u + z)), min(min((min(u, y) + z), w), x)) ||
 rewrite(min(min(min((z + y), x), w), (y + u)), min(min((min(u, z) + y), x), w)) ||
 rewrite(min(min(min((z + y), x), w), (z + u)), min(min((min(u, y) + z), w), x)) ||
 rewrite(min(min(min(min(x, y), z), w), (y + c0)), min(min(min(x, z), w), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(y, x), z), w), (x + c0)), min(min(min(y, x), z), w), (0 <= c0)) ||
 rewrite(min(min(min(min(y, x), z), w), (y + c0)), min(min(min(y, x), z), w), (0 <= c0)) ||
 rewrite(min(min(min(min(y, x), z), w), (y + c0)), min(min(min(x, z), w), (y + c0)), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(min(z, (x - y)), (w - y)), min((min(w, x) - y), z)) ||
 rewrite(min(min((x - y), z), (w - y)), min((min(w, x) - y), z)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(min(min(x, ((y*c0) + c1)), (z*c0)), min(x, (min((y + c0), z)*c0)), (((0 <= c1) && ((c0*c0) == c1)) && (1 <= c0))) ||
 rewrite(min(min(y, ((x*c0) + c0)), (z*c0)), min(y, (min((x + 1), z)*c0)), (1 <= c0)) ||
 rewrite(min(min(y, (x*c0)), ((x + z)*c0)), min(y, ((min(z, 0) + x)*c0)), (1 <= c0)) ||
 rewrite(min(min(y, (x*c0)), ((z + x)*c0)), min(y, ((min(z, 0) + x)*c0)), (1 <= c0)) ||
 rewrite(min(min(y, (x*c0)), (min(z, c0)*c0)), min(y, (min(x, min(z, c0))*c0)), ((1 <= c0) && (c0 <= 18))) ||
 rewrite(min(min(y, (x*c0)), (min(z, c1)*c0)), min(y, (min(x, min(z, c1))*c0)), (((((0 <= c1) && (1 <= c0)) && (c1 <= 1)) && ((c0*c1) <= c0)) && ((c0*c1) <= (c0*2)))) ||
 rewrite(min(min(y, (x*c0)), (min(z, x)*c0)), min(y, (min(z, x)*c0)), (1 <= c0)) ||
 rewrite(min(min(z, ((x + y)*c0)), (x*c0)), min(z, ((x + min(0, y))*c0)), (1 <= c0)) ||
 rewrite(min(min(z, ((x + y)*c0)), (y*c0)), min(((min(x, 0) + y)*c0), z), (1 <= c0)) ||
 rewrite(min(min(((y*c0) + c0), x), (z*c0)), min((min((y + 1), z)*c0), x), (1 <= c0)) ||
 rewrite(min(min(((y*c0) + c1), x), (z*c0)), min((min((y + c0), z)*c0), x), (((0 <= c1) && ((c0*c0) == c1)) && (1 <= c0))) ||
 rewrite(min(min((x*c0), y), ((x + z)*c0)), min(y, ((min(z, 0) + x)*c0)), (1 <= c0)) ||
 rewrite(min(min((x*c0), y), ((z + x)*c0)), min(y, ((min(z, 0) + x)*c0)), (1 <= c0)) ||
 rewrite(min(min((x*c0), y), (min(z, c0)*c0)), min((min(c0, min(z, x))*c0), y), ((1 <= c0) && (c0 <= 18))) ||
 rewrite(min(min((x*c0), (y + c1)), (z*c0)), min((min(z, x)*c0), (y + c1)), (1 <= c0)) ||
 rewrite(min(min((y*c0), x), (min(z, c1)*c0)), min((min(y, min(z, c1))*c0), x), (((((0 <= c1) && (1 <= c0)) && (c1 <= 1)) && ((c0*c1) <= c0)) && ((c0*c1) <= (c0*2)))) ||
 rewrite(min(min((y*c0), x), (min(z, y)*c0)), min(x, (min(z, y)*c0)), (1 <= c0)) ||
 rewrite(min(min(((y + z)*c0), x), (y*c0)), min(x, ((y + min(0, z))*c0)), (1 <= c0)) ||
 rewrite(min(min(((y + z)*c0), x), (z*c0)), min(x, ((z + min(0, y))*c0)), (1 <= c0)))))) ||
((a.node_type() == IRNodeType::Max) && (
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(max(x, y), min(w, max(y, z))), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(x, y), min(w, max(z, y))), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(x, y), min(max(y, z), w)), min(max(y, min(x, z)), w)) ||
 rewrite(min(max(x, y), min(max(z, y), w)), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(y, x), min(w, max(y, z))), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(y, x), min(w, max(z, y))), min(max(y, min(z, x)), w)) ||
 rewrite(min(max(y, x), min(max(y, z), w)), min(max(y, min(x, z)), w)) ||
 rewrite(min(max(y, x), min(max(z, y), w)), min(max(y, min(x, z)), w)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(min(max(min(x, c0), c1), max(min(x, c2), c3)), max(min(x, c2), c3), (((c2 <= c1) || (c3 <= c0)) && ((c3 <= c1) && (c2 <= max(c0, c1))))))))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite(min(select(x, (y + c0), y), y), y, (0 <= c0)) ||
 rewrite(min(select(x, (min(y, c0) + c1), y), y), select(x, min(fold((c0 + c1)), y), y), (0 <= c1)) ||
 rewrite(min(select(x, min(y, c0), c1), c1), select(x, min(y, c1), c1), (c1 <= c0)) ||
 rewrite(min(select(x, min(y, z), z), z), select(x, min(y, z), z)) ||
 rewrite(min(select(x, min(z, y), z), z), select(x, min(y, z), z)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(select(x, y, (y + c0)), (y + c0)), (y - select(x, fold((0 - min(c0, 0))), fold((0 - c0))))) ||
 rewrite(min(select(x, y, (y + c0)), (y + c1)), (y - select(x, fold((0 - min(c1, 0))), fold((0 - c0)))), (c0 <= c1)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(select(x, y, z), min(z, select(x, w, z))), min(select(x, min(w, y), z), z)) ||
 rewrite(min(select(x, y, z), min(select(x, w, z), z)), min(select(x, min(y, w), z), z)) ||
 rewrite(min(select(x, (y + c0), y), min(y, z)), min(y, z), (0 <= c0)) ||
 rewrite(min(select(x, (y + c0), y), min(z, y)), min(y, z), (0 <= c0))))))