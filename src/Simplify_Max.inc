((b.node_type() == IRNodeType::Add) && (
 rewrite(max(x, (max(y, c0) + ((z*c1) + (w - z)))), max(x, (max(y, c0) + (z + w))), (max((1 - max(c0, 0)), c1) == 2)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(x, min(y, (max(x, y) + c0))), max(x, (y + c0)), (c0 <= max(min(c0, 0), -1))) ||
 rewrite(max(x, min(y, (max(y, x) + c0))), max(x, (y + c0)), (c0 <= max(min(c0, 0), -1))) ||
 rewrite(max(x, min((max(x, y) + c0), y)), max(x, (y + c0)), (c0 <= max(min(c0, 0), -1))) ||
 rewrite(max(x, min((max(y, x) + c0), y)), max(x, (y + c0)), (c0 <= max(min(c0, 0), -1))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(max(x, max(x, y)), max(x, y)) ||
 rewrite(max(x, max(y, x)), max(y, x)) ||
 rewrite(max(x, max(y, (x + max(z, c0)))), max(y, (x + max(z, c0))), (0 <= c0)) ||
 rewrite(max(x, max(y, (x + max((z + (w*c0)), c1)))), max(y, (x + max((z + (w*c0)), c1))), (0 <= c1)) ||
 rewrite(max(x, max(y, (x + max(((z*c0) + w), c1)))), max(y, (x + max(((z*c0) + w), c1))), (0 <= c1)) ||
 rewrite(max(x, max(y, (max(z, c0) + x))), max(y, (max(z, c0) + x)), (0 <= c0)) ||
 rewrite(max(x, max(y, (max((z + (w*c0)), c1) + x))), max(y, (max((z + (w*c0)), c1) + x)), (0 <= c1)) ||
 rewrite(max(x, max(y, (max(((z*c0) + w), c1) + x))), max(y, (max(((z*c0) + w), c1) + x)), (0 <= c1)) ||
 rewrite(max(x, max((x + max(y, c0)), z)), max((x + max(y, c0)), z), (0 <= c0)) ||
 rewrite(max(x, max((x + max((y + (z*c0)), c1)), w)), max((x + max((y + (z*c0)), c1)), w), (0 <= c1)) ||
 rewrite(max(x, max((x + max(((y*c0) + z), c1)), w)), max((x + max(((y*c0) + z), c1)), w), (0 <= c1)) ||
 rewrite(max(x, max((max(y, c0) + x), z)), max((max(y, c0) + x), z), (0 <= c0)) ||
 rewrite(max(x, max((max((y + (z*c0)), c1) + x), w)), max((max((y + (z*c0)), c1) + x), w), (0 <= c1)) ||
 rewrite(max(x, max((max(((y*c0) + z), c1) + x), w)), max((max(((y*c0) + z), c1) + x), w), (0 <= c1)))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(max((max(x, c0) + ((y*c1) + (z - y))), w), max((max(x, c0) + (y + z)), w), (max((1 - max(c0, 0)), c1) == 2)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x + y), (z + (w + x))), (x + max(y, (w + z)))) ||
 rewrite(max((x + y), (z + (w + y))), (max(x, (z + w)) + y)) ||
 rewrite(max((x + y), (z + (w + (x - u)))), (x + max(y, (w + (z - u))))) ||
 rewrite(max((x + y), (z + (w + (y - u)))), (y + max(x, (w + (z - u))))) ||
 rewrite(max((x + y), (z + (x + w))), (x + max(y, (z + w)))) ||
 rewrite(max((x + y), (z + (y + w))), (max((z + w), x) + y)) ||
 rewrite(max((x + y), (z + ((x - w) + u))), (x + max(y, ((z - w) + u)))) ||
 rewrite(max((x + y), (z + ((y - w) + u))), (y + max(x, ((z - w) + u)))) ||
 rewrite(max((x + y), ((x + z) + w)), (x + max((w + z), y))) ||
 rewrite(max((x + y), ((y + z) + w)), (max(x, (w + z)) + y)) ||
 rewrite(max((x + y), ((z + x) + w)), (x + max((z + w), y))) ||
 rewrite(max((x + y), ((z + y) + w)), (max(x, (z + w)) + y)) ||
 rewrite(max((x + y), ((z + (x - w)) + u)), (max(y, ((z + u) - w)) + x)) ||
 rewrite(max((x + y), ((z + (y - w)) + u)), (max(x, ((z + u) - w)) + y)) ||
 rewrite(max((x + y), (((x - z) + w) + u)), (x + max(((u - z) + w), y))) ||
 rewrite(max((x + y), (((y - z) + w) + u)), (max(x, ((u - z) + w)) + y)) ||
 rewrite(max((x + (y + z)), (w + y)), (max((x + z), w) + y)) ||
 rewrite(max((x + (y + z)), (w + z)), (max((x + y), w) + z)) ||
 rewrite(max((x + (y + z)), (w + (u + y))), (max((x + z), (w + u)) + y)) ||
 rewrite(max((x + (y + z)), (w + (u + z))), (max((x + y), (w + u)) + z)) ||
 rewrite(max((x + (y + z)), (w + (u + (v + y)))), (max((x + z), (w + (u + v))) + y)) ||
 rewrite(max((x + (y + z)), (w + (u + (v + z)))), (max((x + y), (w + (u + v))) + z)) ||
 rewrite(max((x + (y + z)), (w + (u + (y + v)))), (max((x + z), (w + (u + v))) + y)) ||
 rewrite(max((x + (y + z)), (w + (u + (z + v)))), (max((x + y), (w + (u + v))) + z)) ||
 rewrite(max((x + (y + z)), (w + (y + u))), (max((x + z), (w + u)) + y)) ||
 rewrite(max((x + (y + z)), (w + (z + u))), (max((x + y), (w + u)) + z)) ||
 rewrite(max((x + (y + z)), (w + ((u + y) + v))), (max((x + z), (w + (u + v))) + y)) ||
 rewrite(max((x + (y + z)), (w + ((u + z) + v))), (max((x + y), (w + (u + v))) + z)) ||
 rewrite(max((x + (y + z)), (w + ((y + u) + v))), (max((x + z), (w + (v + u))) + y)) ||
 rewrite(max((x + (y + z)), (w + ((z + u) + v))), (max((x + y), (w + (v + u))) + z)) ||
 rewrite(max((x + (y + z)), (w + (y - u))), (max((w - u), (x + z)) + y)) ||
 rewrite(max((x + (y + z)), (w + (z - u))), (max((w - u), (x + y)) + z)) ||
 rewrite(max((x + (y + z)), (y + w)), (max((x + z), w) + y)) ||
 rewrite(max((x + (y + z)), (z + w)), (max((x + y), w) + z)) ||
 rewrite(max((x + (y + z)), ((w + y) + u)), (max((x + z), (w + u)) + y)) ||
 rewrite(max((x + (y + z)), ((w + z) + u)), (max((x + y), (w + u)) + z)) ||
 rewrite(max((x + (y + z)), ((w + (u + y)) + v)), (max((x + z), (w + (u + v))) + y)) ||
 rewrite(max((x + (y + z)), ((w + (u + z)) + v)), (max((x + y), (w + (u + v))) + z)) ||
 rewrite(max((x + (y + z)), ((w + (y + u)) + v)), (max((x + z), ((w + v) + u)) + y)) ||
 rewrite(max((x + (y + z)), ((w + (z + u)) + v)), (max((x + y), ((w + v) + u)) + z)) ||
 rewrite(max((x + (y + z)), ((y + w) + u)), (max((x + z), (u + w)) + y)) ||
 rewrite(max((x + (y + z)), ((z + w) + u)), (max((x + y), (u + w)) + z)) ||
 rewrite(max((x + (y + z)), (((w + y) + u) + v)), (max((x + z), ((w + v) + u)) + y)) ||
 rewrite(max((x + (y + z)), (((w + z) + u) + v)), (max((x + y), ((w + v) + u)) + z)) ||
 rewrite(max((x + (y + z)), (((y + w) + u) + v)), (max((x + z), ((v + w) + u)) + y)) ||
 rewrite(max((x + (y + z)), (((z + w) + u) + v)), (max((x + y), ((v + w) + u)) + z)) ||
 rewrite(max((x + (y + z)), ((y - w) + u)), (max((u - w), (x + z)) + y)) ||
 rewrite(max((x + (y + z)), ((z - w) + u)), (max((u - w), (x + y)) + z)) ||
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
 rewrite(max((x + (y - z)), (w + (u + y))), (max((x - z), (w + u)) + y)) ||
 rewrite(max((x + (y - z)), (w + (y + u))), (max((x - z), (w + u)) + y)) ||
 rewrite(max((x + (y - z)), (w + (u - z))), (max((x + y), (w + u)) - z)) ||
 rewrite(max((x + (y - z)), (y + w)), (max((x - z), w) + y)) ||
 rewrite(max((x + (y - z)), ((w + y) + u)), (max((x - z), (w + u)) + y)) ||
 rewrite(max((x + (y - z)), ((y + w) + u)), (max((x - z), (u + w)) + y)) ||
 rewrite(max((x + (y - z)), ((w - z) + u)), (max((x + y), (w + u)) - z)) ||
 rewrite(max((x + min(y, z)), (w + (u + min(z, y)))), (max(x, (w + u)) + min(z, y))) ||
 rewrite(max((x + min(y, z)), (w + (min(z, y) + u))), (max((w + u), x) + min(z, y))) ||
 rewrite(max((x + min(y, z)), ((w + min(z, y)) + u)), (max((u + w), x) + min(z, y))) ||
 rewrite(max((x + min(y, z)), ((min(z, y) + w) + u)), (max((u + w), x) + min(z, y))) ||
 rewrite(max((x + max(y, z)), (w + (u + max(z, y)))), (max(x, (w + u)) + max(z, y))) ||
 rewrite(max((x + max(y, z)), (w + (max(z, y) + u))), (max(y, z) + max(x, (w + u)))) ||
 rewrite(max((x + max(y, z)), ((w + max(z, y)) + u)), (max(y, z) + max(x, (w + u)))) ||
 rewrite(max((x + max(y, z)), ((max(z, y) + w) + u)), (max(y, z) + max(x, (u + w)))) ||
 rewrite(max(((x + y) + z), (w + x)), (max((y + z), w) + x)) ||
 rewrite(max(((x + y) + z), (w + y)), (max((x + z), w) + y)) ||
 rewrite(max(((x + y) + z), (w + (u + x))), (max((y + z), (w + u)) + x)) ||
 rewrite(max(((x + y) + z), (w + (u + y))), (max((x + z), (w + u)) + y)) ||
 rewrite(max(((x + y) + z), (w + (u + (v + x)))), (max((y + z), (w + (u + v))) + x)) ||
 rewrite(max(((x + y) + z), (w + (u + (v + y)))), (max((x + z), (w + (u + v))) + y)) ||
 rewrite(max(((x + y) + z), (w + (u + (x + v)))), (max((y + z), (w + (u + v))) + x)) ||
 rewrite(max(((x + y) + z), (w + (u + (y + v)))), (max((x + z), (w + (u + v))) + y)) ||
 rewrite(max(((x + y) + z), (w + (x + u))), (max((y + z), (w + u)) + x)) ||
 rewrite(max(((x + y) + z), (w + (y + u))), (max((x + z), (w + u)) + y)) ||
 rewrite(max(((x + y) + z), (w + ((u + x) + v))), (max((y + z), (w + (u + v))) + x)) ||
 rewrite(max(((x + y) + z), (w + ((u + y) + v))), (max((x + z), (w + (u + v))) + y)) ||
 rewrite(max(((x + y) + z), (w + ((x + u) + v))), (max((y + z), (w + (v + u))) + x)) ||
 rewrite(max(((x + y) + z), (w + ((y + u) + v))), (max((x + z), (w + (v + u))) + y)) ||
 rewrite(max(((x + y) + z), (w + (x - u))), (max((w - u), (z + y)) + x)) ||
 rewrite(max(((x + y) + z), (w + (y - u))), (max((w - u), (z + x)) + y)) ||
 rewrite(max(((x + y) + z), (x + w)), (max((y + z), w) + x)) ||
 rewrite(max(((x + y) + z), (y + w)), (max((x + z), w) + y)) ||
 rewrite(max(((x + y) + z), ((w + x) + u)), (max((y + z), (w + u)) + x)) ||
 rewrite(max(((x + y) + z), ((w + y) + u)), (max((x + z), (w + u)) + y)) ||
 rewrite(max(((x + y) + z), ((w + (u + x)) + v)), (max((y + z), (w + (u + v))) + x)) ||
 rewrite(max(((x + y) + z), ((w + (u + y)) + v)), (max((x + z), (w + (u + v))) + y)) ||
 rewrite(max(((x + y) + z), ((w + (x + u)) + v)), (max((y + z), ((w + v) + u)) + x)) ||
 rewrite(max(((x + y) + z), ((w + (y + u)) + v)), (max((x + z), ((w + v) + u)) + y)) ||
 rewrite(max(((x + y) + z), ((x + w) + u)), (max((y + z), (u + w)) + x)) ||
 rewrite(max(((x + y) + z), ((y + w) + u)), (max((x + z), (u + w)) + y)) ||
 rewrite(max(((x + y) + z), (((w + x) + u) + v)), (max((y + z), ((w + v) + u)) + x)) ||
 rewrite(max(((x + y) + z), (((w + y) + u) + v)), (max((x + z), ((w + v) + u)) + y)) ||
 rewrite(max(((x + y) + z), (((x + w) + u) + v)), (max((y + z), ((v + w) + u)) + x)) ||
 rewrite(max(((x + y) + z), (((y + w) + u) + v)), (max((x + z), ((v + w) + u)) + y)) ||
 rewrite(max(((x + y) + z), ((x - w) + u)), (max((u - w), (z + y)) + x)) ||
 rewrite(max(((x + y) + z), ((y - w) + u)), (max((u - w), (z + x)) + y)) ||
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
 rewrite(max(((x - y) + z), (w + (u + x))), (max((z - y), (w + u)) + x)) ||
 rewrite(max(((x - y) + z), (w + (x + u))), (max((z - y), (w + u)) + x)) ||
 rewrite(max(((x - y) + z), (w + (u - y))), (max((x + z), (w + u)) - y)) ||
 rewrite(max(((x - y) + z), (x + w)), (max((z - y), w) + x)) ||
 rewrite(max(((x - y) + z), ((w + x) + u)), (max((z - y), (w + u)) + x)) ||
 rewrite(max(((x - y) + z), ((x + w) + u)), (max((z - y), (u + w)) + x)) ||
 rewrite(max(((x - y) + z), ((w - y) + u)), (max((x + z), (w + u)) - y)) ||
 rewrite(max((min(x, y) + z), (w + (u + min(y, x)))), (max(z, (w + u)) + min(y, x))) ||
 rewrite(max((min(x, y) + z), (w + (min(y, x) + u))), (max((w + u), z) + min(y, x))) ||
 rewrite(max((min(x, y) + z), ((w + min(y, x)) + u)), (max((u + w), z) + min(y, x))) ||
 rewrite(max((min(x, y) + z), ((min(y, x) + w) + u)), (max(z, (u + w)) + min(y, x))) ||
 rewrite(max((max(x, y) + z), (w + (u + max(y, x)))), (max(z, (w + u)) + max(y, x))) ||
 rewrite(max((max(x, y) + z), (w + (max(y, x) + u))), (max(x, y) + max(z, (w + u)))) ||
 rewrite(max((max(x, y) + z), ((w + max(y, x)) + u)), (max(z, (u + w)) + max(y, x))) ||
 rewrite(max((max(x, y) + z), ((max(y, x) + w) + u)), (max(x, y) + max(z, (u + w)))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x - y), (z + (w - y))), (max(x, (z + w)) - y)) ||
 rewrite(max((x - y), ((z - y) + w)), (max((z + w), x) - y)) ||
 rewrite(max((x - max(y, c0)), (x + max(z, c0))), (x + max(z, c0)), (0 <= (max(c0, 0) + max(c0, -1)))) ||
 rewrite(max((x - max(y, c0)), (max(z, c0) + x)), (max(z, c0) + x), (0 <= (max(c0, 0) + max(c0, -1)))) ||
 rewrite(max(((x + y) - z), (w + (x + y))), (max((y - z), (w + y)) + x)) ||
 rewrite(max(((x + y) - z), (w + (y + x))), (max((x - z), (w + x)) + y)) ||
 rewrite(max(((x + y) - z), ((x + y) + w)), (max((x - z), (x + w)) + y)) ||
 rewrite(max(((x + y) - z), ((y + x) + w)), (max((y - z), (y + w)) + x)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite(max(min(x, (max(x, y) + c0)), y), max((x + c0), y), (c0 <= max(min(c0, 0), -1))) ||
 rewrite(max(min(x, (max(y, x) + c0)), y), max((x + c0), y), (c0 <= max(min(c0, 0), -1))) ||
 rewrite(max(min((max(x, y) + c0), x), y), max((x + c0), y), (c0 <= max(min(c0, 0), -1))) ||
 rewrite(max(min((max(x, y) + c0), y), x), max((y + c0), x), (c0 <= max(min(c0, 0), -1))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite(max(max(x, y), x), max(y, x)) ||
 rewrite(max(max(x, y), y), max(x, y)) ||
 rewrite(max(max(x, (y + max(z, c0))), y), max(x, (y + max(z, c0))), (0 <= c0)) ||
 rewrite(max(max(x, (y + max((z + (w*c0)), c1))), y), max(x, (y + max((z + (w*c0)), c1))), (0 <= c1)) ||
 rewrite(max(max(x, (y + max(((z*c0) + w), c1))), y), max(x, (y + max(((z*c0) + w), c1))), (0 <= c1)) ||
 rewrite(max(max(x, (max(y, c0) + z)), z), max(x, (max(y, c0) + z)), (0 <= c0)) ||
 rewrite(max(max(x, (max((y + (z*c0)), c1) + w)), w), max(x, (max((y + (z*c0)), c1) + w)), (0 <= c1)) ||
 rewrite(max(max(x, (max(((y*c0) + z), c1) + w)), w), max(x, (max(((y*c0) + z), c1) + w)), (0 <= c1)) ||
 rewrite(max(max((x + max(y, c0)), z), x), max((x + max(y, c0)), z), (0 <= c0)) ||
 rewrite(max(max((x + max((y + (z*c0)), c1)), w), x), max((x + max((y + (z*c0)), c1)), w), (0 <= c1)) ||
 rewrite(max(max((x + max(((y*c0) + z), c1)), w), x), max((x + max(((y*c0) + z), c1)), w), (0 <= c1)) ||
 rewrite(max(max((max(x, c0) + y), z), y), max((max(x, c0) + y), z), (0 <= c0)) ||
 rewrite(max(max((max((x + (y*c0)), c1) + z), w), z), max((max((x + (y*c0)), c1) + z), w), (0 <= c1)) ||
 rewrite(max(max((max(((x*c0) + y), c1) + z), w), z), max((max(((x*c0) + y), c1) + z), w), (0 <= c1)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max(max(x, (y + z)), (w + (u + y))), max(x, (y + max((w + u), z)))) ||
 rewrite(max(max(x, (y + z)), (w + (u + z))), max(x, (z + max((w + u), y)))) ||
 rewrite(max(max(x, (y + z)), (w + (y + u))), max(x, (y + max(z, (w + u))))) ||
 rewrite(max(max(x, (y + z)), (w + (z + u))), max((max(y, (u + w)) + z), x)) ||
 rewrite(max(max(x, (y + z)), (w + (y - u))), max(x, (y + max(z, (w - u))))) ||
 rewrite(max(max(x, (y + z)), (w + (z - u))), max(x, (z + max(y, (w - u))))) ||
 rewrite(max(max(x, (y + z)), ((w + y) + u)), max((max(z, (w + u)) + y), x)) ||
 rewrite(max(max(x, (y + z)), ((w + z) + u)), max((max(y, (u + w)) + z), x)) ||
 rewrite(max(max(x, (y + z)), ((y + w) + u)), max(x, (y + max((u + w), z)))) ||
 rewrite(max(max(x, (y + z)), ((z + w) + u)), max((max(y, (w + u)) + z), x)) ||
 rewrite(max(max(x, (y + z)), ((y - w) + u)), max(x, (y + max(z, (u - w))))) ||
 rewrite(max(max(x, (y + z)), ((z - w) + u)), max(x, (z + max((u - w), y)))) ||
 rewrite(max(max(x, (y + (z + w))), (u + w)), max(x, (max((z + y), u) + w))) ||
 rewrite(max(max(x, (y + (z + w))), (u + z)), max(x, (max((y + w), u) + z))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (v + w))), max(x, (max((z + y), (u + v)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (v + z))), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (w + v))), max(x, (max((z + y), (u + v)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), (u + (z + v))), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, (y + (z + w))), (w + u)), max(x, (max((z + y), u) + w))) ||
 rewrite(max(max(x, (y + (z + w))), (z + u)), max(x, (max((y + w), u) + z))) ||
 rewrite(max(max(x, (y + (z + w))), ((u + w) + v)), max(x, (max((z + y), (u + v)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), ((u + z) + v)), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, (y + (z + w))), ((w + u) + v)), max(x, (max((z + y), (v + u)) + w))) ||
 rewrite(max(max(x, (y + (z + w))), ((z + u) + v)), max(x, (max((y + w), (v + u)) + z))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + y)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + z)), max(x, (max((y + w), u) + z))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (v + y))), max(x, (max((z + w), (u + v)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (v + z))), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (y + v))), max(x, (max((z + w), (u + v)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + (z + v))), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, ((y + z) + w)), (y + u)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), (z + u)), max((max(u, (y + w)) + z), x)) ||
 rewrite(max(max(x, ((y + z) + w)), ((u + y) + v)), max(x, (max((z + w), (u + v)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), ((u + z) + v)), max(x, (max((y + w), (u + v)) + z))) ||
 rewrite(max(max(x, ((y + z) + w)), ((y + u) + v)), max(x, (max((z + w), (v + u)) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), ((z + u) + v)), max(x, (max((y + w), (v + u)) + z))) ||
 rewrite(max(max(x, (y - z)), (w + (u + (v - z)))), max(x, (max(y, (w + (u + v))) - z))) ||
 rewrite(max(max(x, (y - z)), (w + ((u - z) + v))), max((max(y, (u + (w + v))) - z), x)) ||
 rewrite(max(max(x, (y - z)), ((w + (u - z)) + v)), max(x, (max(y, (w + (u + v))) - z))) ||
 rewrite(max(max(x, (y - z)), (((w - z) + u) + v)), max(x, (max(y, ((w + v) + u)) - z))) ||
 rewrite(max(max(x, max(y, (z + w))), (u + w)), max(max(x, (max(z, u) + w)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (u + z)), max(max(x, (max(w, u) + z)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (u + (w - v))), max(max(x, y), (w + max(z, (u - v))))) ||
 rewrite(max(max(x, max(y, (z + w))), (u + (z - v))), max(max(x, y), (z + max(w, (u - v))))) ||
 rewrite(max(max(x, max(y, (z + w))), (w + u)), max(max(x, (max(z, u) + w)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (z + u)), max(max(x, (max(w, u) + z)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), ((w - u) + v)), max(x, max(y, (w + max(z, (v - u)))))) ||
 rewrite(max(max(x, max(y, (z + w))), ((z - u) + v)), max(x, max(y, (z + max(w, (v - u)))))) ||
 rewrite(max(max(x, max((y + z), w)), (u + y)), max(x, max((y + max(z, u)), w))) ||
 rewrite(max(max(x, max((y + z), w)), (u + z)), max(max(x, (max(y, u) + z)), w)) ||
 rewrite(max(max(x, max((y + z), w)), (u + (y - v))), max(max(x, w), (y + max(z, (u - v))))) ||
 rewrite(max(max(x, max((y + z), w)), (u + (z - v))), max(max(x, w), (z + max(y, (u - v))))) ||
 rewrite(max(max(x, max((y + z), w)), (y + u)), max(x, max((y + max(z, u)), w))) ||
 rewrite(max(max(x, max((y + z), w)), (z + u)), max(max(x, (max(y, u) + z)), w)) ||
 rewrite(max(max(x, max((y + z), w)), ((y - u) + v)), max(x, max((y + max(z, (v - u))), w))) ||
 rewrite(max(max(x, max((y + z), w)), ((z - u) + v)), max(x, max((z + max(y, (v - u))), w))) ||
 rewrite(max(max((x + y), z), (w + (u + x))), max(z, (x + max(y, (w + u))))) ||
 rewrite(max(max((x + y), z), (w + (u + y))), max((max((u + w), x) + y), z)) ||
 rewrite(max(max((x + y), z), (w + (x + u))), max((max((u + w), y) + x), z)) ||
 rewrite(max(max((x + y), z), (w + (y + u))), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((x + y), z), (w + (x - u))), max((max((w - u), y) + x), z)) ||
 rewrite(max(max((x + y), z), (w + (y - u))), max((max((w - u), x) + y), z)) ||
 rewrite(max(max((x + y), z), ((w + x) + u)), max((max((u + w), y) + x), z)) ||
 rewrite(max(max((x + y), z), ((w + y) + u)), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((x + y), z), ((x + w) + u)), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((x + y), z), ((y + w) + u)), max((max((u + w), x) + y), z)) ||
 rewrite(max(max((x + y), z), ((x - w) + u)), max(z, (x + max(y, (u - w))))) ||
 rewrite(max(max((x + y), z), ((y - w) + u)), max((max((u - w), x) + y), z)) ||
 rewrite(max(max((x + (y + z)), w), (u + y)), max(w, (max((x + z), u) + y))) ||
 rewrite(max(max((x + (y + z)), w), (u + z)), max(w, (max((y + x), u) + z))) ||
 rewrite(max(max((x + (y + z)), w), (u + (v + y))), max((max((x + z), (u + v)) + y), w)) ||
 rewrite(max(max((x + (y + z)), w), (u + (v + z))), max((max((x + y), (u + v)) + z), w)) ||
 rewrite(max(max((x + (y + z)), w), (u + (y + v))), max((max((x + z), (u + v)) + y), w)) ||
 rewrite(max(max((x + (y + z)), w), (u + (z + v))), max((max((x + y), (u + v)) + z), w)) ||
 rewrite(max(max((x + (y + z)), w), (y + u)), max((max(u, (x + z)) + y), w)) ||
 rewrite(max(max((x + (y + z)), w), (z + u)), max((max(u, (y + x)) + z), w)) ||
 rewrite(max(max((x + (y + z)), w), ((u + y) + v)), max(w, (max((x + z), (u + v)) + y))) ||
 rewrite(max(max((x + (y + z)), w), ((u + z) + v)), max(w, (max((y + x), (u + v)) + z))) ||
 rewrite(max(max((x + (y + z)), w), ((y + u) + v)), max(w, (max((x + z), (v + u)) + y))) ||
 rewrite(max(max((x + (y + z)), w), ((z + u) + v)), max(w, (max((y + x), (v + u)) + z))) ||
 rewrite(max(max(((x + y) + z), w), (u + x)), max(w, (max((y + z), u) + x))) ||
 rewrite(max(max(((x + y) + z), w), (u + y)), max(w, (max((x + z), u) + y))) ||
 rewrite(max(max(((x + y) + z), w), (u + (v + x))), max(w, (x + max((z + y), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), (u + (v + y))), max((max((x + z), (u + v)) + y), w)) ||
 rewrite(max(max(((x + y) + z), w), (u + (x + v))), max(w, (x + max((z + y), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), (u + (y + v))), max((max((x + z), (u + v)) + y), w)) ||
 rewrite(max(max(((x + y) + z), w), (x + u)), max((max((y + z), u) + x), w)) ||
 rewrite(max(max(((x + y) + z), w), (y + u)), max((max((x + z), u) + y), w)) ||
 rewrite(max(max(((x + y) + z), w), ((u + x) + v)), max(w, (x + max((z + y), (u + v))))) ||
 rewrite(max(max(((x + y) + z), w), ((u + y) + v)), max(w, (max((x + z), (u + v)) + y))) ||
 rewrite(max(max(((x + y) + z), w), ((x + u) + v)), max(w, (x + max((z + y), (v + u))))) ||
 rewrite(max(max(((x + y) + z), w), ((y + u) + v)), max(w, (max((x + z), (v + u)) + y))) ||
 rewrite(max(max((x - y), z), (w + (u + (v - y)))), max((max(x, (w + (u + v))) - y), z)) ||
 rewrite(max(max((x - y), z), (w + ((u - y) + v))), max((max(x, (u + (w + v))) - y), z)) ||
 rewrite(max(max((x - y), z), ((w + (u - y)) + v)), max(z, (max(x, (w + (u + v))) - y))) ||
 rewrite(max(max((x - y), z), (((w - y) + u) + v)), max(z, (max(x, ((w + v) + u)) - y))) ||
 rewrite(max(max(max(x, (y + z)), w), (u + y)), max(max((max(z, u) + y), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (u + z)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (u + (y - v))), max(w, max(x, (y + max(z, (u - v)))))) ||
 rewrite(max(max(max(x, (y + z)), w), (u + (z - v))), max(w, max(x, (z + max(y, (u - v)))))) ||
 rewrite(max(max(max(x, (y + z)), w), (y + u)), max(max((max(z, u) + y), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (z + u)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), ((y - u) + v)), max(w, max(x, (y + max(z, (v - u)))))) ||
 rewrite(max(max(max(x, (y + z)), w), ((z - u) + v)), max(w, max(x, (z + max(y, (v - u)))))) ||
 rewrite(max(max(max((x + y), z), w), (u + x)), max(max((max(u, y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (u + y)), max(max((max(x, u) + y), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (u + (x - v))), max(max((max((u - v), y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (u + (y - v))), max(max((max((u - v), x) + y), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (x + u)), max(max((max(u, y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (y + u)), max(max((max(x, u) + y), w), z)) ||
 rewrite(max(max(max((x + y), z), w), ((x - u) + v)), max(max(z, w), (max(y, (v - u)) + x))) ||
 rewrite(max(max(max((x + y), z), w), ((y - u) + v)), max(max(z, w), (max(x, (v - u)) + y)))))))
