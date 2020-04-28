((b.node_type() == IRNodeType::Add) && (
 rewrite((x + ((y*c0) + (z + (w*c1)))), (x + (z + ((w - y)*c1))), ((c0 + c1) == 0)) ||
 rewrite((x + ((y*c0) + ((z*c1) + w))), (x + (((z - y)*c1) + w)), ((c0 + c1) == 0)))) ||
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
 rewrite((x + (y - (z + min((w + x), u)))), (y - (z + min((u - x), w)))) ||
 rewrite((x + (y - (z + min((x + w), u)))), (y - (z + min((u - x), w)))) ||
 rewrite((x + (y - ((x + z) + w))), (y - (z + w))) ||
 rewrite((x + (y - ((z + x) + w))), (y - (z + w))) ||
 rewrite((x + (y - ((z + (w + x)) + u))), (y - (z + (w + u)))) ||
 rewrite((x + (y - ((z + (x + w)) + u))), (y - (z + (w + u)))) ||
 rewrite((x + (y - (((x + z) + w) + u))), (y - ((z + w) + u))) ||
 rewrite((x + (y - (((z + x) + w) + u))), (y - ((z + w) + u))) ||
 rewrite((x + (y - (min(z, (w + x)) + u))), (y - (min(w, (z - x)) + u))) ||
 rewrite((x + (y - (min(z, (x + w)) + u))), (y - (min(w, (z - x)) + u))) ||
 rewrite((x + (y - (min((x + z), w) + u))), (y - (min((w - x), z) + u))) ||
 rewrite((x + (y - (min((z + x), w) + u))), (y - (min((w - x), z) + u))) ||
 rewrite((x + (y - (((x + y)/c0)*c0))), ((x + y) % c0)) ||
 rewrite((x + (y - (((y + x)/c0)*c0))), ((y + x) % c0)) ||
 rewrite((x + (y - min(z, min(w, (u + (v + y)))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min(w, (u + (y + v)))))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite((x + (y - min(z, min(w, ((u + y) + v))))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite((x + (y - min(z, min(w, ((y + u) + v))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite((x + (y - min(z, min((w + (u + y)), v)))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(z, min((w + (y + u)), v)))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min(z, min(((w + y) + u), v)))), (x - min((min(v, z) - y), (u + w)))) ||
 rewrite((x + (y - min(z, min(((y + w) + u), v)))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min(min(z, (w + (u + y))), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, (w + (y + u))), v))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite((x + (y - min(min(z, ((w + y) + u)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min(z, ((y + w) + u)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite((x + (y - min(min((z + (w + y)), u), v))), (x - min((min(u, v) - y), (z + w)))) ||
 rewrite((x + (y - min(min((z + (y + w)), u), v))), (x - min((min(u, v) - y), (w + z)))) ||
 rewrite((x + (y - min(min(((y + z) + w), u), v))), (x - min((min(u, v) - y), (z + w)))) ||
 rewrite((x + (y - min(min(((z + y) + w), u), v))), (x - min((min(u, v) - y), (w + z)))) ||
 rewrite((x + (y - select(z, w, (u + (v + y))))), (x - select(z, (w - y), (v + u)))) ||
 rewrite((x + (y - select(z, w, (u + (y + v))))), (x - select(z, (w - y), (u + v)))) ||
 rewrite((x + (y - select(z, w, ((u + y) + v)))), (x - select(z, (w - y), (u + v)))) ||
 rewrite((x + (y - select(z, w, ((y + u) + v)))), (x - select(z, (w - y), (u + v)))) ||
 rewrite((x + (y - select(z, (w + (u + y)), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, (w + (y + u)), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, ((w + y) + u), v))), (x - select(z, (w + u), (v - y)))) ||
 rewrite((x + (y - select(z, ((y + w) + u), v))), (x - select(z, (u + w), (v - y)))) ||
 rewrite((x + ((y/c0) - ((y + z)/c0))), (x - (((y % c0) + z)/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((x + ((y/c0) - ((z + y)/c0))), (x - (((y % c0) + z)/c0)), prove_me((((1 % c0)/c0) == 0))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((x + max((y - max(z, max(w, (u + y)))), c0)), (x - min(max(u, (max(w, z) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(z, max(w, (y + u)))), c0)), (x - min(max(u, (max(w, z) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(z, max((w + y), u))), c0)), (x - min(max(w, (max(u, z) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(z, max((y + w), u))), c0)), (x - min(max((max(z, u) - y), w), fold((0 - c0))))) ||
 rewrite((x + max((y - max(max(z, (w + y)), u)), c0)), (x - min(max(w, (max(z, u) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(max(z, (y + w)), u)), c0)), (x - min(max(w, (max(z, u) - y)), fold((0 - c0))))) ||
 rewrite((x + max((y - max(max((y + z), w), u)), c0)), (x - min(max((max(w, u) - y), z), fold((0 - c0))))) ||
 rewrite((x + max((y - max(max((z + y), w), u)), c0)), (x - min(max((max(w, u) - y), z), fold((0 - c0))))))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(((x + (y - (z + select(w, c0, c1)))) + c2), (x + (y - (z - select(w, fold((c2 - c0)), fold((c2 - c1))))))) ||
 rewrite(((x + (y - (select(z, c0, c1) + w))) + c2), (x + (y + (select(z, fold((c2 - c0)), fold((c2 - c1))) - w)))) ||
 rewrite((((x - (y + select(z, c0, c1))) + w) + c2), (w + (x - (y - select(z, fold((c2 - c0)), fold((c2 - c1))))))) ||
 rewrite((((x - (select(y, c0, c1) + z)) + w) + c2), ((x + (select(y, fold((c2 - c0)), fold((c2 - c1))) + w)) - z)) ||
 rewrite((((x*c0) + (y + (z*c1))) + w), ((y + ((z - x)*c1)) + w), ((c0 + c1) == 0)) ||
 rewrite((((x*c0) + ((y*c1) + z)) + w), ((((y - x)*c1) + z) + w), ((c0 + c1) == 0)))) ||
((a.node_type() == IRNodeType::Sub) && (
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
 rewrite(((x - (y + min((z + w), u))) + w), (x - (y + min((u - w), z)))) ||
 rewrite(((x - (y + min((z + w), u))) + z), (x - (y + min((u - z), w)))) ||
 rewrite(((x - ((y + z) + w)) + y), (x - (z + w))) ||
 rewrite(((x - ((y + z) + w)) + z), (x - (y + w))) ||
 rewrite(((x - ((y + (z + w)) + u)) + w), (x - (y + (z + u)))) ||
 rewrite(((x - ((y + (z + w)) + u)) + z), (x - (y + (w + u)))) ||
 rewrite(((x - (((y + z) + w) + u)) + y), (x - ((z + w) + u))) ||
 rewrite(((x - (((y + z) + w) + u)) + z), (x - ((y + w) + u))) ||
 rewrite(((x - (min(y, (z + w)) + u)) + w), (x - (min(z, (y - w)) + u))) ||
 rewrite(((x - (min(y, (z + w)) + u)) + z), (x - (min(w, (y - z)) + u))) ||
 rewrite(((x - (min((y + z), w) + u)) + y), (x - (min((w - y), z) + u))) ||
 rewrite(((x - (min((y + z), w) + u)) + z), (x - (min((w - z), y) + u))) ||
 rewrite(((x - (((x + y)/c0)*c0)) + y), ((x + y) % c0)) ||
 rewrite(((x - (((y + x)/c0)*c0)) + y), ((y + x) % c0)) ||
 rewrite(((x - min(y, min(z, (w + (u + x))))) + v), (v - min((min(z, y) - x), (u + w)))) ||
 rewrite(((x - min(y, min(z, (w + (x + u))))) + v), (v - min((min(z, y) - x), (u + w)))) ||
 rewrite(((x - min(y, min(z, ((w + x) + u)))) + v), (v - min((min(z, y) - x), (u + w)))) ||
 rewrite(((x - min(y, min(z, ((x + w) + u)))) + v), (v - min((min(z, y) - x), (w + u)))) ||
 rewrite(((x - min(y, min((z + (w + x)), u))) + v), (v - min((min(u, y) - x), (w + z)))) ||
 rewrite(((x - min(y, min((z + (x + w)), u))) + v), (v - min((min(u, y) - x), (z + w)))) ||
 rewrite(((x - min(y, min(((x + z) + w), u))) + v), (v - min((min(u, y) - x), (w + z)))) ||
 rewrite(((x - min(y, min(((z + x) + w), u))) + v), (v - min((min(u, y) - x), (z + w)))) ||
 rewrite(((x - min(min(y, (z + (w + x))), u)) + v), (v - min((min(y, u) - x), (w + z)))) ||
 rewrite(((x - min(min(y, (z + (x + w))), u)) + v), (v - min((min(y, u) - x), (w + z)))) ||
 rewrite(((x - min(min(y, ((x + z) + w)), u)) + v), (v - min((min(y, u) - x), (z + w)))) ||
 rewrite(((x - min(min(y, ((z + x) + w)), u)) + v), (v - min((min(y, u) - x), (z + w)))) ||
 rewrite(((x - min(min((y + (x + z)), w), u)) + v), (v - min((min(w, u) - x), (y + z)))) ||
 rewrite(((x - min(min((y + (z + x)), w), u)) + v), (v - min((min(w, u) - x), (z + y)))) ||
 rewrite(((x - min(min(((x + y) + z), w), u)) + v), (v - min((min(w, u) - x), (z + y)))) ||
 rewrite(((x - min(min(((y + x) + z), w), u)) + v), (v - min((min(w, u) - x), (z + y)))) ||
 rewrite(((x - select(y, z, (w + (u + x)))) + v), (v - select(y, (z - x), (u + w)))) ||
 rewrite(((x - select(y, z, (w + (x + u)))) + v), (v - select(y, (z - x), (w + u)))) ||
 rewrite(((x - select(y, z, ((w + x) + u))) + v), (v - select(y, (z - x), (w + u)))) ||
 rewrite(((x - select(y, z, ((x + w) + u))) + v), (v - select(y, (z - x), (w + u)))) ||
 rewrite(((x - select(y, (z + (w + x)), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, (z + (x + w)), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, ((x + z) + w), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite(((x - select(y, ((z + x) + w), u)) + v), (v - select(y, (z + w), (u - x)))) ||
 rewrite((((x/c0) - ((x + y)/c0)) + z), (z - ((y + (x % c0))/c0)), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x/c0) - ((y + x)/c0)) + z), (z - ((y + (x % c0))/c0)), prove_me((((1 % c0)/c0) == 0))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x - y) + (y + z)), (x + z)) ||
 rewrite(((x - y) + (z + y)), (x + z)) ||
 rewrite(((x - (y + (z + (w + u)))) + (u + v)), ((x - (y + (z + w))) + v)) ||
 rewrite(((x - (y + (z + (w + u)))) + (v + u)), ((x - (y + (z + w))) + v)) ||
 rewrite(((x - (y + (z + (w + u)))) + (v + w)), ((x - (y + (z + u))) + v)) ||
 rewrite(((x - (y + (z + (w + u)))) + (w + v)), ((x - (y + (z + u))) + v)) ||
 rewrite(((x - (y + ((z + w) + u))) + (v + w)), ((x - (y + (z + u))) + v)) ||
 rewrite(((x - (y + ((z + w) + u))) + (v + z)), ((x - (y + (w + u))) + v)) ||
 rewrite(((x - (y + ((z + w) + u))) + (w + v)), ((x - (y + (z + u))) + v)) ||
 rewrite(((x - (y + ((z + w) + u))) + (z + v)), ((x - (y + (w + u))) + v)) ||
 rewrite(((x - ((y + (z + w)) + u)) + (v + w)), ((x - ((y + z) + u)) + v)) ||
 rewrite(((x - ((y + (z + w)) + u)) + (v + z)), ((x - ((y + w) + u)) + v)) ||
 rewrite(((x - ((y + (z + w)) + u)) + (w + v)), ((x - ((y + z) + u)) + v)) ||
 rewrite(((x - ((y + (z + w)) + u)) + (z + v)), ((x - ((y + w) + u)) + v)) ||
 rewrite(((x - (((y + z) + w) + u)) + (v + y)), ((x - ((z + w) + u)) + v)) ||
 rewrite(((x - (((y + z) + w) + u)) + (v + z)), ((x - ((y + w) + u)) + v)) ||
 rewrite(((x - (((y + z) + w) + u)) + (y + v)), ((x - ((z + w) + u)) + v)) ||
 rewrite(((x - (((y + z) + w) + u)) + (z + v)), ((x - ((y + w) + u)) + v)) ||
 rewrite(((x - (y*z)) + (w + (z*y))), (x + w)) ||
 rewrite(((x - (y*z)) + ((z*y) + w)), (x + w)))))) ||
((a.node_type() == IRNodeType::Mul) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*c0) + (y + ((x + z)*c1))), (y + (z*c1)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + ((x + (z*w))*c1))), (y + ((z*w)*c1)), prove_me(((c0 + c1) == 0))) ||
 rewrite(((x*c0) + (y + ((z + x)*c1))), (y + (z*c1)), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (y + (((z*w) + x)*c1))), (y + ((z*w)*c1)), prove_me(((c0 + c1) == 0))) ||
 rewrite(((x*c0) + (((x + y)*c1) + z)), ((y*c1) + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + (((x + (y*z))*c1) + w)), (((y*z)*c1) + w), prove_me(((c0 + c1) == 0))) ||
 rewrite(((x*c0) + (((y + x)*c1) + z)), ((y*c1) + z), ((c0 + c1) == 0)) ||
 rewrite(((x*c0) + ((((y*z) + x)*c1) + w)), (((y*z)*c1) + w), prove_me(((c0 + c1) == 0))) ||
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
 rewrite(((x*y) + (z + (w*x))), (z + ((w + y)*x))) ||
 rewrite(((x*y) + (z + (w*y))), (z + ((w + x)*y))) ||
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
 rewrite(((x*y) + ((x*z) + w)), ((x*(y + z)) + w)) ||
 rewrite(((x*y) + ((y*z) + w)), ((y*(z + x)) + w)) ||
 rewrite(((x*y) + ((z*x) + w)), ((x*(y + z)) + w)) ||
 rewrite(((x*y) + ((z*y) + w)), ((y*(x + z)) + w)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x*2) + (y - x)), (x + y)) ||
 rewrite(((x*c0) + (((c1 - (x*c2))*c3) - y)), (c1 - y), (((((c1 - c2)*c3) + c0) == c1) && (((c0*4) + ((c1 - (c2*4))*c3)) == c1))) ||
 rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((c1*c2) == c0)) ||
 rewrite(((x*y) + (z - (w + (u + (y*x))))), (z - (w + u))) ||
 rewrite(((x*y) + (z - (w + ((y*x) + u)))), (z - (w + u))) ||
 rewrite(((x*y) + (z - (w + (u*x)))), (z - (w - ((y - u)*x)))) ||
 rewrite(((x*y) + (z - (w + (u*y)))), (z - (w - ((x - u)*y)))) ||
 rewrite(((x*y) + (z - (w + (x*u)))), ((x*(y - u)) - (w - z))) ||
 rewrite(((x*y) + (z - (w + (y*u)))), (z - (w - (y*(x - u))))) ||
 rewrite(((x*y) + (z - ((w + (y*x)) + u))), (z - (w + u))) ||
 rewrite(((x*y) + (z - (((y*x) + w) + u))), (z - (w + u))) ||
 rewrite(((x*y) + (z - ((w*x) + u))), (z + (((y - w)*x) - u))) ||
 rewrite(((x*y) + (z - ((w*y) + u))), (z + (((x - w)*y) - u))) ||
 rewrite(((x*y) + (z - ((x*w) + u))), (z - (u - (x*(y - w))))) ||
 rewrite(((x*y) + (z - ((y*w) + u))), (z - (u - (y*(x - w))))) ||
 rewrite(((x*y) + ((x*z) - w)), ((x*(z + y)) - w)) ||
 rewrite(((x*y) + ((y*z) - w)), ((y*(z + x)) - w)) ||
 rewrite(((x*y) + ((z*x) - w)), ((x*(y + z)) - w)) ||
 rewrite(((x*y) + ((z*y) - w)), ((y*(x + z)) - w)) ||
 rewrite((((x/c0)*c0) + ((y*c1) - (x/c0))), ((x/c0) + (y*c1)), ((((-3/c0) == 0) || (c0 == 2)) && ((((3/c0) == 0) || (c0 == 2)) && ((((2/c0) == 0) || (c0 == 2)) && ((((1/c0)*c0) + (c1 - (1/c0))) == ((1/c0) + c1)))))))))) ||
((a.node_type() == IRNodeType::Div) && (
 rewrite((((c0 - (x % c1))/c1) + c2), ((x + c2) % c1), prove_me((((((c2 % c1) == ((c0/c1) + c2)) && (((c2 + 5) % c1) == (((c0 - (5 % c1))/c1) + c2))) && (((c2 + 1) % c1) == (((c0 - (1 % c1))/c1) + c2))) && (((c2 + 9) % c1) == (((c0 - (9 % c1))/c1) + c2))))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((((x + y)/c0) + (z - (w + (x/c0)))), ((((x % c0) + y)/c0) + (z - w))) ||
 rewrite((((x + y)/c0) + (z - (w + (y/c0)))), (((x + (y % c0))/c0) - (w - z))) ||
 rewrite((((x + y)/c0) + (z - ((x/c0) + w))), (z + (((y + (x % c0))/c0) - w))) ||
 rewrite((((x + y)/c0) + (z - ((y/c0) + w))), (z + (((x + (y % c0))/c0) - w))) ||
 rewrite((((x + y)/c0) + (z - (x/c0))), (z + ((y + (x % c0))/c0))) ||
 rewrite((((x + y)/c0) + (z - (y/c0))), (z + ((x + (y % c0))/c0))))))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite((min((((x + c0)/c1)*c1), (x + c2)) + c3), (x + fold((min(((c3 + c0) - c1), ((c1 + c0) + c3)) + 1))), ((((c1 <= 1) || (c0 < c2)) || (((c1 + c2) + -1) <= c0)) && ((((c0 <= c2) || (c1 <= -1)) || (c0 < (c1 + c2))) && ((((1 <= c1) || (c0 <= c2)) || ((c0 + c1) < c2)) && ((((-1 <= c1) || ((c2 + -1) <= (c0 + c1))) || (c0 < c2)) && (((-1 <= c1) || ((c2 + -2) <= (c0 + c1))) && ((c1 <= 1) || (((c1 + c2) + -2) <= c0)))))))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max((x - max(y, max(z, (w + x)))), c0) + u), (u - min(max(w, (max(z, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max(z, (x + w)))), c0) + u), (u - min(max(w, (max(z, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max((x + z), w))), c0) + u), (u - min(max((max(y, w) - x), z), fold((0 - c0))))) ||
 rewrite((max((x - max(y, max((z + x), w))), c0) + u), (u - min(max(z, (max(w, y) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max(y, (x + z)), w)), c0) + u), (u - min(max(z, (max(y, w) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max(y, (z + x)), w)), c0) + u), (u - min(max(z, (max(y, w) - x)), fold((0 - c0))))) ||
 rewrite((max((x - max(max((x + y), z), w)), c0) + u), (u - min(max((max(z, w) - x), y), fold((0 - c0))))) ||
 rewrite((max((x - max(max((y + x), z), w)), c0) + u), (u - min(max((max(z, w) - x), y), fold((0 - c0)))))))
