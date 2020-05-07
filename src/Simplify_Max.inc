((b.node_type() == IRNodeType::Add) && (
 rewrite(max(x, ((y - (z*c0)) + (w + (z*c1)))), max(x, (y + w)), ((c1 <= c0) && (c0 == c1))) ||
 rewrite(max(x, ((y - (z*c0)) + ((z*c1) + w))), max(x, (y + w)), ((c1 <= c0) && (c0 == c1))) ||
 rewrite(max(x, (min(x, y) + c0)), x, (c0 <= 0)) ||
 rewrite(max(x, (min(y, x) + c0)), x, (c0 <= 0)) ||
 rewrite(max(x, (max(y, max(x, z)) + c0)), (max(y, max(x, z)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(z, x)) + c0)), (max(y, max(z, x)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(z, max(w, x))) + c0)), (max(y, max(z, max(w, x))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(z, max(w, max(u, x)))) + c0)), (max(y, max(z, max(w, max(u, x)))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(z, max(w, max(x, u)))) + c0)), (max(y, max(z, max(w, max(x, u)))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(z, max(x, w))) + c0)), (max(y, max(z, max(x, w))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(z, max(max(w, x), u))) + c0)), (max(y, max(z, max(max(w, x), u))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(z, max(max(x, w), u))) + c0)), (max(y, max(z, max(max(x, w), u))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(max(x, z), w)) + c0)), (max(y, max(max(x, z), w)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(max(z, x), w)) + c0)), (max(y, max(max(z, x), w)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(max(z, max(w, x)), u)) + c0)), (max(y, max(max(z, max(w, x)), u)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(max(z, max(x, w)), u)) + c0)), (max(y, max(max(z, max(x, w)), u)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(max(max(x, z), w), u)) + c0)), (max(y, max(max(max(x, z), w), u)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(y, max(max(max(z, x), w), u)) + c0)), (max(y, max(max(max(z, x), w), u)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(x, y), z) + c0)), (max(max(x, y), z) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(y, x), z) + c0)), (max(max(y, x), z) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(y, max(x, z)), w) + c0)), (max(max(y, max(x, z)), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(y, max(z, x)), w) + c0)), (max(max(y, max(z, x)), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(y, max(z, max(w, x))), u) + c0)), (max(max(y, max(z, max(w, x))), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(y, max(z, max(x, w))), u) + c0)), (max(max(y, max(z, max(x, w))), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(y, max(max(x, z), w)), u) + c0)), (max(max(y, max(max(x, z), w)), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(y, max(max(z, x), w)), u) + c0)), (max(max(y, max(max(z, x), w)), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(max(x, y), z), w) + c0)), (max(max(max(x, y), z), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(max(y, x), z), w) + c0)), (max(max(max(y, x), z), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(max(y, max(x, z)), w), u) + c0)), (max(max(max(y, max(x, z)), w), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(max(y, max(z, x)), w), u) + c0)), (max(max(max(y, max(z, x)), w), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(max(max(x, y), z), w), u) + c0)), (max(max(max(max(x, y), z), w), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max(x, (max(max(max(max(y, x), z), w), u) + c0)), (max(max(max(max(y, x), z), w), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(max(x, max(x, y)), max(x, y)) ||
 rewrite(max(x, max(y, x)), max(y, x)) ||
 rewrite(max(x, max(y, (z + max(x, y)))), (max(z, 0) + max(x, y))) ||
 rewrite(max(x, max(y, (z + max(y, x)))), (max(z, 0) + max(y, x))) ||
 rewrite(max(x, max(y, (max(x, y) + z))), (max(z, 0) + max(x, y))) ||
 rewrite(max(x, max(y, (max(y, x) + z))), (max(z, 0) + max(y, x))) ||
 rewrite(max(x, max(y, max(x, z))), max(y, max(x, z))) ||
 rewrite(max(x, max(y, max(z, x))), max(y, max(z, x))) ||
 rewrite(max(x, max(y, max(z, max(w, x)))), max(x, max(y, max(z, w)))) ||
 rewrite(max(x, max(y, max(z, max(w, max(u, x))))), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(u, max(v, x)))))), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(u, max(x, v)))))), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(x, u))))), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(max(u, x), v))))), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(x, max(y, max(z, max(w, max(max(x, u), v))))), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(x, max(y, max(z, max(x, w)))), max(x, max(y, max(z, w)))) ||
 rewrite(max(x, max(y, max(z, max(max(w, x), u)))), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(x, max(y, max(z, max(max(w, max(u, x)), v)))), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(x, max(y, max(z, max(max(w, max(x, u)), v)))), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(x, max(y, max(z, max(max(x, w), u)))), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(x, max(y, max(z, max(max(max(w, x), u), v)))), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(x, max(y, max(z, max(max(max(x, w), u), v)))), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(x, max(y, max(max(x, z), w))), max(x, max(y, max(z, w)))) ||
 rewrite(max(x, max(y, max(max(z, x), w))), max(x, max(y, max(z, w)))) ||
 rewrite(max(x, max(y, max(max(z, max(w, x)), u))), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(x, max(y, max(max(z, max(w, max(u, x))), v))), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(x, max(y, max(max(z, max(w, max(x, u))), v))), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(x, max(y, max(max(z, max(x, w)), u))), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(x, max(y, max(max(z, max(max(w, x), u)), v))), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(x, max(y, max(max(z, max(max(x, w), u)), v))), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(x, max(y, max(max(max(x, z), w), u))), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(x, max(y, max(max(max(z, x), w), u))), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(x, max(y, max(max(max(z, max(w, x)), u), v))), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(x, max(y, max(max(max(z, max(x, w)), u), v))), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(x, max(y, max(max(max(max(x, z), w), u), v))), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(x, max(y, max(max(max(max(z, x), w), u), v))), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(x, max((y + max(x, z)), z)), (max(y, 0) + max(x, z))) ||
 rewrite(max(x, max((y + max(z, x)), z)), (max(y, 0) + max(x, z))) ||
 rewrite(max(x, max((max(x, y) + z), y)), (max(x, y) + max(z, 0))) ||
 rewrite(max(x, max((max(y, x) + z), y)), (max(y, x) + max(z, 0))) ||
 rewrite(max(x, max(max(x, y), z)), max(x, max(y, z))) ||
 rewrite(max(x, max(max(y, x), z)), max(x, max(y, z))) ||
 rewrite(max(x, max(max(y, max(x, z)), w)), max(x, max(max(y, z), w))) ||
 rewrite(max(x, max(max(y, max(z, x)), w)), max(x, max(max(y, z), w))) ||
 rewrite(max(x, max(max(y, max(z, max(w, x))), u)), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(x, max(max(y, max(z, max(w, max(u, x)))), v)), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(x, max(max(y, max(z, max(w, max(x, u)))), v)), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(x, max(max(y, max(z, max(x, w))), u)), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(x, max(max(y, max(z, max(max(w, x), u))), v)), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(x, max(max(y, max(z, max(max(x, w), u))), v)), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(x, max(max(y, max(max(x, z), w)), u)), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(x, max(max(y, max(max(z, x), w)), u)), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(x, max(max(y, max(max(z, max(w, x)), u)), v)), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(x, max(max(y, max(max(z, max(x, w)), u)), v)), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(x, max(max(y, max(max(max(x, z), w), u)), v)), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(x, max(max(y, max(max(max(z, x), w), u)), v)), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(x, max(max(max(x, y), z), w)), max(x, max(max(y, z), w))) ||
 rewrite(max(x, max(max(max(y, x), z), w)), max(x, max(max(y, z), w))) ||
 rewrite(max(x, max(max(max(y, max(x, z)), w), u)), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(x, max(max(max(y, max(z, x)), w), u)), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(x, max(max(max(y, max(z, max(w, x))), u), v)), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(x, max(max(max(y, max(z, max(x, w))), u), v)), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(x, max(max(max(y, max(max(x, z), w)), u), v)), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(x, max(max(max(y, max(max(z, x), w)), u), v)), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(x, max(max(max(max(x, y), z), w), u)), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(x, max(max(max(max(y, x), z), w), u)), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(x, max(max(max(max(y, max(x, z)), w), u), v)), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(x, max(max(max(max(y, max(z, x)), w), u), v)), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(x, max(max(max(max(max(x, y), z), w), u), v)), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(x, max(max(max(max(max(y, x), z), w), u), v)), max(x, max(max(max(max(y, z), w), u), v))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(max(x, select(y, z, x)), select(y, max(z, x), x)))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(max(((x - (y*c0)) + (z + (y*c1))), w), max((x + z), w), ((c1 <= c0) && (c0 == c1))) ||
 rewrite(max(((x - (y*c0)) + ((y*c1) + z)), w), max((x + z), w), ((c1 <= c0) && (c0 == c1))) ||
 rewrite(max((min(x, y) + c0), x), x, (c0 <= 0)) ||
 rewrite(max((min(x, y) + c0), y), y, (c0 <= 0)) ||
 rewrite(max((max(x, max(y, z)) + c0), y), (max(x, max(y, z)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(y, z)) + c0), z), (max(x, max(y, z)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(y, max(z, w))) + c0), w), (max(x, max(y, max(z, w))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(y, max(z, w))) + c0), z), (max(x, max(y, max(z, w))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(y, max(z, max(w, u)))) + c0), u), (max(x, max(y, max(z, max(w, u)))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(y, max(z, max(w, u)))) + c0), w), (max(x, max(y, max(z, max(w, u)))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(y, max(max(z, w), u))) + c0), w), (max(x, max(y, max(max(z, w), u))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(y, max(max(z, w), u))) + c0), z), (max(x, max(y, max(max(z, w), u))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(max(y, z), w)) + c0), y), (max(x, max(max(y, z), w)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(max(y, z), w)) + c0), z), (max(x, max(max(y, z), w)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(max(y, max(z, w)), u)) + c0), w), (max(x, max(max(y, max(z, w)), u)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(max(y, max(z, w)), u)) + c0), z), (max(x, max(max(y, max(z, w)), u)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(max(max(y, z), w), u)) + c0), y), (max(x, max(max(max(y, z), w), u)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(x, max(max(max(y, z), w), u)) + c0), z), (max(x, max(max(max(y, z), w), u)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(x, y), z) + c0), x), (max(max(x, y), z) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(x, y), z) + c0), y), (max(max(x, y), z) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(x, max(y, z)), w) + c0), y), (max(max(x, max(y, z)), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(x, max(y, z)), w) + c0), z), (max(max(x, max(y, z)), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(x, max(y, max(z, w))), u) + c0), w), (max(max(x, max(y, max(z, w))), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(x, max(y, max(z, w))), u) + c0), z), (max(max(x, max(y, max(z, w))), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(x, max(max(y, z), w)), u) + c0), y), (max(max(x, max(max(y, z), w)), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(x, max(max(y, z), w)), u) + c0), z), (max(max(x, max(max(y, z), w)), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(max(x, y), z), w) + c0), x), (max(max(max(x, y), z), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(max(x, y), z), w) + c0), y), (max(max(max(x, y), z), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(max(x, max(y, z)), w), u) + c0), y), (max(max(max(x, max(y, z)), w), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(max(x, max(y, z)), w), u) + c0), z), (max(max(max(x, max(y, z)), w), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(max(max(x, y), z), w), u) + c0), x), (max(max(max(max(x, y), z), w), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((max(max(max(max(x, y), z), w), u) + c0), y), (max(max(max(max(x, y), z), w), u) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x + y), (z + (w + (u + x)))), (x + max(y, (z + (w + u))))) ||
 rewrite(max((x + y), (z + (w + (u + y)))), (y + max(x, (z + (w + u))))) ||
 rewrite(max((x + y), (z + (w + (u + (v + x))))), (x + max(y, (z + (w + (u + v)))))) ||
 rewrite(max((x + y), (z + (w + (u + (v + y))))), (y + max(x, (z + (w + (u + v)))))) ||
 rewrite(max((x + y), (z + (w + (u + (x + v))))), (x + max(y, (z + (w + (u + v)))))) ||
 rewrite(max((x + y), (z + (w + (u + (y + v))))), (y + max(x, (z + (w + (u + v)))))) ||
 rewrite(max((x + y), (z + (w + (x + u)))), (x + max(y, (z + (w + u))))) ||
 rewrite(max((x + y), (z + (w + (y + u)))), (y + max(x, (z + (w + u))))) ||
 rewrite(max((x + y), (z + (w + ((u + x) + v)))), (x + max(y, (w + ((u + z) + v))))) ||
 rewrite(max((x + y), (z + (w + ((u + y) + v)))), (y + max(x, (w + ((u + z) + v))))) ||
 rewrite(max((x + y), (z + (w + ((x + u) + v)))), (x + max(y, (w + ((z + u) + v))))) ||
 rewrite(max((x + y), (z + (w + ((y + u) + v)))), (y + max(x, (w + ((z + u) + v))))) ||
 rewrite(max((x + y), (z + ((w + x) + u))), (x + max(y, ((w + z) + u)))) ||
 rewrite(max((x + y), (z + ((w + y) + u))), (y + max(x, ((w + z) + u)))) ||
 rewrite(max((x + y), (z + ((w + (u + x)) + v))), (x + max(y, ((w + (u + z)) + v)))) ||
 rewrite(max((x + y), (z + ((w + (u + y)) + v))), (y + max(x, ((w + (u + z)) + v)))) ||
 rewrite(max((x + y), (z + ((w + (x + u)) + v))), (x + max(y, ((w + (z + u)) + v)))) ||
 rewrite(max((x + y), (z + ((w + (y + u)) + v))), (y + max(x, ((w + (z + u)) + v)))) ||
 rewrite(max((x + y), (z + ((x + w) + u))), (x + max(y, ((z + w) + u)))) ||
 rewrite(max((x + y), (z + ((y + w) + u))), (y + max(x, ((z + w) + u)))) ||
 rewrite(max((x + y), (z + (((w + x) + u) + v))), (x + max(y, (((w + z) + u) + v)))) ||
 rewrite(max((x + y), (z + (((w + y) + u) + v))), (y + max(x, (((w + z) + u) + v)))) ||
 rewrite(max((x + y), (z + (((x + w) + u) + v))), (x + max(y, (((z + w) + u) + v)))) ||
 rewrite(max((x + y), (z + (((y + w) + u) + v))), (y + max(x, (((z + w) + u) + v)))) ||
 rewrite(max((x + y), (z + (x - w))), (x + max(y, (z - w)))) ||
 rewrite(max((x + y), (z + (y - w))), (y + max(x, (z - w)))) ||
 rewrite(max((x + y), ((z + (w + x)) + u)), (x + max((z + (w + u)), y))) ||
 rewrite(max((x + y), ((z + (w + y)) + u)), (y + max((z + (w + u)), x))) ||
 rewrite(max((x + y), ((z + (w + (u + x))) + v)), (x + max((z + (w + (u + v))), y))) ||
 rewrite(max((x + y), ((z + (w + (u + y))) + v)), (y + max((z + (w + (u + v))), x))) ||
 rewrite(max((x + y), ((z + (w + (x + u))) + v)), (x + max((z + (w + (v + u))), y))) ||
 rewrite(max((x + y), ((z + (w + (y + u))) + v)), (y + max((z + (w + (v + u))), x))) ||
 rewrite(max((x + y), ((z + (x + w)) + u)), (x + max((z + (u + w)), y))) ||
 rewrite(max((x + y), ((z + (y + w)) + u)), (y + max((z + (u + w)), x))) ||
 rewrite(max((x + y), ((z + ((w + x) + u)) + v)), (x + max((z + ((w + v) + u)), y))) ||
 rewrite(max((x + y), ((z + ((w + y) + u)) + v)), (y + max((z + ((w + v) + u)), x))) ||
 rewrite(max((x + y), ((z + ((x + w) + u)) + v)), (x + max((z + ((v + w) + u)), y))) ||
 rewrite(max((x + y), ((z + ((y + w) + u)) + v)), (y + max((z + ((v + w) + u)), x))) ||
 rewrite(max((x + y), (((x + z) + w) + u)), (x + max(((u + z) + w), y))) ||
 rewrite(max((x + y), (((y + z) + w) + u)), (y + max(((u + z) + w), x))) ||
 rewrite(max((x + y), (((z + x) + w) + u)), (x + max(((z + u) + w), y))) ||
 rewrite(max((x + y), (((z + y) + w) + u)), (y + max(((z + u) + w), x))) ||
 rewrite(max((x + y), (((z + (w + x)) + u) + v)), (x + max(((z + (w + v)) + u), y))) ||
 rewrite(max((x + y), (((z + (w + y)) + u) + v)), (y + max(((z + (w + v)) + u), x))) ||
 rewrite(max((x + y), (((z + (x + w)) + u) + v)), (x + max(((z + (v + w)) + u), y))) ||
 rewrite(max((x + y), (((z + (y + w)) + u) + v)), (y + max(((z + (v + w)) + u), x))) ||
 rewrite(max((x + y), ((((x + z) + w) + u) + v)), (x + max((((v + z) + w) + u), y))) ||
 rewrite(max((x + y), ((((y + z) + w) + u) + v)), (y + max((((v + z) + w) + u), x))) ||
 rewrite(max((x + y), ((((z + x) + w) + u) + v)), (x + max((((z + v) + w) + u), y))) ||
 rewrite(max((x + y), ((((z + y) + w) + u) + v)), (y + max((((z + v) + w) + u), x))) ||
 rewrite(max((x + y), ((x - z) + w)), (x + max((w - z), y))) ||
 rewrite(max((x + y), ((y - z) + w)), (y + max((w - z), x))) ||
 rewrite(max((x + (y + z)), (w + y)), (max(w, (x + z)) + y)) ||
 rewrite(max((x + (y + z)), (w + z)), (max(w, (y + x)) + z)) ||
 rewrite(max((x + (y + z)), (w + (u + y))), (y + max((x + z), (w + u)))) ||
 rewrite(max((x + (y + z)), (w + (u + z))), (z + max((y + x), (w + u)))) ||
 rewrite(max((x + (y + z)), (w + (y + u))), (y + max((z + x), (w + u)))) ||
 rewrite(max((x + (y + z)), (w + (z + u))), (z + max((y + x), (w + u)))) ||
 rewrite(max((x + (y + z)), (y + w)), (max(w, (x + z)) + y)) ||
 rewrite(max((x + (y + z)), (z + w)), (max(w, (y + x)) + z)) ||
 rewrite(max((x + (y + z)), ((w + y) + u)), (y + max((x + z), (u + w)))) ||
 rewrite(max((x + (y + z)), ((w + z) + u)), (z + max((y + x), (u + w)))) ||
 rewrite(max((x + (y + z)), ((y + w) + u)), (y + max((x + z), (u + w)))) ||
 rewrite(max((x + (y + z)), ((z + w) + u)), (z + max((y + x), (u + w)))) ||
 rewrite(max((x + (y + (z + w))), (u + w)), (max(u, (y + (z + x))) + w)) ||
 rewrite(max((x + (y + (z + w))), (u + z)), (max(u, (y + (x + w))) + z)) ||
 rewrite(max((x + (y + (z + w))), (u + (w + z))), (max(u, (y + x)) + (w + z))) ||
 rewrite(max((x + (y + (z + w))), (w + u)), (max(u, (y + (z + x))) + w)) ||
 rewrite(max((x + (y + (z + w))), (z + u)), (max(u, (y + (x + w))) + z)) ||
 rewrite(max((x + (y + (z + w))), ((w + z) + u)), (max(u, (y + x)) + (w + z))) ||
 rewrite(max((x + (y + (z - w))), (u + z)), (max(u, (y + (x - w))) + z)) ||
 rewrite(max((x + (y + (z - w))), (z + u)), (max(u, (y + (x - w))) + z)) ||
 rewrite(max((x + (y + (z*c0))), (w + (z*c1))), (max(w, (y + x)) + (z*fold(max(c0, c1)))), ((c0 <= c1) && (max((c1*5), ((c0*5) + 2)) == ((max(c0, c1)*5) + 2)))) ||
 rewrite(max((x + (y + (z*c0))), ((z*c1) + w)), (max(w, (y + x)) + (z*fold(max(c0, c1)))), ((c0 <= c1) && (max((c1*5), ((c0*5) + 2)) == ((max(c0, c1)*5) + 2)))) ||
 rewrite(max((x + (y + (z*w))), (u + (w*z))), (max(u, (y + x)) + (w*z))) ||
 rewrite(max((x + (y + (z*w))), ((w*z) + u)), (max(u, (y + x)) + (w*z))) ||
 rewrite(max((x + ((y + z) + w)), (u + y)), (max(u, ((x + z) + w)) + y)) ||
 rewrite(max((x + ((y + z) + w)), (u + z)), (max(u, ((y + x) + w)) + z)) ||
 rewrite(max((x + ((y + z) + w)), (u + (z + y))), (max(u, (x + w)) + (z + y))) ||
 rewrite(max((x + ((y + z) + w)), (y + u)), (max(u, ((x + z) + w)) + y)) ||
 rewrite(max((x + ((y + z) + w)), (z + u)), (max(u, ((y + x) + w)) + z)) ||
 rewrite(max((x + ((y + z) + w)), ((z + y) + u)), (max(u, (x + w)) + (z + y))) ||
 rewrite(max((x + ((y - z) + w)), (u + y)), (max(u, ((x - z) + w)) + y)) ||
 rewrite(max((x + ((y - z) + w)), (y + u)), (max(u, ((x - z) + w)) + y)) ||
 rewrite(max((x + ((y*c0) + z)), (w + (y*c1))), (max(w, (x + z)) + (y*fold(max(c0, c1)))), ((c0 <= c1) && (max((c1*5), ((c0*5) + 2)) == ((max(c0, c1)*5) + 2)))) ||
 rewrite(max((x + ((y*c0) + z)), ((y*c1) + w)), (max(w, (z + x)) + (y*fold(max(c0, c1)))), ((c0 <= c1) && (max((c1*5), ((c0*5) + 2)) == ((max(c0, c1)*5) + 2)))) ||
 rewrite(max((x + ((y*z) + w)), (u + (z*y))), (max(u, (x + w)) + (z*y))) ||
 rewrite(max((x + ((y*z) + w)), ((z*y) + u)), (max(u, (x + w)) + (z*y))) ||
 rewrite(max((x + (y - z)), (w + y)), (max(w, (x - z)) + y)) ||
 rewrite(max((x + (y - z)), (w + (u - z))), (max((y + x), (w + u)) - z)) ||
 rewrite(max((x + (y - z)), (y + w)), (max(w, (x - z)) + y)) ||
 rewrite(max((x + (y - z)), ((w - z) + u)), (max((y + x), (u + w)) - z)) ||
 rewrite(max((x + ((y + z) - w)), (u + y)), (max(u, ((x + z) - w)) + y)) ||
 rewrite(max((x + ((y + z) - w)), (u + z)), (max(u, ((y + x) - w)) + z)) ||
 rewrite(max((x + ((y + z) - w)), (y + u)), (max(u, ((x + z) - w)) + y)) ||
 rewrite(max((x + ((y + z) - w)), (z + u)), (max(u, ((y + x) - w)) + z)) ||
 rewrite(max((x + (y*c0)), (y + (y + z))), (max(x, z) + (y*fold(max(c0, 2)))), (max(c0, 2) == min(c0, 2))) ||
 rewrite(max((x + (y*c0)), (y + (z + y))), (max(x, z) + (y*fold(max(c0, 2)))), (max(c0, 2) == min(c0, 2))) ||
 rewrite(max((x + (y*c0)), ((y + z) + y)), (max(z, x) + (y*fold(max(c0, 2)))), (max(c0, 2) == min(c0, 2))) ||
 rewrite(max((x + (y*c0)), ((z + y) + y)), (max(z, x) + (y*fold(max(c0, 2)))), (max(c0, 2) == min(c0, 2))) ||
 rewrite(max(((x + y) + z), (w + x)), (x + max((y + z), w))) ||
 rewrite(max(((x + y) + z), (w + y)), (y + max((x + z), w))) ||
 rewrite(max(((x + y) + z), (w + (u + x))), (x + max((y + z), (w + u)))) ||
 rewrite(max(((x + y) + z), (w + (u + y))), (y + max((x + z), (w + u)))) ||
 rewrite(max(((x + y) + z), (w + (x + u))), (x + max((y + z), (w + u)))) ||
 rewrite(max(((x + y) + z), (w + (y + u))), (y + max((x + z), (w + u)))) ||
 rewrite(max(((x + y) + z), (x + w)), (x + max((y + z), w))) ||
 rewrite(max(((x + y) + z), (y + w)), (y + max((x + z), w))) ||
 rewrite(max(((x + y) + z), ((w + x) + u)), (x + max((y + z), (u + w)))) ||
 rewrite(max(((x + y) + z), ((w + y) + u)), (y + max((x + z), (w + u)))) ||
 rewrite(max(((x + y) + z), ((x + w) + u)), (x + max((y + z), (u + w)))) ||
 rewrite(max(((x + y) + z), ((y + w) + u)), (y + max((x + z), (u + w)))) ||
 rewrite(max(((x + (y + z)) + w), (u + y)), (max((x + (w + z)), u) + y)) ||
 rewrite(max(((x + (y + z)) + w), (u + z)), (max((x + (y + w)), u) + z)) ||
 rewrite(max(((x + (y + z)) + w), (u + (z + y))), (max((x + w), u) + (z + y))) ||
 rewrite(max(((x + (y + z)) + w), (y + u)), (max((x + (w + z)), u) + y)) ||
 rewrite(max(((x + (y + z)) + w), (z + u)), (max((x + (y + w)), u) + z)) ||
 rewrite(max(((x + (y + z)) + w), ((z + y) + u)), (max((x + w), u) + (z + y))) ||
 rewrite(max(((x + (y - z)) + w), (u + y)), (max((x + (w - z)), u) + y)) ||
 rewrite(max(((x + (y - z)) + w), (y + u)), (max((x + (w - z)), u) + y)) ||
 rewrite(max(((x + (y*c0)) + z), (w + (y*c1))), (max(w, (x + z)) + (y*fold(max(c0, c1)))), ((c0 <= c1) && (max((c1*5), ((c0*5) + 2)) == ((max(c0, c1)*5) + 2)))) ||
 rewrite(max(((x + (y*c0)) + z), ((y*c1) + w)), (max(w, (x + z)) + (y*fold(max(c0, c1)))), ((c0 <= c1) && (max((c1*5), ((c0*5) + 2)) == ((max(c0, c1)*5) + 2)))) ||
 rewrite(max(((x + (y*z)) + w), (u + (z*y))), (max((x + w), u) + (z*y))) ||
 rewrite(max(((x + (y*z)) + w), ((z*y) + u)), (max((x + w), u) + (z*y))) ||
 rewrite(max((((x + y) + z) + w), (u + x)), (x + max(((y + z) + w), u))) ||
 rewrite(max((((x + y) + z) + w), (u + y)), (y + max(((x + z) + w), u))) ||
 rewrite(max((((x + y) + z) + w), (u + (y + x))), (max(u, (z + w)) + (y + x))) ||
 rewrite(max((((x + y) + z) + w), (x + u)), (x + max(((y + z) + w), u))) ||
 rewrite(max((((x + y) + z) + w), (y + u)), (y + max(((x + z) + w), u))) ||
 rewrite(max((((x + y) + z) + w), ((y + x) + u)), (max(u, (z + w)) + (y + x))) ||
 rewrite(max((((x - y) + z) + w), (u + x)), (max(((w - y) + z), u) + x)) ||
 rewrite(max((((x - y) + z) + w), (x + u)), (max(((w - y) + z), u) + x)) ||
 rewrite(max((((x*c0) + y) + z), (w + (x*c1))), (max(w, (y + z)) + (x*fold(max(c0, c1)))), ((c0 <= c1) && (max((c1*5), ((c0*5) + 2)) == ((max(c0, c1)*5) + 2)))) ||
 rewrite(max((((x*c0) + y) + z), ((x*c1) + w)), (max(w, (y + z)) + (x*fold(max(c0, c1)))), ((c0 <= c1) && (max((c1*5), ((c0*5) + 2)) == ((max(c0, c1)*5) + 2)))) ||
 rewrite(max((((x*y) + z) + w), (u + (y*x))), (max(u, (z + w)) + (y*x))) ||
 rewrite(max((((x*y) + z) + w), ((y*x) + u)), (max(u, (z + w)) + (y*x))) ||
 rewrite(max(((x - y) + z), (w + x)), (max((z - y), w) + x)) ||
 rewrite(max(((x - y) + z), (w + (u - y))), (max((x + z), (w + u)) - y)) ||
 rewrite(max(((x - y) + z), (x + w)), (max((z - y), w) + x)) ||
 rewrite(max(((x - y) + z), ((w - y) + u)), (max((x + z), (u + w)) - y)) ||
 rewrite(max((((x + y) - z) + w), (u + x)), (x + max(((y - z) + w), u))) ||
 rewrite(max((((x + y) - z) + w), (u + y)), (y + max(((x - z) + w), u))) ||
 rewrite(max((((x + y) - z) + w), (x + u)), (x + max(((y - z) + w), u))) ||
 rewrite(max((((x + y) - z) + w), (y + u)), (y + max(((x - z) + w), u))) ||
 rewrite(max(((x*c0) + y), (x + (x + z))), (max(z, y) + (x*fold(max(c0, 2)))), (max(c0, 2) == min(c0, 2))) ||
 rewrite(max(((x*c0) + y), (x + (z + x))), (max(z, y) + (x*fold(max(c0, 2)))), (max(c0, 2) == min(c0, 2))) ||
 rewrite(max(((x*c0) + y), ((x + z) + x)), (max(z, y) + (x*fold(max(c0, 2)))), (max(c0, 2) == min(c0, 2))) ||
 rewrite(max(((x*c0) + y), ((z + x) + x)), (max(z, y) + (x*fold(max(c0, 2)))), (max(c0, 2) == min(c0, 2))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x - y), (z + (w - y))), (max(x, (z + w)) - y)) ||
 rewrite(max((x - y), (z + (w - (u + y)))), (max(x, (z + (w - u))) - y)) ||
 rewrite(max((x - y), (z + (w - (y + u)))), (max(x, (z + (w - u))) - y)) ||
 rewrite(max((x - y), ((z - y) + w)), (max(x, (z + w)) - y)) ||
 rewrite(max((x - y), ((z - (w + y)) + u)), (max(x, (z - (w - u))) - y)) ||
 rewrite(max((x - y), ((z - (y + w)) + u)), (max(x, (z + (u - w))) - y)) ||
 rewrite(max((x - (y + z)), (w + (u + (v - y)))), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max((x - (y + z)), (w + (u + (v - z)))), (max((x - y), (w + (u + v))) - z)) ||
 rewrite(max((x - (y + z)), (w + ((u - y) + v))), (max((x - z), (w + (v + u))) - y)) ||
 rewrite(max((x - (y + z)), (w + ((u - z) + v))), (max((x - y), (w + (v + u))) - z)) ||
 rewrite(max((x - (y + z)), (w + (u - (v + y)))), (max((x - z), (w + (u - v))) - y)) ||
 rewrite(max((x - (y + z)), (w + (u - (v + z)))), (max((x - y), (w + (u - v))) - z)) ||
 rewrite(max((x - (y + z)), (w + (u - (y + v)))), (max((x - z), (w + (u - v))) - y)) ||
 rewrite(max((x - (y + z)), (w + (u - (z + v)))), (max((x - y), (w + (u - v))) - z)) ||
 rewrite(max((x - (y + z)), ((w + (u - y)) + v)), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max((x - (y + z)), ((w + (u - z)) + v)), (max((x - y), (w + (u + v))) - z)) ||
 rewrite(max((x - (y + z)), (((w - y) + u) + v)), (max((x - z), ((v + w) + u)) - y)) ||
 rewrite(max((x - (y + z)), (((w - z) + u) + v)), (max((x - y), ((v + w) + u)) - z)) ||
 rewrite(max((x - (y + z)), ((w - (u + y)) + v)), (max((x - z), (w - (u - v))) - y)) ||
 rewrite(max((x - (y + z)), ((w - (u + z)) + v)), (max((x - y), (w - (u - v))) - z)) ||
 rewrite(max((x - (y + z)), ((w - (y + u)) + v)), (max((x - z), (w + (v - u))) - y)) ||
 rewrite(max((x - (y + z)), ((w - (z + u)) + v)), (max((x - y), (w + (v - u))) - z)) ||
 rewrite(max(((x + y) - z), (w + x)), (x + max((y - z), w))) ||
 rewrite(max(((x + y) - z), (w + y)), (y + max((x - z), w))) ||
 rewrite(max(((x + y) - z), (w + (u + x))), (x + max((y - z), (w + u)))) ||
 rewrite(max(((x + y) - z), (w + (u + y))), (y + max((x - z), (w + u)))) ||
 rewrite(max(((x + y) - z), (w + (u + (x + y)))), (y + max((x - z), (w + (u + x))))) ||
 rewrite(max(((x + y) - z), (w + (u + (y + x)))), (y + max((x - z), (w + (u + x))))) ||
 rewrite(max(((x + y) - z), (w + (x + u))), (x + max((y - z), (w + u)))) ||
 rewrite(max(((x + y) - z), (w + (y + u))), (y + max((x - z), (w + u)))) ||
 rewrite(max(((x + y) - z), (w + ((x + y) + u))), (x + max((y - z), (w + (u + y))))) ||
 rewrite(max(((x + y) - z), (w + ((y + x) + u))), (y + max((x - z), (w + (u + x))))) ||
 rewrite(max(((x + y) - z), (x + w)), (x + max((y - z), w))) ||
 rewrite(max(((x + y) - z), (y + w)), (y + max((x - z), w))) ||
 rewrite(max(((x + y) - z), ((w + x) + u)), (x + max((y - z), (u + w)))) ||
 rewrite(max(((x + y) - z), ((w + y) + u)), (y + max((x - z), (u + w)))) ||
 rewrite(max(((x + y) - z), ((w + (x + y)) + u)), (y + max((x - z), (w + (x + u))))) ||
 rewrite(max(((x + y) - z), ((w + (y + x)) + u)), (y + max((x - z), (w + (u + x))))) ||
 rewrite(max(((x + y) - z), ((x + w) + u)), (x + max((y - z), (u + w)))) ||
 rewrite(max(((x + y) - z), ((y + w) + u)), (y + max((x - z), (u + w)))) ||
 rewrite(max(((x + y) - z), (((x + y) + w) + u)), (x + max((y - z), ((u + y) + w)))) ||
 rewrite(max(((x + y) - z), (((y + x) + w) + u)), (x + max((y - z), ((u + y) + w)))) ||
 rewrite(max(((x + (y + z)) - w), (u + (v + y))), (y + max((x + (z - w)), (u + v)))) ||
 rewrite(max(((x + (y + z)) - w), (u + (v + z))), (z + max((x + (y - w)), (u + v)))) ||
 rewrite(max(((x + (y + z)) - w), (u + (y + v))), (y + max((x + (z - w)), (u + v)))) ||
 rewrite(max(((x + (y + z)) - w), (u + (z + v))), (z + max((x + (y - w)), (u + v)))) ||
 rewrite(max(((x + (y + z)) - w), ((u + y) + v)), (y + max((x + (z - w)), (v + u)))) ||
 rewrite(max(((x + (y + z)) - w), ((u + z) + v)), (z + max((x + (y - w)), (v + u)))) ||
 rewrite(max(((x + (y + z)) - w), ((y + u) + v)), (y + max((x + (z - w)), (v + u)))) ||
 rewrite(max(((x + (y + z)) - w), ((z + u) + v)), (z + max((x + (y - w)), (v + u)))) ||
 rewrite(max((((x + y) + z) - w), (u + (v + x))), (x + max(((y + z) - w), (u + v)))) ||
 rewrite(max((((x + y) + z) - w), (u + (v + y))), (y + max(((x + z) - w), (u + v)))) ||
 rewrite(max((((x + y) + z) - w), (u + (x + v))), (x + max(((y + z) - w), (u + v)))) ||
 rewrite(max((((x + y) + z) - w), (u + (y + v))), (y + max(((x + z) - w), (u + v)))) ||
 rewrite(max((((x + y) + z) - w), ((u + x) + v)), (x + max(((y + z) - w), (v + u)))) ||
 rewrite(max((((x + y) + z) - w), ((u + y) + v)), (y + max(((x + z) - w), (v + u)))) ||
 rewrite(max((((x + y) + z) - w), ((x + u) + v)), (x + max(((y + z) - w), (v + u)))) ||
 rewrite(max((((x + y) + z) - w), ((y + u) + v)), (y + max(((x + z) - w), (v + u)))) ||
 rewrite(max(((x - y) - z), (w + (u + (v - y)))), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max(((x - y) - z), (w + ((u - y) + v))), (max((x - z), (w + (v + u))) - y)) ||
 rewrite(max(((x - y) - z), ((w + (u - y)) + v)), (max((x - z), (w + (u + v))) - y)) ||
 rewrite(max(((x - y) - z), (((w - y) + u) + v)), (max((x - z), ((v + w) + u)) - y)) ||
 rewrite(max((((x + y) - z) - w), (u + (v + x))), (x + max(((y - z) - w), (u + v)))) ||
 rewrite(max((((x + y) - z) - w), (u + (v + y))), (y + max(((x - z) - w), (u + v)))) ||
 rewrite(max((((x + y) - z) - w), (u + (x + v))), (x + max(((y - z) - w), (u + v)))) ||
 rewrite(max((((x + y) - z) - w), (u + (y + v))), (y + max(((x - z) - w), (u + v)))) ||
 rewrite(max((((x + y) - z) - w), ((u + x) + v)), (x + max(((y - z) - w), (u + v)))) ||
 rewrite(max((((x + y) - z) - w), ((u + y) + v)), (y + max(((x - z) - w), (u + v)))) ||
 rewrite(max((((x + y) - z) - w), ((x + u) + v)), (x + max(((y - z) - w), (v + u)))) ||
 rewrite(max((((x + y) - z) - w), ((y + u) + v)), (y + max(((x - z) - w), (v + u)))))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(max((x - y), (z - (w + y))), (max(x, (z - w)) - y)) ||
 rewrite(max((x - y), (z - (y + w))), (max(x, (z - w)) - y)) ||
 rewrite(max((x - (y + z)), (w - (u + y))), (max((x - z), (w - u)) - y)) ||
 rewrite(max((x - (y + z)), (w - (u + z))), (max((x - y), (w - u)) - z)) ||
 rewrite(max((x - (y + z)), (w - (y + u))), (max((x - z), (w - u)) - y)) ||
 rewrite(max((x - (y + z)), (w - (z + u))), (max((x - y), (w - u)) - z)) ||
 rewrite(max((x - (y + z)), ((w + (u - y)) - v)), (max((x - z), (w + (u - v))) - y)) ||
 rewrite(max((x - (y + z)), ((w + (u - z)) - v)), (max((x - y), (w + (u - v))) - z)) ||
 rewrite(max((x - (y + z)), (((w - y) + u) - v)), (max((x - z), ((w - v) + u)) - y)) ||
 rewrite(max((x - (y + z)), (((w - z) + u) - v)), (max((x - y), ((w - v) + u)) - z)) ||
 rewrite(max((x - (y + z)), ((w - (u + y)) - v)), (max((x - z), (w - (u + v))) - y)) ||
 rewrite(max((x - (y + z)), ((w - (u + z)) - v)), (max((x - y), (w - (u + v))) - z)) ||
 rewrite(max((x - (y + z)), ((w - (y + u)) - v)), (max((x - z), (w - (v + u))) - y)) ||
 rewrite(max((x - (y + z)), ((w - (z + u)) - v)), (max((x - y), (w - (v + u))) - z)) ||
 rewrite(max((x - (y*c0)), (z - (w + (y*c1)))), (max(x, (z - w)) - (y*fold(min(c0, c1)))), (((max((1 - (c0*4)), (0 - (c1*4))) + (min(c0, c1)*4)) == 1) && ((max(((1 - c1)*2), (0 - (c0*2))) + (min(c0, c1)*2)) == 2))) ||
 rewrite(max((x - (y*c0)), (z - ((y*c1) + w))), (max(x, (z - w)) - (y*fold(min(c0, c1)))), (((max((1 - (c0*4)), (0 - (c1*4))) + (min(c0, c1)*4)) == 1) && ((max(((1 - c1)*2), (0 - (c0*2))) + (min(c0, c1)*2)) == 2))) ||
 rewrite(max((x - (y*z)), (w - (u + (z*y)))), (max(x, (w - u)) - (z*y))) ||
 rewrite(max((x - (y*z)), (w - ((z*y) + u))), (max(x, (w - u)) - (z*y))) ||
 rewrite(max(((x + y) - z), (x - (w + (u - v)))), (x + max((y - z), (v - (w + u))))) ||
 rewrite(max(((x + y) - z), (x - ((w - u) + v))), (x + max((y - z), ((u - w) - v)))) ||
 rewrite(max(((x + y) - z), (y - (w + (u - v)))), (y + max((x - z), (v - (w + u))))) ||
 rewrite(max(((x + y) - z), (y - ((w - u) + v))), (y + max((x - z), ((u - w) - v)))) ||
 rewrite(max(((x + y) - z), ((w + x) - u)), (x + max((y - z), (w - u)))) ||
 rewrite(max(((x + y) - z), ((w + y) - u)), (y + max((x - z), (w - u)))) ||
 rewrite(max(((x + y) - z), ((x + w) - u)), (x + max((y - z), (w - u)))) ||
 rewrite(max(((x + y) - z), ((y + w) - u)), (y + max((x - z), (w - u)))) ||
 rewrite(max(((x + y) - (z + w)), (x - u)), (x - min((w + (z - y)), u))) ||
 rewrite(max(((x + y) - (z + w)), (y - u)), (y - min((w + (z - x)), u))) ||
 rewrite(max(((x + y) - (z + w)), ((x - u) - v)), (x - min((w + (z - y)), (v + u)))) ||
 rewrite(max(((x + y) - (z + w)), ((y - u) - v)), (y - min((w + (z - x)), (v + u)))) ||
 rewrite(max(((x + (y - z)) - w), (u - (v + z))), (max(((y + x) - w), (u - v)) - z)) ||
 rewrite(max(((x + (y - z)) - w), (u - (z + v))), (max(((y + x) - w), (u - v)) - z)) ||
 rewrite(max((((x - y) + z) - w), (u - (v + y))), (max(((x + z) - w), (u - v)) - y)) ||
 rewrite(max((((x - y) + z) - w), (u - (y + v))), (max(((x + z) - w), (u - v)) - y)) ||
 rewrite(max(((x - y) - z), (w - (u + y))), (max((x - z), (w - u)) - y)) ||
 rewrite(max(((x - y) - z), (w - (y + u))), (max((x - z), (w - u)) - y)))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x*c0), (x + y)), (x + max(x, y)), (c0 == 2)) ||
 rewrite(max((x*c0), (x + max(x, y))), (x + max(x, y)), (((c0 <= 2) && ((c0*-1) <= 4)) && ((c0*-1) <= -2))) ||
 rewrite(max((x*c0), (x + max(y, x))), (x + max(y, x)), (((c0 <= 2) && ((c0*-1) <= 4)) && ((c0*-1) <= -2))) ||
 rewrite(max((x*c0), (y + x)), (x + max(y, x)), (c0 == 2)) ||
 rewrite(max((x*c0), (max(x, y) + x)), (x + max(y, x)), (((c0 <= 2) && ((c0*-1) <= 4)) && ((c0*-1) <= -2))) ||
 rewrite(max((x*c0), (max(y, x) + x)), (x + max(y, x)), (((c0 <= 2) && ((c0*-1) <= 4)) && ((c0*-1) <= -2))) ||
 rewrite(max((x*y), (max(z, max(w, (y*x))) + c0)), (max(z, max(w, (y*x))) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((x*y), (max(z, max((y*x), w)) + c0)), (max(z, max((y*x), w)) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((x*y), (max(max(z, (y*x)), w) + c0)), (max(max(z, (y*x)), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)) ||
 rewrite(max((x*y), (max(max((y*x), z), w) + c0)), (max(max((y*x), z), w) + fold(max(c0, -1))), (max(min(c0, 0), -1) == 0)))))) ||
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
 rewrite(max(min(min(x, y), z), min(w, x)), min(x, max(min(y, z), w))) ||
 rewrite(max(min(min(x, y), z), min(w, y)), min(y, max(min(x, z), w))) ||
 rewrite(max(min(min(x, y), z), min(x, w)), min(x, max(min(y, z), w))) ||
 rewrite(max(min(min(x, y), z), min(y, w)), min(y, max(min(x, z), w))) ||
 rewrite(max(min(min(x, (y + z)), w), min(u, (z + y))), min(max(min(x, w), u), (z + y))) ||
 rewrite(max(min(min(x, (y + z)), w), min((z + y), u)), min(max(min(x, w), u), (z + y))) ||
 rewrite(max(min(min((x + y), z), w), min(u, (y + x))), min(max(u, min(z, w)), (y + x))) ||
 rewrite(max(min(min((x + y), z), w), min((y + x), u)), min(max(u, min(z, w)), (y + x))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite(max(max(x, y), x), max(y, x)) ||
 rewrite(max(max(x, y), y), max(x, y)) ||
 rewrite(max(max(x, (y + max(x, z))), z), (max(y, 0) + max(x, z))) ||
 rewrite(max(max(x, (y + max(z, x))), z), (max(y, 0) + max(z, x))) ||
 rewrite(max(max(x, (max(x, y) + z)), y), (max(z, 0) + max(x, y))) ||
 rewrite(max(max(x, (max(y, x) + z)), y), (max(z, 0) + max(y, x))) ||
 rewrite(max(max(x, max(y, z)), y), max(x, max(y, z))) ||
 rewrite(max(max(x, max(y, z)), z), max(x, max(y, z))) ||
 rewrite(max(max(x, max(y, max(z, w))), w), max(x, max(y, max(z, w)))) ||
 rewrite(max(max(x, max(y, max(z, w))), z), max(x, max(y, max(z, w)))) ||
 rewrite(max(max(x, max(y, max(z, max(w, u)))), u), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(max(x, max(y, max(z, max(w, u)))), w), max(x, max(y, max(z, max(w, u))))) ||
 rewrite(max(max(x, max(y, max(z, max(w, max(u, v))))), u), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(max(x, max(y, max(z, max(w, max(u, v))))), v), max(x, max(y, max(z, max(w, max(u, v)))))) ||
 rewrite(max(max(x, max(y, max(z, max(max(w, u), v)))), u), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(max(x, max(y, max(z, max(max(w, u), v)))), w), max(x, max(y, max(z, max(max(w, u), v))))) ||
 rewrite(max(max(x, max(y, max(max(z, w), u))), w), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(max(x, max(y, max(max(z, w), u))), z), max(x, max(y, max(max(z, w), u)))) ||
 rewrite(max(max(x, max(y, max(max(z, max(w, u)), v))), u), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(max(x, max(y, max(max(z, max(w, u)), v))), w), max(x, max(y, max(max(z, max(w, u)), v)))) ||
 rewrite(max(max(x, max(y, max(max(max(z, w), u), v))), w), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(max(x, max(y, max(max(max(z, w), u), v))), z), max(x, max(y, max(max(max(z, w), u), v)))) ||
 rewrite(max(max(x, max(max(y, z), w)), y), max(x, max(max(y, z), w))) ||
 rewrite(max(max(x, max(max(y, z), w)), z), max(x, max(max(y, z), w))) ||
 rewrite(max(max(x, max(max(y, max(z, w)), u)), w), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(max(x, max(max(y, max(z, w)), u)), z), max(x, max(max(y, max(z, w)), u))) ||
 rewrite(max(max(x, max(max(y, max(z, max(w, u))), v)), u), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(max(x, max(max(y, max(z, max(w, u))), v)), w), max(x, max(max(y, max(z, max(w, u))), v))) ||
 rewrite(max(max(x, max(max(y, max(max(z, w), u)), v)), w), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(max(x, max(max(y, max(max(z, w), u)), v)), z), max(x, max(max(y, max(max(z, w), u)), v))) ||
 rewrite(max(max(x, max(max(max(y, z), w), u)), y), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(max(x, max(max(max(y, z), w), u)), z), max(x, max(max(max(y, z), w), u))) ||
 rewrite(max(max(x, max(max(max(y, max(z, w)), u), v)), w), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(max(x, max(max(max(y, max(z, w)), u), v)), z), max(x, max(max(max(y, max(z, w)), u), v))) ||
 rewrite(max(max(x, max(max(max(max(y, z), w), u), v)), y), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(max(x, max(max(max(max(y, z), w), u), v)), z), max(x, max(max(max(max(y, z), w), u), v))) ||
 rewrite(max(max((x + max(y, z)), y), z), (max(x, 0) + max(z, y))) ||
 rewrite(max(max((x + max(y, z)), z), y), (max(x, 0) + max(y, z))) ||
 rewrite(max(max((max(x, y) + z), x), y), (max(x, y) + max(z, 0))) ||
 rewrite(max(max((max(x, y) + z), y), x), (max(x, y) + max(z, 0))) ||
 rewrite(max(max(max(x, y), z), x), max(max(y, z), x)) ||
 rewrite(max(max(max(x, y), z), y), max(max(x, z), y)) ||
 rewrite(max(max(max(x, max(y, z)), w), y), max(max(x, max(y, z)), w)) ||
 rewrite(max(max(max(x, max(y, z)), w), z), max(max(x, max(y, z)), w)) ||
 rewrite(max(max(max(x, max(y, max(z, w))), u), w), max(max(x, max(y, max(z, w))), u)) ||
 rewrite(max(max(max(x, max(y, max(z, w))), u), z), max(max(x, max(y, max(z, w))), u)) ||
 rewrite(max(max(max(x, max(y, max(z, max(w, u)))), v), u), max(max(x, max(y, max(z, max(w, u)))), v)) ||
 rewrite(max(max(max(x, max(y, max(z, max(w, u)))), v), w), max(max(x, max(y, max(z, max(w, u)))), v)) ||
 rewrite(max(max(max(x, max(y, max(max(z, w), u))), v), w), max(max(x, max(y, max(max(z, w), u))), v)) ||
 rewrite(max(max(max(x, max(y, max(max(z, w), u))), v), z), max(max(x, max(y, max(max(z, w), u))), v)) ||
 rewrite(max(max(max(x, max(max(y, z), w)), u), y), max(max(x, max(max(y, z), w)), u)) ||
 rewrite(max(max(max(x, max(max(y, z), w)), u), z), max(max(x, max(max(y, z), w)), u)) ||
 rewrite(max(max(max(x, max(max(y, max(z, w)), u)), v), w), max(max(x, max(max(y, max(z, w)), u)), v)) ||
 rewrite(max(max(max(x, max(max(y, max(z, w)), u)), v), z), max(max(x, max(max(y, max(z, w)), u)), v)) ||
 rewrite(max(max(max(x, max(max(max(y, z), w), u)), v), y), max(max(x, max(max(max(y, z), w), u)), v)) ||
 rewrite(max(max(max(x, max(max(max(y, z), w), u)), v), z), max(max(x, max(max(max(y, z), w), u)), v)) ||
 rewrite(max(max(max(max(x, y), z), w), x), max(max(max(x, y), z), w)) ||
 rewrite(max(max(max(max(x, y), z), w), y), max(max(max(x, y), z), w)) ||
 rewrite(max(max(max(max(x, max(y, z)), w), u), y), max(max(max(x, max(y, z)), w), u)) ||
 rewrite(max(max(max(max(x, max(y, z)), w), u), z), max(max(max(x, max(y, z)), w), u)) ||
 rewrite(max(max(max(max(x, max(y, max(z, w))), u), v), w), max(max(max(x, max(y, max(z, w))), u), v)) ||
 rewrite(max(max(max(max(x, max(y, max(z, w))), u), v), z), max(max(max(x, max(y, max(z, w))), u), v)) ||
 rewrite(max(max(max(max(x, max(max(y, z), w)), u), v), y), max(max(max(x, max(max(y, z), w)), u), v)) ||
 rewrite(max(max(max(max(x, max(max(y, z), w)), u), v), z), max(max(max(x, max(max(y, z), w)), u), v)) ||
 rewrite(max(max(max(max(max(x, y), z), w), u), x), max(max(max(max(x, y), z), w), u)) ||
 rewrite(max(max(max(max(max(x, y), z), w), u), y), max(max(max(max(x, y), z), w), u)) ||
 rewrite(max(max(max(max(max(x, max(y, z)), w), u), v), y), max(max(max(max(x, max(y, z)), w), u), v)) ||
 rewrite(max(max(max(max(max(x, max(y, z)), w), u), v), z), max(max(max(max(x, max(y, z)), w), u), v)) ||
 rewrite(max(max(max(max(max(max(x, y), z), w), u), v), x), max(max(max(max(max(x, y), z), w), u), v)) ||
 rewrite(max(max(max(max(max(max(x, y), z), w), u), v), y), max(max(max(max(max(x, y), z), w), u), v)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max(max(x, (y + z)), (w + (u + y))), max(x, (y + max(z, (w + u))))) ||
 rewrite(max(max(x, (y + z)), (w + (u + z))), max(x, (z + max(y, (w + u))))) ||
 rewrite(max(max(x, (y + z)), (w + (y + u))), max(x, (y + max(z, (w + u))))) ||
 rewrite(max(max(x, (y + z)), (w + (z + u))), max(x, (z + max(y, (w + u))))) ||
 rewrite(max(max(x, (y + z)), ((w + y) + u)), max(x, (y + max(z, (w + u))))) ||
 rewrite(max(max(x, (y + z)), ((w + z) + u)), max(x, (z + max(y, (w + u))))) ||
 rewrite(max(max(x, (y + z)), ((y + w) + u)), max(x, (y + max(z, (u + w))))) ||
 rewrite(max(max(x, (y + z)), ((z + w) + u)), max(x, (z + max(y, (u + w))))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (v + w))), max(x, (w + max((z + y), (u + v))))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (v + z))), max(x, (z + max((w + y), (u + v))))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (w + v))), max(x, (w + max((z + y), (u + v))))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (z + v))), max(x, (z + max((w + y), (u + v))))) ||
 rewrite(max(max(x, (y + (z + w))), ((u + w) + v)), max(x, (max((z + y), (v + u)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), ((u + z) + v)), max(x, (z + max((w + y), (v + u))))) ||
 rewrite(max(max(x, (y + (z + w))), ((w + u) + v)), max(x, (w + max((z + y), (v + u))))) ||
 rewrite(max(max(x, (y + (z + w))), ((z + u) + v)), max(x, (z + max((w + y), (v + u))))) ||
 rewrite(max(max(x, (y + (z - w))), (u + (v - w))), max(x, (max((z + y), (u + v)) - w))) ||
 rewrite(max(max(x, (y + (z - w))), ((u - w) + v)), max(x, (max((z + y), (v + u)) - w))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (v + y))), max(x, (y + max((z + w), (u + v))))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (v + z))), max(x, (z + max((y + w), (u + v))))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (y + v))), max(x, (y + max((z + w), (u + v))))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (z + v))), max(x, (z + max((y + w), (u + v))))) ||
 rewrite(max(max(x, ((y + z) + w)), ((u + y) + v)), max(x, (max((z + w), (v + u)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), ((u + z) + v)), max(x, (z + max((y + w), (v + u))))) ||
 rewrite(max(max(x, ((y + z) + w)), ((y + u) + v)), max(x, (y + max((z + w), (v + u))))) ||
 rewrite(max(max(x, ((y + z) + w)), ((z + u) + v)), max(x, (z + max((y + w), (v + u))))) ||
 rewrite(max(max(x, ((y - z) + w)), (u + (v - z))), max(x, (max((y + w), (u + v)) - z))) ||
 rewrite(max(max(x, ((y - z) + w)), ((u - z) + v)), max(x, (max((y + w), (v + u)) - z))) ||
 rewrite(max(max(x, (y - z)), (w + (u + (v - z)))), max(x, (max(y, (w + (u + v))) - z))) ||
 rewrite(max(max(x, (y - z)), (w + ((u - z) + v))), max(x, (max(y, (w + (u + v))) - z))) ||
 rewrite(max(max(x, (y - z)), (w + (u - z))), max(x, (max(y, (w + u)) - z))) ||
 rewrite(max(max(x, (y - z)), ((w + (u - z)) + v)), max(x, (max(y, (v + (w + u))) - z))) ||
 rewrite(max(max(x, (y - z)), (((w - z) + u) + v)), max(x, (max(y, (v + (w + u))) - z))) ||
 rewrite(max(max(x, (y - z)), ((w - z) + u)), max(x, (max(y, (u + w)) - z))) ||
 rewrite(max(max(x, (y*z)), (w + (y*z))), max(x, (max(w, 0) + (y*z)))) ||
 rewrite(max(max(x, (y*z)), (w + (z*y))), max(x, (max(w, 0) + (z*y)))) ||
 rewrite(max(max(x, (y*z)), ((y*z) + w)), max(x, (max(w, 0) + (y*z)))) ||
 rewrite(max(max(x, (y*z)), ((z*y) + w)), max(x, (max(w, 0) + (z*y)))) ||
 rewrite(max(max(x, min(y, z)), (w + min(y, z))), max(x, (max(w, 0) + min(y, z)))) ||
 rewrite(max(max(x, min(y, z)), (w + min(z, y))), max(x, (max(w, 0) + min(z, y)))) ||
 rewrite(max(max(x, min(y, z)), (min(y, z) + w)), max(x, (max(w, 0) + min(y, z)))) ||
 rewrite(max(max(x, min(y, z)), (min(z, y) + w)), max(x, (max(w, 0) + min(z, y)))) ||
 rewrite(max(max(x, max(y, (z + w))), (u + w)), max(max(x, (w + max(z, u))), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (u + z)), max(x, max(y, (z + max(w, u))))) ||
 rewrite(max(max(x, max(y, (z + w))), (w + u)), max(max(x, (w + max(z, u))), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (z + u)), max(x, max(y, (z + max(w, u))))) ||
 rewrite(max(max(x, max(y, (z - w))), (u + (v - w))), max(x, max(y, (max(z, (u + v)) - w)))) ||
 rewrite(max(max(x, max(y, (z - w))), ((u - w) + v)), max(x, max((max(z, (v + u)) - w), y))) ||
 rewrite(max(max(x, max(y, min(z, w))), (u + min(w, z))), max(max(x, y), (max(u, 0) + min(w, z)))) ||
 rewrite(max(max(x, max(y, min(z, w))), (u + min(z, w))), max(max(x, y), (max(u, 0) + min(z, w)))) ||
 rewrite(max(max(x, max(y, min(z, w))), (min(w, z) + u)), max(x, max(y, (max(u, 0) + min(w, z))))) ||
 rewrite(max(max(x, max(y, min(z, w))), (min(z, w) + u)), max(x, max(y, (max(u, 0) + min(z, w))))) ||
 rewrite(max(max(x, max(y, max(z, w))), (u + max(w, y))), max(max(x, z), (max(u, 0) + max(w, y)))) ||
 rewrite(max(max(x, max(y, max(z, w))), (u + max(y, w))), max(max(x, z), (max(u, 0) + max(y, w)))) ||
 rewrite(max(max(x, max(y, max(z, w))), (u + max(y, z))), max(max(x, w), (max(u, 0) + max(y, z)))) ||
 rewrite(max(max(x, max(y, max(z, w))), (u + max(z, y))), max(max(x, w), (max(u, 0) + max(z, y)))) ||
 rewrite(max(max(x, max(y, max(z, w))), (max(w, y) + u)), max(x, max(z, (max(u, 0) + max(w, y))))) ||
 rewrite(max(max(x, max(y, max(z, w))), (max(y, w) + u)), max(x, max(z, (max(u, 0) + max(y, w))))) ||
 rewrite(max(max(x, max(y, max(z, w))), (max(y, z) + u)), max(x, max(w, (max(u, 0) + max(y, z))))) ||
 rewrite(max(max(x, max(y, max(z, w))), (max(z, y) + u)), max(x, max(w, (max(u, 0) + max(z, y))))) ||
 rewrite(max(max(x, max(y, max(z, (w + u)))), (u + v)), max(max(x, max(y, (u + max(w, v)))), z)) ||
 rewrite(max(max(x, max(y, max(z, (w + u)))), (v + u)), max(max(x, max(y, (u + max(w, v)))), z)) ||
 rewrite(max(max(x, max(y, max(z, (w + u)))), (v + w)), max(x, max(y, max(z, (w + max(u, v)))))) ||
 rewrite(max(max(x, max(y, max(z, (w + u)))), (w + v)), max(x, max(y, max(z, (w + max(u, v)))))) ||
 rewrite(max(max(x, max(y, max((z + w), u))), (v + w)), max(max(x, max(y, (max(z, v) + w))), u)) ||
 rewrite(max(max(x, max(y, max((z + w), u))), (v + z)), max(max(x, max(y, (max(v, w) + z))), u)) ||
 rewrite(max(max(x, max(y, max((z + w), u))), (w + v)), max(max(x, max(y, (max(z, v) + w))), u)) ||
 rewrite(max(max(x, max(y, max((z + w), u))), (z + v)), max(max(x, max(y, (max(v, w) + z))), u)) ||
 rewrite(max(max(x, max((y + z), w)), (u + y)), max(max(x, (max(u, z) + y)), w)) ||
 rewrite(max(max(x, max((y + z), w)), (u + z)), max(max(x, (max(y, u) + z)), w)) ||
 rewrite(max(max(x, max((y + z), w)), (y + u)), max(max(x, (max(u, z) + y)), w)) ||
 rewrite(max(max(x, max((y + z), w)), (z + u)), max(max(x, (max(y, u) + z)), w)) ||
 rewrite(max(max(x, max((y - z), w)), (u + (v - z))), max(w, max(x, (max(y, (u + v)) - z)))) ||
 rewrite(max(max(x, max((y - z), w)), ((u - z) + v)), max(w, max(x, (max(y, (v + u)) - z)))) ||
 rewrite(max(max(x, max(min(y, z), w)), (u + min(y, z))), max(max(x, w), (max(u, 0) + min(y, z)))) ||
 rewrite(max(max(x, max(min(y, z), w)), (u + min(z, y))), max(max(x, w), (max(u, 0) + min(z, y)))) ||
 rewrite(max(max(x, max(min(y, z), w)), (min(y, z) + u)), max(w, max(x, (max(u, 0) + min(y, z))))) ||
 rewrite(max(max(x, max(min(y, z), w)), (min(z, y) + u)), max(w, max(x, (max(u, 0) + min(z, y))))) ||
 rewrite(max(max(x, max(max(y, z), w)), (u + max(w, y))), max(z, max(x, (max(y, w) + max(u, 0))))) ||
 rewrite(max(max(x, max(max(y, z), w)), (u + max(w, z))), max(y, max(x, (max(z, w) + max(u, 0))))) ||
 rewrite(max(max(x, max(max(y, z), w)), (u + max(y, w))), max(z, max(x, (max(y, w) + max(u, 0))))) ||
 rewrite(max(max(x, max(max(y, z), w)), (u + max(z, w))), max(y, max(x, (max(z, w) + max(u, 0))))) ||
 rewrite(max(max(x, max(max(y, z), w)), (max(w, y) + u)), max(x, max(z, (max(u, 0) + max(w, y))))) ||
 rewrite(max(max(x, max(max(y, z), w)), (max(w, z) + u)), max(x, max((max(z, w) + max(u, 0)), y))) ||
 rewrite(max(max(x, max(max(y, z), w)), (max(y, w) + u)), max(x, max(z, (max(u, 0) + max(y, w))))) ||
 rewrite(max(max(x, max(max(y, z), w)), (max(z, w) + u)), max(x, max((max(z, w) + max(u, 0)), y))) ||
 rewrite(max(max(x, max(max(y, (z + w)), u)), (v + w)), max(max(x, max((w + max(z, v)), u)), y)) ||
 rewrite(max(max(x, max(max(y, (z + w)), u)), (v + z)), max(max(x, max((z + max(v, w)), u)), y)) ||
 rewrite(max(max(x, max(max(y, (z + w)), u)), (w + v)), max(max(x, max((w + max(z, v)), u)), y)) ||
 rewrite(max(max(x, max(max(y, (z + w)), u)), (z + v)), max(max(x, max((z + max(v, w)), u)), y)) ||
 rewrite(max(max(x, max(max((y + z), w), u)), (v + y)), max(max(x, max((max(v, z) + y), u)), w)) ||
 rewrite(max(max(x, max(max((y + z), w), u)), (v + z)), max(max(x, max((max(y, v) + z), u)), w)) ||
 rewrite(max(max(x, max(max((y + z), w), u)), (y + v)), max(max(x, max((max(v, z) + y), u)), w)) ||
 rewrite(max(max(x, max(max((y + z), w), u)), (z + v)), max(max(x, max((max(y, v) + z), u)), w)) ||
 rewrite(max(max((x + y), z), (w + (u + x))), max(z, (x + max(y, (w + u))))) ||
 rewrite(max(max((x + y), z), (w + (u + y))), max(z, (y + max(x, (w + u))))) ||
 rewrite(max(max((x + y), z), (w + (x + u))), max(z, (x + max(y, (w + u))))) ||
 rewrite(max(max((x + y), z), (w + (y + u))), max(z, (y + max(x, (w + u))))) ||
 rewrite(max(max((x + y), z), ((w + x) + u)), max(z, (x + max(y, (w + u))))) ||
 rewrite(max(max((x + y), z), ((w + y) + u)), max(z, (y + max(x, (w + u))))) ||
 rewrite(max(max((x + y), z), ((x + w) + u)), max(z, (x + max(y, (u + w))))) ||
 rewrite(max(max((x + y), z), ((y + w) + u)), max(z, (y + max(x, (u + w))))) ||
 rewrite(max(max((x + (y + z)), w), (u + (v + y))), max(w, (y + max((z + x), (u + v))))) ||
 rewrite(max(max((x + (y + z)), w), (u + (v + z))), max(w, (z + max((y + x), (u + v))))) ||
 rewrite(max(max((x + (y + z)), w), (u + (y + v))), max(w, (y + max((z + x), (u + v))))) ||
 rewrite(max(max((x + (y + z)), w), (u + (z + v))), max(w, (z + max((y + x), (u + v))))) ||
 rewrite(max(max((x + (y + z)), w), ((u + y) + v)), max(w, (y + max((z + x), (v + u))))) ||
 rewrite(max(max((x + (y + z)), w), ((u + z) + v)), max(w, (max((y + x), (v + u)) + z))) ||
 rewrite(max(max((x + (y + z)), w), ((y + u) + v)), max(w, (y + max((z + x), (v + u))))) ||
 rewrite(max(max((x + (y + z)), w), ((z + u) + v)), max(w, (z + max((y + x), (v + u))))) ||
 rewrite(max(max((x + (y - z)), w), (u + (v - z))), max(w, (max((y + x), (u + v)) - z))) ||
 rewrite(max(max((x + (y - z)), w), ((u - z) + v)), max(w, (max((y + x), (v + u)) - z))) ||
 rewrite(max(max(((x + y) + z), w), (u + (v + x))), max(w, (x + max((y + z), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), (u + (v + y))), max(w, (y + max((x + z), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), (u + (x + v))), max(w, (x + max((y + z), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), (u + (y + v))), max(w, (y + max((x + z), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), ((u + x) + v)), max(w, (max((y + z), (v + u)) + x))) ||
 rewrite(max(max(((x + y) + z), w), ((u + y) + v)), max(w, (max((x + z), (v + u)) + y))) ||
 rewrite(max(max(((x + y) + z), w), ((x + u) + v)), max(w, (x + max((y + z), (v + u))))) ||
 rewrite(max(max(((x + y) + z), w), ((y + u) + v)), max(w, (y + max((x + z), (v + u))))) ||
 rewrite(max(max(((x - y) + z), w), (u + (v - y))), max(w, (max((x + z), (u + v)) - y))) ||
 rewrite(max(max(((x - y) + z), w), ((u - y) + v)), max(w, (max((x + z), (v + u)) - y))) ||
 rewrite(max(max((x - y), z), (w + (u + (v - y)))), max(z, (max(x, (w + (u + v))) - y))) ||
 rewrite(max(max((x - y), z), (w + ((u - y) + v))), max(z, (max(x, (w + (u + v))) - y))) ||
 rewrite(max(max((x - y), z), (w + (u - y))), max(z, (max(x, (w + u)) - y))) ||
 rewrite(max(max((x - y), z), ((w + (u - y)) + v)), max(z, (max(x, (v + (w + u))) - y))) ||
 rewrite(max(max((x - y), z), (((w - y) + u) + v)), max(z, (max(x, (v + (w + u))) - y))) ||
 rewrite(max(max((x - y), z), ((w - y) + u)), max(z, (max(x, (u + w)) - y))) ||
 rewrite(max(max((x*y), z), (w + (x*y))), max(z, (max(w, 0) + (x*y)))) ||
 rewrite(max(max((x*y), z), (w + (y*x))), max(z, (max(w, 0) + (y*x)))) ||
 rewrite(max(max((x*y), z), ((x*y) + w)), max(z, (max(w, 0) + (x*y)))) ||
 rewrite(max(max((x*y), z), ((y*x) + w)), max(z, (max(w, 0) + (y*x)))) ||
 rewrite(max(max(min(x, y), z), (w + min(x, y))), max(z, (max(w, 0) + min(x, y)))) ||
 rewrite(max(max(min(x, y), z), (w + min(y, x))), max(z, (max(w, 0) + min(y, x)))) ||
 rewrite(max(max(min(x, y), z), (min(x, y) + w)), max(z, (max(w, 0) + min(x, y)))) ||
 rewrite(max(max(min(x, y), z), (min(y, x) + w)), max(z, (max(w, 0) + min(y, x)))) ||
 rewrite(max(max(max(x, (y + z)), w), (u + y)), max(max((y + max(u, z)), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (u + z)), max(max((z + max(y, u)), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (y + u)), max(max((y + max(u, z)), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (z + u)), max(max((z + max(y, u)), w), x)) ||
 rewrite(max(max(max(x, (y - z)), w), (u + (v - z))), max(w, max(x, (max(y, (u + v)) - z)))) ||
 rewrite(max(max(max(x, (y - z)), w), ((u - z) + v)), max(w, max((max(y, (v + u)) - z), x))) ||
 rewrite(max(max(max(x, min(y, z)), w), (u + min(y, z))), max(max(x, w), (max(u, 0) + min(y, z)))) ||
 rewrite(max(max(max(x, min(y, z)), w), (u + min(z, y))), max(max(x, w), (max(u, 0) + min(z, y)))) ||
 rewrite(max(max(max(x, min(y, z)), w), (min(y, z) + u)), max(w, max(x, (max(u, 0) + min(y, z))))) ||
 rewrite(max(max(max(x, min(y, z)), w), (min(z, y) + u)), max(w, max(x, (max(u, 0) + min(z, y))))) ||
 rewrite(max(max(max(x, max(y, z)), w), (u + max(x, y))), max(max(z, w), (max(u, 0) + max(x, y)))) ||
 rewrite(max(max(max(x, max(y, z)), w), (u + max(x, z))), max(max(y, w), (max(u, 0) + max(x, z)))) ||
 rewrite(max(max(max(x, max(y, z)), w), (u + max(y, x))), max(max(z, w), (max(u, 0) + max(y, x)))) ||
 rewrite(max(max(max(x, max(y, z)), w), (u + max(z, x))), max(max(y, w), (max(u, 0) + max(z, x)))) ||
 rewrite(max(max(max(x, max(y, z)), w), (max(x, y) + u)), max(max(z, w), (max(x, y) + max(u, 0)))) ||
 rewrite(max(max(max(x, max(y, z)), w), (max(x, z) + u)), max(max(y, w), (max(x, z) + max(u, 0)))) ||
 rewrite(max(max(max(x, max(y, z)), w), (max(y, x) + u)), max(max(z, w), (max(y, x) + max(u, 0)))) ||
 rewrite(max(max(max(x, max(y, z)), w), (max(z, x) + u)), max(max(y, w), (max(z, x) + max(u, 0)))) ||
 rewrite(max(max(max(x, max(y, (z + w))), u), (v + w)), max(max(max(x, (w + max(z, v))), u), y)) ||
 rewrite(max(max(max(x, max(y, (z + w))), u), (v + z)), max(max(max(x, (z + max(v, w))), u), y)) ||
 rewrite(max(max(max(x, max(y, (z + w))), u), (w + v)), max(max(max(x, (w + max(z, v))), u), y)) ||
 rewrite(max(max(max(x, max(y, (z + w))), u), (z + v)), max(max(max(x, (z + max(v, w))), u), y)) ||
 rewrite(max(max(max(x, max((y + z), w)), u), (v + y)), max(max(max(x, (max(v, z) + y)), u), w)) ||
 rewrite(max(max(max(x, max((y + z), w)), u), (v + z)), max(max(max(x, (max(y, v) + z)), u), w)) ||
 rewrite(max(max(max(x, max((y + z), w)), u), (y + v)), max(max(max(x, (max(v, z) + y)), u), w)) ||
 rewrite(max(max(max(x, max((y + z), w)), u), (z + v)), max(max(max(x, (max(y, v) + z)), u), w)) ||
 rewrite(max(max(max((x + y), z), w), (u + x)), max(max((max(u, y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (u + y)), max(max((max(x, u) + y), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (x + u)), max(max((max(u, y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (y + u)), max(max((max(x, u) + y), w), z)) ||
 rewrite(max(max(max((x - y), z), w), (u + (v - y))), max(w, max((max(x, (u + v)) - y), z))) ||
 rewrite(max(max(max((x - y), z), w), ((u - y) + v)), max(w, max((max(x, (v + u)) - y), z))) ||
 rewrite(max(max(max(min(x, y), z), w), (u + min(x, y))), max(max(z, w), (max(u, 0) + min(x, y)))) ||
 rewrite(max(max(max(min(x, y), z), w), (u + min(y, x))), max(max(z, w), (max(u, 0) + min(y, x)))) ||
 rewrite(max(max(max(min(x, y), z), w), (min(x, y) + u)), max(max(z, w), (max(u, 0) + min(x, y)))) ||
 rewrite(max(max(max(min(x, y), z), w), (min(y, x) + u)), max(max(z, w), (max(u, 0) + min(y, x)))) ||
 rewrite(max(max(max(max(x, y), z), w), (u + max(x, z))), max(max(y, w), (max(u, 0) + max(x, z)))) ||
 rewrite(max(max(max(max(x, y), z), w), (u + max(y, z))), max(max(x, w), (max(u, 0) + max(y, z)))) ||
 rewrite(max(max(max(max(x, y), z), w), (u + max(z, x))), max(max(y, w), (max(u, 0) + max(z, x)))) ||
 rewrite(max(max(max(max(x, y), z), w), (u + max(z, y))), max(max(x, w), (max(u, 0) + max(z, y)))) ||
 rewrite(max(max(max(max(x, y), z), w), (max(x, z) + u)), max(max(y, w), (max(x, z) + max(u, 0)))) ||
 rewrite(max(max(max(max(x, y), z), w), (max(y, z) + u)), max(max(x, w), (max(u, 0) + max(y, z)))) ||
 rewrite(max(max(max(max(x, y), z), w), (max(z, x) + u)), max(max(y, w), (max(z, x) + max(u, 0)))) ||
 rewrite(max(max(max(max(x, y), z), w), (max(z, y) + u)), max(max(x, w), (max(u, 0) + max(z, y)))) ||
 rewrite(max(max(max(max(x, (y + z)), w), u), (v + y)), max(max(max((y + max(v, z)), w), u), x)) ||
 rewrite(max(max(max(max(x, (y + z)), w), u), (v + z)), max(max(max((z + max(y, v)), w), u), x)) ||
 rewrite(max(max(max(max(x, (y + z)), w), u), (y + v)), max(max(max((y + max(v, z)), w), u), x)) ||
 rewrite(max(max(max(max(x, (y + z)), w), u), (z + v)), max(max(max((z + max(y, v)), w), u), x)) ||
 rewrite(max(max(max(max((x + y), z), w), u), (v + x)), max(max(max((max(v, y) + x), w), u), z)) ||
 rewrite(max(max(max(max((x + y), z), w), u), (v + y)), max(max(max((max(x, v) + y), w), u), z)) ||
 rewrite(max(max(max(max((x + y), z), w), u), (x + v)), max(max(max((max(v, y) + x), w), u), z)) ||
 rewrite(max(max(max(max((x + y), z), w), u), (y + v)), max(max(max((max(x, v) + y), w), u), z)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(max(max(x, (y + (z - w))), (u - w)), max(x, (max((z + y), u) - w))) ||
 rewrite(max(max(x, ((y - z) + w)), (u - z)), max(x, (max(u, (y + w)) - z))) ||
 rewrite(max(max(x, (y - z)), (w - z)), max(x, (max(y, w) - z))) ||
 rewrite(max(max(x, max(y, (z + (w - u)))), (v - u)), max(x, max((max(v, (w + z)) - u), y))) ||
 rewrite(max(max(x, max(y, ((z - w) + u))), (v - w)), max(x, max((max((z + u), v) - w), y))) ||
 rewrite(max(max(x, max(y, (z - w))), (u - w)), max(x, max((max(z, u) - w), y))) ||
 rewrite(max(max(x, max(y, max(z, (w - u)))), (v - u)), max(x, max(y, max(z, (max(w, v) - u))))) ||
 rewrite(max(max(x, max(y, max((z - w), u))), (v - w)), max(max(x, max(y, (max(z, v) - w))), u)) ||
 rewrite(max(max(x, max((y + (z - w)), u)), (v - w)), max(max(x, (max(v, (z + y)) - w)), u)) ||
 rewrite(max(max(x, max(((y - z) + w), u)), (v - z)), max(max(x, (max((y + w), v) - z)), u)) ||
 rewrite(max(max(x, max((y - z), w)), (u - z)), max(max(x, (max(y, u) - z)), w)) ||
 rewrite(max(max(x, max(max(y, (z - w)), u)), (v - w)), max(max(x, (max(z, v) - w)), max(u, y))) ||
 rewrite(max(max(x, max(max((y - z), w), u)), (v - z)), max(max(x, max((max(y, v) - z), u)), w)) ||
 rewrite(max(max((x + (y - z)), w), (u - z)), max(w, (max(u, (y + x)) - z))) ||
 rewrite(max(max(((x - y) + z), w), (u - y)), max(w, (max((x + z), u) - y))) ||
 rewrite(max(max((x - y), z), (w - y)), max(z, (max(x, w) - y))) ||
 rewrite(max(max(max(x, (y + (z - w))), u), (v - w)), max(u, max(x, (max((z + y), v) - w)))) ||
 rewrite(max(max(max(x, ((y - z) + w)), u), (v - z)), max(u, max(x, (max(v, (y + w)) - z)))) ||
 rewrite(max(max(max(x, (y - z)), w), (u - z)), max(max((max(y, u) - z), w), x)) ||
 rewrite(max(max(max(x, max(y, (z - w))), u), (v - w)), max(y, max(x, max((max(z, v) - w), u)))) ||
 rewrite(max(max(max(x, max((y - z), w)), u), (v - z)), max(max(max(x, (max(y, v) - z)), u), w)) ||
 rewrite(max(max(max((x + (y - z)), w), u), (v - z)), max(max((max(v, (y + x)) - z), u), w)) ||
 rewrite(max(max(max(((x - y) + z), w), u), (v - y)), max(max((max((x + z), v) - y), u), w)) ||
 rewrite(max(max(max((x - y), z), w), (u - y)), max(max((max(x, u) - y), w), z)) ||
 rewrite(max(max(max(max(x, (y - z)), w), u), (v - z)), max(max((max(y, v) - z), u), max(w, x))) ||
 rewrite(max(max(max(max((x - y), z), w), u), (v - y)), max(max(max((max(x, v) - y), w), u), z)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(max(max(x, (y + (z*w))), (w*z)), max(x, (max(y, 0) + (w*z)))) ||
 rewrite(max(max(x, (y + (z*w))), (z*w)), max(x, (max(y, 0) + (z*w)))) ||
 rewrite(max(max(x, ((y*z) + w)), (y*z)), max(x, (max(w, 0) + (y*z)))) ||
 rewrite(max(max(x, ((y*z) + w)), (z*y)), max(x, (max(w, 0) + (z*y)))) ||
 rewrite(max(max(x, ((y*z) - w)), (y*z)), max(x, ((y*z) - min(w, 0)))) ||
 rewrite(max(max(x, ((y*z) - w)), (z*y)), max(x, ((y*z) - min(w, 0)))) ||
 rewrite(max(max((x + (y*z)), w), (y*z)), max((max(x, 0) + (y*z)), w)) ||
 rewrite(max(max((x + (y*z)), w), (z*y)), max((max(x, 0) + (y*z)), w)) ||
 rewrite(max(max(((x*y) + z), w), (x*y)), max(w, (max(z, 0) + (x*y)))) ||
 rewrite(max(max(((x*y) + z), w), (y*x)), max(w, (max(z, 0) + (y*x)))) ||
 rewrite(max(max(((x*y) - z), w), (x*y)), max(w, ((x*y) - min(z, 0)))) ||
 rewrite(max(max(((x*y) - z), w), (y*x)), max(w, ((x*y) - min(z, 0)))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(max(x, y), min(x, z)), max(x, y)) ||
 rewrite(max(max(x, y), min(y, z)), max(x, y)) ||
 rewrite(max(max(x, y), min(z, x)), max(x, y)) ||
 rewrite(max(max(x, y), min(z, y)), max(x, y)) ||
 rewrite(max(max(x, (y + min(z, w))), min(w, z)), max(x, (max(y, 0) + min(w, z)))) ||
 rewrite(max(max(x, (y + min(z, w))), min(z, w)), max(x, (max(y, 0) + min(z, w)))) ||
 rewrite(max(max(x, (min(y, z) + w)), min(y, z)), max(x, (max(w, 0) + min(y, z)))) ||
 rewrite(max(max(x, (min(y, z) + w)), min(z, y)), max(x, (max(w, 0) + min(z, y)))) ||
 rewrite(max(max(x, min(y, z)), min(w, y)), max(x, min(max(z, w), y))) ||
 rewrite(max(max(x, min(y, z)), min(w, z)), max(x, min(z, max(y, w)))) ||
 rewrite(max(max(x, min(y, z)), min(y, w)), max(x, min(max(z, w), y))) ||
 rewrite(max(max(x, min(y, z)), min(z, w)), max(x, min(z, max(y, w)))) ||
 rewrite(max(max(x, max(y, min(z, w))), min(u, w)), max(max(x, min(w, max(z, u))), y)) ||
 rewrite(max(max(x, max(y, min(z, w))), min(u, z)), max(x, max(y, min(z, max(w, u))))) ||
 rewrite(max(max(x, max(y, min(z, w))), min(w, u)), max(max(x, min(w, max(z, u))), y)) ||
 rewrite(max(max(x, max(y, min(z, w))), min(z, u)), max(x, max(y, min(z, max(w, u))))) ||
 rewrite(max(max(x, max(min(y, z), w)), min(u, y)), max(max(x, min(max(u, z), y)), w)) ||
 rewrite(max(max(x, max(min(y, z), w)), min(u, z)), max(max(x, min(max(y, u), z)), w)) ||
 rewrite(max(max(x, max(min(y, z), w)), min(y, u)), max(max(x, min(max(u, z), y)), w)) ||
 rewrite(max(max(x, max(min(y, z), w)), min(z, u)), max(max(x, min(max(y, u), z)), w)) ||
 rewrite(max(max((x + min(y, z)), w), min(y, z)), max((max(x, 0) + min(y, z)), w)) ||
 rewrite(max(max((x + min(y, z)), w), min(z, y)), max((max(x, 0) + min(y, z)), w)) ||
 rewrite(max(max((min(x, y) + z), w), min(x, y)), max(w, (max(z, 0) + min(x, y)))) ||
 rewrite(max(max((min(x, y) + z), w), min(y, x)), max(w, (max(z, 0) + min(y, x)))) ||
 rewrite(max(max(min(x, y), z), min(w, x)), max(z, min(max(y, w), x))) ||
 rewrite(max(max(min(x, y), z), min(w, y)), max(z, min(max(x, w), y))) ||
 rewrite(max(max(min(x, y), z), min(x, w)), max(z, min(max(y, w), x))) ||
 rewrite(max(max(min(x, y), z), min(y, w)), max(z, min(max(x, w), y))) ||
 rewrite(max(max(max(x, min(y, z)), w), min(u, y)), max(max(min(y, max(u, z)), w), x)) ||
 rewrite(max(max(max(x, min(y, z)), w), min(u, z)), max(max(min(z, max(y, u)), w), x)) ||
 rewrite(max(max(max(x, min(y, z)), w), min(y, u)), max(max(min(y, max(u, z)), w), x)) ||
 rewrite(max(max(max(x, min(y, z)), w), min(z, u)), max(max(min(z, max(y, u)), w), x)) ||
 rewrite(max(max(max(min(x, y), z), w), min(u, x)), max(max(min(max(u, y), x), w), z)) ||
 rewrite(max(max(max(min(x, y), z), w), min(u, y)), max(max(min(max(x, u), y), w), z)) ||
 rewrite(max(max(max(min(x, y), z), w), min(x, u)), max(max(min(max(u, y), x), w), z)) ||
 rewrite(max(max(max(min(x, y), z), w), min(y, u)), max(max(min(max(x, u), y), w), z)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite(max(select(x, y, z), z), select(x, max(y, z), z)) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(max(select(x, y, z), max(w, select(x, u, z))), max(select(x, max(y, u), z), w)) ||
 rewrite(max(select(x, y, z), max(select(x, w, z), u)), max(select(x, max(y, w), z), u)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(max(select(x, c0, c1), select(y, c2, c3)), select((y || x), fold(max(c1, c2)), fold(max(c1, c3))), ((max(c0, c3) == max(c1, c2)) && (max(c1, c2) == max(c0, c2)))) ||
 rewrite(max(select(x, c0, c1), select(y, c2, c3)), select((y && x), fold(max(c0, c2)), fold(max(c1, c2))), ((max(c1, c3) == max(c1, c2)) && (max(c0, c3) == max(c1, c2))))))))
