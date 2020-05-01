((b.node_type() == IRNodeType::Add) && (
 rewrite(min(x, ((y + (z + select(w, c0, c1))) + c2)), min(x, (y + (z + select(w, fold((c2 + c0)), fold((c2 + c1))))))) ||
 rewrite(min(x, ((y + (select(z, c0, c1) + w)) + c2)), min(x, (y + (select(z, fold((c2 + c0)), fold((c2 + c1))) + w)))) ||
 rewrite(min(x, (((y + select(z, c0, c1)) + w) + c2)), min(x, (w + (y + select(z, fold((c2 + c0)), fold((c2 + c1))))))) ||
 rewrite(min(x, (((select(y, c0, c1) + z) + w) + c2)), min(x, ((select(y, fold((c2 + c0)), fold((c2 + c1))) + w) + z))) ||
 rewrite(min(x, (((y + select(z, c0, c1)) - w) + c2)), min(x, ((y + select(z, fold((c2 + c0)), fold((c2 + c1)))) - w))) ||
 rewrite(min(x, (((select(y, c0, c1) + z) - w) + c2)), min(x, ((select(y, fold((c2 + c0)), fold((c2 + c1))) - w) + z))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(x, min(y, (x + c0))), min(y, x), (0 <= c0)) ||
 rewrite(min(x, min(y, (min(x, z) + c0))), min(y, (min(x, z) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(z, x) + c0))), min(y, (min(z, x) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(z, min(x, y)) + c0))), min(x, min(y, (min(z, y) + c0))), (0 <= c0)) ||
 rewrite(min(x, min(y, (min(z, min(y, x)) + c0))), min(x, min(y, (min(z, y) + c0))), (0 <= c0)) ||
 rewrite(min(x, min(y, (min(min(x, y), z) + c0))), min(x, min(y, (min(y, z) + c0))), (0 <= c0)) ||
 rewrite(min(x, min(y, (min(min(y, x), z) + c0))), min(x, min(y, (min(y, z) + c0))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(x, z))), min(y, min(x, z))) ||
 rewrite(min(x, min(y, min(z, x))), min(y, min(z, x))) ||
 rewrite(min(x, min(y, min(z, (min(w, x) + c0)))), min(x, min(y, min(z, (w + c0)))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(z, (min(x, w) + c0)))), min(x, min(y, min(z, (w + c0)))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(z, min(w, min(u, min(v, x)))))), min(y, min(z, min(w, min(u, min(v, x)))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(u, min(x, v)))))), min(y, min(z, min(w, min(u, min(x, v)))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(min(u, x), v))))), min(y, min(z, min(w, min(min(u, x), v))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(min(x, u), v))))), min(y, min(z, min(w, min(min(x, u), v))))) ||
 rewrite(min(x, min(y, min(z, min(min(w, min(u, x)), v)))), min(y, min(z, min(min(w, min(u, x)), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(w, min(x, u)), v)))), min(y, min(z, min(min(w, min(x, u)), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(min(w, x), u), v)))), min(y, min(z, min(min(min(w, x), u), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(min(x, w), u), v)))), min(y, min(z, min(min(min(x, w), u), v)))) ||
 rewrite(min(x, min(y, min((min(x, z) + c0), w))), min(x, min(y, min((z + c0), w))), (0 <= c0)) ||
 rewrite(min(x, min(y, min((min(z, x) + c0), w))), min(x, min(y, min((z + c0), w))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(min(z, min(w, min(u, x))), v))), min(y, min(min(z, min(w, min(u, x))), v))) ||
 rewrite(min(x, min(y, min(min(z, min(w, min(x, u))), v))), min(y, min(min(z, min(w, min(x, u))), v))) ||
 rewrite(min(x, min(y, min(min(z, min(min(w, x), u)), v))), min(y, min(min(z, min(min(w, x), u)), v))) ||
 rewrite(min(x, min(y, min(min(z, min(min(x, w), u)), v))), min(y, min(min(z, min(min(x, w), u)), v))) ||
 rewrite(min(x, min(y, min(min(min(z, min(w, x)), u), v))), min(y, min(min(min(z, min(w, x)), u), v))) ||
 rewrite(min(x, min(y, min(min(min(z, min(x, w)), u), v))), min(y, min(min(min(z, min(x, w)), u), v))) ||
 rewrite(min(x, min(y, min(min(min(min(x, z), w), u), v))), min(y, min(min(min(min(x, z), w), u), v))) ||
 rewrite(min(x, min(y, min(min(min(min(z, x), w), u), v))), min(y, min(min(min(min(z, x), w), u), v))) ||
 rewrite(min(x, min((x + c0), y)), min(x, y), (0 <= c0)) ||
 rewrite(min(x, min((min(x, y) + c0), z)), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(x, min((min(y, x) + c0), z)), min((min(y, x) + c0), z), (c0 <= 0)) ||
 rewrite(min(x, min((min(y, min(x, z)) + c0), z)), min(x, min((min(y, z) + c0), z)), (0 <= c0)) ||
 rewrite(min(x, min((min(y, min(z, x)) + c0), z)), min(x, min((min(y, z) + c0), z)), (0 <= c0)) ||
 rewrite(min(x, min((min(min(x, y), z) + c0), y)), min(x, min((min(y, z) + c0), y)), (0 <= c0)) ||
 rewrite(min(x, min((min(min(y, x), z) + c0), y)), min(x, min((min(y, z) + c0), y)), (0 <= c0)) ||
 rewrite(min(x, min(min(x, y), z)), min(x, min(y, z))) ||
 rewrite(min(x, min(min(y, x), z)), min(x, min(y, z))) ||
 rewrite(min(x, min(min(y, (min(x, z) + c0)), w)), min(x, min(min(y, (z + c0)), w)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, (min(z, x) + c0)), w)), min(x, min(min(y, (z + c0)), w)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, (z/c0)), (w/c0))), min(x, min((min(w, z)/c0), y)), prove_me((((0 <= (1/c0)) && (-2 <= (7/c0))) && ((-2/c0) <= 0)))) ||
 rewrite(min(x, min(min(y, (z/c0)), ((w + u)/c0))), min(x, min(y, (min(z, (w + u))/c0))), prove_me(((0 <= (1/c0)) && ((-6/c0) <= 0)))) ||
 rewrite(min(x, min(min(y, (z/c0)), ((w + u)/c0))), min(x, min(y, (min(z, (w + u))/c0))), prove_me((((((0 <= (1/c0)) && (min((-7/c0), 3) <= (-2/c0))) && (min((8/c0), -2) <= (24/c0))) && (min((-12357/c0), 1374) <= (-12348/c0))) && ((-9864/c0) <= 0)))) ||
 rewrite(min(x, min(min(y, min(z, min(w, min(u, x)))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(w, min(x, u)))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(min(w, x), u))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(min(x, w), u))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(min(z, min(w, x)), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(z, min(x, w)), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(min(x, z), w), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(min(z, x), w), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min((min(x, y) + c0), z), w)), min(x, min(min((y + c0), z), w)), (0 <= c0)) ||
 rewrite(min(x, min(min((min(y, x) + c0), z), w)), min(x, min(min((y + c0), z), w)), (0 <= c0)) ||
 rewrite(min(x, min(min((y/c0), z), (w/c0))), min(x, min((min(w, y)/c0), z)), prove_me((((0 <= (1/c0)) && (-2 <= (7/c0))) && ((-2/c0) <= 0)))) ||
 rewrite(min(x, min(min((y/c0), z), ((w + u)/c0))), min(x, min((min(y, (w + u))/c0), z)), prove_me(((0 <= (1/c0)) && ((-6/c0) <= 0)))) ||
 rewrite(min(x, min(min((y/c0), z), ((w + u)/c0))), min(x, min((min(y, (w + u))/c0), z)), prove_me((((((0 <= (1/c0)) && (min((-7/c0), 3) <= (-2/c0))) && (min((8/c0), -2) <= (24/c0))) && (min((-12357/c0), 1374) <= (-12348/c0))) && ((-9864/c0) <= 0)))) ||
 rewrite(min(x, min(min((y/c0), (z + w)), (u/c0))), min(x, min((min(u, y)/c0), (z + w))), prove_me(((((0 <= (1/c0)) && (-1 <= (5/c0))) && (min((-28599/c0), 9532) <= (-28593/c0))) && ((-57198/c0) <= 0)))) ||
 rewrite(min(x, min(min(min(y, min(z, min(w, x))), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(z, min(x, w))), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(min(x, z), w)), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(min(z, x), w)), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(min(y, min(x, z)), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(y, min(z, x)), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(min(x, y), z), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(min(y, x), z), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(select(y, z, (w/c0)), (x + c1))), min(x, select(y, z, (w/c0))), (0 <= c1)))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(min(((x + (y + select(z, c0, c1))) + c2), w), min((x + (y + select(z, fold((c2 + c0)), fold((c2 + c1))))), w)) ||
 rewrite(min(((x + (select(y, c0, c1) + z)) + c2), w), min((x + (select(y, fold((c2 + c0)), fold((c2 + c1))) + z)), w)) ||
 rewrite(min((((x + select(y, c0, c1)) + z) + c2), w), min((z + (x + select(y, fold((c2 + c0)), fold((c2 + c1))))), w)) ||
 rewrite(min((((select(x, c0, c1) + y) + z) + c2), w), min(((select(x, fold((c2 + c0)), fold((c2 + c1))) + z) + y), w)) ||
 rewrite(min((((x + select(y, c0, c1)) - z) + c2), w), min(((x + select(y, fold((c2 + c0)), fold((c2 + c1)))) - z), w)) ||
 rewrite(min((((select(x, c0, c1) + y) - z) + c2), w), min(((select(x, fold((c2 + c0)), fold((c2 + c1))) - z) + y), w)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((x + y), (z + (w + x))), (x + min(y, (w + z)))) ||
 rewrite(min((x + y), (z + (w + y))), (min(x, (z + w)) + y)) ||
 rewrite(min((x + y), (z + (x + w))), (x + min(y, (z + w)))) ||
 rewrite(min((x + y), (z + (y + w))), (min((z + w), x) + y)) ||
 rewrite(min((x + y), ((x + z) + w)), (x + min((w + z), y))) ||
 rewrite(min((x + y), ((y + z) + w)), (min(x, (w + z)) + y)) ||
 rewrite(min((x + y), ((z + x) + w)), (x + min((z + w), y))) ||
 rewrite(min((x + y), ((z + y) + w)), (min(x, (z + w)) + y)) ||
 rewrite(min((x + (y + z)), (w + y)), (min((x + z), w) + y)) ||
 rewrite(min((x + (y + z)), (w + z)), (min((x + y), w) + z)) ||
 rewrite(min((x + (y + z)), (w + (u + y))), (min((x + z), (w + u)) + y)) ||
 rewrite(min((x + (y + z)), (w + (u + z))), (min((x + y), (w + u)) + z)) ||
 rewrite(min((x + (y + z)), (w + (y + u))), (min((x + z), (w + u)) + y)) ||
 rewrite(min((x + (y + z)), (w + (z + u))), (min((x + y), (w + u)) + z)) ||
 rewrite(min((x + (y + z)), (y + w)), (min((x + z), w) + y)) ||
 rewrite(min((x + (y + z)), (z + w)), (min((x + y), w) + z)) ||
 rewrite(min((x + (y + z)), ((w + y) + u)), (min((x + z), (w + u)) + y)) ||
 rewrite(min((x + (y + z)), ((w + z) + u)), (min((x + y), (w + u)) + z)) ||
 rewrite(min((x + (y + z)), ((y + w) + u)), (min((x + z), (u + w)) + y)) ||
 rewrite(min((x + (y + z)), ((z + w) + u)), (min((x + y), (u + w)) + z)) ||
 rewrite(min((x + (y + (z + w))), (u + (w + z))), (min((x + y), u) + (w + z))) ||
 rewrite(min((x + (y + (z + w))), ((w + z) + u)), (min((x + y), u) + (w + z))) ||
 rewrite(min((x + (y + (z*w))), (u + (w*z))), (min((x + y), u) + (w*z))) ||
 rewrite(min((x + (y + (z*w))), ((w*z) + u)), (min(u, (y + x)) + (w*z))) ||
 rewrite(min((x + ((y + z) + w)), (u + (z + y))), (min((x + w), u) + (z + y))) ||
 rewrite(min((x + ((y + z) + w)), ((z + y) + u)), (min((x + w), u) + (z + y))) ||
 rewrite(min((x + ((y*z) + w)), (u + (z*y))), (min((x + w), u) + (z*y))) ||
 rewrite(min((x + ((y*z) + w)), ((z*y) + u)), (min((x + w), u) + (z*y))) ||
 rewrite(min((x + ((y + z) - w)), (u + (y - v))), (y + min(((x + z) - w), (u - v)))) ||
 rewrite(min((x + ((y + z) - w)), (u + (z - v))), (z + min(((y + x) - w), (u - v)))) ||
 rewrite(min((x + ((y + z) - w)), ((y - u) + v)), (min(((x + z) - w), (v - u)) + y)) ||
 rewrite(min((x + ((y + z) - w)), ((z - u) + v)), (min(((y + x) - w), (v - u)) + z)) ||
 rewrite(min((x + (y*z)), (w + (u + (z*y)))), (min(x, (w + u)) + (z*y))) ||
 rewrite(min((x + (y*z)), (w + ((z*y) + u))), (min((w + u), x) + (z*y))) ||
 rewrite(min((x + (y*z)), ((w + (z*y)) + u)), (min((u + w), x) + (z*y))) ||
 rewrite(min((x + (y*z)), (((z*y) + w) + u)), (min((u + w), x) + (z*y))) ||
 rewrite(min(((x + y) + z), (w + x)), (min((y + z), w) + x)) ||
 rewrite(min(((x + y) + z), (w + y)), (min((x + z), w) + y)) ||
 rewrite(min(((x + y) + z), (w + (u + x))), (min((y + z), (w + u)) + x)) ||
 rewrite(min(((x + y) + z), (w + (u + y))), (min((x + z), (w + u)) + y)) ||
 rewrite(min(((x + y) + z), (w + (x + u))), (min((y + z), (w + u)) + x)) ||
 rewrite(min(((x + y) + z), (w + (y + u))), (min((x + z), (w + u)) + y)) ||
 rewrite(min(((x + y) + z), (x + w)), (min((y + z), w) + x)) ||
 rewrite(min(((x + y) + z), (y + w)), (min((x + z), w) + y)) ||
 rewrite(min(((x + y) + z), ((w + x) + u)), (min((y + z), (w + u)) + x)) ||
 rewrite(min(((x + y) + z), ((w + y) + u)), (min((x + z), (w + u)) + y)) ||
 rewrite(min(((x + y) + z), ((x + w) + u)), (min((y + z), (u + w)) + x)) ||
 rewrite(min(((x + y) + z), ((y + w) + u)), (min((x + z), (u + w)) + y)) ||
 rewrite(min(((x + (y + z)) + w), (u + (z + y))), (min((x + w), u) + (z + y))) ||
 rewrite(min(((x + (y + z)) + w), ((z + y) + u)), (min(u, (x + w)) + (z + y))) ||
 rewrite(min(((x + (y*z)) + w), (u + (z*y))), (min((x + w), u) + (z*y))) ||
 rewrite(min(((x + (y*z)) + w), ((z*y) + u)), (min((x + w), u) + (z*y))) ||
 rewrite(min((((x + y) + z) + w), (u + (y + x))), (min((z + w), u) + (y + x))) ||
 rewrite(min((((x + y) + z) + w), ((y + x) + u)), (min((w + z), u) + (y + x))) ||
 rewrite(min((((x*y) + z) + w), (u + (y*x))), (min((z + w), u) + (y*x))) ||
 rewrite(min((((x*y) + z) + w), ((y*x) + u)), (min((w + z), u) + (y*x))) ||
 rewrite(min((((x + y) - z) + w), (u + (x - v))), (min(((w + y) - z), (u - v)) + x)) ||
 rewrite(min((((x + y) - z) + w), (u + (y - v))), (min(((x + w) - z), (u - v)) + y)) ||
 rewrite(min((((x + y) - z) + w), ((x - u) + v)), (min(((w + y) - z), (v - u)) + x)) ||
 rewrite(min((((x + y) - z) + w), ((y - u) + v)), (min(((x + w) - z), (v - u)) + y)) ||
 rewrite(min(((x*y) + z), (w + (u + (y*x)))), (min(z, (w + u)) + (y*x))) ||
 rewrite(min(((x*y) + z), (w + ((y*x) + u))), (min((w + u), z) + (y*x))) ||
 rewrite(min(((x*y) + z), ((w + (y*x)) + u)), (min((u + w), z) + (y*x))) ||
 rewrite(min(((x*y) + z), (((y*x) + w) + u)), (min((u + w), z) + (x*y))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(((x + y) - z), (w + (u + (x + y)))), (min((x - z), (w + (u + x))) + y)) ||
 rewrite(min(((x + y) - z), (w + (u + (y + x)))), (min((y - z), (w + (u + y))) + x)) ||
 rewrite(min(((x + y) - z), (w + ((x + y) + u))), (min((x - z), (w + (x + u))) + y)) ||
 rewrite(min(((x + y) - z), (w + ((y + x) + u))), (min((x - z), (w + (u + x))) + y)) ||
 rewrite(min(((x + y) - z), (w + ((x + y) - u))), (min(((w + y) - u), (y - z)) + x)) ||
 rewrite(min(((x + y) - z), (w + ((y + x) - u))), (min(((w + y) - u), (y - z)) + x)) ||
 rewrite(min(((x + y) - z), ((w + (x + y)) + u)), (min((x - z), (w + (x + u))) + y)) ||
 rewrite(min(((x + y) - z), ((w + (y + x)) + u)), (min((x - z), ((w + u) + x)) + y)) ||
 rewrite(min(((x + y) - z), (((x + y) + w) + u)), (min((y - z), ((u + y) + w)) + x)) ||
 rewrite(min(((x + y) - z), (((y + x) + w) + u)), (min((x - z), ((u + x) + w)) + y)) ||
 rewrite(min(((x + y) - z), (((x + y) - w) + u)), (min((x - z), ((x + u) - w)) + y)) ||
 rewrite(min(((x + y) - z), (((y + x) - w) + u)), (min((y - z), ((y + u) - w)) + x)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(((x - y) - z), (x - w)), (x - max(w, (z + y)))) ||
 rewrite(min((((x + y) - z) - w), (x - u)), (x - max((z - (y - w)), u))) ||
 rewrite(min((((x + y) - z) - w), (y - u)), (y - max(((w + z) - x), u))))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((((x + c0)/c1)*c1), (x + c2)), (x + c2), (((c2 + -1) <= (c0 + c1)) && (((c1 + c2) + -1) <= c0))) ||
 rewrite(min((((x + c0)/c1)*c1), (x + c2)), (x + c2), prove_me(((((((((c2 + 12297) <= (((c0 + 12297)/c1)*c1)) && ((c2 + 12296) <= (((c0 + 12296)/c1)*c1))) && ((c2 + 12294) <= (((c0 + 12294)/c1)*c1))) && ((c2 + 1843) <= (((c0 + 1843)/c1)*c1))) && ((c2 + 4) <= (((c0 + 4)/c1)*c1))) && ((c2 + 1) <= (((c0 + 1)/c1)*c1))) && (c2 <= ((c0/c1)*c1))))))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite(min(min(x, (y + c0)), y), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, (min(y, z) + c0)), y), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(y, z) + c0)), z), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(y, min(x, z)) + c0)), z), min(min(x, (min(y, x) + c0)), z), (0 <= c0)) ||
 rewrite(min(min(x, (min(y, min(z, x)) + c0)), z), min(min(x, (min(y, x) + c0)), z), (0 <= c0)) ||
 rewrite(min(min(x, (min(min(x, y), z) + c0)), y), min(min(x, (min(x, z) + c0)), y), (0 <= c0)) ||
 rewrite(min(min(x, (min(min(y, x), z) + c0)), y), min(min(x, (min(x, z) + c0)), y), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), y), min(x, min(y, z))) ||
 rewrite(min(min(x, min(y, z)), z), min(x, min(y, z))) ||
 rewrite(min(min(x, min(y, (min(z, w) + c0))), w), min(min(x, min(y, (z + c0))), w), (0 <= c0)) ||
 rewrite(min(min(x, min(y, (min(z, w) + c0))), z), min(min(x, min(y, (w + c0))), z), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, min(w, min(u, v))))), u), min(x, min(y, min(z, min(w, min(u, v)))))) ||
 rewrite(min(min(x, min(y, min(z, min(w, min(u, v))))), v), min(x, min(y, min(z, min(w, min(u, v)))))) ||
 rewrite(min(min(x, min(y, min(z, min(min(w, u), v)))), u), min(x, min(y, min(z, min(min(w, u), v))))) ||
 rewrite(min(min(x, min(y, min(z, min(min(w, u), v)))), w), min(x, min(y, min(z, min(min(w, u), v))))) ||
 rewrite(min(min(x, min(y, min(min(z, min(w, u)), v))), u), min(x, min(y, min(min(z, min(w, u)), v)))) ||
 rewrite(min(min(x, min(y, min(min(z, min(w, u)), v))), w), min(x, min(y, min(min(z, min(w, u)), v)))) ||
 rewrite(min(min(x, min(y, min(min(min(z, w), u), v))), w), min(x, min(y, min(min(min(z, w), u), v)))) ||
 rewrite(min(min(x, min(y, min(min(min(z, w), u), v))), z), min(x, min(y, min(min(min(z, w), u), v)))) ||
 rewrite(min(min(x, min((min(y, z) + c0), w)), y), min(min(x, min((z + c0), w)), y), (0 <= c0)) ||
 rewrite(min(min(x, min((min(y, z) + c0), w)), z), min(min(x, min((y + c0), w)), z), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, min(z, min(w, u))), v)), u), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(min(x, min(min(y, min(z, min(w, u))), v)), w), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(min(x, min(min(y, min(min(z, w), u)), v)), w), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(min(x, min(min(y, min(min(z, w), u)), v)), z), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(min(x, min(min(min(y, min(z, w)), u), v)), w), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(min(x, min(min(min(y, min(z, w)), u), v)), z), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(min(x, min(min(min(min(y, z), w), u), v)), y), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(min(x, min(min(min(min(y, z), w), u), v)), z), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(min((x + c0), y), x), min(x, y), (0 <= c0)) ||
 rewrite(min(min((min(x, y) + c0), z), x), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(min((min(x, y) + c0), z), y), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(min((min(x, min(y, z)) + c0), y), z), min(min((min(x, y) + c0), y), z), (0 <= c0)) ||
 rewrite(min(min((min(x, min(y, z)) + c0), z), y), min(min((min(x, z) + c0), z), y), (0 <= c0)) ||
 rewrite(min(min((min(min(x, y), z) + c0), x), y), min(min((min(x, z) + c0), x), y), (0 <= c0)) ||
 rewrite(min(min((min(min(x, y), z) + c0), y), x), min(min((min(y, z) + c0), y), x), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), x), min(min(y, z), x)) ||
 rewrite(min(min(min(x, y), z), y), min(min(x, z), y)) ||
 rewrite(min(min(min(x, (min(y, z) + c0)), w), y), min(min(min(x, (z + c0)), w), y), (0 <= c0)) ||
 rewrite(min(min(min(x, (min(y, z) + c0)), w), z), min(min(min(x, (y + c0)), w), z), (0 <= c0)) ||
 rewrite(min(min(min(x, (y/c0)), (z/c0)), w), min(min((min(z, y)/c0), x), w), prove_me((((0 <= (1/c0)) && (-2 <= (7/c0))) && ((-2/c0) <= 0)))) ||
 rewrite(min(min(min(x, (y/c0)), ((z + w)/c0)), u), min(min(x, (min(y, (z + w))/c0)), u), prove_me(((0 <= (1/c0)) && ((-6/c0) <= 0)))) ||
 rewrite(min(min(min(x, (y/c0)), ((z + w)/c0)), u), min(min(x, (min(y, (z + w))/c0)), u), prove_me((((((0 <= (1/c0)) && (min((-7/c0), 3) <= (-2/c0))) && (min((8/c0), -2) <= (24/c0))) && (min((-12357/c0), 1374) <= (-12348/c0))) && ((-9864/c0) <= 0)))) ||
 rewrite(min(min(min(x, min(y, min(z, min(w, u)))), v), u), min(min(min(x, min(y, min(z, w))), v), u)) ||
 rewrite(min(min(min(x, min(y, min(z, min(w, u)))), v), w), min(min(min(x, min(y, min(z, u))), v), w)) ||
 rewrite(min(min(min(x, min(y, min(min(z, w), u))), v), w), min(min(min(x, min(y, min(z, u))), v), w)) ||
 rewrite(min(min(min(x, min(y, min(min(z, w), u))), v), z), min(min(min(x, min(y, min(w, u))), v), z)) ||
 rewrite(min(min(min(x, min(min(y, min(z, w)), u)), v), w), min(min(min(x, min(min(y, z), u)), v), w)) ||
 rewrite(min(min(min(x, min(min(y, min(z, w)), u)), v), z), min(min(min(x, min(min(y, w), u)), v), z)) ||
 rewrite(min(min(min(x, min(min(min(y, z), w), u)), v), y), min(min(min(x, min(min(z, w), u)), v), y)) ||
 rewrite(min(min(min(x, min(min(min(y, z), w), u)), v), z), min(min(min(x, min(min(y, w), u)), v), z)) ||
 rewrite(min(min(min((min(x, y) + c0), z), w), x), min(min(min((y + c0), z), w), x), (0 <= c0)) ||
 rewrite(min(min(min((min(x, y) + c0), z), w), y), min(min(min((x + c0), z), w), y), (0 <= c0)) ||
 rewrite(min(min(min((x/c0), y), (z/c0)), w), min(min((min(z, x)/c0), y), w), prove_me((((0 <= (1/c0)) && (-2 <= (7/c0))) && ((-2/c0) <= 0)))) ||
 rewrite(min(min(min((x/c0), y), ((z + w)/c0)), u), min(min((min(x, (z + w))/c0), y), u), prove_me(((0 <= (1/c0)) && ((-6/c0) <= 0)))) ||
 rewrite(min(min(min((x/c0), y), ((z + w)/c0)), u), min(min((min(x, (z + w))/c0), y), u), prove_me((((((0 <= (1/c0)) && (min((-7/c0), 3) <= (-2/c0))) && (min((8/c0), -2) <= (24/c0))) && (min((-12357/c0), 1374) <= (-12348/c0))) && ((-9864/c0) <= 0)))) ||
 rewrite(min(min(min((x/c0), (y + z)), (w/c0)), u), min(min((min(w, x)/c0), (y + z)), u), prove_me(((((0 <= (1/c0)) && (-1 <= (5/c0))) && (min((-28599/c0), 9532) <= (-28593/c0))) && ((-57198/c0) <= 0)))) ||
 rewrite(min(min(min(min(x, min(y, min(z, w))), u), v), w), min(min(min(min(x, min(y, z)), u), v), w)) ||
 rewrite(min(min(min(min(x, min(y, min(z, w))), u), v), z), min(min(min(min(x, min(y, w)), u), v), z)) ||
 rewrite(min(min(min(min(x, min(min(y, z), w)), u), v), y), min(min(min(min(x, min(z, w)), u), v), y)) ||
 rewrite(min(min(min(min(x, min(min(y, z), w)), u), v), z), min(min(min(min(x, min(y, w)), u), v), z)) ||
 rewrite(min(min(min(min(min(x, min(y, z)), w), u), v), y), min(min(min(min(min(x, z), w), u), v), y)) ||
 rewrite(min(min(min(min(min(x, min(y, z)), w), u), v), z), min(min(min(min(min(x, y), w), u), v), z)) ||
 rewrite(min(min(min(min(min(min(x, y), z), w), u), v), x), min(min(min(min(min(y, z), w), u), v), x)) ||
 rewrite(min(min(min(min(min(min(x, y), z), w), u), v), y), min(min(min(min(min(x, z), w), u), v), y)) ||
 rewrite(min(min(select(x, y, (z/c0)), (w + c1)), w), min(select(x, y, (z/c0)), w), (0 <= c1)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(min(x, y), (x + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, y), (x + c0)), min(y, (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (y + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, y), (y + c0)), min(x, (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(x, z) + c0)), min(y, (min(x, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(x, (z + (w*c0))) + c1)), min(y, (min(x, (z + (w*c0))) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(x, ((z*c0) + w)) + c1)), min(y, (min(x, ((z*c0) + w)) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(y, z) + c0)), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(y, (z + (w*c0))) + c1)), min(x, (min(y, (z + (w*c0))) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(y, ((z*c0) + w)) + c1)), min(x, (min(y, ((z*c0) + w)) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(z, x) + c0)), min(y, (min(z, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(z, y) + c0)), min(x, (min(z, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min((z + (w*c0)), x) + c1)), min(y, (min((z + (w*c0)), x) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min((z + (w*c0)), y) + c1)), min(x, (min((z + (w*c0)), y) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(((z*c0) + w), x) + c1)), min(y, (min(((z*c0) + w), x) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(((z*c0) + w), y) + c1)), min(x, (min(((z*c0) + w), y) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, (y + z)), (min(w, (z + y)) + c0)), min(x, (min(w, (z + y)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (y + z)), (min((z + y), w) + c0)), min(x, (min((z + y), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (y + (z + w))), (u + (v + w))), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (v + z))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (w + v))), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (z + v))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + w) + v)), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + z) + v)), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((w + u) + v)), min(x, (min((z + y), (v + u)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((z + u) + v)), min(x, (min((y + w), (v + u)) + z))) ||
 rewrite(min(min(x, (y + (z - w))), (u + (v - w))), min(x, (min((z + y), (u + v)) - w))) ||
 rewrite(min(min(x, (y + (z - w))), ((u - w) + v)), min(x, (min((z + y), (u + v)) - w))) ||
 rewrite(min(min(x, (y + (z*c0))), (min(w, x) + c1)), min((y + (z*c0)), (min(w, x) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, (y + (z*c0))), (min(x, w) + c1)), min((y + (z*c0)), (min(x, w) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (v + y))), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (v + z))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (y + v))), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (z + v))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + y) + v)), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + z) + v)), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((y + u) + v)), min(x, (min((z + w), (v + u)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z + u) + v)), min(x, (min((y + w), (v + u)) + z))) ||
 rewrite(min(min(x, ((y - z) + w)), (u + (v - z))), min(x, (min((y + w), (u + v)) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), ((u - z) + v)), min(x, (min((y + w), (u + v)) - z))) ||
 rewrite(min(min(x, ((y*c0) + z)), (min(w, x) + c1)), min(((y*c0) + z), (min(w, x) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, ((y*c0) + z)), (min(x, w) + c1)), min(((y*c0) + z), (min(x, w) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (y + c0)), min(x, min(z, y)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (y + c0)), min(min(x, z), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (z + c0)), min(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (z + c0)), min(min(x, y), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + w)), min(min(x, (min(z, u) + w)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + z)), min(min(x, (min(w, u) + z)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (w + u)), min(min(x, (min(z, u) + w)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (z + u)), min(min(x, (min(w, u) + z)), y)) ||
 rewrite(min(min(x, min(y, (z*w))), ((w*z) + c0)), min(min(x, y), ((w*z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (w + c0)), min(min(x, min(y, z)), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (z + c0)), min(min(x, min(y, w)), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(w, z) + c0)), min(min(x, min(y, w)), (min(w, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (u + v)), min(min(x, min(y, (min(w, v) + u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + u)), min(min(x, min(y, (min(w, v) + u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + w)), min(min(x, min(y, (min(u, v) + w))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (w + v)), min(min(x, min(y, (min(u, v) + w))), z)) ||
 rewrite(min(min(x, min(y, min(z, min(w, u)))), (u + c0)), min(min(x, min(y, min(z, w))), (u + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, min(w, u)))), (w + c0)), min(min(x, min(y, min(z, u))), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + w)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + z)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (w + v)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (z + v)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min(y, min(min(z, w), u))), (w + c0)), min(min(x, min(y, min(z, u))), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(min(z, w), u))), (z + c0)), min(min(x, min(y, min(w, u))), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min((y + z), w)), (u + y)), min(min(x, w), (min(z, u) + y))) ||
 rewrite(min(min(x, min((y + z), w)), (u + z)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (y + u)), min(min(x, (min(u, z) + y)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (z + u)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y*z), w)), ((z*y) + c0)), min(min(x, w), ((z*y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (y + c0)), min(min(x, min(z, w)), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (z + c0)), min(min(x, min(y, w)), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(z, y) + c0)), min(min(x, min(z, w)), (min(z, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + w)), min(min(x, min((min(z, v) + w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + z)), min(min(x, min((min(w, v) + z), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (w + v)), min(min(x, min((min(z, v) + w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (z + v)), min(min(x, min((min(w, v) + z), u)), y)) ||
 rewrite(min(min(x, min(min(y, min(z, w)), u)), (w + c0)), min(min(x, min(min(y, z), u)), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, min(z, w)), u)), (z + c0)), min(min(x, min(min(y, w), u)), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + y)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + z)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (y + v)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (z + v)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min(x, min(min(min(y, z), w), u)), (y + c0)), min(min(x, min(min(z, w), u)), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(min(y, z), w), u)), (z + c0)), min(min(x, min(min(y, w), u)), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min((x + y), z), (min(w, (y + x)) + c0)), min(z, (min(w, (y + x)) + c0)), (c0 <= 0)) ||
 rewrite(min(min((x + y), z), (min((y + x), w) + c0)), min(z, (min((y + x), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min((x + (y + z)), w), (u + (v + y))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (v + z))), min((min((x + y), (u + v)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (y + v))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (z + v))), min((min((x + y), (u + v)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), ((u + y) + v)), min(w, (min((x + z), (u + v)) + y))) ||
 rewrite(min(min((x + (y + z)), w), ((u + z) + v)), min(w, (min((y + x), (u + v)) + z))) ||
 rewrite(min(min((x + (y + z)), w), ((y + u) + v)), min(w, (min((x + z), (v + u)) + y))) ||
 rewrite(min(min((x + (y + z)), w), ((z + u) + v)), min(w, (min((y + x), (v + u)) + z))) ||
 rewrite(min(min((x + (y - z)), w), (u + (v - z))), min((min((x + y), (u + v)) - z), w)) ||
 rewrite(min(min((x + (y - z)), w), ((u - z) + v)), min((min((x + y), (v + u)) - z), w)) ||
 rewrite(min(min((x + (y*c0)), z), (min(w, z) + c1)), min((x + (y*c0)), (min(w, z) + c1)), (c1 <= 0)) ||
 rewrite(min(min((x + (y*c0)), z), (min(z, w) + c1)), min((x + (y*c0)), (min(z, w) + c1)), (c1 <= 0)) ||
 rewrite(min(min(((x + y) + z), w), (u + (v + x))), min((min((y + z), (u + v)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (v + y))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (x + v))), min((min((y + z), (u + v)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (y + v))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), ((u + x) + v)), min(w, (min((y + z), (u + v)) + x))) ||
 rewrite(min(min(((x + y) + z), w), ((u + y) + v)), min(w, (min((x + z), (u + v)) + y))) ||
 rewrite(min(min(((x + y) + z), w), ((x + u) + v)), min(w, (min((y + z), (v + u)) + x))) ||
 rewrite(min(min(((x + y) + z), w), ((y + u) + v)), min(w, (min((x + z), (v + u)) + y))) ||
 rewrite(min(min(((x - y) + z), w), (u + (v - y))), min((min((x + z), (u + v)) - y), w)) ||
 rewrite(min(min(((x - y) + z), w), ((u - y) + v)), min((min((x + z), (v + u)) - y), w)) ||
 rewrite(min(min(((x*c0) + y), z), (min(w, z) + c1)), min(((x*c0) + y), (min(w, z) + c1)), (c1 <= 0)) ||
 rewrite(min(min(((x*c0) + y), z), (min(z, w) + c1)), min(((x*c0) + y), (min(z, w) + c1)), (c1 <= 0)) ||
 rewrite(min(min(min(x, y), z), (x + c0)), min(min(y, z), x), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (x + c0)), min(min(y, z), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (y + c0)), min(min(x, z), y), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (y + c0)), min(min(x, z), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + y)), min(min((min(z, u) + y), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + z)), min(min((min(y, u) + z), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (y + u)), min(min((min(z, u) + y), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (z + u)), min(min((min(y, u) + z), w), x)) ||
 rewrite(min(min(min(x, (y*z)), w), ((z*y) + c0)), min(min(x, w), ((z*y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (y + c0)), min(min(min(x, z), w), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (z + c0)), min(min(min(x, y), w), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(z, y) + c0)), min(min(min(x, y), w), (min(z, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + w)), min(min(min(x, (min(z, v) + w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + z)), min(min(min(x, (min(w, v) + z)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (w + v)), min(min(min(x, (min(z, v) + w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (z + v)), min(min(min(x, (min(w, v) + z)), u), y)) ||
 rewrite(min(min(min(x, min(y, min(z, w))), u), (w + c0)), min(min(min(x, min(y, z)), u), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, min(z, w))), u), (z + c0)), min(min(min(x, min(y, w)), u), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + y)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + z)), min(min(min((min(v, y) + z), w), u), x)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (y + v)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (z + v)), min(min(min((min(v, y) + z), w), u), x)) ||
 rewrite(min(min(min(x, min(min(y, z), w)), u), (y + c0)), min(min(min(x, min(z, w)), u), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(min(y, z), w)), u), (z + c0)), min(min(min(x, min(y, w)), u), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(min((x + y), z), w), (u + x)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (u + y)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (x + u)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (y + u)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x*y), z), w), ((y*x) + c0)), min(min(z, w), ((y*x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (x + c0)), min(min(min(y, z), w), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (y + c0)), min(min(min(x, z), w), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(y, x) + c0)), min(min(min(y, z), w), (min(y, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + y)), min(min(min((min(z, v) + y), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + z)), min(min(min((min(y, v) + z), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (y + v)), min(min(min((min(z, v) + y), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (z + v)), min(min(min((min(y, v) + z), w), u), x)) ||
 rewrite(min(min(min(min(x, min(y, z)), w), u), (y + c0)), min(min(min(min(x, z), w), u), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, min(y, z)), w), u), (z + c0)), min(min(min(min(x, y), w), u), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + x)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + y)), min(min(min((min(x, v) + y), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (x + v)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (y + v)), min(min(min((min(x, v) + y), w), u), z)) ||
 rewrite(min(min(min(min(min(x, y), z), w), u), (x + c0)), min(min(min(min(y, z), w), u), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(min(x, y), z), w), u), (y + c0)), min(min(min(min(x, z), w), u), (y + c0)), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(min(x, (y + (z + w))), ((u + w) - v)), min(x, (min((u - v), (z + y)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + z) - v)), min(x, (min((u - v), (w + y)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((w + u) - v)), min(x, (min((u - v), (z + y)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((z + u) - v)), min(x, (min((u - v), (y + w)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + y) - v)), min((min((u - v), (z + w)) + y), x)) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + z) - v)), min((min((u - v), (y + w)) + z), x)) ||
 rewrite(min(min(x, ((y + z) + w)), ((y + u) - v)), min(x, (min((u - v), (z + w)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z + u) - v)), min(x, (min((u - v), (y + w)) + z))) ||
 rewrite(min(min(x, (y - z)), (w - z)), min((min(y, w) - z), x)) ||
 rewrite(min(min(x, (y - z)), (y - w)), min(x, (y - max(z, w)))) ||
 rewrite(min(min(x, ((y + z) - w)), ((z + y) - u)), min(x, ((y + z) - max(w, u)))) ||
 rewrite(min(min(x, min(y, (z - w))), (z - u)), min(x, min(y, (z - max(w, u))))) ||
 rewrite(min(min(x, min(y, min(z, (w - u)))), (w - v)), min(x, min(y, min(z, (w - max(u, v)))))) ||
 rewrite(min(min(x, min(y, min((z - w), u))), (z - v)), min(x, min(y, min((z - max(w, v)), u)))) ||
 rewrite(min(min(x, min((y - z), w)), (y - u)), min(x, min((y - max(z, u)), w))) ||
 rewrite(min(min(x, min(min(y, (z - w)), u)), (z - v)), min(x, min(min(y, (z - max(w, v))), u))) ||
 rewrite(min(min(x, min(min((y - z), w), u)), (y - v)), min(x, min(min((y - max(z, v)), u), w))) ||
 rewrite(min(min((x + (y + z)), w), ((u + y) - v)), min((min((u - v), (z + x)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), ((u + z) - v)), min((min((u - v), (y + x)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), ((y + u) - v)), min(w, (y + min((u - v), (x + z))))) ||
 rewrite(min(min((x + (y + z)), w), ((z + u) - v)), min(w, (z + min((u - v), (y + x))))) ||
 rewrite(min(min(((x + y) + z), w), ((u + x) - v)), min((min((u - v), (y + z)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), ((u + y) - v)), min((min((u - v), (x + z)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), ((x + u) - v)), min(w, (x + min((u - v), (y + z))))) ||
 rewrite(min(min(((x + y) + z), w), ((y + u) - v)), min((min((u - v), (x + z)) + y), w)) ||
 rewrite(min(min((x - y), z), (w - y)), min(z, (min(x, w) - y))) ||
 rewrite(min(min((x - y), z), (x - w)), min(z, (x - max(y, w)))) ||
 rewrite(min(min(((x + y) - z), w), ((y + x) - u)), min(w, ((x + y) - max(z, u)))) ||
 rewrite(min(min(min(x, (y - z)), w), (y - u)), min(w, min(x, (y - max(z, u))))) ||
 rewrite(min(min(min(x, min(y, (z - w))), u), (z - v)), min(u, min(x, min(y, (z - max(w, v)))))) ||
 rewrite(min(min(min(x, min((y - z), w)), u), (y - v)), min(w, min(x, min((y - max(z, v)), u)))) ||
 rewrite(min(min(min((x - y), z), w), (x - u)), min(w, min((x - max(y, u)), z))) ||
 rewrite(min(min(min(min(x, (y - z)), w), u), (y - v)), min(min(min((y - max(v, z)), w), u), x)) ||
 rewrite(min(min(min(min((x - y), z), w), u), (x - v)), min(z, min(min((x - max(y, v)), u), w))))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(min(min(x, ((y*z) + c0)), (z*y)), min(x, (y*z)), (0 <= c0)) ||
 rewrite(min(min(((x*y) + c0), z), (y*x)), min(z, (y*x)), (0 <= c0)))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite(min(min(x, (y/c0)), (z/c0)), min((min(z, y)/c0), x), prove_me((((0 <= (1/c0)) && (-1 <= (4/c0))) && (min((-8/c0), 1) <= (3/c0))))) ||
 rewrite(min(min(x, (y/c0)), (z/c0)), min((min(z, y)/c0), x), prove_me((((0 <= (1/c0)) && (min((-6/c0), 2) <= (-1/c0))) && ((-7/c0) <= 0)))) ||
 rewrite(min(min(x, (y/c0)), ((z + c1)/c0)), min(x, (min(y, (z + c1))/c0)), prove_me(((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))))) ||
 rewrite(min(min(x, (y/c0)), ((z + w)/c0)), min(x, (min(y, (z + w))/c0)), prove_me(((0 <= (1/c0)) && ((-4/c0) <= 0)))) ||
 rewrite(min(min(x, (y/c0)), ((z + w)/c0)), min(x, (min(y, (z + w))/c0)), prove_me(((min((2/c0), 0) <= (4/c0)) && (0 <= min(min(min(min((1/c0), (5/c0)), (11/c0)), (23/c0)), (24/c0)))))) ||
 rewrite(min(min(x, (y/c0)), ((z + (w + u))/c0)), min(x, (min(y, (z + (w + u)))/c0)), prove_me(((((0 <= (1/c0)) && (min((-7/c0), 3) <= (-2/c0))) && (min((-17/c0), 2) <= (-7/c0))) && (min((-34/c0), 1) <= (16/c0))))) ||
 rewrite(min(min(x, (y/c0)), (((z + w) + c1)/c0)), min(x, (min(y, ((z + w) + c1))/c0)), prove_me((((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((-7/c0), ((c1 + -11)/c0)), 5) == min(((min(c1, 4) + -11)/c0), 5))) && (min((-1/c0), ((c1 + -12)/c0)) == min(((min(c1, 11) + -12)/c0), 3))))) ||
 rewrite(min(min(x, (y/c0)), (((z + w) + u)/c0)), min(x, (min(y, ((z + w) + u))/c0)), prove_me(((((0 <= (1/c0)) && (min((-7/c0), 3) <= (-2/c0))) && (min((-17/c0), 2) <= (-7/c0))) && (min((-34/c0), 1) <= (16/c0))))) ||
 rewrite(min(min(x, ((y + c0)/c1)), ((y + z)/c1)), min(x, ((y + min(z, c0))/c1)), prove_me(((((min((c0/c1), 0) == min((min(c0, 0)/c1), 0)) && (min((c0/c1), 0) == min((min(c0, 0)/c1), 1))) && (min(min(((c0 + 1)/c1), (1/c1)), 0) == min(((min(c0, 0) + 1)/c1), 0))) && (min(min((c0/c1), (1/c1)), 0) == min((min(c0, 1)/c1), 0))))) ||
 rewrite(min(min(x, ((y + c0)/c1)), ((z + y)/c1)), min(x, ((y + min(z, c0))/c1)), prove_me(((((min((c0/c1), 0) == min((min(c0, 0)/c1), 0)) && (min((c0/c1), 0) == min((min(c0, 0)/c1), 1))) && (min(min(((c0 + 1)/c1), (1/c1)), 0) == min(((min(c0, 0) + 1)/c1), 0))) && (min(min((c0/c1), (1/c1)), 0) == min((min(c0, 1)/c1), 0))))) ||
 rewrite(min(min(x, ((y + z)/c0)), (w/c0)), min((min(w, (y + z))/c0), x), prove_me(((0 <= (1/c0)) && ((-4/c0) <= 0)))) ||
 rewrite(min(min(x, ((y + z)/c0)), (w/c0)), min((min(w, (y + z))/c0), x), prove_me(((((0 <= (1/c0)) && (min((-1/c0), 0) <= (3/c0))) && (min((4/c0), -2) <= (12/c0))) && ((-7/c0) <= 0)))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((w + c1)/c0)), min(x, (min((y + z), (w + c1))/c0)), prove_me(((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((w + u)/c0)), min(x, (min((y + z), (w + u))/c0)), prove_me(((0 <= (1/c0)) && ((-3/c0) <= 0)))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((w + u)/c0)), min(x, (min((y + z), (w + u))/c0)), prove_me(((0 <= (1/c0)) && ((-1/c0) <= 0)))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((w + y)/c0)), min(x, ((y + min(z, w))/c0)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((w + z)/c0)), min(x, ((z + min(y, w))/c0)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((y + c1)/c0)), min(x, ((y + min(z, c1))/c0)), prove_me((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), ((c1 + 1)/c0)), 0) == min(((min(c1, 0) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(min((3/c0), ((c1 + -4)/c0)), 0) == min(((min(c1, 7) + -4)/c0), 0))))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((y + w)/c0)), min(x, ((y + min(z, w))/c0)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((z + c1)/c0)), min(x, ((z + min(y, c1))/c0)), prove_me((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), ((c1 + 1)/c0)), 0) == min(((min(c1, 0) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(min((3/c0), ((c1 + -4)/c0)), 0) == min(((min(c1, 7) + -4)/c0), 0))))) ||
 rewrite(min(min(x, ((y + z)/c0)), ((z + w)/c0)), min(x, ((z + min(y, w))/c0)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(x, ((y + (z + w))/c0)), (u/c0)), min((min(u, (y + (z + w)))/c0), x), prove_me(((0 <= (1/c0)) && ((-1/c0) <= 0)))) ||
 rewrite(min(min(x, ((y + (z + w))/c0)), (u/c0)), min((min(u, (y + (z + w)))/c0), x), prove_me((((0 <= (1/c0)) && (-1 <= (4/c0))) && ((-6/c0) <= 0)))) ||
 rewrite(min(min(x, ((y + min(z, c0))/c1)), (w/c1)), min((min(w, (y + min(z, c0)))/c1), x), prove_me((((((min((min(c0, 0)/c1), 0) <= (1/c1)) && ((min(c0, 0)/c1) <= 0)) && (min((min(c0, 1)/c1), 0) == min((min(c0, 0)/c1), 0))) && (min(((min(c0, 0) + 1)/c1), 0) == min(((min(c0, -1) + 1)/c1), 0))) && (min(((min(c0, 0) + -7)/c1), 4) <= (-4/c1))))) ||
 rewrite(min(min(x, (((y + z) + w)/c0)), (u/c0)), min((min(u, ((y + z) + w))/c0), x), prove_me(((0 <= (1/c0)) && ((-1/c0) <= 0)))) ||
 rewrite(min(min(x, (((y + z) + w)/c0)), (u/c0)), min((min(u, ((y + z) + w))/c0), x), prove_me((((0 <= (1/c0)) && (-1 <= (4/c0))) && ((-6/c0) <= 0)))) ||
 rewrite(min(min(x, ((min(y, c0) + z)/c1)), (w/c1)), min((min(w, (min(y, c0) + z))/c1), x), prove_me((((((min((min(c0, 0)/c1), 0) <= (1/c1)) && ((min(c0, 0)/c1) <= 0)) && (min((min(c0, 1)/c1), 0) == min((min(c0, 0)/c1), 0))) && (min(((min(c0, 0) + 1)/c1), 0) == min(((min(c0, -1) + 1)/c1), 0))) && (min(((min(c0, 0) + -7)/c1), 4) <= (-4/c1))))) ||
 rewrite(min(min(x, min(y, (z/c0))), (w/c0)), min(min(x, (min(z, w)/c0)), y), prove_me(((((((min((-15859/c0), -1) <= (44052/c0)) && (min((-22026/c0), 7340) <= (-22017/c0))) && ((-88110/c0) <= 0)) && (0 <= (1/c0))) && (-2 <= (88107/c0))) && (-3 <= (8/c0))))) ||
 rewrite(min(min(x, min(y, (z/c0))), ((w + c1)/c0)), min(x, min(y, (min(z, (w + c1))/c0))), prove_me((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))))) ||
 rewrite(min(min(x, min(y, (z/c0))), ((w + c1)/c0)), min(x, min(y, (min(z, (w + c1))/c0))), prove_me(((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min((1/c0), ((c1 + -1)/c0)) == min(((min(c1, 2) + -1)/c0), 1))))) ||
 rewrite(min(min(x, min(y, ((z + w)/c0))), (u/c0)), min(min(x, (min(u, (z + w))/c0)), y), prove_me((max((-2/c0), 0) <= (1/c0)))) ||
 rewrite(min(min(x, min((y/c0), z)), (w/c0)), min(min(x, (min(y, w)/c0)), z), prove_me(((((((min((-15859/c0), -1) <= (44052/c0)) && (min((-22026/c0), 7340) <= (-22017/c0))) && ((-88110/c0) <= 0)) && (0 <= (1/c0))) && (-2 <= (88107/c0))) && (-3 <= (8/c0))))) ||
 rewrite(min(min(x, min((y/c0), z)), ((w + c1)/c0)), min(min(x, z), (min(y, (w + c1))/c0)), prove_me((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))))) ||
 rewrite(min(min(x, min((y/c0), z)), ((w + c1)/c0)), min(min(x, z), (min(y, (w + c1))/c0)), prove_me(((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min((1/c0), ((c1 + -1)/c0)) == min(((min(c1, 2) + -1)/c0), 1))))) ||
 rewrite(min(min(x, min((y/c0), (z + c1))), (w/c0)), min(min((min(w, y)/c0), (z + c1)), x), prove_me((((min(c1, 0) <= (1/c0)) && (min(min(min((1/c0), (c1 + 3)), (-1/c0)), 0) == min(min((-1/c0), (c1 + 3)), 0))) && (min(min(min((2357/c0), (c1 + -2350)), (2356/c0)), -2354) == min(min((2356/c0), (c1 + -2350)), -2354))))) ||
 rewrite(min(min(x, min((y/c0), (z + w))), (u/c0)), min(min((min(u, y)/c0), (z + w)), x), prove_me(((0 <= (1/c0)) && ((-4/c0) <= 0)))) ||
 rewrite(min(min(x, min(((y + z)/c0), w)), (u/c0)), min(min(x, (min((y + z), u)/c0)), w), prove_me((max((-2/c0), 0) <= (1/c0)))) ||
 rewrite(min(min((x/c0), y), (z/c0)), min(y, (min(z, x)/c0)), prove_me((((0 <= (1/c0)) && (-1 <= (4/c0))) && (min((-8/c0), 1) <= (3/c0))))) ||
 rewrite(min(min((x/c0), y), (z/c0)), min(y, (min(z, x)/c0)), prove_me((((0 <= (1/c0)) && (min((-6/c0), 2) <= (-1/c0))) && ((-7/c0) <= 0)))) ||
 rewrite(min(min((x/c0), y), ((z + c1)/c0)), min(y, (min(x, (z + c1))/c0)), prove_me(((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))))) ||
 rewrite(min(min((x/c0), y), ((z + w)/c0)), min(y, (min(x, (z + w))/c0)), prove_me(((0 <= (1/c0)) && ((-4/c0) <= 0)))) ||
 rewrite(min(min((x/c0), y), ((z + w)/c0)), min(y, (min(x, (z + w))/c0)), prove_me(((min((2/c0), 0) <= (4/c0)) && (0 <= min(min(min(min((1/c0), (5/c0)), (11/c0)), (23/c0)), (24/c0)))))) ||
 rewrite(min(min((x/c0), y), ((z + (w + u))/c0)), min(y, (min(x, (z + (w + u)))/c0)), prove_me(((((0 <= (1/c0)) && (min((-7/c0), 3) <= (-2/c0))) && (min((-17/c0), 2) <= (-7/c0))) && (min((-34/c0), 1) <= (16/c0))))) ||
 rewrite(min(min((x/c0), y), (((z + w) + c1)/c0)), min(y, (min(x, ((z + w) + c1))/c0)), prove_me((((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((-7/c0), ((c1 + -11)/c0)), 5) == min(((min(c1, 4) + -11)/c0), 5))) && (min((-1/c0), ((c1 + -12)/c0)) == min(((min(c1, 11) + -12)/c0), 3))))) ||
 rewrite(min(min((x/c0), y), (((z + w) + u)/c0)), min(y, (min(x, ((z + w) + u))/c0)), prove_me(((((0 <= (1/c0)) && (min((-7/c0), 3) <= (-2/c0))) && (min((-17/c0), 2) <= (-7/c0))) && (min((-34/c0), 1) <= (16/c0))))) ||
 rewrite(min(min((x/c0), (y + z)), ((w + u)/c0)), min((min(x, (u + w))/c0), (y + z)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(((x + c0)/c1), y), ((x + z)/c1)), min(y, ((x + min(z, c0))/c1)), prove_me(((((min((c0/c1), 0) == min((min(c0, 0)/c1), 0)) && (min((c0/c1), 0) == min((min(c0, 0)/c1), 1))) && (min(min(((c0 + 1)/c1), (1/c1)), 0) == min(((min(c0, 0) + 1)/c1), 0))) && (min(min((c0/c1), (1/c1)), 0) == min((min(c0, 1)/c1), 0))))) ||
 rewrite(min(min(((x + c0)/c1), y), ((z + x)/c1)), min(y, ((x + min(z, c0))/c1)), prove_me(((((min((c0/c1), 0) == min((min(c0, 0)/c1), 0)) && (min((c0/c1), 0) == min((min(c0, 0)/c1), 1))) && (min(min(((c0 + 1)/c1), (1/c1)), 0) == min(((min(c0, 0) + 1)/c1), 0))) && (min(min((c0/c1), (1/c1)), 0) == min((min(c0, 1)/c1), 0))))) ||
 rewrite(min(min(((x + y)/c0), z), (w/c0)), min(z, (min((x + y), w)/c0)), prove_me(((0 <= (1/c0)) && ((-4/c0) <= 0)))) ||
 rewrite(min(min(((x + y)/c0), z), (w/c0)), min(z, (min((x + y), w)/c0)), prove_me(((((0 <= (1/c0)) && (min((-1/c0), 0) <= (3/c0))) && (min((4/c0), -2) <= (12/c0))) && ((-7/c0) <= 0)))) ||
 rewrite(min(min(((x + y)/c0), z), ((w + c1)/c0)), min(z, (min((x + y), (w + c1))/c0)), prove_me(((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))))) ||
 rewrite(min(min(((x + y)/c0), z), ((w + u)/c0)), min(z, (min((x + y), (w + u))/c0)), prove_me(((0 <= (1/c0)) && ((-3/c0) <= 0)))) ||
 rewrite(min(min(((x + y)/c0), z), ((w + u)/c0)), min(z, (min((x + y), (w + u))/c0)), prove_me(((0 <= (1/c0)) && ((-1/c0) <= 0)))) ||
 rewrite(min(min(((x + y)/c0), z), ((w + x)/c0)), min(z, ((min(y, w) + x)/c0)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(((x + y)/c0), z), ((w + y)/c0)), min(z, ((min(x, w) + y)/c0)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(((x + y)/c0), z), ((x + c1)/c0)), min(z, ((x + min(y, c1))/c0)), prove_me((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), ((c1 + 1)/c0)), 0) == min(((min(c1, 0) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(min((3/c0), ((c1 + -4)/c0)), 0) == min(((min(c1, 7) + -4)/c0), 0))))) ||
 rewrite(min(min(((x + y)/c0), z), ((x + w)/c0)), min(z, ((x + min(y, w))/c0)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(((x + y)/c0), z), ((y + c1)/c0)), min(z, ((y + min(x, c1))/c0)), prove_me((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), ((c1 + 1)/c0)), 0) == min(((min(c1, 0) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(min((3/c0), ((c1 + -4)/c0)), 0) == min(((min(c1, 7) + -4)/c0), 0))))) ||
 rewrite(min(min(((x + y)/c0), z), ((y + w)/c0)), min(z, ((y + min(x, w))/c0)), prove_me((((0 <= (1/c0)) && (-2 <= (6/c0))) && (min((-6/c0), 1) <= (2/c0))))) ||
 rewrite(min(min(((x + (y + z))/c0), w), (u/c0)), min(w, (min((x + (y + z)), u)/c0)), prove_me(((0 <= (1/c0)) && ((-1/c0) <= 0)))) ||
 rewrite(min(min(((x + (y + z))/c0), w), (u/c0)), min(w, (min((x + (y + z)), u)/c0)), prove_me((((0 <= (1/c0)) && (-1 <= (4/c0))) && ((-6/c0) <= 0)))) ||
 rewrite(min(min(((x + min(y, c0))/c1), z), (w/c1)), min(z, (min((x + min(y, c0)), w)/c1)), prove_me((((((min((min(c0, 0)/c1), 0) <= (1/c1)) && ((min(c0, 0)/c1) <= 0)) && (min((min(c0, 1)/c1), 0) == min((min(c0, 0)/c1), 0))) && (min(((min(c0, 0) + 1)/c1), 0) == min(((min(c0, -1) + 1)/c1), 0))) && (min(((min(c0, 0) + -7)/c1), 4) <= (-4/c1))))) ||
 rewrite(min(min((((x + y) + z)/c0), w), (u/c0)), min(w, (min(((x + y) + z), u)/c0)), prove_me(((0 <= (1/c0)) && ((-1/c0) <= 0)))) ||
 rewrite(min(min((((x + y) + z)/c0), w), (u/c0)), min(w, (min(((x + y) + z), u)/c0)), prove_me((((0 <= (1/c0)) && (-1 <= (4/c0))) && ((-6/c0) <= 0)))) ||
 rewrite(min(min(((min(x, c0) + y)/c1), z), (w/c1)), min(z, (min((min(x, c0) + y), w)/c1)), prove_me((((((min((min(c0, 0)/c1), 0) <= (1/c1)) && ((min(c0, 0)/c1) <= 0)) && (min((min(c0, 1)/c1), 0) == min((min(c0, 0)/c1), 0))) && (min(((min(c0, 0) + 1)/c1), 0) == min(((min(c0, -1) + 1)/c1), 0))) && (min(((min(c0, 0) + -7)/c1), 4) <= (-4/c1))))) ||
 rewrite(min(min(min(x, y), (z/c0)), (w/c0)), min(min(x, y), (min(z, w)/c0)), prove_me(((((((min((-2607/c0), 868) <= (-2599/c0)) && ((-5216/c0) <= 0)) && (0 <= (31/c0))) && (0 <= (11/c0))) && (0 <= (1/c0))) && (-1 <= (5/c0))))) ||
 rewrite(min(min(min(x, y), (z/c0)), ((w + c1)/c0)), min(min(x, y), (min(z, (w + c1))/c0)), prove_me(((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(min((4/c0), ((c1 + 1)/c0)), 1) == min(((min(c1, 3) + 1)/c0), 1))) && (min(min((-10/c0), ((c1 + -3)/c0)), 3) == min(((min(c1, -7) + -3)/c0), 3))) && (min(min((-21/c0), ((c1 + -9)/c0)), 3) == min(((min(c1, -12) + -9)/c0), 3))))) ||
 rewrite(min(min(min(x, y), (z/c0)), ((w + u)/c0)), min(min(x, y), (min(z, (w + u))/c0)), prove_me(((((min((-9/c0), 2) <= (-1/c0)) && (min((-21/c0), 2) <= (-9/c0))) && (min((-51238/c0), 2330) <= (-51216/c0))) && (0 <= min(min((1/c0), (3/c0)), (21/c0)))))) ||
 rewrite(min(min(min(x, y), ((z + w)/c0)), (u/c0)), min(min((min(u, (z + w))/c0), x), y), prove_me(((((min((-7821/c0), 868) <= (-7803/c0)) && (min((-11840/c0), 5921) <= (-11838/c0))) && (min((-194241/c0), 8093) <= (-194208/c0))) && (0 <= min(min((1/c0), (5/c0)), (8/c0)))))) ||
 rewrite(min(min(min(x, (y + c0)), (z/c1)), (w/c1)), min(min(x, (y + c0)), (min(z, w)/c1)), prove_me((((min(c0, 0) <= (1/c1)) && (min((-450/c1), (min(c0, -1) + 450)) <= (5405/c1))) && (min((-451/c1), (min(c0, -5854) + 6305)) <= (-449/c1))))) ||
 rewrite(min(min(min(x, (y/c0)), z), (w/c0)), min(min((min(y, w)/c0), z), x), prove_me(((((((min((-15859/c0), -1) <= (44052/c0)) && (min((-22026/c0), 7340) <= (-22017/c0))) && ((-88110/c0) <= 0)) && (0 <= (1/c0))) && (-2 <= (88107/c0))) && (-3 <= (8/c0))))) ||
 rewrite(min(min(min(x, (y/c0)), z), ((w + c1)/c0)), min(z, min(x, (min(y, (w + c1))/c0))), prove_me((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))))) ||
 rewrite(min(min(min(x, (y/c0)), z), ((w + c1)/c0)), min(z, min(x, (min(y, (w + c1))/c0))), prove_me(((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min((1/c0), ((c1 + -1)/c0)) == min(((min(c1, 2) + -1)/c0), 1))))) ||
 rewrite(min(min(min(x, (y/c0)), (z/c0)), (w/c0)), min(min((min(z, w)/c0), x), (y/c0)), prove_me(((0 <= (1/c0)) && (min((-1/c0), (-4/c0)) <= 0)))) ||
 rewrite(min(min(min(x, ((y + z)/c0)), w), (u/c0)), min(min((min(u, (y + z))/c0), w), x), prove_me((max((-2/c0), 0) <= (1/c0)))) ||
 rewrite(min(min(min((x + c0), y), (z/c1)), (w/c1)), min(min((x + c0), y), (min(z, w)/c1)), prove_me((((min(c0, 0) <= (1/c1)) && (min((-450/c1), (min(c0, -1) + 450)) <= (5405/c1))) && (min((-451/c1), (min(c0, -5854) + 6305)) <= (-449/c1))))) ||
 rewrite(min(min(min((x/c0), y), z), (w/c0)), min(min((min(x, w)/c0), z), y), prove_me(((((((min((-15859/c0), -1) <= (44052/c0)) && (min((-22026/c0), 7340) <= (-22017/c0))) && ((-88110/c0) <= 0)) && (0 <= (1/c0))) && (-2 <= (88107/c0))) && (-3 <= (8/c0))))) ||
 rewrite(min(min(min((x/c0), y), z), ((w + c1)/c0)), min(min(y, z), (min(x, (w + c1))/c0)), prove_me((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))))) ||
 rewrite(min(min(min((x/c0), y), z), ((w + c1)/c0)), min(min(y, z), (min(x, (w + c1))/c0)), prove_me(((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min((1/c0), ((c1 + -1)/c0)) == min(((min(c1, 2) + -1)/c0), 1))))) ||
 rewrite(min(min(min((x/c0), y), (z/c0)), (w/c0)), min(min((x/c0), y), (min(z, w)/c0)), prove_me(((0 <= (1/c0)) && (min((-1/c0), (-4/c0)) <= 0)))) ||
 rewrite(min(min(min((x/c0), (y + c1)), z), (w/c0)), min(min((min(w, x)/c0), (y + c1)), z), prove_me((((min(c1, 0) <= (1/c0)) && (min(min(min((1/c0), (c1 + 3)), (-1/c0)), 0) == min(min((-1/c0), (c1 + 3)), 0))) && (min(min(min((2357/c0), (c1 + -2350)), (2356/c0)), -2354) == min(min((2356/c0), (c1 + -2350)), -2354))))) ||
 rewrite(min(min(min((x/c0), (y + z)), w), (u/c0)), min(min((min(u, x)/c0), (y + z)), w), prove_me(((0 <= (1/c0)) && ((-4/c0) <= 0)))) ||
 rewrite(min(min(min(((x + y)/c0), z), w), (u/c0)), min(min((min((x + y), u)/c0), w), z), prove_me((max((-2/c0), 0) <= (1/c0)))))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite(min(select(x, c0, c1), c2), select(x, c2, c1), ((c1 <= c2) && (c2 <= c0))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(select(x, y, z), min(w, select(x, u, v))), min(select(x, min(y, u), min(z, v)), w)) ||
 rewrite(min(select(x, y, z), min(select(x, w, u), v)), min(select(x, min(y, w), min(u, z)), v))))))
