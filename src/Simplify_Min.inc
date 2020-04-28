((b.node_type() == IRNodeType::Add) && (
 rewrite(min(x, (y + ((z + w) - (u + (v + w))))), min(x, (y + (z - (u + v))))) ||
 rewrite(min(x, (y + ((z + w) - (u + (v + z))))), min(x, (y + (w - (u + v))))) ||
 rewrite(min(x, (y + ((z + w) - (u + (w + v))))), min(x, (y + (z - (u + v))))) ||
 rewrite(min(x, (y + ((z + w) - (u + (z + v))))), min(x, (y + (w - (u + v))))) ||
 rewrite(min(x, (y + ((z + w) - ((u + w) + v)))), min(x, (y + (z - (u + v))))) ||
 rewrite(min(x, (y + ((z + w) - ((u + z) + v)))), min(x, (y + (w - (u + v))))) ||
 rewrite(min(x, (y + ((z + w) - ((w + u) + v)))), min(x, (y + (z - (u + v))))) ||
 rewrite(min(x, (y + ((z + w) - ((z + u) + v)))), min(x, (y + (w - (u + v))))) ||
 rewrite(min(x, ((y + (z + select(w, c0, c1))) + c2)), min(x, (y + (z + select(w, fold((c2 + c0)), fold((c2 + c1))))))) ||
 rewrite(min(x, ((y + (select(z, c0, c1) + w)) + c2)), min(x, (y + (select(z, fold((c2 + c0)), fold((c2 + c1))) + w)))) ||
 rewrite(min(x, (((y + select(z, c0, c1)) + w) + c2)), min(x, (w + (y + select(z, fold((c2 + c0)), fold((c2 + c1))))))) ||
 rewrite(min(x, (((select(y, c0, c1) + z) + w) + c2)), min(x, ((select(y, fold((c2 + c0)), fold((c2 + c1))) + w) + z))) ||
 rewrite(min(x, (((y + z) - (w + (u + y))) + v)), min(x, ((z - (w + u)) + v))) ||
 rewrite(min(x, (((y + z) - (w + (u + z))) + v)), min(x, ((y - (w + u)) + v))) ||
 rewrite(min(x, (((y + z) - (w + (y + u))) + v)), min(x, ((z - (w + u)) + v))) ||
 rewrite(min(x, (((y + z) - (w + (z + u))) + v)), min(x, ((y - (w + u)) + v))) ||
 rewrite(min(x, (((y + z) - ((w + y) + u)) + v)), min(x, ((z - (w + u)) + v))) ||
 rewrite(min(x, (((y + z) - ((w + z) + u)) + v)), min(x, ((y - (w + u)) + v))) ||
 rewrite(min(x, (((y + z) - ((y + w) + u)) + v)), min(x, ((z - (w + u)) + v))) ||
 rewrite(min(x, (((y + z) - ((z + w) + u)) + v)), min(x, ((y - (w + u)) + v))) ||
 rewrite(min(x, (((y + select(z, c0, c1)) - w) + c2)), min(x, ((y + select(z, fold((c2 + c0)), fold((c2 + c1)))) - w))) ||
 rewrite(min(x, (((select(y, c0, c1) + z) - w) + c2)), min(x, ((select(y, fold((c2 + c0)), fold((c2 + c1))) - w) + z))))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(min((x + ((y + z) - (w + (u + y)))), v), min((x + (z - (w + u))), v)) ||
 rewrite(min((x + ((y + z) - (w + (u + z)))), v), min((x + (y - (w + u))), v)) ||
 rewrite(min((x + ((y + z) - (w + (y + u)))), v), min((x + (z - (w + u))), v)) ||
 rewrite(min((x + ((y + z) - (w + (z + u)))), v), min((x + (y - (w + u))), v)) ||
 rewrite(min((x + ((y + z) - ((w + y) + u))), v), min((x + (z - (w + u))), v)) ||
 rewrite(min((x + ((y + z) - ((w + z) + u))), v), min((x + (y - (w + u))), v)) ||
 rewrite(min((x + ((y + z) - ((y + w) + u))), v), min((x + (z - (w + u))), v)) ||
 rewrite(min((x + ((y + z) - ((z + w) + u))), v), min((x + (y - (w + u))), v)) ||
 rewrite(min(((x + (y + select(z, c0, c1))) + c2), w), min((x + (y + select(z, fold((c2 + c0)), fold((c2 + c1))))), w)) ||
 rewrite(min(((x + (select(y, c0, c1) + z)) + c2), w), min((x + (select(y, fold((c2 + c0)), fold((c2 + c1))) + z)), w)) ||
 rewrite(min((((x + select(y, c0, c1)) + z) + c2), w), min((z + (x + select(y, fold((c2 + c0)), fold((c2 + c1))))), w)) ||
 rewrite(min((((select(x, c0, c1) + y) + z) + c2), w), min(((select(x, fold((c2 + c0)), fold((c2 + c1))) + z) + y), w)) ||
 rewrite(min((((x + y) - (z + (w + x))) + u), v), min(((y - (z + w)) + u), v)) ||
 rewrite(min((((x + y) - (z + (w + y))) + u), v), min(((x - (z + w)) + u), v)) ||
 rewrite(min((((x + y) - (z + (x + w))) + u), v), min(((y - (z + w)) + u), v)) ||
 rewrite(min((((x + y) - (z + (y + w))) + u), v), min(((x - (z + w)) + u), v)) ||
 rewrite(min((((x + y) - ((x + z) + w)) + u), v), min(((y - (z + w)) + u), v)) ||
 rewrite(min((((x + y) - ((y + z) + w)) + u), v), min(((x - (z + w)) + u), v)) ||
 rewrite(min((((x + y) - ((z + x) + w)) + u), v), min(((y - (z + w)) + u), v)) ||
 rewrite(min((((x + y) - ((z + y) + w)) + u), v), min(((x - (z + w)) + u), v)) ||
 rewrite(min((((x + select(y, c0, c1)) - z) + c2), w), min(((x + select(y, fold((c2 + c0)), fold((c2 + c1)))) - z), w)) ||
 rewrite(min((((select(x, c0, c1) + y) - z) + c2), w), min(((select(x, fold((c2 + c0)), fold((c2 + c1))) - z) + y), w)) ||
((b.node_type() == IRNodeType::Add) && (
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
 rewrite(min((x + ((y + z) + w)), (u + (z + y))), (min((x + w), u) + (z + y))) ||
 rewrite(min((x + ((y + z) + w)), ((z + y) + u)), (min((x + w), u) + (z + y))) ||
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
 rewrite(min((((x + y) + z) + w), (u + (y + x))), (min((z + w), u) + (y + x))) ||
 rewrite(min((((x + y) + z) + w), ((y + x) + u)), (min((z + w), u) + (y + x))))))) ||
((a.node_type() == IRNodeType::Sub) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(((x + y) - z), (w + (u + (x + y)))), (min((x - z), (w + (u + x))) + y)) ||
 rewrite(min(((x + y) - z), (w + (u + (y + x)))), (min((y - z), (w + (u + y))) + x)) ||
 rewrite(min(((x + y) - z), (w + ((x + y) + u))), (min((x - z), (w + (x + u))) + y)) ||
 rewrite(min(((x + y) - z), (w + ((y + x) + u))), (min((y - z), (w + (y + u))) + x)) ||
 rewrite(min(((x + y) - z), ((w + (x + y)) + u)), (min((x - z), (w + (x + u))) + y)) ||
 rewrite(min(((x + y) - z), ((w + (y + x)) + u)), (min((y - z), (w + (y + u))) + x)) ||
 rewrite(min(((x + y) - z), (((x + y) + w) + u)), (min((y - z), ((u + y) + w)) + x)) ||
 rewrite(min(((x + y) - z), (((y + x) + w) + u)), (min((y - z), ((y + u) + w)) + x)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(((x - y) - z), (x - w)), (x - max(w, (z + y)))))))) ||
((a.node_type() == IRNodeType::Mul) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((((x + c0)/c1)*c1), (x + c2)), (x + c2), (((c2 + -1) <= (c0 + c1)) && (((c1 + c2) + -1) <= c0))))))) ||
((a.node_type() == IRNodeType::Min) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(min(x, (y + (z + w))), (u + (v + w))), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (v + z))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (w + v))), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (z + v))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + w) + v)), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + z) + v)), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((w + u) + v)), min(x, (min((z + y), (v + u)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((z + u) + v)), min(x, (min((y + w), (v + u)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (v + y))), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (v + z))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (y + v))), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (z + v))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + y) + v)), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + z) + v)), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((y + u) + v)), min(x, (min((z + w), (v + u)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z + u) + v)), min(x, (min((y + w), (v + u)) + z))) ||
 rewrite(min(min(x, min(y, (z + w))), (u + w)), min(min(x, (min(z, u) + w)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + z)), min(min(x, (min(w, u) + z)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (w + u)), min(min(x, (min(z, u) + w)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (z + u)), min(min(x, (min(w, u) + z)), y)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (u + v)), min(min(x, min(y, (min(w, v) + u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + u)), min(min(x, min(y, (min(w, v) + u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + w)), min(min(x, min(y, (min(u, v) + w))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (w + v)), min(min(x, min(y, (min(u, v) + w))), z)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + w)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + z)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (w + v)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (z + v)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min((y + z), w)), (u + y)), min(min(x, w), (min(z, u) + y))) ||
 rewrite(min(min(x, min((y + z), w)), (u + z)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (y + u)), min(min(x, (min(u, z) + y)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (z + u)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + w)), min(min(x, min((min(z, v) + w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + z)), min(min(x, min((min(w, v) + z), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (w + v)), min(min(x, min((min(z, v) + w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (z + v)), min(min(x, min((min(w, v) + z), u)), y)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + y)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + z)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (y + v)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (z + v)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (v + y))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (v + z))), min((min((x + y), (u + v)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (y + v))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (z + v))), min((min((x + y), (u + v)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), ((u + y) + v)), min(w, (min((x + z), (u + v)) + y))) ||
 rewrite(min(min((x + (y + z)), w), ((u + z) + v)), min(w, (min((y + x), (u + v)) + z))) ||
 rewrite(min(min((x + (y + z)), w), ((y + u) + v)), min(w, (min((x + z), (v + u)) + y))) ||
 rewrite(min(min((x + (y + z)), w), ((z + u) + v)), min(w, (min((y + x), (v + u)) + z))) ||
 rewrite(min(min(((x + y) + z), w), (u + (v + x))), min((min((y + z), (u + v)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (v + y))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (x + v))), min((min((y + z), (u + v)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (y + v))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), ((u + x) + v)), min(w, (min((y + z), (u + v)) + x))) ||
 rewrite(min(min(((x + y) + z), w), ((u + y) + v)), min(w, (min((x + z), (u + v)) + y))) ||
 rewrite(min(min(((x + y) + z), w), ((x + u) + v)), min(w, (min((y + z), (v + u)) + x))) ||
 rewrite(min(min(((x + y) + z), w), ((y + u) + v)), min(w, (min((x + z), (v + u)) + y))) ||
 rewrite(min(min(min(x, (y + z)), w), (u + y)), min(min((min(z, u) + y), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + z)), min(min((min(y, u) + z), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (y + u)), min(min((min(z, u) + y), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (z + u)), min(min((min(y, u) + z), w), x)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + w)), min(min(min(x, (min(z, v) + w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + z)), min(min(min(x, (min(w, v) + z)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (w + v)), min(min(min(x, (min(z, v) + w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (z + v)), min(min(min(x, (min(w, v) + z)), u), y)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + y)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + z)), min(min(min((min(v, y) + z), w), u), x)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (y + v)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (z + v)), min(min(min((min(v, y) + z), w), u), x)) ||
 rewrite(min(min(min((x + y), z), w), (u + x)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (u + y)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (x + u)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (y + u)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + y)), min(min(min((min(z, v) + y), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + z)), min(min(min((min(y, v) + z), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (y + v)), min(min(min((min(z, v) + y), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (z + v)), min(min(min((min(y, v) + z), w), u), x)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + x)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + y)), min(min(min((min(x, v) + y), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (x + v)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (y + v)), min(min(min((min(x, v) + y), w), u), z)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(min(x, (y - z)), (y - w)), min(x, (y - max(z, w)))) ||
 rewrite(min(min(x, ((y + z) - w)), ((z + y) - u)), min(x, ((y + z) - max(w, u)))) ||
 rewrite(min(min(x, min(y, (z - w))), (z - u)), min(x, min(y, (z - max(w, u))))) ||
 rewrite(min(min(x, min(y, min(z, (w - u)))), (w - v)), min(x, min(y, min(z, (w - max(u, v)))))) ||
 rewrite(min(min(x, min(y, min((z - w), u))), (z - v)), min(x, min(y, min((z - max(w, v)), u)))) ||
 rewrite(min(min(x, min((y - z), w)), (y - u)), min(x, min((y - max(z, u)), w))) ||
 rewrite(min(min(x, min(min(y, (z - w)), u)), (z - v)), min(x, min(min(y, (z - max(w, v))), u))) ||
 rewrite(min(min(x, min(min((y - z), w), u)), (y - v)), min(x, min(min((y - max(z, v)), u), w))) ||
 rewrite(min(min((x - y), z), (x - w)), min(z, (x - max(y, w)))) ||
 rewrite(min(min(((x + y) - z), w), ((y + x) - u)), min(w, ((x + y) - max(z, u)))) ||
 rewrite(min(min(min(x, (y - z)), w), (y - u)), min(w, min(x, (y - max(z, u))))) ||
 rewrite(min(min(min(x, min(y, (z - w))), u), (z - v)), min(u, min(x, min(y, (z - max(w, v)))))) ||
 rewrite(min(min(min(x, min((y - z), w)), u), (y - v)), min(w, min(x, min((y - max(z, v)), u)))) ||
 rewrite(min(min(min((x - y), z), w), (x - u)), min(w, min((x - max(y, u)), z))) ||
 rewrite(min(min(min(min(x, (y - z)), w), u), (y - v)), min(min(min((y - max(v, z)), w), u), x)) ||
 rewrite(min(min(min(min((x - y), z), w), u), (x - v)), min(z, min(min((x - max(y, v)), u), w)))))))
