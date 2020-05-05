((b.node_type() == IRNodeType::Add) && (
 rewrite(max(x, (min(x, y) + c0)), x, (c0 <= 0)) ||
 rewrite(max(x, (min(y, x) + c0)), x, (c0 <= 0)) ||
 rewrite(max(x, (max(y, c0) + ((z*c1) + (w - z)))), max(x, (max(y, c0) + (z + w))), (max((1 - max(c0, 0)), c1) == 2)) ||
 rewrite(max(x, (max(y, min(x, z)) + c0)), max(x, (y + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(y, min(z, x)) + c0)), max(x, (y + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(y, max(x, z)) + c0)), (max(y, max(x, z)) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(z, x)) + c0)), (max(y, max(z, x)) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(z, max(w, x))) + c0)), (max(y, max(z, max(w, x))) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(z, max(w, x))) + c0)), max(x, (max(y, max(z, w)) + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(y, max(z, max(w, max(u, x)))) + c0)), (max(y, max(z, max(w, max(u, x)))) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(z, max(w, max(x, u)))) + c0)), (max(y, max(z, max(w, max(x, u)))) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(z, max(x, w))) + c0)), (max(y, max(z, max(x, w))) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(z, max(x, w))) + c0)), max(x, (max(y, max(z, w)) + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(y, max(z, max(max(w, x), u))) + c0)), (max(y, max(z, max(max(w, x), u))) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(z, max(max(x, w), u))) + c0)), (max(y, max(z, max(max(x, w), u))) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(max(x, z), w)) + c0)), (max(y, max(max(x, z), w)) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(max(x, z), w)) + c0)), max(x, (max(y, max(z, w)) + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(y, max(max(z, x), w)) + c0)), (max(y, max(max(z, x), w)) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(max(z, x), w)) + c0)), max(x, (max(y, max(z, w)) + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(y, max(max(z, max(w, x)), u)) + c0)), (max(y, max(max(z, max(w, x)), u)) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(max(z, max(x, w)), u)) + c0)), (max(y, max(max(z, max(x, w)), u)) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(max(max(x, z), w), u)) + c0)), (max(y, max(max(max(x, z), w), u)) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(y, max(max(max(z, x), w), u)) + c0)), (max(y, max(max(max(z, x), w), u)) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(min(x, y), z) + c0)), max(x, (z + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(min(y, x), z) + c0)), max(x, (z + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(max(x, y), z) + c0)), (max(max(x, y), z) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(x, y), (z*c0)) + c1)), (max(max(x, y), (z*c0)) + c1), (0 <= c1)) ||
 rewrite(max(x, (max(max(y, x), z) + c0)), (max(max(y, x), z) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(y, x), (z*c0)) + c1)), (max(max(y, x), (z*c0)) + c1), (0 <= c1)) ||
 rewrite(max(x, (max(max(y, max(x, z)), w) + c0)), (max(max(y, max(x, z)), w) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(y, max(x, z)), w) + c0)), max(x, (max(max(y, z), w) + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(max(y, max(z, x)), w) + c0)), (max(max(y, max(z, x)), w) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(y, max(z, x)), w) + c0)), max(x, (max(max(y, z), w) + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(max(y, max(z, max(w, x))), u) + c0)), (max(max(y, max(z, max(w, x))), u) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(y, max(z, max(x, w))), u) + c0)), (max(max(y, max(z, max(x, w))), u) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(y, max(max(x, z), w)), u) + c0)), (max(max(y, max(max(x, z), w)), u) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(y, max(max(z, x), w)), u) + c0)), (max(max(y, max(max(z, x), w)), u) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(max(x, y), z), w) + c0)), (max(max(max(x, y), z), w) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(max(x, y), z), w) + c0)), max(x, (max(max(y, z), w) + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(max(max(y, x), z), w) + c0)), (max(max(max(y, x), z), w) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(max(y, x), z), w) + c0)), max(x, (max(max(y, z), w) + c0)), (c0 <= 0)) ||
 rewrite(max(x, (max(max(max(y, max(x, z)), w), u) + c0)), (max(max(max(y, max(x, z)), w), u) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(max(y, max(z, x)), w), u) + c0)), (max(max(max(y, max(z, x)), w), u) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(max(max(x, y), z), w), u) + c0)), (max(max(max(max(x, y), z), w), u) + c0), (0 <= c0)) ||
 rewrite(max(x, (max(max(max(max(y, x), z), w), u) + c0)), (max(max(max(max(y, x), z), w), u) + c0), (0 <= c0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(max(x, max(y, (max(x, z) + c0))), max(y, (max(x, z) + c0)), (0 <= c0)) ||
 rewrite(max(x, max(y, (max(z, x) + c0))), max(y, (max(z, x) + c0)), (0 <= c0)) ||
 rewrite(max(x, max(y, (max(z, max(x, y)) + c0))), max(x, max(y, (max(z, x) + c0))), (c0 <= 0)) ||
 rewrite(max(x, max(y, (max(z, max(y, x)) + c0))), max(x, max(y, (max(z, x) + c0))), (c0 <= 0)) ||
 rewrite(max(x, max(y, (max(max(x, y), z) + c0))), max(x, max(y, (max(x, z) + c0))), (c0 <= 0)) ||
 rewrite(max(x, max(y, (max(max(y, x), z) + c0))), max(x, max(y, (max(x, z) + c0))), (c0 <= 0)) ||
 rewrite(max(x, max(y, max(x, z))), max(y, max(x, z))) ||
 rewrite(max(x, max(y, max(z, x))), max(y, max(z, x))) ||
 rewrite(max(x, max(y, max(z, max(w, x)))), max(x, max(y, max(z, w)))) ||
 rewrite(max(x, max(y, max(z, max(w, (max(u, x) + c0))))), max(y, max(z, max(w, (max(u, x) + c0)))), (0 <= c0)) ||
 rewrite(max(x, max(y, max(z, max(w, (max(x, u) + c0))))), max(y, max(z, max(w, (max(x, u) + c0)))), (0 <= c0)) ||
 rewrite(max(x, max(y, max(z, max(w, max(u, x))))), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(u, max(v, x)))))), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(u, max(x, v)))))), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(x, u))))), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(max(u, x), v))))), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(max(x, u), v))))), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(x, max(y, max(z, max(x, w)))), max(x, max(y, max(z, w)))) ||
 rewrite(max(x, max(y, max(z, max((max(w, x) + c0), u)))), max(y, max(z, max((max(w, x) + c0), u))), (0 <= c0)) ||
 rewrite(max(x, max(y, max(z, max((max(x, w) + c0), u)))), max(y, max(z, max((max(x, w) + c0), u))), (0 <= c0)) ||
 rewrite(max(x, max(y, max(z, max(max(w, x), u)))), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(x, max(y, max(z, max(max(w, max(u, x)), v)))), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(x, max(y, max(z, max(max(w, max(x, u)), v)))), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(x, max(y, max(z, max(max(x, w), u)))), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(x, max(y, max(z, max(max(max(w, x), u), v)))), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(x, max(y, max(z, max(max(max(x, w), u), v)))), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(x, max(y, max(max(x, z), w))), max(x, max(y, max(z, w)))) ||
 rewrite(max(x, max(y, max(max(z, x), w))), max(x, max(y, max(z, w)))) ||
 rewrite(max(x, max(y, max(max(z, (max(w, x) + c0)), u))), max(y, max(max(z, (max(w, x) + c0)), u)), (0 <= c0)) ||
 rewrite(max(x, max(y, max(max(z, (max(x, w) + c0)), u))), max(y, max(max(z, (max(x, w) + c0)), u)), (0 <= c0)) ||
 rewrite(max(x, max(y, max(max(z, max(w, x)), u))), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(x, max(y, max(max(z, max(w, max(u, x))), v))), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(x, max(y, max(max(z, max(w, max(x, u))), v))), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(x, max(y, max(max(z, max(x, w)), u))), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(x, max(y, max(max(z, max(max(w, x), u)), v))), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(x, max(y, max(max(z, max(max(x, w), u)), v))), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(x, max(y, max(max((max(x, z) + c0), w), u))), max(y, max(max((max(x, z) + c0), w), u)), (0 <= c0)) ||
 rewrite(max(x, max(y, max(max((max(z, x) + c0), w), u))), max(y, max(max((max(z, x) + c0), w), u)), (0 <= c0)) ||
 rewrite(max(x, max(y, max(max(max(x, z), w), u))), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(x, max(y, max(max(max(z, x), w), u))), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(x, max(y, max(max(max(z, max(w, x)), u), v))), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(x, max(y, max(max(max(z, max(x, w)), u), v))), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(x, max(y, max(max(max(max(x, z), w), u), v))), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(x, max(y, max(max(max(max(z, x), w), u), v))), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(x, max((max(x, y) + c0), z)), max((max(x, y) + c0), z), (0 <= c0)) ||
 rewrite(max(x, max((max(y, x) + c0), z)), max((max(y, x) + c0), z), (0 <= c0)) ||
 rewrite(max(x, max((max(y, max(x, z)) + c0), z)), max(x, max((max(y, x) + c0), z)), (c0 <= 0)) ||
 rewrite(max(x, max((max(y, max(z, x)) + c0), z)), max(x, max((max(y, x) + c0), z)), (c0 <= 0)) ||
 rewrite(max(x, max((max(max(x, y), z) + c0), y)), max(x, max((max(x, z) + c0), y)), (c0 <= 0)) ||
 rewrite(max(x, max((max(max(y, x), z) + c0), y)), max(x, max((max(x, z) + c0), y)), (c0 <= 0)) ||
 rewrite(max(x, max((y*c0), (max(x, z) + c1))), max((y*c0), (max(x, z) + c1)), (0 <= c1)) ||
 rewrite(max(x, max((y*c0), (max(z, x) + c1))), max((y*c0), (max(z, x) + c1)), (0 <= c1)) ||
 rewrite(max(x, max(max(x, y), z)), max(x, max(y, z))) ||
 rewrite(max(x, max(max(y, x), z)), max(x, max(y, z))) ||
 rewrite(max(x, max(max(y, max(x, z)), w)), max(x, max(max(y, z), w))) ||
 rewrite(max(x, max(max(y, max(z, x)), w)), max(x, max(max(y, z), w))) ||
 rewrite(max(x, max(max(y, max(z, (max(w, x) + c0))), u)), max(max(y, max(z, (max(w, x) + c0))), u), (0 <= c0)) ||
 rewrite(max(x, max(max(y, max(z, (max(x, w) + c0))), u)), max(max(y, max(z, (max(x, w) + c0))), u), (0 <= c0)) ||
 rewrite(max(x, max(max(y, max(z, max(w, x))), u)), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(x, max(max(y, max(z, max(w, max(u, x)))), v)), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(x, max(max(y, max(z, max(w, max(x, u)))), v)), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(x, max(max(y, max(z, max(x, w))), u)), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(x, max(max(y, max(z, max(max(w, x), u))), v)), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(x, max(max(y, max(z, max(max(x, w), u))), v)), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(x, max(max(y, max((max(x, z) + c0), w)), u)), max(max(y, max((max(x, z) + c0), w)), u), (0 <= c0)) ||
 rewrite(max(x, max(max(y, max((max(z, x) + c0), w)), u)), max(max(y, max((max(z, x) + c0), w)), u), (0 <= c0)) ||
 rewrite(max(x, max(max(y, max(max(x, z), w)), u)), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(x, max(max(y, max(max(z, x), w)), u)), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(x, max(max(y, max(max(z, max(w, x)), u)), v)), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(x, max(max(y, max(max(z, max(x, w)), u)), v)), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(x, max(max(y, max(max(max(x, z), w), u)), v)), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(x, max(max(y, max(max(max(z, x), w), u)), v)), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(x, max(max(max(x, y), z), w)), max(x, max(max(y, z), w))) ||
 rewrite(max(x, max(max(max(y, x), z), w)), max(x, max(max(y, z), w))) ||
 rewrite(max(x, max(max(max(y, (max(x, z) + c0)), w), u)), max(max(max(y, (max(x, z) + c0)), w), u), (0 <= c0)) ||
 rewrite(max(x, max(max(max(y, (max(z, x) + c0)), w), u)), max(max(max(y, (max(z, x) + c0)), w), u), (0 <= c0)) ||
 rewrite(max(x, max(max(max(y, max(x, z)), w), u)), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(x, max(max(max(y, max(z, x)), w), u)), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(x, max(max(max(y, max(z, max(w, x))), u), v)), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(x, max(max(max(y, max(z, max(x, w))), u), v)), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(x, max(max(max(y, max(max(x, z), w)), u), v)), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(x, max(max(max(y, max(max(z, x), w)), u), v)), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(x, max(max(max((max(x, y) + c0), z), w), u)), max(max(max((max(x, y) + c0), z), w), u), (0 <= c0)) ||
 rewrite(max(x, max(max(max((max(y, x) + c0), z), w), u)), max(max(max((max(y, x) + c0), z), w), u), (0 <= c0)) ||
 rewrite(max(x, max(max(max(max(x, y), z), w), u)), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(x, max(max(max(max(y, x), z), w), u)), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(x, max(max(max(max(y, max(x, z)), w), u), v)), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(x, max(max(max(max(y, max(z, x)), w), u), v)), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(x, max(max(max(max(max(x, y), z), w), u), v)), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(x, max(max(max(max(max(y, x), z), w), u), v)), max(x, max(max(max(max(y, z), w), u), v))))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(max((min(x, y) + c0), x), x, (c0 <= 0)) ||
 rewrite(max((min(x, y) + c0), y), y, (c0 <= 0)) ||
 rewrite(max((max(x, c0) + ((y*c1) + (z - y))), w), max((max(x, c0) + (y + z)), w), (max((1 - max(c0, 0)), c1) == 2)) ||
 rewrite(max((max(x, min(y, z)) + c0), y), max((x + c0), y), (c0 <= 0)) ||
 rewrite(max((max(x, min(y, z)) + c0), z), max((x + c0), z), (c0 <= 0)) ||
 rewrite(max((max(x, max(y, z)) + c0), y), (max(x, max(y, z)) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(y, z)) + c0), z), (max(x, max(y, z)) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(y, max(z, w))) + c0), w), (max(x, max(y, max(z, w))) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(y, max(z, w))) + c0), w), max((max(x, max(y, z)) + c0), w), (c0 <= 0)) ||
 rewrite(max((max(x, max(y, max(z, w))) + c0), z), (max(x, max(y, max(z, w))) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(y, max(z, w))) + c0), z), max((max(x, max(y, w)) + c0), z), (c0 <= 0)) ||
 rewrite(max((max(x, max(y, max(z, max(w, u)))) + c0), u), (max(x, max(y, max(z, max(w, u)))) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(y, max(z, max(w, u)))) + c0), w), (max(x, max(y, max(z, max(w, u)))) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(y, max(max(z, w), u))) + c0), w), (max(x, max(y, max(max(z, w), u))) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(y, max(max(z, w), u))) + c0), z), (max(x, max(y, max(max(z, w), u))) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(max(y, z), w)) + c0), y), (max(x, max(max(y, z), w)) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(max(y, z), w)) + c0), y), max((max(x, max(z, w)) + c0), y), (c0 <= 0)) ||
 rewrite(max((max(x, max(max(y, z), w)) + c0), z), (max(x, max(max(y, z), w)) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(max(y, z), w)) + c0), z), max((max(x, max(y, w)) + c0), z), (c0 <= 0)) ||
 rewrite(max((max(x, max(max(y, max(z, w)), u)) + c0), w), (max(x, max(max(y, max(z, w)), u)) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(max(y, max(z, w)), u)) + c0), z), (max(x, max(max(y, max(z, w)), u)) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(max(max(y, z), w), u)) + c0), y), (max(x, max(max(max(y, z), w), u)) + c0), (0 <= c0)) ||
 rewrite(max((max(x, max(max(max(y, z), w), u)) + c0), z), (max(x, max(max(max(y, z), w), u)) + c0), (0 <= c0)) ||
 rewrite(max((max(min(x, y), z) + c0), x), max((z + c0), x), (c0 <= 0)) ||
 rewrite(max((max(min(x, y), z) + c0), y), max((z + c0), y), (c0 <= 0)) ||
 rewrite(max((max(max(x, y), z) + c0), x), (max(max(x, y), z) + c0), (0 <= c0)) ||
 rewrite(max((max(max(x, y), z) + c0), y), (max(max(x, y), z) + c0), (0 <= c0)) ||
 rewrite(max((max(max(x, y), (z*c0)) + c1), x), (max(max(x, y), (z*c0)) + c1), (0 <= c1)) ||
 rewrite(max((max(max(x, y), (z*c0)) + c1), y), (max(max(x, y), (z*c0)) + c1), (0 <= c1)) ||
 rewrite(max((max(max(x, max(y, z)), w) + c0), y), (max(max(x, max(y, z)), w) + c0), (0 <= c0)) ||
 rewrite(max((max(max(x, max(y, z)), w) + c0), y), max((max(max(x, z), w) + c0), y), (c0 <= 0)) ||
 rewrite(max((max(max(x, max(y, z)), w) + c0), z), (max(max(x, max(y, z)), w) + c0), (0 <= c0)) ||
 rewrite(max((max(max(x, max(y, z)), w) + c0), z), max((max(max(x, y), w) + c0), z), (c0 <= 0)) ||
 rewrite(max((max(max(x, max(y, max(z, w))), u) + c0), w), (max(max(x, max(y, max(z, w))), u) + c0), (0 <= c0)) ||
 rewrite(max((max(max(x, max(y, max(z, w))), u) + c0), z), (max(max(x, max(y, max(z, w))), u) + c0), (0 <= c0)) ||
 rewrite(max((max(max(x, max(max(y, z), w)), u) + c0), y), (max(max(x, max(max(y, z), w)), u) + c0), (0 <= c0)) ||
 rewrite(max((max(max(x, max(max(y, z), w)), u) + c0), z), (max(max(x, max(max(y, z), w)), u) + c0), (0 <= c0)) ||
 rewrite(max((max(max(max(x, y), z), w) + c0), x), (max(max(max(x, y), z), w) + c0), (0 <= c0)) ||
 rewrite(max((max(max(max(x, y), z), w) + c0), x), max((max(max(y, z), w) + c0), x), (c0 <= 0)) ||
 rewrite(max((max(max(max(x, y), z), w) + c0), y), (max(max(max(x, y), z), w) + c0), (0 <= c0)) ||
 rewrite(max((max(max(max(x, y), z), w) + c0), y), max((max(max(x, z), w) + c0), y), (c0 <= 0)) ||
 rewrite(max((max(max(max(x, max(y, z)), w), u) + c0), y), (max(max(max(x, max(y, z)), w), u) + c0), (0 <= c0)) ||
 rewrite(max((max(max(max(x, max(y, z)), w), u) + c0), z), (max(max(max(x, max(y, z)), w), u) + c0), (0 <= c0)) ||
 rewrite(max((max(max(max(max(x, y), z), w), u) + c0), x), (max(max(max(max(x, y), z), w), u) + c0), (0 <= c0)) ||
 rewrite(max((max(max(max(max(x, y), z), w), u) + c0), y), (max(max(max(max(x, y), z), w), u) + c0), (0 <= c0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x + y), (z + (w + (u + x)))), (x + max(y, (w + (u + z))))) ||
 rewrite(max((x + y), (z + (w + (u + y)))), (y + max(x, (w + (u + z))))) ||
 rewrite(max((x + y), (z + (w + (x + u)))), (x + max(y, (w + (z + u))))) ||
 rewrite(max((x + y), (z + (w + (y + u)))), (y + max(x, (w + (z + u))))) ||
 rewrite(max((x + y), (z + ((w + x) + u))), (x + max(y, ((w + z) + u)))) ||
 rewrite(max((x + y), (z + ((w + y) + u))), (y + max(x, ((w + z) + u)))) ||
 rewrite(max((x + y), (z + ((x + w) + u))), (x + max(y, ((z + w) + u)))) ||
 rewrite(max((x + y), (z + ((y + w) + u))), (y + max(x, ((z + w) + u)))) ||
 rewrite(max((x + y), (z + (x - w))), (x + max(y, (z - w)))) ||
 rewrite(max((x + y), (z + (y - w))), (y + max(x, (z - w)))) ||
 rewrite(max((x + y), ((z + (w + x)) + u)), (x + max((z + (w + u)), y))) ||
 rewrite(max((x + y), ((z + (w + y)) + u)), (max(x, (z + (w + u))) + y)) ||
 rewrite(max((x + y), ((z + (x + w)) + u)), (max(y, ((z + u) + w)) + x)) ||
 rewrite(max((x + y), ((z + (y + w)) + u)), (max(x, ((z + u) + w)) + y)) ||
 rewrite(max((x + y), (((x + z) + w) + u)), (x + max(((u + z) + w), y))) ||
 rewrite(max((x + y), (((y + z) + w) + u)), (max(x, ((u + z) + w)) + y)) ||
 rewrite(max((x + y), (((z + x) + w) + u)), (x + max(((z + u) + w), y))) ||
 rewrite(max((x + y), (((z + y) + w) + u)), (max(x, ((z + u) + w)) + y)) ||
 rewrite(max((x + y), ((x - z) + w)), (x + max((w - z), y))) ||
 rewrite(max((x + y), ((y - z) + w)), (max(x, (w - z)) + y)) ||
 rewrite(max((x + (y + z)), (w + y)), (max((x + z), w) + y)) ||
 rewrite(max((x + (y + z)), (w + z)), (max((x + y), w) + z)) ||
 rewrite(max((x + (y + z)), (w + (u + y))), (max((x + z), (w + u)) + y)) ||
 rewrite(max((x + (y + z)), (w + (u + z))), (max((x + y), (w + u)) + z)) ||
 rewrite(max((x + (y + z)), (w + (y + u))), (max((x + z), (w + u)) + y)) ||
 rewrite(max((x + (y + z)), (w + (z + u))), (max((x + y), (w + u)) + z)) ||
 rewrite(max((x + (y + z)), (y + w)), (max((x + z), w) + y)) ||
 rewrite(max((x + (y + z)), (z + w)), (max((x + y), w) + z)) ||
 rewrite(max((x + (y + z)), ((w + y) + u)), (max((x + z), (w + u)) + y)) ||
 rewrite(max((x + (y + z)), ((w + z) + u)), (max((x + y), (w + u)) + z)) ||
 rewrite(max((x + (y + z)), ((y + w) + u)), (max((x + z), (u + w)) + y)) ||
 rewrite(max((x + (y + z)), ((z + w) + u)), (max((x + y), (u + w)) + z)) ||
 rewrite(max((x + (y + (z + w))), (u + w)), (max((x + (y + z)), u) + w)) ||
 rewrite(max((x + (y + (z + w))), (u + z)), (max((x + (y + w)), u) + z)) ||
 rewrite(max((x + (y + (z + w))), (u + (w + z))), (max((x + y), u) + (w + z))) ||
 rewrite(max((x + (y + (z + w))), (w + u)), (max((x + (y + z)), u) + w)) ||
 rewrite(max((x + (y + (z + w))), (z + u)), (max((x + (y + w)), u) + z)) ||
 rewrite(max((x + (y + (z + w))), ((w + z) + u)), (max((x + y), u) + (w + z))) ||
 rewrite(max((x + (y + (z - w))), (u + z)), (max((x + (y - w)), u) + z)) ||
 rewrite(max((x + (y + (z - w))), (z + u)), (max((x + (y - w)), u) + z)) ||
 rewrite(max((x + (y + (z*w))), (u + (w*z))), (max((x + y), u) + (w*z))) ||
 rewrite(max((x + (y + (z*w))), ((w*z) + u)), (max(u, (y + x)) + (w*z))) ||
 rewrite(max((x + ((y + z) + w)), (u + y)), (max((x + (z + w)), u) + y)) ||
 rewrite(max((x + ((y + z) + w)), (u + z)), (max((x + (y + w)), u) + z)) ||
 rewrite(max((x + ((y + z) + w)), (u + (z + y))), (max((x + w), u) + (z + y))) ||
 rewrite(max((x + ((y + z) + w)), (y + u)), (max((x + (z + w)), u) + y)) ||
 rewrite(max((x + ((y + z) + w)), (z + u)), (max((x + (y + w)), u) + z)) ||
 rewrite(max((x + ((y + z) + w)), ((z + y) + u)), (max((x + w), u) + (z + y))) ||
 rewrite(max((x + ((y - z) + w)), (u + y)), (max(u, ((x - z) + w)) + y)) ||
 rewrite(max((x + ((y - z) + w)), (y + u)), (max(u, ((x - z) + w)) + y)) ||
 rewrite(max((x + ((y*z) + w)), (u + (z*y))), (max((x + w), u) + (z*y))) ||
 rewrite(max((x + ((y*z) + w)), ((z*y) + u)), (max((x + w), u) + (z*y))) ||
 rewrite(max((x + (y - z)), (w + y)), (max((x - z), w) + y)) ||
 rewrite(max((x + (y - z)), (w + (u - z))), (max((x + y), (w + u)) - z)) ||
 rewrite(max((x + (y - z)), (y + w)), (max((x - z), w) + y)) ||
 rewrite(max((x + (y - z)), ((w - z) + u)), (max((x + y), (w + u)) - z)) ||
 rewrite(max((x + ((y + z) - w)), (u + y)), (max((x + (z - w)), u) + y)) ||
 rewrite(max((x + ((y + z) - w)), (u + z)), (max((x + (y - w)), u) + z)) ||
 rewrite(max((x + ((y + z) - w)), (y + u)), (max((x + (z - w)), u) + y)) ||
 rewrite(max((x + ((y + z) - w)), (z + u)), (max((x + (y - w)), u) + z)) ||
 rewrite(max((x + (y*c0)), (y + (y + z))), (max(x, z) + (y*c0)), ((2 <= c0) && ((c0*6) == 12))) ||
 rewrite(max((x + (y*c0)), (y + (z + y))), (max(x, z) + (y*c0)), ((2 <= c0) && ((c0*6) == 12))) ||
 rewrite(max((x + (y*c0)), ((y + z) + y)), (max(z, x) + (y*c0)), ((2 <= c0) && ((c0*6) == 12))) ||
 rewrite(max((x + (y*c0)), ((z + y) + y)), (max(z, x) + (y*c0)), ((2 <= c0) && ((c0*6) == 12))) ||
 rewrite(max(((x + y) + z), (w + x)), (max((y + z), w) + x)) ||
 rewrite(max(((x + y) + z), (w + y)), (max((x + z), w) + y)) ||
 rewrite(max(((x + y) + z), (w + (u + x))), (max((y + z), (w + u)) + x)) ||
 rewrite(max(((x + y) + z), (w + (u + y))), (max((x + z), (w + u)) + y)) ||
 rewrite(max(((x + y) + z), (w + (x + u))), (max((y + z), (w + u)) + x)) ||
 rewrite(max(((x + y) + z), (w + (y + u))), (max((x + z), (w + u)) + y)) ||
 rewrite(max(((x + y) + z), (x + w)), (max((y + z), w) + x)) ||
 rewrite(max(((x + y) + z), (y + w)), (max((x + z), w) + y)) ||
 rewrite(max(((x + y) + z), ((w + x) + u)), (max((y + z), (w + u)) + x)) ||
 rewrite(max(((x + y) + z), ((w + y) + u)), (max((x + z), (w + u)) + y)) ||
 rewrite(max(((x + y) + z), ((x + w) + u)), (max((y + z), (u + w)) + x)) ||
 rewrite(max(((x + y) + z), ((y + w) + u)), (max((x + z), (u + w)) + y)) ||
 rewrite(max(((x + (y + z)) + w), (u + y)), (max(((x + z) + w), u) + y)) ||
 rewrite(max(((x + (y + z)) + w), (u + z)), (max(((x + y) + w), u) + z)) ||
 rewrite(max(((x + (y + z)) + w), (u + (z + y))), (max((x + w), u) + (z + y))) ||
 rewrite(max(((x + (y + z)) + w), (y + u)), (max(((x + z) + w), u) + y)) ||
 rewrite(max(((x + (y + z)) + w), (z + u)), (max(((x + y) + w), u) + z)) ||
 rewrite(max(((x + (y + z)) + w), ((z + y) + u)), (max(u, (x + w)) + (z + y))) ||
 rewrite(max(((x + (y - z)) + w), (u + y)), (max(((x - z) + w), u) + y)) ||
 rewrite(max(((x + (y - z)) + w), (y + u)), (max(((x - z) + w), u) + y)) ||
 rewrite(max(((x + (y*z)) + w), (u + (z*y))), (max((x + w), u) + (z*y))) ||
 rewrite(max(((x + (y*z)) + w), ((z*y) + u)), (max((x + w), u) + (z*y))) ||
 rewrite(max((((x + y) + z) + w), (u + x)), (max(((y + z) + w), u) + x)) ||
 rewrite(max((((x + y) + z) + w), (u + y)), (max(((x + z) + w), u) + y)) ||
 rewrite(max((((x + y) + z) + w), (u + (y + x))), (max((z + w), u) + (y + x))) ||
 rewrite(max((((x + y) + z) + w), (x + u)), (max(((y + z) + w), u) + x)) ||
 rewrite(max((((x + y) + z) + w), (y + u)), (max(((x + z) + w), u) + y)) ||
 rewrite(max((((x + y) + z) + w), ((y + x) + u)), (max((z + w), u) + (y + x))) ||
 rewrite(max((((x - y) + z) + w), (u + x)), (max(((z - y) + w), u) + x)) ||
 rewrite(max((((x - y) + z) + w), (x + u)), (max(((z - y) + w), u) + x)) ||
 rewrite(max((((x*y) + z) + w), (u + (y*x))), (max((z + w), u) + (y*x))) ||
 rewrite(max((((x*y) + z) + w), ((y*x) + u)), (max((w + z), u) + (y*x))) ||
 rewrite(max(((x - y) + z), (w + x)), (max((z - y), w) + x)) ||
 rewrite(max(((x - y) + z), (w + (u - y))), (max((x + z), (w + u)) - y)) ||
 rewrite(max(((x - y) + z), (x + w)), (max((z - y), w) + x)) ||
 rewrite(max(((x - y) + z), ((w - y) + u)), (max((x + z), (w + u)) - y)) ||
 rewrite(max((((x + y) - z) + w), (u + x)), (max(((y - z) + w), u) + x)) ||
 rewrite(max((((x + y) - z) + w), (u + y)), (max(((x - z) + w), u) + y)) ||
 rewrite(max((((x + y) - z) + w), (x + u)), (max(((y - z) + w), u) + x)) ||
 rewrite(max((((x + y) - z) + w), (y + u)), (max(((x - z) + w), u) + y)) ||
 rewrite(max(((x*c0) + y), (x + (x + z))), (max(z, y) + (x*c0)), ((2 <= c0) && ((c0*6) == 12))) ||
 rewrite(max(((x*c0) + y), (x + (z + x))), (max(z, y) + (x*c0)), ((2 <= c0) && ((c0*6) == 12))) ||
 rewrite(max(((x*c0) + y), ((x + z) + x)), (max(z, y) + (x*c0)), ((2 <= c0) && ((c0*6) == 12))) ||
 rewrite(max(((x*c0) + y), ((z + x) + x)), (max(z, y) + (x*c0)), ((2 <= c0) && ((c0*6) == 12))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x - y), (z + (w - y))), (max(x, (z + w)) - y)) ||
 rewrite(max((x - y), (z + (w - (u + y)))), (max(x, (z + (w - u))) - y)) ||
 rewrite(max((x - y), (z + (w - (y + u)))), (max(x, (z + (w - u))) - y)) ||
 rewrite(max((x - y), ((z - y) + w)), (max((z + w), x) - y)) ||
 rewrite(max((x - y), ((z - (w + y)) + u)), (max(x, ((z - w) + u)) - y)) ||
 rewrite(max((x - y), ((z - (y + w)) + u)), (max(x, ((z - w) + u)) - y)) ||
 rewrite(max((x - (y + z)), (w + (u + (v - y)))), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max((x - (y + z)), (w + (u + (v - z)))), (max((x - y), (w + (u + v))) - z)) ||
 rewrite(max((x - (y + z)), (w + ((u - y) + v))), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max((x - (y + z)), (w + ((u - z) + v))), (max((x - y), (w + (u + v))) - z)) ||
 rewrite(max((x - (y + z)), (w + (u - (v + y)))), (max((x - z), (w + (u - v))) - y)) ||
 rewrite(max((x - (y + z)), (w + (u - (v + z)))), (max((x - y), (w + (u - v))) - z)) ||
 rewrite(max((x - (y + z)), (w + (u - (y + v)))), (max((x - z), (w + (u - v))) - y)) ||
 rewrite(max((x - (y + z)), (w + (u - (z + v)))), (max((x - y), (w + (u - v))) - z)) ||
 rewrite(max((x - (y + z)), ((w + (u - y)) + v)), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max((x - (y + z)), ((w + (u - z)) + v)), (max((x - y), (w + (u + v))) - z)) ||
 rewrite(max((x - (y + z)), (((w - y) + u) + v)), (max((x - z), ((w + v) + u)) - y)) ||
 rewrite(max((x - (y + z)), (((w - z) + u) + v)), (max((x - y), ((w + v) + u)) - z)) ||
 rewrite(max((x - (y + z)), ((w - (u + y)) + v)), (max((x - z), ((w - u) + v)) - y)) ||
 rewrite(max((x - (y + z)), ((w - (u + z)) + v)), (max((x - y), ((w - u) + v)) - z)) ||
 rewrite(max((x - (y + z)), ((w - (y + u)) + v)), (max((x - z), ((w - u) + v)) - y)) ||
 rewrite(max((x - (y + z)), ((w - (z + u)) + v)), (max((x - y), ((w - u) + v)) - z)) ||
 rewrite(max(((x + y) - z), (w + x)), (max((y - z), w) + x)) ||
 rewrite(max(((x + y) - z), (w + y)), (max((x - z), w) + y)) ||
 rewrite(max(((x + y) - z), (w + (u + x))), (max((y - z), (w + u)) + x)) ||
 rewrite(max(((x + y) - z), (w + (u + y))), (max((x - z), (w + u)) + y)) ||
 rewrite(max(((x + y) - z), (w + (u + (x + y)))), (max((x - z), (w + (u + x))) + y)) ||
 rewrite(max(((x + y) - z), (w + (u + (y + x)))), (max((y - z), (w + (u + y))) + x)) ||
 rewrite(max(((x + y) - z), (w + (x + u))), (max((y - z), (w + u)) + x)) ||
 rewrite(max(((x + y) - z), (w + (y + u))), (max((x - z), (w + u)) + y)) ||
 rewrite(max(((x + y) - z), (w + ((x + y) + u))), (max((x - z), (w + (x + u))) + y)) ||
 rewrite(max(((x + y) - z), (w + ((y + x) + u))), (max((y - z), (w + (y + u))) + x)) ||
 rewrite(max(((x + y) - z), (x + w)), (max((y - z), w) + x)) ||
 rewrite(max(((x + y) - z), (y + w)), (max((x - z), w) + y)) ||
 rewrite(max(((x + y) - z), ((w + x) + u)), (max((y - z), (w + u)) + x)) ||
 rewrite(max(((x + y) - z), ((w + y) + u)), (max((x - z), (w + u)) + y)) ||
 rewrite(max(((x + y) - z), ((w + (x + y)) + u)), (max((x - z), (w + (x + u))) + y)) ||
 rewrite(max(((x + y) - z), ((w + (y + x)) + u)), (max((y - z), (w + (y + u))) + x)) ||
 rewrite(max(((x + y) - z), ((x + w) + u)), (max((y - z), (u + w)) + x)) ||
 rewrite(max(((x + y) - z), ((y + w) + u)), (max((x - z), (u + w)) + y)) ||
 rewrite(max(((x + y) - z), (((x + y) + w) + u)), (max((x - z), ((x + u) + w)) + y)) ||
 rewrite(max(((x + y) - z), (((y + x) + w) + u)), (max((y - z), ((y + u) + w)) + x)) ||
 rewrite(max(((x + (y + z)) - w), (u + (v + y))), (max(((x + z) - w), (u + v)) + y)) ||
 rewrite(max(((x + (y + z)) - w), (u + (v + z))), (max(((x + y) - w), (u + v)) + z)) ||
 rewrite(max(((x + (y + z)) - w), (u + (y + v))), (max(((x + z) - w), (u + v)) + y)) ||
 rewrite(max(((x + (y + z)) - w), (u + (z + v))), (max(((x + y) - w), (u + v)) + z)) ||
 rewrite(max(((x + (y + z)) - w), ((u + y) + v)), (max(((x + z) - w), (u + v)) + y)) ||
 rewrite(max(((x + (y + z)) - w), ((u + z) + v)), (max(((x + y) - w), (u + v)) + z)) ||
 rewrite(max(((x + (y + z)) - w), ((y + u) + v)), (max(((x + z) - w), (v + u)) + y)) ||
 rewrite(max(((x + (y + z)) - w), ((z + u) + v)), (max(((x + y) - w), (v + u)) + z)) ||
 rewrite(max((((x + y) + z) - w), (u + (v + x))), (max(((y + z) - w), (u + v)) + x)) ||
 rewrite(max((((x + y) + z) - w), (u + (v + y))), (max(((x + z) - w), (u + v)) + y)) ||
 rewrite(max((((x + y) + z) - w), (u + (x + v))), (max(((y + z) - w), (u + v)) + x)) ||
 rewrite(max((((x + y) + z) - w), (u + (y + v))), (max(((x + z) - w), (u + v)) + y)) ||
 rewrite(max((((x + y) + z) - w), ((u + x) + v)), (max(((y + z) - w), (u + v)) + x)) ||
 rewrite(max((((x + y) + z) - w), ((u + y) + v)), (max(((x + z) - w), (u + v)) + y)) ||
 rewrite(max((((x + y) + z) - w), ((x + u) + v)), (max(((y + z) - w), (v + u)) + x)) ||
 rewrite(max((((x + y) + z) - w), ((y + u) + v)), (max(((x + z) - w), (v + u)) + y)) ||
 rewrite(max(((x - y) - z), (w + (u + (v - y)))), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max(((x - y) - z), (w + ((u - y) + v))), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max(((x - y) - z), ((w + (u - y)) + v)), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max(((x - y) - z), (((w - y) + u) + v)), (max((x - z), ((w + v) + u)) - y)) ||
 rewrite(max((((x + y) - z) - w), (u + (v + x))), (max(((y - z) - w), (u + v)) + x)) ||
 rewrite(max((((x + y) - z) - w), (u + (v + y))), (max(((x - z) - w), (u + v)) + y)) ||
 rewrite(max((((x + y) - z) - w), (u + (x + v))), (max(((y - z) - w), (u + v)) + x)) ||
 rewrite(max((((x + y) - z) - w), (u + (y + v))), (max(((x - z) - w), (u + v)) + y)) ||
 rewrite(max((((x + y) - z) - w), ((u + x) + v)), (max(((y - z) - w), (u + v)) + x)) ||
 rewrite(max((((x + y) - z) - w), ((u + y) + v)), (max(((x - z) - w), (u + v)) + y)) ||
 rewrite(max((((x + y) - z) - w), ((x + u) + v)), (max(((y - z) - w), (v + u)) + x)) ||
 rewrite(max((((x + y) - z) - w), ((y + u) + v)), (max(((x - z) - w), (v + u)) + y)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(max((x - y), (z - (w + y))), (max((z - w), x) - y)) ||
 rewrite(max((x - y), (z - (y + w))), (max((z - w), x) - y)) ||
 rewrite(max((x - (y + z)), (w - (u + y))), (max((x - z), (w - u)) - y)) ||
 rewrite(max((x - (y + z)), (w - (u + z))), (max((x - y), (w - u)) - z)) ||
 rewrite(max((x - (y + z)), (w - (y + u))), (max((x - z), (w - u)) - y)) ||
 rewrite(max((x - (y + z)), (w - (z + u))), (max((x - y), (w - u)) - z)) ||
 rewrite(max((x - (y + z)), ((w + (u - y)) - v)), (max((x - z), (w + (u - v))) - y)) ||
 rewrite(max((x - (y + z)), ((w + (u - z)) - v)), (max((x - y), (w + (u - v))) - z)) ||
 rewrite(max((x - (y + z)), (((w - y) + u) - v)), (max((x - z), ((w - v) + u)) - y)) ||
 rewrite(max((x - (y + z)), (((w - z) + u) - v)), (max((x - y), ((w - v) + u)) - z)) ||
 rewrite(max((x - (y*z)), (w - (u + (z*y)))), (max((w - u), x) - (z*y))) ||
 rewrite(max((x - (y*z)), (w - ((z*y) + u))), (max((w - u), x) - (z*y))) ||
 rewrite(max(((x + y) - z), (x - (w + (u - v)))), (max((y - z), (v - (w + u))) + x)) ||
 rewrite(max(((x + y) - z), (x - ((w - u) + v))), (max((y - z), (u - (w + v))) + x)) ||
 rewrite(max(((x + y) - z), (y - (w + (u - v)))), (max((x - z), (v - (w + u))) + y)) ||
 rewrite(max(((x + y) - z), (y - ((w - u) + v))), (max((x - z), (u - (w + v))) + y)) ||
 rewrite(max(((x + y) - z), ((w + x) - u)), (max((y - z), (w - u)) + x)) ||
 rewrite(max(((x + y) - z), ((w + y) - u)), (max((x - z), (w - u)) + y)) ||
 rewrite(max(((x + y) - z), ((x + w) - u)), (max((y - z), (w - u)) + x)) ||
 rewrite(max(((x + y) - z), ((y + w) - u)), (max((x - z), (w - u)) + y)) ||
 rewrite(max(((x + y) - (z + w)), (x - u)), (x - min(((z + w) - y), u))) ||
 rewrite(max(((x + y) - (z + w)), (y - u)), (y - min((z - (x - w)), u))) ||
 rewrite(max(((x + (y - z)) - w), (u - (v + z))), (max(((x + y) - w), (u - v)) - z)) ||
 rewrite(max(((x + (y - z)) - w), (u - (z + v))), (max(((x + y) - w), (u - v)) - z)) ||
 rewrite(max((((x - y) + z) - w), (u - (v + y))), (max(((x + z) - w), (u - v)) - y)) ||
 rewrite(max((((x - y) + z) - w), (u - (y + v))), (max(((x + z) - w), (u - v)) - y)) ||
 rewrite(max(((x - y) - z), (w - (u + y))), (max((x - z), (w - u)) - y)) ||
 rewrite(max(((x - y) - z), (w - (y + u))), (max((x - z), (w - u)) - y)))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x*c0), (x + max(x, y))), (x + max(x, y)), (((c0 <= 2) && ((c0*-1) <= 4)) && ((c0*-1) <= -2))) ||
 rewrite(max((x*c0), (x + max(y, x))), (x + max(y, x)), (((c0 <= 2) && ((c0*-1) <= 4)) && ((c0*-1) <= -2))) ||
 rewrite(max((x*c0), (max(x, y) + x)), (max(x, y) + x), (((c0 <= 2) && ((c0*-1) <= 4)) && ((c0*-1) <= -2))) ||
 rewrite(max((x*c0), (max(y, x) + x)), (max(y, x) + x), (((c0 <= 2) && ((c0*-1) <= 4)) && ((c0*-1) <= -2))) ||
 rewrite(max((x*c0), (max(y, max(z, (x*c0))) + c1)), (max(y, max(z, (x*c0))) + c1), (((0 <= c1) && (c0 <= (max(c0, 0) + c1))) && (-1 <= c1))) ||
 rewrite(max((x*c0), (max(y, max((x*c0), z)) + c1)), (max(y, max((x*c0), z)) + c1), (((0 <= c1) && (c0 <= (max(c0, 0) + c1))) && (-1 <= c1))) ||
 rewrite(max((x*c0), (max(max(y, (x*c0)), z) + c1)), (max(max(y, (x*c0)), z) + c1), (((0 <= c1) && (c0 <= (max(c0, 0) + c1))) && (-1 <= c1))) ||
 rewrite(max((x*c0), (max(max((x*c0), y), z) + c1)), (max(max((x*c0), y), z) + c1), (((0 <= c1) && (c0 <= (max(c0, 0) + c1))) && (-1 <= c1))) ||
 rewrite(max((x*y), (max(z, max(w, (y*x))) + c0)), (max(z, max(w, (y*x))) + c0), (0 <= c0)) ||
 rewrite(max((x*y), (max(z, max((y*x), w)) + c0)), (max(z, max((y*x), w)) + c0), (0 <= c0)) ||
 rewrite(max((x*y), (max(max(z, (y*x)), w) + c0)), (max(max(z, (y*x)), w) + c0), (0 <= c0)) ||
 rewrite(max((x*y), (max(max((y*x), z), w) + c0)), (max(max((y*x), z), w) + c0), (0 <= c0)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(min(x, min(y, z)), min(w, y)), min(max(w, min(x, z)), y)) ||
 rewrite(max(min(x, min(y, z)), min(w, z)), min(max(w, min(y, x)), z)) ||
 rewrite(max(min(x, min(y, z)), min(y, w)), min(max(w, min(x, z)), y)) ||
 rewrite(max(min(x, min(y, z)), min(z, w)), min(max(w, min(y, x)), z)) ||
 rewrite(max(min(x, min(y, (z + w))), min(u, (w + z))), min(max(u, min(y, x)), (w + z))) ||
 rewrite(max(min(x, min(y, (z + w))), min((w + z), u)), min(max(u, min(y, x)), (w + z))) ||
 rewrite(max(min(x, min((y + z), w)), min(u, (z + y))), min(max(u, min(x, w)), (z + y))) ||
 rewrite(max(min(x, min((y + z), w)), min((z + y), u)), min(max(u, min(x, w)), (z + y))) ||
 rewrite(max(min(min(x, y), z), min(w, x)), min(max(min(z, y), w), x)) ||
 rewrite(max(min(min(x, y), z), min(w, y)), min(max(min(x, z), w), y)) ||
 rewrite(max(min(min(x, y), z), min(x, w)), min(max(min(z, y), w), x)) ||
 rewrite(max(min(min(x, y), z), min(y, w)), min(max(min(x, z), w), y)) ||
 rewrite(max(min(min(x, (y + z)), w), min(u, (z + y))), min(max(min(x, w), u), (z + y))) ||
 rewrite(max(min(min(x, (y + z)), w), min((z + y), u)), min(max(min(x, w), u), (z + y))) ||
 rewrite(max(min(min((x + y), z), w), min(u, (y + x))), min(max(min(w, z), u), (y + x))) ||
 rewrite(max(min(min((x + y), z), w), min((y + x), u)), min(max(min(w, z), u), (y + x))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite(max(max(x, (max(y, z) + c0)), y), max(x, (max(y, z) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, (max(y, z) + c0)), z), max(x, (max(y, z) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, (max(y, max(x, z)) + c0)), z), max(max(x, (max(y, z) + c0)), z), (c0 <= 0)) ||
 rewrite(max(max(x, (max(y, max(z, x)) + c0)), z), max(max(x, (max(y, z) + c0)), z), (c0 <= 0)) ||
 rewrite(max(max(x, (max(max(x, y), z) + c0)), y), max(max(x, (max(y, z) + c0)), y), (c0 <= 0)) ||
 rewrite(max(max(x, (max(max(y, x), z) + c0)), y), max(max(x, (max(y, z) + c0)), y), (c0 <= 0)) ||
 rewrite(max(max(x, max(y, z)), y), max(x, max(y, z))) ||
 rewrite(max(max(x, max(y, z)), z), max(x, max(y, z))) ||
 rewrite(max(max(x, max(y, max(z, w))), w), max(x, max(y, max(z, w)))) ||
 rewrite(max(max(x, max(y, max(z, w))), z), max(x, max(y, max(z, w)))) ||
 rewrite(max(max(x, max(y, max(z, (max(w, u) + c0)))), u), max(x, max(y, max(z, (max(w, u) + c0)))), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max(z, (max(w, u) + c0)))), w), max(x, max(y, max(z, (max(w, u) + c0)))), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max(z, max(w, u)))), u), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(max(x, max(y, max(z, max(w, u)))), w), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(max(x, max(y, max(z, max(w, max(u, v))))), u), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(max(x, max(y, max(z, max(w, max(u, v))))), v), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(max(x, max(y, max(z, max(max(w, u), v)))), u), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(max(x, max(y, max(z, max(max(w, u), v)))), w), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(max(x, max(y, max((max(z, w) + c0), u))), w), max(x, max(y, max((max(z, w) + c0), u))), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max((max(z, w) + c0), u))), z), max(x, max(y, max((max(z, w) + c0), u))), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max(max(z, w), u))), w), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(max(x, max(y, max(max(z, w), u))), z), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(max(x, max(y, max(max(z, max(w, u)), v))), u), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(max(x, max(y, max(max(z, max(w, u)), v))), w), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(max(x, max(y, max(max(max(z, w), u), v))), w), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(max(x, max(y, max(max(max(z, w), u), v))), z), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(max(x, max(max(y, z), w)), y), max(x, max(max(y, z), w))) ||
 rewrite(max(max(x, max(max(y, z), w)), z), max(x, max(max(y, z), w))) ||
 rewrite(max(max(x, max(max(y, (max(z, w) + c0)), u)), w), max(x, max(max(y, (max(z, w) + c0)), u)), (0 <= c0)) ||
 rewrite(max(max(x, max(max(y, (max(z, w) + c0)), u)), z), max(x, max(max(y, (max(z, w) + c0)), u)), (0 <= c0)) ||
 rewrite(max(max(x, max(max(y, max(z, w)), u)), w), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(max(x, max(max(y, max(z, w)), u)), z), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(max(x, max(max(y, max(z, max(w, u))), v)), u), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(max(x, max(max(y, max(z, max(w, u))), v)), w), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(max(x, max(max(y, max(max(z, w), u)), v)), w), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(max(x, max(max(y, max(max(z, w), u)), v)), z), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(max(x, max(max((max(y, z) + c0), w), u)), y), max(x, max(max((max(y, z) + c0), w), u)), (0 <= c0)) ||
 rewrite(max(max(x, max(max((max(y, z) + c0), w), u)), z), max(x, max(max((max(y, z) + c0), w), u)), (0 <= c0)) ||
 rewrite(max(max(x, max(max(max(y, z), w), u)), y), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(max(x, max(max(max(y, z), w), u)), z), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(max(x, max(max(max(y, max(z, w)), u), v)), w), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(max(x, max(max(max(y, max(z, w)), u), v)), z), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(max(x, max(max(max(max(y, z), w), u), v)), y), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(max(x, max(max(max(max(y, z), w), u), v)), z), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(max((max(x, y) + c0), z), x), max((max(x, y) + c0), z), (0 <= c0)) ||
 rewrite(max(max((max(x, y) + c0), z), y), max((max(x, y) + c0), z), (0 <= c0)) ||
 rewrite(max(max((max(x, max(y, z)) + c0), y), z), max(max((max(x, z) + c0), y), z), (c0 <= 0)) ||
 rewrite(max(max((max(x, max(y, z)) + c0), z), y), max(max((max(x, y) + c0), z), y), (c0 <= 0)) ||
 rewrite(max(max((max(max(x, y), z) + c0), x), y), max(max((max(y, z) + c0), x), y), (c0 <= 0)) ||
 rewrite(max(max((max(max(x, y), z) + c0), y), x), max(max((max(x, z) + c0), y), x), (c0 <= 0)) ||
 rewrite(max(max((x*c0), (max(y, z) + c1)), y), max((x*c0), (max(y, z) + c1)), (0 <= c1)) ||
 rewrite(max(max((x*c0), (max(y, z) + c1)), z), max((x*c0), (max(y, z) + c1)), (0 <= c1)) ||
 rewrite(max(max(max(x, y), z), x), max(max(y, z), x)) ||
 rewrite(max(max(max(x, y), z), y), max(max(x, z), y)) ||
 rewrite(max(max(max(x, max(y, z)), w), y), max(max(x, max(y, z)), w)) ||
 rewrite(max(max(max(x, max(y, z)), w), z), max(max(x, max(y, z)), w)) ||
 rewrite(max(max(max(x, max(y, (max(z, w) + c0))), u), w), max(max(x, max(y, (max(z, w) + c0))), u), (0 <= c0)) ||
 rewrite(max(max(max(x, max(y, (max(z, w) + c0))), u), z), max(max(x, max(y, (max(z, w) + c0))), u), (0 <= c0)) ||
 rewrite(max(max(max(x, max(y, max(z, w))), u), w), max(max(x, max(y, max(z, w))), u)) ||
 rewrite(max(max(max(x, max(y, max(z, w))), u), z), max(max(x, max(y, max(z, w))), u)) ||
 rewrite(max(max(max(x, max(y, max(z, max(w, u)))), v), u), max(max(x, max(y, max(z, max(w, u)))), v)) ||
 rewrite(max(max(max(x, max(y, max(z, max(w, u)))), v), w), max(max(x, max(y, max(z, max(w, u)))), v)) ||
 rewrite(max(max(max(x, max(y, max(max(z, w), u))), v), w), max(max(x, max(y, max(max(z, w), u))), v)) ||
 rewrite(max(max(max(x, max(y, max(max(z, w), u))), v), z), max(max(x, max(y, max(max(z, w), u))), v)) ||
 rewrite(max(max(max(x, max((max(y, z) + c0), w)), u), y), max(max(x, max((max(y, z) + c0), w)), u), (0 <= c0)) ||
 rewrite(max(max(max(x, max((max(y, z) + c0), w)), u), z), max(max(x, max((max(y, z) + c0), w)), u), (0 <= c0)) ||
 rewrite(max(max(max(x, max(max(y, z), w)), u), y), max(max(x, max(max(y, z), w)), u)) ||
 rewrite(max(max(max(x, max(max(y, z), w)), u), z), max(max(x, max(max(y, z), w)), u)) ||
 rewrite(max(max(max(x, max(max(y, max(z, w)), u)), v), w), max(max(x, max(max(y, max(z, w)), u)), v)) ||
 rewrite(max(max(max(x, max(max(y, max(z, w)), u)), v), z), max(max(x, max(max(y, max(z, w)), u)), v)) ||
 rewrite(max(max(max(x, max(max(max(y, z), w), u)), v), y), max(max(x, max(max(max(y, z), w), u)), v)) ||
 rewrite(max(max(max(x, max(max(max(y, z), w), u)), v), z), max(max(x, max(max(max(y, z), w), u)), v)) ||
 rewrite(max(max(max(max(x, y), z), w), x), max(max(max(x, y), z), w)) ||
 rewrite(max(max(max(max(x, y), z), w), y), max(max(max(x, y), z), w)) ||
 rewrite(max(max(max(max(x, (max(y, z) + c0)), w), u), y), max(max(max(x, (max(y, z) + c0)), w), u), (0 <= c0)) ||
 rewrite(max(max(max(max(x, (max(y, z) + c0)), w), u), z), max(max(max(x, (max(y, z) + c0)), w), u), (0 <= c0)) ||
 rewrite(max(max(max(max(x, max(y, z)), w), u), y), max(max(max(x, max(y, z)), w), u)) ||
 rewrite(max(max(max(max(x, max(y, z)), w), u), z), max(max(max(x, max(y, z)), w), u)) ||
 rewrite(max(max(max(max(x, max(y, max(z, w))), u), v), w), max(max(max(x, max(y, max(z, w))), u), v)) ||
 rewrite(max(max(max(max(x, max(y, max(z, w))), u), v), z), max(max(max(x, max(y, max(z, w))), u), v)) ||
 rewrite(max(max(max(max(x, max(max(y, z), w)), u), v), y), max(max(max(x, max(max(y, z), w)), u), v)) ||
 rewrite(max(max(max(max(x, max(max(y, z), w)), u), v), z), max(max(max(x, max(max(y, z), w)), u), v)) ||
 rewrite(max(max(max(max((max(x, y) + c0), z), w), u), x), max(max(max((max(x, y) + c0), z), w), u), (0 <= c0)) ||
 rewrite(max(max(max(max((max(x, y) + c0), z), w), u), y), max(max(max((max(x, y) + c0), z), w), u), (0 <= c0)) ||
 rewrite(max(max(max(max(max(x, y), z), w), u), x), max(max(max(max(x, y), z), w), u)) ||
 rewrite(max(max(max(max(max(x, y), z), w), u), y), max(max(max(max(x, y), z), w), u)) ||
 rewrite(max(max(max(max(max(x, max(y, z)), w), u), v), y), max(max(max(max(x, max(y, z)), w), u), v)) ||
 rewrite(max(max(max(max(max(x, max(y, z)), w), u), v), z), max(max(max(max(x, max(y, z)), w), u), v)) ||
 rewrite(max(max(max(max(max(max(x, y), z), w), u), v), x), max(max(max(max(max(x, y), z), w), u), v)) ||
 rewrite(max(max(max(max(max(max(x, y), z), w), u), v), y), max(max(max(max(max(x, y), z), w), u), v)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max(max(x, y), (x + c0)), max(y, (x + c0)), (0 <= c0)) ||
 rewrite(max(max(x, y), (y + c0)), max(x, (y + c0)), (0 <= c0)) ||
 rewrite(max(max(x, y), (max(x, z) + c0)), max(y, (max(x, z) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, y), (max(y, z) + c0)), max(x, (max(y, z) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, y), (max(z, x) + c0)), max(y, (max(z, x) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, y), (max(z, y) + c0)), max(x, (max(z, y) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, (y + z)), (w + (u + y))), max(x, (y + max((w + u), z)))) ||
 rewrite(max(max(x, (y + z)), (w + (u + z))), max(x, (z + max((w + u), y)))) ||
 rewrite(max(max(x, (y + z)), (w + (y + u))), max(x, (y + max(z, (w + u))))) ||
 rewrite(max(max(x, (y + z)), (w + (z + u))), max((max(y, (u + w)) + z), x)) ||
 rewrite(max(max(x, (y + z)), ((w + y) + u)), max((max(z, (w + u)) + y), x)) ||
 rewrite(max(max(x, (y + z)), ((w + z) + u)), max((max(y, (w + u)) + z), x)) ||
 rewrite(max(max(x, (y + z)), ((y + w) + u)), max(x, (y + max((u + w), z)))) ||
 rewrite(max(max(x, (y + z)), ((z + w) + u)), max((max(y, (w + u)) + z), x)) ||
 rewrite(max(max(x, (y + (z + w))), (u + (v + w))), max(x, (max((z + y), (u + v)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (v + z))), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (w + v))), max(x, (max((z + y), (u + v)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (z + v))), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, (y + (z + w))), ((u + w) + v)), max(x, (max((z + y), (u + v)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), ((u + z) + v)), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, (y + (z + w))), ((w + u) + v)), max(x, (max((z + y), (v + u)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), ((z + u) + v)), max(x, (max((y + w), (v + u)) + z))) ||
 rewrite(max(max(x, (y + (z - w))), (u + (v - w))), max(x, (max((z + y), (u + v)) - w))) ||
 rewrite(max(max(x, (y + (z - w))), ((u - w) + v)), max(x, (max((z + y), (u + v)) - w))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (v + y))), max(x, (max((z + w), (u + v)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (v + z))), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (y + v))), max(x, (max((z + w), (u + v)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (z + v))), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, ((y + z) + w)), ((u + y) + v)), max(x, (max((z + w), (u + v)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), ((u + z) + v)), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, ((y + z) + w)), ((y + u) + v)), max(x, (max((z + w), (v + u)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), ((z + u) + v)), max(x, (max((y + w), (v + u)) + z))) ||
 rewrite(max(max(x, ((y - z) + w)), (u + (v - z))), max(x, (max((y + w), (u + v)) - z))) ||
 rewrite(max(max(x, ((y - z) + w)), ((u - z) + v)), max(x, (max((y + w), (u + v)) - z))) ||
 rewrite(max(max(x, (y - z)), (w + (u + (v - z)))), max(x, (max(y, (w + (u + v))) - z))) ||
 rewrite(max(max(x, (y - z)), (w + ((u - z) + v))), max((max(y, (u + (w + v))) - z), x)) ||
 rewrite(max(max(x, (y - z)), (w + (u - z))), max(x, (max(y, (w + u)) - z))) ||
 rewrite(max(max(x, (y - z)), ((w + (u - z)) + v)), max(x, (max(y, (w + (u + v))) - z))) ||
 rewrite(max(max(x, (y - z)), (((w - z) + u) + v)), max(x, (max(y, ((w + v) + u)) - z))) ||
 rewrite(max(max(x, (y - z)), ((w - z) + u)), max((max(y, (w + u)) - z), x)) ||
 rewrite(max(max(x, (y*c0)), ((y*c0) + c1)), max(x, ((y*c0) + c1)), ((c0 <= max((c0 + c1), -7)) && ((c0*-1) <= max((c1 - c0), 0)))) ||
 rewrite(max(max(x, (y*z)), ((z*y) + c0)), max(x, ((z*y) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, min(y, z)), (y + c0)), max(x, (y + c0)), (0 <= c0)) ||
 rewrite(max(max(x, min(y, z)), (z + c0)), max(x, (z + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (y + c0)), max(max(x, z), (y + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (z + c0)), max(max(x, y), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (max(w, max(u, y)) + c0)), max(max(x, z), (max(w, max(u, y)) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (max(w, max(u, z)) + c0)), max(max(x, y), (max(w, max(u, z)) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (max(w, max(y, u)) + c0)), max(max(x, z), (max(w, max(y, u)) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (max(w, max(z, u)) + c0)), max(max(x, y), (max(w, max(z, u)) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (max(max(w, y), u) + c0)), max(max(x, z), (max(max(w, y), u) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (max(max(w, z), u) + c0)), max(max(x, y), (max(max(w, z), u) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (max(max(y, w), u) + c0)), max(max(x, z), (max(max(y, w), u) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, z)), (max(max(z, w), u) + c0)), max(max(x, y), (max(max(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, (z + w))), (u + w)), max(max(x, (max(z, u) + w)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (u + z)), max(max(x, y), (max(w, u) + z))) ||
 rewrite(max(max(x, max(y, (z + w))), (w + u)), max(max(x, (max(z, u) + w)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (z + u)), max(max(x, (max(w, u) + z)), y)) ||
 rewrite(max(max(x, max(y, (z - w))), (u + (v - w))), max(x, max(y, (max(z, (u + v)) - w)))) ||
 rewrite(max(max(x, max(y, (z - w))), ((u - w) + v)), max(max(x, (max(z, (v + u)) - w)), y)) ||
 rewrite(max(max(x, max(y, (z*c0))), (x + c1)), max(max(y, (z*c0)), (x + c1)), (0 <= c1)) ||
 rewrite(max(max(x, max(y, max(z, w))), (w + c0)), max(max(x, max(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max(z, w))), (z + c0)), max(max(x, max(y, w)), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max(z, (w + u)))), (u + v)), max(max(x, max(y, (max(w, v) + u))), z)) ||
 rewrite(max(max(x, max(y, max(z, (w + u)))), (v + u)), max(max(x, max(y, (max(w, v) + u))), z)) ||
 rewrite(max(max(x, max(y, max(z, (w + u)))), (v + w)), max(max(x, max(y, (max(u, v) + w))), z)) ||
 rewrite(max(max(x, max(y, max(z, (w + u)))), (w + v)), max(max(x, max(y, (max(u, v) + w))), z)) ||
 rewrite(max(max(x, max(y, max(z, (w*c0)))), (x + c1)), max(max(y, max(z, (w*c0))), (x + c1)), (0 <= c1)) ||
 rewrite(max(max(x, max(y, max(z, max(w, u)))), (u + c0)), max(max(x, max(y, max(z, w))), (u + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max(z, max(w, u)))), (w + c0)), max(max(x, max(y, max(z, u))), (w + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max((z + w), u))), (v + w)), max(max(x, max((max(v, z) + w), u)), y)) ||
 rewrite(max(max(x, max(y, max((z + w), u))), (v + z)), max(max(x, max(y, (max(v, w) + z))), u)) ||
 rewrite(max(max(x, max(y, max((z + w), u))), (w + v)), max(max(x, max((max(v, z) + w), u)), y)) ||
 rewrite(max(max(x, max(y, max((z + w), u))), (z + v)), max(max(x, max(y, (max(v, w) + z))), u)) ||
 rewrite(max(max(x, max(y, max((z*c0), w))), (x + c1)), max(max(y, max((z*c0), w)), (x + c1)), (0 <= c1)) ||
 rewrite(max(max(x, max(y, max(max(z, w), u))), (w + c0)), max(max(x, max(y, max(z, u))), (w + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(y, max(max(z, w), u))), (z + c0)), max(max(x, max(y, max(w, u))), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max((y + z), w)), (u + y)), max(x, max((y + max(z, u)), w))) ||
 rewrite(max(max(x, max((y + z), w)), (u + z)), max(max(x, w), (max(y, u) + z))) ||
 rewrite(max(max(x, max((y + z), w)), (y + u)), max(x, max((y + max(z, u)), w))) ||
 rewrite(max(max(x, max((y + z), w)), (z + u)), max(max(x, (max(y, u) + z)), w)) ||
 rewrite(max(max(x, max((y - z), w)), (u + (v - z))), max(x, max(w, (max(y, (u + v)) - z)))) ||
 rewrite(max(max(x, max((y - z), w)), ((u - z) + v)), max(max(x, (max((v + u), y) - z)), w)) ||
 rewrite(max(max(x, max((y*c0), z)), (x + c1)), max(max((y*c0), z), (x + c1)), (0 <= c1)) ||
 rewrite(max(max(x, max(max(y, z), w)), (y + c0)), max(max(x, max(z, w)), (y + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(max(y, z), w)), (z + c0)), max(max(x, max(y, w)), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(max(y, (z + w)), u)), (v + w)), max(max(x, max((max(z, v) + w), u)), y)) ||
 rewrite(max(max(x, max(max(y, (z + w)), u)), (v + z)), max(max(x, max((max(w, v) + z), u)), y)) ||
 rewrite(max(max(x, max(max(y, (z + w)), u)), (w + v)), max(max(x, max((max(z, v) + w), u)), y)) ||
 rewrite(max(max(x, max(max(y, (z + w)), u)), (z + v)), max(max(x, max((max(w, v) + z), u)), y)) ||
 rewrite(max(max(x, max(max(y, (z*c0)), w)), (x + c1)), max(max(max(y, (z*c0)), w), (x + c1)), (0 <= c1)) ||
 rewrite(max(max(x, max(max(y, max(z, w)), u)), (w + c0)), max(max(x, max(max(y, z), u)), (w + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(max(y, max(z, w)), u)), (z + c0)), max(max(x, max(max(y, w), u)), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(max((y + z), w), u)), (v + y)), max(max(x, max((max(v, z) + y), u)), w)) ||
 rewrite(max(max(x, max(max((y + z), w), u)), (v + z)), max(max(x, max((max(y, v) + z), u)), w)) ||
 rewrite(max(max(x, max(max((y + z), w), u)), (y + v)), max(max(x, max((max(v, z) + y), u)), w)) ||
 rewrite(max(max(x, max(max((y + z), w), u)), (z + v)), max(max(x, max((max(y, v) + z), u)), w)) ||
 rewrite(max(max(x, max(max((y*c0), z), w)), (x + c1)), max(max(max((y*c0), z), w), (x + c1)), (0 <= c1)) ||
 rewrite(max(max(x, max(max(max(y, z), w), u)), (y + c0)), max(max(x, max(max(z, w), u)), (y + c0)), (0 <= c0)) ||
 rewrite(max(max(x, max(max(max(y, z), w), u)), (z + c0)), max(max(x, max(max(y, w), u)), (z + c0)), (0 <= c0)) ||
 rewrite(max(max((x + y), z), (w + (u + x))), max(z, (x + max(y, (w + u))))) ||
 rewrite(max(max((x + y), z), (w + (u + y))), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((x + y), z), (w + (x + u))), max((max((u + w), y) + x), z)) ||
 rewrite(max(max((x + y), z), (w + (y + u))), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((x + y), z), ((w + x) + u)), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((x + y), z), ((w + y) + u)), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((x + y), z), ((x + w) + u)), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((x + y), z), ((y + w) + u)), max((max((u + w), x) + y), z)) ||
 rewrite(max(max((x + (y + z)), w), (u + (v + y))), max((max((x + z), (u + v)) + y), w)) ||
 rewrite(max(max((x + (y + z)), w), (u + (v + z))), max((max((x + y), (u + v)) + z), w)) ||
 rewrite(max(max((x + (y + z)), w), (u + (y + v))), max((max((x + z), (u + v)) + y), w)) ||
 rewrite(max(max((x + (y + z)), w), (u + (z + v))), max((max((x + y), (u + v)) + z), w)) ||
 rewrite(max(max((x + (y + z)), w), ((u + y) + v)), max(w, (max((x + z), (u + v)) + y))) ||
 rewrite(max(max((x + (y + z)), w), ((u + z) + v)), max(w, (max((x + y), (u + v)) + z))) ||
 rewrite(max(max((x + (y + z)), w), ((y + u) + v)), max(w, (max((x + z), (v + u)) + y))) ||
 rewrite(max(max((x + (y + z)), w), ((z + u) + v)), max(w, (max((y + x), (v + u)) + z))) ||
 rewrite(max(max((x + (y - z)), w), (u + (v - z))), max((max((x + y), (u + v)) - z), w)) ||
 rewrite(max(max((x + (y - z)), w), ((u - z) + v)), max((max((x + y), (v + u)) - z), w)) ||
 rewrite(max(max(((x + y) + z), w), (u + (v + x))), max(w, (x + max((z + y), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), (u + (v + y))), max((max((x + z), (u + v)) + y), w)) ||
 rewrite(max(max(((x + y) + z), w), (u + (x + v))), max(w, (x + max((z + y), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), (u + (y + v))), max((max((x + z), (u + v)) + y), w)) ||
 rewrite(max(max(((x + y) + z), w), ((u + x) + v)), max(w, (x + max((z + y), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), ((u + y) + v)), max(w, (max((x + z), (u + v)) + y))) ||
 rewrite(max(max(((x + y) + z), w), ((x + u) + v)), max(w, (x + max((z + y), (v + u))))) ||
 rewrite(max(max(((x + y) + z), w), ((y + u) + v)), max(w, (max((x + z), (v + u)) + y))) ||
 rewrite(max(max(((x - y) + z), w), (u + (v - y))), max((max((x + z), (u + v)) - y), w)) ||
 rewrite(max(max(((x - y) + z), w), ((u - y) + v)), max((max((x + z), (v + u)) - y), w)) ||
 rewrite(max(max((x - y), z), (w + (u + (v - y)))), max((max(x, (w + (u + v))) - y), z)) ||
 rewrite(max(max((x - y), z), (w + ((u - y) + v))), max((max(x, (u + (w + v))) - y), z)) ||
 rewrite(max(max((x - y), z), (w + (u - y))), max((max(x, (w + u)) - y), z)) ||
 rewrite(max(max((x - y), z), ((w + (u - y)) + v)), max(z, (max(x, (w + (u + v))) - y))) ||
 rewrite(max(max((x - y), z), (((w - y) + u) + v)), max(z, (max(x, ((w + v) + u)) - y))) ||
 rewrite(max(max((x - y), z), ((w - y) + u)), max((max((w + u), x) - y), z)) ||
 rewrite(max(max((x*c0), y), ((x*c0) + c1)), max(y, ((x*c0) + c1)), ((c0 <= max((c0 + c1), -7)) && ((c0*-1) <= max((c1 - c0), 0)))) ||
 rewrite(max(max((x*y), z), ((y*x) + c0)), max(z, ((y*x) + c0)), (0 <= c0)) ||
 rewrite(max(max(min(x, y), z), (x + c0)), max(z, (x + c0)), (0 <= c0)) ||
 rewrite(max(max(min(x, y), z), (y + c0)), max(z, (y + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (x + c0)), max(max(y, z), (x + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (y + c0)), max(max(x, z), (y + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (max(w, max(u, x)) + c0)), max(max(y, z), (max(w, max(u, x)) + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (max(w, max(u, y)) + c0)), max(max(x, z), (max(w, max(u, y)) + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (max(w, max(x, u)) + c0)), max(max(y, z), (max(w, max(x, u)) + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (max(w, max(y, u)) + c0)), max(max(x, z), (max(w, max(y, u)) + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (max(max(w, x), u) + c0)), max(max(y, z), (max(max(w, x), u) + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (max(max(w, y), u) + c0)), max(max(x, z), (max(max(w, y), u) + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (max(max(x, w), u) + c0)), max(max(y, z), (max(max(x, w), u) + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), z), (max(max(y, w), u) + c0)), max(max(x, z), (max(max(y, w), u) + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, y), (z*c0)), ((z*c0) + c1)), max(max(x, y), ((z*c0) + c1)), (((c0 <= max((c0 + c1), 0)) && ((c0*2) <= max(((c0*2) + c1), -7))) && ((c0*-3) <= max(((c0*-3) + c1), 0)))) ||
 rewrite(max(max(max(x, (y + z)), w), (u + y)), max(max((max(z, u) + y), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (u + z)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (y + u)), max(max((max(z, u) + y), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (z + u)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max(x, (y - z)), w), (u + (v - z))), max(w, max(x, (max(y, (u + v)) - z)))) ||
 rewrite(max(max(max(x, (y - z)), w), ((u - z) + v)), max(max((max(y, (v + u)) - z), w), x)) ||
 rewrite(max(max(max(x, (y*c0)), z), (z + c1)), max(max(x, (y*c0)), (z + c1)), (0 <= c1)) ||
 rewrite(max(max(max(x, max(y, z)), w), (y + c0)), max(max(max(x, z), w), (y + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, max(y, z)), w), (z + c0)), max(max(max(x, y), w), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, max(y, (z + w))), u), (v + w)), max(max(max(x, (max(z, v) + w)), u), y)) ||
 rewrite(max(max(max(x, max(y, (z + w))), u), (v + z)), max(max(max(x, (max(w, v) + z)), u), y)) ||
 rewrite(max(max(max(x, max(y, (z + w))), u), (w + v)), max(max(max(x, (max(z, v) + w)), u), y)) ||
 rewrite(max(max(max(x, max(y, (z + w))), u), (z + v)), max(max(max(x, (max(w, v) + z)), u), y)) ||
 rewrite(max(max(max(x, max(y, (z*c0))), w), (w + c1)), max(max(x, max(y, (z*c0))), (w + c1)), (0 <= c1)) ||
 rewrite(max(max(max(x, max(y, max(z, w))), u), (w + c0)), max(max(max(x, max(y, z)), u), (w + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, max(y, max(z, w))), u), (z + c0)), max(max(max(x, max(y, w)), u), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, max((y + z), w)), u), (v + y)), max(max(max(x, (max(v, z) + y)), u), w)) ||
 rewrite(max(max(max(x, max((y + z), w)), u), (v + z)), max(max(max((max(v, y) + z), w), u), x)) ||
 rewrite(max(max(max(x, max((y + z), w)), u), (y + v)), max(max(max(x, (max(v, z) + y)), u), w)) ||
 rewrite(max(max(max(x, max((y + z), w)), u), (z + v)), max(max(max((max(v, y) + z), w), u), x)) ||
 rewrite(max(max(max(x, max((y*c0), z)), w), (w + c1)), max(max(x, max((y*c0), z)), (w + c1)), (0 <= c1)) ||
 rewrite(max(max(max(x, max(max(y, z), w)), u), (y + c0)), max(max(max(x, max(z, w)), u), (y + c0)), (0 <= c0)) ||
 rewrite(max(max(max(x, max(max(y, z), w)), u), (z + c0)), max(max(max(x, max(y, w)), u), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(max((x + y), z), w), (u + x)), max(max((max(u, y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (u + y)), max(max((max(x, u) + y), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (x + u)), max(max((max(u, y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (y + u)), max(max((max(x, u) + y), w), z)) ||
 rewrite(max(max(max((x - y), z), w), (u + (v - y))), max(max(z, w), (max(x, (u + v)) - y))) ||
 rewrite(max(max(max((x - y), z), w), ((u - y) + v)), max(max((max((v + u), x) - y), w), z)) ||
 rewrite(max(max(max((x*c0), y), z), (z + c1)), max(max((x*c0), y), (z + c1)), (0 <= c1)) ||
 rewrite(max(max(max(max(x, y), z), w), (x + c0)), max(max(max(y, z), w), (x + c0)), (0 <= c0)) ||
 rewrite(max(max(max(max(x, y), z), w), (y + c0)), max(max(max(x, z), w), (y + c0)), (0 <= c0)) ||
 rewrite(max(max(max(max(x, (y + z)), w), u), (v + y)), max(max(max((max(z, v) + y), w), u), x)) ||
 rewrite(max(max(max(max(x, (y + z)), w), u), (v + z)), max(max(max((max(y, v) + z), w), u), x)) ||
 rewrite(max(max(max(max(x, (y + z)), w), u), (y + v)), max(max(max((max(z, v) + y), w), u), x)) ||
 rewrite(max(max(max(max(x, (y + z)), w), u), (z + v)), max(max(max((max(y, v) + z), w), u), x)) ||
 rewrite(max(max(max(max(x, (y*c0)), z), w), (w + c1)), max(max(max(x, (y*c0)), z), (w + c1)), (0 <= c1)) ||
 rewrite(max(max(max(max(x, max(y, z)), w), u), (y + c0)), max(max(max(max(x, z), w), u), (y + c0)), (0 <= c0)) ||
 rewrite(max(max(max(max(x, max(y, z)), w), u), (z + c0)), max(max(max(max(x, y), w), u), (z + c0)), (0 <= c0)) ||
 rewrite(max(max(max(max((x + y), z), w), u), (v + x)), max(max(max((max(v, y) + x), w), u), z)) ||
 rewrite(max(max(max(max((x + y), z), w), u), (v + y)), max(max(max((max(x, v) + y), w), u), z)) ||
 rewrite(max(max(max(max((x + y), z), w), u), (x + v)), max(max(max((max(v, y) + x), w), u), z)) ||
 rewrite(max(max(max(max((x + y), z), w), u), (y + v)), max(max(max((max(x, v) + y), w), u), z)) ||
 rewrite(max(max(max(max((x*c0), y), z), w), (w + c1)), max(max(max((x*c0), y), z), (w + c1)), (0 <= c1)) ||
 rewrite(max(max(max(max(max(x, y), z), w), u), (x + c0)), max(max(max(max(y, z), w), u), (x + c0)), (0 <= c0)) ||
 rewrite(max(max(max(max(max(x, y), z), w), u), (y + c0)), max(max(max(max(x, z), w), u), (y + c0)), (0 <= c0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(max(max(x, (y + (z - w))), (u - w)), max(x, (max((z + y), u) - w))) ||
 rewrite(max(max(x, ((y - z) + w)), (u - z)), max(x, (max((y + w), u) - z))) ||
 rewrite(max(max(x, (y - z)), (w - z)), max((max(y, w) - z), x)) ||
 rewrite(max(max(x, max(y, (z + (w - u)))), (v - u)), max(max(x, (max(v, (z + w)) - u)), y)) ||
 rewrite(max(max(x, max(y, ((z - w) + u))), (v - w)), max(max(x, y), (max((z + u), v) - w))) ||
 rewrite(max(max(x, max(y, (z - w))), (u - w)), max(max(x, (max(z, u) - w)), y)) ||
 rewrite(max(max(x, max(y, max(z, (w - u)))), (v - u)), max(max(x, max(y, (max(w, v) - u))), z)) ||
 rewrite(max(max(x, max(y, max((z - w), u))), (v - w)), max(max(x, max((max(v, z) - w), u)), y)) ||
 rewrite(max(max(x, max((y + (z - w)), u)), (v - w)), max(max(x, (max((y + z), v) - w)), u)) ||
 rewrite(max(max(x, max(((y - z) + w), u)), (v - z)), max(max(x, (max((y + w), v) - z)), u)) ||
 rewrite(max(max(x, max((y - z), w)), (u - z)), max(max(x, w), (max(y, u) - z))) ||
 rewrite(max(max(x, max(max(y, (z - w)), u)), (v - w)), max(max(x, max((max(z, v) - w), u)), y)) ||
 rewrite(max(max(x, max(max((y - z), w), u)), (v - z)), max(max(x, max((max(y, v) - z), u)), w)) ||
 rewrite(max(max((x + (y - z)), w), (u - z)), max(w, (max((x + y), u) - z))) ||
 rewrite(max(max(((x - y) + z), w), (u - y)), max(w, (max((x + z), u) - y))) ||
 rewrite(max(max((x - y), z), (w - y)), max(z, (max(x, w) - y))) ||
 rewrite(max(max(max(x, (y + (z - w))), u), (v - w)), max(max((max(v, (y + z)) - w), u), x)) ||
 rewrite(max(max(max(x, ((y - z) + w)), u), (v - z)), max(max((max(v, (y + w)) - z), u), x)) ||
 rewrite(max(max(max(x, (y - z)), w), (u - z)), max(max((max(y, u) - z), w), x)) ||
 rewrite(max(max(max(x, max(y, (z - w))), u), (v - w)), max(max(max(x, (max(z, v) - w)), u), y)) ||
 rewrite(max(max(max(x, max((y - z), w)), u), (v - z)), max(max(max((max(v, y) - z), w), u), x)) ||
 rewrite(max(max(max((x + (y - z)), w), u), (v - z)), max(max((max((x + y), v) - z), u), w)) ||
 rewrite(max(max(max(((x - y) + z), w), u), (v - y)), max(max((max((x + z), v) - y), u), w)) ||
 rewrite(max(max(max((x - y), z), w), (u - y)), max(max((max(x, u) - y), w), z)) ||
 rewrite(max(max(max(max(x, (y - z)), w), u), (v - z)), max(max(max((max(y, v) - z), w), u), x)) ||
 rewrite(max(max(max(max((x - y), z), w), u), (v - y)), max(max(max((max(x, v) - y), w), u), z)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(max(max(x, (max(y, (z*c0)) + c1)), (z*c0)), max(x, (max(y, (z*c0)) + c1)), (((((c0 <= max((max(c0, 0) + c1), 0)) && ((c0*-1) <= max((c1 - min(c0, -7)), 2))) && ((c0*-2) <= max((max((c0*-2), 7) + c1), 3))) && ((c0*-3) <= max((max((c0*-3), 7) + c1), 5))) && ((c0*-3) <= max((max((c0*-3), 1) + c1), 1)))) ||
 rewrite(max(max(x, (max(y, (z*w)) + c0)), (w*z)), max(x, (max(y, (z*w)) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, (max((y*c0), z) + c1)), (y*c0)), max(x, (max((y*c0), z) + c1)), (((((c0 <= max((max(c0, 0) + c1), 0)) && ((c0*-1) <= max((c1 - min(c0, -7)), 2))) && ((c0*-2) <= max((max((c0*-2), 7) + c1), 3))) && ((c0*-3) <= max((max((c0*-3), 7) + c1), 5))) && ((c0*-3) <= max((max((c0*-3), 1) + c1), 1)))) ||
 rewrite(max(max(x, (max((y*z), w) + c0)), (z*y)), max(x, (max((y*z), w) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, ((y + z)*c0)), ((w + y)*c0)), max(x, ((y + max(z, w))*c0)), ((max(c0, 0) + min(min((c0*-1), c0), 0)) == 0)) ||
 rewrite(max(max(x, ((y + z)*c0)), ((w + z)*c0)), max(x, ((z + max(y, w))*c0)), ((max(c0, 0) + min(min((c0*-1), c0), 0)) == 0)) ||
 rewrite(max(max(x, ((y + z)*c0)), ((y + w)*c0)), max(x, ((y + max(z, w))*c0)), ((max(c0, 0) + min(min((c0*-1), c0), 0)) == 0)) ||
 rewrite(max(max(x, ((y + z)*c0)), ((z + w)*c0)), max(x, ((z + max(y, w))*c0)), ((max(c0, 0) + min(min((c0*-1), c0), 0)) == 0)) ||
 rewrite(max(max((max(x, (y*c0)) + c1), z), (y*c0)), max((max(x, (y*c0)) + c1), z), (((((c0 <= max((max(c0, 0) + c1), 0)) && ((c0*-1) <= max((c1 - min(c0, -7)), 2))) && ((c0*-2) <= max((max((c0*-2), 7) + c1), 3))) && ((c0*-3) <= max((max((c0*-3), 7) + c1), 5))) && ((c0*-3) <= max((max((c0*-3), 1) + c1), 1)))) ||
 rewrite(max(max((max(x, (y*z)) + c0), w), (z*y)), max((max(x, (y*z)) + c0), w), (0 <= c0)) ||
 rewrite(max(max((max((x*c0), y) + c1), z), (x*c0)), max((max((x*c0), y) + c1), z), (((((c0 <= max((max(c0, 0) + c1), 0)) && ((c0*-1) <= max((c1 - min(c0, -7)), 2))) && ((c0*-2) <= max((max((c0*-2), 7) + c1), 3))) && ((c0*-3) <= max((max((c0*-3), 7) + c1), 5))) && ((c0*-3) <= max((max((c0*-3), 1) + c1), 1)))) ||
 rewrite(max(max((max((x*y), z) + c0), w), (y*x)), max((max((x*y), z) + c0), w), (0 <= c0)) ||
 rewrite(max(max(((x + y)*c0), z), ((w + x)*c0)), max(z, ((max(y, w) + x)*c0)), ((max(c0, 0) + min(min((c0*-1), c0), 0)) == 0)) ||
 rewrite(max(max(((x + y)*c0), z), ((w + y)*c0)), max(z, ((max(x, w) + y)*c0)), ((max(c0, 0) + min(min((c0*-1), c0), 0)) == 0)) ||
 rewrite(max(max(((x + y)*c0), z), ((x + w)*c0)), max(z, ((x + max(y, w))*c0)), ((max(c0, 0) + min(min((c0*-1), c0), 0)) == 0)) ||
 rewrite(max(max(((x + y)*c0), z), ((y + w)*c0)), max(z, ((y + max(x, w))*c0)), ((max(c0, 0) + min(min((c0*-1), c0), 0)) == 0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(max(x, y), min(x, z)), max(x, y)) ||
 rewrite(max(max(x, y), min(y, z)), max(x, y)) ||
 rewrite(max(max(x, y), min(z, x)), max(x, y)) ||
 rewrite(max(max(x, y), min(z, y)), max(x, y)) ||
 rewrite(max(max(x, min(y, z)), min(w, y)), max(x, min(y, max(w, z)))) ||
 rewrite(max(max(x, min(y, z)), min(w, z)), max(x, min(z, max(w, y)))) ||
 rewrite(max(max(x, min(y, z)), min(y, w)), max(x, min(y, max(z, w)))) ||
 rewrite(max(max(x, min(y, z)), min(z, w)), max(x, min(z, max(y, w)))) ||
 rewrite(max(max(x, max(y, min(z, w))), min(u, w)), max(x, max(y, min(w, max(u, z))))) ||
 rewrite(max(max(x, max(y, min(z, w))), min(u, z)), max(x, max(y, min(z, max(u, w))))) ||
 rewrite(max(max(x, max(y, min(z, w))), min(w, u)), max(x, max(y, min(w, max(z, u))))) ||
 rewrite(max(max(x, max(y, min(z, w))), min(z, u)), max(x, max(y, min(z, max(w, u))))) ||
 rewrite(max(max(x, max(min(y, z), w)), min(u, y)), max(max(x, min(max(u, z), y)), w)) ||
 rewrite(max(max(x, max(min(y, z), w)), min(u, z)), max(max(x, min(max(y, u), z)), w)) ||
 rewrite(max(max(x, max(min(y, z), w)), min(y, u)), max(max(x, min(max(u, z), y)), w)) ||
 rewrite(max(max(x, max(min(y, z), w)), min(z, u)), max(max(x, min(max(y, u), z)), w)) ||
 rewrite(max(max(min(x, y), z), min(w, x)), max(min(max(w, y), x), z)) ||
 rewrite(max(max(min(x, y), z), min(w, y)), max(min(max(x, w), y), z)) ||
 rewrite(max(max(min(x, y), z), min(x, w)), max(min(max(w, y), x), z)) ||
 rewrite(max(max(min(x, y), z), min(y, w)), max(min(max(x, w), y), z)) ||
 rewrite(max(max(max(x, min(y, z)), w), min(u, y)), max(max(min(y, max(u, z)), w), x)) ||
 rewrite(max(max(max(x, min(y, z)), w), min(u, z)), max(max(min(z, max(y, u)), w), x)) ||
 rewrite(max(max(max(x, min(y, z)), w), min(y, u)), max(max(min(y, max(u, z)), w), x)) ||
 rewrite(max(max(max(x, min(y, z)), w), min(z, u)), max(max(min(z, max(y, u)), w), x)) ||
 rewrite(max(max(max(min(x, y), z), w), min(u, x)), max(max(min(max(u, y), x), w), z)) ||
 rewrite(max(max(max(min(x, y), z), w), min(u, y)), max(max(min(max(x, u), y), w), z)) ||
 rewrite(max(max(max(min(x, y), z), w), min(x, u)), max(max(min(max(u, y), x), w), z)) ||
 rewrite(max(max(max(min(x, y), z), w), min(y, u)), max(max(min(max(x, u), y), w), z)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Max) && (
 rewrite(max(select(x, y, z), max(w, select(x, u, z))), max(select(x, max(y, u), z), w)) ||
 rewrite(max(select(x, y, z), max(select(x, w, z), u)), max(select(x, max(y, w), z), u))))))
