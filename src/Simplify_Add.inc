((b.node_type() == IRNodeType::Add) && (
 rewrite((x + (y + (z + (w - x)))), (y + (z + w))) ||
 rewrite((x + (y + (z + min(w, (u + (v - x)))))), (y + (z + min((v + u), (w + x))))) ||
 rewrite((x + (y + (z + min(w, ((u - x) + v))))), (y + (z + min((w + x), (u + v))))) ||
 rewrite((x + (y + (z + min((w + (u - x)), v)))), (y + (z + min((w + u), (x + v))))) ||
 rewrite((x + (y + (z + min(((w - x) + u), v)))), (y + (z + min((v + x), (u + w))))) ||
 rewrite((x + (y + ((z - x) + w))), (y + (z + w))) ||
 rewrite((x + (y + (min(z, (w + (u - x))) + v))), (v + (y + min((w + u), (x + z))))) ||
 rewrite((x + (y + (min(z, ((w - x) + u)) + v))), (v + (y + min((z + x), (u + w))))) ||
 rewrite((x + (y + (min((z + (w - x)), u) + v))), (v + (y + min((z + w), (u + x))))) ||
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
 rewrite((x + (y + ((x*c0) - z))), (y - z), (c0 == -1)) ||
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
 rewrite((x + ((min((y + (z - x)), w) + u) + v)), (u + (min((y + z), (w + x)) + v))) ||
 rewrite((x + ((min(((y - x) + z), w) + u) + v)), (u + (min((y + z), (w + x)) + v))) ||
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
 rewrite((x + (((x*c0) - y) + z)), (z - y), (c0 == -1)) ||
 rewrite((x + ((y/c0) + ((x*c1) - z))), ((y/c0) - z), (c1 == -1)) ||
 rewrite((x + (max(y, (z + (w - x))) + u)), (u + max((z + w), (y + x)))) ||
 rewrite((x + (max(y, ((z - x) + w)) + u)), (u + max((y + x), (z + w)))) ||
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
 rewrite((x + (y - min(z, (w + (x - u))))), (y - min((z - x), (w - u)))) ||
 rewrite((x + (y - min(z, (w + (y - u))))), (x - min((z - y), (w - u)))) ||
 rewrite((x + (y - min(z, (x + w)))), (y - min(w, (z - x)))) ||
 rewrite((x + (y - min(z, ((x - w) + u)))), (y - min((z - x), (u - w)))) ||
 rewrite((x + (y - min(z, ((y - w) + u)))), (x - min((z - y), (u - w)))) ||
 rewrite((x + (y - min(z, (x - w)))), (y + max(w, (x - z)))) ||
 rewrite((x + (y - min(z, min(w, (u + (v + y)))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min(w, (u + (y + v)))))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite((x + (y - min(z, min(w, ((u + y) + v))))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite((x + (y - min(z, min(w, ((y + u) + v))))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite((x + (y - min(z, min((w + (u + y)), v)))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(z, min((w + (y + u)), v)))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min(z, min(((w + y) + u), v)))), (x - min((min(v, z) - y), (u + w)))) ||
 rewrite((x + (y - min(z, min(((y + w) + u), v)))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min((x + z), w))), (y - min((w - x), z))) ||
 rewrite((x + (y - min((z + x), w))), (y - min((w - x), z))) ||
 rewrite((x + (y - min((z + (x - w)), u))), (y - min((u - x), (z - w)))) ||
 rewrite((x + (y - min((z + (y - w)), u))), (x - min((z - w), (u - y)))) ||
 rewrite((x + (y - min(((x - z) + w), u))), (y - min((u - x), (w - z)))) ||
 rewrite((x + (y - min(((y - z) + w), u))), (x - min((w - z), (u - y)))) ||
 rewrite((x + (y - min((x - z), w))), (y + max((x - w), z))) ||
 rewrite((x + (y - min(min(z, (w + (u + y))), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, (w + (y + u))), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, ((w + y) + u)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, ((y + w) + u)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min((z + (w + y)), u), v))), (x - min((min(u, v) - y), (w + z)))) ||
 rewrite((x + (y - min(min((z + (y + w)), u), v))), (x - min((min(u, v) - y), (w + z)))) ||
 rewrite((x + (y - min(min(((y + z) + w), u), v))), (x - min((min(u, v) - y), (w + z)))) ||
 rewrite((x + (y - min(min(((z + y) + w), u), v))), (x - min((min(u, v) - y), (z + w)))) ||
 rewrite((x + (y - max(z, (w + (y - x))))), (x - max((z - y), (w - x)))) ||
 rewrite((x + (y - max(z, ((y - x) + w)))), (x - max((w - x), (z - y)))) ||
 rewrite((x + (y - max((z + (y - x)), w))), (x - max((z - x), (w - y)))) ||
 rewrite((x + (y - max(((y - x) + z), w))), (x - max((z - x), (w - y)))) ||
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
 rewrite((x + ((y + (z/c0)) - ((w + z)/c0))), (x + (y - (((z % c0) + w)/c0))), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + ((y + (z/c0)) - ((z + w)/c0))), (x + (y - (((z % c0) + w)/c0))), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + (((y/c0) + z) - ((w + y)/c0))), (x + (z - (((y % c0) + w)/c0))), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + (((y/c0) + z) - ((y + w)/c0))), (x + (z - (((y % c0) + w)/c0))), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + (((y/c0) + ((y + c1)/c0)) - z)), (x + (y - z)), ((((c1/c0) == 0) && (((c1 + 1)/c0) == 1)) && (((-3/c0) + ((c1 + -3)/c0)) == -3))) ||
 rewrite((x + ((y % c0) - select(z, x, c1))), (select(z, c1, x) + (y % c0)), (c1 == 0)) ||
 rewrite((x + ((y % c0) - select((c1 < z), x, c1))), (select((c1 < z), c1, x) + (y % c0)), ((min(c1, 0) == (0 - max(c1, 0))) && (c1 == 0))) ||
 rewrite((x + ((y/c0) - ((y + z)/c0))), (x - (((y % c0) + z)/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + ((y/c0) - ((z + y)/c0))), (x - (((y % c0) + z)/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + (((y + (z + w))/c0) - (w/c0))), (x + ((y + (z + (w % c0)))/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + (((y + (z + w))/c0) - (z/c0))), (x + (((y + (z % c0)) + w)/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + ((((y + z) + w)/c0) - (y/c0))), (x + ((((y % c0) + w) + z)/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite((x + ((((y + z) + w)/c0) - (z/c0))), (x + ((((z % c0) + w) + y)/c0)), prove_me(IRMatcher::Const(1))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((x + min(min(y, min(z, w)), (min(u, w) + c0))), (x + min(min(y, z), (min(u, w) + c0))), (c0 <= 0)) ||
 rewrite((x + min(min(y, min(z, w)), (min(u, z) + c0))), (x + min(min(y, w), (min(u, z) + c0))), (c0 <= 0)) ||
 rewrite((x + min(min(y, min(z, w)), (min(w, u) + c0))), (x + min(min(y, z), (min(w, u) + c0))), (c0 <= 0)) ||
 rewrite((x + min(min(y, min(z, w)), (min(z, u) + c0))), (x + min(min(y, w), (min(z, u) + c0))), (c0 <= 0)) ||
 rewrite((x + min(min(min(y, z), w), (min(u, y) + c0))), (x + min(min(z, w), (min(u, y) + c0))), (c0 <= 0)) ||
 rewrite((x + min(min(min(y, z), w), (min(u, z) + c0))), (x + min(min(y, w), (min(u, z) + c0))), (c0 <= 0)) ||
 rewrite((x + min(min(min(y, z), w), (min(y, u) + c0))), (x + min(min(z, w), (min(y, u) + c0))), (c0 <= 0)) ||
 rewrite((x + min(min(min(y, z), w), (min(z, u) + c0))), (x + min(min(y, w), (min(z, u) + c0))), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((x + max(y, (z - min(max(w, u), min(u, v))))), (x + max(y, (z - min(u, v))))) ||
 rewrite((x + max(y, (z - min(max(w, u), min(v, u))))), (x + max(y, (z - min(u, v))))) ||
 rewrite((x + max(y, (z - min(max(w, u), min(v, w))))), (x + max(y, (z - min(w, v))))) ||
 rewrite((x + max(y, (z - min(max(w, u), min(w, v))))), (x + max(y, (z - min(w, v))))) ||
 rewrite((x + max((y - min(max(z, w), min(u, w))), v)), (x + max((y - min(w, u)), v))) ||
 rewrite((x + max((y - min(max(z, w), min(u, z))), v)), (x + max((y - min(z, u)), v))) ||
 rewrite((x + max((y - min(max(z, w), min(w, u))), v)), (x + max((y - min(w, u)), v))) ||
 rewrite((x + max((y - min(max(z, w), min(z, u))), v)), (x + max((y - min(z, u)), v))) ||
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
 rewrite(((x + (min((y + (z - w)), u) + v)) + w), (x + (min((y + z), (u + w)) + v))) ||
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
 rewrite(((x + ((y*c0) - z)) + y), (x - z), (c0 == -1)) ||
 rewrite(((x + max(y, (z + (w - u)))) + u), (x + max((z + w), (y + u)))) ||
 rewrite(((x + max(y, ((z - w) + u))) + w), (x + max((w + y), (z + u)))) ||
 rewrite(((x + max((y + (z - w)), u)) + w), (x + max((y + z), (u + w)))) ||
 rewrite(((x + max(((y - z) + w), u)) + z), (x + max((y + w), (u + z)))) ||
 rewrite((((x + (y - z)) + w) + z), (x + (y + w))) ||
 rewrite((((x + min(y, (z + (w - u)))) + v) + u), ((x + min((w + z), (y + u))) + v)) ||
 rewrite((((x + min(y, ((z - w) + u))) + v) + w), ((x + min((w + y), (z + u))) + v)) ||
 rewrite((((x + min((y + (z - w)), u)) + v) + w), ((x + min((y + z), (w + u))) + v)) ||
 rewrite((((x + min(((y - z) + w), u)) + v) + z), ((x + min((y + w), (u + z))) + v)) ||
 rewrite(((((x - y) + z) + w) + y), ((x + z) + w)) ||
 rewrite((((min(x, (y + (z - w))) + u) + v) + w), ((min((y + z), (x + w)) + v) + u)) ||
 rewrite((((min(x, ((y - z) + w)) + u) + v) + z), ((min((x + z), (y + w)) + u) + v)) ||
 rewrite((((min((x + (y - z)), w) + u) + v) + z), ((min((x + y), (z + w)) + v) + u)) ||
 rewrite((((min(((x - y) + z), w) + u) + v) + y), ((min((x + z), (w + y)) + v) + u)) ||
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
 rewrite(((((x*c0) - y) + z) + x), (z - y), (c0 == -1)) ||
 rewrite((((x/c0) + ((y*c1) - z)) + y), ((x/c0) - z), (c1 == -1)) ||
 rewrite(((max(x, (y + (z - w))) + u) + w), (max((y + z), (w + x)) + u)) ||
 rewrite(((max(x, ((y - z) + w)) + u) + z), (max((z + x), (y + w)) + u)) ||
 rewrite(((max((x + (y - z)), w) + u) + z), (max((x + y), (w + z)) + u)) ||
 rewrite(((max(((x - y) + z), w) + u) + y), (max((x + z), (y + w)) + u)))) ||
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
 rewrite(((x - min(y, (z + (w - u)))) + w), (x - min((y - w), (z - u)))) ||
 rewrite(((x - min(y, (z + (x - w)))) + u), (u - min((y - x), (z - w)))) ||
 rewrite(((x - min(y, ((x - z) + w))) + u), (u - min((w - z), (y - x)))) ||
 rewrite(((x - min(y, ((z - w) + u))) + z), (x - min((u - w), (y - z)))) ||
 rewrite(((x - min(y, (z - w))) + z), (x + max(w, (z - y)))) ||
 rewrite(((x - min(y, min(z, (w + (u + x))))) + v), (v - min((min(z, y) - x), (w + u)))) ||
 rewrite(((x - min(y, min(z, (w + (x + u))))) + v), (v - min((min(z, y) - x), (w + u)))) ||
 rewrite(((x - min(y, min(z, ((w + x) + u)))) + v), (v - min((min(z, y) - x), (w + u)))) ||
 rewrite(((x - min(y, min(z, ((x + w) + u)))) + v), (v - min((min(z, y) - x), (u + w)))) ||
 rewrite(((x - min(y, min((z + (w + x)), u))) + v), (v - min((min(u, y) - x), (w + z)))) ||
 rewrite(((x - min(y, min((z + (x + w)), u))) + v), (v - min((min(u, y) - x), (w + z)))) ||
 rewrite(((x - min(y, min(((x + z) + w), u))) + v), (v - min((min(u, y) - x), (z + w)))) ||
 rewrite(((x - min(y, min(((z + x) + w), u))) + v), (v - min((min(u, y) - x), (w + z)))) ||
 rewrite(((x - min((y + z), w)) + y), (x - min((w - y), z))) ||
 rewrite(((x - min((y + z), w)) + z), (x - min((w - z), y))) ||
 rewrite(((x - min((y + (x - z)), w)) + u), (u - min((y - z), (w - x)))) ||
 rewrite(((x - min((y + (z - w)), u)) + z), (x - min((y - w), (u - z)))) ||
 rewrite(((x - min(((x - y) + z), w)) + u), (u - min((z - y), (w - x)))) ||
 rewrite(((x - min(((y - z) + w), u)) + y), (x - min((w - z), (u - y)))) ||
 rewrite(((x - min((y - z), w)) + y), (x + max((y - w), z))) ||
 rewrite(((x - min(min(y, (z + (w + x))), u)) + v), (v - min((min(y, u) - x), (w + z)))) ||
 rewrite(((x - min(min(y, (z + (x + w))), u)) + v), (v - min((min(y, u) - x), (z + w)))) ||
 rewrite(((x - min(min(y, ((x + z) + w)), u)) + v), (v - min((min(y, u) - x), (z + w)))) ||
 rewrite(((x - min(min(y, ((z + x) + w)), u)) + v), (v - min((min(y, u) - x), (w + z)))) ||
 rewrite(((x - min(min((y + (x + z)), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - min(min((y + (z + x)), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - min(min(((x + y) + z), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - min(min(((y + x) + z), w), u)) + v), (v - min((min(w, u) - x), (z + y)))) ||
 rewrite(((x - max(y, (z + (x - w)))) + w), (w - max((y - x), (z - w)))) ||
 rewrite(((x - max(y, ((x - z) + w))) + z), (z - max((w - z), (y - x)))) ||
 rewrite(((x - max((y + (x - z)), w)) + z), (z - max((y - z), (w - x)))) ||
 rewrite(((x - max(((x - y) + z), w)) + y), (y - max((z - y), (w - x)))) ||
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
 rewrite((((x + (y/c0)) - ((y + z)/c0)) + w), ((x - (((y % c0) + z)/c0)) + w), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x + (y/c0)) - ((z + y)/c0)) + w), ((x - (((y % c0) + z)/c0)) + w), prove_me(IRMatcher::Const(1))) ||
 rewrite(((((x/c0) + y) - ((x + z)/c0)) + w), ((y - (((x % c0) + z)/c0)) + w), prove_me(IRMatcher::Const(1))) ||
 rewrite(((((x/c0) + y) - ((z + x)/c0)) + w), ((w + y) - ((z + (x % c0))/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite(((((x/c0) + ((x + c1)/c0)) - y) + z), ((x - y) + z), ((((c1/c0) == 0) && (((c1 + 1)/c0) == 1)) && (((-3/c0) + ((c1 + -3)/c0)) == -3))) ||
 rewrite((((x % c0) - select(y, z, c1)) + z), (select(y, c1, z) + (x % c0)), (c1 == 0)) ||
 rewrite((((x % c0) - select((c1 < y), z, c1)) + z), (select((c1 < y), c1, z) + (x % c0)), ((min(c1, 0) == (0 - max(c1, 0))) && (c1 == 0))) ||
 rewrite((((x/c0) - ((x + y)/c0)) + z), (z - ((y + (x % c0))/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x/c0) - ((y + x)/c0)) + z), (z - ((y + (x % c0))/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite(((((x + (y + z))/c0) - (y/c0)) + w), ((((x + (y % c0)) + z)/c0) + w), prove_me(IRMatcher::Const(1))) ||
 rewrite(((((x + (y + z))/c0) - (z/c0)) + w), (((x + (y + (z % c0)))/c0) + w), prove_me(IRMatcher::Const(1))) ||
 rewrite((((((x + y) + z)/c0) - (x/c0)) + w), (((((x % c0) + z) + y)/c0) + w), prove_me(IRMatcher::Const(1))) ||
 rewrite((((((x + y) + z)/c0) - (y/c0)) + w), (((((y % c0) + z) + x)/c0) + w), prove_me(IRMatcher::Const(1))) ||
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
 rewrite(((x - (y + z)) + (x + (w + y))), ((x - z) + (x + w))) ||
 rewrite(((x - (y + z)) + (x + (w + z))), ((x - y) + (x + w))) ||
 rewrite(((x - (y + z)) + (x + (y + w))), ((x - z) + (x + w))) ||
 rewrite(((x - (y + z)) + (x + (z + w))), ((x - y) + (x + w))) ||
 rewrite(((x - (y + z)) + (y + w)), ((x - z) + w)) ||
 rewrite(((x - (y + z)) + (z + w)), ((x - y) + w)) ||
 rewrite(((x - (y + z)) + ((w + y) + x)), ((x - z) + (w + x))) ||
 rewrite(((x - (y + z)) + ((w + z) + x)), ((x - y) + (w + x))) ||
 rewrite(((x - (y + z)) + ((y + w) + x)), ((x - z) + (w + x))) ||
 rewrite(((x - (y + z)) + ((z + w) + x)), ((x - y) + (w + x))) ||
 rewrite(((x - (y + (z + w))) + (u + (v + w))), ((x - (y + z)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (v + z))), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (w + v))), ((x - (y + z)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (u + (z + v))), ((x - (y + w)) + (u + v))) ||
 rewrite(((x - (y + (z + w))) + (w + z)), ((x - (y + z)) + z)) ||
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
 rewrite(((x - (y + (z*w))) + (u + (v*w))), ((x - y) + (u + ((v - z)*w)))) ||
 rewrite(((x - (y + (z*w))) + (u + (v*z))), ((x - y) + (u + ((v - w)*z)))) ||
 rewrite(((x - (y + (z*w))) + (u + (w*v))), ((x - y) + (u + ((v - z)*w)))) ||
 rewrite(((x - (y + (z*w))) + (u + (z*v))), ((x - y) + (u + ((v - w)*z)))) ||
 rewrite(((x - (y + (z*w))) + ((u*w) + v)), ((x - y) + (((u - z)*w) + v))) ||
 rewrite(((x - (y + (z*w))) + ((u*z) + v)), ((x - y) + (((u - w)*z) + v))) ||
 rewrite(((x - (y + (z*w))) + ((w*u) + v)), ((x - y) + ((w*(u - z)) + v))) ||
 rewrite(((x - (y + (z*w))) + ((z*u) + v)), ((x - (y - (z*(u - w)))) + v)) ||
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
 rewrite(((x - (((y + z) + w) + u)) + (z + v)), ((x - ((y + w) + u)) + v)) ||
 rewrite(((x - ((y*z) + w)) + (u + (v*y))), (x + ((y*(v - z)) + (u - w)))) ||
 rewrite(((x - ((y*z) + w)) + (u + (v*z))), (x + ((z*(v - y)) + (u - w)))) ||
 rewrite(((x - ((y*z) + w)) + (u + (y*v))), (x + ((y*(v - z)) + (u - w)))) ||
 rewrite(((x - ((y*z) + w)) + (u + (z*v))), (x + ((z*(v - y)) + (u - w)))) ||
 rewrite(((x - ((y*z) + w)) + ((u*y) + v)), ((x + ((y*(u - z)) - w)) + v)) ||
 rewrite(((x - ((y*z) + w)) + ((u*z) + v)), ((((u - y)*z) - w) + (x + v))) ||
 rewrite(((x - ((y*z) + w)) + ((y*u) + v)), ((x + ((y*(u - z)) - w)) + v)) ||
 rewrite(((x - ((y*z) + w)) + ((z*u) + v)), ((((u - y)*z) - w) + (x + v))) ||
 rewrite(((x - (y*z)) + (w + (z*y))), (x + w)) ||
 rewrite(((x - (y*z)) + ((z*y) + w)), (x + w)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x - y) + ((y + z) - w)), (x + (z - w))) ||
 rewrite(((x - y) + ((z + y) - w)), (x + (z - w))))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*c0) + (y + ((x + z)*c1))), (y + (z*c1)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((z + x)*c1))), (y + (z*c1)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((x + y)*c1) + z)), ((y*c1) + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((y + x)*c1) + z)), ((y*c1) + z), ((c0 + c1) == 0)) ||
 rewrite(((x*y) + (z + (w + (u + (v*x))))), (u + (z + (w + ((y + v)*x))))) ||
 rewrite(((x*y) + (z + (w + (u + (v*y))))), (u + (z + (w + ((x + v)*y))))) ||
 rewrite(((x*y) + (z + (w + (u + (x*v))))), (u + (z + (w + ((y + v)*x))))) ||
 rewrite(((x*y) + (z + (w + (u + (y*v))))), (u + (z + (w + ((x + v)*y))))) ||
 rewrite(((x*y) + (z + (w + ((u*x) + v)))), (v + (z + (w + (x*(y + u)))))) ||
 rewrite(((x*y) + (z + (w + ((u*y) + v)))), (v + (z + (w + (y*(x + u)))))) ||
 rewrite(((x*y) + (z + (w + ((x*u) + v)))), (v + (z + (w + (x*(y + u)))))) ||
 rewrite(((x*y) + (z + (w + ((y*u) + v)))), (v + (z + (w + (y*(x + u)))))) ||
 rewrite(((x*y) + (z + (w + (u*x)))), (w + (z + ((y + u)*x)))) ||
 rewrite(((x*y) + (z + (w + (u*y)))), (w + (z + ((x + u)*y)))) ||
 rewrite(((x*y) + (z + (w + (x*u)))), (w + (z + ((y + u)*x)))) ||
 rewrite(((x*y) + (z + (w + (y*u)))), (w + (z + ((x + u)*y)))) ||
 rewrite(((x*y) + (z + ((w + (u*x)) + v))), (w + (z + (((y + u)*x) + v)))) ||
 rewrite(((x*y) + (z + ((w + (u*y)) + v))), (w + (z + (((x + u)*y) + v)))) ||
 rewrite(((x*y) + (z + ((w + (x*u)) + v))), (w + (z + (((y + u)*x) + v)))) ||
 rewrite(((x*y) + (z + ((w + (y*u)) + v))), (w + (z + (((x + u)*y) + v)))) ||
 rewrite(((x*y) + (z + (((w*x) + u) + v))), (u + (z + ((x*(y + w)) + v)))) ||
 rewrite(((x*y) + (z + (((w*y) + u) + v))), (u + (z + ((y*(x + w)) + v)))) ||
 rewrite(((x*y) + (z + (((x*w) + u) + v))), (u + (z + ((x*(y + w)) + v)))) ||
 rewrite(((x*y) + (z + (((y*w) + u) + v))), (u + (z + ((y*(x + w)) + v)))) ||
 rewrite(((x*y) + (z + ((w*x) + u))), (u + (z + (x*(y + w))))) ||
 rewrite(((x*y) + (z + ((w*y) + u))), (u + (z + (y*(x + w))))) ||
 rewrite(((x*y) + (z + ((x*w) + u))), (u + (z + (x*(y + w))))) ||
 rewrite(((x*y) + (z + ((y*w) + u))), (u + (z + (y*(x + w))))) ||
 rewrite(((x*y) + (z + (w - (u + (y*x))))), (z + (w - u))) ||
 rewrite(((x*y) + (z + (w - ((y*x) + u)))), (z + (w - u))) ||
 rewrite(((x*y) + (z + (w*x))), (z + ((y + w)*x))) ||
 rewrite(((x*y) + (z + (w*y))), (z + ((x + w)*y))) ||
 rewrite(((x*y) + (z + (x*w))), (z + ((w + y)*x))) ||
 rewrite(((x*y) + (z + (y*w))), (z + ((w + x)*y))) ||
 rewrite(((x*y) + ((z + (w + (u*x))) + v)), (w + ((z + ((y + u)*x)) + v))) ||
 rewrite(((x*y) + ((z + (w + (u*y))) + v)), (w + ((z + ((x + u)*y)) + v))) ||
 rewrite(((x*y) + ((z + (w + (x*u))) + v)), (w + ((z + ((y + u)*x)) + v))) ||
 rewrite(((x*y) + ((z + (w + (y*u))) + v)), (w + ((z + ((x + u)*y)) + v))) ||
 rewrite(((x*y) + ((z + ((w*x) + u)) + v)), ((x*(y + w)) + ((z + u) + v))) ||
 rewrite(((x*y) + ((z + ((w*y) + u)) + v)), ((z + (((x + w)*y) + u)) + v)) ||
 rewrite(((x*y) + ((z + ((x*w) + u)) + v)), (u + (z + ((x*(y + w)) + v)))) ||
 rewrite(((x*y) + ((z + ((y*w) + u)) + v)), (u + (z + ((y*(x + w)) + v)))) ||
 rewrite(((x*y) + ((z + (w*x)) + u)), ((z + ((y + w)*x)) + u)) ||
 rewrite(((x*y) + ((z + (w*y)) + u)), ((z + ((x + w)*y)) + u)) ||
 rewrite(((x*y) + ((z + (x*w)) + u)), ((z + ((y + w)*x)) + u)) ||
 rewrite(((x*y) + ((z + (y*w)) + u)), ((z + ((x + w)*y)) + u)) ||
 rewrite(((x*y) + (((z + (w*x)) + u) + v)), (((z + ((y + w)*x)) + u) + v)) ||
 rewrite(((x*y) + (((z + (w*y)) + u) + v)), (((z + ((x + w)*y)) + u) + v)) ||
 rewrite(((x*y) + (((z + (x*w)) + u) + v)), (((z + ((y + w)*x)) + u) + v)) ||
 rewrite(((x*y) + (((z + (y*w)) + u) + v)), (((z + ((x + w)*y)) + u) + v)) ||
 rewrite(((x*y) + ((((x*z) + w) + u) + v)), (w + (((x*(y + z)) + u) + v))) ||
 rewrite(((x*y) + ((((y*z) + w) + u) + v)), (w + (((y*(x + z)) + u) + v))) ||
 rewrite(((x*y) + ((((z*x) + w) + u) + v)), ((((y + z)*x) + w) + (u + v))) ||
 rewrite(((x*y) + ((((z*y) + w) + u) + v)), (((((x + z)*y) + w) + u) + v)) ||
 rewrite(((x*y) + (((x*z) + w) + u)), ((((y + z)*x) + w) + u)) ||
 rewrite(((x*y) + (((y*z) + w) + u)), (w + ((y*(x + z)) + u))) ||
 rewrite(((x*y) + (((z*x) + w) + u)), ((((y + z)*x) + w) + u)) ||
 rewrite(((x*y) + (((z*y) + w) + u)), ((((x + z)*y) + w) + u)) ||
 rewrite(((x*y) + ((z - (w + (y*x))) + u)), ((z - w) + u)) ||
 rewrite(((x*y) + ((z - ((y*x) + w)) + u)), ((z - w) + u)) ||
 rewrite(((x*y) + ((x*z) + w)), ((x*(y + z)) + w)) ||
 rewrite(((x*y) + ((y*z) + w)), ((y*(z + x)) + w)) ||
 rewrite(((x*y) + ((z*x) + w)), ((x*(y + z)) + w)) ||
 rewrite(((x*y) + ((z*y) + w)), ((y*(x + z)) + w)) ||
 rewrite(((x*(y + z)) + (w + (u*(z + y)))), (w + ((x + u)*(z + y)))) ||
 rewrite(((x*(y + z)) + (w + ((z + y)*u))), (((y + z)*(x + u)) + w)) ||
 rewrite(((x*(y + z)) + ((w*(z + y)) + u)), (((x + w)*(z + y)) + u)) ||
 rewrite(((x*(y + z)) + (((z + y)*w) + u)), (((x + w)*(z + y)) + u)) ||
 rewrite((((x + y)*z) + (w + (u*(y + x)))), (((x + y)*(z + u)) + w)) ||
 rewrite((((x + y)*z) + (w + ((y + x)*u))), (((x + y)*(z + u)) + w)) ||
 rewrite((((x + y)*z) + ((w*(y + x)) + u)), (((w + z)*(y + x)) + u)) ||
 rewrite((((x + y)*z) + (((y + x)*w) + u)), (((z + w)*(y + x)) + u)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x*2) + (y - x)), (x + y)) ||
 rewrite(((x*c0) + (y - x)), (x + y), (c0 == 2)) ||
 rewrite(((x*c0) + (((c1 - (x*c2))*c3) - y)), (c1 - y), (((((c1 - c2)*c3) + c0) == c1) && (c1 == 0))) ||
 rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((((0 - c1)*c2) + c0) == 0)) ||
 rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((c1*c2) == c0)) ||
 rewrite(((x*y) + (z - (w + (u + (v*x))))), ((x*(y - v)) - (w + (u - z)))) ||
 rewrite(((x*y) + (z - (w + (u + (v*y))))), ((y*(x - v)) - (w + (u - z)))) ||
 rewrite(((x*y) + (z - (w + (u + (x*v))))), ((x*(y - v)) - (w + (u - z)))) ||
 rewrite(((x*y) + (z - (w + (u + (y*v))))), ((y*(x - v)) - (w + (u - z)))) ||
 rewrite(((x*y) + (z - (w + ((u*x) + v)))), ((z - v) - (w - (x*(y - u))))) ||
 rewrite(((x*y) + (z - (w + ((u*y) + v)))), ((z - v) - (w - (y*(x - u))))) ||
 rewrite(((x*y) + (z - (w + ((x*u) + v)))), ((z - v) - (w - (x*(y - u))))) ||
 rewrite(((x*y) + (z - (w + ((y*u) + v)))), ((z - v) - (w - (y*(x - u))))) ||
 rewrite(((x*y) + (z - (w + (u*x)))), (z - (w - ((y - u)*x)))) ||
 rewrite(((x*y) + (z - (w + (u*y)))), (z - (w - ((x - u)*y)))) ||
 rewrite(((x*y) + (z - (w + (x*u)))), ((x*(y - u)) - (w - z))) ||
 rewrite(((x*y) + (z - (w + (y*u)))), (z - (w - (y*(x - u))))) ||
 rewrite(((x*y) + (z - ((w + (u*x)) + v))), (z - ((w - ((y - u)*x)) + v))) ||
 rewrite(((x*y) + (z - ((w + (u*y)) + v))), (z - ((w - ((x - u)*y)) + v))) ||
 rewrite(((x*y) + (z - ((w + (x*u)) + v))), ((x*(y - u)) - ((w - z) + v))) ||
 rewrite(((x*y) + (z - ((w + (y*u)) + v))), (z - ((w - ((x - u)*y)) + v))) ||
 rewrite(((x*y) + (z - (((w*x) + u) + v))), ((x*(y - w)) - ((u - z) + v))) ||
 rewrite(((x*y) + (z - (((w*y) + u) + v))), (z + (((x - w)*y) - (u + v)))) ||
 rewrite(((x*y) + (z - (((x*w) + u) + v))), (z - (u - ((x*(y - w)) - v)))) ||
 rewrite(((x*y) + (z - (((y*w) + u) + v))), (z - (u - ((y*(x - w)) - v)))) ||
 rewrite(((x*y) + (z - ((w*x) + u))), (z + (((y - w)*x) - u))) ||
 rewrite(((x*y) + (z - ((w*y) + u))), (z + (((x - w)*y) - u))) ||
 rewrite(((x*y) + (z - ((x*w) + u))), (z - (u - (x*(y - w))))) ||
 rewrite(((x*y) + (z - ((y*w) + u))), (z - (u - (y*(x - w))))) ||
 rewrite(((x*y) + ((x*z) - w)), ((x*(z + y)) - w)) ||
 rewrite(((x*y) + ((y*z) - w)), ((y*(z + x)) - w)) ||
 rewrite(((x*y) + ((z*x) - w)), ((x*(y + z)) - w)) ||
 rewrite(((x*y) + ((z*y) - w)), ((y*(x + z)) - w)) ||
 rewrite((((x + (y*c0))*c1) + ((y*c2) - z)), ((x*c1) - z), (((c0*c1) + c2) == 0)) ||
 rewrite(((((x*c0) + y)*c1) + ((x*c2) - z)), ((y*c1) - z), (((c0*c1) + c2) == 0)))) ||
((b.node_type() == IRNodeType::Mod) && (
 rewrite(((x*c0) + ((y + (z + (w*c1))) % c2)), ((x*c0) + ((y + z) % c2)), prove_me(((c1 % c2) == 0))) ||
 rewrite(((x*c0) + ((y + ((z*c1) + w)) % c2)), ((x*c0) + ((y + w) % c2)), prove_me(((c1 % c2) == 0))) ||
 rewrite(((x*c0) + (((y + (z*c1)) + w) % c2)), ((x*c0) + ((y + w) % c2)), prove_me(((c1 % c2) == 0))) ||
 rewrite(((x*c0) + ((((y*c1) + z) + w) % c2)), ((x*c0) + ((z + w) % c2)), prove_me(((c1 % c2) == 0))))))) ||
((a.node_type() == IRNodeType::Div) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x/c0) + (y - ((x + z)/c0))), (y - (((x % c0) + z)/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite(((x/c0) + (y - ((z + x)/c0))), (y - (((x % c0) + z)/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x + y)/c0) + (z - (w + (x/c0)))), ((((x % c0) + y)/c0) + (z - w)), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x + y)/c0) + (z - (w + (y/c0)))), (((x + (y % c0))/c0) - (w - z)), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x + y)/c0) + (z - ((x/c0) + w))), (z + (((y + (x % c0))/c0) - w)), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x + y)/c0) + (z - ((y/c0) + w))), (z + (((x + (y % c0))/c0) - w)), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x + y)/c0) + (z - (x/c0))), (z + ((y + (x % c0))/c0)), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x + y)/c0) + (z - (y/c0))), (z + ((x + (y % c0))/c0)), prove_me(IRMatcher::Const(1))))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite((min((((x + c0)/c1)*c1), (x + c2)) + c3), (x + fold((min(((c3 + c0) - c1), ((c1 + c0) + c3)) + 1))), ((((c1 <= 1) || (c0 < c2)) || (((c1 + c2) + -1) <= c0)) && ((((c0 <= c2) || (c1 <= -1)) || (c0 < (c1 + c2))) && ((((1 <= c1) || (c0 <= c2)) || ((c0 + c1) < c2)) && ((((-1 <= c1) || ((c2 + -1) <= (c0 + c1))) || (c0 < c2)) && (((-1 <= c1) || ((c2 + -2) <= (c0 + c1))) && ((c1 <= 1) || (((c1 + c2) + -2) <= c0)))))))) ||
 rewrite((min(min(x, min(y, z)), (min(w, y) + c0)) + u), (min(min(x, z), (min(w, y) + c0)) + u), (c0 <= 0)) ||
 rewrite((min(min(x, min(y, z)), (min(w, z) + c0)) + u), (min(min(x, y), (min(w, z) + c0)) + u), (c0 <= 0)) ||
 rewrite((min(min(x, min(y, z)), (min(y, w) + c0)) + u), (min(min(x, z), (min(y, w) + c0)) + u), (c0 <= 0)) ||
 rewrite((min(min(x, min(y, z)), (min(z, w) + c0)) + u), (min(min(x, y), (min(z, w) + c0)) + u), (c0 <= 0)) ||
 rewrite((min(min(min(x, y), z), (min(w, x) + c0)) + u), (min(min(y, z), (min(w, x) + c0)) + u), (c0 <= 0)) ||
 rewrite((min(min(min(x, y), z), (min(w, y) + c0)) + u), (min(min(x, z), (min(w, y) + c0)) + u), (c0 <= 0)) ||
 rewrite((min(min(min(x, y), z), (min(x, w) + c0)) + u), (min(min(y, z), (min(x, w) + c0)) + u), (c0 <= 0)) ||
 rewrite((min(min(min(x, y), z), (min(y, w) + c0)) + u), (min(min(x, z), (min(y, w) + c0)) + u), (c0 <= 0)) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((min(x, (y + (z + w))) + (y - (u + w))), (min((x - w), (z + y)) + (y - u))) ||
 rewrite((min(x, (y + (z + w))) + (y - (u + z))), (min((x - z), (y + w)) + (y - u))) ||
 rewrite((min(x, (y + (z + w))) + (y - (w + u))), (min((x - w), (z + y)) + (y - u))) ||
 rewrite((min(x, (y + (z + w))) + (y - (z + u))), (min((x - z), (y + w)) + (y - u))) ||
 rewrite((min(x, (y + (z + (w - u)))) + (u - w)), (min((x - w), (z + (y - u))) + u)) ||
 rewrite((min(x, (y + ((z - w) + u))) + (w - z)), (min((x - z), ((y - w) + u)) + w)) ||
 rewrite((min(x, ((y + z) + w)) + (w - (u + y))), (min((x - y), (z + w)) + (w - u))) ||
 rewrite((min(x, ((y + z) + w)) + (w - (u + z))), (min((x - z), (y + w)) + (w - u))) ||
 rewrite((min(x, ((y + z) + w)) + (w - (y + u))), (min((x - y), (z + w)) + (w - u))) ||
 rewrite((min(x, ((y + z) + w)) + (w - (z + u))), (min((x - z), (y + w)) + (w - u))) ||
 rewrite((min(x, ((y + (z - w)) + u)) + (w - z)), (min((x - z), ((y - w) + u)) + w)) ||
 rewrite((min(x, (((y - z) + w) + u)) + (z - y)), (min((u - (z - w)), (x - y)) + z)) ||
 rewrite((min((x + (y + z)), w) + (x - (u + y))), (min((w - y), (z + x)) - (u - x))) ||
 rewrite((min((x + (y + z)), w) + (x - (u + z))), (min((w - z), (y + x)) - (u - x))) ||
 rewrite((min((x + (y + z)), w) + (x - (y + u))), (min((w - y), (z + x)) + (x - u))) ||
 rewrite((min((x + (y + z)), w) + (x - (z + u))), (min((w - z), (y + x)) + (x - u))) ||
 rewrite((min((x + (y + (z - w))), u) + (w - z)), (min(((y + x) - w), (u - z)) + w)) ||
 rewrite((min((x + ((y - z) + w)), u) + (z - y)), (min((x - (z - w)), (u - y)) + z)) ||
 rewrite((min(((x + y) + z), w) + (z - (u + x))), (min((w - x), (y + z)) + (z - u))) ||
 rewrite((min(((x + y) + z), w) + (z - (u + y))), (min((w - y), (x + z)) + (z - u))) ||
 rewrite((min(((x + y) + z), w) + (z - (x + u))), (min((w - x), (y + z)) + (z - u))) ||
 rewrite((min(((x + y) + z), w) + (z - (y + u))), (min((w - y), (x + z)) + (z - u))) ||
 rewrite((min(((x + (y - z)) + w), u) + (z - y)), (min(((x + w) - z), (u - y)) + z)) ||
 rewrite((min((((x - y) + z) + w), u) + (y - x)), (min(((w + z) - y), (u - x)) + y)))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite((max(x, (y - min(max(z, w), min(u, w)))) + v), (max(x, (y - min(w, u))) + v)) ||
 rewrite((max(x, (y - min(max(z, w), min(u, z)))) + v), (max(x, (y - min(z, u))) + v)) ||
 rewrite((max(x, (y - min(max(z, w), min(w, u)))) + v), (max(x, (y - min(w, u))) + v)) ||
 rewrite((max(x, (y - min(max(z, w), min(z, u)))) + v), (max(x, (y - min(z, u))) + v)) ||
 rewrite((max((x - min(max(y, z), min(w, y))), u) + v), (max((x - min(y, w)), u) + v)) ||
 rewrite((max((x - min(max(y, z), min(w, z))), u) + v), (max((x - min(z, w)), u) + v)) ||
 rewrite((max((x - min(max(y, z), min(y, w))), u) + v), (max((x - min(y, w)), u) + v)) ||
 rewrite((max((x - min(max(y, z), min(z, w))), u) + v), (max((x - min(z, w)), u) + v)) ||
 rewrite((max((x - max(y, max(z, (w + x)))), c0) + u), (u - min(max(w, (max(z, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max(z, (x + w)))), c0) + u), (u - min(max(w, (max(z, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max((x + z), w))), c0) + u), (u - min(max((max(y, w) - x), z), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max((z + x), w))), c0) + u), (u - min(max(z, (max(w, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max(y, (x + z)), w)), c0) + u), (u - min(max(z, (max(y, w) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max(y, (z + x)), w)), c0) + u), (u - min(max(z, (max(y, w) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max((x + y), z), w)), c0) + u), (u - min(max((max(z, w) - x), y), fold((0 - c0))))) ||
 rewrite((max((x - max(max((y + x), z), w)), c0) + u), (u - min(max((max(z, w) - x), y), fold((0 - c0))))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((max(x, (y + z)) + (w - y)), (max(z, (x - y)) + w)) ||
 rewrite((max(x, (y + z)) + (w - z)), (max(y, (x - z)) + w)) ||
 rewrite((max(x, (y + (z - w))) + (w - z)), max(y, (x - (z - w)))) ||
 rewrite((max(x, ((y - z) + w)) + (z - y)), max(w, ((z + x) - y))) ||
 rewrite((max((x + y), z) + (w - x)), (max((z - x), y) + w)) ||
 rewrite((max((x + y), z) + (w - y)), (max((z - y), x) + w)) ||
 rewrite((max((x + (y - z)), w) + (z - y)), max(x, (w + (z - y)))) ||
 rewrite((max(((x - y) + z), w) + (y - x)), max(z, (w + (y - x)))))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Sub) && (
 rewrite((select(x, (y + (z + w)), u) + (v - w)), (select(x, (y + z), (u - w)) + v)) ||
 rewrite((select(x, (y + (z + w)), u) + (v - z)), (select(x, (y + w), (u - z)) + v)) ||
 rewrite((select(x, ((y + z) + w), u) + (v - y)), (select(x, (z + w), (u - y)) + v)) ||
 rewrite((select(x, ((y + z) + w), u) + (v - z)), (select(x, (y + w), (u - z)) + v))))))
