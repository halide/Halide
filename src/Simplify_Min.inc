((b.node_type() == IRNodeType::Add) && (
 rewrite(min(x, (y + (z + ((w*c0) + (u - w))))), min(x, (y + (z + (u + w)))), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(x, (y + (((z*c0) + (w - z)) + u))), min(x, (y + ((w + z) + u))), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(x, (y + ((z*c0) + (w - z)))), min(x, (y + (w + z))), ((((1 <= c0) && (c0 <= 6)) && (-22 <= (c0*-7))) && ((min(c0, 3)*7) == 14))) ||
 rewrite(min(x, ((y + ((z*c0) + (w - z))) + u)), min(x, ((y + (w + z)) + u)), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(x, ((((y*c0) + (z - y)) + w) + u)), min(x, (((z + y) + w) + u)), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(x, (((y*c0) + (z - y)) + w)), min(x, ((z + y) + w)), ((((1 <= c0) && (c0 <= 6)) && (-22 <= (c0*-7))) && ((min(c0, 3)*7) == 14))) ||
 rewrite(min(x, (min(y, min(x, z)) + c0)), (min(y, min(x, z)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, x)) + c0)), (min(y, min(z, x)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, min(w, min(u, x)))) + c0)), (min(y, min(z, min(w, min(u, x)))) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, min(w, min(x, u)))) + c0)), (min(y, min(z, min(w, min(x, u)))) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, min(min(w, x), u))) + c0)), (min(y, min(z, min(min(w, x), u))) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, min(min(x, w), u))) + c0)), (min(y, min(z, min(min(x, w), u))) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(min(z, min(w, x)), u)) + c0)), (min(y, min(min(z, min(w, x)), u)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(min(z, min(x, w)), u)) + c0)), (min(y, min(min(z, min(x, w)), u)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(min(min(x, z), w), u)) + c0)), (min(y, min(min(min(x, z), w), u)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(min(min(z, x), w), u)) + c0)), (min(y, min(min(min(z, x), w), u)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(x, y), z) + c0)), (min(min(x, y), z) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, x), z) + c0)), (min(min(y, x), z) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, min(z, min(w, x))), u) + c0)), (min(min(y, min(z, min(w, x))), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, min(z, min(x, w))), u) + c0)), (min(min(y, min(z, min(x, w))), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, min(min(x, z), w)), u) + c0)), (min(min(y, min(min(x, z), w)), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, min(min(z, x), w)), u) + c0)), (min(min(y, min(min(z, x), w)), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(y, min(x, z)), w), u) + c0)), (min(min(min(y, min(x, z)), w), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(y, min(z, x)), w), u) + c0)), (min(min(min(y, min(z, x)), w), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(min(x, y), z), w), u) + c0)), (min(min(min(min(x, y), z), w), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(min(y, x), z), w), u) + c0)), (min(min(min(min(y, x), z), w), u) + fold(min(c0, 1))), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(x, (min(y, (z*w)) - (u + (w*z)))), min(x, (min((y - (z*w)), 0) - u))) ||
 rewrite(min(x, (min(y, (z*w)) - (u + (z*w)))), min(x, (min((y - (z*w)), 0) - u))) ||
 rewrite(min(x, (min(y, (z*w)) - ((w*z) + u))), min(x, (min((y - (z*w)), 0) - u))) ||
 rewrite(min(x, (min(y, (z*w)) - ((z*w) + u))), min(x, (min((y - (z*w)), 0) - u))) ||
 rewrite(min(x, (min((y*z), w) - (u + (y*z)))), min(x, (min((w - (z*y)), 0) - u))) ||
 rewrite(min(x, (min((y*z), w) - (u + (z*y)))), min(x, (min((w - (y*z)), 0) - u))) ||
 rewrite(min(x, (min((y*z), w) - ((y*z) + u))), min(x, (min((w - (y*z)), 0) - u))) ||
 rewrite(min(x, (min((y*z), w) - ((z*y) + u))), min(x, (min((w - (z*y)), 0) - u))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(x, min(x, y)), min(x, y)) ||
 rewrite(min(x, min(y, x)), min(y, x)) ||
 rewrite(min(x, min(y, (x + c0))), min(y, x), (0 <= c0)) ||
 rewrite(min(x, min(y, (z + (w + min(x, y))))), (min((w + z), 0) + min(x, y))) ||
 rewrite(min(x, min(y, (z + (w + min(y, x))))), (min((w + z), 0) + min(y, x))) ||
 rewrite(min(x, min(y, (z + (min(x, y) + w)))), (min(y, x) + min((w + z), 0))) ||
 rewrite(min(x, min(y, (z + (min(y, x) + w)))), (min(y, x) + min((w + z), 0))) ||
 rewrite(min(x, min(y, (z + min(x, y)))), (min(z, 0) + min(x, y))) ||
 rewrite(min(x, min(y, (z + min(y, x)))), (min(z, 0) + min(y, x))) ||
 rewrite(min(x, min(y, ((z + min(x, y)) + w))), (min(x, y) + min((z + w), 0))) ||
 rewrite(min(x, min(y, ((z + min(y, x)) + w))), (min(x, y) + min((z + w), 0))) ||
 rewrite(min(x, min(y, ((min(x, y) + z) + w))), (min(x, y) + min((z + w), 0))) ||
 rewrite(min(x, min(y, ((min(y, x) + z) + w))), (min(x, y) + min((z + w), 0))) ||
 rewrite(min(x, min(y, (min(x, y) + z))), (min(z, 0) + min(x, y))) ||
 rewrite(min(x, min(y, (min(y, x) + z))), (min(z, 0) + min(y, x))) ||
 rewrite(min(x, min(y, min(x, z))), min(y, min(x, z))) ||
 rewrite(min(x, min(y, min(z, x))), min(y, min(z, x))) ||
 rewrite(min(x, min(y, min(z, (w + (u + min(x, y)))))), min(z, (min((u + w), 0) + min(x, y)))) ||
 rewrite(min(x, min(y, min(z, (w + (u + min(y, x)))))), min(z, (min((u + w), 0) + min(y, x)))) ||
 rewrite(min(x, min(y, min(z, (w + (min(x, y) + u))))), min(z, (min(x, y) + min((u + w), 0)))) ||
 rewrite(min(x, min(y, min(z, (w + (min(y, x) + u))))), min(z, (min(x, y) + min((u + w), 0)))) ||
 rewrite(min(x, min(y, min(z, (w + min(x, z))))), min(y, (min(w, 0) + min(x, z)))) ||
 rewrite(min(x, min(y, min(z, (w + min(x, min(y, z)))))), (min(w, 0) + min(x, min(y, z)))) ||
 rewrite(min(x, min(y, min(z, (w + min(x, min(z, y)))))), (min(w, 0) + min(x, min(z, y)))) ||
 rewrite(min(x, min(y, min(z, (w + min(z, x))))), min(y, (min(w, 0) + min(z, x)))) ||
 rewrite(min(x, min(y, min(z, (w + min(min(y, z), x))))), (min(w, 0) + min(min(y, z), x))) ||
 rewrite(min(x, min(y, min(z, (w + min(min(z, y), x))))), (min(w, 0) + min(min(z, y), x))) ||
 rewrite(min(x, min(y, min(z, ((w + min(x, y)) + u)))), min(z, (min(x, y) + min((w + u), 0)))) ||
 rewrite(min(x, min(y, min(z, ((w + min(y, x)) + u)))), min(z, (min(y, x) + min((w + u), 0)))) ||
 rewrite(min(x, min(y, min(z, ((min(x, y) + w) + u)))), min(z, (min(x, y) + min((w + u), 0)))) ||
 rewrite(min(x, min(y, min(z, ((min(y, x) + w) + u)))), min(z, (min(y, x) + min((w + u), 0)))) ||
 rewrite(min(x, min(y, min(z, (min(x, z) + w)))), min(y, (min(x, z) + min(w, 0)))) ||
 rewrite(min(x, min(y, min(z, (min(x, min(y, z)) + w)))), (min(w, 0) + min(x, min(y, z)))) ||
 rewrite(min(x, min(y, min(z, (min(x, min(z, y)) + w)))), (min(w, 0) + min(x, min(z, y)))) ||
 rewrite(min(x, min(y, min(z, (min(z, x) + w)))), min(y, (min(z, x) + min(w, 0)))) ||
 rewrite(min(x, min(y, min(z, (min(min(y, z), x) + w)))), (min(w, 0) + min(min(y, z), x))) ||
 rewrite(min(x, min(y, min(z, (min(min(z, y), x) + w)))), (min(w, 0) + min(min(z, y), x))) ||
 rewrite(min(x, min(y, min(z, (min(x, y) - w)))), min(z, (min(x, y) - max(w, 0)))) ||
 rewrite(min(x, min(y, min(z, (min(y, x) - w)))), min(z, (min(y, x) - max(w, 0)))) ||
 rewrite(min(x, min(y, min(z, min(w, (min(x, y) - u))))), min(z, min(w, (min(x, y) - max(u, 0))))) ||
 rewrite(min(x, min(y, min(z, min(w, (min(y, x) - u))))), min(z, min(w, (min(y, x) - max(u, 0))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(u, min(v, x)))))), min(y, min(z, min(w, min(u, min(v, x)))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(u, min(x, v)))))), min(y, min(z, min(w, min(u, min(x, v)))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(min(u, x), v))))), min(y, min(z, min(w, min(min(u, x), v))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(min(x, u), v))))), min(y, min(z, min(w, min(min(x, u), v))))) ||
 rewrite(min(x, min(y, min(z, min((min(x, y) - w), u)))), min(u, min(z, (min(x, y) - max(w, 0))))) ||
 rewrite(min(x, min(y, min(z, min((min(y, x) - w), u)))), min(u, min(z, (min(y, x) - max(w, 0))))) ||
 rewrite(min(x, min(y, min(z, min(min(w, min(u, x)), v)))), min(y, min(z, min(min(w, min(u, x)), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(w, min(x, u)), v)))), min(y, min(z, min(min(w, min(x, u)), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(min(w, x), u), v)))), min(y, min(z, min(min(min(w, x), u), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(min(x, w), u), v)))), min(y, min(z, min(min(min(x, w), u), v)))) ||
 rewrite(min(x, min(y, min((z + (w + min(x, y))), u))), min((min((w + z), 0) + min(x, y)), u)) ||
 rewrite(min(x, min(y, min((z + (w + min(y, x))), u))), min((min((w + z), 0) + min(y, x)), u)) ||
 rewrite(min(x, min(y, min((z + (min(x, y) + w)), u))), min((min((w + z), 0) + min(x, y)), u)) ||
 rewrite(min(x, min(y, min((z + (min(y, x) + w)), u))), min((min((w + z), 0) + min(y, x)), u)) ||
 rewrite(min(x, min(y, min((z + min(w, x)), w))), min(y, (min(z, 0) + min(x, w)))) ||
 rewrite(min(x, min(y, min((z + min(x, w)), w))), min(y, (min(z, 0) + min(x, w)))) ||
 rewrite(min(x, min(y, min((z + min(x, min(w, y))), w))), (min(z, 0) + min(x, min(w, y)))) ||
 rewrite(min(x, min(y, min((z + min(x, min(y, w))), w))), (min(z, 0) + min(x, min(y, w)))) ||
 rewrite(min(x, min(y, min((z + min(min(w, y), x)), w))), (min(z, 0) + min(y, min(x, w)))) ||
 rewrite(min(x, min(y, min((z + min(min(y, w), x)), w))), (min(z, 0) + min(y, min(x, w)))) ||
 rewrite(min(x, min(y, min(((z + min(x, y)) + w), u))), min(u, (min((z + w), 0) + min(x, y)))) ||
 rewrite(min(x, min(y, min(((z + min(y, x)) + w), u))), min(u, (min((z + w), 0) + min(x, y)))) ||
 rewrite(min(x, min(y, min(((min(x, y) + z) + w), u))), min(u, (min(x, y) + min((z + w), 0)))) ||
 rewrite(min(x, min(y, min(((min(y, x) + z) + w), u))), min(u, (min(y, x) + min((z + w), 0)))) ||
 rewrite(min(x, min(y, min((min(x, z) + w), z))), min(y, (min(x, z) + min(w, 0)))) ||
 rewrite(min(x, min(y, min((min(x, min(y, z)) + w), z))), (min(w, 0) + min(x, min(y, z)))) ||
 rewrite(min(x, min(y, min((min(x, min(z, y)) + w), z))), (min(w, 0) + min(x, min(z, y)))) ||
 rewrite(min(x, min(y, min((min(z, x) + w), z))), min(y, (min(z, x) + min(w, 0)))) ||
 rewrite(min(x, min(y, min((min(min(y, z), x) + w), z))), (min(w, 0) + min(min(y, x), z))) ||
 rewrite(min(x, min(y, min((min(min(z, y), x) + w), z))), (min(w, 0) + min(min(y, x), z))) ||
 rewrite(min(x, min(y, min((min(x, y) - z), w))), min(w, (min(x, y) - max(z, 0)))) ||
 rewrite(min(x, min(y, min((min(y, x) - z), w))), min(w, (min(y, x) - max(z, 0)))) ||
 rewrite(min(x, min(y, min(min(z, (min(x, y) - w)), u))), min(z, min((min(x, y) - max(w, 0)), u))) ||
 rewrite(min(x, min(y, min(min(z, (min(y, x) - w)), u))), min(z, min((min(y, x) - max(w, 0)), u))) ||
 rewrite(min(x, min(y, min(min(z, min(w, min(u, x))), v))), min(y, min(min(z, min(w, min(u, x))), v))) ||
 rewrite(min(x, min(y, min(min(z, min(w, min(x, u))), v))), min(y, min(min(z, min(w, min(x, u))), v))) ||
 rewrite(min(x, min(y, min(min(z, min(min(w, x), u)), v))), min(y, min(min(z, min(min(w, x), u)), v))) ||
 rewrite(min(x, min(y, min(min(z, min(min(x, w), u)), v))), min(y, min(min(z, min(min(x, w), u)), v))) ||
 rewrite(min(x, min(y, min(min((min(x, y) - z), w), u))), min(w, min((min(x, y) - max(z, 0)), u))) ||
 rewrite(min(x, min(y, min(min((min(y, x) - z), w), u))), min(w, min((min(y, x) - max(z, 0)), u))) ||
 rewrite(min(x, min(y, min(min(min(z, min(w, x)), u), v))), min(y, min(min(min(z, min(w, x)), u), v))) ||
 rewrite(min(x, min(y, min(min(min(z, min(x, w)), u), v))), min(y, min(min(min(z, min(x, w)), u), v))) ||
 rewrite(min(x, min(y, min(min(min(min(x, z), w), u), v))), min(y, min(min(min(min(x, z), w), u), v))) ||
 rewrite(min(x, min(y, min(min(min(min(z, x), w), u), v))), min(y, min(min(min(min(z, x), w), u), v))) ||
 rewrite(min(x, min(y, max(z, min(w, x)))), min(x, min(y, max(z, w)))) ||
 rewrite(min(x, min(y, max(z, min(x, w)))), min(x, min(y, max(z, w)))) ||
 rewrite(min(x, min(y, max(min(x, z), w))), min(x, min(y, max(z, w)))) ||
 rewrite(min(x, min(y, max(min(z, x), w))), min(x, min(y, max(z, w)))) ||
 rewrite(min(x, min((x + c0), y)), min(x, y), (0 <= c0)) ||
 rewrite(min(x, min((y + (z + min(w, x))), w)), (min((y + z), 0) + min(x, w))) ||
 rewrite(min(x, min((y + (z + min(x, w))), w)), (min((y + z), 0) + min(x, w))) ||
 rewrite(min(x, min((y + (min(x, z) + w)), z)), (min((y + w), 0) + min(x, z))) ||
 rewrite(min(x, min((y + (min(z, x) + w)), z)), (min((y + w), 0) + min(x, z))) ||
 rewrite(min(x, min((y + min(x, z)), z)), (min(y, 0) + min(x, z))) ||
 rewrite(min(x, min((y + min(z, x)), z)), (min(y, 0) + min(x, z))) ||
 rewrite(min(x, min(((y + min(x, z)) + w), z)), (min((y + w), 0) + min(x, z))) ||
 rewrite(min(x, min(((y + min(z, x)) + w), z)), (min((y + w), 0) + min(x, z))) ||
 rewrite(min(x, min(((min(x, y) + z) + w), y)), (min((z + w), 0) + min(x, y))) ||
 rewrite(min(x, min(((min(y, x) + z) + w), y)), (min((z + w), 0) + min(x, y))) ||
 rewrite(min(x, min((min(x, y) + z), y)), (min(x, y) + min(z, 0))) ||
 rewrite(min(x, min((min(y, x) + z), y)), (min(y, x) + min(z, 0))) ||
 rewrite(min(x, min(min(x, y), z)), min(x, min(y, z))) ||
 rewrite(min(x, min(min(y, x), z)), min(x, min(y, z))) ||
 rewrite(min(x, min(min(y, (z + (w + min(u, x)))), u)), min(y, (min((w + z), 0) + min(u, x)))) ||
 rewrite(min(x, min(min(y, (z + (w + min(x, u)))), u)), min(y, (min((w + z), 0) + min(x, u)))) ||
 rewrite(min(x, min(min(y, (z + (min(w, x) + u))), w)), min(y, (min(x, w) + min((u + z), 0)))) ||
 rewrite(min(x, min(min(y, (z + (min(x, w) + u))), w)), min(y, (min(x, w) + min((u + z), 0)))) ||
 rewrite(min(x, min(min(y, (z + min(x, y))), w)), min((min(z, 0) + min(x, y)), w)) ||
 rewrite(min(x, min(min(y, (z + min(x, min(w, y)))), w)), (min(z, 0) + min(x, min(w, y)))) ||
 rewrite(min(x, min(min(y, (z + min(x, min(y, w)))), w)), (min(z, 0) + min(x, min(y, w)))) ||
 rewrite(min(x, min(min(y, (z + min(y, x))), w)), min((min(z, 0) + min(y, x)), w)) ||
 rewrite(min(x, min(min(y, (z + min(min(w, y), x))), w)), (min(z, 0) + min(min(w, y), x))) ||
 rewrite(min(x, min(min(y, (z + min(min(y, w), x))), w)), (min(z, 0) + min(min(y, w), x))) ||
 rewrite(min(x, min(min(y, ((z + min(w, x)) + u)), w)), min(y, (min(w, x) + min((z + u), 0)))) ||
 rewrite(min(x, min(min(y, ((z + min(x, w)) + u)), w)), min(y, (min(x, w) + min((z + u), 0)))) ||
 rewrite(min(x, min(min(y, ((min(x, z) + w) + u)), z)), min(y, (min(x, z) + min((w + u), 0)))) ||
 rewrite(min(x, min(min(y, ((min(z, x) + w) + u)), z)), min(y, (min(z, x) + min((w + u), 0)))) ||
 rewrite(min(x, min(min(y, (min(x, y) + z)), w)), min((min(z, 0) + min(x, y)), w)) ||
 rewrite(min(x, min(min(y, (min(x, min(y, z)) + w)), z)), (min(w, 0) + min(x, min(y, z)))) ||
 rewrite(min(x, min(min(y, (min(x, min(z, y)) + w)), z)), (min(w, 0) + min(x, min(z, y)))) ||
 rewrite(min(x, min(min(y, (min(y, x) + z)), w)), min((min(z, 0) + min(y, x)), w)) ||
 rewrite(min(x, min(min(y, (min(min(y, z), x) + w)), z)), (min(w, 0) + min(min(y, z), x))) ||
 rewrite(min(x, min(min(y, (min(min(z, y), x) + w)), z)), (min(w, 0) + min(min(z, y), x))) ||
 rewrite(min(x, min(min(y, (min(x, z) - w)), z)), min(y, (min(x, z) - max(w, 0)))) ||
 rewrite(min(x, min(min(y, (min(z, x) - w)), z)), min(y, (min(z, x) - max(w, 0)))) ||
 rewrite(min(x, min(min(y, min(z, (min(w, x) - u))), w)), min(y, min(z, (min(w, x) - max(u, 0))))) ||
 rewrite(min(x, min(min(y, min(z, (min(x, w) - u))), w)), min(y, min(z, (min(x, w) - max(u, 0))))) ||
 rewrite(min(x, min(min(y, min(z, min(w, min(u, x)))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(w, min(x, u)))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(min(w, x), u))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(min(x, w), u))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min((min(x, z) - w), u)), z)), min(u, min(y, (min(x, z) - max(w, 0))))) ||
 rewrite(min(x, min(min(y, min((min(z, x) - w), u)), z)), min(u, min(y, (min(z, x) - max(w, 0))))) ||
 rewrite(min(x, min(min(y, min(min(z, min(w, x)), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(z, min(x, w)), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(min(x, z), w), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(min(z, x), w), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min((y + (z + min(w, x))), u), w)), min((min((z + y), 0) + min(w, x)), u)) ||
 rewrite(min(x, min(min((y + (z + min(x, w))), u), w)), min((min((z + y), 0) + min(x, w)), u)) ||
 rewrite(min(x, min(min((y + (min(x, z) + w)), u), z)), min(u, (min((y + w), 0) + min(x, z)))) ||
 rewrite(min(x, min(min((y + (min(z, x) + w)), u), z)), min(u, (min((y + w), 0) + min(x, z)))) ||
 rewrite(min(x, min(min((y + min(x, z)), z), w)), min((min(y, 0) + min(x, z)), w)) ||
 rewrite(min(x, min(min((y + min(x, min(z, w))), w), z)), (min(y, 0) + min(x, min(z, w)))) ||
 rewrite(min(x, min(min((y + min(x, min(z, w))), z), w)), (min(y, 0) + min(x, min(z, w)))) ||
 rewrite(min(x, min(min((y + min(z, x)), z), w)), min((min(y, 0) + min(z, x)), w)) ||
 rewrite(min(x, min(min((y + min(min(z, w), x)), w), z)), (min(y, 0) + min(z, min(x, w)))) ||
 rewrite(min(x, min(min((y + min(min(z, w), x)), z), w)), (min(y, 0) + min(w, min(x, z)))) ||
 rewrite(min(x, min(min(((y + min(x, z)) + w), u), z)), min(u, (min((y + w), 0) + min(x, z)))) ||
 rewrite(min(x, min(min(((y + min(z, x)) + w), u), z)), min(u, (min((y + w), 0) + min(x, z)))) ||
 rewrite(min(x, min(min(((min(x, y) + z) + w), u), y)), min(u, (min(x, y) + min((z + w), 0)))) ||
 rewrite(min(x, min(min(((min(y, x) + z) + w), u), y)), min(u, (min(y, x) + min((z + w), 0)))) ||
 rewrite(min(x, min(min((min(x, y) + z), y), w)), min((min(x, y) + min(z, 0)), w)) ||
 rewrite(min(x, min(min((min(x, min(y, z)) + w), y), z)), (min(w, 0) + min(x, min(y, z)))) ||
 rewrite(min(x, min(min((min(x, min(y, z)) + w), z), y)), (min(w, 0) + min(x, min(y, z)))) ||
 rewrite(min(x, min(min((min(y, x) + z), y), w)), min((min(y, x) + min(z, 0)), w)) ||
 rewrite(min(x, min(min((min(min(y, z), x) + w), y), z)), (min(w, 0) + min(min(z, x), y))) ||
 rewrite(min(x, min(min((min(min(y, z), x) + w), z), y)), (min(w, 0) + min(min(y, x), z))) ||
 rewrite(min(x, min(min((min(x, y) - z), w), y)), min(w, (min(x, y) - max(z, 0)))) ||
 rewrite(min(x, min(min((min(y, x) - z), w), y)), min(w, (min(y, x) - max(z, 0)))) ||
 rewrite(min(x, min(min(min(y, (min(x, z) - w)), u), z)), min(y, min((min(x, z) - max(w, 0)), u))) ||
 rewrite(min(x, min(min(min(y, (min(z, x) - w)), u), z)), min(y, min((min(z, x) - max(w, 0)), u))) ||
 rewrite(min(x, min(min(min(y, min(z, min(w, x))), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(z, min(x, w))), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(min(x, z), w)), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(min(z, x), w)), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min((min(x, y) - z), w), u), y)), min(w, min((min(x, y) - max(z, 0)), u))) ||
 rewrite(min(x, min(min(min((min(y, x) - z), w), u), y)), min(w, min((min(y, x) - max(z, 0)), u))) ||
 rewrite(min(x, min(min(min(min(y, min(x, z)), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(y, min(z, x)), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(min(x, y), z), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(min(y, x), z), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(max(y, min(x, z)), w)), min(x, min(max(y, z), w))) ||
 rewrite(min(x, min(max(y, min(z, x)), w)), min(x, min(max(y, z), w))) ||
 rewrite(min(x, min(max(min(x, y), z), w)), min(x, min(max(y, z), w))) ||
 rewrite(min(x, min(max(min(y, x), z), w)), min(x, min(max(y, z), w))))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(min((x + (y + ((z*c0) + (w - z)))), u), min((x + (y + (w + z))), u), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min((x + (((y*c0) + (z - y)) + w)), u), min((x + ((z + y) + w)), u), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min((x + ((y*c0) + (z - y))), w), min((x + (z + y)), w), ((((1 <= c0) && (c0 <= 6)) && (-22 <= (c0*-7))) && ((min(c0, 3)*7) == 14))) ||
 rewrite(min(((x + ((y*c0) + (z - y))) + w), u), min(((x + (z + y)) + w), u), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(((((x*c0) + (y - x)) + z) + w), u), min((((y + x) + z) + w), u), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min((((x*c0) + (y - x)) + z), w), min(((y + x) + z), w), ((((1 <= c0) && (c0 <= 6)) && (-22 <= (c0*-7))) && ((min(c0, 3)*7) == 14))) ||
 rewrite(min((min(x, min(y, z)) + c0), y), (min(x, min(y, z)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, z)) + c0), z), (min(x, min(y, z)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, min(z, min(w, u)))) + c0), u), (min(x, min(y, min(z, min(w, u)))) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, min(z, min(w, u)))) + c0), w), (min(x, min(y, min(z, min(w, u)))) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, min(min(z, w), u))) + c0), w), (min(x, min(y, min(min(z, w), u))) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, min(min(z, w), u))) + c0), z), (min(x, min(y, min(min(z, w), u))) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(min(y, min(z, w)), u)) + c0), w), (min(x, min(min(y, min(z, w)), u)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(min(y, min(z, w)), u)) + c0), z), (min(x, min(min(y, min(z, w)), u)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(min(min(y, z), w), u)) + c0), y), (min(x, min(min(min(y, z), w), u)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(x, min(min(min(y, z), w), u)) + c0), z), (min(x, min(min(min(y, z), w), u)) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(x, y), z) + c0), x), (min(min(x, y), z) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(x, y), z) + c0), y), (min(min(x, y), z) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(x, min(y, min(z, w))), u) + c0), w), (min(min(x, min(y, min(z, w))), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(x, min(y, min(z, w))), u) + c0), z), (min(min(x, min(y, min(z, w))), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(x, min(min(y, z), w)), u) + c0), y), (min(min(x, min(min(y, z), w)), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(x, min(min(y, z), w)), u) + c0), z), (min(min(x, min(min(y, z), w)), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(min(x, min(y, z)), w), u) + c0), y), (min(min(min(x, min(y, z)), w), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(min(x, min(y, z)), w), u) + c0), z), (min(min(min(x, min(y, z)), w), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(min(min(x, y), z), w), u) + c0), x), (min(min(min(min(x, y), z), w), u) + fold(min(c0, 1))), (c0 <= 0)) ||
 rewrite(min((min(min(min(min(x, y), z), w), u) + c0), y), (min(min(min(min(x, y), z), w), u) + fold(min(c0, 1))), (c0 <= 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((x + y), (z + (w + x))), (x + min(y, (z + w)))) ||
 rewrite(min((x + y), (z + (w + y))), (y + min(x, (z + w)))) ||
 rewrite(min((x + y), (z + (w + (u + x)))), (x + min(y, (z + (w + u))))) ||
 rewrite(min((x + y), (z + (w + (u + y)))), (y + min(x, (z + (w + u))))) ||
 rewrite(min((x + y), (z + (w + (u + (v + x))))), (x + min(y, (z + (w + (u + v)))))) ||
 rewrite(min((x + y), (z + (w + (u + (v + y))))), (y + min(x, (z + (w + (u + v)))))) ||
 rewrite(min((x + y), (z + (w + (u + (x + v))))), (x + min(y, (z + (w + (u + v)))))) ||
 rewrite(min((x + y), (z + (w + (u + (y + v))))), (y + min(x, (z + (w + (u + v)))))) ||
 rewrite(min((x + y), (z + (w + (x + u)))), (x + min(y, (z + (w + u))))) ||
 rewrite(min((x + y), (z + (w + (y + u)))), (y + min(x, (z + (w + u))))) ||
 rewrite(min((x + y), (z + (w + ((u + x) + v)))), (x + min(y, (w + ((u + z) + v))))) ||
 rewrite(min((x + y), (z + (w + ((u + y) + v)))), (y + min(x, (w + ((u + z) + v))))) ||
 rewrite(min((x + y), (z + (w + ((x + u) + v)))), (x + min(y, (w + ((z + u) + v))))) ||
 rewrite(min((x + y), (z + (w + ((y + u) + v)))), (y + min(x, (w + ((z + u) + v))))) ||
 rewrite(min((x + y), (z + (x + w))), (x + min(y, (z + w)))) ||
 rewrite(min((x + y), (z + (y + w))), (y + min(x, (z + w)))) ||
 rewrite(min((x + y), (z + ((w + x) + u))), (x + min(y, ((w + z) + u)))) ||
 rewrite(min((x + y), (z + ((w + y) + u))), (y + min(x, ((w + z) + u)))) ||
 rewrite(min((x + y), (z + ((w + (u + x)) + v))), (x + min(y, ((w + (u + z)) + v)))) ||
 rewrite(min((x + y), (z + ((w + (u + y)) + v))), (y + min(x, ((w + (u + z)) + v)))) ||
 rewrite(min((x + y), (z + ((w + (x + u)) + v))), (x + min(y, ((w + (z + u)) + v)))) ||
 rewrite(min((x + y), (z + ((w + (y + u)) + v))), (y + min(x, ((w + (z + u)) + v)))) ||
 rewrite(min((x + y), (z + ((x + w) + u))), (x + min(y, ((z + w) + u)))) ||
 rewrite(min((x + y), (z + ((y + w) + u))), (y + min(x, ((z + w) + u)))) ||
 rewrite(min((x + y), (z + (((w + x) + u) + v))), (x + min(y, (((w + z) + u) + v)))) ||
 rewrite(min((x + y), (z + (((w + y) + u) + v))), (y + min(x, (((w + z) + u) + v)))) ||
 rewrite(min((x + y), (z + (((x + w) + u) + v))), (x + min(y, (((z + w) + u) + v)))) ||
 rewrite(min((x + y), (z + (((y + w) + u) + v))), (y + min(x, (((z + w) + u) + v)))) ||
 rewrite(min((x + y), (z + (x - w))), (x + min(y, (z - w)))) ||
 rewrite(min((x + y), (z + (y - w))), (y + min(x, (z - w)))) ||
 rewrite(min((x + y), ((x + z) + w)), (x + min((w + z), y))) ||
 rewrite(min((x + y), ((y + z) + w)), (y + min((w + z), x))) ||
 rewrite(min((x + y), ((z + x) + w)), (x + min((z + w), y))) ||
 rewrite(min((x + y), ((z + y) + w)), (y + min((z + w), x))) ||
 rewrite(min((x + y), ((z + (w + x)) + u)), (x + min((z + (w + u)), y))) ||
 rewrite(min((x + y), ((z + (w + y)) + u)), (y + min((z + (w + u)), x))) ||
 rewrite(min((x + y), ((z + (w + (u + x))) + v)), (x + min((z + (w + (u + v))), y))) ||
 rewrite(min((x + y), ((z + (w + (u + y))) + v)), (y + min((z + (w + (u + v))), x))) ||
 rewrite(min((x + y), ((z + (w + (x + u))) + v)), (x + min((z + (w + (v + u))), y))) ||
 rewrite(min((x + y), ((z + (w + (y + u))) + v)), (y + min((z + (w + (v + u))), x))) ||
 rewrite(min((x + y), ((z + (x + w)) + u)), (x + min((z + (u + w)), y))) ||
 rewrite(min((x + y), ((z + (y + w)) + u)), (y + min((z + (u + w)), x))) ||
 rewrite(min((x + y), ((z + ((w + x) + u)) + v)), (x + min((z + ((w + v) + u)), y))) ||
 rewrite(min((x + y), ((z + ((w + y) + u)) + v)), (y + min((z + ((w + v) + u)), x))) ||
 rewrite(min((x + y), ((z + ((x + w) + u)) + v)), (x + min((z + ((v + w) + u)), y))) ||
 rewrite(min((x + y), ((z + ((y + w) + u)) + v)), (y + min((z + ((v + w) + u)), x))) ||
 rewrite(min((x + y), (((x + z) + w) + u)), (x + min(((u + z) + w), y))) ||
 rewrite(min((x + y), (((y + z) + w) + u)), (y + min(((u + z) + w), x))) ||
 rewrite(min((x + y), (((z + x) + w) + u)), (x + min(((z + u) + w), y))) ||
 rewrite(min((x + y), (((z + y) + w) + u)), (y + min(((z + u) + w), x))) ||
 rewrite(min((x + y), (((z + (w + x)) + u) + v)), (x + min(((z + (w + v)) + u), y))) ||
 rewrite(min((x + y), (((z + (w + y)) + u) + v)), (y + min(((z + (w + v)) + u), x))) ||
 rewrite(min((x + y), (((z + (x + w)) + u) + v)), (x + min(((z + (v + w)) + u), y))) ||
 rewrite(min((x + y), (((z + (y + w)) + u) + v)), (y + min(((z + (v + w)) + u), x))) ||
 rewrite(min((x + y), ((((x + z) + w) + u) + v)), (x + min((((v + z) + w) + u), y))) ||
 rewrite(min((x + y), ((((y + z) + w) + u) + v)), (y + min((((v + z) + w) + u), x))) ||
 rewrite(min((x + y), ((((z + x) + w) + u) + v)), (x + min((((z + v) + w) + u), y))) ||
 rewrite(min((x + y), ((((z + y) + w) + u) + v)), (y + min((((z + v) + w) + u), x))) ||
 rewrite(min((x + y), ((x - z) + w)), (x + min((w - z), y))) ||
 rewrite(min((x + y), ((y - z) + w)), (y + min((w - z), x))) ||
 rewrite(min((x + y), ((z/c0) + ((z + c1)/c2))), min((x + y), z), (((((0 <= (c1/c2)) && ((c1/c2) == 0)) && (0 <= ((c1 + 1)/c2))) && (((-1182/c0) + ((c1 + -1182)/c2)) == -1182)) && (((3/c0) + ((c1 + 3)/c2)) == 3))) ||
 rewrite(min((x + (y + z)), (w + y)), (min(w, (x + z)) + y)) ||
 rewrite(min((x + (y + z)), (w + z)), (min(w, (y + x)) + z)) ||
 rewrite(min((x + (y + z)), (w + (u + y))), (y + min((z + x), (w + u)))) ||
 rewrite(min((x + (y + z)), (w + (u + z))), (z + min((y + x), (w + u)))) ||
 rewrite(min((x + (y + z)), (w + (u + (v + y)))), (y + min((z + x), (w + (u + v))))) ||
 rewrite(min((x + (y + z)), (w + (u + (v + z)))), (z + min((y + x), (w + (u + v))))) ||
 rewrite(min((x + (y + z)), (w + (u + (y + v)))), (y + min((z + x), (w + (u + v))))) ||
 rewrite(min((x + (y + z)), (w + (u + (z + v)))), (z + min((y + x), (w + (u + v))))) ||
 rewrite(min((x + (y + z)), (w + (y + u))), (y + min((z + x), (w + u)))) ||
 rewrite(min((x + (y + z)), (w + (z + u))), (z + min((y + x), (w + u)))) ||
 rewrite(min((x + (y + z)), (w + ((u + y) + v))), (y + min((z + x), (w + (u + v))))) ||
 rewrite(min((x + (y + z)), (w + ((u + z) + v))), (z + min((y + x), (w + (u + v))))) ||
 rewrite(min((x + (y + z)), (w + ((y + u) + v))), (y + min((z + x), (w + (v + u))))) ||
 rewrite(min((x + (y + z)), (w + ((z + u) + v))), (z + min((y + x), (w + (v + u))))) ||
 rewrite(min((x + (y + z)), (w + ((z + y) - u))), (y + min((z + x), (w + (z - u))))) ||
 rewrite(min((x + (y + z)), (y + w)), (min(w, (x + z)) + y)) ||
 rewrite(min((x + (y + z)), (z + w)), (min(w, (y + x)) + z)) ||
 rewrite(min((x + (y + z)), ((w + y) + u)), (y + min((z + x), (w + u)))) ||
 rewrite(min((x + (y + z)), ((w + z) + u)), (z + min((y + x), (w + u)))) ||
 rewrite(min((x + (y + z)), ((w + (u + y)) + v)), (y + min((z + x), (w + (u + v))))) ||
 rewrite(min((x + (y + z)), ((w + (u + z)) + v)), (z + min((y + x), (w + (u + v))))) ||
 rewrite(min((x + (y + z)), ((w + (y + u)) + v)), (y + min((z + x), (w + (v + u))))) ||
 rewrite(min((x + (y + z)), ((w + (z + u)) + v)), (z + min((y + x), (w + (v + u))))) ||
 rewrite(min((x + (y + z)), ((y + w) + u)), (y + min((z + x), (u + w)))) ||
 rewrite(min((x + (y + z)), ((z + w) + u)), (z + min((y + x), (u + w)))) ||
 rewrite(min((x + (y + z)), (((w + y) + u) + v)), (y + min((z + x), ((v + w) + u)))) ||
 rewrite(min((x + (y + z)), (((w + z) + u) + v)), (z + min((y + x), ((v + w) + u)))) ||
 rewrite(min((x + (y + z)), (((y + w) + u) + v)), (y + min((z + x), ((v + w) + u)))) ||
 rewrite(min((x + (y + z)), (((z + w) + u) + v)), (z + min((y + x), ((v + w) + u)))) ||
 rewrite(min((x + (y + z)), (((z + y) - w) + u)), (y + min((z + x), (u + (z - w))))) ||
 rewrite(min((x + (y + (z + w))), (u + w)), (min(u, (y + (z + x))) + w)) ||
 rewrite(min((x + (y + (z + w))), (u + z)), (min(u, (y + (x + w))) + z)) ||
 rewrite(min((x + (y + (z + w))), (u + (w + z))), (min(u, (y + x)) + (w + z))) ||
 rewrite(min((x + (y + (z + w))), (w + u)), (min(u, (y + (z + x))) + w)) ||
 rewrite(min((x + (y + (z + w))), (z + u)), (min(u, (y + (x + w))) + z)) ||
 rewrite(min((x + (y + (z + w))), ((w + z) + u)), (min(u, (y + x)) + (w + z))) ||
 rewrite(min((x + (y + (z + (w + u)))), (u + v)), (min(v, (y + (z + (w + x)))) + u)) ||
 rewrite(min((x + (y + (z + (w + u)))), (v + u)), (min(v, (y + (z + (w + x)))) + u)) ||
 rewrite(min((x + (y + (z + (w + u)))), (v + w)), (min(v, (y + (z + (x + u)))) + w)) ||
 rewrite(min((x + (y + (z + (w + u)))), (w + v)), (min(v, (y + (z + (x + u)))) + w)) ||
 rewrite(min((x + (y + ((z + w) + u))), (v + w)), (min(v, (y + ((z + x) + u))) + w)) ||
 rewrite(min((x + (y + ((z + w) + u))), (v + z)), (min(v, (y + ((x + w) + u))) + z)) ||
 rewrite(min((x + (y + ((z + w) + u))), (w + v)), (min(v, (y + ((z + x) + u))) + w)) ||
 rewrite(min((x + (y + ((z + w) + u))), (z + v)), (min(v, (y + ((x + w) + u))) + z)) ||
 rewrite(min((x + (y + (z - w))), (u + (v + z))), (z + min((y + (x - w)), (u + v)))) ||
 rewrite(min((x + (y + (z - w))), (u + (z + v))), (z + min((y + (x - w)), (u + v)))) ||
 rewrite(min((x + (y + (z - w))), (u + (z - v))), (z + min((y - (w - x)), (u - v)))) ||
 rewrite(min((x + (y + (z - w))), ((u + z) + v)), (z + min((y + (x - w)), (u + v)))) ||
 rewrite(min((x + (y + (z - w))), ((z + u) + v)), (z + min((y + (x - w)), (v + u)))) ||
 rewrite(min((x + (y + (z - w))), ((z - u) + v)), (z + min((y - (w - x)), (v - u)))) ||
 rewrite(min((x + (y + (z*c0))), (w + (z*c1))), (min(w, (y + x)) + (z*fold(min(c0, c1)))), ((c0 <= c1) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min((x + (y + (z*c0))), ((z*c1) + w)), (min(w, (y + x)) + (z*fold(min(c0, c1)))), ((c0 <= c1) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min((x + (y + (z*w))), (u + (w*z))), (min(u, (y + x)) + (w*z))) ||
 rewrite(min((x + (y + (z*w))), ((w*z) + u)), (min(u, (y + x)) + (w*z))) ||
 rewrite(min((x + ((y + z) + w)), (u + y)), (min(u, ((x + z) + w)) + y)) ||
 rewrite(min((x + ((y + z) + w)), (u + z)), (min(u, ((y + x) + w)) + z)) ||
 rewrite(min((x + ((y + z) + w)), (u + (z + y))), (min(u, (x + w)) + (z + y))) ||
 rewrite(min((x + ((y + z) + w)), (y + u)), (min(u, ((x + z) + w)) + y)) ||
 rewrite(min((x + ((y + z) + w)), (z + u)), (min(u, ((y + x) + w)) + z)) ||
 rewrite(min((x + ((y + z) + w)), ((z + y) + u)), (min(u, (x + w)) + (z + y))) ||
 rewrite(min((x + ((y + (z + w)) + u)), (v + w)), (min(v, ((y + (z + x)) + u)) + w)) ||
 rewrite(min((x + ((y + (z + w)) + u)), (v + z)), (min(v, ((y + (x + w)) + u)) + z)) ||
 rewrite(min((x + ((y + (z + w)) + u)), (w + v)), (min(v, ((y + (z + x)) + u)) + w)) ||
 rewrite(min((x + ((y + (z + w)) + u)), (z + v)), (min(v, ((y + (x + w)) + u)) + z)) ||
 rewrite(min((x + (((y + z) + w) + u)), (v + y)), (min(v, (((x + z) + w) + u)) + y)) ||
 rewrite(min((x + (((y + z) + w) + u)), (v + z)), (min(v, (((y + x) + w) + u)) + z)) ||
 rewrite(min((x + (((y + z) + w) + u)), (y + v)), (min(v, (((x + z) + w) + u)) + y)) ||
 rewrite(min((x + (((y + z) + w) + u)), (z + v)), (min(v, (((y + x) + w) + u)) + z)) ||
 rewrite(min((x + ((y - z) + w)), (u + (v + y))), (y + min(((x - z) + w), (u + v)))) ||
 rewrite(min((x + ((y - z) + w)), (u + (y + v))), (y + min(((x - z) + w), (u + v)))) ||
 rewrite(min((x + ((y - z) + w)), (u + (y - v))), (y + min((x - (z - w)), (u - v)))) ||
 rewrite(min((x + ((y - z) + w)), ((u + y) + v)), (y + min(((x - z) + w), (u + v)))) ||
 rewrite(min((x + ((y - z) + w)), ((y + u) + v)), (y + min(((x - z) + w), (v + u)))) ||
 rewrite(min((x + ((y - z) + w)), ((y - u) + v)), (y + min((x - (z - w)), (v - u)))) ||
 rewrite(min((x + ((y*c0) + z)), (w + (y*c1))), (min(w, (x + z)) + (y*fold(min(c0, c1)))), ((c0 <= c1) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min((x + ((y*c0) + z)), ((y*c1) + w)), (min(w, (z + x)) + (y*fold(min(c0, c1)))), ((c0 <= c1) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min((x + ((y*z) + w)), (u + (z*y))), (min(u, (x + w)) + (z*y))) ||
 rewrite(min((x + ((y*z) + w)), ((z*y) + u)), (min(u, (x + w)) + (z*y))) ||
 rewrite(min((x + (y - z)), (w + (u + y))), (y + min((x - z), (w + u)))) ||
 rewrite(min((x + (y - z)), (w + (u + (v + y)))), (y + min((x - z), (w + (u + v))))) ||
 rewrite(min((x + (y - z)), (w + (u + (y + v)))), (y + min((x - z), (w + (u + v))))) ||
 rewrite(min((x + (y - z)), (w + (u + (y - v)))), (y + min((x - z), (w + (u - v))))) ||
 rewrite(min((x + (y - z)), (w + (y + u))), (y + min((x - z), (w + u)))) ||
 rewrite(min((x + (y - z)), (w + ((u + y) + v))), (y + min((x - z), (w + (v + u))))) ||
 rewrite(min((x + (y - z)), (w + ((y + u) + v))), (y + min((x - z), (w + (v + u))))) ||
 rewrite(min((x + (y - z)), (w + ((y - u) + v))), (y + min((x - z), (w + (v - u))))) ||
 rewrite(min((x + (y - z)), (w + (u - z))), (min((y + x), (w + u)) - z)) ||
 rewrite(min((x + (y - z)), (w + (y - u))), (y + min((x - z), (w - u)))) ||
 rewrite(min((x + (y - z)), ((w + y) + u)), (y + min((x - z), (u + w)))) ||
 rewrite(min((x + (y - z)), ((w + (u + y)) + v)), (y + min((x - z), (w + (u + v))))) ||
 rewrite(min((x + (y - z)), ((w + (y + u)) + v)), (y + min((x - z), (w + (v + u))))) ||
 rewrite(min((x + (y - z)), ((w + (y - u)) + v)), (y + min((x - z), (w + (v - u))))) ||
 rewrite(min((x + (y - z)), ((y + w) + u)), (y + min((x - z), (u + w)))) ||
 rewrite(min((x + (y - z)), (((w + y) + u) + v)), (y + min((x - z), ((v + w) + u)))) ||
 rewrite(min((x + (y - z)), (((y + w) + u) + v)), (y + min((x - z), ((v + w) + u)))) ||
 rewrite(min((x + (y - z)), (((y - w) + u) + v)), (y + min((x - z), ((v - w) + u)))) ||
 rewrite(min((x + (y - z)), ((w - z) + u)), (min((y + x), (w + u)) - z)) ||
 rewrite(min((x + (y - z)), ((y - w) + u)), (y + min((x - z), (u - w)))) ||
 rewrite(min((x + ((y + z) - w)), (u + (y - v))), (y + min(((x + z) - w), (u - v)))) ||
 rewrite(min((x + ((y + z) - w)), (u + (z - v))), (z + min(((y + x) - w), (u - v)))) ||
 rewrite(min((x + ((y + z) - w)), (w + (u + y))), (y + min(((x + z) - w), (w + u)))) ||
 rewrite(min((x + ((y + z) - w)), (w + (u + z))), (z + min((y + (x - w)), (w + u)))) ||
 rewrite(min((x + ((y + z) - w)), (w + (y + u))), (y + min(((x + z) - w), (w + u)))) ||
 rewrite(min((x + ((y + z) - w)), (w + (z + u))), (z + min((y + (x - w)), (w + u)))) ||
 rewrite(min((x + ((y + z) - w)), ((u + y) + w)), (y + min(((x + z) - w), (w + u)))) ||
 rewrite(min((x + ((y + z) - w)), ((u + z) + w)), (z + min((y + (x - w)), (w + u)))) ||
 rewrite(min((x + ((y + z) - w)), ((y + u) + w)), (y + min(((x + z) - w), (w + u)))) ||
 rewrite(min((x + ((y + z) - w)), ((z + u) + w)), (z + min((y + (x - w)), (w + u)))) ||
 rewrite(min((x + ((y + z) - w)), ((y - u) + v)), (y + min(((x + z) - w), (v - u)))) ||
 rewrite(min((x + ((y + z) - w)), ((z - u) + v)), (z + min(((y + x) - w), (v - u)))) ||
 rewrite(min((x + (y*c0)), (z + (w + (y*c1)))), (min(x, (z + w)) + (y*fold(min(c0, c1)))), (max(c0, c1) == min(c0, c1))) ||
 rewrite(min((x + (y*c0)), (z + ((y*c1) + w))), (min(x, (z + w)) + (y*fold(min(c0, c1)))), (max(c0, c1) == min(c0, c1))) ||
 rewrite(min((x + (y*c0)), ((z + (y*c1)) + w)), (min(x, (w + z)) + (y*fold(min(c0, c1)))), (max(c0, c1) == min(c0, c1))) ||
 rewrite(min((x + (y*c0)), (((y*c1) + z) + w)), (min(x, (w + z)) + (y*fold(min(c0, c1)))), (max(c0, c1) == min(c0, c1))) ||
 rewrite(min((x + (y*z)), (w + (u + (z*y)))), (min(x, (w + u)) + (z*y))) ||
 rewrite(min((x + (y*z)), (w + ((z*y) + u))), (min(x, (w + u)) + (z*y))) ||
 rewrite(min((x + (y*z)), ((w + (z*y)) + u)), (min(x, (u + w)) + (z*y))) ||
 rewrite(min((x + (y*z)), (((z*y) + w) + u)), (min(x, (u + w)) + (z*y))) ||
 rewrite(min(((x + y) + z), (w + x)), (x + min((y + z), w))) ||
 rewrite(min(((x + y) + z), (w + y)), (y + min((x + z), w))) ||
 rewrite(min(((x + y) + z), (w + (u + x))), (x + min((y + z), (w + u)))) ||
 rewrite(min(((x + y) + z), (w + (u + y))), (y + min((x + z), (w + u)))) ||
 rewrite(min(((x + y) + z), (w + (u + (v + x)))), (x + min((y + z), (w + (u + v))))) ||
 rewrite(min(((x + y) + z), (w + (u + (v + y)))), (y + min((x + z), (w + (u + v))))) ||
 rewrite(min(((x + y) + z), (w + (u + (x + v)))), (x + min((y + z), (w + (u + v))))) ||
 rewrite(min(((x + y) + z), (w + (u + (y + v)))), (y + min((x + z), (w + (u + v))))) ||
 rewrite(min(((x + y) + z), (w + (x + u))), (x + min((y + z), (w + u)))) ||
 rewrite(min(((x + y) + z), (w + (y + u))), (y + min((x + z), (w + u)))) ||
 rewrite(min(((x + y) + z), (w + ((u + x) + v))), (x + min((y + z), (w + (u + v))))) ||
 rewrite(min(((x + y) + z), (w + ((u + y) + v))), (y + min((x + z), (w + (u + v))))) ||
 rewrite(min(((x + y) + z), (w + ((x + u) + v))), (x + min((y + z), (w + (v + u))))) ||
 rewrite(min(((x + y) + z), (w + ((y + u) + v))), (y + min((x + z), (w + (v + u))))) ||
 rewrite(min(((x + y) + z), (w + ((y + x) - u))), (y + min((x + z), (w + (x - u))))) ||
 rewrite(min(((x + y) + z), (x + w)), (x + min((y + z), w))) ||
 rewrite(min(((x + y) + z), (y + w)), (y + min((x + z), w))) ||
 rewrite(min(((x + y) + z), ((w + x) + u)), (x + min((y + z), (w + u)))) ||
 rewrite(min(((x + y) + z), ((w + y) + u)), (y + min((x + z), (w + u)))) ||
 rewrite(min(((x + y) + z), ((w + (u + x)) + v)), (x + min((y + z), (w + (u + v))))) ||
 rewrite(min(((x + y) + z), ((w + (u + y)) + v)), (y + min((x + z), (w + (u + v))))) ||
 rewrite(min(((x + y) + z), ((w + (x + u)) + v)), (x + min((y + z), (w + (v + u))))) ||
 rewrite(min(((x + y) + z), ((w + (y + u)) + v)), (y + min((x + z), (w + (v + u))))) ||
 rewrite(min(((x + y) + z), ((x + w) + u)), (x + min((y + z), (u + w)))) ||
 rewrite(min(((x + y) + z), ((y + w) + u)), (y + min((x + z), (u + w)))) ||
 rewrite(min(((x + y) + z), (((w + x) + u) + v)), (x + min((y + z), ((v + w) + u)))) ||
 rewrite(min(((x + y) + z), (((w + y) + u) + v)), (y + min((x + z), ((v + w) + u)))) ||
 rewrite(min(((x + y) + z), (((x + w) + u) + v)), (x + min((y + z), ((v + w) + u)))) ||
 rewrite(min(((x + y) + z), (((y + w) + u) + v)), (y + min((x + z), ((v + w) + u)))) ||
 rewrite(min(((x + y) + z), (((y + x) - w) + u)), (x + min((y + z), (u + (y - w))))) ||
 rewrite(min(((x + (y + z)) + w), (u + y)), (min((x + (w + z)), u) + y)) ||
 rewrite(min(((x + (y + z)) + w), (u + z)), (min((x + (y + w)), u) + z)) ||
 rewrite(min(((x + (y + z)) + w), (u + (z + y))), (min((x + w), u) + (z + y))) ||
 rewrite(min(((x + (y + z)) + w), (y + u)), (min((x + (w + z)), u) + y)) ||
 rewrite(min(((x + (y + z)) + w), (z + u)), (min((x + (y + w)), u) + z)) ||
 rewrite(min(((x + (y + z)) + w), ((z + y) + u)), (min((x + w), u) + (z + y))) ||
 rewrite(min(((x + (y + (z + w))) + u), (v + w)), (min((x + (y + (z + u))), v) + w)) ||
 rewrite(min(((x + (y + (z + w))) + u), (v + z)), (min((x + (y + (u + w))), v) + z)) ||
 rewrite(min(((x + (y + (z + w))) + u), (w + v)), (min((x + (y + (z + u))), v) + w)) ||
 rewrite(min(((x + (y + (z + w))) + u), (z + v)), (min((x + (y + (u + w))), v) + z)) ||
 rewrite(min(((x + ((y + z) + w)) + u), (v + y)), (min((x + ((u + z) + w)), v) + y)) ||
 rewrite(min(((x + ((y + z) + w)) + u), (v + z)), (min((x + ((y + u) + w)), v) + z)) ||
 rewrite(min(((x + ((y + z) + w)) + u), (y + v)), (min((x + ((u + z) + w)), v) + y)) ||
 rewrite(min(((x + ((y + z) + w)) + u), (z + v)), (min((x + ((y + u) + w)), v) + z)) ||
 rewrite(min(((x + (y - z)) + w), (u + (v + y))), (y + min((x + (w - z)), (u + v)))) ||
 rewrite(min(((x + (y - z)) + w), (u + (y + v))), (y + min((x + (w - z)), (u + v)))) ||
 rewrite(min(((x + (y - z)) + w), (u + (y - v))), (y + min((x - (z - w)), (u - v)))) ||
 rewrite(min(((x + (y - z)) + w), ((u + y) + v)), (y + min((x + (w - z)), (u + v)))) ||
 rewrite(min(((x + (y - z)) + w), ((y + u) + v)), (y + min((x + (w - z)), (v + u)))) ||
 rewrite(min(((x + (y - z)) + w), ((y - u) + v)), (y + min((x - (z - w)), (v - u)))) ||
 rewrite(min(((x + (y*c0)) + z), (w + (y*c1))), (min(w, (x + z)) + (y*fold(min(c0, c1)))), ((c0 <= c1) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min(((x + (y*c0)) + z), ((y*c1) + w)), (min(w, (x + z)) + (y*fold(min(c0, c1)))), ((c0 <= c1) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min(((x + (y*z)) + w), (u + (z*y))), (min((x + w), u) + (z*y))) ||
 rewrite(min(((x + (y*z)) + w), ((z*y) + u)), (min((x + w), u) + (z*y))) ||
 rewrite(min((((x + y) + z) + w), (u + x)), (x + min(((y + z) + w), u))) ||
 rewrite(min((((x + y) + z) + w), (u + y)), (y + min(((x + z) + w), u))) ||
 rewrite(min((((x + y) + z) + w), (u + (y + x))), (min(u, (w + z)) + (y + x))) ||
 rewrite(min((((x + y) + z) + w), (x + u)), (x + min(((y + z) + w), u))) ||
 rewrite(min((((x + y) + z) + w), (y + u)), (y + min(((x + z) + w), u))) ||
 rewrite(min((((x + y) + z) + w), ((y + x) + u)), (min(u, (z + w)) + (y + x))) ||
 rewrite(min((((x + (y + z)) + w) + u), (v + y)), (min(((x + (u + z)) + w), v) + y)) ||
 rewrite(min((((x + (y + z)) + w) + u), (v + z)), (min(((x + (y + u)) + w), v) + z)) ||
 rewrite(min((((x + (y + z)) + w) + u), (y + v)), (min(((x + (u + z)) + w), v) + y)) ||
 rewrite(min((((x + (y + z)) + w) + u), (z + v)), (min(((x + (y + u)) + w), v) + z)) ||
 rewrite(min(((((x + y) + z) + w) + u), (v + x)), (x + min((((y + z) + w) + u), v))) ||
 rewrite(min(((((x + y) + z) + w) + u), (v + y)), (y + min((((x + z) + w) + u), v))) ||
 rewrite(min(((((x + y) + z) + w) + u), (x + v)), (x + min((((y + z) + w) + u), v))) ||
 rewrite(min(((((x + y) + z) + w) + u), (y + v)), (y + min((((x + z) + w) + u), v))) ||
 rewrite(min((((x - y) + z) + w), (u + (v + x))), (x + min(((w - y) + z), (u + v)))) ||
 rewrite(min((((x - y) + z) + w), (u + (x + v))), (x + min(((w - y) + z), (u + v)))) ||
 rewrite(min((((x - y) + z) + w), (u + (x - v))), (x + min((w - (y - z)), (u - v)))) ||
 rewrite(min((((x - y) + z) + w), ((u + x) + v)), (x + min(((w - y) + z), (u + v)))) ||
 rewrite(min((((x - y) + z) + w), ((x + u) + v)), (x + min(((w - y) + z), (v + u)))) ||
 rewrite(min((((x - y) + z) + w), ((x - u) + v)), (x + min((w - (y - z)), (v - u)))) ||
 rewrite(min((((x*c0) + y) + z), (w + (x*c1))), (min(w, (y + z)) + (x*fold(min(c0, c1)))), ((c0 <= c1) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min((((x*c0) + y) + z), ((x*c1) + w)), (min(w, (y + z)) + (x*fold(min(c0, c1)))), ((c0 <= c1) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min((((x*y) + z) + w), (u + (y*x))), (min(u, (z + w)) + (y*x))) ||
 rewrite(min((((x*y) + z) + w), ((y*x) + u)), (min(u, (z + w)) + (y*x))) ||
 rewrite(min(((x - y) + z), (w + (u + x))), (x + min((z - y), (w + u)))) ||
 rewrite(min(((x - y) + z), (w + (u + (v + x)))), (x + min((z - y), (w + (u + v))))) ||
 rewrite(min(((x - y) + z), (w + (u + (x + v)))), (x + min((z - y), (w + (u + v))))) ||
 rewrite(min(((x - y) + z), (w + (u + (x - v)))), (x + min((z - y), (w + (u - v))))) ||
 rewrite(min(((x - y) + z), (w + (x + u))), (x + min((z - y), (w + u)))) ||
 rewrite(min(((x - y) + z), (w + ((u + x) + v))), (x + min((z - y), (w + (v + u))))) ||
 rewrite(min(((x - y) + z), (w + ((x + u) + v))), (x + min((z - y), (w + (v + u))))) ||
 rewrite(min(((x - y) + z), (w + ((x - u) + v))), (x + min((z - y), (w + (v - u))))) ||
 rewrite(min(((x - y) + z), (w + (u - y))), (min((x + z), (w + u)) - y)) ||
 rewrite(min(((x - y) + z), (w + (x - u))), (x + min((z - y), (w - u)))) ||
 rewrite(min(((x - y) + z), ((w + x) + u)), (x + min((z - y), (u + w)))) ||
 rewrite(min(((x - y) + z), ((w + (u + x)) + v)), (x + min((z - y), (w + (u + v))))) ||
 rewrite(min(((x - y) + z), ((w + (x + u)) + v)), (x + min((z - y), (w + (v + u))))) ||
 rewrite(min(((x - y) + z), ((w + (x - u)) + v)), (x + min((z - y), (w + (v - u))))) ||
 rewrite(min(((x - y) + z), ((x + w) + u)), (x + min((z - y), (u + w)))) ||
 rewrite(min(((x - y) + z), (((w + x) + u) + v)), (x + min((z - y), ((v + w) + u)))) ||
 rewrite(min(((x - y) + z), (((x + w) + u) + v)), (x + min((z - y), ((v + w) + u)))) ||
 rewrite(min(((x - y) + z), (((x - w) + u) + v)), (x + min((z - y), ((v - w) + u)))) ||
 rewrite(min(((x - y) + z), ((w - y) + u)), (min((x + z), (w + u)) - y)) ||
 rewrite(min(((x - y) + z), ((x - w) + u)), (x + min((z - y), (u - w)))) ||
 rewrite(min((((x + y) - z) + w), (u + (x - v))), (x + min(((w + y) - z), (u - v)))) ||
 rewrite(min((((x + y) - z) + w), (u + (y - v))), (y + min(((x + w) - z), (u - v)))) ||
 rewrite(min((((x + y) - z) + w), (z + (u + x))), (x + min(((y - z) + w), (z + u)))) ||
 rewrite(min((((x + y) - z) + w), (z + (u + y))), (y + min(((x - z) + w), (z + u)))) ||
 rewrite(min((((x + y) - z) + w), (z + (x + u))), (x + min(((y - z) + w), (z + u)))) ||
 rewrite(min((((x + y) - z) + w), (z + (y + u))), (y + min(((x - z) + w), (z + u)))) ||
 rewrite(min((((x + y) - z) + w), ((u + x) + z)), (x + min(((y - z) + w), (z + u)))) ||
 rewrite(min((((x + y) - z) + w), ((u + y) + z)), (y + min(((x - z) + w), (z + u)))) ||
 rewrite(min((((x + y) - z) + w), ((x + u) + z)), (x + min(((y - z) + w), (z + u)))) ||
 rewrite(min((((x + y) - z) + w), ((y + u) + z)), (y + min(((x - z) + w), (z + u)))) ||
 rewrite(min((((x + y) - z) + w), ((x - u) + v)), (x + min((y - (z - w)), (v - u)))) ||
 rewrite(min((((x + y) - z) + w), ((y - u) + v)), (y + min((x - (z - w)), (v - u)))) ||
 rewrite(min(((x*c0) + y), (z + (w + (x*c1)))), (min((z + w), y) + (x*fold(min(c0, c1)))), (max(c0, c1) == min(c0, c1))) ||
 rewrite(min(((x*c0) + y), (z + ((x*c1) + w))), (min((z + w), y) + (x*fold(min(c0, c1)))), (max(c0, c1) == min(c0, c1))) ||
 rewrite(min(((x*c0) + y), ((z + (x*c1)) + w)), (min((w + z), y) + (x*fold(min(c0, c1)))), (max(c0, c1) == min(c0, c1))) ||
 rewrite(min(((x*c0) + y), (((x*c1) + z) + w)), (min((w + z), y) + (x*fold(min(c0, c1)))), (max(c0, c1) == min(c0, c1))) ||
 rewrite(min(((x*y) + z), (w + (u + (y*x)))), (min((w + u), z) + (y*x))) ||
 rewrite(min(((x*y) + z), (w + ((y*x) + u))), (min((w + u), z) + (y*x))) ||
 rewrite(min(((x*y) + z), ((w + (y*x)) + u)), (min((u + w), z) + (y*x))) ||
 rewrite(min(((x*y) + z), (((y*x) + w) + u)), (min((u + w), z) + (y*x))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(min((min(x, (y*z)) - (w + (y*z))), u), min((min((x - (y*z)), 0) - w), u)) ||
 rewrite(min((min(x, (y*z)) - (w + (z*y))), u), min((min((x - (y*z)), 0) - w), u)) ||
 rewrite(min((min(x, (y*z)) - ((y*z) + w)), u), min((min((x - (y*z)), 0) - w), u)) ||
 rewrite(min((min(x, (y*z)) - ((z*y) + w)), u), min((min((x - (y*z)), 0) - w), u)) ||
 rewrite(min((min((x*y), z) - (w + (x*y))), u), min((min((z - (y*x)), 0) - w), u)) ||
 rewrite(min((min((x*y), z) - (w + (y*x))), u), min((min((z - (y*x)), 0) - w), u)) ||
 rewrite(min((min((x*y), z) - ((x*y) + w)), u), min((min((z - (x*y)), 0) - w), u)) ||
 rewrite(min((min((x*y), z) - ((y*x) + w)), u), min((min((z - (y*x)), 0) - w), u)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((x - y), (z + (w + (u - y)))), (min(x, (z + (w + u))) - y)) ||
 rewrite(min((x - y), (z + ((w - y) + u))), (min(x, (z + (w + u))) - y)) ||
 rewrite(min((x - y), (z + (w - y))), (min(x, (z + w)) - y)) ||
 rewrite(min((x - y), (z + (w - (u + y)))), (min(x, (z + (w - u))) - y)) ||
 rewrite(min((x - y), (z + (w - (y + u)))), (min(x, (z + (w - u))) - y)) ||
 rewrite(min((x - y), (z + (x - (w + u)))), (x - max(y, (u + (w - z))))) ||
 rewrite(min((x - y), ((z + (w - y)) + u)), (min(x, (z + (w + u))) - y)) ||
 rewrite(min((x - y), (((z - y) + w) + u)), (min(x, ((u + z) + w)) - y)) ||
 rewrite(min((x - y), ((x - (z + w)) + u)), (x - max((w + (z - u)), y))) ||
 rewrite(min((x - y), ((x - min(y, z)) + c0)), (x - y), (0 <= c0)) ||
 rewrite(min((x - y), ((x - min(z, y)) + c0)), (x - y), (0 <= c0)) ||
 rewrite(min((x - y), ((z - y) + w)), (min(x, (z + w)) - y)) ||
 rewrite(min((x - y), ((z - (w + y)) + u)), (min(x, (z - (w - u))) - y)) ||
 rewrite(min((x - y), ((z - (y + w)) + u)), (min(x, (z + (u - w))) - y)) ||
 rewrite(min((x - (y + z)), (w + (x + (u - y)))), (min((x - z), (w + (x + u))) - y)) ||
 rewrite(min((x - (y + z)), (w + (x + (u - z)))), (min((x - y), (w + (x + u))) - z)) ||
 rewrite(min((x - (y + z)), (w + ((u - y) + x))), (min((x - z), (w + (x + u))) - y)) ||
 rewrite(min((x - (y + z)), (w + ((u - z) + x))), (min((x - y), (w + (x + u))) - z)) ||
 rewrite(min((x - (y + z)), (w + (u - y))), (min((x - z), (w + u)) - y)) ||
 rewrite(min((x - (y + z)), (w + (u - z))), (min((x - y), (w + u)) - z)) ||
 rewrite(min((x - (y + z)), (w + (u - (v + y)))), (min((x - z), (w + (u - v))) - y)) ||
 rewrite(min((x - (y + z)), (w + (u - (v + z)))), (min((x - y), (w + (u - v))) - z)) ||
 rewrite(min((x - (y + z)), (w + (u - (y + v)))), (min((x - z), (w + (u - v))) - y)) ||
 rewrite(min((x - (y + z)), (w + (u - (z + v)))), (min((x - y), (w + (u - v))) - z)) ||
 rewrite(min((x - (y + z)), ((x + (w - y)) + u)), (min((x - z), (x + (w + u))) - y)) ||
 rewrite(min((x - (y + z)), ((x + (w - z)) + u)), (min((x - y), (x + (w + u))) - z)) ||
 rewrite(min((x - (y + z)), (((w - y) + x) + u)), (min((x - z), ((u + w) + x)) - y)) ||
 rewrite(min((x - (y + z)), (((w - z) + x) + u)), (min((x - y), ((u + w) + x)) - z)) ||
 rewrite(min((x - (y + z)), ((w - y) + u)), (min((x - z), (w + u)) - y)) ||
 rewrite(min((x - (y + z)), ((w - z) + u)), (min((x - y), (w + u)) - z)) ||
 rewrite(min((x - (y + z)), ((w - (u + y)) + v)), (min((x - z), (w - (u - v))) - y)) ||
 rewrite(min((x - (y + z)), ((w - (u + z)) + v)), (min((x - y), (w - (u - v))) - z)) ||
 rewrite(min((x - (y + z)), ((w - (y + u)) + v)), (min((x - z), (w + (v - u))) - y)) ||
 rewrite(min((x - (y + z)), ((w - (z + u)) + v)), (min((x - y), (w + (v - u))) - z)) ||
 rewrite(min((x - (y + (z - w))), (u + (w - v))), (w + min((x - (z + y)), (u - v)))) ||
 rewrite(min((x - (y + (z - w))), ((w - u) + v)), (w + min((x - (z + y)), (v - u)))) ||
 rewrite(min((x - ((y - z) + w)), (u + (z - v))), (z + min((x - (y + w)), (u - v)))) ||
 rewrite(min((x - ((y - z) + w)), ((z - u) + v)), (z + min((x - (y + w)), (v - u)))) ||
 rewrite(min((x - (y*c0)), (z + (w - (y*c1)))), (min(x, (w + z)) - (y*fold(max(c0, c1)))), (((max(c0, c1) + min((7 - c0), (5 - c1))) == 5) && ((min((c1*7), ((c0*7) + 6)) + (max(c0, c1)*-7)) == 0))) ||
 rewrite(min((x - (y*c0)), ((z - (y*c1)) + w)), (min(x, (z + w)) - (y*fold(max(c0, c1)))), (((max(c0, c1) + min((7 - c0), (5 - c1))) == 5) && ((min((c1*7), ((c0*7) + 6)) + (max(c0, c1)*-7)) == 0))) ||
 rewrite(min((x - (y*z)), (w + (u - (z*y)))), (min(x, (u + w)) - (z*y))) ||
 rewrite(min((x - (y*z)), ((w - (z*y)) + u)), (min(x, (w + u)) - (z*y))) ||
 rewrite(min(((x + y) - z), (w + x)), (x + min((y - z), w))) ||
 rewrite(min(((x + y) - z), (w + y)), (y + min((x - z), w))) ||
 rewrite(min(((x + y) - z), (w + (u + (v + x)))), (x + min((y - z), (w + (u + v))))) ||
 rewrite(min(((x + y) - z), (w + (u + (v + y)))), (y + min((x - z), (w + (u + v))))) ||
 rewrite(min(((x + y) - z), (w + (u + (x + v)))), (x + min((y - z), (w + (u + v))))) ||
 rewrite(min(((x + y) - z), (w + (u + (y + v)))), (y + min((x - z), (w + (u + v))))) ||
 rewrite(min(((x + y) - z), (w + (u + (x - v)))), (x + min((y - z), (w + (u - v))))) ||
 rewrite(min(((x + y) - z), (w + (u + (y - v)))), (y + min((x - z), (w + (u - v))))) ||
 rewrite(min(((x + y) - z), (w + (x + y))), (x + min((y - z), (w + y)))) ||
 rewrite(min(((x + y) - z), (w + (y + x))), (y + min((x - z), (w + x)))) ||
 rewrite(min(((x + y) - z), (w + ((u + x) + v))), (x + min((y - z), (w + (v + u))))) ||
 rewrite(min(((x + y) - z), (w + ((u + y) + v))), (y + min((x - z), (w + (v + u))))) ||
 rewrite(min(((x + y) - z), (w + ((x + u) + v))), (x + min((y - z), (w + (v + u))))) ||
 rewrite(min(((x + y) - z), (w + ((y + u) + v))), (y + min((x - z), (w + (v + u))))) ||
 rewrite(min(((x + y) - z), (w + ((x - u) + v))), (x + min((y - z), (w + (v - u))))) ||
 rewrite(min(((x + y) - z), (w + ((y - u) + v))), (y + min((x - z), (w + (v - u))))) ||
 rewrite(min(((x + y) - z), (w + ((x + y) - u))), (x + min((y - z), (w - (u - y))))) ||
 rewrite(min(((x + y) - z), (w + ((y + x) - u))), (y + min((x - z), (w - (u - x))))) ||
 rewrite(min(((x + y) - z), (x + w)), (x + min((y - z), w))) ||
 rewrite(min(((x + y) - z), (y + w)), (y + min((x - z), w))) ||
 rewrite(min(((x + y) - z), ((w + (u + x)) + v)), (x + min((y - z), (w + (u + v))))) ||
 rewrite(min(((x + y) - z), ((w + (u + y)) + v)), (y + min((x - z), (w + (u + v))))) ||
 rewrite(min(((x + y) - z), ((w + (x + u)) + v)), (x + min((y - z), (w + (v + u))))) ||
 rewrite(min(((x + y) - z), ((w + (y + u)) + v)), (y + min((x - z), (w + (v + u))))) ||
 rewrite(min(((x + y) - z), ((w + (x - u)) + v)), (x + min((y - z), (w + (v - u))))) ||
 rewrite(min(((x + y) - z), ((w + (y - u)) + v)), (y + min((x - z), (w + (v - u))))) ||
 rewrite(min(((x + y) - z), ((x + y) + w)), (y + min((x - z), (x + w)))) ||
 rewrite(min(((x + y) - z), ((y + x) + w)), (x + min((y - z), (y + w)))) ||
 rewrite(min(((x + y) - z), (((w + x) + u) + v)), (x + min((y - z), ((v + w) + u)))) ||
 rewrite(min(((x + y) - z), (((w + y) + u) + v)), (y + min((x - z), ((v + w) + u)))) ||
 rewrite(min(((x + y) - z), (((x + w) + u) + v)), (x + min((y - z), ((v + w) + u)))) ||
 rewrite(min(((x + y) - z), (((y + w) + u) + v)), (y + min((x - z), ((v + w) + u)))) ||
 rewrite(min(((x + y) - z), (((x - w) + u) + v)), (x + min((y - z), ((v - w) + u)))) ||
 rewrite(min(((x + y) - z), (((y - w) + u) + v)), (y + min((x - z), ((v - w) + u)))) ||
 rewrite(min(((x + y) - z), (((x + y) - w) + u)), (y + min((x - z), ((u + x) - w)))) ||
 rewrite(min(((x + y) - z), (((y + x) - w) + u)), (x + min((y - z), ((u + y) - w)))) ||
 rewrite(min(((x + (y + z)) - w), (u + y)), (min((x - (w - z)), u) + y)) ||
 rewrite(min(((x + (y + z)) - w), (u + z)), (min((x + (y - w)), u) + z)) ||
 rewrite(min(((x + (y + z)) - w), (y + u)), (min((x - (w - z)), u) + y)) ||
 rewrite(min(((x + (y + z)) - w), (z + u)), (min((x + (y - w)), u) + z)) ||
 rewrite(min((((x + y) + z) - w), (u + x)), (x + min(((y + z) - w), u))) ||
 rewrite(min((((x + y) + z) - w), (u + y)), (y + min(((x + z) - w), u))) ||
 rewrite(min((((x + y) + z) - w), (x + u)), (x + min(((y + z) - w), u))) ||
 rewrite(min((((x + y) + z) - w), (y + u)), (y + min(((x + z) - w), u))) ||
 rewrite(min(((x - y) - z), (w + (u - y))), (min((x - z), (w + u)) - y)) ||
 rewrite(min(((x - y) - z), ((w - y) + u)), (min((x - z), (w + u)) - y)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min((x - y), (z - (w + y))), (min(x, (z - w)) - y)) ||
 rewrite(min((x - y), (z - (y + w))), (min(x, (z - w)) - y)) ||
 rewrite(min((x - (y + z)), (w - y)), (min(w, (x - z)) - y)) ||
 rewrite(min((x - (y + z)), (w - z)), (min(w, (x - y)) - z)) ||
 rewrite(min((x - (y*c0)), (z - (w + (y*c1)))), (min(x, (z - w)) - (y*fold(max(c0, c1)))), (((min((c0*8), ((c1*8) + 10)) + (max(c0, c1)*-8)) == 0) && ((min((-1 - (c0*8)), (-9 - (c1*8))) + (max(c0, c1)*8)) == -9))) ||
 rewrite(min((x - (y*c0)), (z - (w + (y*c1)))), (min(x, (z - w)) - (y*fold(max(c0, c1)))), (((min((c0*8), ((c1*8) + 10)) + (max(c0, c1)*-8)) == 0) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min((x - (y*c0)), (z - ((y*c1) + w))), (min(x, (z - w)) - (y*fold(max(c0, c1)))), (((min((c0*8), ((c1*8) + 10)) + (max(c0, c1)*-8)) == 0) && ((min((-1 - (c0*8)), (-9 - (c1*8))) + (max(c0, c1)*8)) == -9))) ||
 rewrite(min((x - (y*c0)), (z - ((y*c1) + w))), (min(x, (z - w)) - (y*fold(max(c0, c1)))), (((min((c0*8), ((c1*8) + 10)) + (max(c0, c1)*-8)) == 0) && (max(c0, c1) == min(c0, c1)))) ||
 rewrite(min((x - (y*z)), (w - (u + (z*y)))), (min(x, (w - u)) - (z*y))) ||
 rewrite(min((x - (y*z)), (w - ((z*y) + u))), (min(x, (w - u)) - (z*y))) ||
 rewrite(min(((c0 - x) - y), (c1 - z)), (fold(min((c0 + 1), c1)) - max((y + x), z)), ((c1 <= c0) && (c0 == c1))) ||
 rewrite(min(((x - y) - z), (x - w)), (x - max((y + z), w))) ||
 rewrite(min((((x + y) - z) - w), (x - u)), (x - max(((w - y) + z), u))) ||
 rewrite(min((((x + y) - z) - w), (y - u)), (y - max((w - (x - z)), u))) ||
 rewrite(min((((x + y) - z) - w), ((u + x) - v)), (x + min(((y - z) - w), (u - v)))) ||
 rewrite(min((((x + y) - z) - w), ((u + y) - v)), (y + min(((x - z) - w), (u - v)))) ||
 rewrite(min((((x + y) - z) - w), ((x + u) - v)), (x + min(((y - z) - w), (u - v)))) ||
 rewrite(min((((x + y) - z) - w), ((y + u) - v)), (y + min(((x - z) - w), (u - v)))))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite(min(min(x, y), x), min(y, x)) ||
 rewrite(min(min(x, y), y), min(x, y)) ||
 rewrite(min(min(x, (y + c0)), y), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, (y + (z + min(w, x)))), w), (min((z + y), 0) + min(w, x))) ||
 rewrite(min(min(x, (y + (z + min(x, w)))), w), (min((z + y), 0) + min(x, w))) ||
 rewrite(min(min(x, (y + (min(x, z) + w))), z), (min(x, z) + min((w + y), 0))) ||
 rewrite(min(min(x, (y + (min(z, x) + w))), z), (min(x, z) + min((w + y), 0))) ||
 rewrite(min(min(x, (y + min(x, z))), z), (min(y, 0) + min(x, z))) ||
 rewrite(min(min(x, (y + min(z, x))), z), (min(y, 0) + min(z, x))) ||
 rewrite(min(min(x, ((y + min(x, z)) + w)), z), (min(z, x) + min((y + w), 0))) ||
 rewrite(min(min(x, ((y + min(z, x)) + w)), z), (min(z, x) + min((y + w), 0))) ||
 rewrite(min(min(x, ((min(x, y) + z) + w)), y), (min(y, x) + min((z + w), 0))) ||
 rewrite(min(min(x, ((min(y, x) + z) + w)), y), (min(y, x) + min((z + w), 0))) ||
 rewrite(min(min(x, (min(x, y) + z)), y), (min(z, 0) + min(x, y))) ||
 rewrite(min(min(x, (min(y, x) + z)), y), (min(z, 0) + min(y, x))) ||
 rewrite(min(min(x, min(y, z)), y), min(x, min(y, z))) ||
 rewrite(min(min(x, min(y, z)), z), min(x, min(y, z))) ||
 rewrite(min(min(x, min(y, (z + (w + min(u, x))))), u), min((min((w + z), 0) + min(u, x)), y)) ||
 rewrite(min(min(x, min(y, (z + (w + min(x, u))))), u), min((min((w + z), 0) + min(x, u)), y)) ||
 rewrite(min(min(x, min(y, (z + (min(w, x) + u)))), w), min(y, (min(w, x) + min((u + z), 0)))) ||
 rewrite(min(min(x, min(y, (z + (min(x, w) + u)))), w), min(y, (min(x, w) + min((u + z), 0)))) ||
 rewrite(min(min(x, min(y, (z + min(w, y)))), w), min(x, (min(z, 0) + min(w, y)))) ||
 rewrite(min(min(x, min(y, (z + min(w, min(x, y))))), w), (min(z, 0) + min(w, min(x, y)))) ||
 rewrite(min(min(x, min(y, (z + min(w, min(y, x))))), w), (min(z, 0) + min(w, min(y, x)))) ||
 rewrite(min(min(x, min(y, (z + min(y, w)))), w), min(x, (min(z, 0) + min(y, w)))) ||
 rewrite(min(min(x, min(y, (z + min(min(x, y), w)))), w), (min(z, 0) + min(min(x, y), w))) ||
 rewrite(min(min(x, min(y, (z + min(min(y, x), w)))), w), (min(z, 0) + min(min(y, x), w))) ||
 rewrite(min(min(x, min(y, ((z + min(w, x)) + u))), w), min(y, (min(w, x) + min((z + u), 0)))) ||
 rewrite(min(min(x, min(y, ((z + min(x, w)) + u))), w), min(y, (min(x, w) + min((z + u), 0)))) ||
 rewrite(min(min(x, min(y, ((min(x, z) + w) + u))), z), min(y, (min(x, z) + min((w + u), 0)))) ||
 rewrite(min(min(x, min(y, ((min(z, x) + w) + u))), z), min(y, (min(z, x) + min((w + u), 0)))) ||
 rewrite(min(min(x, min(y, (min(y, z) + w))), z), min(x, (min(y, z) + min(w, 0)))) ||
 rewrite(min(min(x, min(y, (min(z, y) + w))), z), min(x, (min(z, y) + min(w, 0)))) ||
 rewrite(min(min(x, min(y, (min(z, min(x, y)) + w))), z), (min(w, 0) + min(z, min(x, y)))) ||
 rewrite(min(min(x, min(y, (min(z, min(y, x)) + w))), z), (min(w, 0) + min(z, min(y, x)))) ||
 rewrite(min(min(x, min(y, (min(min(x, y), z) + w))), z), (min(w, 0) + min(min(x, y), z))) ||
 rewrite(min(min(x, min(y, (min(min(y, x), z) + w))), z), (min(w, 0) + min(min(y, x), z))) ||
 rewrite(min(min(x, min(y, (min(x, z) - w))), z), min(y, (min(x, z) - max(w, 0)))) ||
 rewrite(min(min(x, min(y, (min(z, x) - w))), z), min(y, (min(z, x) - max(w, 0)))) ||
 rewrite(min(min(x, min(y, min(z, (min(w, x) - u)))), w), min(y, min(z, (min(w, x) - max(u, 0))))) ||
 rewrite(min(min(x, min(y, min(z, (min(x, w) - u)))), w), min(y, min(z, (min(x, w) - max(u, 0))))) ||
 rewrite(min(min(x, min(y, min(z, min(w, min(u, v))))), u), min(x, min(y, min(z, min(w, min(u, v)))))) ||
 rewrite(min(min(x, min(y, min(z, min(w, min(u, v))))), v), min(x, min(y, min(z, min(w, min(u, v)))))) ||
 rewrite(min(min(x, min(y, min(z, min(min(w, u), v)))), u), min(x, min(y, min(z, min(min(w, u), v))))) ||
 rewrite(min(min(x, min(y, min(z, min(min(w, u), v)))), w), min(x, min(y, min(z, min(min(w, u), v))))) ||
 rewrite(min(min(x, min(y, min((min(x, z) - w), u))), z), min(u, min(y, (min(x, z) - max(w, 0))))) ||
 rewrite(min(min(x, min(y, min((min(z, x) - w), u))), z), min(u, min(y, (min(z, x) - max(w, 0))))) ||
 rewrite(min(min(x, min(y, min(min(z, min(w, u)), v))), u), min(x, min(y, min(min(z, min(w, u)), v)))) ||
 rewrite(min(min(x, min(y, min(min(z, min(w, u)), v))), w), min(x, min(y, min(min(z, min(w, u)), v)))) ||
 rewrite(min(min(x, min(y, min(min(min(z, w), u), v))), w), min(x, min(y, min(min(min(z, w), u), v)))) ||
 rewrite(min(min(x, min(y, min(min(min(z, w), u), v))), z), min(x, min(y, min(min(min(z, w), u), v)))) ||
 rewrite(min(min(x, min((y + (z + min(w, x))), u)), w), min((min((z + y), 0) + min(w, x)), u)) ||
 rewrite(min(min(x, min((y + (z + min(x, w))), u)), w), min((min((z + y), 0) + min(x, w)), u)) ||
 rewrite(min(min(x, min((y + (min(x, z) + w)), u)), z), min((min((w + y), 0) + min(x, z)), u)) ||
 rewrite(min(min(x, min((y + (min(z, x) + w)), u)), z), min((min((w + y), 0) + min(z, x)), u)) ||
 rewrite(min(min(x, min((y + min(z, w)), w)), z), min(x, (min(y, 0) + min(z, w)))) ||
 rewrite(min(min(x, min((y + min(z, w)), z)), w), min(x, (min(y, 0) + min(w, z)))) ||
 rewrite(min(min(x, min((y + min(z, min(w, x))), w)), z), (min(y, 0) + min(z, min(w, x)))) ||
 rewrite(min(min(x, min((y + min(z, min(x, w))), w)), z), (min(y, 0) + min(z, min(x, w)))) ||
 rewrite(min(min(x, min((y + min(min(x, z), w)), z)), w), (min(y, 0) + min(x, min(w, z)))) ||
 rewrite(min(min(x, min((y + min(min(z, x), w)), z)), w), (min(y, 0) + min(x, min(w, z)))) ||
 rewrite(min(min(x, min(((y + min(x, z)) + w), u)), z), min(u, (min((y + w), 0) + min(z, x)))) ||
 rewrite(min(min(x, min(((y + min(z, x)) + w), u)), z), min(u, (min((y + w), 0) + min(z, x)))) ||
 rewrite(min(min(x, min(((min(x, y) + z) + w), u)), y), min(u, (min(x, y) + min((z + w), 0)))) ||
 rewrite(min(min(x, min(((min(y, x) + z) + w), u)), y), min(u, (min(y, x) + min((z + w), 0)))) ||
 rewrite(min(min(x, min((min(y, z) + w), y)), z), min(x, (min(y, z) + min(w, 0)))) ||
 rewrite(min(min(x, min((min(y, z) + w), z)), y), min(x, (min(y, z) + min(w, 0)))) ||
 rewrite(min(min(x, min((min(y, min(x, z)) + w), z)), y), (min(w, 0) + min(y, min(x, z)))) ||
 rewrite(min(min(x, min((min(y, min(z, x)) + w), z)), y), (min(w, 0) + min(y, min(z, x)))) ||
 rewrite(min(min(x, min((min(min(x, y), z) + w), y)), z), (min(w, 0) + min(min(x, z), y))) ||
 rewrite(min(min(x, min((min(min(y, x), z) + w), y)), z), (min(w, 0) + min(min(x, z), y))) ||
 rewrite(min(min(x, min((min(x, y) - z), w)), y), min(w, (min(x, y) - max(z, 0)))) ||
 rewrite(min(min(x, min((min(y, x) - z), w)), y), min(w, (min(y, x) - max(z, 0)))) ||
 rewrite(min(min(x, min(min(y, (min(x, z) - w)), u)), z), min(y, min((min(x, z) - max(w, 0)), u))) ||
 rewrite(min(min(x, min(min(y, (min(z, x) - w)), u)), z), min(y, min((min(z, x) - max(w, 0)), u))) ||
 rewrite(min(min(x, min(min(y, min(z, min(w, u))), v)), u), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(min(x, min(min(y, min(z, min(w, u))), v)), w), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(min(x, min(min(y, min(min(z, w), u)), v)), w), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(min(x, min(min(y, min(min(z, w), u)), v)), z), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(min(x, min(min((min(x, y) - z), w), u)), y), min(w, min((min(x, y) - max(z, 0)), u))) ||
 rewrite(min(min(x, min(min((min(y, x) - z), w), u)), y), min(w, min((min(y, x) - max(z, 0)), u))) ||
 rewrite(min(min(x, min(min(min(y, min(z, w)), u), v)), w), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(min(x, min(min(min(y, min(z, w)), u), v)), z), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(min(x, min(min(min(min(y, z), w), u), v)), y), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(min(x, min(min(min(min(y, z), w), u), v)), z), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(min(x, max(y, min(z, w))), w), min(min(x, max(y, z)), w)) ||
 rewrite(min(min(x, max(y, min(z, w))), z), min(min(x, max(y, w)), z)) ||
 rewrite(min(min(x, max(min(y, z), w)), y), min(min(x, max(z, w)), y)) ||
 rewrite(min(min(x, max(min(y, z), w)), z), min(min(x, max(y, w)), z)) ||
 rewrite(min(min((x + c0), y), x), min(x, y), (0 <= c0)) ||
 rewrite(min(min((x + (y + min(z, w))), w), z), (min((x + y), 0) + min(z, w))) ||
 rewrite(min(min((x + (y + min(z, w))), z), w), (min((x + y), 0) + min(w, z))) ||
 rewrite(min(min((x + (min(y, z) + w)), y), z), (min((x + w), 0) + min(z, y))) ||
 rewrite(min(min((x + (min(y, z) + w)), z), y), (min((x + w), 0) + min(y, z))) ||
 rewrite(min(min((x + min(y, z)), y), z), (min(x, 0) + min(z, y))) ||
 rewrite(min(min((x + min(y, z)), z), y), (min(x, 0) + min(y, z))) ||
 rewrite(min(min(((x + min(y, z)) + w), y), z), (min((x + w), 0) + min(z, y))) ||
 rewrite(min(min(((x + min(y, z)) + w), z), y), (min((x + w), 0) + min(y, z))) ||
 rewrite(min(min(((min(x, y) + z) + w), x), y), (min((z + w), 0) + min(y, x))) ||
 rewrite(min(min(((min(x, y) + z) + w), y), x), (min((z + w), 0) + min(x, y))) ||
 rewrite(min(min((min(x, y) + z), x), y), (min(x, y) + min(z, 0))) ||
 rewrite(min(min((min(x, y) + z), y), x), (min(x, y) + min(z, 0))) ||
 rewrite(min(min(min(x, y), z), x), min(min(y, z), x)) ||
 rewrite(min(min(min(x, y), z), y), min(min(x, z), y)) ||
 rewrite(min(min(min(x, (y + (z + min(w, u)))), u), w), min(x, (min((z + y), 0) + min(w, u)))) ||
 rewrite(min(min(min(x, (y + (z + min(w, u)))), w), u), min(x, (min((z + y), 0) + min(u, w)))) ||
 rewrite(min(min(min(x, (y + (min(z, w) + u))), w), z), min(x, (min((u + y), 0) + min(z, w)))) ||
 rewrite(min(min(min(x, (y + (min(z, w) + u))), z), w), min(x, (min((u + y), 0) + min(w, z)))) ||
 rewrite(min(min(min(x, (y + min(x, z))), w), z), min((min(y, 0) + min(x, z)), w)) ||
 rewrite(min(min(min(x, (y + min(z, x))), w), z), min((min(y, 0) + min(z, x)), w)) ||
 rewrite(min(min(min(x, (y + min(z, min(w, x)))), w), z), (min(y, 0) + min(z, min(w, x)))) ||
 rewrite(min(min(min(x, (y + min(z, min(x, w)))), w), z), (min(y, 0) + min(z, min(x, w)))) ||
 rewrite(min(min(min(x, (y + min(min(x, z), w))), z), w), (min(y, 0) + min(min(x, z), w))) ||
 rewrite(min(min(min(x, (y + min(min(z, x), w))), z), w), (min(y, 0) + min(min(z, x), w))) ||
 rewrite(min(min(min(x, ((y + min(z, w)) + u)), w), z), min(x, (min((y + u), 0) + min(z, w)))) ||
 rewrite(min(min(min(x, ((y + min(z, w)) + u)), z), w), min(x, (min((y + u), 0) + min(w, z)))) ||
 rewrite(min(min(min(x, ((min(y, z) + w) + u)), y), z), min(x, (min(y, z) + min((w + u), 0)))) ||
 rewrite(min(min(min(x, ((min(y, z) + w) + u)), z), y), min(x, (min(y, z) + min((w + u), 0)))) ||
 rewrite(min(min(min(x, (min(x, y) + z)), w), y), min((min(z, 0) + min(x, y)), w)) ||
 rewrite(min(min(min(x, (min(y, x) + z)), w), y), min((min(z, 0) + min(y, x)), w)) ||
 rewrite(min(min(min(x, (min(y, min(x, z)) + w)), z), y), (min(w, 0) + min(y, min(x, z)))) ||
 rewrite(min(min(min(x, (min(y, min(z, x)) + w)), z), y), (min(w, 0) + min(y, min(z, x)))) ||
 rewrite(min(min(min(x, (min(min(x, y), z) + w)), y), z), (min(w, 0) + min(min(x, y), z))) ||
 rewrite(min(min(min(x, (min(min(y, x), z) + w)), y), z), (min(w, 0) + min(min(y, x), z))) ||
 rewrite(min(min(min(x, (min(y, z) - w)), y), z), min(x, (min(y, z) - max(w, 0)))) ||
 rewrite(min(min(min(x, (min(y, z) - w)), z), y), min(x, (min(y, z) - max(w, 0)))) ||
 rewrite(min(min(min(x, min(y, (min(z, w) - u))), w), z), min(x, min(y, (min(z, w) - max(u, 0))))) ||
 rewrite(min(min(min(x, min(y, (min(z, w) - u))), z), w), min(x, min(y, (min(z, w) - max(u, 0))))) ||
 rewrite(min(min(min(x, min(y, min(z, min(w, u)))), v), u), min(min(min(x, min(y, min(z, w))), v), u)) ||
 rewrite(min(min(min(x, min(y, min(z, min(w, u)))), v), w), min(min(min(x, min(y, min(z, u))), v), w)) ||
 rewrite(min(min(min(x, min(y, min(min(z, w), u))), v), w), min(min(min(x, min(y, min(z, u))), v), w)) ||
 rewrite(min(min(min(x, min(y, min(min(z, w), u))), v), z), min(min(min(x, min(y, min(w, u))), v), z)) ||
 rewrite(min(min(min(x, min((min(y, z) - w), u)), y), z), min(u, min(x, (min(y, z) - max(w, 0))))) ||
 rewrite(min(min(min(x, min((min(y, z) - w), u)), z), y), min(u, min(x, (min(y, z) - max(w, 0))))) ||
 rewrite(min(min(min(x, min(min(y, min(z, w)), u)), v), w), min(min(min(x, min(min(y, z), u)), v), w)) ||
 rewrite(min(min(min(x, min(min(y, min(z, w)), u)), v), z), min(min(min(x, min(min(y, w), u)), v), z)) ||
 rewrite(min(min(min(x, min(min(min(y, z), w), u)), v), y), min(min(min(x, min(min(z, w), u)), v), y)) ||
 rewrite(min(min(min(x, min(min(min(y, z), w), u)), v), z), min(min(min(x, min(min(y, w), u)), v), z)) ||
 rewrite(min(min(min((x + (y + min(z, w))), u), w), z), min((min((y + x), 0) + min(z, w)), u)) ||
 rewrite(min(min(min((x + (y + min(z, w))), u), z), w), min((min((y + x), 0) + min(z, w)), u)) ||
 rewrite(min(min(min((x + (min(y, z) + w)), u), y), z), min(u, (min((x + w), 0) + min(z, y)))) ||
 rewrite(min(min(min((x + (min(y, z) + w)), u), z), y), min(u, (min((x + w), 0) + min(y, z)))) ||
 rewrite(min(min(min((x + min(y, z)), y), w), z), min((min(x, 0) + min(y, z)), w)) ||
 rewrite(min(min(min((x + min(y, z)), z), w), y), min((min(x, 0) + min(y, z)), w)) ||
 rewrite(min(min(min((x + min(y, min(z, w))), w), z), y), (min(x, 0) + min(y, min(z, w)))) ||
 rewrite(min(min(min((x + min(y, min(z, w))), z), w), y), (min(x, 0) + min(y, min(z, w)))) ||
 rewrite(min(min(min((x + min(min(y, z), w)), y), z), w), (min(x, 0) + min(z, min(w, y)))) ||
 rewrite(min(min(min((x + min(min(y, z), w)), z), y), w), (min(x, 0) + min(y, min(w, z)))) ||
 rewrite(min(min(min(((x + min(y, z)) + w), u), y), z), min(u, (min((x + w), 0) + min(z, y)))) ||
 rewrite(min(min(min(((x + min(y, z)) + w), u), z), y), min(u, (min((x + w), 0) + min(y, z)))) ||
 rewrite(min(min(min(((min(x, y) + z) + w), u), x), y), min(u, (min((w + z), 0) + min(y, x)))) ||
 rewrite(min(min(min(((min(x, y) + z) + w), u), y), x), min(u, (min((w + z), 0) + min(x, y)))) ||
 rewrite(min(min(min((min(x, y) + z), x), w), y), min((min(x, y) + min(z, 0)), w)) ||
 rewrite(min(min(min((min(x, y) + z), y), w), x), min((min(x, y) + min(z, 0)), w)) ||
 rewrite(min(min(min((min(x, min(y, z)) + w), y), z), x), (min(w, 0) + min(x, min(y, z)))) ||
 rewrite(min(min(min((min(x, min(y, z)) + w), z), y), x), (min(w, 0) + min(x, min(y, z)))) ||
 rewrite(min(min(min((min(min(x, y), z) + w), x), y), z), (min(w, 0) + min(min(y, z), x))) ||
 rewrite(min(min(min((min(min(x, y), z) + w), y), x), z), (min(w, 0) + min(min(x, z), y))) ||
 rewrite(min(min(min((min(x, y) - z), w), x), y), min(w, (min(x, y) - max(z, 0)))) ||
 rewrite(min(min(min((min(x, y) - z), w), y), x), min(w, (min(x, y) - max(z, 0)))) ||
 rewrite(min(min(min(min(x, (min(y, z) - w)), u), y), z), min(x, min((min(y, z) - max(w, 0)), u))) ||
 rewrite(min(min(min(min(x, (min(y, z) - w)), u), z), y), min(x, min((min(y, z) - max(w, 0)), u))) ||
 rewrite(min(min(min(min(x, min(y, min(z, w))), u), v), w), min(min(min(min(x, min(y, z)), u), v), w)) ||
 rewrite(min(min(min(min(x, min(y, min(z, w))), u), v), z), min(min(min(min(x, min(y, w)), u), v), z)) ||
 rewrite(min(min(min(min(x, min(min(y, z), w)), u), v), y), min(min(min(min(x, min(z, w)), u), v), y)) ||
 rewrite(min(min(min(min(x, min(min(y, z), w)), u), v), z), min(min(min(min(x, min(y, w)), u), v), z)) ||
 rewrite(min(min(min(min((min(x, y) - z), w), u), x), y), min(w, min((min(x, y) - max(z, 0)), u))) ||
 rewrite(min(min(min(min((min(x, y) - z), w), u), y), x), min(w, min((min(x, y) - max(z, 0)), u))) ||
 rewrite(min(min(min(min(min(x, min(y, z)), w), u), v), y), min(min(min(min(min(x, z), w), u), v), y)) ||
 rewrite(min(min(min(min(min(x, min(y, z)), w), u), v), z), min(min(min(min(min(x, y), w), u), v), z)) ||
 rewrite(min(min(min(min(min(min(x, y), z), w), u), v), x), min(min(min(min(min(y, z), w), u), v), x)) ||
 rewrite(min(min(min(min(min(min(x, y), z), w), u), v), y), min(min(min(min(min(x, z), w), u), v), y)) ||
 rewrite(min(min(max(x, min(y, z)), w), y), min(min(max(x, z), w), y)) ||
 rewrite(min(min(max(x, min(y, z)), w), z), min(min(max(x, y), w), z)) ||
 rewrite(min(min(max(min(x, y), z), w), x), min(min(max(y, z), w), x)) ||
 rewrite(min(min(max(min(x, y), z), w), y), min(min(max(x, z), w), y)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(min(x, y), (x + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, y), (y + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, (y + z)), (w + (u + y))), min(x, (y + min(z, (w + u))))) ||
 rewrite(min(min(x, (y + z)), (w + (u + z))), min(x, (z + min(y, (w + u))))) ||
 rewrite(min(min(x, (y + z)), (w + (u + (v + y)))), min(x, (y + min(z, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + (u + (v + z)))), min(x, (z + min(y, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + (u + (y + v)))), min(x, (y + min(z, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + (u + (z + v)))), min(x, (z + min(y, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + (y + u))), min(x, (y + min(z, (w + u))))) ||
 rewrite(min(min(x, (y + z)), (w + (z + u))), min(x, (z + min(y, (w + u))))) ||
 rewrite(min(min(x, (y + z)), (w + ((u + y) + v))), min(x, (y + min(z, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + ((u + z) + v))), min(x, (z + min(y, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + ((y + u) + v))), min(x, (y + min(z, (w + (v + u)))))) ||
 rewrite(min(min(x, (y + z)), (w + ((z + u) + v))), min(x, (z + min(y, (w + (v + u)))))) ||
 rewrite(min(min(x, (y + z)), (w + (y - u))), min(x, (y + min(z, (w - u))))) ||
 rewrite(min(min(x, (y + z)), (w + (z - u))), min(x, (z + min(y, (w - u))))) ||
 rewrite(min(min(x, (y + z)), (w + ((u + y) - v))), min(x, (y + min(z, (w + (u - v)))))) ||
 rewrite(min(min(x, (y + z)), (w + ((u + z) - v))), min(x, (z + min(y, (w + (u - v)))))) ||
 rewrite(min(min(x, (y + z)), (w + ((y + u) - v))), min(x, (y + min(z, (w - (v - u)))))) ||
 rewrite(min(min(x, (y + z)), (w + ((z + u) - v))), min(x, (z + min(y, (w - (v - u)))))) ||
 rewrite(min(min(x, (y + z)), ((w + y) + u)), min(x, (y + min(z, (w + u))))) ||
 rewrite(min(min(x, (y + z)), ((w + z) + u)), min(x, (z + min(y, (w + u))))) ||
 rewrite(min(min(x, (y + z)), ((w + (u + y)) + v)), min(x, (y + min(z, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), ((w + (u + z)) + v)), min(x, (z + min(y, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), ((w + (y + u)) + v)), min(x, (y + min(z, (w + (v + u)))))) ||
 rewrite(min(min(x, (y + z)), ((w + (z + u)) + v)), min(x, (z + min(y, (w + (v + u)))))) ||
 rewrite(min(min(x, (y + z)), ((y + w) + u)), min(x, (y + min(z, (u + w))))) ||
 rewrite(min(min(x, (y + z)), ((z + w) + u)), min(x, (z + min(y, (u + w))))) ||
 rewrite(min(min(x, (y + z)), (((w + y) + u) + v)), min(x, (y + min(z, ((v + w) + u))))) ||
 rewrite(min(min(x, (y + z)), (((w + z) + u) + v)), min(x, (z + min(y, ((v + w) + u))))) ||
 rewrite(min(min(x, (y + z)), (((y + w) + u) + v)), min(x, (y + min(z, ((v + w) + u))))) ||
 rewrite(min(min(x, (y + z)), (((z + w) + u) + v)), min(x, (z + min(y, ((v + w) + u))))) ||
 rewrite(min(min(x, (y + z)), ((y - w) + u)), min(x, (y + min(z, (u - w))))) ||
 rewrite(min(min(x, (y + z)), ((z - w) + u)), min(x, (z + min(y, (u - w))))) ||
 rewrite(min(min(x, (y + z)), (((w + y) - u) + v)), min(x, (y + min(z, ((v + w) - u))))) ||
 rewrite(min(min(x, (y + z)), (((w + z) - u) + v)), min(x, (z + min(y, ((v + w) - u))))) ||
 rewrite(min(min(x, (y + z)), (((y + w) - u) + v)), min(x, (y + min(z, ((v + w) - u))))) ||
 rewrite(min(min(x, (y + z)), (((z + w) - u) + v)), min(x, (z + min(y, ((v + w) - u))))) ||
 rewrite(min(min(x, (y + (z + w))), (u + w)), min(x, (min((z + y), u) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + z)), min(x, (min((y + w), u) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (v + w))), min(x, (w + min((z + y), (u + v))))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (v + z))), min(x, (z + min((w + y), (u + v))))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (w + v))), min(x, (w + min((z + y), (u + v))))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (z + v))), min(x, (z + min((w + y), (u + v))))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (w - v))), min(x, (w + min((u - v), (z + y))))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (z - v))), min(x, (z + min((u - v), (w + y))))) ||
 rewrite(min(min(x, (y + (z + w))), (w + u)), min(x, (min((z + y), u) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (z + u)), min(x, (min((y + w), u) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + w) + v)), min(x, (min((z + y), (v + u)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + z) + v)), min(x, (z + min((w + y), (v + u))))) ||
 rewrite(min(min(x, (y + (z + w))), ((w + u) + v)), min(x, (w + min((z + y), (v + u))))) ||
 rewrite(min(min(x, (y + (z + w))), ((z + u) + v)), min(x, (z + min((w + y), (v + u))))) ||
 rewrite(min(min(x, (y + (z + w))), ((w - u) + v)), min(x, (w + min((v - u), (z + y))))) ||
 rewrite(min(min(x, (y + (z + w))), ((z - u) + v)), min(x, (z + min((v - u), (w + y))))) ||
 rewrite(min(min(x, (y + (z + (w + u)))), (u + v)), min(x, (min((z + (w + y)), v) + u))) ||
 rewrite(min(min(x, (y + (z + (w + u)))), (v + u)), min(x, (min((z + (w + y)), v) + u))) ||
 rewrite(min(min(x, (y + (z + (w + u)))), (v + w)), min(x, (min((z + (y + u)), v) + w))) ||
 rewrite(min(min(x, (y + (z + (w + u)))), (w + v)), min(x, (min((z + (y + u)), v) + w))) ||
 rewrite(min(min(x, (y + ((z + w) + u))), (v + w)), min(x, (min(((z + y) + u), v) + w))) ||
 rewrite(min(min(x, (y + ((z + w) + u))), (v + z)), min(x, (min(((y + w) + u), v) + z))) ||
 rewrite(min(min(x, (y + ((z + w) + u))), (w + v)), min(x, (min(((z + y) + u), v) + w))) ||
 rewrite(min(min(x, (y + ((z + w) + u))), (z + v)), min(x, (min(((y + w) + u), v) + z))) ||
 rewrite(min(min(x, (y + (z - w))), (u + z)), min(x, (min((y - w), u) + z))) ||
 rewrite(min(min(x, (y + (z - w))), (u + (v - w))), min(x, (min((z + y), (u + v)) - w))) ||
 rewrite(min(min(x, (y + (z - w))), (z + u)), min(x, (min((y - w), u) + z))) ||
 rewrite(min(min(x, (y + (z - w))), ((u - w) + v)), min(x, (min((z + y), (v + u)) - w))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + y)), min(x, (min(u, (z + w)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + z)), min(x, (min(u, (y + w)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (v + y))), min(x, (y + min((z + w), (u + v))))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (v + z))), min(x, (z + min((y + w), (u + v))))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (y + v))), min(x, (y + min((z + w), (u + v))))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (z + v))), min(x, (z + min((y + w), (u + v))))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (y - v))), min(x, (y + min((u - v), (z + w))))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (z - v))), min((min((u - v), (y + w)) + z), x)) ||
 rewrite(min(min(x, ((y + z) + w)), (y + u)), min(x, (min(u, (z + w)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (z + u)), min(x, (min(u, (y + w)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + y) + v)), min(x, (min((z + w), (v + u)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + z) + v)), min(x, (z + min((y + w), (v + u))))) ||
 rewrite(min(min(x, ((y + z) + w)), ((y + u) + v)), min(x, (y + min((z + w), (v + u))))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z + u) + v)), min(x, (z + min((y + w), (v + u))))) ||
 rewrite(min(min(x, ((y + z) + w)), ((y - u) + v)), min(x, (y + min((v - u), (z + w))))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z - u) + v)), min(x, (min((v - u), (y + w)) + z))) ||
 rewrite(min(min(x, ((y + (z + w)) + u)), (v + w)), min(x, (w + min((y + (z + u)), v)))) ||
 rewrite(min(min(x, ((y + (z + w)) + u)), (v + z)), min(x, (min(v, ((w + y) + u)) + z))) ||
 rewrite(min(min(x, ((y + (z + w)) + u)), (w + v)), min(x, (min(v, (z + (y + u))) + w))) ||
 rewrite(min(min(x, ((y + (z + w)) + u)), (z + v)), min(x, (min(v, ((w + y) + u)) + z))) ||
 rewrite(min(min(x, (((y + z) + w) + u)), (v + y)), min(x, (min(v, ((z + w) + u)) + y))) ||
 rewrite(min(min(x, (((y + z) + w) + u)), (v + z)), min(x, (min((y + (w + u)), v) + z))) ||
 rewrite(min(min(x, (((y + z) + w) + u)), (y + v)), min(x, (min(v, ((z + w) + u)) + y))) ||
 rewrite(min(min(x, (((y + z) + w) + u)), (z + v)), min(x, (min(v, (y + (u + w))) + z))) ||
 rewrite(min(min(x, ((y - z) + w)), (u + y)), min(x, (y + min((w - z), u)))) ||
 rewrite(min(min(x, ((y - z) + w)), (u + (v - z))), min(x, (min((y + w), (u + v)) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), (y + u)), min(x, (min(u, (w - z)) + y))) ||
 rewrite(min(min(x, ((y - z) + w)), ((u - z) + v)), min(x, (min((w + y), (v + u)) - z))) ||
 rewrite(min(min(x, (y - z)), (w + (u + (v - z)))), min(x, (min(y, (w + (u + v))) - z))) ||
 rewrite(min(min(x, (y - z)), (w + ((u - z) + v))), min(x, (min(y, (w + (u + v))) - z))) ||
 rewrite(min(min(x, (y - z)), (w + (u - z))), min(x, (min(y, (w + u)) - z))) ||
 rewrite(min(min(x, (y - z)), (w + (u - (v + z)))), min(x, (min(y, (w + (u - v))) - z))) ||
 rewrite(min(min(x, (y - z)), (w + (u - (z + v)))), min(x, (min(y, (w + (u - v))) - z))) ||
 rewrite(min(min(x, (y - z)), ((w + (u - z)) + v)), min(x, (min(y, (v + (w + u))) - z))) ||
 rewrite(min(min(x, (y - z)), (((w - z) + u) + v)), min(x, (min(y, (u + (w + v))) - z))) ||
 rewrite(min(min(x, (y - z)), ((w - z) + u)), min(x, (min(y, (u + w)) - z))) ||
 rewrite(min(min(x, (y - z)), ((w - (u + z)) + v)), min(x, (min(y, (v + (w - u))) - z))) ||
 rewrite(min(min(x, (y - z)), ((w - (z + u)) + v)), min(x, (min(y, ((w + v) - u)) - z))) ||
 rewrite(min(min(x, (y - (z + w))), (u + (v - w))), min(x, (min((y - z), (u + v)) - w))) ||
 rewrite(min(min(x, (y - (z + w))), (u + (v - z))), min(x, (min((y - w), (u + v)) - z))) ||
 rewrite(min(min(x, (y - (z + w))), ((u - w) + v)), min(x, (min((y - z), (v + u)) - w))) ||
 rewrite(min(min(x, (y - (z + w))), ((u - z) + v)), min(x, (min((y - w), (v + u)) - z))) ||
 rewrite(min(min(x, ((y + z) - w)), (y + z)), min(x, ((z - max(w, 0)) + y))) ||
 rewrite(min(min(x, ((y + z) - w)), (z + y)), min(x, ((z - max(w, 0)) + y))) ||
 rewrite(min(min(x, min(y, z)), (y + c0)), min(x, min(z, y)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (z + c0)), min(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + w)), min(min(x, (w + min(z, u))), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + z)), min(x, min(y, (z + min(w, u))))) ||
 rewrite(min(min(x, min(y, (z + w))), (u + (w + z))), min(x, min(y, (z + (min(u, 0) + w))))) ||
 rewrite(min(min(x, min(y, (z + w))), (u + (z + w))), min(x, min(y, (w + (min(u, 0) + z))))) ||
 rewrite(min(min(x, min(y, (z + w))), (w + u)), min(min(x, (w + min(z, u))), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (z + u)), min(x, min(y, (z + min(w, u))))) ||
 rewrite(min(min(x, min(y, (z + w))), ((w + z) + u)), min(x, min(y, (min(u, 0) + (w + z))))) ||
 rewrite(min(min(x, min(y, (z + w))), ((z + w) + u)), min(x, min(y, (min(u, 0) + (z + w))))) ||
 rewrite(min(min(x, min(y, (z + (w + u)))), (u + w)), min(x, min(y, (u + (w + min(z, 0)))))) ||
 rewrite(min(min(x, min(y, (z + (w + u)))), (w + u)), min(x, min(y, (u + (w + min(z, 0)))))) ||
 rewrite(min(min(x, min(y, ((z + w) + u))), (w + z)), min(x, min(y, ((z + min(u, 0)) + w)))) ||
 rewrite(min(min(x, min(y, ((z + w) + u))), (z + w)), min(x, min(y, ((w + min(u, 0)) + z)))) ||
 rewrite(min(min(x, min(y, (z - w))), (u + (v - w))), min(x, min(y, (min(z, (u + v)) - w)))) ||
 rewrite(min(min(x, min(y, (z - w))), ((u - w) + v)), min(x, min((min(z, (v + u)) - w), y))) ||
 rewrite(min(min(x, min(y, (z*w))), (u + (w*z))), min(min(x, y), (min(u, 0) + (w*z)))) ||
 rewrite(min(min(x, min(y, (z*w))), (u + (z*w))), min(min(x, y), (min(u, 0) + (z*w)))) ||
 rewrite(min(min(x, min(y, (z*w))), ((w*z) + u)), min(x, min(y, (min(u, 0) + (w*z))))) ||
 rewrite(min(min(x, min(y, (z*w))), ((z*w) + u)), min(x, min(y, (min(u, 0) + (z*w))))) ||
 rewrite(min(min(x, min(y, min(z, w))), (u + min(w, y))), min(min(x, z), (min(u, 0) + min(w, y)))) ||
 rewrite(min(min(x, min(y, min(z, w))), (u + min(w, z))), min(x, min(y, (min(z, w) + min(u, 0))))) ||
 rewrite(min(min(x, min(y, min(z, w))), (u + min(y, w))), min(min(x, z), (min(u, 0) + min(y, w)))) ||
 rewrite(min(min(x, min(y, min(z, w))), (u + min(y, z))), min(min(x, w), (min(u, 0) + min(y, z)))) ||
 rewrite(min(min(x, min(y, min(z, w))), (u + min(z, w))), min(x, min(y, (min(z, w) + min(u, 0))))) ||
 rewrite(min(min(x, min(y, min(z, w))), (u + min(z, y))), min(min(x, w), (min(u, 0) + min(z, y)))) ||
 rewrite(min(min(x, min(y, min(z, w))), (w + c0)), min(x, min(y, min(z, w))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (z + c0)), min(x, min(y, min(z, w))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(w, y) + u)), min(x, min(z, (min(u, 0) + min(w, y))))) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(w, z) + u)), min(x, min(y, (min(u, 0) + min(w, z))))) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(y, w) + u)), min(x, min(z, (min(u, 0) + min(y, w))))) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(y, z) + u)), min(x, min(w, (min(u, 0) + min(y, z))))) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(z, w) + u)), min(x, min(y, (min(u, 0) + min(z, w))))) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(z, y) + u)), min(x, min(w, (min(u, 0) + min(z, y))))) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (u + v)), min(min(x, min(y, (u + min(w, v)))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + u)), min(min(x, min(y, (u + min(w, v)))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + w)), min(x, min(y, min(z, (w + min(u, v)))))) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (w + v)), min(x, min(y, min(z, (w + min(u, v)))))) ||
 rewrite(min(min(x, min(y, min(z, min(w, u)))), (u + c0)), min(x, min(y, min(z, min(w, u)))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, min(w, u)))), (w + c0)), min(x, min(y, min(z, min(w, u)))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + w)), min(min(x, min(y, (min(z, v) + w))), u)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + z)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (w + v)), min(min(x, min(y, (min(z, v) + w))), u)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (z + v)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min(y, min(min(z, w), u))), (w + c0)), min(x, min(y, min(min(z, w), u))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(min(z, w), u))), (z + c0)), min(x, min(y, min(min(z, w), u))), (0 <= c0)) ||
 rewrite(min(min(x, min((y + z), w)), (u + y)), min(min(x, (min(u, z) + y)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (u + z)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (u + (y + z))), min(min(x, w), (min(u, 0) + (y + z)))) ||
 rewrite(min(min(x, min((y + z), w)), (u + (z + y))), min(min(x, w), (min(u, 0) + (z + y)))) ||
 rewrite(min(min(x, min((y + z), w)), (y + u)), min(min(x, (min(u, z) + y)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (z + u)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y + z), w)), ((y + z) + u)), min(x, min(((z + min(u, 0)) + y), w))) ||
 rewrite(min(min(x, min((y + z), w)), ((z + y) + u)), min(x, min(((z + min(u, 0)) + y), w))) ||
 rewrite(min(min(x, min((y + (z + w)), u)), (w + z)), min(min(x, (min(y, 0) + (z + w))), u)) ||
 rewrite(min(min(x, min((y + (z + w)), u)), (z + w)), min(min(x, (min(y, 0) + (z + w))), u)) ||
 rewrite(min(min(x, min(((y + z) + w), u)), (y + z)), min(x, min((y + (z + min(w, 0))), u))) ||
 rewrite(min(min(x, min(((y + z) + w), u)), (z + y)), min(x, min((z + (y + min(w, 0))), u))) ||
 rewrite(min(min(x, min((y - z), w)), (u + (v - z))), min(w, min(x, (min(y, (u + v)) - z)))) ||
 rewrite(min(min(x, min((y - z), w)), ((u - z) + v)), min(w, min(x, (min(y, (v + u)) - z)))) ||
 rewrite(min(min(x, min((y*z), w)), (u + (y*z))), min(min(x, w), (min(u, 0) + (y*z)))) ||
 rewrite(min(min(x, min((y*z), w)), (u + (z*y))), min(min(x, w), (min(u, 0) + (z*y)))) ||
 rewrite(min(min(x, min((y*z), w)), ((y*z) + u)), min(w, min(x, (min(u, 0) + (y*z))))) ||
 rewrite(min(min(x, min((y*z), w)), ((z*y) + u)), min(w, min(x, (min(u, 0) + (z*y))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (u + min(w, y))), min(z, min(x, (min(y, w) + min(u, 0))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (u + min(w, z))), min(y, min(x, (min(z, w) + min(u, 0))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (u + min(y, w))), min(z, min(x, (min(y, w) + min(u, 0))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (u + min(y, z))), min(w, min(x, (min(y, z) + min(u, 0))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (u + min(z, w))), min(y, min(x, (min(z, w) + min(u, 0))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (u + min(z, y))), min(w, min(x, (min(y, z) + min(u, 0))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (y + c0)), min(x, min(min(y, z), w)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (z + c0)), min(x, min(min(y, z), w)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(w, y) + u)), min(x, min(z, (min(u, 0) + min(w, y))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(w, z) + u)), min(x, min((min(z, w) + min(u, 0)), y))) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(y, w) + u)), min(x, min(z, (min(u, 0) + min(y, w))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(y, z) + u)), min(w, min(x, (min(u, 0) + min(y, z))))) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(z, w) + u)), min(x, min((min(z, w) + min(u, 0)), y))) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(z, y) + u)), min(w, min(x, (min(u, 0) + min(z, y))))) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + w)), min(min(x, min((w + min(z, v)), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + z)), min(min(x, min((z + min(v, w)), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (w + v)), min(min(x, min((w + min(z, v)), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (z + v)), min(min(x, min((z + min(v, w)), u)), y)) ||
 rewrite(min(min(x, min(min(y, min(z, w)), u)), (w + c0)), min(x, min(min(y, min(z, u)), w)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, min(z, w)), u)), (z + c0)), min(x, min(min(y, min(w, u)), z)), (0 <= c0)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + y)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + z)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (y + v)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (z + v)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min(x, min(min(min(y, z), w), u)), (y + c0)), min(x, min(min(min(y, z), w), u)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(min(y, z), w), u)), (z + c0)), min(x, min(min(min(y, w), u), z)), (0 <= c0)) ||
 rewrite(min(min((x + y), z), (w + (u + x))), min(z, (x + min(y, (w + u))))) ||
 rewrite(min(min((x + y), z), (w + (u + y))), min(z, (y + min(x, (w + u))))) ||
 rewrite(min(min((x + y), z), (w + (u + (v + x)))), min(z, (x + min(y, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + (u + (v + y)))), min(z, (y + min(x, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + (u + (x + v)))), min(z, (x + min(y, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + (u + (y + v)))), min(z, (y + min(x, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + (x + u))), min(z, (x + min(y, (w + u))))) ||
 rewrite(min(min((x + y), z), (w + (y + u))), min(z, (y + min(x, (w + u))))) ||
 rewrite(min(min((x + y), z), (w + ((u + x) + v))), min(z, (x + min(y, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + ((u + y) + v))), min(z, (y + min(x, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + ((x + u) + v))), min(z, (x + min(y, (w + (v + u)))))) ||
 rewrite(min(min((x + y), z), (w + ((y + u) + v))), min(z, (y + min(x, (w + (v + u)))))) ||
 rewrite(min(min((x + y), z), (w + (x - u))), min(z, (x + min(y, (w - u))))) ||
 rewrite(min(min((x + y), z), (w + (y - u))), min(z, (y + min(x, (w - u))))) ||
 rewrite(min(min((x + y), z), (w + ((u + x) - v))), min(z, (x + min(y, (w + (u - v)))))) ||
 rewrite(min(min((x + y), z), (w + ((u + y) - v))), min(z, (y + min(x, (w + (u - v)))))) ||
 rewrite(min(min((x + y), z), (w + ((x + u) - v))), min(z, (x + min(y, (w - (v - u)))))) ||
 rewrite(min(min((x + y), z), (w + ((y + u) - v))), min(z, (y + min(x, (w - (v - u)))))) ||
 rewrite(min(min((x + y), z), ((w + x) + u)), min(z, (x + min(y, (w + u))))) ||
 rewrite(min(min((x + y), z), ((w + y) + u)), min(z, (y + min(x, (w + u))))) ||
 rewrite(min(min((x + y), z), ((w + (u + x)) + v)), min(z, (x + min(y, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), ((w + (u + y)) + v)), min(z, (y + min(x, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), ((w + (x + u)) + v)), min(z, (x + min(y, (w + (v + u)))))) ||
 rewrite(min(min((x + y), z), ((w + (y + u)) + v)), min(z, (y + min(x, (w + (v + u)))))) ||
 rewrite(min(min((x + y), z), ((x + w) + u)), min(z, (x + min(y, (u + w))))) ||
 rewrite(min(min((x + y), z), ((y + w) + u)), min(z, (y + min(x, (u + w))))) ||
 rewrite(min(min((x + y), z), (((w + x) + u) + v)), min(z, (x + min(y, ((v + w) + u))))) ||
 rewrite(min(min((x + y), z), (((w + y) + u) + v)), min(z, (y + min(x, ((v + w) + u))))) ||
 rewrite(min(min((x + y), z), (((x + w) + u) + v)), min(z, (x + min(y, ((v + w) + u))))) ||
 rewrite(min(min((x + y), z), (((y + w) + u) + v)), min(z, (y + min(x, ((v + w) + u))))) ||
 rewrite(min(min((x + y), z), ((x - w) + u)), min(z, (x + min(y, (u - w))))) ||
 rewrite(min(min((x + y), z), ((y - w) + u)), min(z, (y + min(x, (u - w))))) ||
 rewrite(min(min((x + y), z), (((w + x) - u) + v)), min(z, (x + min(y, ((v + w) - u))))) ||
 rewrite(min(min((x + y), z), (((w + y) - u) + v)), min(z, (y + min(x, ((v + w) - u))))) ||
 rewrite(min(min((x + y), z), (((x + w) - u) + v)), min(z, (x + min(y, ((v + w) - u))))) ||
 rewrite(min(min((x + y), z), (((y + w) - u) + v)), min(z, (y + min(x, ((v + w) - u))))) ||
 rewrite(min(min((x + (y + z)), w), (u + y)), min(w, (min(u, (x + z)) + y))) ||
 rewrite(min(min((x + (y + z)), w), (u + z)), min(w, (min(u, (y + x)) + z))) ||
 rewrite(min(min((x + (y + z)), w), (u + (v + y))), min(w, (y + min((z + x), (u + v))))) ||
 rewrite(min(min((x + (y + z)), w), (u + (v + z))), min(w, (z + min((y + x), (u + v))))) ||
 rewrite(min(min((x + (y + z)), w), (u + (y + v))), min(w, (y + min((z + x), (u + v))))) ||
 rewrite(min(min((x + (y + z)), w), (u + (z + v))), min(w, (z + min((y + x), (u + v))))) ||
 rewrite(min(min((x + (y + z)), w), (u + (y - v))), min(w, (y + min((u - v), (z + x))))) ||
 rewrite(min(min((x + (y + z)), w), (u + (z - v))), min(w, (z + min((u - v), (y + x))))) ||
 rewrite(min(min((x + (y + z)), w), (y + u)), min(w, (min(u, (x + z)) + y))) ||
 rewrite(min(min((x + (y + z)), w), (z + u)), min(w, (min(u, (y + x)) + z))) ||
 rewrite(min(min((x + (y + z)), w), ((u + y) + v)), min(w, (y + min((z + x), (v + u))))) ||
 rewrite(min(min((x + (y + z)), w), ((u + z) + v)), min(w, (min((y + x), (v + u)) + z))) ||
 rewrite(min(min((x + (y + z)), w), ((y + u) + v)), min(w, (y + min((z + x), (v + u))))) ||
 rewrite(min(min((x + (y + z)), w), ((z + u) + v)), min(w, (z + min((y + x), (v + u))))) ||
 rewrite(min(min((x + (y + z)), w), ((y - u) + v)), min(w, (y + min((v - u), (z + x))))) ||
 rewrite(min(min((x + (y + z)), w), ((z - u) + v)), min(w, (z + min((v - u), (y + x))))) ||
 rewrite(min(min((x + (y + (z + w))), u), (v + w)), min(u, (min(v, (y + (z + x))) + w))) ||
 rewrite(min(min((x + (y + (z + w))), u), (v + z)), min(u, (min(v, (y + (x + w))) + z))) ||
 rewrite(min(min((x + (y + (z + w))), u), (w + v)), min(u, (min(v, (y + (z + x))) + w))) ||
 rewrite(min(min((x + (y + (z + w))), u), (z + v)), min(u, (min(v, (y + (x + w))) + z))) ||
 rewrite(min(min((x + ((y + z) + w)), u), (v + y)), min(u, (min(v, ((x + z) + w)) + y))) ||
 rewrite(min(min((x + ((y + z) + w)), u), (v + z)), min(u, (min(v, ((y + x) + w)) + z))) ||
 rewrite(min(min((x + ((y + z) + w)), u), (y + v)), min(u, (min(v, ((x + z) + w)) + y))) ||
 rewrite(min(min((x + ((y + z) + w)), u), (z + v)), min(u, (min(v, ((y + x) + w)) + z))) ||
 rewrite(min(min((x + (y - z)), w), (u + y)), min(w, (min(u, (x - z)) + y))) ||
 rewrite(min(min((x + (y - z)), w), (u + (v - z))), min(w, (min((y + x), (u + v)) - z))) ||
 rewrite(min(min((x + (y - z)), w), (y + u)), min(w, (min(u, (x - z)) + y))) ||
 rewrite(min(min((x + (y - z)), w), ((u - z) + v)), min(w, (min((y + x), (v + u)) - z))) ||
 rewrite(min(min(((x + y) + z), w), (u + x)), min(w, (min((y + z), u) + x))) ||
 rewrite(min(min(((x + y) + z), w), (u + y)), min(w, (min((x + z), u) + y))) ||
 rewrite(min(min(((x + y) + z), w), (u + (v + x))), min(w, (x + min((y + z), (u + v))))) ||
 rewrite(min(min(((x + y) + z), w), (u + (v + y))), min(w, (y + min((x + z), (u + v))))) ||
 rewrite(min(min(((x + y) + z), w), (u + (x + v))), min(w, (x + min((y + z), (u + v))))) ||
 rewrite(min(min(((x + y) + z), w), (u + (y + v))), min(w, (y + min((x + z), (u + v))))) ||
 rewrite(min(min(((x + y) + z), w), (u + (x - v))), min((min((u - v), (y + z)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (y - v))), min((min((u - v), (x + z)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), (x + u)), min(w, (min((y + z), u) + x))) ||
 rewrite(min(min(((x + y) + z), w), (y + u)), min(w, (min((x + z), u) + y))) ||
 rewrite(min(min(((x + y) + z), w), ((u + x) + v)), min(w, (min((y + z), (v + u)) + x))) ||
 rewrite(min(min(((x + y) + z), w), ((u + y) + v)), min(w, (min((x + z), (v + u)) + y))) ||
 rewrite(min(min(((x + y) + z), w), ((x + u) + v)), min(w, (x + min((y + z), (v + u))))) ||
 rewrite(min(min(((x + y) + z), w), ((y + u) + v)), min(w, (y + min((x + z), (v + u))))) ||
 rewrite(min(min(((x + y) + z), w), ((x - u) + v)), min((min((v - u), (y + z)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), ((y - u) + v)), min((min((v - u), (x + z)) + y), w)) ||
 rewrite(min(min(((x + (y + z)) + w), u), (v + y)), min(u, (min(v, ((z + x) + w)) + y))) ||
 rewrite(min(min(((x + (y + z)) + w), u), (v + z)), min(u, (min((x + (y + w)), v) + z))) ||
 rewrite(min(min(((x + (y + z)) + w), u), (y + v)), min(u, (min(v, ((z + x) + w)) + y))) ||
 rewrite(min(min(((x + (y + z)) + w), u), (z + v)), min(u, (min((x + (y + w)), v) + z))) ||
 rewrite(min(min((((x + y) + z) + w), u), (v + x)), min(u, (min(((y + z) + w), v) + x))) ||
 rewrite(min(min((((x + y) + z) + w), u), (v + y)), min(u, (min(((x + z) + w), v) + y))) ||
 rewrite(min(min((((x + y) + z) + w), u), (x + v)), min(u, (min(((y + z) + w), v) + x))) ||
 rewrite(min(min((((x + y) + z) + w), u), (y + v)), min(u, (min(((x + z) + w), v) + y))) ||
 rewrite(min(min(((x - y) + z), w), (u + x)), min(w, (min((z - y), u) + x))) ||
 rewrite(min(min(((x - y) + z), w), (u + (v - y))), min(w, (min((x + z), (u + v)) - y))) ||
 rewrite(min(min(((x - y) + z), w), (x + u)), min(w, (min((z - y), u) + x))) ||
 rewrite(min(min(((x - y) + z), w), ((u - y) + v)), min(w, (min((x + z), (v + u)) - y))) ||
 rewrite(min(min((x - y), z), (w + (u + (v - y)))), min(z, (min(x, (w + (u + v))) - y))) ||
 rewrite(min(min((x - y), z), (w + ((u - y) + v))), min(z, (min(x, (w + (u + v))) - y))) ||
 rewrite(min(min((x - y), z), (w + (u - y))), min(z, (min(x, (w + u)) - y))) ||
 rewrite(min(min((x - y), z), (w + (u - (v + y)))), min(z, (min(x, (w + (u - v))) - y))) ||
 rewrite(min(min((x - y), z), (w + (u - (y + v)))), min(z, (min(x, (w + (u - v))) - y))) ||
 rewrite(min(min((x - y), z), ((w + (u - y)) + v)), min(z, (min(x, (w + (u + v))) - y))) ||
 rewrite(min(min((x - y), z), (((w - y) + u) + v)), min(z, (min(x, (u + (w + v))) - y))) ||
 rewrite(min(min((x - y), z), ((w - y) + u)), min(z, (min(x, (u + w)) - y))) ||
 rewrite(min(min((x - y), z), ((w - (u + y)) + v)), min(z, (min(x, (v + (w - u))) - y))) ||
 rewrite(min(min((x - y), z), ((w - (y + u)) + v)), min(z, (min(x, ((w + v) - u)) - y))) ||
 rewrite(min(min((x - (y + z)), w), (u + (v - y))), min(w, (min((x - z), (u + v)) - y))) ||
 rewrite(min(min((x - (y + z)), w), (u + (v - z))), min(w, (min((x - y), (u + v)) - z))) ||
 rewrite(min(min((x - (y + z)), w), ((u - y) + v)), min(w, (min((x - z), (v + u)) - y))) ||
 rewrite(min(min((x - (y + z)), w), ((u - z) + v)), min(w, (min((x - y), (v + u)) - z))) ||
 rewrite(min(min(((x + y) - z), w), (x + y)), min(w, ((y - max(z, 0)) + x))) ||
 rewrite(min(min(((x + y) - z), w), (y + x)), min(w, ((y - max(z, 0)) + x))) ||
 rewrite(min(min(min(x, y), z), (x + c0)), min(min(y, z), x), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (y + c0)), min(min(x, z), y), (0 <= c0)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + y)), min(min((y + min(u, z)), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + z)), min(min((z + min(y, u)), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + (y + z))), min(w, min(x, (z + (min(u, 0) + y))))) ||
 rewrite(min(min(min(x, (y + z)), w), (u + (z + y))), min(w, min(x, (y + (min(u, 0) + z))))) ||
 rewrite(min(min(min(x, (y + z)), w), (y + u)), min(min((y + min(u, z)), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (z + u)), min(min((z + min(y, u)), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), ((y + z) + u)), min(w, min(x, (min(u, 0) + (y + z))))) ||
 rewrite(min(min(min(x, (y + z)), w), ((z + y) + u)), min(w, min(x, (min(u, 0) + (z + y))))) ||
 rewrite(min(min(min(x, (y + (z + w))), u), (w + z)), min(min((min(y, 0) + (z + w)), u), x)) ||
 rewrite(min(min(min(x, (y + (z + w))), u), (z + w)), min(min((min(y, 0) + (z + w)), u), x)) ||
 rewrite(min(min(min(x, ((y + z) + w)), u), (y + z)), min(u, min(x, ((y + min(w, 0)) + z)))) ||
 rewrite(min(min(min(x, ((y + z) + w)), u), (z + y)), min(u, min(x, ((z + min(w, 0)) + y)))) ||
 rewrite(min(min(min(x, (y - z)), w), (u + (v - z))), min(w, min(x, (min(y, (u + v)) - z)))) ||
 rewrite(min(min(min(x, (y - z)), w), ((u - z) + v)), min(w, min((min(y, (v + u)) - z), x))) ||
 rewrite(min(min(min(x, (y*z)), w), (u + (y*z))), min(min(x, w), (min(u, 0) + (y*z)))) ||
 rewrite(min(min(min(x, (y*z)), w), (u + (z*y))), min(min(x, w), (min(u, 0) + (z*y)))) ||
 rewrite(min(min(min(x, (y*z)), w), ((y*z) + u)), min(w, min(x, (min(u, 0) + (y*z))))) ||
 rewrite(min(min(min(x, (y*z)), w), ((z*y) + u)), min(w, min(x, (min(u, 0) + (z*y))))) ||
 rewrite(min(min(min(x, min(y, z)), w), (u + min(x, y))), min(min(z, w), (min(u, 0) + min(x, y)))) ||
 rewrite(min(min(min(x, min(y, z)), w), (u + min(x, z))), min(min(y, w), (min(u, 0) + min(x, z)))) ||
 rewrite(min(min(min(x, min(y, z)), w), (u + min(y, x))), min(min(z, w), (min(u, 0) + min(y, x)))) ||
 rewrite(min(min(min(x, min(y, z)), w), (u + min(y, z))), min(w, min(x, (min(z, y) + min(u, 0))))) ||
 rewrite(min(min(min(x, min(y, z)), w), (u + min(z, x))), min(min(y, w), (min(u, 0) + min(z, x)))) ||
 rewrite(min(min(min(x, min(y, z)), w), (u + min(z, y))), min(w, min(x, (min(z, y) + min(u, 0))))) ||
 rewrite(min(min(min(x, min(y, z)), w), (y + c0)), min(min(min(x, z), w), y), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (z + c0)), min(min(min(x, y), w), z), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(x, y) + u)), min(min(z, w), (min(x, y) + min(u, 0)))) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(x, z) + u)), min(min(y, w), (min(x, z) + min(u, 0)))) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(y, x) + u)), min(min(z, w), (min(y, x) + min(u, 0)))) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(y, z) + u)), min(w, min(x, (min(u, 0) + min(y, z))))) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(z, x) + u)), min(min(y, w), (min(z, x) + min(u, 0)))) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(z, y) + u)), min(w, min(x, (min(u, 0) + min(z, y))))) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + w)), min(min(min(x, (w + min(z, v))), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + z)), min(min(min(x, (z + min(v, w))), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (w + v)), min(min(min(x, (w + min(z, v))), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (z + v)), min(min(min(x, (z + min(v, w))), u), y)) ||
 rewrite(min(min(min(x, min(y, min(z, w))), u), (w + c0)), min(min(min(x, min(y, z)), u), w), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, min(z, w))), u), (z + c0)), min(min(min(x, min(y, w)), u), z), (0 <= c0)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + y)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + z)), min(min(min(x, (min(y, v) + z)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (y + v)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (z + v)), min(min(min(x, (min(y, v) + z)), u), w)) ||
 rewrite(min(min(min(x, min(min(y, z), w)), u), (y + c0)), min(min(min(x, min(z, w)), u), y), (0 <= c0)) ||
 rewrite(min(min(min(x, min(min(y, z), w)), u), (z + c0)), min(min(min(x, min(y, w)), u), z), (0 <= c0)) ||
 rewrite(min(min(min((x + y), z), w), (u + x)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (u + y)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (u + (x + y))), min(min(z, w), (min(u, 0) + (x + y)))) ||
 rewrite(min(min(min((x + y), z), w), (u + (y + x))), min(min(z, w), (min(u, 0) + (y + x)))) ||
 rewrite(min(min(min((x + y), z), w), (x + u)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (y + u)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x + y), z), w), ((x + y) + u)), min(w, min(((y + min(u, 0)) + x), z))) ||
 rewrite(min(min(min((x + y), z), w), ((y + x) + u)), min(w, min(((x + min(u, 0)) + y), z))) ||
 rewrite(min(min(min((x + (y + z)), w), u), (y + z)), min(min((min(x, 0) + (y + z)), w), u)) ||
 rewrite(min(min(min((x + (y + z)), w), u), (z + y)), min(min((min(x, 0) + (y + z)), w), u)) ||
 rewrite(min(min(min(((x + y) + z), w), u), (x + y)), min(u, min(((x + min(z, 0)) + y), w))) ||
 rewrite(min(min(min(((x + y) + z), w), u), (y + x)), min(u, min(((y + min(z, 0)) + x), w))) ||
 rewrite(min(min(min((x - y), z), w), (u + (v - y))), min(w, min((min(x, (u + v)) - y), z))) ||
 rewrite(min(min(min((x - y), z), w), ((u - y) + v)), min(w, min((min(x, (v + u)) - y), z))) ||
 rewrite(min(min(min((x*y), z), w), (u + (x*y))), min(min(z, w), (min(u, 0) + (x*y)))) ||
 rewrite(min(min(min((x*y), z), w), (u + (y*x))), min(min(z, w), (min(u, 0) + (y*x)))) ||
 rewrite(min(min(min((x*y), z), w), ((x*y) + u)), min(min(z, w), (min(u, 0) + (x*y)))) ||
 rewrite(min(min(min((x*y), z), w), ((y*x) + u)), min(min(z, w), (min(u, 0) + (y*x)))) ||
 rewrite(min(min(min(min(x, y), z), w), (u + min(x, y))), min(min(z, w), (min(u, 0) + min(x, y)))) ||
 rewrite(min(min(min(min(x, y), z), w), (u + min(x, z))), min(min(y, w), (min(u, 0) + min(x, z)))) ||
 rewrite(min(min(min(min(x, y), z), w), (u + min(y, x))), min(min(z, w), (min(u, 0) + min(y, x)))) ||
 rewrite(min(min(min(min(x, y), z), w), (u + min(y, z))), min(min(x, w), (min(u, 0) + min(y, z)))) ||
 rewrite(min(min(min(min(x, y), z), w), (u + min(z, x))), min(min(y, w), (min(u, 0) + min(z, x)))) ||
 rewrite(min(min(min(min(x, y), z), w), (u + min(z, y))), min(min(x, w), (min(u, 0) + min(z, y)))) ||
 rewrite(min(min(min(min(x, y), z), w), (x + c0)), min(min(min(y, z), w), x), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (y + c0)), min(min(min(x, z), w), y), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(x, y) + u)), min(min(z, w), (min(x, y) + min(u, 0)))) ||
 rewrite(min(min(min(min(x, y), z), w), (min(x, z) + u)), min(min(y, w), (min(x, z) + min(u, 0)))) ||
 rewrite(min(min(min(min(x, y), z), w), (min(y, x) + u)), min(min(z, w), (min(y, x) + min(u, 0)))) ||
 rewrite(min(min(min(min(x, y), z), w), (min(y, z) + u)), min(min(x, w), (min(u, 0) + min(y, z)))) ||
 rewrite(min(min(min(min(x, y), z), w), (min(z, x) + u)), min(min(y, w), (min(z, x) + min(u, 0)))) ||
 rewrite(min(min(min(min(x, y), z), w), (min(z, y) + u)), min(min(x, w), (min(u, 0) + min(z, y)))) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + y)), min(min(min((y + min(v, z)), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + z)), min(min(min((z + min(y, v)), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (y + v)), min(min(min((y + min(v, z)), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (z + v)), min(min(min((z + min(y, v)), w), u), x)) ||
 rewrite(min(min(min(min(x, min(y, z)), w), u), (y + c0)), min(min(min(min(x, z), w), u), y), (0 <= c0)) ||
 rewrite(min(min(min(min(x, min(y, z)), w), u), (z + c0)), min(min(min(min(x, y), w), u), z), (0 <= c0)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + x)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + y)), min(min(min((min(x, v) + y), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (x + v)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (y + v)), min(min(min((min(x, v) + y), w), u), z)) ||
 rewrite(min(min(min(min(min(x, y), z), w), u), (x + c0)), min(min(min(min(y, z), w), u), x), (0 <= c0)) ||
 rewrite(min(min(min(min(min(x, y), z), w), u), (y + c0)), min(min(min(min(x, z), w), u), y), (0 <= c0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(min(x, (y + z)), (((w + y) - u) - v)), min(x, (y + min(z, ((w - v) - u))))) ||
 rewrite(min(min(x, (y + z)), (((w + z) - u) - v)), min(x, (z + min(y, ((w - v) - u))))) ||
 rewrite(min(min(x, (y + z)), (((y + w) - u) - v)), min(x, (y + min(z, ((w - v) - u))))) ||
 rewrite(min(min(x, (y + z)), (((z + w) - u) - v)), min(x, (z + min(y, ((w - v) - u))))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + w) - v)), min(x, (w + min((u - v), (z + y))))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + z) - v)), min(x, (z + min((u - v), (w + y))))) ||
 rewrite(min(min(x, (y + (z + w))), ((w + u) - v)), min(x, (w + min((u - v), (z + y))))) ||
 rewrite(min(min(x, (y + (z + w))), ((z + u) - v)), min(x, (z + min((u - v), (w + y))))) ||
 rewrite(min(min(x, (y + (z - w))), (u - w)), min(x, (min((z + y), u) - w))) ||
 rewrite(min(min(x, (y + (z - w))), (y - (w + z))), min(x, (min((y - z), (y + z)) - w))) ||
 rewrite(min(min(x, (y + (z - w))), (y - (z + w))), min(x, (min((y - z), (y + z)) - w))) ||
 rewrite(min(min(x, (y + (z - w))), ((z - w) - u)), min(x, (min((z - u), (y + z)) - w))) ||
 rewrite(min(min(x, (y + (z - (w + u)))), (z - v)), min(x, (z - max(v, (w + (u - y)))))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + y) - v)), min(x, (y + min((u - v), (z + w))))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + z) - v)), min(x, (min((u - v), (y + w)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((y + u) - v)), min(x, (y + min((u - v), (z + w))))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z + u) - v)), min(x, (min((u - v), (y + w)) + z))) ||
 rewrite(min(min(x, ((y - z) + w)), (u - z)), min(x, (min(u, (y + w)) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), (w - (y + z))), min(x, (min((w - y), (w + y)) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), (w - (z + y))), min(x, (min((w - y), (w + y)) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), ((y - z) - u)), min(x, (min((y - u), (w + y)) - z))) ||
 rewrite(min(min(x, ((y - (z + w)) + u)), (y - v)), min(x, (y - max(v, ((w + z) - u))))) ||
 rewrite(min(min(x, (y - z)), (w - z)), min(x, (min(y, w) - z))) ||
 rewrite(min(min(x, (y - z)), (y - w)), min(x, (y - max(z, w)))) ||
 rewrite(min(min(x, (y - z)), (((w + y) - u) - v)), min(x, (y - max(z, ((v - w) + u))))) ||
 rewrite(min(min(x, (y - z)), (((y + w) - u) - v)), min(x, (y - max(z, ((v - w) + u))))) ||
 rewrite(min(min(x, (y - (z + w))), (u - w)), min(x, (min((y - z), u) - w))) ||
 rewrite(min(min(x, (y - (z + w))), (u - z)), min(x, (min((y - w), u) - z))) ||
 rewrite(min(min(x, ((y + z) - w)), ((z + y) - u)), min(x, ((y + z) - max(w, u)))) ||
 rewrite(min(min(x, min(y, (z + (w - u)))), (v - u)), min(x, min((min(v, (w + z)) - u), y))) ||
 rewrite(min(min(x, min(y, ((z - w) + u))), (v - w)), min(x, min((min((z + u), v) - w), y))) ||
 rewrite(min(min(x, min(y, (z - w))), (u - w)), min(x, min((min(z, u) - w), y))) ||
 rewrite(min(min(x, min(y, (z - w))), (z - u)), min(x, min(y, (z - max(w, u))))) ||
 rewrite(min(min(x, min(y, min(z, (w - u)))), (v - u)), min(x, min(y, min(z, (min(w, v) - u))))) ||
 rewrite(min(min(x, min(y, min(z, (w - u)))), (w - v)), min(x, min(y, min(z, (w - max(u, v)))))) ||
 rewrite(min(min(x, min(y, min((z - w), u))), (v - w)), min(min(x, min(y, (min(z, v) - w))), u)) ||
 rewrite(min(min(x, min(y, min((z - w), u))), (z - v)), min(min(x, (z - max(w, v))), min(y, u))) ||
 rewrite(min(min(x, min((y + (z - w)), u)), (v - w)), min(min(x, (min(v, (z + y)) - w)), u)) ||
 rewrite(min(min(x, min(((y - z) + w), u)), (v - z)), min(min(x, (min((y + w), v) - z)), u)) ||
 rewrite(min(min(x, min((y - z), w)), (u - z)), min(min(x, (min(y, u) - z)), w)) ||
 rewrite(min(min(x, min((y - z), w)), (y - u)), min(x, min((y - max(z, u)), w))) ||
 rewrite(min(min(x, min(min(y, (z - w)), u)), (v - w)), min(min(x, (min(z, v) - w)), min(u, y))) ||
 rewrite(min(min(x, min(min(y, (z - w)), u)), (z - v)), min(min(x, min((z - max(v, w)), u)), y)) ||
 rewrite(min(min(x, min(min((y - z), w), u)), (v - z)), min(min(x, min((min(y, v) - z), u)), w)) ||
 rewrite(min(min(x, min(min((y - z), w), u)), (y - v)), min(min(x, (y - max(z, v))), min(u, w))) ||
 rewrite(min(min((x + y), z), (((w + x) - u) - v)), min(z, (x + min(y, ((w - v) - u))))) ||
 rewrite(min(min((x + y), z), (((w + y) - u) - v)), min(z, (y + min(x, ((w - v) - u))))) ||
 rewrite(min(min((x + y), z), (((x + w) - u) - v)), min(z, (x + min(y, ((w - v) - u))))) ||
 rewrite(min(min((x + y), z), (((y + w) - u) - v)), min(z, (y + min(x, ((w - v) - u))))) ||
 rewrite(min(min((x + (y + z)), w), ((u + y) - v)), min(w, (y + min((u - v), (z + x))))) ||
 rewrite(min(min((x + (y + z)), w), ((u + z) - v)), min(w, (z + min((u - v), (y + x))))) ||
 rewrite(min(min((x + (y + z)), w), ((y + u) - v)), min(w, (y + min((u - v), (z + x))))) ||
 rewrite(min(min((x + (y + z)), w), ((z + u) - v)), min(w, (z + min((u - v), (y + x))))) ||
 rewrite(min(min((x + (y - z)), w), (u - z)), min(w, (min(u, (y + x)) - z))) ||
 rewrite(min(min((x + (y - z)), w), (x - (y + z))), min(w, (min((x - y), (x + y)) - z))) ||
 rewrite(min(min((x + (y - z)), w), (x - (z + y))), min(w, (min((x - y), (x + y)) - z))) ||
 rewrite(min(min((x + (y - z)), w), ((y - z) - u)), min(w, (min((y - u), (x + y)) - z))) ||
 rewrite(min(min((x + (y - (z + w))), u), (y - v)), min(u, (y - max(((z + w) - x), v)))) ||
 rewrite(min(min(((x + y) + z), w), ((u + x) - v)), min((min((u - v), (y + z)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), ((u + y) - v)), min((min((u - v), (x + z)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), ((x + u) - v)), min((min((u - v), (y + z)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), ((y + u) - v)), min((min((u - v), (x + z)) + y), w)) ||
 rewrite(min(min(((x - y) + z), w), (u - y)), min(w, (min((x + z), u) - y))) ||
 rewrite(min(min(((x - y) + z), w), (z - (x + y))), min(w, (min((z - x), (z + x)) - y))) ||
 rewrite(min(min(((x - y) + z), w), (z - (y + x))), min(w, (min((z - x), (z + x)) - y))) ||
 rewrite(min(min(((x - y) + z), w), ((x - y) - u)), min(w, (min((x - u), (z + x)) - y))) ||
 rewrite(min(min(((x - (y + z)) + w), u), (x - v)), min(u, (x - max(((y + z) - w), v)))) ||
 rewrite(min(min((x - y), z), (w - y)), min(z, (min(x, w) - y))) ||
 rewrite(min(min((x - y), z), (x - w)), min(z, (x - max(y, w)))) ||
 rewrite(min(min((x - y), z), (((w + x) - u) - v)), min(z, (x - max(y, ((v - w) + u))))) ||
 rewrite(min(min((x - y), z), (((x + w) - u) - v)), min(z, (x - max(y, ((v - w) + u))))) ||
 rewrite(min(min((x - (y + z)), w), (u - y)), min(w, (min(u, (x - z)) - y))) ||
 rewrite(min(min((x - (y + z)), w), (u - z)), min(w, (min(u, (x - y)) - z))) ||
 rewrite(min(min(((x + y) - z), w), ((y + x) - u)), min(w, ((x + y) - max(z, u)))) ||
 rewrite(min(min(min(x, (y + (z - w))), u), (v - w)), min(u, min(x, (min((z + y), v) - w)))) ||
 rewrite(min(min(min(x, ((y - z) + w)), u), (v - z)), min(u, min(x, (min(v, (y + w)) - z)))) ||
 rewrite(min(min(min(x, (y - z)), w), (u - z)), min(min((min(y, u) - z), w), x)) ||
 rewrite(min(min(min(x, (y - z)), w), (y - u)), min(min((y - max(u, z)), w), x)) ||
 rewrite(min(min(min(x, min(y, (z - w))), u), (v - w)), min(y, min(x, min((min(z, v) - w), u)))) ||
 rewrite(min(min(min(x, min(y, (z - w))), u), (z - v)), min(min(min(x, (z - max(v, w))), u), y)) ||
 rewrite(min(min(min(x, min((y - z), w)), u), (v - z)), min(min(min(x, (min(y, v) - z)), u), w)) ||
 rewrite(min(min(min(x, min((y - z), w)), u), (y - v)), min(w, min(x, min((y - max(z, v)), u)))) ||
 rewrite(min(min(min((x + (y - z)), w), u), (v - z)), min(min((min(v, (y + x)) - z), u), w)) ||
 rewrite(min(min(min(((x - y) + z), w), u), (v - y)), min(min((min((x + z), v) - y), u), w)) ||
 rewrite(min(min(min((x - y), z), w), (u - y)), min(min((min(x, u) - y), w), z)) ||
 rewrite(min(min(min((x - y), z), w), (x - u)), min(min((x - max(u, y)), w), z)) ||
 rewrite(min(min(min(min(x, (y - z)), w), u), (v - z)), min(min((min(y, v) - z), u), min(w, x))) ||
 rewrite(min(min(min(min(x, (y - z)), w), u), (y - v)), min(min(min((y - max(v, z)), w), u), x)) ||
 rewrite(min(min(min(min((x - y), z), w), u), (v - y)), min(min(min((min(x, v) - y), w), u), z)) ||
 rewrite(min(min(min(min((x - y), z), w), u), (x - v)), min(min((x - max(y, v)), u), min(w, z))))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(min(min(x, (y + (z*w))), (w*z)), min(x, (min(y, 0) + (w*z)))) ||
 rewrite(min(min(x, (y + (z*w))), (z*w)), min(x, (min(y, 0) + (z*w)))) ||
 rewrite(min(min(x, ((y*z) + w)), (y*z)), min(x, (min(w, 0) + (y*z)))) ||
 rewrite(min(min(x, ((y*z) + w)), (z*y)), min(x, (min(w, 0) + (z*y)))) ||
 rewrite(min(min((x + (y*z)), w), (y*z)), min((min(x, 0) + (y*z)), w)) ||
 rewrite(min(min((x + (y*z)), w), (z*y)), min((min(x, 0) + (y*z)), w)) ||
 rewrite(min(min(((x*y) + z), w), (x*y)), min(w, (min(z, 0) + (x*y)))) ||
 rewrite(min(min(((x*y) + z), w), (y*x)), min(w, (min(z, 0) + (y*x)))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(max(x, y), (max(x, max(y, z)) + c0)), max(x, y), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(x, max(z, y)) + c0)), max(x, y), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(y, max(x, z)) + c0)), max(y, x), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(y, max(z, x)) + c0)), max(y, x), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(max(x, z), y) + c0)), max(y, x), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(max(y, z), x) + c0)), max(x, y), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(max(z, x), y) + c0)), max(y, x), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(max(z, y), x) + c0)), max(x, y), (0 <= c0)) ||
 rewrite(min(max(x, min(y, z)), (max(x, y) + c0)), max(x, min(z, y)), (0 <= c0)) ||
 rewrite(min(max(x, min(y, z)), (max(x, z) + c0)), max(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(max(x, min(y, z)), (max(y, x) + c0)), max(x, min(z, y)), (0 <= c0)) ||
 rewrite(min(max(x, min(y, z)), (max(z, x) + c0)), max(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(max(min(x, y), z), (max(x, z) + c0)), max(min(x, y), z), (0 <= c0)) ||
 rewrite(min(max(min(x, y), z), (max(y, z) + c0)), max(min(x, y), z), (0 <= c0)) ||
 rewrite(min(max(min(x, y), z), (max(z, x) + c0)), max(min(x, y), z), (0 <= c0)) ||
 rewrite(min(max(min(x, y), z), (max(z, y) + c0)), max(min(x, y), z), (0 <= c0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(max(x, y), min(z, max(w, x))), min(z, max(x, min(w, y)))) ||
 rewrite(min(max(x, y), min(z, max(w, y))), min(z, max(y, min(w, x)))) ||
 rewrite(min(max(x, y), min(z, max(x, w))), min(z, max(x, min(y, w)))) ||
 rewrite(min(max(x, y), min(z, max(y, w))), min(z, max(y, min(x, w)))) ||
 rewrite(min(max(x, y), min(max(x, z), w)), min(w, max(x, min(y, z)))) ||
 rewrite(min(max(x, y), min(max(y, z), w)), min(w, max(y, min(x, z)))) ||
 rewrite(min(max(x, y), min(max(z, x), w)), min(max(x, min(y, z)), w)) ||
 rewrite(min(max(x, y), min(max(z, y), w)), min(max(y, min(x, z)), w)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite(min(select(x, c0, c1), c2), select(x, fold(min(c0, c2)), fold(min(c1, c2)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(select(x, y, z), min(w, select(x, u, v))), min(w, select(x, min(y, u), min(z, v)))) ||
 rewrite(min(select(x, y, z), min(select(x, w, u), v)), min(v, select(x, min(y, w), min(z, u)))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(min(select(x, c0, c1), select(y, c2, c3)), select((y && x), fold(min(c0, c2)), fold(min(c1, c2))), ((min(c1, c3) == min(c1, c2)) && (min(c0, c3) == min(c1, c2))))))))
