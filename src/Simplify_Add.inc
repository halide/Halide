((b.node_type() == IRNodeType::Add) && (
 rewrite((x + (y + (z + (w - x)))), (y + (z + w))) ||
 rewrite((x + (y + (z + min(w, (u + (v - x)))))), (y + (z + min((v + u), (w + x))))) ||
 rewrite((x + (y + (z + min(w, ((u - x) + v))))), (y + (z + min((w + x), (u + v))))) ||
 rewrite((x + (y + (z + min((w + (u - x)), v)))), (y + (z + min((w + u), (x + v))))) ||
 rewrite((x + (y + (z + min(((w - x) + u), v)))), (y + (z + min((v + x), (u + w))))) ||
 rewrite((x + (y + ((z - x) + w))), (y + (z + w))) ||
 rewrite((x + (y + (min(z, (w + (u - x))) + v))), (v + (y + min((w + u), (x + z))))) ||
 rewrite((x + (y + (min(z, ((w - x) + u)) + v))), (v + (y + min((z + x), (u + w))))) ||
 rewrite((x + (y + (min((z + (w - x)), u) + v))), (v + (y + min((z + w), (x + u))))) ||
 rewrite((x + (y + (min(((z - x) + w), u) + v))), (v + (y + min((z + w), (u + x))))) ||
 rewrite((x + (y + (z - (w + x)))), (y + (z - w))) ||
 rewrite((x + (y + (z - (x + w)))), (y + (z - w))) ||
 rewrite((x + (y + (z - min(w, min(u, (v + x)))))), (y + (z - min(v, (min(u, w) - x))))) ||
 rewrite((x + (y + (z - min(w, min(u, (x + v)))))), (y + (z - min(v, (min(u, w) - x))))) ||
 rewrite((x + (y + (z - min(w, min((u + x), v))))), (y + (z - min(u, (min(v, w) - x))))) ||
 rewrite((x + (y + (z - min(w, min((x + u), v))))), (y + (z - min(u, (min(v, w) - x))))) ||
 rewrite((x + (y + (z - min(min(w, (u + x)), v)))), (y + (z - min((min(w, v) - x), u)))) ||
 rewrite((x + (y + (z - min(min(w, (x + u)), v)))), (y + (z - min((min(w, v) - x), u)))) ||
 rewrite((x + (y + (z - min(min((w + x), u), v)))), (y + (z - min((min(u, v) - x), w)))) ||
 rewrite((x + (y + (z - min(min((x + w), u), v)))), (y + (z - min((min(u, v) - x), w)))) ||
 rewrite((x + (y + max(z, (w + (u - x))))), (y + max((u + w), (z + x)))) ||
 rewrite((x + (y + max(z, ((w - x) + u)))), (y + max((z + x), (w + u)))) ||
 rewrite((x + (y + max((z + (w - x)), u))), (y + max((z + w), (x + u)))) ||
 rewrite((x + (y + max(((z - x) + w), u))), (y + max((u + x), (w + z)))) ||
 rewrite((x + ((y + (z - x)) + w)), (y + (z + w))) ||
 rewrite((x + ((y + min(z, (w + (u - x)))) + v)), ((y + min((u + w), (z + x))) + v)) ||
 rewrite((x + ((y + min(z, ((w - x) + u))) + v)), ((y + min((x + z), (w + u))) + v)) ||
 rewrite((x + ((y + min((z + (w - x)), u)) + v)), (y + (min((z + w), (x + u)) + v))) ||
 rewrite((x + ((y + min(((z - x) + w), u)) + v)), (v + (y + min((z + w), (x + u))))) ||
 rewrite((x + (((y - x) + z) + w)), ((y + z) + w)) ||
 rewrite((x + ((min(y, (z + (w - x))) + u) + v)), (u + (min((z + w), (x + y)) + v))) ||
 rewrite((x + ((min(y, ((z - x) + w)) + u) + v)), (u + (min((y + x), (w + z)) + v))) ||
 rewrite((x + ((min((y + (z - x)), w) + u) + v)), (u + (min((y + z), (x + w)) + v))) ||
 rewrite((x + ((min(((y - x) + z), w) + u) + v)), (u + (min((y + z), (x + w)) + v))) ||
 rewrite((x + ((y - (x + z)) + w)), ((y - z) + w)) ||
 rewrite((x + ((y - (z + x)) + w)), ((y - z) + w)) ||
 rewrite((x + ((y - min(z, min(w, (u + x)))) + v)), ((y - min(u, (min(w, z) - x))) + v)) ||
 rewrite((x + ((y - min(z, min(w, (x + u)))) + v)), ((y - min(u, (min(w, z) - x))) + v)) ||
 rewrite((x + ((y - min(z, min((w + x), u))) + v)), ((y - min(w, (min(u, z) - x))) + v)) ||
 rewrite((x + ((y - min(z, min((x + w), u))) + v)), ((y - min(w, (min(u, z) - x))) + v)) ||
 rewrite((x + ((y - min(min(z, (w + x)), u)) + v)), ((y - min((min(z, u) - x), w)) + v)) ||
 rewrite((x + ((y - min(min(z, (x + w)), u)) + v)), ((y - min((min(z, u) - x), w)) + v)) ||
 rewrite((x + ((y - min(min((x + z), w), u)) + v)), ((y - min((min(w, u) - x), z)) + v)) ||
 rewrite((x + ((y - min(min((z + x), w), u)) + v)), ((y - min((min(w, u) - x), z)) + v)) ||
 rewrite((x + (max(y, (z + (w - x))) + u)), (u + max((z + w), (y + x)))) ||
 rewrite((x + (max(y, ((z - x) + w)) + u)), (u + max((y + x), (w + z)))) ||
 rewrite((x + (max((y + (z - x)), w) + u)), (u + max((y + z), (x + w)))) ||
 rewrite((x + (max(((y - x) + z), w) + u)), (u + max((y + z), (x + w)))))) ||
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
 rewrite((x + (y - (z + min(w, (x + u))))), (y - (z + min(u, (w - x))))) ||
 rewrite((x + (y - (z + min(w, min(u, (v + x)))))), (y - (z + min(v, (min(u, w) - x))))) ||
 rewrite((x + (y - (z + min(w, min(u, (x + v)))))), (y - (z + min(v, (min(u, w) - x))))) ||
 rewrite((x + (y - (z + min(w, min((u + x), v))))), (y - (z + min(u, (min(v, w) - x))))) ||
 rewrite((x + (y - (z + min(w, min((x + u), v))))), (y - (z + min(u, (min(v, w) - x))))) ||
 rewrite((x + (y - (z + min((w + x), u)))), (y - (z + min((u - x), w)))) ||
 rewrite((x + (y - (z + min((x + w), u)))), (y - (z + min((u - x), w)))) ||
 rewrite((x + (y - (z + min(min(w, (u + x)), v)))), (y - (z + min((min(w, v) - x), u)))) ||
 rewrite((x + (y - (z + min(min(w, (x + u)), v)))), (y - (z + min((min(w, v) - x), u)))) ||
 rewrite((x + (y - (z + min(min((w + x), u), v)))), (y - (z + min((min(u, v) - x), w)))) ||
 rewrite((x + (y - (z + min(min((x + w), u), v)))), (y - (z + min((min(u, v) - x), w)))) ||
 rewrite((x + (y - (z + select(w, u, (v + y))))), (x - (z + select(w, (u - y), v)))) ||
 rewrite((x + (y - (z + select(w, u, (y + v))))), (x - (z + select(w, (u - y), v)))) ||
 rewrite((x + (y - ((x + z) + w))), (y - (z + w))) ||
 rewrite((x + (y - ((z + x) + w))), (y - (z + w))) ||
 rewrite((x + (y - ((z + (w + x)) + u))), (y - (z + (w + u)))) ||
 rewrite((x + (y - ((z + (x + w)) + u))), (y - (z + (w + u)))) ||
 rewrite((x + (y - (((x + z) + w) + u))), (y - ((z + w) + u))) ||
 rewrite((x + (y - (((z + x) + w) + u))), (y - ((z + w) + u))) ||
 rewrite((x + (y - (min(z, (w + x)) + u))), (y - (min(w, (z - x)) + u))) ||
 rewrite((x + (y - (min(z, (x + w)) + u))), (y - (min(w, (z - x)) + u))) ||
 rewrite((x + (y - (min(z, min(w, (u + x))) + v))), (y - (min(u, (min(w, z) - x)) + v))) ||
 rewrite((x + (y - (min(z, min(w, (x + u))) + v))), (y - (min(u, (min(w, z) - x)) + v))) ||
 rewrite((x + (y - (min(z, min((w + x), u)) + v))), (y - (min(w, (min(u, z) - x)) + v))) ||
 rewrite((x + (y - (min(z, min((x + w), u)) + v))), (y - (min(w, (min(u, z) - x)) + v))) ||
 rewrite((x + (y - (min((x + z), w) + u))), (y - (min((w - x), z) + u))) ||
 rewrite((x + (y - (min((z + x), w) + u))), (y - (min((w - x), z) + u))) ||
 rewrite((x + (y - (min(min(z, (w + x)), u) + v))), (y - (min((min(z, u) - x), w) + v))) ||
 rewrite((x + (y - (min(min(z, (x + w)), u) + v))), (y - (min((min(z, u) - x), w) + v))) ||
 rewrite((x + (y - (min(min((x + z), w), u) + v))), (y - (min((min(w, u) - x), z) + v))) ||
 rewrite((x + (y - (min(min((z + x), w), u) + v))), (y - (min((min(w, u) - x), z) + v))) ||
 rewrite((x + (y - (select(z, w, (u + y)) + v))), (x - (v + select(z, (w - y), u)))) ||
 rewrite((x + (y - (select(z, w, (y + u)) + v))), (x - (select(z, (w - y), u) + v))) ||
 rewrite((x + (y - (((x + y)/c0)*c0))), ((x + y) % c0)) ||
 rewrite((x + (y - (((y + x)/c0)*c0))), ((y + x) % c0)) ||
 rewrite((x + (y - min(z, (w + x)))), (y - min(w, (z - x)))) ||
 rewrite((x + (y - min(z, (x + w)))), (y - min(w, (z - x)))) ||
 rewrite((x + (y - min(z, min(w, (u + (v + y)))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min(w, (u + (y + v)))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min(w, ((u + y) + v))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min(w, ((y + u) + v))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min((w + (u + y)), v)))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(z, min((w + (y + u)), v)))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min(z, min(((w + y) + u), v)))), (x - min((min(v, z) - y), (u + w)))) ||
 rewrite((x + (y - min(z, min(((y + w) + u), v)))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min((x + z), w))), (y - min((w - x), z))) ||
 rewrite((x + (y - min((z + x), w))), (y - min((w - x), z))) ||
 rewrite((x + (y - min(min(z, (w + (u + y))), v))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min(min(z, (w + (y + u))), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, ((w + y) + u)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, ((y + w) + u)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min((z + (w + y)), u), v))), (x - min((min(u, v) - y), (z + w)))) ||
 rewrite((x + (y - min(min((z + (y + w)), u), v))), (x - min((min(u, v) - y), (w + z)))) ||
 rewrite((x + (y - min(min(((y + z) + w), u), v))), (x - min((min(u, v) - y), (z + w)))) ||
 rewrite((x + (y - min(min(((z + y) + w), u), v))), (x - min((min(u, v) - y), (z + w)))) ||
 rewrite((x + (y - select(z, w, (u + (v + y))))), (x - select(z, (w - y), (v + u)))) ||
 rewrite((x + (y - select(z, w, (u + (y + v))))), (x - select(z, (w - y), (u + v)))) ||
 rewrite((x + (y - select(z, w, ((u + y) + v)))), (x - select(z, (w - y), (u + v)))) ||
 rewrite((x + (y - select(z, w, ((y + u) + v)))), (x - select(z, (w - y), (u + v)))) ||
 rewrite((x + (y - select(z, x, c0))), (y + select(z, c0, x)), (c0 == 0)) ||
 rewrite((x + (y - select(z, (w + (u + x)), v))), (y - select(z, (w + u), (v - x)))) ||
 rewrite((x + (y - select(z, (w + (u + y)), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, (w + (x + u)), v))), (y - select(z, (w + u), (v - x)))) ||
 rewrite((x + (y - select(z, (w + (y + u)), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, ((w + x) + u), v))), (y - select(z, (w + u), (v - x)))) ||
 rewrite((x + (y - select(z, ((w + y) + u), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, ((x + w) + u), v))), (y - select(z, (u + w), (v - x)))) ||
 rewrite((x + (y - select(z, ((y + w) + u), v))), (x - select(z, (u + w), (v - y)))) ||
 rewrite((x + (y - select((c0 < z), x, c0))), (y + select((c0 < z), c0, x)), ((min(c0, 0) == (0 - max(c0, 0))) && (c0 == 0))) ||
 rewrite((x + (y - select(((z/c0) < w), x, c1))), (y + select(((z/c0) < w), c1, x)), (c1 == 0)) ||
 rewrite((x + ((y/c0) - ((y + z)/c0))), (x - (((y % c0) + z)/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((x + ((y/c0) - ((z + y)/c0))), (x - (((y % c0) + z)/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((x + (((y + (z + w))/c0) - (w/c0))), (x + ((y + (z + (w % c0)))/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((x + (((y + (z + w))/c0) - (z/c0))), (x + (((y + (z % c0)) + w)/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((x + ((((y + z) + w)/c0) - (y/c0))), (x + ((((y % c0) + w) + z)/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((x + ((((y + z) + w)/c0) - (z/c0))), (x + ((((z % c0) + w) + y)/c0)), prove_me((((1 % c0)/c0) == 0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((x + min((((y + c0)/c1)*c1), (y + c2))), (x + (y + c2)), prove_me(((((((((c2 + 12296) <= (((c0 + 12296)/c1)*c1)) && ((c2 + 6149) <= (((c0 + 6149)/c1)*c1))) && ((c2 + 1) <= (((c0 + 1)/c1)*c1))) && ((c2 + -1) <= (((c0 + -1)/c1)*c1))) && ((c2 + -2) <= (((c0 + -2)/c1)*c1))) && ((c2 + -3) <= (((c0 + -3)/c1)*c1))) && (c2 <= ((c0/c1)*c1))))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((x + max((y - max(z, max(w, (u + y)))), c0)), (x - min(max(u, (max(w, z) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(z, max(w, (y + u)))), c0)), (x - min(max(u, (max(w, z) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(z, max((w + y), u))), c0)), (x - min(max(w, (max(u, z) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(z, max((y + w), u))), c0)), (x - min(max((max(z, u) - y), w), fold((0 - c0))))) ||
 rewrite((x + max((y - max(max(z, (w + y)), u)), c0)), (x - min(max(w, (max(z, u) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(max(z, (y + w)), u)), c0)), (x - min(max(w, (max(z, u) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(max((y + z), w), u)), c0)), (x - min(max((max(w, u) - y), z), fold((0 - c0))))) ||
 rewrite((x + max((y - max(max((z + y), w), u)), c0)), (x - min(max((max(w, u) - y), z), fold((0 - c0))))))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + (y + (z - w))) + w), (x + (y + z))) ||
 rewrite(((x + (y + min(z, (w + (u - v))))) + v), (x + (y + min((u + w), (z + v))))) ||
 rewrite(((x + (y + min(z, ((w - u) + v)))) + u), (x + (y + min((u + z), (w + v))))) ||
 rewrite(((x + (y + min((z + (w - u)), v))) + u), (x + (y + min((z + w), (u + v))))) ||
 rewrite(((x + (y + min(((z - w) + u), v))) + w), (x + (y + min((v + w), (u + z))))) ||
 rewrite(((x + ((y - z) + w)) + z), (x + (y + w))) ||
 rewrite(((x + (min(y, (z + (w - u))) + v)) + u), (x + (min((z + w), (y + u)) + v))) ||
 rewrite(((x + (min(y, ((z - w) + u)) + v)) + w), (x + (min((y + w), (u + z)) + v))) ||
 rewrite(((x + (min((y + (z - w)), u) + v)) + w), (x + (min((y + z), (w + u)) + v))) ||
 rewrite(((x + (min(((y - z) + w), u) + v)) + z), (x + (min((y + w), (u + z)) + v))) ||
 rewrite(((x + (y - (z + w))) + w), (x + (y - z))) ||
 rewrite(((x + (y - (z + w))) + z), (x + (y - w))) ||
 rewrite(((x + (y - min(z, min(w, (u + v))))) + u), (x + (y - min(v, (min(w, z) - u))))) ||
 rewrite(((x + (y - min(z, min(w, (u + v))))) + v), (x + (y - min(u, (min(w, z) - v))))) ||
 rewrite(((x + (y - min(z, min((w + u), v)))) + u), (x + (y - min(w, (min(v, z) - u))))) ||
 rewrite(((x + (y - min(z, min((w + u), v)))) + w), (x + (y - min(u, (min(v, z) - w))))) ||
 rewrite(((x + (y - min(min(z, (w + u)), v))) + u), (x + (y - min((min(z, v) - u), w)))) ||
 rewrite(((x + (y - min(min(z, (w + u)), v))) + w), (x + (y - min((min(z, v) - w), u)))) ||
 rewrite(((x + (y - min(min((z + w), u), v))) + w), (x + (y - min((min(u, v) - w), z)))) ||
 rewrite(((x + (y - min(min((z + w), u), v))) + z), (x + (y - min((min(u, v) - z), w)))) ||
 rewrite(((x + max(y, (z + (w - u)))) + u), (x + max((z + w), (y + u)))) ||
 rewrite(((x + max(y, ((z - w) + u))) + w), (x + max((w + y), (z + u)))) ||
 rewrite(((x + max((y + (z - w)), u)) + w), (x + max((y + z), (w + u)))) ||
 rewrite(((x + max(((y - z) + w), u)) + z), (x + max((y + w), (u + z)))) ||
 rewrite((((x + (y - z)) + w) + z), (x + (y + w))) ||
 rewrite((((x + min(y, (z + (w - u)))) + v) + u), ((x + min((w + z), (y + u))) + v)) ||
 rewrite((((x + min(y, ((z - w) + u))) + v) + w), ((x + min((w + y), (z + u))) + v)) ||
 rewrite((((x + min((y + (z - w)), u)) + v) + w), ((x + min((y + z), (w + u))) + v)) ||
 rewrite((((x + min(((y - z) + w), u)) + v) + z), ((x + min((y + w), (u + z))) + v)) ||
 rewrite(((((x - y) + z) + w) + y), ((x + z) + w)) ||
 rewrite((((min(x, (y + (z - w))) + u) + v) + w), ((min((y + z), (w + x)) + v) + u)) ||
 rewrite((((min(x, ((y - z) + w)) + u) + v) + z), ((min((x + z), (y + w)) + u) + v)) ||
 rewrite((((min((x + (y - z)), w) + u) + v) + z), ((min((x + y), (z + w)) + v) + u)) ||
 rewrite((((min(((x - y) + z), w) + u) + v) + y), ((min((x + z), (y + w)) + v) + u)) ||
 rewrite((((x - (y + z)) + w) + y), ((x - z) + w)) ||
 rewrite((((x - (y + z)) + w) + z), ((x - y) + w)) ||
 rewrite((((x - min(y, min(z, (w + u)))) + v) + u), ((x - min(w, (min(z, y) - u))) + v)) ||
 rewrite((((x - min(y, min(z, (w + u)))) + v) + w), ((x - min(u, (min(z, y) - w))) + v)) ||
 rewrite((((x - min(y, min((z + w), u))) + v) + w), ((x - min(z, (min(u, y) - w))) + v)) ||
 rewrite((((x - min(y, min((z + w), u))) + v) + z), ((x - min(w, (min(u, y) - z))) + v)) ||
 rewrite((((x - min(min(y, (z + w)), u)) + v) + w), ((x - min((min(y, u) - w), z)) + v)) ||
 rewrite((((x - min(min(y, (z + w)), u)) + v) + z), ((x - min((min(y, u) - z), w)) + v)) ||
 rewrite((((x - min(min((y + z), w), u)) + v) + y), ((x - min((min(w, u) - y), z)) + v)) ||
 rewrite((((x - min(min((y + z), w), u)) + v) + z), ((x - min((min(w, u) - z), y)) + v)) ||
 rewrite(((max(x, (y + (z - w))) + u) + w), (max((y + z), (x + w)) + u)) ||
 rewrite(((max(x, ((y - z) + w)) + u) + z), (max((z + x), (y + w)) + u)) ||
 rewrite(((max((x + (y - z)), w) + u) + z), (max((x + y), (w + z)) + u)) ||
 rewrite(((max(((x - y) + z), w) + u) + y), (max((x + z), (w + y)) + u)))) ||
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
 rewrite(((x - (y + min(z, (w + u)))) + w), (x - (y + min(u, (z - w))))) ||
 rewrite(((x - (y + min(z, min(w, (u + v))))) + u), (x - (y + min(v, (min(w, z) - u))))) ||
 rewrite(((x - (y + min(z, min(w, (u + v))))) + v), (x - (y + min(u, (min(w, z) - v))))) ||
 rewrite(((x - (y + min(z, min((w + u), v)))) + u), (x - (y + min(w, (min(v, z) - u))))) ||
 rewrite(((x - (y + min(z, min((w + u), v)))) + w), (x - (y + min(u, (min(v, z) - w))))) ||
 rewrite(((x - (y + min((z + w), u))) + w), (x - (y + min((u - w), z)))) ||
 rewrite(((x - (y + min((z + w), u))) + z), (x - (y + min((u - z), w)))) ||
 rewrite(((x - (y + min(min(z, (w + u)), v))) + u), (x - (y + min((min(z, v) - u), w)))) ||
 rewrite(((x - (y + min(min(z, (w + u)), v))) + w), (x - (y + min((min(z, v) - w), u)))) ||
 rewrite(((x - (y + min(min((z + w), u), v))) + w), (x - (y + min((min(u, v) - w), z)))) ||
 rewrite(((x - (y + min(min((z + w), u), v))) + z), (x - (y + min((min(u, v) - z), w)))) ||
 rewrite(((x - (y + select(z, w, (u + x)))) + v), (v - (y + select(z, (w - x), u)))) ||
 rewrite(((x - (y + select(z, w, (x + u)))) + v), (v - (y + select(z, (w - x), u)))) ||
 rewrite(((x - ((y + z) + w)) + y), (x - (z + w))) ||
 rewrite(((x - ((y + z) + w)) + z), (x - (y + w))) ||
 rewrite(((x - ((y + (z + w)) + u)) + w), (x - (y + (z + u)))) ||
 rewrite(((x - ((y + (z + w)) + u)) + z), (x - (y + (w + u)))) ||
 rewrite(((x - (((y + z) + w) + u)) + y), (x - ((z + w) + u))) ||
 rewrite(((x - (((y + z) + w) + u)) + z), (x - ((y + w) + u))) ||
 rewrite(((x - (min(y, (z + w)) + u)) + w), (x - (min(z, (y - w)) + u))) ||
 rewrite(((x - (min(y, (z + w)) + u)) + z), (x - (min(w, (y - z)) + u))) ||
 rewrite(((x - (min(y, min(z, (w + u))) + v)) + u), (x - (min(w, (min(z, y) - u)) + v))) ||
 rewrite(((x - (min(y, min(z, (w + u))) + v)) + w), (x - (min(u, (min(z, y) - w)) + v))) ||
 rewrite(((x - (min(y, min((z + w), u)) + v)) + w), (x - (min(z, (min(y, u) - w)) + v))) ||
 rewrite(((x - (min(y, min((z + w), u)) + v)) + z), (x - (min(w, (min(y, u) - z)) + v))) ||
 rewrite(((x - (min((y + z), w) + u)) + y), (x - (min((w - y), z) + u))) ||
 rewrite(((x - (min((y + z), w) + u)) + z), (x - (min((w - z), y) + u))) ||
 rewrite(((x - (min(min(y, (z + w)), u) + v)) + w), (x - (min((min(y, u) - w), z) + v))) ||
 rewrite(((x - (min(min(y, (z + w)), u) + v)) + z), (x - (min((min(y, u) - z), w) + v))) ||
 rewrite(((x - (min(min((y + z), w), u) + v)) + y), (x - (min((min(w, u) - y), z) + v))) ||
 rewrite(((x - (min(min((y + z), w), u) + v)) + z), (x - (min((min(w, u) - z), y) + v))) ||
 rewrite(((x - (select(y, z, (w + x)) + u)) + v), ((v - select(y, (z - x), w)) - u)) ||
 rewrite(((x - (select(y, z, (x + w)) + u)) + v), (v - (select(y, (z - x), w) + u))) ||
 rewrite(((x - (((x + y)/c0)*c0)) + y), ((x + y) % c0)) ||
 rewrite(((x - (((y + x)/c0)*c0)) + y), ((y + x) % c0)) ||
 rewrite(((x - min(y, (z + w))) + w), (x - min(z, (y - w)))) ||
 rewrite(((x - min(y, (z + w))) + z), (x - min(w, (y - z)))) ||
 rewrite(((x - min(y, min(z, (w + (u + x))))) + v), (v - min((min(z, y) - x), (w + u)))) ||
 rewrite(((x - min(y, min(z, (w + (x + u))))) + v), (v - min((min(z, y) - x), (u + w)))) ||
 rewrite(((x - min(y, min(z, ((w + x) + u)))) + v), (v - min((min(z, y) - x), (u + w)))) ||
 rewrite(((x - min(y, min(z, ((x + w) + u)))) + v), (v - min((min(z, y) - x), (w + u)))) ||
 rewrite(((x - min(y, min((z + (w + x)), u))) + v), (v - min((min(u, y) - x), (z + w)))) ||
 rewrite(((x - min(y, min((z + (x + w)), u))) + v), (v - min((min(u, y) - x), (w + z)))) ||
 rewrite(((x - min(y, min(((x + z) + w), u))) + v), (v - min((min(u, y) - x), (z + w)))) ||
 rewrite(((x - min(y, min(((z + x) + w), u))) + v), (v - min((min(u, y) - x), (z + w)))) ||
 rewrite(((x - min((y + z), w)) + y), (x - min((w - y), z))) ||
 rewrite(((x - min((y + z), w)) + z), (x - min((w - z), y))) ||
 rewrite(((x - min(min(y, (z + (w + x))), u)) + v), (v - min((min(y, u) - x), (z + w)))) ||
 rewrite(((x - min(min(y, (z + (x + w))), u)) + v), (v - min((min(y, u) - x), (w + z)))) ||
 rewrite(((x - min(min(y, ((x + z) + w)), u)) + v), (v - min((min(y, u) - x), (z + w)))) ||
 rewrite(((x - min(min(y, ((z + x) + w)), u)) + v), (v - min((min(y, u) - x), (w + z)))) ||
 rewrite(((x - min(min((y + (x + z)), w), u)) + v), (v - min((min(w, u) - x), (z + y)))) ||
 rewrite(((x - min(min((y + (z + x)), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - min(min(((x + y) + z), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - min(min(((y + x) + z), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - select(y, z, c0)) + z), (x + select(y, c0, z)), (c0 == 0)) ||
 rewrite(((x - select(y, z, (w + (u + x)))) + v), (v - select(y, (z - x), (u + w)))) ||
 rewrite(((x - select(y, z, (w + (x + u)))) + v), (v - select(y, (z - x), (w + u)))) ||
 rewrite(((x - select(y, z, ((w + x) + u))) + v), (v - select(y, (z - x), (w + u)))) ||
 rewrite(((x - select(y, z, ((x + w) + u))) + v), (v - select(y, (z - x), (w + u)))) ||
 rewrite(((x - select(y, (z + (w + u)), v)) + u), (x - select(y, (z + w), (v - u)))) ||
 rewrite(((x - select(y, (z + (w + u)), v)) + w), (x - select(y, (z + u), (v - w)))) ||
 rewrite(((x - select(y, (z + (w + x)), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, (z + (x + w)), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, ((x + z) + w), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, ((z + w) + u), v)) + w), (x - select(y, (z + u), (v - w)))) ||
 rewrite(((x - select(y, ((z + w) + u), v)) + z), (x - select(y, (w + u), (v - z)))) ||
 rewrite(((x - select(y, ((z + x) + w), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select((c0 < y), z, c0)) + z), (x + select((c0 < y), c0, z)), ((min(c0, 0) == (0 - max(c0, 0))) && (c0 == 0))) ||
 rewrite(((x - select(((y/c0) < z), w, c1)) + w), (x + select(((y/c0) < z), c1, w)), (c1 == 0)) ||
 rewrite((((x/c0) - ((x + y)/c0)) + z), (z - ((y + (x % c0))/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x/c0) - ((y + x)/c0)) + z), (z - ((y + (x % c0))/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite(((((x + (y + z))/c0) - (y/c0)) + w), ((((x + (y % c0)) + z)/c0) + w), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite(((((x + (y + z))/c0) - (z/c0)) + w), (((x + (y + (z % c0)))/c0) + w), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((((x + y) + z)/c0) - (x/c0)) + w), (((((x % c0) + z) + y)/c0) + w), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((((x + y) + z)/c0) - (y/c0)) + w), (((((y % c0) + z) + x)/c0) + w), prove_me((((1 % c0)/c0) == 0))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x - y) + (y + z)), (x + z)) ||
 rewrite(((x - y) + (z + y)), (x + z)) ||
 rewrite(((x - y) + (z + (w + y))), (x + (z + w))) ||
 rewrite(((x - y) + (z + (y + w))), (x + (z + w))) ||
 rewrite(((x - y) + (z + (w - x))), (z + (w - y))) ||
 rewrite(((x - y) + ((y + z) + w)), (x + (z + w))) ||
 rewrite(((x - y) + ((z + y) + w)), (x + (z + w))) ||
 rewrite(((x - y) + ((z - x) + w)), ((w - y) + z)) ||
 rewrite(((x - (y + z)) + (w + y)), ((x - z) + w)) ||
 rewrite(((x - (y + z)) + (w + z)), ((x - y) + w)) ||
 rewrite(((x - (y + z)) + (y + w)), ((x - z) + w)) ||
 rewrite(((x - (y + z)) + (z + w)), ((x - y) + w)) ||
 rewrite(((x - (y + (z + w))) + (u + (v + w))), ((x - (y + z)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (v + z))), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (w + v))), ((x - (y + z)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (z + v))), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (w + z)), ((x - (y + w)) + w)) ||
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
 rewrite(((x - ((y + z) + w)) + (u + (v + y))), ((x - (z + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + (u + (v + z))), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + (u + (y + v))), ((x - (z + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + (u + (z + v))), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + (z + y)), ((x - (z + w)) + z)) ||
 rewrite(((x - ((y + z) + w)) + ((u + y) + v)), ((x - (z + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + ((u + z) + v)), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + ((y + u) + v)), ((x - (z + w)) + (u + v))) ||
 rewrite(((x - ((y + z) + w)) + ((z + u) + v)), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - ((y + (z + w)) + u)) + (v + w)), ((x - ((y + z) + u)) + v)) ||
 rewrite(((x - ((y + (z + w)) + u)) + (v + z)), ((x - ((y + w) + u)) + v)) ||
 rewrite(((x - ((y + (z + w)) + u)) + (w + v)), ((x - ((y + z) + u)) + v)) ||
 rewrite(((x - ((y + (z + w)) + u)) + (z + v)), ((x - ((y + w) + u)) + v)) ||
 rewrite(((x - (((y + z) + w) + u)) + (v + y)), ((x - ((z + w) + u)) + v)) ||
 rewrite(((x - (((y + z) + w) + u)) + (v + z)), ((x - ((y + w) + u)) + v)) ||
 rewrite(((x - (((y + z) + w) + u)) + (y + v)), ((x - ((z + w) + u)) + v)) ||
 rewrite(((x - (((y + z) + w) + u)) + (z + v)), ((x - ((y + w) + u)) + v)))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*c0) + (y + (z + (w + (x*c1))))), (y + (z + w)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + (z + ((x*c1) + w)))), (y + (z + w)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + (z + (x*c1)))), (y + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((z + (x*c1)) + w))), (y + (z + w)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + (((x*c1) + z) + w))), (y + (z + w)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((x*c1) + z))), (y + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((z/c1) + (x*c2)))), (y + (z/c1)), ((c0 + c2) == 0)) ||
 rewrite(((x*c0) + (y + ((z/c1) + (x*c2)))), (y + (z/c1)), ((((1 <= max(max(c0, c1), c2)) || (c0 <= -1)) || (c1 <= -1)) && ((c0 + c2) == 0))) ||
 rewrite(((x*c0) + (y + (x*c1))), y, ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((x + z)*c1))), (y + (z*c1)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((z + x)*c1))), (y + (z*c1)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((y + (z + (x*c1))) + w)), (y + (z + w)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((y + ((x*c1) + z)) + w)), (y + (z + w)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((y + (x*c1)) + z)), (y + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((y + (x*c1)) + z) + w)), (y + (z + w)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((((x*c1) + y) + z) + w)), ((y + z) + w), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((x*c1) + y) + z)), (y + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((y/c1) + (x*c2)) + z)), ((y/c1) + z), ((c0 + c2) == 0)) ||
 rewrite(((x*c0) + (((y/c1) + (x*c2)) + z)), ((y/c1) + z), ((((1 <= max(max(c0, c1), c2)) || (c0 <= -1)) || (c1 <= -1)) && ((c0 + c2) == 0))) ||
 rewrite(((x*c0) + ((x*c1) + y)), y, ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((y*c1) + (z + (x*c2)))), ((y*c1) + z), ((c0 + c2) == 0)) ||
 rewrite(((x*c0) + ((y*c1) + (z + (x*c2)))), ((y*c1) + z), ((((1 <= max(max(c0, c1), c2)) || (c0 <= -1)) || (c1 <= -1)) && ((c0 + c2) == 0))) ||
 rewrite(((x*c0) + ((y*c1) + ((x*c2) + z))), ((y*c1) + z), ((c0 + c2) == 0)) ||
 rewrite(((x*c0) + ((y*c1) + ((x*c2) + z))), ((y*c1) + z), ((((1 <= max(max(c0, c1), c2)) || (c0 <= -1)) || (c1 <= -1)) && ((c0 + c2) == 0))) ||
 rewrite(((x*c0) + (((x + y)*c1) + z)), ((y*c1) + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((y + x)*c1) + z)), ((y*c1) + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((y/c1) + (x*c2))), (y/c1), ((c0 + c2) == 0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x*2) + (y - x)), (x + y)) ||
 rewrite(((x*c0) + (y - x)), (x + y), (c0 == 2)) ||
 rewrite(((x*c0) + (((c1 - (x*c2))*c3) - y)), (c1 - y), (((c3 == 1) || (c1 == 0)) && ((((c1 - c2)*c3) + c0) == c1))) ||
 rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((((0 - c1)*c2) + c0) == 0)) ||
 rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((c1*c2) == c0)))))) ||
((a.node_type() == IRNodeType::Div) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Sub) && (
 rewrite((((x + y)/c0) + (z - (w + (x/c0)))), ((((x % c0) + y)/c0) + (z - w)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x + y)/c0) + (z - (w + (y/c0)))), (((x + (y % c0))/c0) - (w - z)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x + y)/c0) + (z - ((x/c0) + w))), (z + (((y + (x % c0))/c0) - w)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x + y)/c0) + (z - ((y/c0) + w))), (z + (((x + (y % c0))/c0) - w)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x + y)/c0) + (z - (x/c0))), (z + ((y + (x % c0))/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x + y)/c0) + (z - (y/c0))), (z + ((x + (y % c0))/c0)), prove_me((((1 % c0)/c0) == 0))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite(((x/c0) + ((c1 - x)/c0)), (c0 - (x % c0)), prove_me((((c1/c0) == c0) && ((((1 % c0) - c0) + ((1/c0) + ((c1 + -1)/c0))) == 0)))))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite((min((((x + c0)/c1)*c1), (x + c2)) + c3), (x + fold((min(((c3 + c0) - c1), ((c1 + c0) + c3)) + 1))), ((((c1 <= 1) || (c0 < c2)) || (((c1 + c2) + -1) <= c0)) && ((((c0 <= c2) || (c1 <= -1)) || (c0 < (c1 + c2))) && ((((1 <= c1) || (c0 <= c2)) || ((c0 + c1) < c2)) && ((((-1 <= c1) || ((c2 + -1) <= (c0 + c1))) || (c0 < c2)) && (((-1 <= c1) || ((c2 + -2) <= (c0 + c1))) && ((c1 <= 1) || (((c1 + c2) + -2) <= c0)))))))) ||
 rewrite((min((((x + c0)/c1)*c1), (x + c2)) + y), ((x + c2) + y), prove_me(((((((((c2 + 12296) <= (((c0 + 12296)/c1)*c1)) && ((c2 + 6149) <= (((c0 + 6149)/c1)*c1))) && ((c2 + 1) <= (((c0 + 1)/c1)*c1))) && ((c2 + -1) <= (((c0 + -1)/c1)*c1))) && ((c2 + -2) <= (((c0 + -2)/c1)*c1))) && ((c2 + -3) <= (((c0 + -3)/c1)*c1))) && (c2 <= ((c0/c1)*c1))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite((max((x - max(y, max(z, (w + x)))), c0) + u), (u - min(max(w, (max(z, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max(z, (x + w)))), c0) + u), (u - min(max(w, (max(z, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max((x + z), w))), c0) + u), (u - min(max((max(y, w) - x), z), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max((z + x), w))), c0) + u), (u - min(max(z, (max(w, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max(y, (x + z)), w)), c0) + u), (u - min(max(z, (max(y, w) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max(y, (z + x)), w)), c0) + u), (u - min(max(z, (max(y, w) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max((x + y), z), w)), c0) + u), (u - min(max((max(z, w) - x), y), fold((0 - c0))))) ||
 rewrite((max((x - max(max((y + x), z), w)), c0) + u), (u - min(max((max(z, w) - x), y), fold((0 - c0)))))))
