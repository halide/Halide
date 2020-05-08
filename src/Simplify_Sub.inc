((b.node_type() == IRNodeType::Add) && (
 rewrite((c0 - (x + (y*c1))), ((y*fold((c0 - c1))) - x), (c0 == 0)) ||
 rewrite((c0 - (x + select(y, c1, c2))), (select(y, fold((c0 - c1)), fold((c0 - c2))) - x)) ||
 rewrite((c0 - ((x*c1) + y)), ((x*fold((c0 - c1))) - y), (c0 == 0)) ||
 rewrite((c0 - (select(x, c1, c2) + y)), (select(x, fold((c0 - c1)), fold((c0 - c2))) - y)) ||
 rewrite((x - (y + (x + z))), (0 - (y + z))) ||
 rewrite((x - (y + (z + x))), (0 - (y + z))) ||
 rewrite((x - (y + (z + (w + x)))), (0 - (y + (z + w)))) ||
 rewrite((x - (y + (z + (x + w)))), (0 - (y + (z + w)))) ||
 rewrite((x - (y + ((x + z) + w))), (0 - (y + (z + w)))) ||
 rewrite((x - (y + ((z + x) + w))), (0 - (y + (z + w)))) ||
 rewrite((x - (y + select(z, (w + c0), (x + c1)))), (select(z, (x - (w - fold((0 - c0)))), fold((0 - c1))) - y)) ||
 rewrite((x - ((x + y) + z)), (0 - (y + z))) ||
 rewrite((x - ((y + x) + z)), (0 - (y + z))) ||
 rewrite((x - ((y + (x + z)) + w)), (0 - ((y + z) + w))) ||
 rewrite((x - ((y + (z + x)) + w)), (0 - ((y + z) + w))) ||
 rewrite((x - (((x + y) + z) + w)), (0 - ((y + z) + w))) ||
 rewrite((x - (((y + x) + z) + w)), (0 - ((y + z) + w))) ||
 rewrite((x - (select(y, (z + c0), (x + c1)) + w)), (select(y, ((x - z) + fold((0 - c0))), fold((0 - c1))) - w)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((c0 - (x*c1)), (x*fold((c0 - c1))), (c0 == 0)) ||
 rewrite((c0 - ((x - y)*c1)), (((y - x)*fold(((c0 + c1) - c0))) + c0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((x - min(y, (z + (x - (w + u))))), max((x - y), ((w + u) - z))) ||
 rewrite((x - min(y, (z + min(w, min(u, max(v, w)))))), (x - min(y, (z + min(w, u))))) ||
 rewrite((x - min(y, (z + min(w, min(u, max(w, v)))))), (x - min(y, (z + min(w, u))))) ||
 rewrite((x - min(y, (z + min(w, min(max(u, w), v))))), (x - min(y, (z + min(w, v))))) ||
 rewrite((x - min(y, (z + min(w, min(max(w, u), v))))), (x - min(y, (z + min(w, v))))) ||
 rewrite((x - min(y, (z + min(min(w, max(u, v)), u)))), (x - min(y, (z + min(w, u))))) ||
 rewrite((x - min(y, (z + min(min(w, max(u, v)), v)))), (x - min(y, (z + min(w, v))))) ||
 rewrite((x - min(y, (z + min(min(max(w, u), v), u)))), (x - min(y, (z + min(u, v))))) ||
 rewrite((x - min(y, (z + min(min(max(w, u), v), w)))), (x - min(y, (z + min(w, v))))) ||
 rewrite((x - min(y, ((x - (z + w)) + u))), max((x - y), ((z + w) - u))) ||
 rewrite((x - min(y, (min(z, min(w, max(u, z))) + v))), (x - min(y, (min(z, w) + v)))) ||
 rewrite((x - min(y, (min(z, min(w, max(z, u))) + v))), (x - min(y, (min(z, w) + v)))) ||
 rewrite((x - min(y, (min(z, min(max(w, z), u)) + v))), (x - min(y, (min(z, u) + v)))) ||
 rewrite((x - min(y, (min(z, min(max(z, w), u)) + v))), (x - min(y, (min(z, u) + v)))) ||
 rewrite((x - min(y, (min(min(z, max(w, u)), u) + v))), (x - min(y, (min(z, u) + v)))) ||
 rewrite((x - min(y, (min(min(z, max(w, u)), w) + v))), (x - min(y, (min(z, w) + v)))) ||
 rewrite((x - min(y, (min(min(max(z, w), u), w) + v))), (x - min(y, (min(w, u) + v)))) ||
 rewrite((x - min(y, (min(min(max(z, w), u), z) + v))), (x - min(y, (min(z, u) + v)))) ||
 rewrite((x - min(y, (x - z))), max(z, (x - y))) ||
 rewrite((x - min(y, min(z, (x - w)))), max(w, (x - min(z, y)))) ||
 rewrite((x - min(y, min((x - z), w))), max(z, (x - min(y, w)))) ||
 rewrite((x - min((y + (x - (z + w))), u)), max((z - (y - w)), (x - u))) ||
 rewrite((x - min((y + min(z, min(w, max(u, z)))), v)), (x - min((y + min(z, w)), v))) ||
 rewrite((x - min((y + min(z, min(w, max(z, u)))), v)), (x - min((y + min(z, w)), v))) ||
 rewrite((x - min((y + min(z, min(max(w, z), u))), v)), (x - min((y + min(z, u)), v))) ||
 rewrite((x - min((y + min(z, min(max(z, w), u))), v)), (x - min((y + min(z, u)), v))) ||
 rewrite((x - min((y + min(min(z, max(w, u)), u)), v)), (x - min((y + min(z, u)), v))) ||
 rewrite((x - min((y + min(min(z, max(w, u)), w)), v)), (x - min((y + min(z, w)), v))) ||
 rewrite((x - min((y + min(min(max(z, w), u), w)), v)), (x - min((y + min(w, u)), v))) ||
 rewrite((x - min((y + min(min(max(z, w), u), z)), v)), (x - min((y + min(z, u)), v))) ||
 rewrite((x - min(((x - (y + z)) + w), u)), max((z - (w - y)), (x - u))) ||
 rewrite((x - min((min(y, min(z, max(w, y))) + u), v)), (x - min((min(y, z) + u), v))) ||
 rewrite((x - min((min(y, min(z, max(y, w))) + u), v)), (x - min((min(y, z) + u), v))) ||
 rewrite((x - min((min(y, min(max(y, z), w)) + u), v)), (x - min((min(y, w) + u), v))) ||
 rewrite((x - min((min(y, min(max(z, y), w)) + u), v)), (x - min((min(y, w) + u), v))) ||
 rewrite((x - min((min(min(y, max(z, w)), w) + u), v)), (x - min((min(y, w) + u), v))) ||
 rewrite((x - min((min(min(y, max(z, w)), z) + u), v)), (x - min((min(y, z) + u), v))) ||
 rewrite((x - min((min(min(max(y, z), w), y) + u), v)), (x - min((min(y, w) + u), v))) ||
 rewrite((x - min((min(min(max(y, z), w), z) + u), v)), (x - min((min(z, w) + u), v))) ||
 rewrite((x - min((x - y), z)), max(y, (x - z))) ||
 rewrite((x - min(min(y, (x - z)), w)), max(z, (x - min(y, w)))) ||
 rewrite((x - min(min(y, min(z, w)), (min(w, y) - u))), (x - min(z, (min(w, y) - max(u, 0))))) ||
 rewrite((x - min(min(y, min(z, w)), (min(y, w) - u))), (x - min(z, (min(w, y) - max(u, 0))))) ||
 rewrite((x - min(min(y, min(z, w)), (min(y, z) - u))), (x - min(w, (min(z, y) - max(u, 0))))) ||
 rewrite((x - min(min(y, min(z, w)), (min(z, y) - u))), (x - min(w, (min(z, y) - max(u, 0))))) ||
 rewrite((x - min(min((x - y), z), w)), max(y, (x - min(w, z)))) ||
 rewrite((x - min(min(min(y, z), w), (min(w, y) - u))), (x - min(z, (min(y, w) - max(u, 0))))) ||
 rewrite((x - min(min(min(y, z), w), (min(w, z) - u))), (x - min(y, (min(z, w) - max(u, 0))))) ||
 rewrite((x - min(min(min(y, z), w), (min(y, w) - u))), (x - min(z, (min(y, w) - max(u, 0))))) ||
 rewrite((x - min(min(min(y, z), w), (min(z, w) - u))), (x - min(y, (min(z, w) - max(u, 0))))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((x - max(y, (x - z))), min(z, (x - y))) ||
 rewrite((x - max((x - y), z)), min(y, (x - z))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((c0 - select(x, c1, (y + c2))), select(x, fold((c0 - c1)), (fold((c0 - c2)) - y))) ||
 rewrite((c0 - select(x, c1, (y - z))), select(x, fold((c0 - c1)), (z - y)), (c0 == 0)))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + (y + min(z, (w + u)))) - u), (x + (y + min(w, (z - u))))) ||
 rewrite(((x + (y + min(z, (w + u)))) - w), ((x + min(u, (z - w))) + y)) ||
 rewrite(((x + (y + min(z, (w - u)))) - w), (x + (y - max(u, (w - z))))) ||
 rewrite(((x + (y + min((z + w), u))) - w), (x + (y + min((u - w), z)))) ||
 rewrite(((x + (y + min((z + w), u))) - z), (x + (y + min(w, (u - z))))) ||
 rewrite(((x + (y + min((z - w), u))) - z), (x + (y - max(w, (z - u))))) ||
 rewrite(((x + (min(y, (z + w)) + u)) - w), (x + (min(z, (y - w)) + u))) ||
 rewrite(((x + (min(y, (z + w)) + u)) - z), ((x + min((y - z), w)) + u)) ||
 rewrite(((x + (min(y, (z - w)) + u)) - z), (x - (max(w, (z - y)) - u))) ||
 rewrite(((x + (min((y + z), w) + u)) - y), (x + (min((w - y), z) + u))) ||
 rewrite(((x + (min((y + z), w) + u)) - z), (x + (min((w - z), y) + u))) ||
 rewrite(((x + (min((y - z), w) + u)) - y), (x - (max((y - w), z) - u))) ||
 rewrite(((x + min(y, (z + w))) - w), (x + min(z, (y - w)))) ||
 rewrite(((x + min(y, (z + w))) - z), (min(w, (y - z)) + x)) ||
 rewrite(((x + min(y, (z + (w + u)))) - u), (min((y - u), (w + z)) + x)) ||
 rewrite(((x + min(y, (z + (w + u)))) - w), (min((y - w), (u + z)) + x)) ||
 rewrite(((x + min(y, ((z + w) + u))) - w), (x + min((y - w), (u + z)))) ||
 rewrite(((x + min(y, ((z + w) + u))) - z), (x + min((y - z), (u + w)))) ||
 rewrite(((x + min(y, ((z + w) - u))) - w), (min((y - w), (z - u)) + x)) ||
 rewrite(((x + min(y, ((z + w) - u))) - z), (min((y - z), (w - u)) + x)) ||
 rewrite(((x + min((y + z), w)) - y), (x + min(z, (w - y)))) ||
 rewrite(((x + min((y + z), w)) - z), (x + min((w - z), y))) ||
 rewrite(((x + min((y + (z + w)), u)) - w), (x + min((u - w), (z + y)))) ||
 rewrite(((x + min((y + (z + w)), u)) - z), (x + min((u - z), (w + y)))) ||
 rewrite(((x + min(((y + z) + w), u)) - y), (x + min((u - y), (z + w)))) ||
 rewrite(((x + min(((y + z) + w), u)) - z), (x + min((u - z), (y + w)))) ||
 rewrite(((x + min(((y + z) - w), u)) - y), (x + min((z - w), (u - y)))) ||
 rewrite(((x + min(((y + z) - w), u)) - z), (x + min((y - w), (u - z)))) ||
 rewrite((((x + min(y, (z + w))) + u) - w), ((x + min(z, (y - w))) + u)) ||
 rewrite((((x + min(y, (z + w))) + u) - z), ((min(w, (y - z)) + u) + x)) ||
 rewrite((((x + min(y, (z - w))) + u) - z), ((x - max(w, (z - y))) + u)) ||
 rewrite((((x + min((y + z), w)) + u) - y), (x + (min(z, (w - y)) + u))) ||
 rewrite((((x + min((y + z), w)) + u) - z), ((x + min((w - z), y)) + u)) ||
 rewrite((((x + min((y - z), w)) + u) - y), ((x - max((y - w), z)) + u)) ||
 rewrite((((min(x, (y + z)) + w) + u) - y), ((min(z, (x - y)) + w) + u)) ||
 rewrite((((min(x, (y + z)) + w) + u) - z), ((min(y, (x - z)) + w) + u)) ||
 rewrite((((min(x, (y - z)) + w) + u) - y), (w - (max(z, (y - x)) - u))) ||
 rewrite((((min((x + y), z) + w) + u) - x), ((min((z - x), y) + w) + u)) ||
 rewrite((((min((x + y), z) + w) + u) - y), ((min(x, (z - y)) + w) + u)) ||
 rewrite((((min((x - y), z) + w) + u) - x), (w - (max((x - z), y) - u))) ||
 rewrite(((min(x, (y + z)) + w) - y), (min(z, (x - y)) + w)) ||
 rewrite(((min(x, (y + z)) + w) - z), (min(y, (x - z)) + w)) ||
 rewrite(((min(x, (y + (z + w))) + u) - w), (min((x - w), (z + y)) + u)) ||
 rewrite(((min(x, (y + (z + w))) + u) - z), (min((x - z), (w + y)) + u)) ||
 rewrite(((min(x, ((y + z) + w)) + u) - y), (min((x - y), (z + w)) + u)) ||
 rewrite(((min(x, ((y + z) + w)) + u) - z), (min((x - z), (y + w)) + u)) ||
 rewrite(((min(x, ((y + z) - w)) + u) - y), (min((x - y), (z - w)) + u)) ||
 rewrite(((min(x, ((y + z) - w)) + u) - z), (min((x - z), (y - w)) + u)) ||
 rewrite(((min((x + y), z) + w) - x), (min((z - x), y) + w)) ||
 rewrite(((min((x + y), z) + w) - y), (min(x, (z - y)) + w)) ||
 rewrite(((min((x + (y + z)), w) + u) - y), (min((w - y), (z + x)) + u)) ||
 rewrite(((min((x + (y + z)), w) + u) - z), (min((w - z), (y + x)) + u)) ||
 rewrite(((min(((x + y) + z), w) + u) - x), (u + min((w - x), (y + z)))) ||
 rewrite(((min(((x + y) + z), w) + u) - y), (u + min((w - y), (x + z)))) ||
 rewrite(((min(((x + y) - z), w) + u) - x), (u + min((y - z), (w - x)))) ||
 rewrite(((min(((x + y) - z), w) + u) - y), (u + min((x - z), (w - y)))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x + y) - (z + (w + x))), (y - (z + w))) ||
 rewrite(((x + y) - (z + (w + y))), (x - (z + w))) ||
 rewrite(((x + y) - (z + (w + (u + x)))), (y - (z + (w + u)))) ||
 rewrite(((x + y) - (z + (w + (u + y)))), (x - (z + (w + u)))) ||
 rewrite(((x + y) - (z + (w + (u + (v + x))))), (y - (z + (w + (u + v))))) ||
 rewrite(((x + y) - (z + (w + (u + (v + y))))), (x - (z + (w + (u + v))))) ||
 rewrite(((x + y) - (z + (w + (u + (x + v))))), (y - (z + (w + (u + v))))) ||
 rewrite(((x + y) - (z + (w + (u + (y + v))))), (x - (z + (w + (u + v))))) ||
 rewrite(((x + y) - (z + (w + (x + u)))), (y - (z + (w + u)))) ||
 rewrite(((x + y) - (z + (w + (y + u)))), (x - (z + (w + u)))) ||
 rewrite(((x + y) - (z + (w + ((u + x) + v)))), (y - (z + (w + (u + v))))) ||
 rewrite(((x + y) - (z + (w + ((u + y) + v)))), (x - (z + (w + (u + v))))) ||
 rewrite(((x + y) - (z + (w + ((x + u) + v)))), (y - (z + (w + (u + v))))) ||
 rewrite(((x + y) - (z + (w + ((y + u) + v)))), (x - (z + (w + (u + v))))) ||
 rewrite(((x + y) - (z + (x + w))), (y - (z + w))) ||
 rewrite(((x + y) - (z + (y + w))), (x - (z + w))) ||
 rewrite(((x + y) - (z + ((w + x) + u))), (y - (z + (w + u)))) ||
 rewrite(((x + y) - (z + ((w + y) + u))), (x - (z + (w + u)))) ||
 rewrite(((x + y) - (z + ((w + (u + x)) + v))), (y - (z + ((w + u) + v)))) ||
 rewrite(((x + y) - (z + ((w + (u + y)) + v))), (x - (z + ((w + u) + v)))) ||
 rewrite(((x + y) - (z + ((w + (x + u)) + v))), (y - (z + ((w + u) + v)))) ||
 rewrite(((x + y) - (z + ((w + (y + u)) + v))), (x - (z + ((w + u) + v)))) ||
 rewrite(((x + y) - (z + ((x + w) + u))), (y - (z + (w + u)))) ||
 rewrite(((x + y) - (z + ((y + w) + u))), (x - (z + (w + u)))) ||
 rewrite(((x + y) - (z + (((w + x) + u) + v))), (y - (z + ((w + u) + v)))) ||
 rewrite(((x + y) - (z + (((w + y) + u) + v))), (x - (z + ((w + u) + v)))) ||
 rewrite(((x + y) - (z + (((x + w) + u) + v))), (y - (z + ((w + u) + v)))) ||
 rewrite(((x + y) - (z + (((y + w) + u) + v))), (x - (z + ((w + u) + v)))) ||
 rewrite(((x + y) - (z + (x - w))), (y - (z - w))) ||
 rewrite(((x + y) - (z + (y - w))), (x - (z - w))) ||
 rewrite(((x + y) - (z + ((w + x) - u))), (y - (z + (w - u)))) ||
 rewrite(((x + y) - (z + ((w + y) - u))), (x - (z + (w - u)))) ||
 rewrite(((x + y) - (z + ((x + w) - u))), (y - (z + (w - u)))) ||
 rewrite(((x + y) - (z + ((y + w) - u))), (x - (z + (w - u)))) ||
 rewrite(((x + y) - (z + min(w, (u + x)))), (y - (z + min(u, (w - x))))) ||
 rewrite(((x + y) - (z + min(w, (u + y)))), (x - (z + min(u, (w - y))))) ||
 rewrite(((x + y) - (z + min(w, (x + u)))), (y - (z + min(u, (w - x))))) ||
 rewrite(((x + y) - (z + min(w, (y + u)))), ((x - z) - min(u, (w - y)))) ||
 rewrite(((x + y) - (z + min(w, (x - u)))), (y - (z - max(u, (x - w))))) ||
 rewrite(((x + y) - (z + min(w, (y - u)))), (x - (z - max(u, (y - w))))) ||
 rewrite(((x + y) - (z + min((w + x), u))), (y - (z + min((u - x), w)))) ||
 rewrite(((x + y) - (z + min((w + y), u))), (x - (z + min((u - y), w)))) ||
 rewrite(((x + y) - (z + min((x + w), u))), (y - (z + min((u - x), w)))) ||
 rewrite(((x + y) - (z + min((y + w), u))), (x - (z + min((u - y), w)))) ||
 rewrite(((x + y) - (z + min((x - w), u))), (y - (z - max((x - u), w)))) ||
 rewrite(((x + y) - (z + min((y - w), u))), (x - (z - max((y - u), w)))) ||
 rewrite(((x + y) - ((x + z) + w)), (y - (z + w))) ||
 rewrite(((x + y) - ((y + z) + w)), (x - (z + w))) ||
 rewrite(((x + y) - ((z + x) + w)), (y - (z + w))) ||
 rewrite(((x + y) - ((z + y) + w)), (x - (z + w))) ||
 rewrite(((x + y) - ((z + (w + x)) + u)), (y - ((z + w) + u))) ||
 rewrite(((x + y) - ((z + (w + y)) + u)), (x - ((z + w) + u))) ||
 rewrite(((x + y) - ((z + (w + (u + x))) + v)), (y - ((z + (w + u)) + v))) ||
 rewrite(((x + y) - ((z + (w + (u + y))) + v)), (x - ((z + (w + u)) + v))) ||
 rewrite(((x + y) - ((z + (w + (x + u))) + v)), (y - ((z + (w + u)) + v))) ||
 rewrite(((x + y) - ((z + (w + (y + u))) + v)), (x - ((z + (w + u)) + v))) ||
 rewrite(((x + y) - ((z + (x + w)) + u)), (y - ((z + w) + u))) ||
 rewrite(((x + y) - ((z + (y + w)) + u)), (x - ((z + w) + u))) ||
 rewrite(((x + y) - ((z + ((w + x) + u)) + v)), (y - ((z + (w + u)) + v))) ||
 rewrite(((x + y) - ((z + ((w + y) + u)) + v)), (x - ((z + (w + u)) + v))) ||
 rewrite(((x + y) - ((z + ((x + w) + u)) + v)), (y - ((z + (w + u)) + v))) ||
 rewrite(((x + y) - ((z + ((y + w) + u)) + v)), (x - ((z + (w + u)) + v))) ||
 rewrite(((x + y) - (((x + z) + w) + u)), (y - ((z + w) + u))) ||
 rewrite(((x + y) - (((y + z) + w) + u)), (x - ((z + w) + u))) ||
 rewrite(((x + y) - (((z + x) + w) + u)), (y - ((z + w) + u))) ||
 rewrite(((x + y) - (((z + y) + w) + u)), (x - ((z + w) + u))) ||
 rewrite(((x + y) - (((z + (w + x)) + u) + v)), (y - (((z + w) + u) + v))) ||
 rewrite(((x + y) - (((z + (w + y)) + u) + v)), (x - (((z + w) + u) + v))) ||
 rewrite(((x + y) - (((z + (x + w)) + u) + v)), (y - (((z + w) + u) + v))) ||
 rewrite(((x + y) - (((z + (y + w)) + u) + v)), (x - (((z + w) + u) + v))) ||
 rewrite(((x + y) - ((((x + z) + w) + u) + v)), (y - (((z + w) + u) + v))) ||
 rewrite(((x + y) - ((((y + z) + w) + u) + v)), (x - (((z + w) + u) + v))) ||
 rewrite(((x + y) - ((((z + x) + w) + u) + v)), (y - (((z + w) + u) + v))) ||
 rewrite(((x + y) - ((((z + y) + w) + u) + v)), (x - (((z + w) + u) + v))) ||
 rewrite(((x + y) - ((x - z) + w)), (y + (z - w))) ||
 rewrite(((x + y) - ((y - z) + w)), (x + (z - w))) ||
 rewrite(((x + y) - (((x + z) - w) + u)), (y - ((z - w) + u))) ||
 rewrite(((x + y) - (((y + z) - w) + u)), (x - ((z - w) + u))) ||
 rewrite(((x + y) - (((z + x) - w) + u)), (y - ((z - w) + u))) ||
 rewrite(((x + y) - (((z + y) - w) + u)), (x - ((z - w) + u))) ||
 rewrite(((x + y) - (min(z, (w + x)) + u)), (y - (min(w, (z - x)) + u))) ||
 rewrite(((x + y) - (min(z, (w + y)) + u)), (x - (min(w, (z - y)) + u))) ||
 rewrite(((x + y) - (min(z, (x + w)) + u)), ((y - min((z - x), w)) - u)) ||
 rewrite(((x + y) - (min(z, (y + w)) + u)), ((x - u) - min((z - y), w))) ||
 rewrite(((x + y) - (min(z, (x - w)) + u)), (y + (max(w, (x - z)) - u))) ||
 rewrite(((x + y) - (min(z, (y - w)) + u)), (x + (max(w, (y - z)) - u))) ||
 rewrite(((x + y) - (min((x + z), w) + u)), (y - (min((w - x), z) + u))) ||
 rewrite(((x + y) - (min((y + z), w) + u)), (x - (min((w - y), z) + u))) ||
 rewrite(((x + y) - (min((z + x), w) + u)), (y - (min((w - x), z) + u))) ||
 rewrite(((x + y) - (min((z + y), w) + u)), (x - (min((w - y), z) + u))) ||
 rewrite(((x + y) - (min((x - z), w) + u)), (y + (max((x - w), z) - u))) ||
 rewrite(((x + y) - (min((y - z), w) + u)), (x + (max((y - w), z) - u))) ||
 rewrite(((x + (y + z)) - (w + y)), (x + (z - w))) ||
 rewrite(((x + (y + z)) - (w + z)), (x + (y - w))) ||
 rewrite(((x + (y + z)) - (y + w)), (x + (z - w))) ||
 rewrite(((x + (y + z)) - (z + w)), (x + (y - w))) ||
 rewrite(((x + (y + (z + w))) - (u + w)), ((x + (y + z)) - u)) ||
 rewrite(((x + (y + (z + w))) - (u + z)), ((x + (y + w)) - u)) ||
 rewrite(((x + (y + (z + w))) - (w + u)), ((x + (y + z)) - u)) ||
 rewrite(((x + (y + (z + w))) - (z + u)), ((x + (y + w)) - u)) ||
 rewrite(((x + (y + (z*c0))) - (w + (u*c1))), (((z - u)*c0) - (w - (y + x))), (c0 == c1)) ||
 rewrite(((x + (y + (z*c0))) - ((w*c1) + u)), (((z - w)*c0) + ((y + x) - u)), (c0 == c1)) ||
 rewrite(((x + (y + (z*w))) - (u + (v*w))), ((x + (w*(z - v))) - (u - y))) ||
 rewrite(((x + (y + (z*w))) - (u + (v*z))), (x + ((z*(w - v)) - (u - y)))) ||
 rewrite(((x + (y + (z*w))) - (u + (w*v))), ((x + (w*(z - v))) - (u - y))) ||
 rewrite(((x + (y + (z*w))) - (u + (z*v))), (x + ((z*(w - v)) - (u - y)))) ||
 rewrite(((x + (y + (z*w))) - ((u*w) + v)), ((w*(z - u)) + ((y + x) - v))) ||
 rewrite(((x + (y + (z*w))) - ((u*z) + v)), ((z*(w - u)) + ((y + x) - v))) ||
 rewrite(((x + (y + (z*w))) - ((w*u) + v)), ((w*(z - u)) + ((y + x) - v))) ||
 rewrite(((x + (y + (z*w))) - ((z*u) + v)), ((z*(w - u)) + ((y + x) - v))) ||
 rewrite(((x + (y + min(z, (w + u)))) - (u + v)), ((x + (y + min(w, (z - u)))) - v)) ||
 rewrite(((x + (y + min(z, (w + u)))) - (v + u)), ((x + min(w, (z - u))) - (v - y))) ||
 rewrite(((x + (y + min(z, (w + u)))) - (v + w)), ((x + min(u, (z - w))) - (v - y))) ||
 rewrite(((x + (y + min(z, (w + u)))) - (w + v)), ((x + min(u, (z - w))) - (v - y))) ||
 rewrite(((x + (y + min(z, (w - u)))) - (v + w)), ((x + (y - max(u, (w - z)))) - v)) ||
 rewrite(((x + (y + min(z, (w - u)))) - (w + v)), ((x + (y - max(u, (w - z)))) - v)) ||
 rewrite(((x + (y + min((z + w), u))) - (v + w)), ((x + (y + min((u - w), z))) - v)) ||
 rewrite(((x + (y + min((z + w), u))) - (v + z)), (x + (y + (min(w, (u - z)) - v)))) ||
 rewrite(((x + (y + min((z + w), u))) - (w + v)), ((x + (y + min((u - w), z))) - v)) ||
 rewrite(((x + (y + min((z + w), u))) - (z + v)), ((x + (y + min((u - z), w))) - v)) ||
 rewrite(((x + (y + min((z - w), u))) - (v + z)), ((x - max(w, (z - u))) - (v - y))) ||
 rewrite(((x + (y + min((z - w), u))) - (z + v)), ((x - max(w, (z - u))) + (y - v))) ||
 rewrite(((x + ((y + z) + w)) - (u + y)), (x + ((z + w) - u))) ||
 rewrite(((x + ((y + z) + w)) - (u + z)), (x + ((y + w) - u))) ||
 rewrite(((x + ((y + z) + w)) - (y + u)), (x + ((z + w) - u))) ||
 rewrite(((x + ((y + z) + w)) - (z + u)), (x + ((y + w) - u))) ||
 rewrite(((x + ((y*c0) + z)) - (w + (u*c1))), (((y - u)*c0) - (w - (z + x))), (c0 == c1)) ||
 rewrite(((x + ((y*c0) + z)) - ((w*c1) + u)), (((y - w)*c0) + ((z + x) - u)), (c0 == c1)) ||
 rewrite(((x + ((y*z) + w)) - (u + (v*y))), (x + ((y*(z - v)) - (u - w)))) ||
 rewrite(((x + ((y*z) + w)) - (u + (v*z))), ((x + ((y - v)*z)) - (u - w))) ||
 rewrite(((x + ((y*z) + w)) - (u + (y*v))), (x + ((y*(z - v)) - (u - w)))) ||
 rewrite(((x + ((y*z) + w)) - (u + (z*v))), ((x + ((y - v)*z)) - (u - w))) ||
 rewrite(((x + ((y*z) + w)) - ((u*y) + v)), ((y*(z - u)) + ((w + x) - v))) ||
 rewrite(((x + ((y*z) + w)) - ((u*z) + v)), ((z*(y - u)) + ((w + x) - v))) ||
 rewrite(((x + ((y*z) + w)) - ((y*u) + v)), ((y*(z - u)) + ((w + x) - v))) ||
 rewrite(((x + ((y*z) + w)) - ((z*u) + v)), ((z*(y - u)) + ((w + x) - v))) ||
 rewrite(((x + (min(y, (z + w)) + u)) - (v + w)), ((x + min((y - w), z)) - (v - u))) ||
 rewrite(((x + (min(y, (z + w)) + u)) - (v + z)), ((x + min((y - z), w)) - (v - u))) ||
 rewrite(((x + (min(y, (z + w)) + u)) - (w + v)), ((x + (min(z, (y - w)) + u)) - v)) ||
 rewrite(((x + (min(y, (z + w)) + u)) - (z + v)), ((x + min((y - z), w)) - (v - u))) ||
 rewrite(((x + (min(y, (z - w)) + u)) - (v + z)), ((x - (max(w, (z - y)) - u)) - v)) ||
 rewrite(((x + (min(y, (z - w)) + u)) - (z + v)), ((x - (max(w, (z - y)) - u)) - v)) ||
 rewrite(((x + (min((y + z), w) + u)) - (v + y)), ((x + min((w - y), z)) - (v - u))) ||
 rewrite(((x + (min((y + z), w) + u)) - (v + z)), ((x + (min((w - z), y) + u)) - v)) ||
 rewrite(((x + (min((y + z), w) + u)) - (y + v)), ((x + (min((w - y), z) + u)) - v)) ||
 rewrite(((x + (min((y + z), w) + u)) - (z + v)), ((x + (min((w - z), y) + u)) - v)) ||
 rewrite(((x + (min((y - z), w) + u)) - (v + y)), ((x - max(z, (y - w))) - (v - u))) ||
 rewrite(((x + (min((y - z), w) + u)) - (y + v)), ((x - max(z, (y - w))) + (u - v))) ||
 rewrite(((x + (y*z)) - (w + (u + (v*y)))), ((x - u) - (w - (y*(z - v))))) ||
 rewrite(((x + (y*z)) - (w + (u + (v*z)))), ((z*(y - v)) - (w + (u - x)))) ||
 rewrite(((x + (y*z)) - (w + (u + (y*v)))), ((x - u) - (w - (y*(z - v))))) ||
 rewrite(((x + (y*z)) - (w + (u + (z*v)))), ((x - u) - (w - (z*(y - v))))) ||
 rewrite(((x + (y*z)) - (w + ((u*y) + v))), ((x - v) - (w - ((z - u)*y)))) ||
 rewrite(((x + (y*z)) - (w + ((u*z) + v))), ((z*(y - u)) - (w - (x - v)))) ||
 rewrite(((x + (y*z)) - (w + ((y*u) + v))), ((x - v) - (w - ((z - u)*y)))) ||
 rewrite(((x + (y*z)) - (w + ((z*u) + v))), ((x - v) - (w - ((y - u)*z)))) ||
 rewrite(((x + (y*z)) - (w + (u*y))), ((y*(z - u)) - (w - x))) ||
 rewrite(((x + (y*z)) - (w + (u*z))), ((z*(y - u)) - (w - x))) ||
 rewrite(((x + (y*z)) - (w + (y*u))), ((y*(z - u)) - (w - x))) ||
 rewrite(((x + (y*z)) - (w + (z*u))), ((z*(y - u)) - (w - x))) ||
 rewrite(((x + (y*z)) - ((w + (u*y)) + v)), ((x - w) + ((y*(z - u)) - v))) ||
 rewrite(((x + (y*z)) - ((w + (u*z)) + v)), ((z*(y - u)) - ((w - x) + v))) ||
 rewrite(((x + (y*z)) - ((w + (y*u)) + v)), ((x - w) + ((y*(z - u)) - v))) ||
 rewrite(((x + (y*z)) - ((w + (z*u)) + v)), ((x - w) + ((z*(y - u)) - v))) ||
 rewrite(((x + (y*z)) - (((w*y) + u) + v)), (x + ((((z - w)*y) - u) - v))) ||
 rewrite(((x + (y*z)) - (((w*z) + u) + v)), ((z*(y - w)) + ((x - u) - v))) ||
 rewrite(((x + (y*z)) - (((y*w) + u) + v)), ((x - u) + (((z - w)*y) - v))) ||
 rewrite(((x + (y*z)) - (((z*w) + u) + v)), ((x - u) + (((y - w)*z) - v))) ||
 rewrite(((x + (y*z)) - ((w*y) + u)), (x + ((y*(z - w)) - u))) ||
 rewrite(((x + (y*z)) - ((w*z) + u)), (x + ((z*(y - w)) - u))) ||
 rewrite(((x + (y*z)) - ((y*w) + u)), ((x + ((z - w)*y)) - u)) ||
 rewrite(((x + (y*z)) - ((z*w) + u)), ((z*(y - w)) + (x - u))) ||
 rewrite(((x + max(y, (z + w))) - (u + w)), (max(z, (y - w)) - (u - x))) ||
 rewrite(((x + max(y, (z + w))) - (u + z)), (max(w, (y - z)) - (u - x))) ||
 rewrite(((x + max(y, (z + w))) - (w + u)), (max(z, (y - w)) + (x - u))) ||
 rewrite(((x + max(y, (z + w))) - (z + u)), (max(w, (y - z)) + (x - u))) ||
 rewrite(((x + max((y + z), w)) - (u + y)), (x + (max(z, (w - y)) - u))) ||
 rewrite(((x + max((y + z), w)) - (u + z)), ((x + max((w - z), y)) - u)) ||
 rewrite(((x + max((y + z), w)) - (y + u)), (x + (max(z, (w - y)) - u))) ||
 rewrite(((x + max((y + z), w)) - (z + u)), ((x + max((w - z), y)) - u)) ||
 rewrite((((x + y) + z) - (w + x)), ((y + z) - w)) ||
 rewrite((((x + y) + z) - (w + y)), ((x + z) - w)) ||
 rewrite((((x + y) + z) - (x + w)), ((y + z) - w)) ||
 rewrite((((x + y) + z) - (y + w)), ((x + z) - w)) ||
 rewrite((((x + (y + z)) + w) - (u + y)), (((x + z) + w) - u)) ||
 rewrite((((x + (y + z)) + w) - (u + z)), (((x + y) + w) - u)) ||
 rewrite((((x + (y + z)) + w) - (y + u)), (((x + z) + w) - u)) ||
 rewrite((((x + (y + z)) + w) - (z + u)), (((x + y) + w) - u)) ||
 rewrite((((x + (y*c0)) + z) - (w + (u*c1))), (((y - u)*c0) + (z - (w - x))), (c0 == c1)) ||
 rewrite((((x + (y*c0)) + z) - ((w*c1) + u)), (((y - w)*c0) + ((x + z) - u)), (c0 == c1)) ||
 rewrite((((x + (y*z)) + w) - (u + (v*y))), ((y*(z - v)) - (u - (x + w)))) ||
 rewrite((((x + (y*z)) + w) - (u + (v*z))), (((z*(y - v)) + w) - (u - x))) ||
 rewrite((((x + (y*z)) + w) - (u + (y*v))), ((y*(z - v)) - (u - (x + w)))) ||
 rewrite((((x + (y*z)) + w) - (u + (z*v))), (((z*(y - v)) + w) - (u - x))) ||
 rewrite((((x + (y*z)) + w) - ((u*y) + v)), ((y*(z - u)) + ((x + w) - v))) ||
 rewrite((((x + (y*z)) + w) - ((u*z) + v)), ((z*(y - u)) + ((x + w) - v))) ||
 rewrite((((x + (y*z)) + w) - ((y*u) + v)), ((y*(z - u)) + ((x + w) - v))) ||
 rewrite((((x + (y*z)) + w) - ((z*u) + v)), ((z*(y - u)) + ((x + w) - v))) ||
 rewrite((((x + min(y, (z + w))) + u) - (v + w)), ((min(z, (y - w)) + u) - (v - x))) ||
 rewrite((((x + min(y, (z + w))) + u) - (v + z)), ((min(w, (y - z)) + u) - (v - x))) ||
 rewrite((((x + min(y, (z + w))) + u) - (w + v)), (((x + min(z, (y - w))) + u) - v)) ||
 rewrite((((x + min(y, (z + w))) + u) - (z + v)), ((min(w, (y - z)) + u) - (v - x))) ||
 rewrite((((x + min(y, (z - w))) + u) - (v + z)), (((x - max(w, (z - y))) + u) - v)) ||
 rewrite((((x + min(y, (z - w))) + u) - (z + v)), (((x - max(w, (z - y))) + u) - v)) ||
 rewrite((((x + min((y + z), w)) + u) - (v + y)), ((min(z, (w - y)) + u) - (v - x))) ||
 rewrite((((x + min((y + z), w)) + u) - (v + z)), (((x + min((w - z), y)) + u) - v)) ||
 rewrite((((x + min((y + z), w)) + u) - (y + v)), (((x + min((w - y), z)) + u) - v)) ||
 rewrite((((x + min((y + z), w)) + u) - (z + v)), (((x + min((w - z), y)) + u) - v)) ||
 rewrite((((x + min((y - z), w)) + u) - (v + y)), (((x - max((y - w), z)) + u) - v)) ||
 rewrite((((x + min((y - z), w)) + u) - (y + v)), (((x - max((y - w), z)) + u) - v)) ||
 rewrite(((((x + y) + z) + w) - (u + x)), (((y + z) + w) - u)) ||
 rewrite(((((x + y) + z) + w) - (u + y)), (((x + z) + w) - u)) ||
 rewrite(((((x + y) + z) + w) - (x + u)), (((y + z) + w) - u)) ||
 rewrite(((((x + y) + z) + w) - (y + u)), (((x + z) + w) - u)) ||
 rewrite(((((x*c0) + y) + z) - (w + (u*c1))), (((x - u)*c0) - (w - (y + z))), (c0 == c1)) ||
 rewrite(((((x*c0) + y) + z) - ((w*c1) + u)), (((x - w)*c0) + ((y + z) - u)), (c0 == c1)) ||
 rewrite(((((x*y) + z) + w) - (u + (v*x))), ((x*(y - v)) - (u - (z + w)))) ||
 rewrite(((((x*y) + z) + w) - (u + (v*y))), ((((x - v)*y) + w) - (u - z))) ||
 rewrite(((((x*y) + z) + w) - (u + (x*v))), ((x*(y - v)) - (u - (z + w)))) ||
 rewrite(((((x*y) + z) + w) - (u + (y*v))), ((((x - v)*y) + w) - (u - z))) ||
 rewrite(((((x*y) + z) + w) - ((u*x) + v)), ((x*(y - u)) + ((z + w) - v))) ||
 rewrite(((((x*y) + z) + w) - ((u*y) + v)), ((y*(x - u)) + ((z + w) - v))) ||
 rewrite(((((x*y) + z) + w) - ((x*u) + v)), ((x*(y - u)) + ((z + w) - v))) ||
 rewrite(((((x*y) + z) + w) - ((y*u) + v)), ((y*(x - u)) + ((z + w) - v))) ||
 rewrite((((min(x, (y + z)) + w) + u) - (v + y)), ((min((x - y), z) + u) - (v - w))) ||
 rewrite((((min(x, (y + z)) + w) + u) - (v + z)), ((min((x - z), y) + u) - (v - w))) ||
 rewrite((((min(x, (y + z)) + w) + u) - (y + v)), ((min((x - y), z) + u) - (v - w))) ||
 rewrite((((min(x, (y + z)) + w) + u) - (z + v)), (((min(y, (x - z)) + w) + u) - v)) ||
 rewrite((((min(x, (y - z)) + w) + u) - (v + y)), (u - ((max(z, (y - x)) - w) + v))) ||
 rewrite((((min(x, (y - z)) + w) + u) - (y + v)), (u - ((max(z, (y - x)) - w) + v))) ||
 rewrite((((min((x + y), z) + w) + u) - (v + x)), ((min((z - x), y) + u) - (v - w))) ||
 rewrite((((min((x + y), z) + w) + u) - (v + y)), (((min((z - y), x) + w) + u) - v)) ||
 rewrite((((min((x + y), z) + w) + u) - (x + v)), (((min((z - x), y) + w) + u) - v)) ||
 rewrite((((min((x + y), z) + w) + u) - (y + v)), (((min((z - y), x) + w) + u) - v)) ||
 rewrite((((min((x - y), z) + w) + u) - (v + x)), ((u - max((x - z), y)) - (v - w))) ||
 rewrite((((min((x - y), z) + w) + u) - (x + v)), ((u - max((x - z), y)) + (w - v))) ||
 rewrite((((x*y) + z) - (w + (u + (v*x)))), (((y - v)*x) - (w + (u - z)))) ||
 rewrite((((x*y) + z) - (w + (u + (v*y)))), (((x - v)*y) - (w + (u - z)))) ||
 rewrite((((x*y) + z) - (w + (u + (x*v)))), ((z - u) - (w - (x*(y - v))))) ||
 rewrite((((x*y) + z) - (w + (u + (y*v)))), (((x - v)*y) - (w + (u - z)))) ||
 rewrite((((x*y) + z) - (w + ((u*x) + v))), (((y - u)*x) - (w - (z - v)))) ||
 rewrite((((x*y) + z) - (w + ((u*y) + v))), (((x - u)*y) - (w - (z - v)))) ||
 rewrite((((x*y) + z) - (w + ((x*u) + v))), ((z - v) - (w - ((y - u)*x)))) ||
 rewrite((((x*y) + z) - (w + ((y*u) + v))), (((x - u)*y) - (w - (z - v)))) ||
 rewrite((((x*y) + z) - (w + (u*x))), (((y - u)*x) - (w - z))) ||
 rewrite((((x*y) + z) - (w + (u*y))), (((x - u)*y) - (w - z))) ||
 rewrite((((x*y) + z) - (w + (x*u))), (((y - u)*x) - (w - z))) ||
 rewrite((((x*y) + z) - (w + (y*u))), (((x - u)*y) - (w - z))) ||
 rewrite((((x*y) + z) - ((w + (u*x)) + v)), (((y - u)*x) - ((w - z) + v))) ||
 rewrite((((x*y) + z) - ((w + (u*y)) + v)), (((x - u)*y) - ((w - z) + v))) ||
 rewrite((((x*y) + z) - ((w + (x*u)) + v)), ((z - w) + ((x*(y - u)) - v))) ||
 rewrite((((x*y) + z) - ((w + (y*u)) + v)), (((x - u)*y) - ((w - z) + v))) ||
 rewrite((((x*y) + z) - (((w*x) + u) + v)), (((y - w)*x) + ((z - u) - v))) ||
 rewrite((((x*y) + z) - (((w*y) + u) + v)), (((x - w)*y) + ((z - u) - v))) ||
 rewrite((((x*y) + z) - (((x*w) + u) + v)), ((z - u) + (((y - w)*x) - v))) ||
 rewrite((((x*y) + z) - (((y*w) + u) + v)), (((x - w)*y) + ((z - u) - v))) ||
 rewrite((((x*y) + z) - ((w*x) + u)), (z + ((x*(y - w)) - u))) ||
 rewrite((((x*y) + z) - ((w*y) + u)), (((x - w)*y) + (z - u))) ||
 rewrite((((x*y) + z) - ((x*w) + u)), (z + (((y - w)*x) - u))) ||
 rewrite((((x*y) + z) - ((y*w) + u)), (((x - w)*y) + (z - u))) ||
 rewrite(((max(x, (y + z)) + w) - (u + y)), (max((x - y), z) - (u - w))) ||
 rewrite(((max(x, (y + z)) + w) - (u + z)), (max((x - z), y) - (u - w))) ||
 rewrite(((max(x, (y + z)) + w) - (y + u)), (max((x - y), z) + (w - u))) ||
 rewrite(((max(x, (y + z)) + w) - (z + u)), (max((x - z), y) + (w - u))) ||
 rewrite(((max((x + y), z) + w) - (u + x)), (max((z - x), y) - (u - w))) ||
 rewrite(((max((x + y), z) + w) - (u + y)), ((max((z - y), x) + w) - u)) ||
 rewrite(((max((x + y), z) + w) - (x + u)), (max((z - x), y) + (w - u))) ||
 rewrite(((max((x + y), z) + w) - (y + u)), ((max((z - y), x) + w) - u)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((x + (y*c0)) - (z*c1)), (x - ((z + y)*c1)), ((c0 + c1) == 0)) ||
 rewrite((((x*c0) + y) - (z*c1)), (y - ((x + z)*c1)), ((c0 + c1) == 0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((x + y) - min(z, (w + x))), (y - min(w, (z - x)))) ||
 rewrite(((x + y) - min(z, (w + y))), (x - min(w, (z - y)))) ||
 rewrite(((x + y) - min(z, (w + (u + x)))), (y - min((z - x), (u + w)))) ||
 rewrite(((x + y) - min(z, (w + (u + y)))), (x - min((z - y), (u + w)))) ||
 rewrite(((x + y) - min(z, (w + (x + u)))), (y - min((z - x), (u + w)))) ||
 rewrite(((x + y) - min(z, (w + (y + u)))), (x - min((z - y), (u + w)))) ||
 rewrite(((x + y) - min(z, (w + ((x + y) - u)))), (y - min((z - x), ((w + y) - u)))) ||
 rewrite(((x + y) - min(z, (w + ((y + x) - u)))), (y - min((z - x), ((y + w) - u)))) ||
 rewrite(((x + y) - min(z, (x + w))), (y - min(w, (z - x)))) ||
 rewrite(((x + y) - min(z, (y + w))), (x - min(w, (z - y)))) ||
 rewrite(((x + y) - min(z, ((w + x) + u))), (y - min((z - x), (w + u)))) ||
 rewrite(((x + y) - min(z, ((w + y) + u))), (x - min((z - y), (w + u)))) ||
 rewrite(((x + y) - min(z, ((x + w) + u))), (y - min((z - x), (w + u)))) ||
 rewrite(((x + y) - min(z, ((y + w) + u))), (x - min((z - y), (w + u)))) ||
 rewrite(((x + y) - min(z, (((x + y) - w) + u))), (x - min((z - y), ((x - w) + u)))) ||
 rewrite(((x + y) - min(z, (((y + x) - w) + u))), (y - min((z - x), ((y - w) + u)))) ||
 rewrite(((x + y) - min(z, ((w + x) - u))), (y - min((z - x), (w - u)))) ||
 rewrite(((x + y) - min(z, ((w + y) - u))), (x - min((z - y), (w - u)))) ||
 rewrite(((x + y) - min(z, ((x + w) - u))), (y - min((z - x), (w - u)))) ||
 rewrite(((x + y) - min(z, ((y + w) - u))), (x - min((z - y), (w - u)))) ||
 rewrite(((x + y) - min(z, min(w, (u + x)))), (y - min((min(w, z) - x), u))) ||
 rewrite(((x + y) - min(z, min(w, (u + y)))), (x - min(u, (min(w, z) - y)))) ||
 rewrite(((x + y) - min(z, min(w, (u + (v + x))))), (y - min((min(z, w) - x), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, (u + (v + y))))), (x - min((min(z, w) - y), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, (u + (x + v))))), (y - min((min(z, w) - x), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, (u + (y + v))))), (x - min((min(z, w) - y), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, (x + u)))), (y - min(u, (min(w, z) - x)))) ||
 rewrite(((x + y) - min(z, min(w, (y + u)))), (x - min((min(z, w) - y), u))) ||
 rewrite(((x + y) - min(z, min(w, ((u + x) + v)))), (y - min((min(w, z) - x), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, ((u + y) + v)))), (x - min((min(z, w) - y), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, ((x + u) + v)))), (y - min((min(w, z) - x), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, ((y + u) + v)))), (x - min((min(z, w) - y), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, ((y + x) - u)))), (x - min((x - u), (min(w, z) - y)))) ||
 rewrite(((x + y) - min(z, min(w, min(u, (x - v))))), (y + max(v, (x - min(u, min(w, z)))))) ||
 rewrite(((x + y) - min(z, min(w, min(u, (y - v))))), (x + max(v, (y - min(u, min(w, z)))))) ||
 rewrite(((x + y) - min(z, min(w, min((x - u), v)))), (y + max(u, (x - min(min(w, z), v))))) ||
 rewrite(((x + y) - min(z, min(w, min((y - u), v)))), (x + max(u, (y - min(min(w, z), v))))) ||
 rewrite(((x + y) - min(z, min((w + x), u))), (y - min(w, (min(u, z) - x)))) ||
 rewrite(((x + y) - min(z, min((w + y), u))), (x - min(w, (min(u, z) - y)))) ||
 rewrite(((x + y) - min(z, min((w + (u + x)), v))), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(z, min((w + (u + y)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite(((x + y) - min(z, min((w + (x + u)), v))), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(z, min((w + (y + u)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite(((x + y) - min(z, min((x + w), u))), (y - min(w, (min(u, z) - x)))) ||
 rewrite(((x + y) - min(z, min((y + w), u))), (x - min(w, (min(u, z) - y)))) ||
 rewrite(((x + y) - min(z, min(((w + x) + u), v))), (y - min((min(z, v) - x), (w + u)))) ||
 rewrite(((x + y) - min(z, min(((w + y) + u), v))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite(((x + y) - min(z, min(((x + w) + u), v))), (y - min((min(z, v) - x), (w + u)))) ||
 rewrite(((x + y) - min(z, min(((y + w) + u), v))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite(((x + y) - min(z, min(((y + x) - w), u))), (y - min((min(u, z) - x), (y - w)))) ||
 rewrite(((x + y) - min(z, min(min(w, (x - u)), v))), (y + max(u, (x - min(w, min(v, z)))))) ||
 rewrite(((x + y) - min(z, min(min(w, (y - u)), v))), (x + max(u, (y - min(w, min(v, z)))))) ||
 rewrite(((x + y) - min(z, min(min((x - w), u), v))), (y + max(w, (x - min(min(v, z), u))))) ||
 rewrite(((x + y) - min(z, min(min((y - w), u), v))), (x + max(w, (y - min(min(v, z), u))))) ||
 rewrite(((x + y) - min((x + z), w)), (y - min((w - x), z))) ||
 rewrite(((x + y) - min((y + z), w)), (x - min(z, (w - y)))) ||
 rewrite(((x + y) - min((z + x), w)), (y - min((w - x), z))) ||
 rewrite(((x + y) - min((z + y), w)), (x - min((w - y), z))) ||
 rewrite(((x + y) - min((z + (w + x)), u)), (y - min((u - x), (w + z)))) ||
 rewrite(((x + y) - min((z + (w + y)), u)), (x - min((u - y), (z + w)))) ||
 rewrite(((x + y) - min((z + (x + w)), u)), (y - min((u - x), (w + z)))) ||
 rewrite(((x + y) - min((z + (y + w)), u)), (x - min((u - y), (z + w)))) ||
 rewrite(((x + y) - min((z + ((x + y) - w)), u)), (x - min((u - y), (z + (x - w))))) ||
 rewrite(((x + y) - min((z + ((y + x) - w)), u)), (y - min((z - (w - y)), (u - x)))) ||
 rewrite(((x + y) - min(((x + z) + w), u)), (y - min((u - x), (z + w)))) ||
 rewrite(((x + y) - min(((y + z) + w), u)), (x - min((u - y), (z + w)))) ||
 rewrite(((x + y) - min(((z + x) + w), u)), (y - min((u - x), (z + w)))) ||
 rewrite(((x + y) - min(((z + y) + w), u)), (x - min((u - y), (z + w)))) ||
 rewrite(((x + y) - min((((x + y) - z) + w), u)), (x - min((u - y), ((x - z) + w)))) ||
 rewrite(((x + y) - min((((y + x) - z) + w), u)), (y - min(((w + y) - z), (u - x)))) ||
 rewrite(((x + y) - min(((x + z) - w), u)), (y - min((u - x), (z - w)))) ||
 rewrite(((x + y) - min(((y + z) - w), u)), (x - min((u - y), (z - w)))) ||
 rewrite(((x + y) - min(((z + x) - w), u)), (y - min((u - x), (z - w)))) ||
 rewrite(((x + y) - min(((z + y) - w), u)), (x - min((u - y), (z - w)))) ||
 rewrite(((x + y) - min(min(z, (w + x)), u)), (y - min((min(z, u) - x), w))) ||
 rewrite(((x + y) - min(min(z, (w + y)), u)), (x - min(w, (min(z, u) - y)))) ||
 rewrite(((x + y) - min(min(z, (w + (u + x))), v)), (y - min((min(v, z) - x), (u + w)))) ||
 rewrite(((x + y) - min(min(z, (w + (u + y))), v)), (x - min((min(v, z) - y), (u + w)))) ||
 rewrite(((x + y) - min(min(z, (w + (x + u))), v)), (y - min((min(v, z) - x), (u + w)))) ||
 rewrite(((x + y) - min(min(z, (w + (y + u))), v)), (x - min((min(v, z) - y), (u + w)))) ||
 rewrite(((x + y) - min(min(z, (x + w)), u)), (y - min((min(z, u) - x), w))) ||
 rewrite(((x + y) - min(min(z, (y + w)), u)), (x - min(w, (min(z, u) - y)))) ||
 rewrite(((x + y) - min(min(z, ((w + x) + u)), v)), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(min(z, ((w + y) + u)), v)), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite(((x + y) - min(min(z, ((x + w) + u)), v)), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(min(z, ((y + w) + u)), v)), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite(((x + y) - min(min(z, ((y + x) - w)), u)), (y - min((y - w), (min(z, u) - x)))) ||
 rewrite(((x + y) - min(min(z, min(w, (x - u))), v)), (y + max((x - min(w, min(z, v))), u))) ||
 rewrite(((x + y) - min(min(z, min(w, (y - u))), v)), (x + max((y - min(w, min(z, v))), u))) ||
 rewrite(((x + y) - min(min(z, min((x - w), u)), v)), (y + max((x - min(min(z, v), u)), w))) ||
 rewrite(((x + y) - min(min(z, min((y - w), u)), v)), (x + max((y - min(min(z, v), u)), w))) ||
 rewrite(((x + y) - min(min((x + z), w), u)), (y - min((min(w, u) - x), z))) ||
 rewrite(((x + y) - min(min((y + z), w), u)), (x - min((min(w, u) - y), z))) ||
 rewrite(((x + y) - min(min((z + x), w), u)), (y - min((min(w, u) - x), z))) ||
 rewrite(((x + y) - min(min((z + y), w), u)), (x - min((min(w, u) - y), z))) ||
 rewrite(((x + y) - min(min((z + (w + x)), u), v)), (y - min((min(v, u) - x), (w + z)))) ||
 rewrite(((x + y) - min(min((z + (w + y)), u), v)), (x - min((min(v, u) - y), (w + z)))) ||
 rewrite(((x + y) - min(min((z + (x + w)), u), v)), (y - min((min(v, u) - x), (w + z)))) ||
 rewrite(((x + y) - min(min((z + (y + w)), u), v)), (x - min((min(v, u) - y), (w + z)))) ||
 rewrite(((x + y) - min(min(((x + z) + w), u), v)), (y - min((min(v, u) - x), (z + w)))) ||
 rewrite(((x + y) - min(min(((y + z) + w), u), v)), (x - min((min(v, u) - y), (z + w)))) ||
 rewrite(((x + y) - min(min(((z + x) + w), u), v)), (y - min((min(v, u) - x), (z + w)))) ||
 rewrite(((x + y) - min(min(((z + y) + w), u), v)), (x - min((min(v, u) - y), (z + w)))) ||
 rewrite(((x + y) - min(min(((y + x) - z), w), u)), (x - min((x - z), (min(w, u) - y)))) ||
 rewrite(((x + y) - min(min(min(z, (x - w)), u), v)), (y + max((x - min(z, min(u, v))), w))) ||
 rewrite(((x + y) - min(min(min(z, (y - w)), u), v)), (x + max(w, (y - min(z, min(u, v)))))) ||
 rewrite(((x + y) - min(min(min((x - z), w), u), v)), (y + max((x - min(min(u, v), w)), z))) ||
 rewrite(((x + y) - min(min(min((y - z), w), u), v)), (x + max(z, (y - min(min(v, w), u))))) ||
 rewrite(((x + min(y, z)) - min(w, (u + min(z, y)))), (x - min(u, (w - min(z, y))))) ||
 rewrite(((x + min(y, z)) - min(w, (min(z, y) + u))), (x - min(u, (w - min(z, y))))) ||
 rewrite(((x + min(y, z)) - min(y, min(w, z))), (x + max((min(y, z) - w), 0))) ||
 rewrite(((x + min(y, z)) - min(y, min(z, w))), (x + max((min(y, z) - w), 0))) ||
 rewrite(((x + min(y, z)) - min(z, min(w, y))), (x + max((min(y, z) - w), 0))) ||
 rewrite(((x + min(y, z)) - min(z, min(y, w))), (x + max((min(z, y) - w), 0))) ||
 rewrite(((x + min(y, z)) - min((w + min(z, y)), u)), (x - min(w, (u - min(z, y))))) ||
 rewrite(((x + min(y, z)) - min((min(z, y) + w), u)), (x - min(w, (u - min(z, y))))) ||
 rewrite(((x + min(y, z)) - min(min(w, y), z)), (x + max((min(y, z) - w), 0))) ||
 rewrite(((x + min(y, z)) - min(min(w, z), y)), (x + max((min(y, z) - w), 0))) ||
 rewrite(((x + min(y, z)) - min(min(y, w), z)), (x + max((min(y, z) - w), 0))) ||
 rewrite(((x + min(y, z)) - min(min(z, w), y)), (x + max((min(y, z) - w), 0))) ||
 rewrite(((min(x, y) + z) - min(w, (u + min(y, x)))), (z - min(u, (w - min(y, x))))) ||
 rewrite(((min(x, y) + z) - min(w, (min(y, x) + u))), (z - min(u, (w - min(y, x))))) ||
 rewrite(((min(x, y) + z) - min(x, min(w, y))), (z + max((min(x, y) - w), 0))) ||
 rewrite(((min(x, y) + z) - min(x, min(y, w))), (z + max((min(x, y) - w), 0))) ||
 rewrite(((min(x, y) + z) - min(y, min(w, x))), (z + max((min(y, x) - w), 0))) ||
 rewrite(((min(x, y) + z) - min(y, min(x, w))), (z + max((min(y, x) - w), 0))) ||
 rewrite(((min(x, y) + z) - min((w + min(y, x)), u)), (z - min((u - min(y, x)), w))) ||
 rewrite(((min(x, y) + z) - min((min(y, x) + w), u)), (z - min(w, (u - min(y, x))))) ||
 rewrite(((min(x, y) + z) - min(min(w, x), y)), (z + max((min(y, x) - w), 0))) ||
 rewrite(((min(x, y) + z) - min(min(w, y), x)), (z + max((min(x, y) - w), 0))) ||
 rewrite(((min(x, y) + z) - min(min(x, w), y)), (z + max((min(y, x) - w), 0))) ||
 rewrite(((min(x, y) + z) - min(min(y, w), x)), (z + max((min(x, y) - w), 0))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(((x + (y + z)) - max(w, (u + (v + y)))), (x + (z - max((w - y), (v + u))))) ||
 rewrite(((x + (y + z)) - max(w, (u + (v + z)))), (x + (y - max((w - z), (v + u))))) ||
 rewrite(((x + (y + z)) - max(w, (u + (y + v)))), (x + (z - max((w - y), (u + v))))) ||
 rewrite(((x + (y + z)) - max(w, (u + (z + v)))), (x + (y - max((w - z), (u + v))))) ||
 rewrite(((x + (y + z)) - max(w, ((u + y) + v))), ((x + z) - max((w - y), (u + v)))) ||
 rewrite(((x + (y + z)) - max(w, ((u + z) + v))), ((x + y) - max((w - z), (u + v)))) ||
 rewrite(((x + (y + z)) - max(w, ((y + u) + v))), ((x + z) - max((w - y), (u + v)))) ||
 rewrite(((x + (y + z)) - max(w, ((z + u) + v))), ((x + y) - max((w - z), (u + v)))) ||
 rewrite(((x + (y + z)) - max((w + (u + y)), v)), ((x - max((v - y), (w + u))) + z)) ||
 rewrite(((x + (y + z)) - max((w + (u + z)), v)), (x - (max((v - z), (w + u)) - y))) ||
 rewrite(((x + (y + z)) - max((w + (y + u)), v)), ((x - max((v - y), (w + u))) + z)) ||
 rewrite(((x + (y + z)) - max((w + (z + u)), v)), (x - (max((v - z), (w + u)) - y))) ||
 rewrite(((x + (y + z)) - max(((w + y) + u), v)), ((x + z) - max((v - y), (u + w)))) ||
 rewrite(((x + (y + z)) - max(((w + z) + u), v)), ((x + y) - max((v - z), (u + w)))) ||
 rewrite(((x + (y + z)) - max(((y + w) + u), v)), ((x + z) - max((v - y), (u + w)))) ||
 rewrite(((x + (y + z)) - max(((z + w) + u), v)), ((x + y) - max((v - z), (u + w)))) ||
 rewrite(((x + (y + (z + w))) - max(u, (v + w))), ((x + (y + z)) - max(v, (u - w)))) ||
 rewrite(((x + (y + (z + w))) - max(u, (v + z))), ((x + (y + w)) - max(v, (u - z)))) ||
 rewrite(((x + (y + (z + w))) - max(u, (w + v))), ((x + (y + z)) - max(v, (u - w)))) ||
 rewrite(((x + (y + (z + w))) - max(u, (z + v))), ((x + (y + w)) - max(v, (u - z)))) ||
 rewrite(((x + (y + (z + w))) - max((u + w), v)), ((x + (y + z)) - max((v - w), u))) ||
 rewrite(((x + (y + (z + w))) - max((u + z), v)), ((x + (y + w)) - max((v - z), u))) ||
 rewrite(((x + (y + (z + w))) - max((w + u), v)), ((x + (y + z)) - max((v - w), u))) ||
 rewrite(((x + (y + (z + w))) - max((z + u), v)), ((x + (y + w)) - max((v - z), u))) ||
 rewrite(((x + ((y + z) + w)) - max(u, (v + y))), ((x + (z + w)) - max(v, (u - y)))) ||
 rewrite(((x + ((y + z) + w)) - max(u, (v + z))), ((x + (y + w)) - max(v, (u - z)))) ||
 rewrite(((x + ((y + z) + w)) - max(u, (y + v))), (x + ((z + w) - max((u - y), v)))) ||
 rewrite(((x + ((y + z) + w)) - max(u, (z + v))), (x + ((y + w) - max((u - z), v)))) ||
 rewrite(((x + ((y + z) + w)) - max((u + y), v)), ((x + (z + w)) - max((v - y), u))) ||
 rewrite(((x + ((y + z) + w)) - max((u + z), v)), ((x + (y + w)) - max((v - z), u))) ||
 rewrite(((x + ((y + z) + w)) - max((y + u), v)), ((x + (z + w)) - max((v - y), u))) ||
 rewrite(((x + ((y + z) + w)) - max((z + u), v)), ((x + (y + w)) - max((v - z), u))) ||
 rewrite((((x + y) + z) - max(w, (u + (v + x)))), (z + (y - max((w - x), (v + u))))) ||
 rewrite((((x + y) + z) - max(w, (u + (v + y)))), (z + (x - max((w - y), (v + u))))) ||
 rewrite((((x + y) + z) - max(w, (u + (x + v)))), (z + (y - max((w - x), (u + v))))) ||
 rewrite((((x + y) + z) - max(w, (u + (y + v)))), (z + (x - max((w - y), (u + v))))) ||
 rewrite((((x + y) + z) - max(w, ((u + x) + v))), ((z + y) - max((w - x), (u + v)))) ||
 rewrite((((x + y) + z) - max(w, ((u + y) + v))), ((z + x) - max((w - y), (u + v)))) ||
 rewrite((((x + y) + z) - max(w, ((x + u) + v))), ((z + y) - max((w - x), (u + v)))) ||
 rewrite((((x + y) + z) - max(w, ((y + u) + v))), ((z + x) - max((w - y), (u + v)))) ||
 rewrite((((x + y) + z) - max((w + (u + x)), v)), (z - (max((v - x), (w + u)) - y))) ||
 rewrite((((x + y) + z) - max((w + (u + y)), v)), (z + (x - max((v - y), (w + u))))) ||
 rewrite((((x + y) + z) - max((w + (x + u)), v)), (z - (max((v - x), (w + u)) - y))) ||
 rewrite((((x + y) + z) - max((w + (y + u)), v)), (z + (x - max((v - y), (w + u))))) ||
 rewrite((((x + y) + z) - max(((w + x) + u), v)), ((y + z) - max((v - x), (u + w)))) ||
 rewrite((((x + y) + z) - max(((w + y) + u), v)), ((x + z) - max((v - y), (u + w)))) ||
 rewrite((((x + y) + z) - max(((x + w) + u), v)), ((y + z) - max((v - x), (u + w)))) ||
 rewrite((((x + y) + z) - max(((y + w) + u), v)), ((x + z) - max((v - y), (u + w)))) ||
 rewrite((((x + (y + z)) + w) - max(u, (v + y))), (((x + z) + w) - max(v, (u - y)))) ||
 rewrite((((x + (y + z)) + w) - max(u, (v + z))), (((x + y) + w) - max(v, (u - z)))) ||
 rewrite((((x + (y + z)) + w) - max(u, (y + v))), ((x + (w + z)) - max(v, (u - y)))) ||
 rewrite((((x + (y + z)) + w) - max(u, (z + v))), ((x + (y + w)) - max(v, (u - z)))) ||
 rewrite((((x + (y + z)) + w) - max((u + y), v)), (((x + z) + w) - max((v - y), u))) ||
 rewrite((((x + (y + z)) + w) - max((u + z), v)), (((x + y) + w) - max((v - z), u))) ||
 rewrite((((x + (y + z)) + w) - max((y + u), v)), (((x + z) + w) - max((v - y), u))) ||
 rewrite((((x + (y + z)) + w) - max((z + u), v)), (((x + y) + w) - max((v - z), u))) ||
 rewrite(((((x + y) + z) + w) - max(u, (v + x))), (((y + z) + w) - max(v, (u - x)))) ||
 rewrite(((((x + y) + z) + w) - max(u, (v + y))), (((x + z) + w) - max(v, (u - y)))) ||
 rewrite(((((x + y) + z) + w) - max(u, (x + v))), (((w + y) + z) - max(v, (u - x)))) ||
 rewrite(((((x + y) + z) + w) - max(u, (y + v))), (((w + x) + z) - max(v, (u - y)))) ||
 rewrite(((((x + y) + z) + w) - max((u + x), v)), (((y + z) + w) - max((v - x), u))) ||
 rewrite(((((x + y) + z) + w) - max((u + y), v)), (((x + z) + w) - max((v - y), u))) ||
 rewrite(((((x + y) + z) + w) - max((x + u), v)), (((y + z) + w) - max((v - x), u))) ||
 rewrite(((((x + y) + z) + w) - max((y + u), v)), (((x + z) + w) - max((v - y), u))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(((x + y) - select(z, (w + (u + x)), v)), (y - select(z, (u + w), (v - x)))) ||
 rewrite(((x + y) - select(z, (w + (u + y)), v)), (x - select(z, (u + w), (v - y)))) ||
 rewrite(((x + y) - select(z, (w + (x + u)), v)), (y - select(z, (w + u), (v - x)))) ||
 rewrite(((x + y) - select(z, (w + (y + u)), v)), (x - select(z, (w + u), (v - y)))) ||
 rewrite(((x + y) - select(z, ((w + x) + u), v)), (y - select(z, (w + u), (v - x)))) ||
 rewrite(((x + y) - select(z, ((w + y) + u), v)), (x - select(z, (w + u), (v - y)))) ||
 rewrite(((x + y) - select(z, ((x + w) + u), v)), (y - select(z, (w + u), (v - x)))) ||
 rewrite(((x + y) - select(z, ((y + w) + u), v)), (x - select(z, (w + u), (v - y)))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(((x - min(y, (z + (x - w)))) - w), (0 - min(z, (y - (x - w))))) ||
 rewrite(((x - min(y, ((x - z) + w))) - z), (0 - min(w, (y - (x - z))))) ||
 rewrite(((x - min((y + (x - z)), w)) - z), (0 - min(y, ((z - x) + w)))) ||
 rewrite(((x - min(((x - y) + z), w)) - y), (0 - min(z, ((y - x) + w)))) ||
 rewrite((((x + y) - z) - x), (y - z)) ||
 rewrite((((x + y) - z) - y), (x - z)) ||
 rewrite((((x + min(y, (z + (w + u)))) - v) - u), ((min((y - u), (w + z)) + x) - v)) ||
 rewrite((((x + min(y, (z + (w + u)))) - v) - w), ((min((y - w), (z + u)) + x) - v)) ||
 rewrite((((x + min(y, ((z + w) + u))) - v) - w), (x + (min((y - w), (u + z)) - v))) ||
 rewrite((((x + min(y, ((z + w) + u))) - v) - z), ((min((y - z), (w + u)) - v) + x)) ||
 rewrite((((x + min(y, (z - w))) - u) - z), ((x - max(w, (z - y))) - u)) ||
 rewrite((((x + min((y + (z + w)), u)) - v) - w), (x + (min((u - w), (z + y)) - v))) ||
 rewrite((((x + min((y + (z + w)), u)) - v) - z), (x + (min((u - z), (w + y)) - v))) ||
 rewrite((((x + min(((y + z) + w), u)) - v) - y), (x + (min((u - y), (z + w)) - v))) ||
 rewrite((((x + min(((y + z) + w), u)) - v) - z), (x + (min((u - z), (y + w)) - v))) ||
 rewrite((((x + min((y - z), w)) - u) - y), ((x - max((y - w), z)) - u)) ||
 rewrite((((min(x, (y + (z + w))) + u) - v) - w), ((min((x - w), (z + y)) + u) - v)) ||
 rewrite((((min(x, (y + (z + w))) + u) - v) - z), ((min((x - z), (w + y)) + u) - v)) ||
 rewrite((((min(x, ((y + z) + w)) + u) - v) - y), ((min((x - y), (z + w)) + u) - v)) ||
 rewrite((((min(x, ((y + z) + w)) + u) - v) - z), ((min((x - z), (y + w)) + u) - v)) ||
 rewrite((((min(x, (y - z)) + w) - u) - y), (w - (max(z, (y - x)) + u))) ||
 rewrite((((min((x + (y + z)), w) + u) - v) - y), (min((w - y), (x + z)) + (u - v))) ||
 rewrite((((min((x + (y + z)), w) + u) - v) - z), (min((w - z), (y + x)) + (u - v))) ||
 rewrite((((min(((x + y) + z), w) + u) - v) - x), (u + (min((w - x), (y + z)) - v))) ||
 rewrite((((min(((x + y) + z), w) + u) - v) - y), (u + (min((w - y), (x + z)) - v))) ||
 rewrite((((min((x - y), z) + w) - u) - x), (w - (max((x - z), y) + u))) ||
 rewrite((((x - y) - z) - x), (0 - (y + z))) ||
 rewrite(((min(x, (y + z)) - w) - y), (min(z, (x - y)) - w)) ||
 rewrite(((min(x, (y + z)) - w) - z), (min(y, (x - z)) - w)) ||
 rewrite(((min((x + y), z) - w) - x), (min((z - x), y) - w)) ||
 rewrite(((min((x + y), z) - w) - y), (min(x, (z - y)) - w)) ||
 rewrite(((select(x, (y + z), w) - u) - y), (select(x, z, (w - y)) - u)) ||
 rewrite(((select(x, (y + z), w) - u) - z), (select(x, y, (w - z)) - u)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((((x + y) - z) - (w + x)), ((y - z) - w)) ||
 rewrite((((x + y) - z) - (w + y)), ((x - z) - w)) ||
 rewrite((((x + y) - z) - (x + w)), ((y - z) - w)) ||
 rewrite((((x + y) - z) - (y + w)), ((x - z) - w)) ||
 rewrite((((x + min(y, z)) - w) - (u + min(z, y))), (x - (w + u))) ||
 rewrite((((x + min(y, z)) - w) - (min(z, y) + u)), (x - (w + u))) ||
 rewrite((((min(x, y) + z) - w) - (u + min(y, x))), ((z - w) - u)) ||
 rewrite((((min(x, y) + z) - w) - (min(y, x) + u)), ((z - w) - u)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((c0 - x) - (y*c1)), ((y*fold((c0 - c1))) - x), (c0 == 0)) ||
 rewrite(((x - (y*c0)) - (z*c1)), (x - ((z + y)*c1)), (c0 == c1)) ||
 rewrite(((x - (y*z)) - (w*y)), (x - ((z + w)*y))) ||
 rewrite(((x - (y*z)) - (w*z)), (x - (z*(y + w)))) ||
 rewrite(((x - (y*z)) - (y*w)), (x - ((z + w)*y))) ||
 rewrite(((x - (y*z)) - (z*w)), (x - (z*(y + w)))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((((x + min(y, min(z, w))) - u) - min(w, y)), (min((z - min(y, w)), 0) + (x - u))) ||
 rewrite((((x + min(y, min(z, w))) - u) - min(y, w)), (min((z - min(y, w)), 0) + (x - u))) ||
 rewrite((((x + min(y, min(z, w))) - u) - min(y, z)), (min((w - min(y, z)), 0) + (x - u))) ||
 rewrite((((x + min(y, min(z, w))) - u) - min(z, y)), (min((w - min(y, z)), 0) + (x - u))) ||
 rewrite((((x + min(min(y, z), w)) - u) - min(w, y)), (min((z - min(y, w)), 0) + (x - u))) ||
 rewrite((((x + min(min(y, z), w)) - u) - min(w, z)), (min((y - min(z, w)), 0) + (x - u))) ||
 rewrite((((x + min(min(y, z), w)) - u) - min(y, w)), (min((z - min(y, w)), 0) + (x - u))) ||
 rewrite((((x + min(min(y, z), w)) - u) - min(z, w)), (min((y - min(z, w)), 0) + (x - u))) ||
 rewrite((((min(x, min(y, z)) + w) - u) - min(x, y)), ((min((z - min(y, x)), 0) - u) + w)) ||
 rewrite((((min(x, min(y, z)) + w) - u) - min(x, z)), ((min((y - min(x, z)), 0) - u) + w)) ||
 rewrite((((min(x, min(y, z)) + w) - u) - min(y, x)), ((min((z - min(y, x)), 0) - u) + w)) ||
 rewrite((((min(x, min(y, z)) + w) - u) - min(z, x)), ((min((y - min(x, z)), 0) - u) + w)) ||
 rewrite((((min(min(x, y), z) + w) - u) - min(x, z)), (min((y - min(x, z)), 0) + (w - u))) ||
 rewrite((((min(min(x, y), z) + w) - u) - min(y, z)), (min((x - min(y, z)), 0) + (w - u))) ||
 rewrite((((min(min(x, y), z) + w) - u) - min(z, x)), (min((y - min(x, z)), 0) + (w - u))) ||
 rewrite((((min(min(x, y), z) + w) - u) - min(z, y)), (min((x - min(y, z)), 0) + (w - u))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(((x - select(y, z, w)) - select(y, u, w)), (x - select(y, (z + u), (w*2)))))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*c0) - (y + (z + (x*c1)))), ((fold((c0 - c1)) - z) - y), (c0 == c1)) ||
 rewrite(((x*c0) - (y + ((x*c1) + z))), (0 - (y + z)), (c0 == c1)) ||
 rewrite(((x*c0) - (y + (z*c1))), (((x - z)*c1) - y), (c0 == c1)) ||
 rewrite(((x*c0) - ((y + (x*c1)) + z)), ((fold((c0 - c1)) - z) - y), (c0 == c1)) ||
 rewrite(((x*c0) - (((x*c1) + y) + z)), (0 - (y + z)), (c0 == c1)) ||
 rewrite(((x*c0) - ((y*c1) + z)), (((x - y)*c1) - z), (c0 == c1)) ||
 rewrite(((x*c0) - ((y*c1) + (z + (x*c2)))), (0 - ((y*c1) + z)), (c0 == c2)) ||
 rewrite(((x*c0) - ((y*c1) + ((x*c2) + z))), (0 - ((y*c1) + z)), (c0 == c2)) ||
 rewrite(((x*y) - (z + (w + (y*x)))), (0 - (z + w))) ||
 rewrite(((x*y) - (z + ((y*x) + w))), (0 - (z + w))) ||
 rewrite(((x*y) - (z + (w*x))), ((x*(y - w)) - z)) ||
 rewrite(((x*y) - (z + (w*y))), ((y*(x - w)) - z)) ||
 rewrite(((x*y) - (z + (x*w))), ((x*(y - w)) - z)) ||
 rewrite(((x*y) - (z + (y*w))), ((y*(x - w)) - z)) ||
 rewrite(((x*y) - (z + min(w, (x*y)))), (max(((x*y) - w), 0) - z)) ||
 rewrite(((x*y) - (z + min(w, (y*x)))), (max(((y*x) - w), 0) - z)) ||
 rewrite(((x*y) - (z + min((x*y), w))), (max(((x*y) - w), 0) - z)) ||
 rewrite(((x*y) - (z + min((y*x), w))), (max(((y*x) - w), 0) - z)) ||
 rewrite(((x*y) - ((z + (y*x)) + w)), (0 - (z + w))) ||
 rewrite(((x*y) - (((y*x) + z) + w)), (0 - (z + w))) ||
 rewrite(((x*y) - ((x*z) + w)), ((x*(y - z)) - w)) ||
 rewrite(((x*y) - ((y*z) + w)), ((y*(x - z)) - w)) ||
 rewrite(((x*y) - ((z*x) + w)), ((x*(y - z)) - w)) ||
 rewrite(((x*y) - ((z*y) + w)), ((y*(x - z)) - w)) ||
 rewrite(((x*y) - (min(z, (x*y)) + w)), (max(((y*x) - z), 0) - w)) ||
 rewrite(((x*y) - (min(z, (y*x)) + w)), (max(((x*y) - z), 0) - w)) ||
 rewrite(((x*y) - (min((x*y), z) + w)), (max(((x*y) - z), 0) - w)) ||
 rewrite(((x*y) - (min((y*x), z) + w)), (max(((y*x) - z), 0) - w)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((x*c0) - min(y, min(z, (y + c1)))), ((x*fold((c0 - min(c1, 0)))) - min(y, z)), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min(z, min(w, (z + c1))))), ((x*fold((c0 - min(c1, 0)))) - min(z, min(w, y))), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min(z, min((z + c1), w)))), ((x*fold((c0 - min(c1, 0)))) - min(y, min(w, z))), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min((y + c1), z))), ((x*fold((c0 - min(c1, 0)))) - min(y, z)), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min(min(z, (w + c1)), w))), ((x*fold((c0 - min(c1, 0)))) - min(y, min(z, w))), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min(min((z + c1), w), z))), ((x*fold((c0 - min(c1, 0)))) - min(y, min(w, z))), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(y, (z + c1)), z)), ((x*fold((c0 - min(c1, 0)))) - min(z, y)), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(y, min(z, (y + c1))), w)), ((x*fold((c0 - min(c1, 0)))) - min(w, min(z, y))), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(y, min((y + c1), z)), w)), ((x*fold((c0 - min(c1, 0)))) - min(w, min(z, y))), (0 <= c1)) ||
 rewrite(((x*c0) - min(min((y + c1), z), y)), ((x*fold((c0 - min(c1, 0)))) - min(y, z)), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(min(y, (z + c1)), z), w)), ((x*fold((c0 - min(c1, 0)))) - min(min(y, z), w)), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(min((y + c1), z), y), w)), ((x*fold((c0 - min(c1, 0)))) - min(min(z, y), w)), (0 <= c1)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(((x*c0) - select(y, c1, c2)), (select(y, fold((0 - c1)), fold(((c0 - c2) + 1))) - x), (c0 == -1)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite((min(x, (y + (z + (w + u)))) - u), min((x - u), (z + (w + y)))) ||
 rewrite((min(x, (y + (z + (w + u)))) - w), min((x - w), (z + (y + u)))) ||
 rewrite((min(x, (y + ((z + w) + u))) - w), min((x - w), ((z + y) + u))) ||
 rewrite((min(x, (y + ((z + w) + u))) - z), min((x - z), ((y + w) + u))) ||
 rewrite((min(x, (y + ((z + w) - u))) - w), min((x - w), ((z + y) - u))) ||
 rewrite((min(x, (y + ((z + w) - u))) - z), min((x - z), ((y + w) - u))) ||
 rewrite((min(x, ((y + (z + w)) + u)) - w), min((x - w), (y + (z + u)))) ||
 rewrite((min(x, ((y + (z + w)) + u)) - z), min((x - z), (y + (w + u)))) ||
 rewrite((min(x, (((y + z) + w) + u)) - y), min((x - y), (z + (w + u)))) ||
 rewrite((min(x, (((y + z) + w) + u)) - z), min((x - z), (y + (w + u)))) ||
 rewrite((min(x, (((y + z) - w) + u)) - y), min((x - y), (z - (w - u)))) ||
 rewrite((min(x, (((y + z) - w) + u)) - z), min((x - z), (y - (w - u)))) ||
 rewrite((min(x, ((y + z) - w)) - y), min((x - y), (z - w))) ||
 rewrite((min(x, ((y + z) - w)) - z), min((x - z), (y - w))) ||
 rewrite((min(x, (((y + z) - w) - u)) - y), min((x - y), (z - (w + u)))) ||
 rewrite((min(x, (((y + z) - w) - u)) - z), min((x - z), (y - (w + u)))) ||
 rewrite((min(x, min(y, (z + (w + u)))) - u), min((min(y, x) - u), (w + z))) ||
 rewrite((min(x, min(y, (z + (w + u)))) - w), min((min(y, x) - w), (u + z))) ||
 rewrite((min(x, min(y, ((z + w) + u))) - w), min((min(y, x) - w), (z + u))) ||
 rewrite((min(x, min(y, ((z + w) + u))) - z), min((min(y, x) - z), (w + u))) ||
 rewrite((min(x, min(y, min(z, (w + (u + v))))) - u), min((min(y, min(z, x)) - u), (v + w))) ||
 rewrite((min(x, min(y, min(z, (w + (u + v))))) - v), min((min(y, min(z, x)) - v), (u + w))) ||
 rewrite((min(x, min(y, min(z, ((w + u) + v)))) - u), min((min(y, min(z, x)) - u), (w + v))) ||
 rewrite((min(x, min(y, min(z, ((w + u) + v)))) - w), min((min(x, min(y, z)) - w), (u + v))) ||
 rewrite((min(x, min(y, min((z + (w + u)), v))) - u), min((min(y, min(x, v)) - u), (w + z))) ||
 rewrite((min(x, min(y, min((z + (w + u)), v))) - w), min((min(x, min(y, v)) - w), (u + z))) ||
 rewrite((min(x, min(y, min(((z + w) + u), v))) - w), min((min(y, min(x, v)) - w), (z + u))) ||
 rewrite((min(x, min(y, min(((z + w) + u), v))) - z), min((min(y, min(x, v)) - z), (w + u))) ||
 rewrite((min(x, min((y + (z + w)), u)) - w), min((min(u, x) - w), (z + y))) ||
 rewrite((min(x, min((y + (z + w)), u)) - z), min((min(u, x) - z), (w + y))) ||
 rewrite((min(x, min(((y + z) + w), u)) - y), min((min(u, x) - y), (z + w))) ||
 rewrite((min(x, min(((y + z) + w), u)) - z), min((min(u, x) - z), (y + w))) ||
 rewrite((min(x, min(min(y, (z + (w + u))), v)) - u), min((min(min(y, x), v) - u), (w + z))) ||
 rewrite((min(x, min(min(y, (z + (w + u))), v)) - w), min((min(y, min(v, x)) - w), (u + z))) ||
 rewrite((min(x, min(min(y, ((z + w) + u)), v)) - w), min((min(y, min(v, x)) - w), (z + u))) ||
 rewrite((min(x, min(min(y, ((z + w) + u)), v)) - z), min((min(y, min(v, x)) - z), (w + u))) ||
 rewrite((min(x, min(min((y + (z + w)), u), v)) - w), min((min(u, min(v, x)) - w), (z + y))) ||
 rewrite((min(x, min(min((y + (z + w)), u), v)) - z), min((min(u, min(v, x)) - z), (w + y))) ||
 rewrite((min(x, min(min(((y + z) + w), u), v)) - y), min((min(u, min(v, x)) - y), (z + w))) ||
 rewrite((min(x, min(min(((y + z) + w), u), v)) - z), min((min(u, min(v, x)) - z), (y + w))) ||
 rewrite((min((x + (y + (z + w))), u) - w), min((u - w), (y + (z + x)))) ||
 rewrite((min((x + (y + (z + w))), u) - z), min((u - z), (y + (x + w)))) ||
 rewrite((min((x + ((y + z) + w)), u) - y), min((u - y), (z + (w + x)))) ||
 rewrite((min((x + ((y + z) + w)), u) - z), min((u - z), (y + (x + w)))) ||
 rewrite((min((x + ((y + z) - w)), u) - y), min(((x + z) - w), (u - y))) ||
 rewrite((min((x + ((y + z) - w)), u) - z), min(((x + y) - w), (u - z))) ||
 rewrite((min(((x + (y + z)) + w), u) - y), min((u - y), (x + (z + w)))) ||
 rewrite((min(((x + (y + z)) + w), u) - z), min((u - z), (x + (y + w)))) ||
 rewrite((min((((x + y) + z) + w), u) - x), min((u - x), (y + (z + w)))) ||
 rewrite((min((((x + y) + z) + w), u) - y), min((u - y), (x + (z + w)))) ||
 rewrite((min((((x + y) - z) + w), u) - x), min(((w + y) - z), (u - x))) ||
 rewrite((min((((x + y) - z) + w), u) - y), min(((w + x) - z), (u - y))) ||
 rewrite((min(((x + y) - z), w) - x), min((y - z), (w - x))) ||
 rewrite((min(((x + y) - z), w) - y), min((x - z), (w - y))) ||
 rewrite((min((((x + y) - z) - w), u) - x), min(((y - z) - w), (u - x))) ||
 rewrite((min((((x + y) - z) - w), u) - y), min(((x - z) - w), (u - y))) ||
 rewrite((min(min(x, (y + (z + w))), u) - w), min((min(x, u) - w), (z + y))) ||
 rewrite((min(min(x, (y + (z + w))), u) - z), min((min(x, u) - z), (w + y))) ||
 rewrite((min(min(x, ((y + z) + w)), u) - y), min((min(x, u) - y), (z + w))) ||
 rewrite((min(min(x, ((y + z) + w)), u) - z), min((min(x, u) - z), (y + w))) ||
 rewrite((min(min(x, min(y, (z + (w + u)))), v) - u), min((min(x, min(y, v)) - u), (w + z))) ||
 rewrite((min(min(x, min(y, (z + (w + u)))), v) - w), min((min(x, min(y, v)) - w), (u + z))) ||
 rewrite((min(min(x, min(y, ((z + w) + u))), v) - w), min((min(x, min(y, v)) - w), (z + u))) ||
 rewrite((min(min(x, min(y, ((z + w) + u))), v) - z), min((min(x, min(y, v)) - z), (w + u))) ||
 rewrite((min(min(x, min((y + (z + w)), u)), v) - w), min((min(x, min(v, u)) - w), (z + y))) ||
 rewrite((min(min(x, min((y + (z + w)), u)), v) - z), min((min(x, min(v, u)) - z), (w + y))) ||
 rewrite((min(min(x, min(((y + z) + w), u)), v) - y), min((min(x, min(v, u)) - y), (z + w))) ||
 rewrite((min(min(x, min(((y + z) + w), u)), v) - z), min((min(x, min(v, u)) - z), (y + w))) ||
 rewrite((min(min((x + (y + z)), w), u) - y), min((min(w, u) - y), (z + x))) ||
 rewrite((min(min((x + (y + z)), w), u) - z), min((min(w, u) - z), (y + x))) ||
 rewrite((min(min(((x + y) + z), w), u) - x), min((min(w, u) - x), (y + z))) ||
 rewrite((min(min(((x + y) + z), w), u) - y), min((min(w, u) - y), (x + z))) ||
 rewrite((min(min(min(x, (y + (z + w))), u), v) - w), min((min(x, min(u, v)) - w), (z + y))) ||
 rewrite((min(min(min(x, (y + (z + w))), u), v) - z), min((min(x, min(u, v)) - z), (w + y))) ||
 rewrite((min(min(min(x, ((y + z) + w)), u), v) - y), min((min(x, min(u, v)) - y), (z + w))) ||
 rewrite((min(min(min(x, ((y + z) + w)), u), v) - z), min((min(x, min(u, v)) - z), (y + w))) ||
 rewrite((min(min(min((x + (y + z)), w), u), v) - y), min((min(w, min(u, v)) - y), (z + x))) ||
 rewrite((min(min(min((x + (y + z)), w), u), v) - z), min((min(w, min(u, v)) - z), (y + x))) ||
 rewrite((min(min(min(((x + y) + z), w), u), v) - x), min((min(w, min(u, v)) - x), (y + z))) ||
 rewrite((min(min(min(((x + y) + z), w), u), v) - y), min((min(w, min(u, v)) - y), (x + z))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(x, y) - (z + (w + min(y, x)))), (0 - (z + w))) ||
 rewrite((min(x, y) - (z + (min(y, x) + w))), (0 - (z + w))) ||
 rewrite((min(x, y) - ((z + min(y, x)) + w)), (0 - (z + w))) ||
 rewrite((min(x, y) - ((min(y, x) + z) + w)), (0 - (z + w))) ||
 rewrite((min(x, (y + z)) - (w + y)), (min(z, (x - y)) - w)) ||
 rewrite((min(x, (y + z)) - (w + z)), (min(y, (x - z)) - w)) ||
 rewrite((min(x, (y + z)) - (y + w)), (min(z, (x - y)) - w)) ||
 rewrite((min(x, (y + z)) - (z + w)), (min(y, (x - z)) - w)) ||
 rewrite((min(x, (y + (z + w))) - (u + w)), (min((x - w), (z + y)) - u)) ||
 rewrite((min(x, (y + (z + w))) - (u + z)), (min((x - z), (w + y)) - u)) ||
 rewrite((min(x, (y + (z + w))) - (w + u)), (min((x - w), (z + y)) - u)) ||
 rewrite((min(x, (y + (z + w))) - (z + u)), (min((x - z), (w + y)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (u + y)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (u + z)), (min((x - z), (y + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (y + u)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (z + u)), (min((x - z), (y + w)) - u)) ||
 rewrite((min(x, ((y + z) - w)) - (y + z)), (min((y - w), (x - z)) - y)) ||
 rewrite((min(x, ((y + z) - w)) - (z + y)), (min((y - w), (x - z)) - y)) ||
 rewrite((min(x, min(y, ((z + w) - u))) - (w + z)), (min((min(y, x) - z), (w - u)) - w)) ||
 rewrite((min(x, min(y, ((z + w) - u))) - (z + w)), (min((min(y, x) - w), (z - u)) - z)) ||
 rewrite((min(x, min(((y + z) - w), u)) - (y + z)), (min((min(u, x) - z), (y - w)) - y)) ||
 rewrite((min(x, min(((y + z) - w), u)) - (z + y)), (min((y - w), (min(u, x) - z)) - y)) ||
 rewrite((min((x + y), z) - (w + x)), (min((z - x), y) - w)) ||
 rewrite((min((x + y), z) - (w + y)), (min((z - y), x) - w)) ||
 rewrite((min((x + y), z) - (x + w)), (min((z - x), y) - w)) ||
 rewrite((min((x + y), z) - (y + w)), (min((z - y), x) - w)) ||
 rewrite((min((x + (y + z)), w) - (u + y)), (min((w - y), (z + x)) - u)) ||
 rewrite((min((x + (y + z)), w) - (u + z)), (min((w - z), (y + x)) - u)) ||
 rewrite((min((x + (y + z)), w) - (y + u)), (min((w - y), (z + x)) - u)) ||
 rewrite((min((x + (y + z)), w) - (z + u)), (min((w - z), (y + x)) - u)) ||
 rewrite((min(((x + y) + z), w) - (u + x)), (min((w - x), (y + z)) - u)) ||
 rewrite((min(((x + y) + z), w) - (u + y)), (min((w - y), (x + z)) - u)) ||
 rewrite((min(((x + y) + z), w) - (x + u)), (min((w - x), (y + z)) - u)) ||
 rewrite((min(((x + y) + z), w) - (y + u)), (min((w - y), (x + z)) - u)) ||
 rewrite((min(((x + y) - z), w) - (x + y)), (min((y - z), (w - x)) - y)) ||
 rewrite((min(((x + y) - z), w) - (y + x)), (min((x - z), (w - y)) - x)) ||
 rewrite((min(min(x, ((y + z) - w)), u) - (y + z)), (min((z - w), (min(x, u) - y)) - z)) ||
 rewrite((min(min(x, ((y + z) - w)), u) - (z + y)), (min((z - w), (min(x, u) - y)) - z)) ||
 rewrite((min(min(((x + y) - z), w), u) - (x + y)), (min((x - z), (min(w, u) - y)) - x)) ||
 rewrite((min(min(((x + y) - z), w), u) - (y + x)), (min((x - z), (min(w, u) - y)) - x)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((min(x, (y + (z*(w + u)))) - (u*z)), min((x - (z*u)), (y + (w*z)))) ||
 rewrite((min(x, (y + (z*(w + u)))) - (w*z)), min((x - (z*w)), (y + (u*z)))) ||
 rewrite((min(x, (y + (z*(w + u)))) - (z*u)), min((x - (z*u)), (y + (z*w)))) ||
 rewrite((min(x, (y + (z*(w + u)))) - (z*w)), min((x - (z*w)), (y + (z*u)))) ||
 rewrite((min(x, (y + ((z + w)*u))) - (u*w)), min((x - (u*w)), (y + (u*z)))) ||
 rewrite((min(x, (y + ((z + w)*u))) - (u*z)), min((x - (u*z)), (y + (u*w)))) ||
 rewrite((min(x, (y + ((z + w)*u))) - (w*u)), min((x - (u*w)), (y + (z*u)))) ||
 rewrite((min(x, (y + ((z + w)*u))) - (z*u)), min((x - (u*z)), (y + (w*u)))) ||
 rewrite((min(x, ((y*(z + w)) + u)) - (w*y)), min((x - (y*w)), (u + (z*y)))) ||
 rewrite((min(x, ((y*(z + w)) + u)) - (y*w)), min((x - (y*w)), (u + (y*z)))) ||
 rewrite((min(x, ((y*(z + w)) + u)) - (y*z)), min((x - (y*z)), (u + (y*w)))) ||
 rewrite((min(x, ((y*(z + w)) + u)) - (z*y)), min((x - (y*z)), (u + (w*y)))) ||
 rewrite((min(x, (((y + z)*w) + u)) - (w*y)), min((x - (w*y)), (u + (w*z)))) ||
 rewrite((min(x, (((y + z)*w) + u)) - (w*z)), min((x - (w*z)), (u + (w*y)))) ||
 rewrite((min(x, (((y + z)*w) + u)) - (y*w)), min((x - (w*y)), (u + (z*w)))) ||
 rewrite((min(x, (((y + z)*w) + u)) - (z*w)), min((x - (w*z)), (u + (y*w)))) ||
 rewrite((min((x + (y*(z + w))), u) - (w*y)), min((u - (y*w)), (x + (z*y)))) ||
 rewrite((min((x + (y*(z + w))), u) - (y*w)), min((u - (y*w)), (x + (y*z)))) ||
 rewrite((min((x + (y*(z + w))), u) - (y*z)), min((u - (y*z)), (x + (y*w)))) ||
 rewrite((min((x + (y*(z + w))), u) - (z*y)), min((u - (y*z)), (x + (w*y)))) ||
 rewrite((min((x + ((y + z)*w)), u) - (w*y)), min((u - (y*w)), (x + (w*z)))) ||
 rewrite((min((x + ((y + z)*w)), u) - (w*z)), min((u - (z*w)), (x + (w*y)))) ||
 rewrite((min((x + ((y + z)*w)), u) - (y*w)), min((u - (y*w)), (x + (z*w)))) ||
 rewrite((min((x + ((y + z)*w)), u) - (z*w)), min((u - (z*w)), (x + (y*w)))) ||
 rewrite((min(((x*(y + z)) + w), u) - (x*y)), min((u - (x*y)), (w + (x*z)))) ||
 rewrite((min(((x*(y + z)) + w), u) - (x*z)), min((u - (x*z)), (w + (x*y)))) ||
 rewrite((min(((x*(y + z)) + w), u) - (y*x)), min((u - (x*y)), (w + (z*x)))) ||
 rewrite((min(((x*(y + z)) + w), u) - (z*x)), min((u - (x*z)), (w + (y*x)))) ||
 rewrite((min((((x + y)*z) + w), u) - (x*z)), min((u - (x*z)), (w + (y*z)))) ||
 rewrite((min((((x + y)*z) + w), u) - (y*z)), min((u - (y*z)), (w + (x*z)))) ||
 rewrite((min((((x + y)*z) + w), u) - (z*x)), min((u - (x*z)), (w + (z*y)))) ||
 rewrite((min((((x + y)*z) + w), u) - (z*y)), min((u - (y*z)), (w + (z*x)))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((min(x, y) - min(x, min(z, (w + min(x, y))))), (0 - min(w, (min(z, x) - min(x, y))))) ||
 rewrite((min(x, y) - min(x, min(z, (w + min(y, x))))), (0 - min(w, (min(z, x) - min(y, x))))) ||
 rewrite((min(x, y) - min(x, min(z, (min(x, y) + w)))), (0 - min(w, (min(x, z) - min(x, y))))) ||
 rewrite((min(x, y) - min(x, min(z, (min(y, x) + w)))), (0 - min(w, (min(x, z) - min(y, x))))) ||
 rewrite((min(x, y) - min(x, min((z + min(x, y)), w))), (0 - min(z, (min(x, w) - min(x, y))))) ||
 rewrite((min(x, y) - min(x, min((z + min(y, x)), w))), (0 - min(z, (min(x, w) - min(y, x))))) ||
 rewrite((min(x, y) - min(x, min((min(x, y) + z), w))), (0 - min(z, (min(x, w) - min(x, y))))) ||
 rewrite((min(x, y) - min(x, min((min(y, x) + z), w))), (0 - min(z, (min(x, w) - min(y, x))))) ||
 rewrite((min(x, y) - min(y, min(z, (w + min(x, y))))), (0 - min(w, (min(z, y) - min(x, y))))) ||
 rewrite((min(x, y) - min(y, min(z, (w + min(y, x))))), (0 - min(w, (min(z, y) - min(y, x))))) ||
 rewrite((min(x, y) - min(y, min(z, (min(x, y) + w)))), (0 - min(w, (min(y, z) - min(x, y))))) ||
 rewrite((min(x, y) - min(y, min(z, (min(y, x) + w)))), (0 - min(w, (min(y, z) - min(y, x))))) ||
 rewrite((min(x, y) - min(y, min((z + min(x, y)), w))), (0 - min(z, (min(y, w) - min(x, y))))) ||
 rewrite((min(x, y) - min(y, min((z + min(y, x)), w))), (0 - min(z, (min(y, w) - min(y, x))))) ||
 rewrite((min(x, y) - min(y, min((min(x, y) + z), w))), (0 - min(z, (min(y, w) - min(x, y))))) ||
 rewrite((min(x, y) - min(y, min((min(y, x) + z), w))), (0 - min(z, (min(y, w) - min(y, x))))) ||
 rewrite((min(x, y) - min(z, (w + (min(x, y) - u)))), (0 - min((w - u), (z - min(x, y))))) ||
 rewrite((min(x, y) - min(z, (w + (min(y, x) - u)))), (0 - min((w - u), (z - min(y, x))))) ||
 rewrite((min(x, y) - min(z, ((min(x, y) - w) + u))), (0 - min((u - w), (z - min(x, y))))) ||
 rewrite((min(x, y) - min(z, ((min(y, x) - w) + u))), (0 - min((u - w), (z - min(y, x))))) ||
 rewrite((min(x, y) - min(z, min(w, (min(y, x) - u)))), max((min(x, y) - min(z, w)), u)) ||
 rewrite((min(x, y) - min(z, min((min(y, x) - w), u))), max(w, (min(y, x) - min(z, u)))) ||
 rewrite((min(x, y) - min((z + (min(x, y) - w)), u)), (0 - min((z - w), (u - min(x, y))))) ||
 rewrite((min(x, y) - min((z + (min(y, x) - w)), u)), (0 - min((z - w), (u - min(y, x))))) ||
 rewrite((min(x, y) - min(((min(x, y) - z) + w), u)), (0 - min((w - z), (u - min(x, y))))) ||
 rewrite((min(x, y) - min(((min(y, x) - z) + w), u)), (0 - min((w - z), (u - min(y, x))))) ||
 rewrite((min(x, y) - min(min(z, (w + min(x, y))), x)), (0 - min(w, (min(z, x) - min(x, y))))) ||
 rewrite((min(x, y) - min(min(z, (w + min(x, y))), y)), (0 - min(w, (min(z, y) - min(x, y))))) ||
 rewrite((min(x, y) - min(min(z, (w + min(y, x))), x)), (0 - min(w, (min(z, x) - min(y, x))))) ||
 rewrite((min(x, y) - min(min(z, (w + min(y, x))), y)), (0 - min(w, (min(z, y) - min(y, x))))) ||
 rewrite((min(x, y) - min(min(z, (min(x, y) + w)), x)), (0 - min(w, (min(z, x) - min(x, y))))) ||
 rewrite((min(x, y) - min(min(z, (min(x, y) + w)), y)), (0 - min(w, (min(z, y) - min(x, y))))) ||
 rewrite((min(x, y) - min(min(z, (min(y, x) + w)), x)), (0 - min(w, (min(z, x) - min(y, x))))) ||
 rewrite((min(x, y) - min(min(z, (min(y, x) + w)), y)), (0 - min(w, (min(z, y) - min(y, x))))) ||
 rewrite((min(x, y) - min(min(z, (min(y, x) - w)), u)), max((min(x, y) - min(z, u)), w)) ||
 rewrite((min(x, y) - min(min((z + min(x, y)), w), x)), (0 - min(z, (min(w, x) - min(x, y))))) ||
 rewrite((min(x, y) - min(min((z + min(x, y)), w), y)), (0 - min(z, (min(w, y) - min(x, y))))) ||
 rewrite((min(x, y) - min(min((z + min(y, x)), w), x)), (0 - min(z, (min(w, x) - min(y, x))))) ||
 rewrite((min(x, y) - min(min((z + min(y, x)), w), y)), (0 - min(z, (min(w, y) - min(y, x))))) ||
 rewrite((min(x, y) - min(min((min(x, y) + z), w), x)), (0 - min(z, (min(w, x) - min(x, y))))) ||
 rewrite((min(x, y) - min(min((min(x, y) + z), w), y)), (0 - min(z, (min(w, y) - min(y, x))))) ||
 rewrite((min(x, y) - min(min((min(y, x) + z), w), x)), (0 - min(z, (min(w, x) - min(x, y))))) ||
 rewrite((min(x, y) - min(min((min(y, x) + z), w), y)), (0 - min(z, (min(w, y) - min(y, x))))) ||
 rewrite((min(x, y) - min(min((min(y, x) - z), w), u)), max(z, (min(y, x) - min(u, w)))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite((max(x, (y + (z - w))) - z), max((x - z), (y - w))) ||
 rewrite((max(x, ((y - z) + w)) - y), max((x - y), (w - z))) ||
 rewrite((max(x, max(y, (z + w))) - w), max((max(y, x) - w), z)) ||
 rewrite((max(x, max(y, (z + w))) - z), max((max(y, x) - z), w)) ||
 rewrite((max(x, max((y + z), w)) - y), max(z, (max(w, x) - y))) ||
 rewrite((max(x, max((y + z), w)) - z), max(y, (max(w, x) - z))) ||
 rewrite((max((x + (y - z)), w) - y), max((x - z), (w - y))) ||
 rewrite((max(((x - y) + z), w) - x), max((z - y), (w - x))) ||
 rewrite((max(max(x, (y + z)), w) - y), max((max(x, w) - y), z)) ||
 rewrite((max(max(x, (y + z)), w) - z), max((max(x, w) - z), y)) ||
 rewrite((max(max((x + y), z), w) - x), max(y, (max(z, w) - x))) ||
 rewrite((max(max((x + y), z), w) - y), max(x, (max(z, w) - y))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((max(x, (y + z)) - (w + y)), (max(z, (x - y)) - w)) ||
 rewrite((max(x, (y + z)) - (w + z)), (max(y, (x - z)) - w)) ||
 rewrite((max(x, (y + z)) - (y + w)), (max(z, (x - y)) - w)) ||
 rewrite((max(x, (y + z)) - (z + w)), (max(y, (x - z)) - w)) ||
 rewrite((max(x, (y + (z + w))) - (u + w)), (max((x - w), (z + y)) - u)) ||
 rewrite((max(x, (y + (z + w))) - (u + z)), (max((x - z), (w + y)) - u)) ||
 rewrite((max(x, (y + (z + w))) - (u + (w + z))), (max((x - z), (w + y)) - (u + w))) ||
 rewrite((max(x, (y + (z + w))) - (w + u)), (max((x - w), (z + y)) - u)) ||
 rewrite((max(x, (y + (z + w))) - (z + u)), (max((x - z), (w + y)) - u)) ||
 rewrite((max(x, (y + (z + w))) - ((w + z) + u)), (max((x - z), (w + y)) - (w + u))) ||
 rewrite((max(x, (y + (z + (w + u)))) - (u + v)), (max((x - u), (z + (w + y))) - v)) ||
 rewrite((max(x, (y + (z + (w + u)))) - (v + u)), (max((x - u), (z + (w + y))) - v)) ||
 rewrite((max(x, (y + (z + (w + u)))) - (v + w)), (max((x - w), (z + (y + u))) - v)) ||
 rewrite((max(x, (y + (z + (w + u)))) - (w + v)), (max((x - w), (z + (y + u))) - v)) ||
 rewrite((max(x, (y + ((z + w) + u))) - (v + w)), (max((x - w), ((z + y) + u)) - v)) ||
 rewrite((max(x, (y + ((z + w) + u))) - (v + z)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, (y + ((z + w) + u))) - (w + v)), (max((x - w), ((z + y) + u)) - v)) ||
 rewrite((max(x, (y + ((z + w) + u))) - (z + v)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, (y + (z*w))) - (u + (w*z))), (max(y, (x - (z*w))) - u)) ||
 rewrite((max(x, (y + (z*w))) - ((w*z) + u)), (max(y, (x - (z*w))) - u)) ||
 rewrite((max(x, ((y + z) + w)) - (u + y)), (max((x - y), (z + w)) - u)) ||
 rewrite((max(x, ((y + z) + w)) - (u + z)), (max((x - z), (y + w)) - u)) ||
 rewrite((max(x, ((y + z) + w)) - (u + (z + y))), (max((x - z), (y + w)) - (u + y))) ||
 rewrite((max(x, ((y + z) + w)) - (y + u)), (max((x - y), (z + w)) - u)) ||
 rewrite((max(x, ((y + z) + w)) - (z + u)), (max((x - z), (y + w)) - u)) ||
 rewrite((max(x, ((y + z) + w)) - ((z + y) + u)), (max((x - y), (z + w)) - (z + u))) ||
 rewrite((max(x, ((y + (z + w)) + u)) - (v + w)), (max((x - w), (y + (z + u))) - v)) ||
 rewrite((max(x, ((y + (z + w)) + u)) - (v + z)), (max((x - z), (y + (u + w))) - v)) ||
 rewrite((max(x, ((y + (z + w)) + u)) - (w + v)), (max((x - w), (y + (z + u))) - v)) ||
 rewrite((max(x, ((y + (z + w)) + u)) - (z + v)), (max((x - z), (y + (u + w))) - v)) ||
 rewrite((max(x, (((y + z) + w) + u)) - (v + y)), (max((x - y), ((z + w) + u)) - v)) ||
 rewrite((max(x, (((y + z) + w) + u)) - (v + z)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, (((y + z) + w) + u)) - (y + v)), (max((x - y), ((z + w) + u)) - v)) ||
 rewrite((max(x, (((y + z) + w) + u)) - (z + v)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, ((y*z) + w)) - (u + (z*y))), (max(w, (x - (y*z))) - u)) ||
 rewrite((max(x, ((y*z) + w)) - ((z*y) + u)), (max(w, (x - (y*z))) - u)) ||
 rewrite((max((x + y), z) - (w + x)), (max((z - x), y) - w)) ||
 rewrite((max((x + y), z) - (w + y)), (max((z - y), x) - w)) ||
 rewrite((max((x + y), z) - (x + w)), (max((z - x), y) - w)) ||
 rewrite((max((x + y), z) - (y + w)), (max((z - y), x) - w)) ||
 rewrite((max((x + (y + z)), w) - (u + y)), (max((w - y), (z + x)) - u)) ||
 rewrite((max((x + (y + z)), w) - (u + z)), (max((w - z), (y + x)) - u)) ||
 rewrite((max((x + (y + z)), w) - (u + (z + y))), (max((w - z), (y + x)) - (u + y))) ||
 rewrite((max((x + (y + z)), w) - (y + u)), (max((w - y), (z + x)) - u)) ||
 rewrite((max((x + (y + z)), w) - (z + u)), (max((w - z), (y + x)) - u)) ||
 rewrite((max((x + (y + z)), w) - ((z + y) + u)), (max((w - y), (z + x)) - (z + u))) ||
 rewrite((max((x + (y + (z + w))), u) - (v + w)), (max((u - w), (y + (z + x))) - v)) ||
 rewrite((max((x + (y + (z + w))), u) - (v + z)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((x + (y + (z + w))), u) - (w + v)), (max((u - w), (y + (z + x))) - v)) ||
 rewrite((max((x + (y + (z + w))), u) - (z + v)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((x + ((y + z) + w)), u) - (v + y)), (max((u - y), ((x + z) + w)) - v)) ||
 rewrite((max((x + ((y + z) + w)), u) - (v + z)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((x + ((y + z) + w)), u) - (y + v)), (max((u - y), ((x + z) + w)) - v)) ||
 rewrite((max((x + ((y + z) + w)), u) - (z + v)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((x + (y*z)), w) - (u + (z*y))), (max((w - (y*z)), x) - u)) ||
 rewrite((max((x + (y*z)), w) - ((z*y) + u)), (max((w - (y*z)), x) - u)) ||
 rewrite((max(((x + y) + z), w) - (u + x)), (max((w - x), (y + z)) - u)) ||
 rewrite((max(((x + y) + z), w) - (u + y)), (max((w - y), (x + z)) - u)) ||
 rewrite((max(((x + y) + z), w) - (u + (y + x))), (max((w - y), (x + z)) - (u + x))) ||
 rewrite((max(((x + y) + z), w) - (x + u)), (max((w - x), (y + z)) - u)) ||
 rewrite((max(((x + y) + z), w) - (y + u)), (max((w - y), (x + z)) - u)) ||
 rewrite((max(((x + y) + z), w) - ((y + x) + u)), (max((w - y), (x + z)) - (x + u))) ||
 rewrite((max(((x + (y + z)) + w), u) - (v + y)), (max((u - y), ((z + x) + w)) - v)) ||
 rewrite((max(((x + (y + z)) + w), u) - (v + z)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max(((x + (y + z)) + w), u) - (y + v)), (max((u - y), ((z + x) + w)) - v)) ||
 rewrite((max(((x + (y + z)) + w), u) - (z + v)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((((x + y) + z) + w), u) - (v + x)), (max((u - x), ((y + z) + w)) - v)) ||
 rewrite((max((((x + y) + z) + w), u) - (v + y)), (max((u - y), ((x + z) + w)) - v)) ||
 rewrite((max((((x + y) + z) + w), u) - (x + v)), (max((u - x), ((y + z) + w)) - v)) ||
 rewrite((max((((x + y) + z) + w), u) - (y + v)), (max((u - y), ((x + z) + w)) - v)) ||
 rewrite((max(((x*y) + z), w) - (u + (y*x))), (max((w - (x*y)), z) - u)) ||
 rewrite((max(((x*y) + z), w) - ((y*x) + u)), (max((w - (x*y)), z) - u)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((max(x, max(y, (z + min(w, u)))) - min(u, w)), max((max(y, x) - min(w, u)), z)) ||
 rewrite((max(x, max(y, (min(z, w) + u))) - min(w, z)), max((max(y, x) - min(z, w)), u)) ||
 rewrite((max(x, max((y + min(z, w)), u)) - min(w, z)), max(y, (max(u, x) - min(w, z)))) ||
 rewrite((max(x, max((min(y, z) + w), u)) - min(z, y)), max(w, (max(u, x) - min(z, y)))) ||
 rewrite((max(max(x, (y + min(z, w))), u) - min(w, z)), max((max(x, u) - min(z, w)), y)) ||
 rewrite((max(max(x, (min(y, z) + w)), u) - min(z, y)), max(w, (max(x, u) - min(z, y)))) ||
 rewrite((max(max((x + min(y, z)), w), u) - min(z, y)), max(x, (max(w, u) - min(z, y)))) ||
 rewrite((max(max((min(x, y) + z), w), u) - min(y, x)), max(z, (max(w, u) - min(y, x)))))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, (y + (z + w)), u) - w), select(x, (y + z), (u - w))) ||
 rewrite((select(x, (y + (z + w)), u) - z), select(x, (y + w), (u - z))) ||
 rewrite((select(x, ((y + z) + w), u) - y), select(x, (z + w), (u - y))) ||
 rewrite((select(x, ((y + z) + w), u) - z), select(x, (y + w), (u - z))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((select(x, (y + z), w) - (u + y)), (select(x, z, (w - y)) - u)) ||
 rewrite((select(x, (y + z), w) - (u + z)), (select(x, y, (w - z)) - u)) ||
 rewrite((select(x, (y + z), w) - (y + u)), (select(x, z, (w - y)) - u)) ||
 rewrite((select(x, (y + z), w) - (z + u)), (select(x, y, (w - z)) - u))))))
