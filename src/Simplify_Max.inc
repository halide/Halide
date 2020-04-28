((a.node_type() == IRNodeType::Add) && (
((b.node_type() == IRNodeType::Add) && (
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
 rewrite(max((x + ((y + z) + w)), (u + y)), (max((x + (z + w)), u) + y)) ||
 rewrite(max((x + ((y + z) + w)), (u + z)), (max((x + (y + w)), u) + z)) ||
 rewrite(max((x + ((y + z) + w)), (u + (z + y))), (max((x + w), u) + (z + y))) ||
 rewrite(max((x + ((y + z) + w)), (y + u)), (max((x + (z + w)), u) + y)) ||
 rewrite(max((x + ((y + z) + w)), (z + u)), (max((x + (y + w)), u) + z)) ||
 rewrite(max((x + ((y + z) + w)), ((z + y) + u)), (max((x + w), u) + (z + y))) ||
 rewrite(max((x + ((y - z) + w)), (u + y)), (max(u, ((x - z) + w)) + y)) ||
 rewrite(max((x + ((y - z) + w)), (y + u)), (max(u, ((x - z) + w)) + y)) ||
 rewrite(max((x + (y - z)), (w + (u - z))), (max((x + y), (w + u)) - z)) ||
 rewrite(max((x + (y - z)), ((w - z) + u)), (max((x + y), (w + u)) - z)) ||
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
 rewrite(max((((x + y) + z) + w), (u + x)), (max(((y + z) + w), u) + x)) ||
 rewrite(max((((x + y) + z) + w), (u + y)), (max(((x + z) + w), u) + y)) ||
 rewrite(max((((x + y) + z) + w), (u + (y + x))), (max((z + w), u) + (y + x))) ||
 rewrite(max((((x + y) + z) + w), (x + u)), (max(((y + z) + w), u) + x)) ||
 rewrite(max((((x + y) + z) + w), (y + u)), (max(((x + z) + w), u) + y)) ||
 rewrite(max((((x + y) + z) + w), ((y + x) + u)), (max((z + w), u) + (y + x))) ||
 rewrite(max((((x - y) + z) + w), (u + x)), (max(((z - y) + w), u) + x)) ||
 rewrite(max((((x - y) + z) + w), (x + u)), (max(((z - y) + w), u) + x)) ||
 rewrite(max(((x - y) + z), (w + (u - y))), (max((x + z), (w + u)) - y)) ||
 rewrite(max(((x - y) + z), ((w - y) + u)), (max((x + z), (w + u)) - y)))))) ||
((a.node_type() == IRNodeType::Div) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite(max((x/c0), 0), (max(x, 1)/c0), (2 <= c0)))))) ||
((a.node_type() == IRNodeType::Max) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(max(max(x, (y + z)), (w + (u + y))), max(x, (y + max((w + u), z)))) ||
 rewrite(max(max(x, (y + z)), (w + (u + z))), max(x, (z + max((w + u), y)))) ||
 rewrite(max(max(x, (y + z)), (w + (y + u))), max(x, (y + max(z, (w + u))))) ||
 rewrite(max(max(x, (y + z)), (w + (z + u))), max((max(y, (u + w)) + z), x)) ||
 rewrite(max(max(x, (y + z)), ((w + y) + u)), max((max(z, (w + u)) + y), x)) ||
 rewrite(max(max(x, (y + z)), ((w + z) + u)), max((max(y, (w + u)) + z), x)) ||
 rewrite(max(max(x, (y + z)), ((y + w) + u)), max(x, (y + max((u + w), z)))) ||
 rewrite(max(max(x, (y + z)), ((z + w) + u)), max((max(y, (u + w)) + z), x)) ||
 rewrite(max(max(x, (y - z)), (w + (u + (v - z)))), max(x, (max(y, (w + (u + v))) - z))) ||
 rewrite(max(max(x, (y - z)), (w + ((u - z) + v))), max((max(y, (u + (w + v))) - z), x)) ||
 rewrite(max(max(x, (y - z)), ((w + (u - z)) + v)), max(x, (max(y, (w + (u + v))) - z))) ||
 rewrite(max(max(x, (y - z)), (((w - z) + u) + v)), max(x, (max(y, ((w + v) + u)) - z))) ||
 rewrite(max(max(x, max(y, (z + w))), (u + w)), max(max(x, (max(z, u) + w)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (u + z)), max(max(x, (max(w, u) + z)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (w + u)), max(max(x, (max(z, u) + w)), y)) ||
 rewrite(max(max(x, max(y, (z + w))), (z + u)), max(max(x, (max(w, u) + z)), y)) ||
 rewrite(max(max(x, max((y + z), w)), (u + y)), max(x, max((y + max(z, u)), w))) ||
 rewrite(max(max(x, max((y + z), w)), (u + z)), max(max(x, (max(y, u) + z)), w)) ||
 rewrite(max(max(x, max((y + z), w)), (y + u)), max(x, max((y + max(z, u)), w))) ||
 rewrite(max(max(x, max((y + z), w)), (z + u)), max(max(x, (max(y, u) + z)), w)) ||
 rewrite(max(max((x + y), z), (w + (u + x))), max(z, (x + max(y, (w + u))))) ||
 rewrite(max(max((x + y), z), (w + (u + y))), max((max((u + w), x) + y), z)) ||
 rewrite(max(max((x + y), z), (w + (x + u))), max((max((u + w), y) + x), z)) ||
 rewrite(max(max((x + y), z), (w + (y + u))), max((max((u + w), x) + y), z)) ||
 rewrite(max(max((x + y), z), ((w + x) + u)), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((x + y), z), ((w + y) + u)), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((x + y), z), ((x + w) + u)), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((x + y), z), ((y + w) + u)), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((x - y), z), (w + (u + (v - y)))), max((max(x, (w + (u + v))) - y), z)) ||
 rewrite(max(max((x - y), z), (w + ((u - y) + v))), max((max(x, (u + (w + v))) - y), z)) ||
 rewrite(max(max((x - y), z), ((w + (u - y)) + v)), max(z, (max(x, (w + (u + v))) - y))) ||
 rewrite(max(max((x - y), z), (((w - y) + u) + v)), max(z, (max(x, ((w + v) + u)) - y))) ||
 rewrite(max(max(max(x, (y + z)), w), (u + y)), max(max((max(z, u) + y), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (u + z)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (y + u)), max(max((max(z, u) + y), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (z + u)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max((x + y), z), w), (u + x)), max(max((max(u, y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (u + y)), max(max((max(x, u) + y), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (x + u)), max(max((max(u, y) + x), w), z)) ||
 rewrite(max(max(max((x + y), z), w), (y + u)), max(max((max(x, u) + y), w), z))))))
