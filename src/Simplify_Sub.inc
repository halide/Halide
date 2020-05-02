((b.node_type() == IRNodeType::Add) && (
 rewrite((x - (y + (x + z))), (0 - (y + z))) ||
 rewrite((x - (y + (z + x))), (0 - (y + z))) ||
 rewrite((x - (y + (z + (w + x)))), (0 - (y + (z + w)))) ||
 rewrite((x - (y + (z + (x + w)))), (0 - (y + (z + w)))) ||
 rewrite((x - (y + ((x + z) + w))), (0 - (y + (z + w)))) ||
 rewrite((x - (y + ((z + x) + w))), (0 - (y + (z + w)))) ||
 rewrite((x - (y + select(z, (w + c0), (x + c1)))), (select(z, (x + (c1 - w)), c0) - y), ((c0 + c1) == 0)) ||
 rewrite((x - ((x + y) + z)), (0 - (y + z))) ||
 rewrite((x - ((y + x) + z)), (0 - (y + z))) ||
 rewrite((x - ((y + (x + z)) + w)), (0 - ((y + z) + w))) ||
 rewrite((x - ((y + (z + x)) + w)), (0 - ((y + z) + w))) ||
 rewrite((x - (((x + y) + z) + w)), (0 - ((y + z) + w))) ||
 rewrite((x - (((y + x) + z) + w)), (0 - ((y + z) + w))) ||
 rewrite((x - (select(y, (z + c0), (x + c1)) + w)), (select(y, (x + (c1 - z)), c0) - w), ((c0 + c1) == 0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((x - min(y, (z + min(w, min(u, max(v, w)))))), (x - min(y, (z + min(w, u))))) ||
 rewrite((x - min(y, (z + min(w, min(u, max(w, v)))))), (x - min(y, (z + min(w, u))))) ||
 rewrite((x - min(y, (z + min(w, min(max(u, w), v))))), (x - min(y, (z + min(w, v))))) ||
 rewrite((x - min(y, (z + min(w, min(max(w, u), v))))), (x - min(y, (z + min(w, v))))) ||
 rewrite((x - min(y, (z + min(min(w, max(u, v)), u)))), (x - min(y, (z + min(w, u))))) ||
 rewrite((x - min(y, (z + min(min(w, max(u, v)), v)))), (x - min(y, (z + min(w, v))))) ||
 rewrite((x - min(y, (z + min(min(max(w, u), v), u)))), (x - min(y, (z + min(u, v))))) ||
 rewrite((x - min(y, (z + min(min(max(w, u), v), w)))), (x - min(y, (z + min(w, v))))) ||
 rewrite((x - min(y, (min(z, min(w, max(u, z))) + v))), (x - min(y, (min(z, w) + v)))) ||
 rewrite((x - min(y, (min(z, min(w, max(z, u))) + v))), (x - min(y, (min(z, w) + v)))) ||
 rewrite((x - min(y, (min(z, min(max(w, z), u)) + v))), (x - min(y, (min(z, u) + v)))) ||
 rewrite((x - min(y, (min(z, min(max(z, w), u)) + v))), (x - min(y, (min(z, u) + v)))) ||
 rewrite((x - min(y, (min(min(z, max(w, u)), u) + v))), (x - min(y, (min(z, u) + v)))) ||
 rewrite((x - min(y, (min(min(z, max(w, u)), w) + v))), (x - min(y, (min(z, w) + v)))) ||
 rewrite((x - min(y, (min(min(max(z, w), u), w) + v))), (x - min(y, (min(w, u) + v)))) ||
 rewrite((x - min(y, (min(min(max(z, w), u), z) + v))), (x - min(y, (min(z, u) + v)))) ||
 rewrite((x - min(y, (x - z))), max(z, (x - y))) ||
 rewrite((x - min(y, min(z, (min(y, (w/c0)) + c1)))), (x - min(z, (min(y, (w/c0)) + c1))), (c1 <= 0)) ||
 rewrite((x - min(y, min(z, (min((w/c0), y) + c1)))), (x - min(z, (min((w/c0), y) + c1))), (c1 <= 0)) ||
 rewrite((x - min(y, min(z, min((w*c0), (z + c1))))), (x - min(y, min((w*c0), z))), (0 <= c1)) ||
 rewrite((x - min(y, min((min(y, (z/c0)) + c1), w))), (x - min((min(y, (z/c0)) + c1), w)), (c1 <= 0)) ||
 rewrite((x - min(y, min((min((z/c0), y) + c1), w))), (x - min((min((z/c0), y) + c1), w)), (c1 <= 0)) ||
 rewrite((x - min(y, min((z*c0), (y + c1)))), (x - min(y, (z*c0))), (0 <= c1)) ||
 rewrite((x - min(y, min((z/c0), (min(w, y) + c1)))), (x - min((z/c0), (min(w, y) + c1))), (((min((1/c0), c1) <= 0) && (c1 <= 1)) && (c1 <= 0))) ||
 rewrite((x - min(y, min((z/c0), (min(y, w) + c1)))), (x - min((z/c0), (min(y, w) + c1))), (((min((1/c0), c1) <= 0) && (c1 <= 1)) && (c1 <= 0))) ||
 rewrite((x - min(y, min(min((z*c0), (w + c1)), w))), (x - min(y, min((z*c0), w))), (0 <= c1)) ||
 rewrite((x - min((y + min(z, min(w, max(u, z)))), v)), (x - min((y + min(z, w)), v))) ||
 rewrite((x - min((y + min(z, min(w, max(z, u)))), v)), (x - min((y + min(z, w)), v))) ||
 rewrite((x - min((y + min(z, min(max(w, z), u))), v)), (x - min((y + min(z, u)), v))) ||
 rewrite((x - min((y + min(z, min(max(z, w), u))), v)), (x - min((y + min(z, u)), v))) ||
 rewrite((x - min((y + min(min(z, max(w, u)), u)), v)), (x - min((y + min(z, u)), v))) ||
 rewrite((x - min((y + min(min(z, max(w, u)), w)), v)), (x - min((y + min(z, w)), v))) ||
 rewrite((x - min((y + min(min(max(z, w), u), w)), v)), (x - min((y + min(w, u)), v))) ||
 rewrite((x - min((y + min(min(max(z, w), u), z)), v)), (x - min((y + min(z, u)), v))) ||
 rewrite((x - min((min(y, min(z, max(w, y))) + u), v)), (x - min((min(y, z) + u), v))) ||
 rewrite((x - min((min(y, min(z, max(y, w))) + u), v)), (x - min((min(y, z) + u), v))) ||
 rewrite((x - min((min(y, min(max(y, z), w)) + u), v)), (x - min((min(y, w) + u), v))) ||
 rewrite((x - min((min(y, min(max(z, y), w)) + u), v)), (x - min((min(y, w) + u), v))) ||
 rewrite((x - min((min(min(y, max(z, w)), w) + u), v)), (x - min((min(y, w) + u), v))) ||
 rewrite((x - min((min(min(y, max(z, w)), z) + u), v)), (x - min((min(y, z) + u), v))) ||
 rewrite((x - min((min(min(max(y, z), w), y) + u), v)), (x - min((min(y, w) + u), v))) ||
 rewrite((x - min((min(min(max(y, z), w), z) + u), v)), (x - min((min(z, w) + u), v))) ||
 rewrite((x - min((x - y), z)), max((x - z), y)) ||
 rewrite((x - min((y - z), (w + (u - z)))), (x - (min(y, (w + u)) - z))) ||
 rewrite((x - min((y - z), ((w - z) + u))), (x - (min((w + u), y) - z))) ||
 rewrite((x - min(min(y, (z + w)), (u + (v + w)))), (x - min(y, (w + min((u + v), z))))) ||
 rewrite((x - min(min(y, (z + w)), (u + (v + z)))), (x - min(y, (z + min((u + v), w))))) ||
 rewrite((x - min(min(y, (z + w)), (u + (w + v)))), (x - min((min(z, (v + u)) + w), y))) ||
 rewrite((x - min(min(y, (z + w)), (u + (z + v)))), (x - min(y, (z + min(w, (u + v)))))) ||
 rewrite((x - min(min(y, (z + w)), ((u + w) + v))), (x - min((min(z, (u + v)) + w), y))) ||
 rewrite((x - min(min(y, (z + w)), ((u + z) + v))), (x - min((min(w, (u + v)) + z), y))) ||
 rewrite((x - min(min(y, (z + w)), ((w + u) + v))), (x - min((min(z, (u + v)) + w), y))) ||
 rewrite((x - min(min(y, (z + w)), ((z + u) + v))), (x - min(y, (z + min((v + u), w))))) ||
 rewrite((x - min(min(y, ((z*c0) + c1)), (z*c0))), (x - min(y, (z*c0))), ((0 <= c1) && (min(c0, 0) <= (c0 + c1)))) ||
 rewrite((x - min(min(y, (min(z, (w/c0)) + c1)), z)), (x - min(y, (min(z, (w/c0)) + c1))), (c1 <= 0)) ||
 rewrite((x - min(min(y, (min((z/c0), w) + c1)), w)), (x - min(y, (min((z/c0), w) + c1))), (c1 <= 0)) ||
 rewrite((x - min(min(y, min((z*c0), (y + c1))), w)), (x - min(min((z*c0), y), w)), (0 <= c1)) ||
 rewrite((x - min(min((y + z), w), (u + (v + y)))), (x - min(w, (y + min(z, (u + v)))))) ||
 rewrite((x - min(min((y + z), w), (u + (v + z)))), (x - min((min((v + u), y) + z), w))) ||
 rewrite((x - min(min((y + z), w), (u + (y + v)))), (x - min((min((v + u), z) + y), w))) ||
 rewrite((x - min(min((y + z), w), (u + (z + v)))), (x - min((min((v + u), y) + z), w))) ||
 rewrite((x - min(min((y + z), w), ((u + y) + v))), (x - min((min((u + v), z) + y), w))) ||
 rewrite((x - min(min((y + z), w), ((u + z) + v))), (x - min((min((u + v), y) + z), w))) ||
 rewrite((x - min(min((y + z), w), ((y + u) + v))), (x - min((min((u + v), z) + y), w))) ||
 rewrite((x - min(min((y + z), w), ((z + u) + v))), (x - min((min((u + v), y) + z), w))) ||
 rewrite((x - min(min(((y*c0) + c1), z), (y*c0))), (x - min(z, (y*c0))), ((0 <= c1) && (min(c0, 0) <= (c0 + c1)))) ||
 rewrite((x - min(min((min(y, (z/c0)) + c1), w), y)), (x - min((min(y, (z/c0)) + c1), w)), (c1 <= 0)) ||
 rewrite((x - min(min((min((y/c0), z) + c1), w), z)), (x - min((min((y/c0), z) + c1), w)), (c1 <= 0)) ||
 rewrite((x - min(min((y*c0), (z + c1)), z)), (x - min((y*c0), z)), (0 <= c1)) ||
 rewrite((x - min(min((y/c0), (min(z, w) + c1)), w)), (x - min((y/c0), (min(z, w) + c1))), (((min((1/c0), c1) <= 0) && (c1 <= 1)) && (c1 <= 0))) ||
 rewrite((x - min(min((y/c0), (min(z, w) + c1)), z)), (x - min((y/c0), (min(z, w) + c1))), (((min((1/c0), c1) <= 0) && (c1 <= 1)) && (c1 <= 0))) ||
 rewrite((x - min(min(min(y, z), (w*c0)), (y + c1))), (x - min(min(z, (w*c0)), (y + c1))), ((min((c0*-5), (c1 + -8)) <= -8) && (min(min((c0*4), (c1 + -8)), 5) == min((min(c0, -2)*4), (c1 + -8))))) ||
 rewrite((x - min(min(min(y, z), (w*c0)), (z + c1))), (x - min(min(y, (w*c0)), (z + c1))), ((min((c0*-5), (c1 + -8)) <= -8) && (min(min((c0*4), (c1 + -8)), 5) == min((min(c0, -2)*4), (c1 + -8))))) ||
 rewrite((x - min(min(min(y, z), (w/c0)), (y + c1))), (x - min(min(z, (w/c0)), (y + c1))), (c1 <= 0)) ||
 rewrite((x - min(min(min(y, z), (w/c0)), (z + c1))), (x - min(min(y, (w/c0)), (z + c1))), (c1 <= 0)) ||
 rewrite((x - min(min(min((y*c0), (z + c1)), z), w)), (x - min(min((y*c0), z), w)), (0 <= c1)))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + min(y, (z + (w + u)))) - u), (x + min((y - u), (w + z)))) ||
 rewrite(((x + min(y, (z + (w + u)))) - w), (x + min((y - w), (z + u)))) ||
 rewrite(((x + min(y, ((z + w) + u))) - w), (x + min((y - w), (z + u)))) ||
 rewrite(((x + min(y, ((z + w) + u))) - z), (x + min((y - z), (w + u)))) ||
 rewrite(((x + min((y + (z + w)), u)) - w), (x + min((u - w), (z + y)))) ||
 rewrite(((x + min((y + (z + w)), u)) - z), (x + min((u - z), (w + y)))) ||
 rewrite(((x + min(((y + z) + w), u)) - y), (x + min((u - y), (w + z)))) ||
 rewrite(((x + min(((y + z) + w), u)) - z), (x + min((u - z), (w + y)))) ||
 rewrite(((min(x, (y + (z + w))) + u) - w), (min((x - w), (z + y)) + u)) ||
 rewrite(((min(x, (y + (z + w))) + u) - z), (min((x - z), (y + w)) + u)) ||
 rewrite(((min(x, ((y + z) + w)) + u) - y), (min((x - y), (z + w)) + u)) ||
 rewrite(((min(x, ((y + z) + w)) + u) - z), (min((x - z), (y + w)) + u)) ||
 rewrite(((min((x + (y + z)), w) + u) - y), (min((w - y), (x + z)) + u)) ||
 rewrite(((min((x + (y + z)), w) + u) - z), (min((w - z), (x + y)) + u)) ||
 rewrite(((min(((x + y) + z), w) + u) - x), (min((w - x), (y + z)) + u)) ||
 rewrite(((min(((x + y) + z), w) + u) - y), (min((w - y), (x + z)) + u)) ||
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
 rewrite(((x + (y + z)) - (w + y)), (x + (z - w))) ||
 rewrite(((x + (y + z)) - (w + z)), (x + (y - w))) ||
 rewrite(((x + (y + z)) - (y + w)), (x + (z - w))) ||
 rewrite(((x + (y + z)) - (z + w)), (x + (y - w))) ||
 rewrite((((x + y) + z) - (w + x)), ((y + z) - w)) ||
 rewrite((((x + y) + z) - (w + y)), ((x + z) - w)) ||
 rewrite((((x + y) + z) - (x + w)), ((y + z) - w)) ||
 rewrite((((x + y) + z) - (y + w)), ((x + z) - w)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((x + y) - min(z, (w + x))), (y - min(w, (z - x)))) ||
 rewrite(((x + y) - min(z, (w + y))), (x - min(w, (z - y)))) ||
 rewrite(((x + y) - min(z, (w + (u + x)))), (y - min((z - x), (u + w)))) ||
 rewrite(((x + y) - min(z, (w + (u + y)))), (x - min((z - y), (u + w)))) ||
 rewrite(((x + y) - min(z, (w + (x + u)))), (y - min((z - x), (w + u)))) ||
 rewrite(((x + y) - min(z, (w + (y + u)))), (x - min((z - y), (w + u)))) ||
 rewrite(((x + y) - min(z, (x + w))), (y - min(w, (z - x)))) ||
 rewrite(((x + y) - min(z, (y + w))), (x - min(w, (z - y)))) ||
 rewrite(((x + y) - min(z, ((w + x) + u))), (y - min((z - x), (w + u)))) ||
 rewrite(((x + y) - min(z, ((w + y) + u))), (x - min((z - y), (w + u)))) ||
 rewrite(((x + y) - min(z, ((x + w) + u))), (y - min((z - x), (w + u)))) ||
 rewrite(((x + y) - min(z, ((y + w) + u))), (x - min((z - y), (w + u)))) ||
 rewrite(((x + y) - min(z, ((y + x) - w))), (y - min((z - x), (y - w)))) ||
 rewrite(((x + y) - min(z, min(w, (u + x)))), (y - min(u, (min(w, z) - x)))) ||
 rewrite(((x + y) - min(z, min(w, (u + y)))), (x - min(u, (min(w, z) - y)))) ||
 rewrite(((x + y) - min(z, min(w, (u + (v + x))))), (y - min((min(w, z) - x), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, (u + (v + y))))), (x - min((min(w, z) - y), (v + u)))) ||
 rewrite(((x + y) - min(z, min(w, (u + (x + v))))), (y - min((min(w, z) - x), (u + v)))) ||
 rewrite(((x + y) - min(z, min(w, (u + (y + v))))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite(((x + y) - min(z, min(w, (x + u)))), (y - min(u, (min(w, z) - x)))) ||
 rewrite(((x + y) - min(z, min(w, (y + u)))), (x - min(u, (min(w, z) - y)))) ||
 rewrite(((x + y) - min(z, min(w, ((u + x) + v)))), (y - min((min(w, z) - x), (u + v)))) ||
 rewrite(((x + y) - min(z, min(w, ((u + y) + v)))), (x - min((min(w, z) - y), (u + v)))) ||
 rewrite(((x + y) - min(z, min(w, ((x + u) + v)))), (y - min((min(w, z) - x), (u + v)))) ||
 rewrite(((x + y) - min(z, min(w, ((y + u) + v)))), (x - min((min(z, w) - y), (u + v)))) ||
 rewrite(((x + y) - min(z, min(w, ((x + y) - u)))), (x - min((min(w, z) - y), (x - u)))) ||
 rewrite(((x + y) - min(z, min(w, ((y + x) - u)))), (x - min((min(w, z) - y), (x - u)))) ||
 rewrite(((x + y) - min(z, min((w + x), u))), (y - min(w, (min(u, z) - x)))) ||
 rewrite(((x + y) - min(z, min((w + y), u))), (x - min(w, (min(u, z) - y)))) ||
 rewrite(((x + y) - min(z, min((w + (u + x)), v))), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(z, min((w + (u + y)), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite(((x + y) - min(z, min((w + (x + u)), v))), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(z, min((w + (y + u)), v))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite(((x + y) - min(z, min((x + w), u))), (y - min(w, (min(u, z) - x)))) ||
 rewrite(((x + y) - min(z, min((y + w), u))), (x - min(w, (min(u, z) - y)))) ||
 rewrite(((x + y) - min(z, min(((w + x) + u), v))), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(z, min(((w + y) + u), v))), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite(((x + y) - min(z, min(((x + w) + u), v))), (y - min((min(z, v) - x), (w + u)))) ||
 rewrite(((x + y) - min(z, min(((y + w) + u), v))), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite(((x + y) - min(z, min(((x + y) - w), u))), (y - min((min(u, z) - x), (y - w)))) ||
 rewrite(((x + y) - min(z, min(((y + x) - w), u))), (y - min((min(u, z) - x), (y - w)))) ||
 rewrite(((x + y) - min((x + z), w)), (y - min((w - x), z))) ||
 rewrite(((x + y) - min((y + z), w)), (x - min((w - y), z))) ||
 rewrite(((x + y) - min((z + x), w)), (y - min((w - x), z))) ||
 rewrite(((x + y) - min((z + y), w)), (x - min((w - y), z))) ||
 rewrite(((x + y) - min((z + (w + x)), u)), (y - min((u - x), (w + z)))) ||
 rewrite(((x + y) - min((z + (w + y)), u)), (x - min((u - y), (w + z)))) ||
 rewrite(((x + y) - min((z + (x + w)), u)), (y - min((u - x), (z + w)))) ||
 rewrite(((x + y) - min((z + (y + w)), u)), (x - min((u - y), (z + w)))) ||
 rewrite(((x + y) - min(((x + z) + w), u)), (y - min((u - x), (w + z)))) ||
 rewrite(((x + y) - min(((y + z) + w), u)), (x - min((u - y), (w + z)))) ||
 rewrite(((x + y) - min(((z + x) + w), u)), (y - min((u - x), (w + z)))) ||
 rewrite(((x + y) - min(((z + y) + w), u)), (x - min((u - y), (z + w)))) ||
 rewrite(((x + y) - min(((y + x) - z), w)), (x - min((w - y), (x - z)))) ||
 rewrite(((x + y) - min(min(z, (w + x)), u)), (y - min((min(z, u) - x), w))) ||
 rewrite(((x + y) - min(min(z, (w + y)), u)), (x - min((min(z, u) - y), w))) ||
 rewrite(((x + y) - min(min(z, (w + (u + x))), v)), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(min(z, (w + (u + y))), v)), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite(((x + y) - min(min(z, (w + (x + u))), v)), (y - min((min(z, v) - x), (u + w)))) ||
 rewrite(((x + y) - min(min(z, (w + (y + u))), v)), (x - min((min(z, v) - y), (u + w)))) ||
 rewrite(((x + y) - min(min(z, (x + w)), u)), (y - min((min(z, u) - x), w))) ||
 rewrite(((x + y) - min(min(z, (y + w)), u)), (x - min((min(z, u) - y), w))) ||
 rewrite(((x + y) - min(min(z, ((w + x) + u)), v)), (y - min((min(z, v) - x), (w + u)))) ||
 rewrite(((x + y) - min(min(z, ((w + y) + u)), v)), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite(((x + y) - min(min(z, ((x + w) + u)), v)), (y - min((min(z, v) - x), (w + u)))) ||
 rewrite(((x + y) - min(min(z, ((y + w) + u)), v)), (x - min((min(z, v) - y), (w + u)))) ||
 rewrite(((x + y) - min(min(z, ((x + y) - w)), u)), (y - min((min(z, u) - x), (y - w)))) ||
 rewrite(((x + y) - min(min(z, ((y + x) - w)), u)), (x - min((min(z, u) - y), (x - w)))) ||
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
 rewrite(((x + y) - min(min(((x + y) - z), w), u)), (x - min((min(w, u) - y), (x - z)))) ||
 rewrite(((x + y) - min(min(((y + x) - z), w), u)), (x - min((min(w, u) - y), (x - z)))) ||
 rewrite(((x + min(y, z)) - min(w, (u + min(z, y)))), (x - min(u, (w - min(z, y))))) ||
 rewrite(((x + min(y, z)) - min(w, (min(z, y) + u))), (x - min(u, (w - min(z, y))))) ||
 rewrite(((x + min(y, z)) - min((w + min(z, y)), u)), (x - min((u - min(z, y)), w))) ||
 rewrite(((x + min(y, z)) - min((min(z, y) + w), u)), (x - min(w, (u - min(z, y))))) ||
 rewrite(((min(x, y) + z) - min(w, (u + min(y, x)))), (z - min(u, (w - min(y, x))))) ||
 rewrite(((min(x, y) + z) - min(w, (min(y, x) + u))), (z - min((w - min(y, x)), u))) ||
 rewrite(((min(x, y) + z) - min((w + min(y, x)), u)), (z - min((u - min(y, x)), w))) ||
 rewrite(((min(x, y) + z) - min((min(y, x) + w), u)), (z - min((u - min(y, x)), w))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(((x + y) - select(z, (w + (u + x)), v)), (y - select(z, (w + u), (v - x)))) ||
 rewrite(((x + y) - select(z, (w + (u + y)), v)), (x - select(z, (w + u), (v - y)))) ||
 rewrite(((x + y) - select(z, (w + (x + u)), v)), (y - select(z, (w + u), (v - x)))) ||
 rewrite(((x + y) - select(z, (w + (y + u)), v)), (x - select(z, (w + u), (v - y)))) ||
 rewrite(((x + y) - select(z, ((w + x) + u), v)), (y - select(z, (w + u), (v - x)))) ||
 rewrite(((x + y) - select(z, ((w + y) + u), v)), (x - select(z, (w + u), (v - y)))) ||
 rewrite(((x + y) - select(z, ((x + w) + u), v)), (y - select(z, (u + w), (v - x)))) ||
 rewrite(((x + y) - select(z, ((y + w) + u), v)), (x - select(z, (u + w), (v - y)))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite((((x + y) - z) - x), (y - z)) ||
 rewrite((((x + y) - z) - y), (x - z)) ||
 rewrite(((min(x, (y + z)) - w) - y), (min(z, (x - y)) - w)) ||
 rewrite(((min(x, (y + z)) - w) - z), (min(y, (x - z)) - w)) ||
 rewrite(((min((x + y), z) - w) - x), (min((z - x), y) - w)) ||
 rewrite(((min((x + y), z) - w) - y), (min((z - y), x) - w)))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*c0) - ((y*c1) + (z + (y*c2)))), ((x*c0) - z), ((c1 + c2) == 0)) ||
 rewrite(((x*c0) - ((y*c1) + ((y*c2) + z))), ((x*c0) - z), ((c1 + c2) == 0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((x*c0) - min(y, min(z, (y + c1)))), ((x*c0) - min(y, z)), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min(z, min(w, (z + c1))))), ((x*c0) - min(y, min(w, z))), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min(z, min((z + c1), w)))), ((x*c0) - min(y, min(z, w))), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min((y + c1), z))), ((x*c0) - min(y, z)), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min(min(z, (w + c1)), w))), ((x*c0) - min(y, min(z, w))), (0 <= c1)) ||
 rewrite(((x*c0) - min(y, min(min((z + c1), w), z))), ((x*c0) - min(y, min(w, z))), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(y, (z + c1)), z)), ((x*c0) - min(y, z)), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(y, min(z, w)), (w + c1))), ((x*c0) - min(min(y, z), (w + c1))), (c1 <= 0)) ||
 rewrite(((x*c0) - min(min(y, min(z, w)), (z + c1))), ((x*c0) - min(min(y, w), (z + c1))), (c1 <= 0)) ||
 rewrite(((x*c0) - min(min(y, min(z, (y + c1))), w)), ((x*c0) - min(min(z, y), w)), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(y, min((y + c1), z)), w)), ((x*c0) - min(min(y, z), w)), (0 <= c1)) ||
 rewrite(((x*c0) - min(min((y + c1), z), y)), ((x*c0) - min(y, z)), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(min(y, z), w), (y + c1))), ((x*c0) - min(min(z, w), (y + c1))), (c1 <= 0)) ||
 rewrite(((x*c0) - min(min(min(y, z), w), (z + c1))), ((x*c0) - min(min(y, w), (z + c1))), (c1 <= 0)) ||
 rewrite(((x*c0) - min(min(min(y, (z + c1)), z), w)), ((x*c0) - min(min(y, z), w)), (0 <= c1)) ||
 rewrite(((x*c0) - min(min(min((y + c1), z), y), w)), ((x*c0) - min(min(z, y), w)), (0 <= c1)))))) ||
((a.node_type() == IRNodeType::Div) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite((((x + c0)/c1) - (y + (x/c1))), ((((x % c1) + c0)/c1) - y), prove_me((((((1 % c1) + c0)/c1) + ((1/c1) - ((c0 + 1)/c1))) == 0))) ||
 rewrite((((x + c0)/c1) - ((x/c1) + y)), ((((x % c1) + c0)/c1) - y), prove_me((((((1 % c1) + c0)/c1) + ((1/c1) - ((c0 + 1)/c1))) == 0))) ||
 rewrite((((x + y)/c0) - (z + (x/c0))), ((((x % c0) + y)/c0) - z), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x + y)/c0) - (z + (y/c0))), (((x + (y % c0))/c0) - z), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x + y)/c0) - ((x/c0) + z)), ((((x % c0) + y)/c0) - z), prove_me((((1 % c0)/c0) == 0))) ||
 rewrite((((x + y)/c0) - ((y/c0) + z)), (((x + (y % c0))/c0) - z), prove_me((((1 % c0)/c0) == 0))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((((x + y)/c0) - ((x + (y + z))/c0)), ((fold((-1 + c0)) - (((x + y) % c0) + z))/c0), prove_me((((((c0 + -1)/c0) == 0) && ((((c0 - (1 % c0)) + -1)/c0) == 0)) && ((((c0 + -2)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - ((x + (y + z))/c0)), ((fold((-1 - c0)) - (((x + y) % c0) + z))/c0), prove_me((((((-1 - c0)/c0) == 0) && ((((-1 - c0) - (1 % c0))/c0) == 0)) && ((((-2 - c0)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - ((x + (z + y))/c0)), (((fold((-1 + c0)) - z) - ((x + y) % c0))/c0), prove_me((((((c0 + -1)/c0) == 0) && ((((c0 - (1 % c0)) + -1)/c0) == 0)) && ((((c0 + -2)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - ((x + (z + y))/c0)), ((fold((-1 - c0)) - (((x + y) % c0) + z))/c0), prove_me((((((-1 - c0)/c0) == 0) && ((((-1 - c0) - (1 % c0))/c0) == 0)) && ((((-2 - c0)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - ((y + (x + z))/c0)), ((fold((-1 + c0)) - (((y + x) % c0) + z))/c0), prove_me((((((c0 + -1)/c0) == 0) && ((((c0 - (1 % c0)) + -1)/c0) == 0)) && ((((c0 + -2)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - ((y + (x + z))/c0)), ((fold((-1 - c0)) - (((y + x) % c0) + z))/c0), prove_me((((((-1 - c0)/c0) == 0) && ((((-1 - c0) - (1 % c0))/c0) == 0)) && ((((-2 - c0)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - ((y + (z + x))/c0)), (((fold((-1 + c0)) - z) - ((y + x) % c0))/c0), prove_me((((((c0 + -1)/c0) == 0) && ((((c0 - (1 % c0)) + -1)/c0) == 0)) && ((((c0 + -2)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - ((y + (z + x))/c0)), ((fold((-1 - c0)) - (((y + x) % c0) + z))/c0), prove_me((((((-1 - c0)/c0) == 0) && ((((-1 - c0) - (1 % c0))/c0) == 0)) && ((((-2 - c0)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - (((x + z) + y)/c0)), ((fold((-1 + c0)) - (((x + y) % c0) + z))/c0), prove_me((((((c0 + -1)/c0) == 0) && ((((c0 - (1 % c0)) + -1)/c0) == 0)) && ((((c0 + -2)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - (((x + z) + y)/c0)), ((fold((-1 - c0)) - (((x + y) % c0) + z))/c0), prove_me((((((-1 - c0)/c0) == 0) && ((((-1 - c0) - (1 % c0))/c0) == 0)) && ((((-2 - c0)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - (((y + z) + x)/c0)), ((fold((-1 + c0)) - (((y + x) % c0) + z))/c0), prove_me((((((c0 + -1)/c0) == 0) && ((((c0 - (1 % c0)) + -1)/c0) == 0)) && ((((c0 + -2)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - (((y + z) + x)/c0)), ((fold((-1 - c0)) - (((y + x) % c0) + z))/c0), prove_me((((((-1 - c0)/c0) == 0) && ((((-1 - c0) - (1 % c0))/c0) == 0)) && ((((-2 - c0)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - (((z + x) + y)/c0)), ((fold((-1 + c0)) - (((y + x) % c0) + z))/c0), prove_me((((((c0 + -1)/c0) == 0) && ((((c0 - (1 % c0)) + -1)/c0) == 0)) && ((((c0 + -2)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - (((z + x) + y)/c0)), ((fold((-1 - c0)) - (((y + x) % c0) + z))/c0), prove_me((((((-1 - c0)/c0) == 0) && ((((-1 - c0) - (1 % c0))/c0) == 0)) && ((((-2 - c0)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - (((z + y) + x)/c0)), ((fold((-1 + c0)) - (((x + y) % c0) + z))/c0), prove_me((((((c0 + -1)/c0) == 0) && ((((c0 - (1 % c0)) + -1)/c0) == 0)) && ((((c0 + -2)/c0) + (1/c0)) == 0)))) ||
 rewrite((((x + y)/c0) - (((z + y) + x)/c0)), ((fold((-1 - c0)) - (((x + y) % c0) + z))/c0), prove_me((((((-1 - c0)/c0) == 0) && ((((-1 - c0) - (1 % c0))/c0) == 0)) && ((((-2 - c0)/c0) + (1/c0)) == 0)))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((x/c0) - min(y, min(z, (min(w, y) + c1)))), ((x/c0) - min(z, (min(w, y) + c1))), (c1 <= 0)) ||
 rewrite(((x/c0) - min(y, min(z, (min(y, w) + c1)))), ((x/c0) - min(z, (min(y, w) + c1))), (c1 <= 0)) ||
 rewrite(((x/c0) - min(y, min((min(y, z) + c1), w))), ((x/c0) - min((min(y, z) + c1), w)), (c1 <= 0)) ||
 rewrite(((x/c0) - min(y, min((min(z, y) + c1), w))), ((x/c0) - min((min(z, y) + c1), w)), (c1 <= 0)) ||
 rewrite(((x/c0) - min(min(y, (min(z, w) + c1)), w)), ((x/c0) - min(y, (min(z, w) + c1))), (c1 <= 0)) ||
 rewrite(((x/c0) - min(min(y, (min(z, w) + c1)), z)), ((x/c0) - min(y, (min(z, w) + c1))), (c1 <= 0)) ||
 rewrite(((x/c0) - min(min(y, min(z, w)), (w + c1))), ((x/c0) - min(min(y, z), (w + c1))), (c1 <= 0)) ||
 rewrite(((x/c0) - min(min(y, min(z, w)), (z + c1))), ((x/c0) - min(min(y, w), (z + c1))), (c1 <= 0)) ||
 rewrite(((x/c0) - min(min((min(y, z) + c1), w), y)), ((x/c0) - min((min(y, z) + c1), w)), (c1 <= 0)) ||
 rewrite(((x/c0) - min(min((min(y, z) + c1), w), z)), ((x/c0) - min((min(y, z) + c1), w)), (c1 <= 0)) ||
 rewrite(((x/c0) - min(min(min(y, z), w), (y + c1))), ((x/c0) - min(min(z, w), (y + c1))), (c1 <= 0)) ||
 rewrite(((x/c0) - min(min(min(y, z), w), (z + c1))), ((x/c0) - min(min(y, w), (z + c1))), (c1 <= 0)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite((min(x, ((y + z) - w)) - y), min((z - w), (x - y))) ||
 rewrite((min(x, ((y + z) - w)) - z), min((y - w), (x - z))) ||
 rewrite((min(x, (((y + z) - w) - u)) - y), min(((z - u) - w), (x - y))) ||
 rewrite((min(x, (((y + z) - w) - u)) - z), min(((y - u) - w), (x - z))) ||
 rewrite((min(x, min(y, (z + (w + u)))) - u), min((min(y, x) - u), (w + z))) ||
 rewrite((min(x, min(y, (z + (w + u)))) - w), min((min(y, x) - w), (z + u))) ||
 rewrite((min(x, min(y, ((z + w) + u))) - w), min((min(y, x) - w), (u + z))) ||
 rewrite((min(x, min(y, ((z + w) + u))) - z), min((min(y, x) - z), (u + w))) ||
 rewrite((min(x, min(y, min(z, (w + (u + v))))) - u), min((min(y, min(z, x)) - u), (v + w))) ||
 rewrite((min(x, min(y, min(z, (w + (u + v))))) - v), min((min(y, min(z, x)) - v), (u + w))) ||
 rewrite((min(x, min(y, min(z, ((w + u) + v)))) - u), min((min(y, min(z, x)) - u), (v + w))) ||
 rewrite((min(x, min(y, min(z, ((w + u) + v)))) - w), min((min(y, min(z, x)) - w), (v + u))) ||
 rewrite((min(x, min(y, min((z + (w + u)), v))) - u), min((min(y, min(x, v)) - u), (w + z))) ||
 rewrite((min(x, min(y, min((z + (w + u)), v))) - w), min((min(y, min(x, v)) - w), (u + z))) ||
 rewrite((min(x, min(y, min(((z + w) + u), v))) - w), min((min(y, min(x, v)) - w), (z + u))) ||
 rewrite((min(x, min(y, min(((z + w) + u), v))) - z), min((min(y, min(x, v)) - z), (w + u))) ||
 rewrite((min(x, min((y + (z + w)), u)) - w), min((min(u, x) - w), (z + y))) ||
 rewrite((min(x, min((y + (z + w)), u)) - z), min((min(u, x) - z), (y + w))) ||
 rewrite((min(x, min(((y + z) + w), u)) - y), min((min(u, x) - y), (z + w))) ||
 rewrite((min(x, min(((y + z) + w), u)) - z), min((min(u, x) - z), (y + w))) ||
 rewrite((min(x, min(min(y, (z + (w + u))), v)) - u), min((min(min(y, x), v) - u), (w + z))) ||
 rewrite((min(x, min(min(y, (z + (w + u))), v)) - w), min((min(min(y, x), v) - w), (u + z))) ||
 rewrite((min(x, min(min(y, ((z + w) + u)), v)) - w), min((min(min(y, v), x) - w), (u + z))) ||
 rewrite((min(x, min(min(y, ((z + w) + u)), v)) - z), min((min(min(y, x), v) - z), (w + u))) ||
 rewrite((min(x, min(min((y + (z + w)), u), v)) - w), min((min(min(v, u), x) - w), (y + z))) ||
 rewrite((min(x, min(min((y + (z + w)), u), v)) - z), min((min(min(v, u), x) - z), (y + w))) ||
 rewrite((min(x, min(min(((y + z) + w), u), v)) - y), min((min(min(x, u), v) - y), (z + w))) ||
 rewrite((min(x, min(min(((y + z) + w), u), v)) - z), min((min(min(v, u), x) - z), (w + y))) ||
 rewrite((min(((x + y) - z), w) - x), min((y - z), (w - x))) ||
 rewrite((min(((x + y) - z), w) - y), min((x - z), (w - y))) ||
 rewrite((min((((x + y) - z) - w), u) - x), min(((y - z) - w), (u - x))) ||
 rewrite((min((((x + y) - z) - w), u) - y), min(((x - z) - w), (u - y))) ||
 rewrite((min(min(x, (y + (z + w))), u) - w), min((min(x, u) - w), (z + y))) ||
 rewrite((min(min(x, (y + (z + w))), u) - z), min((min(x, u) - z), (w + y))) ||
 rewrite((min(min(x, ((y + z) + w)), u) - y), min((min(x, u) - y), (w + z))) ||
 rewrite((min(min(x, ((y + z) + w)), u) - z), min((min(x, u) - z), (w + y))) ||
 rewrite((min(min(x, min(y, (z + (w + u)))), v) - u), min((min(x, min(y, v)) - u), (z + w))) ||
 rewrite((min(min(x, min(y, (z + (w + u)))), v) - w), min((min(x, min(y, v)) - w), (z + u))) ||
 rewrite((min(min(x, min(y, ((z + w) + u))), v) - w), min((min(x, min(y, v)) - w), (z + u))) ||
 rewrite((min(min(x, min(y, ((z + w) + u))), v) - z), min((min(x, min(y, v)) - z), (w + u))) ||
 rewrite((min(min(x, min((y + (z + w)), u)), v) - w), min((min(x, min(v, u)) - w), (z + y))) ||
 rewrite((min(min(x, min((y + (z + w)), u)), v) - z), min((min(x, min(v, u)) - z), (y + w))) ||
 rewrite((min(min(x, min(((y + z) + w), u)), v) - y), min((min(x, min(v, u)) - y), (z + w))) ||
 rewrite((min(min(x, min(((y + z) + w), u)), v) - z), min((min(x, min(v, u)) - z), (w + y))) ||
 rewrite((min(min((x + (y + z)), w), u) - y), min((min(w, u) - y), (x + z))) ||
 rewrite((min(min((x + (y + z)), w), u) - z), min((min(w, u) - z), (x + y))) ||
 rewrite((min(min(((x + y) + z), w), u) - x), min((min(w, u) - x), (z + y))) ||
 rewrite((min(min(((x + y) + z), w), u) - y), min((min(w, u) - y), (z + x))) ||
 rewrite((min(min(min(x, (y + (z + w))), u), v) - w), min((min(min(x, v), u) - w), (z + y))) ||
 rewrite((min(min(min(x, (y + (z + w))), u), v) - z), min((min(min(x, u), v) - z), (y + w))) ||
 rewrite((min(min(min(x, ((y + z) + w)), u), v) - y), min((min(min(x, u), v) - y), (w + z))) ||
 rewrite((min(min(min(x, ((y + z) + w)), u), v) - z), min((min(min(x, u), v) - z), (w + y))) ||
 rewrite((min(min(min((x + (y + z)), w), u), v) - y), min((min(min(v, u), w) - y), (x + z))) ||
 rewrite((min(min(min((x + (y + z)), w), u), v) - z), min((min(min(u, v), w) - z), (y + x))) ||
 rewrite((min(min(min(((x + y) + z), w), u), v) - x), min((min(min(v, w), u) - x), (y + z))) ||
 rewrite((min(min(min(((x + y) + z), w), u), v) - y), min((min(min(v, w), u) - y), (x + z))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(x, y) - (z + (w + min(y, x)))), (0 - (z + w))) ||
 rewrite((min(x, y) - (z + (min(y, x) + w))), (0 - (z + w))) ||
 rewrite((min(x, y) - ((z + min(y, x)) + w)), (0 - (z + w))) ||
 rewrite((min(x, y) - ((min(y, x) + z) + w)), (0 - (z + w))) ||
 rewrite((min(x, (y + (z + w))) - (u + w)), (min((x - w), (z + y)) - u)) ||
 rewrite((min(x, (y + (z + w))) - (u + z)), (min((x - z), (y + w)) - u)) ||
 rewrite((min(x, (y + (z + w))) - (w + u)), (min((x - w), (z + y)) - u)) ||
 rewrite((min(x, (y + (z + w))) - (z + u)), (min((x - z), (y + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (u + y)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (u + z)), (min((x - z), (y + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (y + u)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (z + u)), (min((x - z), (y + w)) - u)) ||
 rewrite((min(x, ((y + z) - w)) - (y + z)), (min((x - z), (y - w)) - y)) ||
 rewrite((min(x, ((y + z) - w)) - (z + y)), (min((x - y), (z - w)) - z)) ||
 rewrite((min(x, min(y, ((z + w) - u))) - (w + z)), (min((min(y, x) - z), (w - u)) - w)) ||
 rewrite((min(x, min(y, ((z + w) - u))) - (z + w)), (min((min(y, x) - w), (z - u)) - z)) ||
 rewrite((min(x, min(((y + z) - w), u)) - (y + z)), (min((min(u, x) - y), (z - w)) - z)) ||
 rewrite((min(x, min(((y + z) - w), u)) - (z + y)), (min((min(u, x) - z), (y - w)) - y)) ||
 rewrite((min((x + (y + z)), w) - (u + y)), (min((w - y), (z + x)) - u)) ||
 rewrite((min((x + (y + z)), w) - (u + z)), (min((w - z), (y + x)) - u)) ||
 rewrite((min((x + (y + z)), w) - (y + u)), (min((w - y), (z + x)) - u)) ||
 rewrite((min((x + (y + z)), w) - (z + u)), (min((w - z), (y + x)) - u)) ||
 rewrite((min(((x + y) + z), w) - (u + x)), (min((w - x), (y + z)) - u)) ||
 rewrite((min(((x + y) + z), w) - (u + y)), (min((w - y), (x + z)) - u)) ||
 rewrite((min(((x + y) + z), w) - (x + u)), (min((w - x), (z + y)) - u)) ||
 rewrite((min(((x + y) + z), w) - (y + u)), (min((w - y), (z + x)) - u)) ||
 rewrite((min(((x + y) - z), w) - (x + y)), (min((x - z), (w - y)) - x)) ||
 rewrite((min(((x + y) - z), w) - (y + x)), (min((y - z), (w - x)) - y)) ||
 rewrite((min(min(x, ((y + z) - w)), u) - (y + z)), (min((min(x, u) - z), (y - w)) - y)) ||
 rewrite((min(min(x, ((y + z) - w)), u) - (z + y)), (min((min(x, u) - y), (z - w)) - z)) ||
 rewrite((min(min(((x + y) - z), w), u) - (x + y)), (min((min(w, u) - x), (y - z)) - y)) ||
 rewrite((min(min(((x + y) - z), w), u) - (y + x)), (min((min(w, u) - y), (x - z)) - x)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((min(x, (y + ((z + w)*c0))) - (w*c0)), min((x - (w*c0)), (y + (z*c0)))) ||
 rewrite((min(x, (y + ((z + w)*c0))) - (z*c0)), min((x - (z*c0)), (y + (w*c0)))) ||
 rewrite((min(x, (((y + z)*c0) + w)) - (y*c0)), min((x - (y*c0)), (w + (z*c0)))) ||
 rewrite((min(x, (((y + z)*c0) + w)) - (z*c0)), min((x - (z*c0)), (w + (y*c0)))) ||
 rewrite((min((x + ((y + z)*c0)), w) - (y*c0)), min((w - (y*c0)), (x + (z*c0)))) ||
 rewrite((min((x + ((y + z)*c0)), w) - (z*c0)), min((w - (z*c0)), (x + (y*c0)))) ||
 rewrite((min((((x + y)*c0) + z), w) - (x*c0)), min((w - (x*c0)), (z + (y*c0)))) ||
 rewrite((min((((x + y)*c0) + z), w) - (y*c0)), min((w - (y*c0)), (z + (x*c0)))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite((max(x, (y + z)) - (w + y)), (max(z, (x - y)) - w)) ||
 rewrite((max(x, (y + z)) - (w + z)), (max(y, (x - z)) - w)) ||
 rewrite((max(x, (y + z)) - (y + w)), (max(z, (x - y)) - w)) ||
 rewrite((max(x, (y + z)) - (z + w)), (max(y, (x - z)) - w)) ||
 rewrite((max(x, (y + (z + w))) - (u + w)), (max((x - w), (z + y)) - u)) ||
 rewrite((max(x, (y + (z + w))) - (u + z)), (max((x - z), (y + w)) - u)) ||
 rewrite((max(x, (y + (z + w))) - (u + (w + z))), (max((x - z), (y + w)) - (u + w))) ||
 rewrite((max(x, (y + (z + w))) - (w + u)), (max((x - w), (z + y)) - u)) ||
 rewrite((max(x, (y + (z + w))) - (z + u)), (max((x - z), (y + w)) - u)) ||
 rewrite((max(x, (y + (z + w))) - ((w + z) + u)), (max((x - z), (y + w)) - (w + u))) ||
 rewrite((max(x, (y + (z + (w + u)))) - (u + v)), (max((x - u), (z + (w + y))) - v)) ||
 rewrite((max(x, (y + (z + (w + u)))) - (v + u)), (max((x - u), (z + (w + y))) - v)) ||
 rewrite((max(x, (y + (z + (w + u)))) - (v + w)), (max((x - w), (z + (y + u))) - v)) ||
 rewrite((max(x, (y + (z + (w + u)))) - (w + v)), (max((x - w), (z + (y + u))) - v)) ||
 rewrite((max(x, (y + ((z + w) + u))) - (v + w)), (max((x - w), ((z + y) + u)) - v)) ||
 rewrite((max(x, (y + ((z + w) + u))) - (v + z)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, (y + ((z + w) + u))) - (w + v)), (max((x - w), ((z + y) + u)) - v)) ||
 rewrite((max(x, (y + ((z + w) + u))) - (z + v)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, ((y + z) + w)) - (u + y)), (max((x - y), (z + w)) - u)) ||
 rewrite((max(x, ((y + z) + w)) - (u + z)), (max((x - z), (y + w)) - u)) ||
 rewrite((max(x, ((y + z) + w)) - (u + (z + y))), (max((x - z), (y + w)) - (u + y))) ||
 rewrite((max(x, ((y + z) + w)) - (y + u)), (max((x - y), (z + w)) - u)) ||
 rewrite((max(x, ((y + z) + w)) - (z + u)), (max((x - z), (y + w)) - u)) ||
 rewrite((max(x, ((y + z) + w)) - ((z + y) + u)), (max((x - z), (y + w)) - (y + u))) ||
 rewrite((max(x, ((y + (z + w)) + u)) - (v + w)), (max((x - w), ((z + y) + u)) - v)) ||
 rewrite((max(x, ((y + (z + w)) + u)) - (v + z)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, ((y + (z + w)) + u)) - (w + v)), (max((x - w), ((z + y) + u)) - v)) ||
 rewrite((max(x, ((y + (z + w)) + u)) - (z + v)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, (((y + z) + w) + u)) - (v + y)), (max((x - y), ((z + w) + u)) - v)) ||
 rewrite((max(x, (((y + z) + w) + u)) - (v + z)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max(x, (((y + z) + w) + u)) - (y + v)), (max((x - y), ((z + w) + u)) - v)) ||
 rewrite((max(x, (((y + z) + w) + u)) - (z + v)), (max((x - z), ((y + w) + u)) - v)) ||
 rewrite((max((x + y), z) - (w + x)), (max((z - x), y) - w)) ||
 rewrite((max((x + y), z) - (w + y)), (max((z - y), x) - w)) ||
 rewrite((max((x + y), z) - (x + w)), (max((z - x), y) - w)) ||
 rewrite((max((x + y), z) - (y + w)), (max((z - y), x) - w)) ||
 rewrite((max((x + (y + z)), w) - (u + y)), (max((w - y), (x + z)) - u)) ||
 rewrite((max((x + (y + z)), w) - (u + z)), (max((w - z), (y + x)) - u)) ||
 rewrite((max((x + (y + z)), w) - (u + (z + y))), (max((w - y), (z + x)) - (z + u))) ||
 rewrite((max((x + (y + z)), w) - (y + u)), (max((w - y), (z + x)) - u)) ||
 rewrite((max((x + (y + z)), w) - (z + u)), (max((w - z), (x + y)) - u)) ||
 rewrite((max((x + (y + z)), w) - ((z + y) + u)), (max((w - z), (y + x)) - (y + u))) ||
 rewrite((max((x + (y + (z + w))), u) - (v + w)), (max((u - w), (y + (z + x))) - v)) ||
 rewrite((max((x + (y + (z + w))), u) - (v + z)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((x + (y + (z + w))), u) - (w + v)), (max((u - w), (y + (z + x))) - v)) ||
 rewrite((max((x + (y + (z + w))), u) - (z + v)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((x + ((y + z) + w)), u) - (v + y)), (max((u - y), ((x + z) + w)) - v)) ||
 rewrite((max((x + ((y + z) + w)), u) - (v + z)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((x + ((y + z) + w)), u) - (y + v)), (max((u - y), ((x + z) + w)) - v)) ||
 rewrite((max((x + ((y + z) + w)), u) - (z + v)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max(((x + y) + z), w) - (u + x)), (max((w - x), (y + z)) - u)) ||
 rewrite((max(((x + y) + z), w) - (u + y)), (max((w - y), (x + z)) - u)) ||
 rewrite((max(((x + y) + z), w) - (u + (y + x))), (max((w - x), (y + z)) - (u + y))) ||
 rewrite((max(((x + y) + z), w) - (x + u)), (max((w - x), (y + z)) - u)) ||
 rewrite((max(((x + y) + z), w) - (y + u)), (max((w - y), (z + x)) - u)) ||
 rewrite((max(((x + y) + z), w) - ((y + x) + u)), (max((w - y), (x + z)) - (x + u))) ||
 rewrite((max(((x + (y + z)) + w), u) - (v + y)), (max((u - y), ((x + z) + w)) - v)) ||
 rewrite((max(((x + (y + z)) + w), u) - (v + z)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max(((x + (y + z)) + w), u) - (y + v)), (max((u - y), ((z + w) + x)) - v)) ||
 rewrite((max(((x + (y + z)) + w), u) - (z + v)), (max((u - z), (y + (x + w))) - v)) ||
 rewrite((max((((x + y) + z) + w), u) - (v + x)), (max((u - x), ((y + z) + w)) - v)) ||
 rewrite((max((((x + y) + z) + w), u) - (v + y)), (max((u - y), ((z + w) + x)) - v)) ||
 rewrite((max((((x + y) + z) + w), u) - (x + v)), (max((u - x), ((z + w) + y)) - v)) ||
 rewrite((max((((x + y) + z) + w), u) - (y + v)), (max((u - y), ((z + w) + x)) - v)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, (y + (z + w)), u) - w), select(x, (y + z), (u - w))) ||
 rewrite((select(x, (y + (z + w)), u) - z), select(x, (y + w), (u - z))) ||
 rewrite((select(x, ((y + z) + w), u) - y), select(x, (z + w), (u - y))) ||
 rewrite((select(x, ((y + z) + w), u) - z), select(x, (y + w), (u - z)))))
