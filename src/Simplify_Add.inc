((b.node_type() == IRNodeType::Add) && (
 rewrite((x + (y + (z + (w - x)))), (y + (z + w))) ||
 rewrite((x + (y + (z + min(w, (u + (v - x)))))), (z + (y + min((w + x), (v + u))))) ||
 rewrite((x + (y + (z + min(w, ((u - x) + v))))), (z + (y + min((x + w), (u + v))))) ||
 rewrite((x + (y + (z + min((w + (u - x)), v)))), (y + (z + min((w + u), (x + v))))) ||
 rewrite((x + (y + (z + min(((w - x) + u), v)))), (y + (z + min((u + w), (x + v))))) ||
 rewrite((x + (y + ((z - x) + w))), (y + (z + w))) ||
 rewrite((x + (y + (min(z, (w + (u - x))) + v))), ((y + min((z + x), (u + w))) + v)) ||
 rewrite((x + (y + (min(z, ((w - x) + u)) + v))), (v + (y + min((x + z), (w + u))))) ||
 rewrite((x + (y + (min((z + (w - x)), u) + v))), (v + (y + min((z + w), (x + u))))) ||
 rewrite((x + (y + (min(((z - x) + w), u) + v))), (v + (y + min((w + z), (x + u))))) ||
 rewrite((x + (y + (z - (w + x)))), (y + (z - w))) ||
 rewrite((x + (y + (z - (x + w)))), (y + (z - w))) ||
 rewrite((x + (y + (z - min(w, min(u, (v + x)))))), (z + (y - min(v, (min(u, w) - x))))) ||
 rewrite((x + (y + (z - min(w, min(u, (x + v)))))), (z + (y - min(v, (min(u, w) - x))))) ||
 rewrite((x + (y + (z - min(w, min((u + x), v))))), (y + (z - min(u, (min(v, w) - x))))) ||
 rewrite((x + (y + (z - min(w, min((x + u), v))))), (y + (z - min(u, (min(v, w) - x))))) ||
 rewrite((x + (y + (z - min(min(w, (u + x)), v)))), (z + (y - min(u, (min(w, v) - x))))) ||
 rewrite((x + (y + (z - min(min(w, (x + u)), v)))), (z + (y - min(u, (min(w, v) - x))))) ||
 rewrite((x + (y + (z - min(min((w + x), u), v)))), (z + (y - min(w, (min(v, u) - x))))) ||
 rewrite((x + (y + (z - min(min((x + w), u), v)))), (z + (y - min(w, (min(v, u) - x))))) ||
 rewrite((x + (y + ((x*c0) - z))), (y - z), (c0 == -1)) ||
 rewrite((x + (y + max(z, (w + (u - x))))), (y + max((z + x), (u + w)))) ||
 rewrite((x + (y + max(z, ((w - x) + u)))), (y + max((x + z), (w + u)))) ||
 rewrite((x + (y + max((z + (w - x)), u))), (y + max((z + w), (x + u)))) ||
 rewrite((x + (y + max(((z - x) + w), u))), (y + max((z + w), (x + u)))) ||
 rewrite((x + ((y + (z - x)) + w)), (y + (z + w))) ||
 rewrite((x + ((y + min(z, (w + (u - x)))) + v)), (y + (min((z + x), (u + w)) + v))) ||
 rewrite((x + ((y + min(z, ((w - x) + u))) + v)), (y + (min((x + z), (w + u)) + v))) ||
 rewrite((x + ((y + min((z + (w - x)), u)) + v)), (y + (min((z + w), (x + u)) + v))) ||
 rewrite((x + ((y + min(((z - x) + w), u)) + v)), (y + (min((z + w), (x + u)) + v))) ||
 rewrite((x + (((y - x) + z) + w)), ((y + z) + w)) ||
 rewrite((x + ((min(y, (z + (w - x))) + u) + v)), ((min((y + x), (w + z)) + u) + v)) ||
 rewrite((x + ((min(y, ((z - x) + w)) + u) + v)), ((min((x + y), (z + w)) + u) + v)) ||
 rewrite((x + ((min((y + (z - x)), w) + u) + v)), (u + (min((y + z), (x + w)) + v))) ||
 rewrite((x + ((min(((y - x) + z), w) + u) + v)), (u + (min((z + y), (x + w)) + v))) ||
 rewrite((x + ((y - (x + z)) + w)), (y - (z - w))) ||
 rewrite((x + ((y - (z + x)) + w)), (y - (z - w))) ||
 rewrite((x + ((y - min(z, min(w, (u + x)))) + v)), (y - (min(u, (min(w, z) - x)) - v))) ||
 rewrite((x + ((y - min(z, min(w, (x + u)))) + v)), (y - (min(u, (min(w, z) - x)) - v))) ||
 rewrite((x + ((y - min(z, min((w + x), u))) + v)), ((y - min(w, (min(u, z) - x))) + v)) ||
 rewrite((x + ((y - min(z, min((x + w), u))) + v)), ((y - min(w, (min(u, z) - x))) + v)) ||
 rewrite((x + ((y - min(min(z, (w + x)), u)) + v)), (y - (min(w, (min(z, u) - x)) - v))) ||
 rewrite((x + ((y - min(min(z, (x + w)), u)) + v)), (y - (min(w, (min(z, u) - x)) - v))) ||
 rewrite((x + ((y - min(min((x + z), w), u)) + v)), (y - (min(z, (min(u, w) - x)) - v))) ||
 rewrite((x + ((y - min(min((z + x), w), u)) + v)), (y - (min(z, (min(u, w) - x)) - v))) ||
 rewrite((x + (((x*c0) - y) + z)), (z - y), (c0 == -1)) ||
 rewrite((x + ((y*c0) + (z + (w*c1)))), (x + (z + ((w - y)*fold((0 - c0))))), ((c0 + c1) == 0)) ||
 rewrite((x + ((y*c0) + ((z*c1) + w))), (x + (w + ((z - y)*fold((0 - c0))))), ((c0 + c1) == 0)) ||
 rewrite((x + (max(y, (z + (w - x))) + u)), (max((y + x), (w + z)) + u)) ||
 rewrite((x + (max(y, ((z - x) + w)) + u)), (max((x + y), (z + w)) + u)) ||
 rewrite((x + (max((y + (z - x)), w) + u)), (u + max((z + y), (x + w)))) ||
 rewrite((x + (max(((y - x) + z), w) + u)), (u + max((z + y), (x + w)))))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((x + (y - (x + z))), (y - z)) ||
 rewrite((x + (y - (z + x))), (y - z)) ||
 rewrite((x + (y - (z + (w + x)))), (y - (z + w))) ||
 rewrite((x + (y - (z + (w + (u + x))))), (y - (z + (w + u)))) ||
 rewrite((x + (y - (z + (w + (x + u))))), (y - (z + (w + u)))) ||
 rewrite((x + (y - (z + (x + w)))), (y - (z + w))) ||
 rewrite((x + (y - (z + ((w + x) + u)))), (y - (z + (w + u)))) ||
 rewrite((x + (y - (z + ((x + w) + u)))), (y - (z + (w + u)))) ||
 rewrite((x + (y - (z + min(w, (u + x))))), (y - (z + min(u, (w - x))))) ||
 rewrite((x + (y - (z + min(w, (x + u))))), ((y - min(u, (w - x))) - z)) ||
 rewrite((x + (y - (z + min(w, min(u, (v + x)))))), (y - (z + min(v, (min(u, w) - x))))) ||
 rewrite((x + (y - (z + min(w, min(u, (x + v)))))), (y - (z + min(v, (min(u, w) - x))))) ||
 rewrite((x + (y - (z + min(w, min((u + x), v))))), (y - (z + min(u, (min(v, w) - x))))) ||
 rewrite((x + (y - (z + min(w, min((x + u), v))))), (y - (z + min(u, (min(v, w) - x))))) ||
 rewrite((x + (y - (z + min((w + x), u)))), (y - (z + min((u - x), w)))) ||
 rewrite((x + (y - (z + min((x + w), u)))), (y - (z + min(w, (u - x))))) ||
 rewrite((x + (y - (z + min(min(w, (u + x)), v)))), ((y - min(u, (min(w, v) - x))) - z)) ||
 rewrite((x + (y - (z + min(min(w, (x + u)), v)))), (y - (z + min(u, (min(w, v) - x))))) ||
 rewrite((x + (y - (z + min(min((w + x), u), v)))), (y - (z + min(w, (min(v, u) - x))))) ||
 rewrite((x + (y - (z + min(min((x + w), u), v)))), (y - (z + min(w, (min(v, u) - x))))) ||
 rewrite((x + (y - (z + select(w, u, (v + y))))), (x - (z + select(w, (u - y), v)))) ||
 rewrite((x + (y - (z + select(w, u, (y + v))))), (x - (z + select(w, (u - y), v)))) ||
 rewrite((x + (y - ((x + z) + w))), (y - (z + w))) ||
 rewrite((x + (y - ((z + x) + w))), (y - (z + w))) ||
 rewrite((x + (y - ((z + (w + x)) + u))), (y - (z + (w + u)))) ||
 rewrite((x + (y - ((z + (x + w)) + u))), (y - (z + (w + u)))) ||
 rewrite((x + (y - (((x + z) + w) + u))), (y - ((z + w) + u))) ||
 rewrite((x + (y - (((z + x) + w) + u))), (y - ((z + w) + u))) ||
 rewrite((x + (y - (min(z, (w + x)) + u))), (y - (min(w, (z - x)) + u))) ||
 rewrite((x + (y - (min(z, (x + w)) + u))), ((y - min((z - x), w)) - u)) ||
 rewrite((x + (y - (min(z, min(w, (u + x))) + v))), (y - (min((min(w, z) - x), u) + v))) ||
 rewrite((x + (y - (min(z, min(w, (x + u))) + v))), (y - (min((min(z, w) - x), u) + v))) ||
 rewrite((x + (y - (min(z, min((w + x), u)) + v))), (y - (min(w, (min(u, z) - x)) + v))) ||
 rewrite((x + (y - (min(z, min((x + w), u)) + v))), (y - (min(w, (min(u, z) - x)) + v))) ||
 rewrite((x + (y - (min((x + z), w) + u))), (y - (min((w - x), z) + u))) ||
 rewrite((x + (y - (min((z + x), w) + u))), (y - (min((w - x), z) + u))) ||
 rewrite((x + (y - (min(min(z, (w + x)), u) + v))), (y - (min((min(z, u) - x), w) + v))) ||
 rewrite((x + (y - (min(min(z, (x + w)), u) + v))), (y - (min((min(z, u) - x), w) + v))) ||
 rewrite((x + (y - (min(min((x + z), w), u) + v))), (y - (min((min(w, u) - x), z) + v))) ||
 rewrite((x + (y - (min(min((z + x), w), u) + v))), (y - (min((min(w, u) - x), z) + v))) ||
 rewrite((x + (y - (select(z, w, (u + y)) + v))), (x - (select(z, (w - y), u) + v))) ||
 rewrite((x + (y - (select(z, w, (y + u)) + v))), (x - (select(z, (w - y), u) + v))) ||
 rewrite((x + (y - min(z, (w + x)))), (y - min(w, (z - x)))) ||
 rewrite((x + (y - min(z, (w + (x - u))))), (y - min((z - x), (w - u)))) ||
 rewrite((x + (y - min(z, (w + (y - u))))), (x - min((z - y), (w - u)))) ||
 rewrite((x + (y - min(z, (x + w)))), (y - min(w, (z - x)))) ||
 rewrite((x + (y - min(z, ((x - w) + u)))), (y - min((z - x), (u - w)))) ||
 rewrite((x + (y - min(z, ((y - w) + u)))), (x - min((z - y), (u - w)))) ||
 rewrite((x + (y - min(z, (x - w)))), (y + max(w, (x - z)))) ||
 rewrite((x + (y - min(z, min(w, (u + (v + y)))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min(w, (u + (y + v)))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min(w, ((u + y) + v))))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite((x + (y - min(z, min(w, ((y + u) + v))))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite((x + (y - min(z, min((w + (u + y)), v)))), (x - min((min(v, z) - y), (u + w)))) ||
 rewrite((x + (y - min(z, min((w + (y + u)), v)))), (x - min((min(v, z) - y), (u + w)))) ||
 rewrite((x + (y - min(z, min(((w + y) + u), v)))), (x - min((min(v, z) - y), (w + u)))) ||
 rewrite((x + (y - min(z, min(((y + w) + u), v)))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min((x + z), w))), (y - min(z, (w - x)))) ||
 rewrite((x + (y - min((z + x), w))), (y - min((w - x), z))) ||
 rewrite((x + (y - min((z + (x - w)), u))), (y - min((z - w), (u - x)))) ||
 rewrite((x + (y - min((z + (y - w)), u))), (x - min((z - w), (u - y)))) ||
 rewrite((x + (y - min(((x - z) + w), u))), (y - min((w - z), (u - x)))) ||
 rewrite((x + (y - min(((y - z) + w), u))), (x - min((w - z), (u - y)))) ||
 rewrite((x + (y - min((x - z), w))), (y + max(z, (x - w)))) ||
 rewrite((x + (y - min(min(z, (w + (u + y))), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, (w + (y + u))), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, ((w + y) + u)), v))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min(min(z, ((y + w) + u)), v))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min(min((z + (w + y)), u), v))), (x - min((min(u, v) - y), (w + z)))) ||
 rewrite((x + (y - min(min((z + (y + w)), u), v))), (x - min((min(u, v) - y), (w + z)))) ||
 rewrite((x + (y - min(min(((y + z) + w), u), v))), (x - min((min(u, v) - y), (z + w)))) ||
 rewrite((x + (y - min(min(((z + y) + w), u), v))), (x - min((min(u, v) - y), (z + w)))) ||
 rewrite((x + (y - max(z, (w + (y - x))))), (x - max((z - y), (w - x)))) ||
 rewrite((x + (y - max(z, ((y - x) + w)))), (x - max((z - y), (w - x)))) ||
 rewrite((x + (y - max((z + (y - x)), w))), (x - max((z - x), (w - y)))) ||
 rewrite((x + (y - max(((y - x) + z), w))), (x - max((z - x), (w - y)))) ||
 rewrite((x + (y - select(z, w, (u + (v + y))))), (x - select(z, (w - y), (v + u)))) ||
 rewrite((x + (y - select(z, w, (u + (y + v))))), (x - select(z, (w - y), (u + v)))) ||
 rewrite((x + (y - select(z, w, ((u + y) + v)))), (x - select(z, (w - y), (v + u)))) ||
 rewrite((x + (y - select(z, w, ((y + u) + v)))), (x - select(z, (w - y), (u + v)))) ||
 rewrite((x + (y - select(z, x, c0))), (y + select(z, 0, x)), (c0 == 0)) ||
 rewrite((x + (y - select(z, (w + (u + x)), v))), (y - select(z, (w + u), (v - x)))) ||
 rewrite((x + (y - select(z, (w + (u + y)), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, (w + (x + u)), v))), (y - select(z, (w + u), (v - x)))) ||
 rewrite((x + (y - select(z, (w + (y + u)), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, ((w + x) + u), v))), (y - select(z, (w + u), (v - x)))) ||
 rewrite((x + (y - select(z, ((w + y) + u), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, ((x + w) + u), v))), (y - select(z, (w + u), (v - x)))) ||
 rewrite((x + (y - select(z, ((y + w) + u), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (((y/c0) + ((y + c1)/c2)) - z)), (x + (y - z)), ((((c1/c2) == 0) && (((c1 + 1)/c2) == 1)) && (((-1736/c0) + ((c1 + -1736)/c2)) == -1736))) ||
 rewrite((x + (max(y, (z - x)) - z)), max((y - (z - x)), 0)) ||
 rewrite((x + (max((y - x), z) - y)), max(((x + z) - y), 0)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((x + (y*(c0 - z))), (x - (y*z)), (c0 == 0)) ||
 rewrite((x + ((c0 - y)*z)), (x - (y*z)), (c0 == 0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((x + max(y, (z - min(max(w, u), min(u, v))))), (x + max(y, (z - min(u, v))))) ||
 rewrite((x + max(y, (z - min(max(w, u), min(v, u))))), (x + max(y, (z - min(u, v))))) ||
 rewrite((x + max(y, (z - min(max(w, u), min(v, w))))), (x + max(y, (z - min(w, v))))) ||
 rewrite((x + max(y, (z - min(max(w, u), min(w, v))))), (x + max(y, (z - min(w, v))))) ||
 rewrite((x + max((y - min(max(z, w), min(u, w))), v)), (x + max((y - min(w, u)), v))) ||
 rewrite((x + max((y - min(max(z, w), min(u, z))), v)), (x + max((y - min(z, u)), v))) ||
 rewrite((x + max((y - min(max(z, w), min(w, u))), v)), (x + max((y - min(w, u)), v))) ||
 rewrite((x + max((y - min(max(z, w), min(z, u))), v)), (x + max((y - min(z, u)), v))))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + (y + (z + select(w, c0, c1)))) + c2), ((x + (y + select(w, fold((c0 + c2)), fold((c1 + c2))))) + z)) ||
 rewrite(((x + (y + (select(z, c0, c1) + w))) + c2), (w + (x + (y + select(z, fold((c0 + c2)), fold((c1 + c2))))))) ||
 rewrite(((x + (y + (z - w))) + w), (x + (y + z))) ||
 rewrite(((x + (y + min(z, (w + (u - v))))) + v), ((x + min((z + v), (u + w))) + y)) ||
 rewrite(((x + (y + min(z, ((w - u) + v)))) + u), (x + (y + min((z + u), (v + w))))) ||
 rewrite(((x + (y + min((z + (w - u)), v))) + u), (x + (y + min((z + w), (u + v))))) ||
 rewrite(((x + (y + min(((z - w) + u), v))) + w), (x + (y + min((u + z), (w + v))))) ||
 rewrite(((x + (y + select(z, c0, c1))) + c2), (y + (x + select(z, fold((c0 + c2)), fold((c1 + c2)))))) ||
 rewrite(((x + ((y + select(z, c0, c1)) + w)) + c2), ((w + x) + (y + select(z, fold((c0 + c2)), fold((c1 + c2)))))) ||
 rewrite(((x + ((select(y, c0, c1) + z) + w)) + c2), ((x + (select(y, fold((c0 + c2)), fold((c1 + c2))) + w)) + z)) ||
 rewrite(((x + ((y - z) + w)) + z), (x + (y + w))) ||
 rewrite(((x + (min(y, (z + (w - u))) + v)) + u), ((x + min((y + u), (w + z))) + v)) ||
 rewrite(((x + (min(y, ((z - w) + u)) + v)) + w), ((min((w + y), (z + u)) + v) + x)) ||
 rewrite(((x + (min((y + (z - w)), u) + v)) + w), ((x + min((y + z), (w + u))) + v)) ||
 rewrite(((x + (min(((y - z) + w), u) + v)) + z), (x + (v + min((y + w), (z + u))))) ||
 rewrite(((x + (select(y, c0, c1) + z)) + c2), (z + (x + select(y, fold((c0 + c2)), fold((c1 + c2)))))) ||
 rewrite(((x + (y - (z + w))) + w), (x + (y - z))) ||
 rewrite(((x + (y - (z + w))) + z), (x + (y - w))) ||
 rewrite(((x + (y - (z + select(w, c0, c1)))) + c2), ((x + (y + select(w, fold((c2 - c0)), fold((c2 - c1))))) - z)) ||
 rewrite(((x + (y - (select(z, c0, c1) + w))) + c2), ((x + (y + select(z, fold((c2 - c0)), fold((c2 - c1))))) - w)) ||
 rewrite(((x + (y - min(z, min(w, (u + v))))) + u), (x + (y - min(v, (min(w, z) - u))))) ||
 rewrite(((x + (y - min(z, min(w, (u + v))))) + v), (x + (y - min(u, (min(w, z) - v))))) ||
 rewrite(((x + (y - min(z, min((w + u), v)))) + u), (x + (y - min(w, (min(v, z) - u))))) ||
 rewrite(((x + (y - min(z, min((w + u), v)))) + w), (x + (y - min(u, (min(v, z) - w))))) ||
 rewrite(((x + (y - min(min(z, (w + u)), v))) + u), ((x - min(w, (min(z, v) - u))) + y)) ||
 rewrite(((x + (y - min(min(z, (w + u)), v))) + w), ((x - min(u, (min(z, v) - w))) + y)) ||
 rewrite(((x + (y - min(min((z + w), u), v))) + w), (x + (y - min(z, (min(v, u) - w))))) ||
 rewrite(((x + (y - min(min((z + w), u), v))) + z), (x + (y - min(w, (min(v, u) - z))))) ||
 rewrite(((x + ((y + select(z, c0, c1)) - w)) + c2), ((x - w) + (y + select(z, fold((c0 + c2)), fold((c1 + c2)))))) ||
 rewrite(((x + ((select(y, c0, c1) + z) - w)) + c2), ((x + (select(y, fold((c0 + c2)), fold((c1 + c2))) - w)) + z)) ||
 rewrite(((x + ((y*c0) - z)) + y), (x - z), (c0 == -1)) ||
 rewrite(((x + (select(y, c0, c1) - z)) + c2), ((x + select(y, fold((c0 + c2)), fold((c1 + c2)))) - z)) ||
 rewrite(((x + max(y, (z + (w - u)))) + u), (max((y + u), (w + z)) + x)) ||
 rewrite(((x + max(y, ((z - w) + u))) + w), (x + max((y + w), (u + z)))) ||
 rewrite(((x + max((y + (z - w)), u)) + w), (x + max((y + z), (u + w)))) ||
 rewrite(((x + max(((y - z) + w), u)) + z), (x + max((w + y), (z + u)))) ||
 rewrite(((x + select(y, c0, c1)) + c2), (x + select(y, fold((c0 + c2)), fold((c1 + c2))))) ||
 rewrite((((x + (y + select(z, c0, c1))) + w) + c2), (w + (x + (y + select(z, fold((c0 + c2)), fold((c1 + c2))))))) ||
 rewrite((((x + (select(y, c0, c1) + z)) + w) + c2), (((x + select(y, fold((c0 + c2)), fold((c1 + c2)))) + w) + z)) ||
 rewrite((((x + (y - z)) + w) + z), (x + (y + w))) ||
 rewrite((((x + min(y, (z + (w - u)))) + v) + u), ((min((y + u), (w + z)) + x) + v)) ||
 rewrite((((x + min(y, ((z - w) + u))) + v) + w), (x + (min((y + w), (u + z)) + v))) ||
 rewrite((((x + min((y + (z - w)), u)) + v) + w), (x + (min((y + z), (w + u)) + v))) ||
 rewrite((((x + min(((y - z) + w), u)) + v) + z), (x + (min((y + w), (z + u)) + v))) ||
 rewrite((((x + select(y, c0, c1)) + z) + c2), (z + (x + select(y, fold((c0 + c2)), fold((c1 + c2)))))) ||
 rewrite(((((x + select(y, c0, c1)) + z) + w) + c2), (((select(y, fold((c0 + c2)), fold((c1 + c2))) + z) + w) + x)) ||
 rewrite(((((select(x, c0, c1) + y) + z) + w) + c2), (((select(x, fold((c0 + c2)), fold((c1 + c2))) + z) + w) + y)) ||
 rewrite(((((x - y) + z) + w) + y), ((x + z) + w)) ||
 rewrite((((min(x, (y + (z - w))) + u) + v) + w), ((min((x + w), (z + y)) + u) + v)) ||
 rewrite((((min(x, ((y - z) + w)) + u) + v) + z), ((min((z + x), (y + w)) + u) + v)) ||
 rewrite((((min((x + (y - z)), w) + u) + v) + z), (u + (min((x + y), (z + w)) + v))) ||
 rewrite((((min(((x - y) + z), w) + u) + v) + y), (u + (min((z + x), (y + w)) + v))) ||
 rewrite((((select(x, c0, c1) + y) + z) + c2), ((select(x, fold((c0 + c2)), fold((c1 + c2))) + z) + y)) ||
 rewrite((((x - (y + z)) + w) + y), (x - (z - w))) ||
 rewrite((((x - (y + z)) + w) + z), (x - (y - w))) ||
 rewrite((((x - (y + select(z, c0, c1))) + w) + c2), (((x + select(z, fold((c2 - c0)), fold((c2 - c1)))) + w) - y)) ||
 rewrite((((x - (select(y, c0, c1) + z)) + w) + c2), (((x + select(y, fold((c2 - c0)), fold((c2 - c1)))) + w) - z)) ||
 rewrite((((x - min(y, min(z, (w + u)))) + v) + u), ((x - min((min(z, y) - u), w)) + v)) ||
 rewrite((((x - min(y, min(z, (w + u)))) + v) + w), (x - (min(u, (min(z, y) - w)) - v))) ||
 rewrite((((x - min(y, min((z + w), u))) + v) + w), ((x - min(z, (min(u, y) - w))) + v)) ||
 rewrite((((x - min(y, min((z + w), u))) + v) + z), ((x - min(w, (min(u, y) - z))) + v)) ||
 rewrite((((x - min(min(y, (z + w)), u)) + v) + w), (x - (min(z, (min(y, u) - w)) - v))) ||
 rewrite((((x - min(min(y, (z + w)), u)) + v) + z), (x - (min(w, (min(y, u) - z)) - v))) ||
 rewrite((((x - min(min((y + z), w), u)) + v) + y), ((x - min((min(w, u) - y), z)) + v)) ||
 rewrite((((x - min(min((y + z), w), u)) + v) + z), ((x - min((min(w, u) - z), y)) + v)) ||
 rewrite(((((x + select(y, c0, c1)) - z) + w) + c2), (((select(y, fold((c0 + c2)), fold((c1 + c2))) - z) + w) + x)) ||
 rewrite(((((select(x, c0, c1) + y) - z) + w) + c2), (((select(x, fold((c0 + c2)), fold((c1 + c2))) - z) + w) + y)) ||
 rewrite(((((x*c0) - y) + z) + x), (z - y), (c0 == -1)) ||
 rewrite((((select(x, c0, c1) - y) + z) + c2), ((select(x, fold((c0 + c2)), fold((c1 + c2))) + z) - y)) ||
 rewrite((((x*c0) + (y + (z*c1))) + w), (y + (((z - x)*fold((0 - c0))) + w)), ((c0 + c1) == 0)) ||
 rewrite((((x*c0) + ((y*c1) + z)) + w), ((z + ((y - x)*fold((0 - c0)))) + w), ((c0 + c1) == 0)) ||
 rewrite(((max(x, (y + (z - w))) + u) + w), (max((x + w), (z + y)) + u)) ||
 rewrite(((max(x, ((y - z) + w)) + u) + z), (max((x + z), (y + w)) + u)) ||
 rewrite(((max((x + (y - z)), w) + u) + z), (u + max((x + y), (z + w)))) ||
 rewrite(((max(((x - y) + z), w) + u) + y), (u + max((z + x), (y + w)))) ||
 rewrite(((select(x, c0, c1) + y) + c2), (y + select(x, fold((c0 + c2)), fold((c1 + c2))))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x + y) + (x + (x + z))), (y + (z + (x*3)))) ||
 rewrite(((x + y) + (x + (z + x))), (y + (z + (x*3)))) ||
 rewrite(((x + y) + (y + (y + z))), (x + (z + (y*3)))) ||
 rewrite(((x + y) + (y + (z + y))), (x + (z + (y*3)))) ||
 rewrite(((x + y) + ((x + z) + x)), (y + ((x*3) + z))) ||
 rewrite(((x + y) + ((y + z) + y)), (x + ((y*3) + z))) ||
 rewrite(((x + y) + ((z + x) + x)), (y + ((x*3) + z))) ||
 rewrite(((x + y) + ((z + y) + y)), (x + ((y*3) + z))) ||
 rewrite(((x + (x + y)) + (x + z)), ((x*3) + (y + z))) ||
 rewrite(((x + (x + y)) + (z + x)), ((x*3) + (z + y))) ||
 rewrite(((x + (y + x)) + (x + z)), ((x*3) + (y + z))) ||
 rewrite(((x + (y + x)) + (z + x)), ((x*3) + (z + y))) ||
 rewrite((((x + y) + x) + (x + z)), ((x*3) + (y + z))) ||
 rewrite((((x + y) + x) + (z + x)), ((x*3) + (z + y))) ||
 rewrite((((x + y) + y) + (y + z)), ((y*3) + (x + z))) ||
 rewrite((((x + y) + y) + (z + y)), ((y*3) + (z + x))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(((x - (y + z)) + y), (x - z)) ||
 rewrite(((x - (y + z)) + z), (x - y)) ||
 rewrite(((x - (y + (z + w))) + w), (x - (y + z))) ||
 rewrite(((x - (y + (z + w))) + z), (x - (y + w))) ||
 rewrite(((x - (y + (z + (w + u)))) + u), (x - (y + (z + w)))) ||
 rewrite(((x - (y + (z + (w + u)))) + w), (x - (y + (z + u)))) ||
 rewrite(((x - (y + ((z + w) + u))) + w), (x - (y + (z + u)))) ||
 rewrite(((x - (y + ((z + w) + u))) + z), (x - (y + (w + u)))) ||
 rewrite(((x - (y + min(z, (w + u)))) + u), (x - (y + min(w, (z - u))))) ||
 rewrite(((x - (y + min(z, (w + u)))) + w), ((x - min(u, (z - w))) - y)) ||
 rewrite(((x - (y + min(z, min(w, (u + v))))) + u), (x - (y + min(v, (min(w, z) - u))))) ||
 rewrite(((x - (y + min(z, min(w, (u + v))))) + v), (x - (y + min(u, (min(w, z) - v))))) ||
 rewrite(((x - (y + min(z, min((w + u), v)))) + u), (x - (y + min(w, (min(v, z) - u))))) ||
 rewrite(((x - (y + min(z, min((w + u), v)))) + w), (x - (y + min(u, (min(v, z) - w))))) ||
 rewrite(((x - (y + min((z + w), u))) + w), (x - (y + min((u - w), z)))) ||
 rewrite(((x - (y + min((z + w), u))) + z), (x - (y + min(w, (u - z))))) ||
 rewrite(((x - (y + min(min(z, (w + u)), v))) + u), ((x - min(w, (min(z, v) - u))) - y)) ||
 rewrite(((x - (y + min(min(z, (w + u)), v))) + w), ((x - min(u, (min(z, v) - w))) - y)) ||
 rewrite(((x - (y + min(min((z + w), u), v))) + w), (x - (y + min(z, (min(v, u) - w))))) ||
 rewrite(((x - (y + min(min((z + w), u), v))) + z), (x - (y + min(w, (min(v, u) - z))))) ||
 rewrite(((x - (y + select(z, w, (u + x)))) + v), (v - (y + select(z, (w - x), u)))) ||
 rewrite(((x - (y + select(z, w, (x + u)))) + v), (v - (y + select(z, (w - x), u)))) ||
 rewrite(((x - ((y + z) + w)) + y), (x - (z + w))) ||
 rewrite(((x - ((y + z) + w)) + z), (x - (y + w))) ||
 rewrite(((x - ((y + (z + w)) + u)) + w), (x - (y + (z + u)))) ||
 rewrite(((x - ((y + (z + w)) + u)) + z), (x - (y + (w + u)))) ||
 rewrite(((x - (((y + z) + w) + u)) + y), (x - ((z + w) + u))) ||
 rewrite(((x - (((y + z) + w) + u)) + z), (x - ((y + w) + u))) ||
 rewrite(((x - (min(y, (z + w)) + u)) + w), (x - (min(z, (y - w)) + u))) ||
 rewrite(((x - (min(y, (z + w)) + u)) + z), ((x - min((y - z), w)) - u)) ||
 rewrite(((x - (min(y, min(z, (w + u))) + v)) + u), (x - (min((min(z, y) - u), w) + v))) ||
 rewrite(((x - (min(y, min(z, (w + u))) + v)) + w), ((x - min((min(z, y) - w), u)) - v)) ||
 rewrite(((x - (min(y, min((z + w), u)) + v)) + w), (x - (min(z, (min(u, y) - w)) + v))) ||
 rewrite(((x - (min(y, min((z + w), u)) + v)) + z), (x - (min(w, (min(u, y) - z)) + v))) ||
 rewrite(((x - (min((y + z), w) + u)) + y), (x - (min((w - y), z) + u))) ||
 rewrite(((x - (min((y + z), w) + u)) + z), (x - (min((w - z), y) + u))) ||
 rewrite(((x - (min(min(y, (z + w)), u) + v)) + w), ((x - min(z, (min(y, u) - w))) - v)) ||
 rewrite(((x - (min(min(y, (z + w)), u) + v)) + z), ((x - min((min(y, u) - z), w)) - v)) ||
 rewrite(((x - (min(min((y + z), w), u) + v)) + y), (x - (min((min(w, u) - y), z) + v))) ||
 rewrite(((x - (min(min((y + z), w), u) + v)) + z), (x - (min((min(w, u) - z), y) + v))) ||
 rewrite(((x - (select(y, z, (w + x)) + u)) + v), (v - (select(y, (z - x), w) + u))) ||
 rewrite(((x - (select(y, z, (x + w)) + u)) + v), (v - (select(y, (z - x), w) + u))) ||
 rewrite(((x - min(y, (z + w))) + w), (x - min(z, (y - w)))) ||
 rewrite(((x - min(y, (z + w))) + z), (x - min(w, (y - z)))) ||
 rewrite(((x - min(y, (z + (w - u)))) + w), (x - min((z - u), (y - w)))) ||
 rewrite(((x - min(y, (z + (x - w)))) + u), (u - min((y - x), (z - w)))) ||
 rewrite(((x - min(y, ((x - z) + w))) + u), (u - min((y - x), (w - z)))) ||
 rewrite(((x - min(y, ((z - w) + u))) + z), (x - min((y - z), (u - w)))) ||
 rewrite(((x - min(y, (z - w))) + z), (x + max(w, (z - y)))) ||
 rewrite(((x - min(y, min(z, (w + (u + x))))) + v), (v - min((min(z, y) - x), (u + w)))) ||
 rewrite(((x - min(y, min(z, (w + (x + u))))) + v), (v - min((min(z, y) - x), (u + w)))) ||
 rewrite(((x - min(y, min(z, ((w + x) + u)))) + v), (v - min((min(z, y) - x), (w + u)))) ||
 rewrite(((x - min(y, min(z, ((x + w) + u)))) + v), (v - min((min(z, y) - x), (w + u)))) ||
 rewrite(((x - min(y, min((z + (w + x)), u))) + v), (v - min((min(u, y) - x), (w + z)))) ||
 rewrite(((x - min(y, min((z + (x + w)), u))) + v), (v - min((min(u, y) - x), (w + z)))) ||
 rewrite(((x - min(y, min(((x + z) + w), u))) + v), (v - min((min(u, y) - x), (z + w)))) ||
 rewrite(((x - min(y, min(((z + x) + w), u))) + v), (v - min((min(u, y) - x), (z + w)))) ||
 rewrite(((x - min((y + z), w)) + y), (x - min(z, (w - y)))) ||
 rewrite(((x - min((y + z), w)) + z), (x - min((w - z), y))) ||
 rewrite(((x - min((y + (x - z)), w)) + u), (u - min((y - z), (w - x)))) ||
 rewrite(((x - min((y + (z - w)), u)) + z), (x - min((y - w), (u - z)))) ||
 rewrite(((x - min(((x - y) + z), w)) + u), (u - min((z - y), (w - x)))) ||
 rewrite(((x - min(((y - z) + w), u)) + y), (x - min((w - z), (u - y)))) ||
 rewrite(((x - min((y - z), w)) + y), (x + max(z, (y - w)))) ||
 rewrite(((x - min(min(y, (z + (w + x))), u)) + v), (v - min((min(y, u) - x), (w + z)))) ||
 rewrite(((x - min(min(y, (z + (x + w))), u)) + v), (v - min((min(y, u) - x), (w + z)))) ||
 rewrite(((x - min(min(y, ((x + z) + w)), u)) + v), (v - min((min(y, u) - x), (z + w)))) ||
 rewrite(((x - min(min(y, ((z + x) + w)), u)) + v), (v - min((min(y, u) - x), (z + w)))) ||
 rewrite(((x - min(min((y + (x + z)), w), u)) + v), (v - min((min(w, u) - x), (z + y)))) ||
 rewrite(((x - min(min((y + (z + x)), w), u)) + v), (v - min((min(w, u) - x), (z + y)))) ||
 rewrite(((x - min(min(((x + y) + z), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - min(min(((y + x) + z), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - max(y, (z + (x - w)))) + w), (w - max((y - x), (z - w)))) ||
 rewrite(((x - max(y, ((x - z) + w))) + z), (z - max((y - x), (w - z)))) ||
 rewrite(((x - max((y + (x - z)), w)) + z), (z - max((y - z), (w - x)))) ||
 rewrite(((x - max(((x - y) + z), w)) + y), (y - max((z - y), (w - x)))) ||
 rewrite(((x - select(y, c0, c1)) + c2), (x + select(y, fold((c2 - c0)), fold((c2 - c1))))) ||
 rewrite(((x - select(y, z, c0)) + z), (x + select(y, 0, z)), (c0 == 0)) ||
 rewrite(((x - select(y, z, (w + (u + x)))) + v), (v - select(y, (z - x), (u + w)))) ||
 rewrite(((x - select(y, z, (w + (x + u)))) + v), (v - select(y, (z - x), (w + u)))) ||
 rewrite(((x - select(y, z, ((w + x) + u))) + v), (v - select(y, (z - x), (u + w)))) ||
 rewrite(((x - select(y, z, ((x + w) + u))) + v), (v - select(y, (z - x), (u + w)))) ||
 rewrite(((x - select(y, (z + (w + u)), v)) + u), (x - select(y, (z + w), (v - u)))) ||
 rewrite(((x - select(y, (z + (w + u)), v)) + w), (x - select(y, (z + u), (v - w)))) ||
 rewrite(((x - select(y, (z + (w + x)), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, (z + (x + w)), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, ((x + z) + w), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, ((z + w) + u), v)) + w), (x - select(y, (z + u), (v - w)))) ||
 rewrite(((x - select(y, ((z + w) + u), v)) + z), (x - select(y, (w + u), (v - z)))) ||
 rewrite(((x - select(y, ((z + x) + w), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((((x/c0) + ((x + c1)/c2)) - y) + z), ((x - y) + z), ((((c1/c2) == 0) && (((c1 + 1)/c2) == 1)) && (((-1736/c0) + ((c1 + -1736)/c2)) == -1736))) ||
 rewrite(((max(x, (y - z)) - y) + z), max((x - (y - z)), 0)) ||
 rewrite(((max((x - y), z) - x) + y), max(((y + z) - x), 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x - y) + (y + z)), (x + z)) ||
 rewrite(((x - y) + (z + y)), (x + z)) ||
 rewrite(((x - y) + (z + (w + y))), (x + (z + w))) ||
 rewrite(((x - y) + (z + (y + w))), (x + (z + w))) ||
 rewrite(((x - y) + (z + (w - x))), (z + (w - y))) ||
 rewrite(((x - y) + ((y + z) + w)), (x + (z + w))) ||
 rewrite(((x - y) + ((z + y) + w)), (x + (z + w))) ||
 rewrite(((x - y) + ((z - x) + w)), ((w - y) + z)) ||
 rewrite(((x - (y + z)) + (w + y)), (x - (z - w))) ||
 rewrite(((x - (y + z)) + (w + z)), (x - (y - w))) ||
 rewrite(((x - (y + z)) + (x + (w + y))), (x - (z - (x + w)))) ||
 rewrite(((x - (y + z)) + (x + (w + z))), (x - (y - (x + w)))) ||
 rewrite(((x - (y + z)) + (x + (y + w))), (x - (z - (x + w)))) ||
 rewrite(((x - (y + z)) + (x + (z + w))), (x - (y - (x + w)))) ||
 rewrite(((x - (y + z)) + (y + w)), (x - (z - w))) ||
 rewrite(((x - (y + z)) + (z + w)), (x - (y - w))) ||
 rewrite(((x - (y + z)) + ((w + y) + x)), ((x - z) + (w + x))) ||
 rewrite(((x - (y + z)) + ((w + z) + x)), ((x - y) + (w + x))) ||
 rewrite(((x - (y + z)) + ((y + w) + x)), ((x - z) + (w + x))) ||
 rewrite(((x - (y + z)) + ((z + w) + x)), ((x - y) + (w + x))) ||
 rewrite(((x - (y + (z + w))) + (u + (v + w))), ((x - (y + z)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (v + z))), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (w + v))), ((x - (y + z)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (z + v))), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (w + z)), ((x - (y - z)) - z)) ||
 rewrite(((x - (y + (z + w))) + ((u + w) + v)), ((x - (y + z)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + ((u + z) + v)), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + ((w + u) + v)), ((x - (y + z)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + ((z + u) + v)), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - (y + (z + (w + u)))) + (u + v)), ((x - (y + (z + w))) + v)) ||
 rewrite(((x - (y + (z + (w + u)))) + (v + u)), ((x - (y + (z + w))) + v)) ||
 rewrite(((x - (y + (z + (w + u)))) + (v + w)), ((x - (y + (z + u))) + v)) ||
 rewrite(((x - (y + (z + (w + u)))) + (w + v)), ((x - (y + (z + u))) + v)) ||
 rewrite(((x - (y + ((z + w) + u))) + (v + w)), ((x - (y + (z + u))) + v)) ||
 rewrite(((x - (y + ((z + w) + u))) + (v + z)), ((x - (y + (w + u))) + v)) ||
 rewrite(((x - (y + ((z + w) + u))) + (w + v)), ((x - (y + (z + u))) + v)) ||
 rewrite(((x - (y + ((z + w) + u))) + (z + v)), ((x - (y + (w + u))) + v)) ||
 rewrite(((x - (y + (z*w))) + (u + (v*w))), ((w*(v - z)) + (u + (x - y)))) ||
 rewrite(((x - (y + (z*w))) + (u + (v*z))), (x + (((v - w)*z) + (u - y)))) ||
 rewrite(((x - (y + (z*w))) + (u + (w*v))), ((w*(v - z)) + (u + (x - y)))) ||
 rewrite(((x - (y + (z*w))) + (u + (z*v))), (x + (((v - w)*z) + (u - y)))) ||
 rewrite(((x - (y + (z*w))) + ((u*w) + v)), ((w*(u - z)) + ((x - y) + v))) ||
 rewrite(((x - (y + (z*w))) + ((u*z) + v)), ((x + (z*(u - w))) - (y - v))) ||
 rewrite(((x - (y + (z*w))) + ((w*u) + v)), ((w*(u - z)) + ((x - y) + v))) ||
 rewrite(((x - (y + (z*w))) + ((z*u) + v)), ((x + (z*(u - w))) - (y - v))) ||
 rewrite(((x - ((y + z) + w)) + (u + (v + y))), (x - ((z + w) - (u + v)))) ||
 rewrite(((x - ((y + z) + w)) + (u + (v + z))), (x - ((y + w) - (u + v)))) ||
 rewrite(((x - ((y + z) + w)) + (u + (y + v))), (x - ((z + w) - (u + v)))) ||
 rewrite(((x - ((y + z) + w)) + (u + (z + v))), (x - ((y + w) - (u + v)))) ||
 rewrite(((x - ((y + z) + w)) + (z + y)), (x + ((y - w) - y))) ||
 rewrite(((x - ((y + z) + w)) + ((u + y) + v)), ((x - (z + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + ((u + z) + v)), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + ((y + u) + v)), ((x - (z + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + ((z + u) + v)), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - ((y + (z + w)) + u)) + (v + w)), (x - ((y + (z + u)) - v))) ||
 rewrite(((x - ((y + (z + w)) + u)) + (v + z)), (x - ((y + (w + u)) - v))) ||
 rewrite(((x - ((y + (z + w)) + u)) + (w + v)), (x - ((y + (z + u)) - v))) ||
 rewrite(((x - ((y + (z + w)) + u)) + (z + v)), (x - ((y + (w + u)) - v))) ||
 rewrite(((x - (((y + z) + w) + u)) + (v + y)), (x - (((z + w) + u) - v))) ||
 rewrite(((x - (((y + z) + w) + u)) + (v + z)), (x - (((y + w) + u) - v))) ||
 rewrite(((x - (((y + z) + w) + u)) + (y + v)), (x - (((z + w) + u) - v))) ||
 rewrite(((x - (((y + z) + w) + u)) + (z + v)), (x - (((y + w) + u) - v))) ||
 rewrite(((x - ((y*z) + w)) + (u + (v*y))), (x + ((y*(v - z)) + (u - w)))) ||
 rewrite(((x - ((y*z) + w)) + (u + (v*z))), ((z*(v - y)) + (u + (x - w)))) ||
 rewrite(((x - ((y*z) + w)) + (u + (y*v))), (x + ((y*(v - z)) + (u - w)))) ||
 rewrite(((x - ((y*z) + w)) + (u + (z*v))), ((z*(v - y)) + (u + (x - w)))) ||
 rewrite(((x - ((y*z) + w)) + ((u*y) + v)), ((x + ((u - z)*y)) - (w - v))) ||
 rewrite(((x - ((y*z) + w)) + ((u*z) + v)), ((z*(u - y)) + ((x - w) + v))) ||
 rewrite(((x - ((y*z) + w)) + ((y*u) + v)), ((x + ((u - z)*y)) - (w - v))) ||
 rewrite(((x - ((y*z) + w)) + ((z*u) + v)), ((z*(u - y)) + ((x - w) + v))) ||
 rewrite(((x - (y*c0)) + (z + (w + (u*c1)))), ((x - ((y + u)*c0)) + (z + w)), ((c0 + c1) == 0)) ||
 rewrite(((x - (y*c0)) + (z + ((w*c1) + u))), ((x + (z + u)) - ((w + y)*fold((0 - c1)))), ((c0 + c1) == 0)) ||
 rewrite(((x - (y*c0)) + (z + (w*c1))), (z + (x - ((y + w)*c0))), ((c0 + c1) == 0)) ||
 rewrite(((x - (y*c0)) + (z + (y*c1))), (x + z), (c0 == c1)) ||
 rewrite(((x - (y*c0)) + ((z + (w*c1)) + u)), ((x + (u + z)) - ((w + y)*fold((0 - c1)))), ((c0 + c1) == 0)) ||
 rewrite(((x - (y*c0)) + (((z*c1) + w) + u)), (x - ((((y + z)*c0) - w) - u)), ((c0 + c1) == 0)) ||
 rewrite(((x - (y*c0)) + ((y*c1) + z)), (x + z), (c0 == c1)) ||
 rewrite(((x - (y*c0)) + ((z*c1) + w)), (x - (((y + z)*c0) - w)), ((c0 + c1) == 0)) ||
 rewrite(((x - (y*c0)) + ((z*c1) + (w*c2))), ((x + (z*c1)) - ((y + w)*c0)), ((c0 + c2) == 0)) ||
 rewrite(((x - (y*z)) + (w + (z*y))), (x + w)) ||
 rewrite(((x - (y*z)) + ((z*y) + w)), (x + w)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x - y) + ((y + z) - w)), (x + (z - w))) ||
 rewrite(((x - y) + ((z + y) - w)), (x + (z - w))))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
 rewrite(((x*(c0 - y)) + z), (z - (x*y)), (c0 == 0)) ||
 rewrite((((c0 - x)*y) + z), (z - (x*y)), (c0 == 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*c0) + (y + (z + (w*c1)))), (z + (y + ((w - x)*c1))), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((z*c1) + w))), (w + (y + ((z - x)*c1))), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + (z - (w + (x*c1))))), (y + (z - w)), (c0 == c1)) ||
 rewrite(((x*c0) + (y + (z - ((x*c1) + w)))), (y + (z - w)), (c0 == c1)) ||
 rewrite(((x*c0) + (y + (z*c1))), (((z + x)*c1) + y), (c0 == c1)) ||
 rewrite(((x*c0) + (y + ((x + z)*c1))), (y + (z*c1)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((x + (z*c1))*c2))), (y + (z*fold((c1*c2)))), ((c0 + c2) == 0)) ||
 rewrite(((x*c0) + (y + ((z + x)*c1))), (y + (z*c1)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + (((z*c1) + x)*c2))), (y + (z*fold((c1*c2)))), ((c0 + c2) == 0)) ||
 rewrite(((x*c0) + ((y + (z*c1)) + w)), ((y + ((z - x)*c1)) + w), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((y*c1) + z) + w)), ((((y - x)*c1) + z) + w), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((y - (z + (x*c1))) + w)), (y - (z - w)), (c0 == c1)) ||
 rewrite(((x*c0) + ((y - ((x*c1) + z)) + w)), (y - (z - w)), (c0 == c1)) ||
 rewrite(((x*c0) + ((y*c1) + z)), (((y + x)*c1) + z), (c0 == c1)) ||
 rewrite(((x*c0) + (((x + y)*c1) + z)), ((y*c1) + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((x + (y*c1))*c2) + z)), (z + (y*fold((c1*c2)))), ((c0 + c2) == 0)) ||
 rewrite(((x*c0) + (((y + x)*c1) + z)), ((y*c1) + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((((y*c1) + x)*c2) + z)), (z + (y*fold((c1*c2)))), ((c0 + c2) == 0)) ||
 rewrite(((x*c0) + (select(y, z, c1) + (w*c2))), (select(y, z, fold(((c1 + c2) - c2))) + ((w + x)*c2)), (c0 == c2)) ||
 rewrite(((x*y) + (z + (w + (u + (v*x))))), (u + (z + (w + (x*(v + y)))))) ||
 rewrite(((x*y) + (z + (w + (u + (v*y))))), (u + (z + (w + (y*(v + x)))))) ||
 rewrite(((x*y) + (z + (w + (u + (x*v))))), (u + (z + (w + (x*(y + v)))))) ||
 rewrite(((x*y) + (z + (w + (u + (y*v))))), (u + (z + (w + (y*(x + v)))))) ||
 rewrite(((x*y) + (z + (w + ((u*x) + v)))), (v + (z + (w + ((u + y)*x))))) ||
 rewrite(((x*y) + (z + (w + ((u*y) + v)))), (v + (z + (w + ((u + x)*y))))) ||
 rewrite(((x*y) + (z + (w + ((x*u) + v)))), (v + (z + (w + ((y + u)*x))))) ||
 rewrite(((x*y) + (z + (w + ((y*u) + v)))), (v + (z + (w + ((x + u)*y))))) ||
 rewrite(((x*y) + (z + (w + (u*x)))), (w + (z + (x*(u + y))))) ||
 rewrite(((x*y) + (z + (w + (u*y)))), (w + (z + (y*(u + x))))) ||
 rewrite(((x*y) + (z + (w + (x*u)))), (w + (z + (x*(y + u))))) ||
 rewrite(((x*y) + (z + (w + (y*u)))), (w + (z + (y*(x + u))))) ||
 rewrite(((x*y) + (z + ((w + (u*x)) + v))), (w + (z + ((x*(u + y)) + v)))) ||
 rewrite(((x*y) + (z + ((w + (u*y)) + v))), (w + (z + ((y*(u + x)) + v)))) ||
 rewrite(((x*y) + (z + ((w + (x*u)) + v))), (w + (z + ((x*(y + u)) + v)))) ||
 rewrite(((x*y) + (z + ((w + (y*u)) + v))), (w + (z + ((y*(x + u)) + v)))) ||
 rewrite(((x*y) + (z + (((w*x) + u) + v))), (u + (z + (((w + y)*x) + v)))) ||
 rewrite(((x*y) + (z + (((w*y) + u) + v))), (u + (z + (((w + x)*y) + v)))) ||
 rewrite(((x*y) + (z + (((x*w) + u) + v))), (u + (z + (((y + w)*x) + v)))) ||
 rewrite(((x*y) + (z + (((y*w) + u) + v))), (u + (z + (((x + w)*y) + v)))) ||
 rewrite(((x*y) + (z + ((w*x) + u))), (u + (z + ((w + y)*x)))) ||
 rewrite(((x*y) + (z + ((w*y) + u))), (u + (z + ((w + x)*y)))) ||
 rewrite(((x*y) + (z + ((x*w) + u))), (u + (z + ((y + w)*x)))) ||
 rewrite(((x*y) + (z + ((y*w) + u))), (u + (z + ((x + w)*y)))) ||
 rewrite(((x*y) + (z + (w - (u + (y*x))))), (z + (w - u))) ||
 rewrite(((x*y) + (z + (w - ((y*x) + u)))), (z + (w - u))) ||
 rewrite(((x*y) + (z + (w*x))), (z + (x*(w + y)))) ||
 rewrite(((x*y) + (z + (w*y))), (z + (y*(x + w)))) ||
 rewrite(((x*y) + (z + (x*w))), (z + (x*(y + w)))) ||
 rewrite(((x*y) + (z + (y*w))), (z + (y*(x + w)))) ||
 rewrite(((x*y) + ((z + (w + (u*x))) + v)), (w + ((z + (x*(u + y))) + v))) ||
 rewrite(((x*y) + ((z + (w + (u*y))) + v)), (w + ((z + (y*(u + x))) + v))) ||
 rewrite(((x*y) + ((z + (w + (x*u))) + v)), (w + ((z + (x*(y + u))) + v))) ||
 rewrite(((x*y) + ((z + (w + (y*u))) + v)), (w + ((z + (y*(x + u))) + v))) ||
 rewrite(((x*y) + ((z + ((w*x) + u)) + v)), (u + ((z + ((w + y)*x)) + v))) ||
 rewrite(((x*y) + ((z + ((w*y) + u)) + v)), (u + ((z + ((w + x)*y)) + v))) ||
 rewrite(((x*y) + ((z + ((x*w) + u)) + v)), (u + ((z + ((y + w)*x)) + v))) ||
 rewrite(((x*y) + ((z + ((y*w) + u)) + v)), (u + ((z + ((x + w)*y)) + v))) ||
 rewrite(((x*y) + ((z + (w*x)) + u)), (z + ((x*(w + y)) + u))) ||
 rewrite(((x*y) + ((z + (w*y)) + u)), (z + ((y*(w + x)) + u))) ||
 rewrite(((x*y) + ((z + (x*w)) + u)), (z + ((x*(y + w)) + u))) ||
 rewrite(((x*y) + ((z + (y*w)) + u)), (z + ((y*(x + w)) + u))) ||
 rewrite(((x*y) + (((z + (w*x)) + u) + v)), (z + (((x*(w + y)) + u) + v))) ||
 rewrite(((x*y) + (((z + (w*y)) + u) + v)), (z + (((y*(w + x)) + u) + v))) ||
 rewrite(((x*y) + (((z + (x*w)) + u) + v)), (z + (((x*(y + w)) + u) + v))) ||
 rewrite(((x*y) + (((z + (y*w)) + u) + v)), (z + (((y*(x + w)) + u) + v))) ||
 rewrite(((x*y) + ((((x*z) + w) + u) + v)), (w + ((((y + z)*x) + u) + v))) ||
 rewrite(((x*y) + ((((y*z) + w) + u) + v)), (w + ((((x + z)*y) + u) + v))) ||
 rewrite(((x*y) + ((((z*x) + w) + u) + v)), (w + ((((z + y)*x) + u) + v))) ||
 rewrite(((x*y) + ((((z*y) + w) + u) + v)), (w + ((((z + x)*y) + u) + v))) ||
 rewrite(((x*y) + (((x*z) + w) + u)), (w + (((y + z)*x) + u))) ||
 rewrite(((x*y) + (((y*z) + w) + u)), (w + (((x + z)*y) + u))) ||
 rewrite(((x*y) + (((z*x) + w) + u)), (w + (((z + y)*x) + u))) ||
 rewrite(((x*y) + (((z*y) + w) + u)), (w + (((z + x)*y) + u))) ||
 rewrite(((x*y) + ((z - (w + (y*x))) + u)), (z - (w - u))) ||
 rewrite(((x*y) + ((z - ((y*x) + w)) + u)), (z - (w - u))) ||
 rewrite(((x*y) + ((x*z) + w)), (w + (x*(y + z)))) ||
 rewrite(((x*y) + ((y*z) + w)), (w + (y*(x + z)))) ||
 rewrite(((x*y) + ((z*x) + w)), ((x*(y + z)) + w)) ||
 rewrite(((x*y) + ((z*y) + w)), ((y*(x + z)) + w)) ||
 rewrite(((x*(y + z)) + (w + (u*(z + y)))), (((y + z)*(x + u)) + w)) ||
 rewrite(((x*(y + z)) + (w + ((z + y)*u))), (w + ((y + z)*(x + u)))) ||
 rewrite(((x*(y + z)) + ((w*(z + y)) + u)), (((y + z)*(x + w)) + u)) ||
 rewrite(((x*(y + z)) + (((z + y)*w) + u)), (((x + w)*(z + y)) + u)) ||
 rewrite((((x + y)*z) + (w + (u*(y + x)))), (((u + z)*(y + x)) + w)) ||
 rewrite((((x + y)*z) + (w + ((y + x)*u))), (((u + z)*(y + x)) + w)) ||
 rewrite((((x + y)*z) + ((w*(y + x)) + u)), (((w + z)*(y + x)) + u)) ||
 rewrite((((x + y)*z) + (((y + x)*w) + u)), (((w + z)*(y + x)) + u)) ||
 rewrite(((min(x, y)*c0) + (z + (w*c1))), (((min(x, y) + w)*c1) + z), (((c1*-4) + ((c0*3) + c1)) == 0)) ||
 rewrite(((min(x, y)*c0) + ((z*c1) + w)), (((min(x, y) + z)*c1) + w), (((c1*-4) + ((c0*3) + c1)) == 0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x*2) + (y - x)), (y + x)) ||
 rewrite(((x*c0) + (y - x)), (y + x), (c0 == 2)) ||
 rewrite(((x*c0) + (y - (z + (w + (u*c1))))), (((x - u)*c0) + (y - (z + w))), (c0 == c1)) ||
 rewrite(((x*c0) + (y - (z + (w + (u*c1))))), ((y - w) - (z + ((x + u)*fold((0 - c0))))), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y - (z + ((w*c1) + u)))), ((y - z) + (((x - w)*c0) - u)), (c0 == c1)) ||
 rewrite(((x*c0) + (y - (z + ((w*c1) + u)))), (y - (u + (z + ((x + w)*fold((0 - c0)))))), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y - (z + (w*c1)))), (((x - w)*c0) + (y - z)), (c0 == c1)) ||
 rewrite(((x*c0) + (y - ((z + (w*c1)) + u))), (((y + ((x - w)*c0)) - u) - z), (c0 == c1)) ||
 rewrite(((x*c0) + (y - ((z + (w*c1)) + u))), ((y - z) - (u + ((x + w)*fold((0 - c0))))), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y - (((z*c1) + w) + u))), (((y + ((x - z)*c0)) - u) - w), (c0 == c1)) ||
 rewrite(((x*c0) + (y - (((z*c1) + w) + u))), (y - (w + (((x + z)*fold((0 - c0))) + u))), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y - ((z*c1) + w))), ((((x - z)*c0) - w) + y), (c0 == c1)) ||
 rewrite(((x*c0) + ((y*c1) - z)), (((y + x)*c1) - z), (c0 == c1)) ||
 rewrite(((x*c0) + ((y*c1) - (z + (w*c2)))), (((x - w)*c0) + ((y*c1) - z)), (c0 == c2)) ||
 rewrite(((x*c0) + ((y*c1) - ((z*c2) + w))), (((x - z)*c0) + ((y*c1) - w)), (c0 == c2)) ||
 rewrite(((x*c0) + (((c1 - (x*c2))*c3) - y)), (fold((((c1 - c2)*c3) + c0)) - y), ((c1*c3) == (((c1 - c2)*c3) + c0))) ||
 rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((((0 - c1)*c2) + c0) == 0)) ||
 rewrite(((x*y) + (z - (w + (u + (v*x))))), (z - (u + (w - (x*(y - v)))))) ||
 rewrite(((x*y) + (z - (w + (u + (v*y))))), (z - (u + (w - (y*(x - v)))))) ||
 rewrite(((x*y) + (z - (w + (u + (x*v))))), (z - (w + (u - (x*(y - v)))))) ||
 rewrite(((x*y) + (z - (w + (u + (y*v))))), (z - (w + (u - (y*(x - v)))))) ||
 rewrite(((x*y) + (z - (w + ((u*x) + v)))), (z - (v + (w - ((y - u)*x))))) ||
 rewrite(((x*y) + (z - (w + ((u*y) + v)))), (z - (v + (w - ((x - u)*y))))) ||
 rewrite(((x*y) + (z - (w + ((x*u) + v)))), (z - (v + (w - ((y - u)*x))))) ||
 rewrite(((x*y) + (z - (w + ((y*u) + v)))), (z - (v + (w - ((x - u)*y))))) ||
 rewrite(((x*y) + (z - (w + (u*x)))), (z - (w - (x*(y - u))))) ||
 rewrite(((x*y) + (z - (w + (u*y)))), (z - (w - (y*(x - u))))) ||
 rewrite(((x*y) + (z - (w + (x*u)))), (z - (w - (x*(y - u))))) ||
 rewrite(((x*y) + (z - (w + (y*u)))), (z - (w - (y*(x - u))))) ||
 rewrite(((x*y) + (z - ((w + (u*x)) + v))), (((z + ((y - u)*x)) - v) - w)) ||
 rewrite(((x*y) + (z - ((w + (u*y)) + v))), (((z + ((x - u)*y)) - v) - w)) ||
 rewrite(((x*y) + (z - ((w + (x*u)) + v))), (z - (w - ((x*(y - u)) - v)))) ||
 rewrite(((x*y) + (z - ((w + (y*u)) + v))), (z - (w - ((y*(x - u)) - v)))) ||
 rewrite(((x*y) + (z - (((w*x) + u) + v))), ((z - u) + (((y - w)*x) - v))) ||
 rewrite(((x*y) + (z - (((w*y) + u) + v))), ((z - u) + (((x - w)*y) - v))) ||
 rewrite(((x*y) + (z - (((x*w) + u) + v))), (z - (u - (((y - w)*x) - v)))) ||
 rewrite(((x*y) + (z - (((y*w) + u) + v))), (z - (u - (((x - w)*y) - v)))) ||
 rewrite(((x*y) + (z - ((w*x) + u))), (((x*(y - w)) - u) + z)) ||
 rewrite(((x*y) + (z - ((w*y) + u))), (((y*(x - w)) - u) + z)) ||
 rewrite(((x*y) + (z - ((x*w) + u))), (z - (u - ((y - w)*x)))) ||
 rewrite(((x*y) + (z - ((y*w) + u))), (z - (u - ((x - w)*y)))) ||
 rewrite(((x*y) + ((x*z) - w)), ((x*(z + y)) - w)) ||
 rewrite(((x*y) + ((y*z) - w)), ((y*(z + x)) - w)) ||
 rewrite(((x*y) + ((z*x) - w)), ((x*(y + z)) - w)) ||
 rewrite(((x*y) + ((z*y) - w)), ((y*(x + z)) - w)) ||
 rewrite((((x + (y*c0))*c1) + ((y*c2) - z)), ((x*c1) - z), (((c0*c1) + c2) == 0)) ||
 rewrite(((((x*c0) + y)*c1) + ((x*c2) - z)), ((y*c1) - z), (((c0*c1) + c2) == 0)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Sub) && (
 rewrite((min(x, (y + (z + w))) + (y - (u + w))), (min((x - w), (z + y)) + (y - u))) ||
 rewrite((min(x, (y + (z + w))) + (y - (u + z))), (min((x - z), (w + y)) + (y - u))) ||
 rewrite((min(x, (y + (z + w))) + (y - (w + u))), (min((x - w), (z + y)) + (y - u))) ||
 rewrite((min(x, (y + (z + w))) + (y - (z + u))), (min((x - z), (w + y)) + (y - u))) ||
 rewrite((min(x, (y + (z + (w - u)))) + (u - w)), (u + min((z - (u - y)), (x - w)))) ||
 rewrite((min(x, (y + ((z - w) + u))) + (w - z)), (w + min((y - (w - u)), (x - z)))) ||
 rewrite((min(x, ((y + z) + w)) + (w - (u + y))), (min((x - y), (z + w)) + (w - u))) ||
 rewrite((min(x, ((y + z) + w)) + (w - (u + z))), (min((x - z), (y + w)) + (w - u))) ||
 rewrite((min(x, ((y + z) + w)) + (w - (y + u))), (min((x - y), (z + w)) + (w - u))) ||
 rewrite((min(x, ((y + z) + w)) + (w - (z + u))), (min((x - z), (y + w)) + (w - u))) ||
 rewrite((min(x, ((y + (z - w)) + u)) + (w - z)), (w + min((y - (w - u)), (x - z)))) ||
 rewrite((min(x, (((y - z) + w) + u)) + (z - y)), (z + min((u - (z - w)), (x - y)))) ||
 rewrite((min((x + (y + z)), w) + (x - (u + y))), (min((w - y), (z + x)) + (x - u))) ||
 rewrite((min((x + (y + z)), w) + (x - (u + z))), (min((w - z), (y + x)) + (x - u))) ||
 rewrite((min((x + (y + z)), w) + (x - (y + u))), (min((w - y), (z + x)) + (x - u))) ||
 rewrite((min((x + (y + z)), w) + (x - (z + u))), (min((w - z), (y + x)) + (x - u))) ||
 rewrite((min((x + (y + (z - w))), u) + (w - z)), (w + min((y - (w - x)), (u - z)))) ||
 rewrite((min((x + ((y - z) + w)), u) + (z - y)), (z + min((x - (z - w)), (u - y)))) ||
 rewrite((min(((x + y) + z), w) + (z - (u + x))), (min((w - x), (y + z)) + (z - u))) ||
 rewrite((min(((x + y) + z), w) + (z - (u + y))), (min((w - y), (x + z)) + (z - u))) ||
 rewrite((min(((x + y) + z), w) + (z - (x + u))), (min((w - x), (y + z)) + (z - u))) ||
 rewrite((min(((x + y) + z), w) + (z - (y + u))), (min((w - y), (x + z)) + (z - u))) ||
 rewrite((min(((x + (y - z)) + w), u) + (z - y)), (z + min((x - (z - w)), (u - y)))) ||
 rewrite((min((((x - y) + z) + w), u) + (y - x)), (y + min(((z + w) - y), (u - x)))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite((max(x, (y - min(max(z, w), min(u, w)))) + v), (max(x, (y - min(w, u))) + v)) ||
 rewrite((max(x, (y - min(max(z, w), min(u, z)))) + v), (max(x, (y - min(z, u))) + v)) ||
 rewrite((max(x, (y - min(max(z, w), min(w, u)))) + v), (max(x, (y - min(w, u))) + v)) ||
 rewrite((max(x, (y - min(max(z, w), min(z, u)))) + v), (max(x, (y - min(z, u))) + v)) ||
 rewrite((max((x - min(max(y, z), min(w, y))), u) + v), (max((x - min(y, w)), u) + v)) ||
 rewrite((max((x - min(max(y, z), min(w, z))), u) + v), (max((x - min(z, w)), u) + v)) ||
 rewrite((max((x - min(max(y, z), min(y, w))), u) + v), (max((x - min(y, w)), u) + v)) ||
 rewrite((max((x - min(max(y, z), min(z, w))), u) + v), (max((x - min(z, w)), u) + v)) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((max(x, (y + z)) + (w - y)), (max(z, (x - y)) + w)) ||
 rewrite((max(x, (y + z)) + (w - z)), (max(y, (x - z)) + w)) ||
 rewrite((max(x, (y + (z - w))) + (w - z)), max((x - (z - w)), y)) ||
 rewrite((max(x, ((y - z) + w)) + (z - y)), max((x - (y - z)), w)) ||
 rewrite((max((x + y), z) + (w - x)), (max((z - x), y) + w)) ||
 rewrite((max((x + y), z) + (w - y)), (max((z - y), x) + w)) ||
 rewrite((max((x + (y - z)), w) + (z - y)), max(x, ((z + w) - y))) ||
 rewrite((max(((x - y) + z), w) + (y - x)), max(z, ((y + w) - x))))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, y, (z + c0)) + c1), select(x, (y + c1), z), ((c0 + c1) == 0)) ||
 rewrite((select(x, (y + c0), (z + c1)) + c2), select(x, (y + fold((c0 + c2))), z), ((c1 + c2) == 0)) ||
 rewrite((select(x, (c0 - y), c1) + c2), (fold((c0 + c2)) - select(x, y, fold((c0 - c1)))), ((((0 - c1) - c2) + (c1 + c2)) == 0)) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((select(x, (y + (z + w)), u) + (v - w)), (select(x, (y + z), (u - w)) + v)) ||
 rewrite((select(x, (y + (z + w)), u) + (v - z)), (select(x, (y + w), (u - z)) + v)) ||
 rewrite((select(x, ((y + z) + w), u) + (v - y)), (v + select(x, (z + w), (u - y)))) ||
 rewrite((select(x, ((y + z) + w), u) + (v - z)), (v + select(x, (y + w), (u - z))))))))
