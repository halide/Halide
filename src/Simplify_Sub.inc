((b.node_type() == IRNodeType::Add) && (
 rewrite((x - (y + min(x, 0))), (max(0, x) - y)) ||
 rewrite((x - (y + max(x, 0))), (min(0, x) - y)) ||
 rewrite((x - (z + (x - y))), (y - z)) ||
 rewrite((x - ((x - y) + z)), (y - z)) ||
 rewrite((x - (min(x, 0) + y)), (max(x, 0) - y)) ||
 rewrite((x - (max(x, 0) + y)), (min(x, 0) - y)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((x - min((x - y), 0)), max(y, x)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((x - max((x - y), 0)), min(y, x)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((x - select(y, 0, (x - z))), select(y, x, z)) ||
 rewrite((x - select(y, z, (x + z))), (select(y, x, 0) - z)) ||
 rewrite((x - select(y, z, (z + x))), (select(y, x, 0) - z)) ||
 rewrite((x - select(y, (x + z), z)), (select(y, 0, x) - z)) ||
 rewrite((x - select(y, (z + x), z)), (select(y, 0, x) - z)) ||
 rewrite((x - select(y, (x - z), 0)), select(y, z, x)) ||
 rewrite((x - select(y, (x - z), z)), select(y, z, (x - z))))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(((x + (y + (w + z))) - z), (x + (y + w))) ||
 rewrite(((x + (y + (z + w))) - z), (x + (y + w))) ||
 rewrite(((x + ((w + z) + y)) - z), (x + (w + y))) ||
 rewrite(((x + ((z + w) + y)) - z), (x + (w + y))) ||
 rewrite(((y + (x*2)) - x), (y + x)) ||
 rewrite(((y + min(x, 0)) - x), (y - max(x, 0))) ||
 rewrite(((y + max(x, 0)) - x), (y - min(x, 0))) ||
 rewrite(((z + (x - y)) - x), (z - y)) ||
 rewrite((((y + (w + z)) + x) - z), (y + (w + x))) ||
 rewrite((((y + (z + w)) + x) - z), (y + (w + x))) ||
 rewrite(((((w + z) + y) + x) - z), ((w + y) + x)) ||
 rewrite(((((z + w) + y) + x) - z), ((w + y) + x)) ||
 rewrite((((x - y) + z) - x), (z - y)) ||
 rewrite((((x*2) + y) - x), (x + y)) ||
 rewrite(((min(x, 0) + y) - x), (y - max(x, 0))) ||
 rewrite(((max(x, 0) + y) - x), (y - min(x, 0))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((w + min(z, (x + y))) - (u + x)), ((w + min(y, (z - x))) - u)) ||
 rewrite(((w + min(z, (x + y))) - (x + u)), ((w + min(y, (z - x))) - u)) ||
 rewrite(((w + min(z, (y + x))) - (u + x)), ((w + min(y, (z - x))) - u)) ||
 rewrite(((w + min(z, (y + x))) - (x + u)), ((w + min(y, (z - x))) - u)) ||
 rewrite(((w + min((x + y), z)) - (u + x)), ((w + min((z - x), y)) - u)) ||
 rewrite(((w + min((x + y), z)) - (x + u)), ((w + min((z - x), y)) - u)) ||
 rewrite(((w + min((y + x), z)) - (u + x)), ((w + min((z - x), y)) - u)) ||
 rewrite(((w + min((y + x), z)) - (x + u)), ((w + min((z - x), y)) - u)) ||
 rewrite(((y + x) - (z + (w + x))), (y - (z + w))) ||
 rewrite(((y + x) - (z + (w + y))), (x - (z + w))) ||
 rewrite(((y + x) - (z + (x + w))), (y - (z + w))) ||
 rewrite(((y + x) - (z + (y + w))), (x - (z + w))) ||
 rewrite(((y + x) - ((w + x) + z)), (y - (w + z))) ||
 rewrite(((y + x) - ((w + y) + z)), (x - (w + z))) ||
 rewrite(((y + x) - ((x + w) + z)), (y - (w + z))) ||
 rewrite(((y + x) - ((y + w) + z)), (x - (w + z))) ||
 rewrite(((min(z, (x + y)) + w) - (u + x)), ((min(y, (z - x)) + w) - u)) ||
 rewrite(((min(z, (x + y)) + w) - (x + u)), ((min(y, (z - x)) + w) - u)) ||
 rewrite(((min(z, (y + x)) + w) - (u + x)), ((min(y, (z - x)) + w) - u)) ||
 rewrite(((min(z, (y + x)) + w) - (x + u)), ((min(y, (z - x)) + w) - u)) ||
 rewrite(((min((x + y), z) + w) - (u + x)), ((min((z - x), y) + w) - u)) ||
 rewrite(((min((x + y), z) + w) - (x + u)), ((min((z - x), y) + w) - u)) ||
 rewrite(((min((y + x), z) + w) - (u + x)), ((min((z - x), y) + w) - u)) ||
 rewrite(((min((y + x), z) + w) - (x + u)), ((min((z - x), y) + w) - u)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((x + (y - (z*c0))) - (z*c0)), (x + (y - (z*fold((c0*2)))))) ||
 rewrite(((y + x) - (x*2)), (y - x)) ||
 rewrite(((y + x) - (y*2)), (x - y)) ||
 rewrite(((z + (x*y)) - (w*y)), (z + (y*(x - w)))) ||
 rewrite(((z + (x*y)) - (y*w)), (z + (y*(x - w)))) ||
 rewrite(((z + (y*x)) - (w*y)), (z + (y*(x - w)))) ||
 rewrite(((z + (y*x)) - (y*w)), (z + (y*(x - w)))) ||
 rewrite((((y - (z*c0)) + x) - (z*c0)), ((y - (z*fold((c0*2)))) + x)) ||
 rewrite((((y*z) + x) - (w*z)), (x + ((y - w)*z))) ||
 rewrite((((y*z) + x) - (z*w)), (x + ((y - w)*z))) ||
 rewrite((((z*y) + x) - (w*z)), (x + ((y - w)*z))) ||
 rewrite((((z*y) + x) - (z*w)), (x + ((y - w)*z))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite(((z + ((x + y)/c0)) - (x/c0)), (z + (((x % c0) + y)/c0))) ||
 rewrite(((z + ((y + x)/c0)) - (x/c0)), (z + ((y + (x % c0))/c0))) ||
 rewrite(((((y + z)/c0) + x) - (y/c0)), (x + (((y % c0) + z)/c0))) ||
 rewrite(((((z + y)/c0) + x) - (y/c0)), (x + (((y % c0) + z)/c0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((x + y) - min(w, (y + z))), (x - min(z, (w - y)))) ||
 rewrite(((x + y) - min(w, (z + y))), (x - min(z, (w - y)))) ||
 rewrite(((x + y) - min((y + z), w)), (x - min((w - y), z))) ||
 rewrite(((x + y) - min((z + y), w)), (x - min((w - y), z))) ||
 rewrite(((y + x) - min(w, (y + z))), (x - min(z, (w - y)))) ||
 rewrite(((y + x) - min(w, (z + y))), (x - min(z, (w - y)))) ||
 rewrite(((y + x) - min(x, y)), max(x, y)) ||
 rewrite(((y + x) - min(y, x)), max(y, x)) ||
 rewrite(((y + x) - min((y + z), w)), (x - min((w - y), z))) ||
 rewrite(((y + x) - min((z + y), w)), (x - min((w - y), z))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(((x + y) - max(y, select(z, (w + y), y))), (x + min(0, select(z, (0 - w), 0)))) ||
 rewrite(((x + y) - max(y, select(z, (y + w), y))), (x + min(0, select(z, (0 - w), 0)))) ||
 rewrite(((x + y) - max(z, (u + (w + y)))), (x - max((w + u), (z - y)))) ||
 rewrite(((x + y) - max(z, (u + (y + w)))), (x - max((z - y), (u + w)))) ||
 rewrite(((x + y) - max(z, ((w + y) + u))), (x - max((z - y), (w + u)))) ||
 rewrite(((x + y) - max(z, ((y + w) + u))), (x - max((z - y), (w + u)))) ||
 rewrite(((x + y) - max((w + (y + z)), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((x + y) - max((w + (z + y)), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((x + y) - max(((y + z) + w), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((x + y) - max(((z + y) + w), u)), (x - max((z + w), (u - y)))) ||
 rewrite(((x + y) - max(select(z, (w + y), y), y)), (x + min(select(z, (0 - w), 0), 0))) ||
 rewrite(((x + y) - max(select(z, (y + w), y), y)), (x + min(select(z, (0 - w), 0), 0))) ||
 rewrite(((x + (z*y)) - max(w, (u + (y*z)))), (x - max(u, (w - (y*z))))) ||
 rewrite(((x + (z*y)) - max(w, ((y*z) + u))), (x - max((w - (y*z)), u))) ||
 rewrite(((x + (z*y)) - max((u + (y*z)), w)), (x - max((w - (y*z)), u))) ||
 rewrite(((x + (z*y)) - max(((y*z) + u), w)), (x - max((w - (y*z)), u))) ||
 rewrite(((x + min(z, y)) - max(w, (u + min(y, z)))), (x - max(u, (w - min(y, z))))) ||
 rewrite(((x + min(z, y)) - max(w, (min(y, z) + u))), (x - max(u, (w - min(y, z))))) ||
 rewrite(((x + min(z, y)) - max((u + min(y, z)), w)), (x - max((w - min(y, z)), u))) ||
 rewrite(((x + min(z, y)) - max((min(y, z) + u), w)), (x - max(u, (w - min(y, z))))) ||
 rewrite(((y + x) - max(x, y)), min(x, y)) ||
 rewrite(((y + x) - max(y, x)), min(y, x)) ||
 rewrite(((y + x) - max(y, select(z, (w + y), y))), (x + min(0, select(z, (0 - w), 0)))) ||
 rewrite(((y + x) - max(y, select(z, (y + w), y))), (x + min(0, select(z, (0 - w), 0)))) ||
 rewrite(((y + x) - max(z, (u + (w + y)))), (x - max((w + u), (z - y)))) ||
 rewrite(((y + x) - max(z, (u + (y + w)))), (x - max((z - y), (u + w)))) ||
 rewrite(((y + x) - max(z, (w + x))), (y - max(w, (z - x)))) ||
 rewrite(((y + x) - max(z, (w + y))), (x - max(w, (z - y)))) ||
 rewrite(((y + x) - max(z, (x + w))), (y - max(w, (z - x)))) ||
 rewrite(((y + x) - max(z, (y + w))), (x - max(w, (z - y)))) ||
 rewrite(((y + x) - max(z, ((w + y) + u))), (x - max((z - y), (w + u)))) ||
 rewrite(((y + x) - max(z, ((y + w) + u))), (x - max((z - y), (w + u)))) ||
 rewrite(((y + x) - max((w + x), z)), (y - max((z - x), w))) ||
 rewrite(((y + x) - max((w + y), z)), (x - max((z - y), w))) ||
 rewrite(((y + x) - max((w + (y + z)), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((y + x) - max((w + (z + y)), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((y + x) - max((x + w), z)), (y - max((z - x), w))) ||
 rewrite(((y + x) - max((y + w), z)), (x - max((z - y), w))) ||
 rewrite(((y + x) - max(((y + z) + w), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((y + x) - max(((z + y) + w), u)), (x - max((z + w), (u - y)))) ||
 rewrite(((y + x) - max(select(z, (w + y), y), y)), (x + min(select(z, (0 - w), 0), 0))) ||
 rewrite(((y + x) - max(select(z, (y + w), y), y)), (x + min(select(z, (0 - w), 0), 0))) ||
 rewrite((((z*y) + x) - max(w, (u + (y*z)))), (x - max(u, (w - (y*z))))) ||
 rewrite((((z*y) + x) - max(w, ((y*z) + u))), (x - max((w - (y*z)), u))) ||
 rewrite((((z*y) + x) - max((u + (y*z)), w)), (x - max((w - (y*z)), u))) ||
 rewrite((((z*y) + x) - max(((y*z) + u), w)), (x - max((w - (y*z)), u))) ||
 rewrite(((min(z, y) + x) - max(w, (u + min(y, z)))), (x - max(u, (w - min(y, z))))) ||
 rewrite(((min(z, y) + x) - max(w, (min(y, z) + u))), (x - max((w - min(y, z)), u))) ||
 rewrite(((min(z, y) + x) - max((u + min(y, z)), w)), (x - max((w - min(y, z)), u))) ||
 rewrite(((min(z, y) + x) - max((min(y, z) + u), w)), (x - max((w - min(y, z)), u))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(((y + x) - select(z, x, y)), select(z, y, x)) ||
 rewrite(((y + x) - select(z, y, x)), select(z, x, y)))))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite((((y + x) - z) - x), (y - z)) ||
 rewrite((((y + x) - z) - y), (x - z)) ||
 rewrite((((x - y) - y) - y), (x - (3*y))) ||
 rewrite((((x - y) - z) - x), ((0 - y) - z)) ||
 rewrite(((((x + y) - z) - w) - y), ((x - z) - w)) ||
 rewrite(((((y + x) - z) - w) - y), ((x - z) - w)) ||
 rewrite((((x*2) - y) - x), (x - y)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x - y) - (x + z)), ((0 - y) - z)) ||
 rewrite(((x - y) - (z + x)), ((0 - y) - z)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((x - (y*c0)) - (y*c0)), (x - (y*fold((c0*2))))))))) ||
((a.node_type() == IRNodeType::Mul) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*2) - (x + y)), (x - y)) ||
 rewrite(((x*2) - (y + x)), (x - y)) ||
 rewrite(((x*y) - (z + (w*y))), ((y*(x - w)) - z)) ||
 rewrite(((x*y) - (z + (y*w))), ((y*(x - w)) - z)) ||
 rewrite(((x*y) - ((y*z) + w)), ((y*(x - z)) - w)) ||
 rewrite(((x*y) - ((z*y) + w)), ((y*(x - z)) - w)) ||
 rewrite(((y*x) - (z + (w*y))), ((y*(x - w)) - z)) ||
 rewrite(((y*x) - (z + (y*w))), ((y*(x - w)) - z)) ||
 rewrite(((y*x) - ((y*z) + w)), ((y*(x - z)) - w)) ||
 rewrite(((y*x) - ((z*y) + w)), ((y*(x - z)) - w)) ||
 rewrite(((y*(x + 1)) - (y + z)), ((y*x) - z)) ||
 rewrite(((y*(x + 1)) - (z + y)), ((y*x) - z)) ||
 rewrite((((x + 1)*y) - (y + z)), ((x*y) - z)) ||
 rewrite((((x + 1)*y) - (z + y)), ((x*y) - z)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((z*(y*x)) - (w*x)), (((y*z) - w)*x)) ||
 rewrite(((z*(y*x)) - (w*y)), (((z*x) - w)*y)) ||
 rewrite(((z*(y*x)) - (x*w)), (x*((y*z) - w))) ||
 rewrite(((z*(y*x)) - (y*w)), (y*((z*x) - w))) ||
 rewrite((((y*x)*z) - (w*x)), (((y*z) - w)*x)) ||
 rewrite((((y*x)*z) - (w*y)), (((x*z) - w)*y)) ||
 rewrite((((y*x)*z) - (x*w)), (((y*z) - w)*x)) ||
 rewrite((((y*x)*z) - (y*w)), (((x*z) - w)*y)))))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite((min(w, (z + (x + y))) - y), min((z + x), (w - y))) ||
 rewrite((min(w, (z + (y + x))) - y), min((z + x), (w - y))) ||
 rewrite((min(w, ((x + y) + z)) - y), min((x + z), (w - y))) ||
 rewrite((min(w, ((y + x) + z)) - y), min((z + x), (w - y))) ||
 rewrite((min(y, (x*2)) - x), min(x, (y - x))) ||
 rewrite((min((z + (x + y)), w) - y), min((z + x), (w - y))) ||
 rewrite((min((z + (y + x)), w) - y), min((z + x), (w - y))) ||
 rewrite((min(((x + y) + z), w) - y), min((x + z), (w - y))) ||
 rewrite((min(((y + x) + z), w) - y), min((x + z), (w - y))) ||
 rewrite((min((x - y), 0) - x), (0 - max(y, x))) ||
 rewrite((min((x*2), y) - x), min(x, (y - x))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(w, (z + (y + x))) - (u + (x + y))), (min(z, (w - (y + x))) - u)) ||
 rewrite((min(w, (z + (y + x))) - ((x + y) + u)), (min(z, (w - (y + x))) - u)) ||
 rewrite((min(w, ((y + x) + z)) - (u + (x + y))), (min(z, ((w - x) - y)) - u)) ||
 rewrite((min(w, ((y + x) + z)) - ((x + y) + u)), (min(z, ((w - x) - y)) - u)) ||
 rewrite((min(x, (w + (y + z))) - (u + y)), (min((x - y), (w + z)) - u)) ||
 rewrite((min(x, (w + (y + z))) - (y + u)), (min((x - y), (w + z)) - u)) ||
 rewrite((min(x, (w + (z + y))) - (u + y)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(x, (w + (z + y))) - (y + u)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(x, (w + (z*y))) - (u + (y*z))), (min(w, (x - (z*y))) - u)) ||
 rewrite((min(x, (w + (z*y))) - ((y*z) + u)), (min(w, (x - (z*y))) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (u + y)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (y + u)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((z + y) + w)) - (u + y)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((z + y) + w)) - (y + u)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(x, ((z*y) + w)) - (u + (y*z))), (min((x - (z*y)), w) - u)) ||
 rewrite((min(x, ((z*y) + w)) - ((y*z) + u)), (min(w, (x - (y*z))) - u)) ||
 rewrite((min(y, x) - (x + y)), (0 - max(x, y))) ||
 rewrite((min(y, x) - (y + x)), (0 - max(y, x))) ||
 rewrite((min(z, (x + y)) - (w + x)), (min(y, (z - x)) - w)) ||
 rewrite((min(z, (x + y)) - (x + w)), (min(y, (z - x)) - w)) ||
 rewrite((min(z, (y + x)) - (w + x)), (min(y, (z - x)) - w)) ||
 rewrite((min(z, (y + x)) - (x + w)), (min(y, (z - x)) - w)) ||
 rewrite((min((w + (y + z)), x) - (u + y)), (min((w + z), (x - y)) - u)) ||
 rewrite((min((w + (y + z)), x) - (y + u)), (min((w + z), (x - y)) - u)) ||
 rewrite((min((w + (z + y)), x) - (u + y)), (min((w + z), (x - y)) - u)) ||
 rewrite((min((w + (z + y)), x) - (y + u)), (min((w + z), (x - y)) - u)) ||
 rewrite((min((w + (z*y)), x) - (u + (y*z))), (min((x - (z*y)), w) - u)) ||
 rewrite((min((w + (z*y)), x) - ((y*z) + u)), (min((x - (z*y)), w) - u)) ||
 rewrite((min((y + z), x) - (w + y)), (min((x - y), z) - w)) ||
 rewrite((min((y + z), x) - (y + w)), (min((x - y), z) - w)) ||
 rewrite((min((z + y), x) - (w + y)), (min((x - y), z) - w)) ||
 rewrite((min((z + y), x) - (y + w)), (min((x - y), z) - w)) ||
 rewrite((min((z + (y + x)), w) - (u + (x + y))), (min((w - (y + x)), z) - u)) ||
 rewrite((min((z + (y + x)), w) - ((x + y) + u)), (min((w - (y + x)), z) - u)) ||
 rewrite((min(((y + x) + z), w) - (u + (x + y))), (min(((w - x) - y), z) - u)) ||
 rewrite((min(((y + x) + z), w) - ((x + y) + u)), (min(((w - x) - y), z) - u)) ||
 rewrite((min(((y + z) + w), x) - (u + y)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(((y + z) + w), x) - (y + u)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(((z + y) + w), x) - (u + y)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(((z + y) + w), x) - (y + u)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(((z*y) + w), x) - (u + (y*z))), (min((x - (z*y)), w) - u)) ||
 rewrite((min(((z*y) + w), x) - ((y*z) + u)), (min(w, (x - (y*z))) - u)))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max(u, max(x, (w + (z + y)))) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(x, (w + (z + y)))) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(u, max(x, ((z + y) + w))) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(x, ((z + y) + w))) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(u, max((z + (x + y)), w)) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(u, max((z + (y + x)), w)) - y), max((z + x), (max(u, w) - y))) ||
 rewrite((max(u, max(((z + y) + w), x)) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(((z + y) + w), x)) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(w, max(z, (y + x))) - x), max(y, (max(z, w) - x))) ||
 rewrite((max(w, max(z, (y + x))) - y), max(x, (max(z, w) - y))) ||
 rewrite((max(w, max((z + y), x)) - y), max(z, (max(w, x) - y))) ||
 rewrite((max(w, max((z + y), x)) - z), max(y, (max(w, x) - z))) ||
 rewrite((max(x, (u + (y + (w + z)))) - z), max((u + (y + w)), (x - z))) ||
 rewrite((max(x, (u + (y + (z + w)))) - z), max((u + (y + w)), (x - z))) ||
 rewrite((max(x, (u + ((y + z) + w))) - z), max((u + (y + w)), (x - z))) ||
 rewrite((max(x, (u + ((z + y) + w))) - z), max((u + (w + y)), (x - z))) ||
 rewrite((max(x, (w + (z + y))) - y), max((w + z), (x - y))) ||
 rewrite((max(x, (w + (z + y))) - z), max((w + y), (x - z))) ||
 rewrite((max(x, ((y + (w + z)) + u)) - z), max((y + (w + u)), (x - z))) ||
 rewrite((max(x, ((y + (z + w)) + u)) - z), max((y + (u + w)), (x - z))) ||
 rewrite((max(x, ((z + y) + w)) - y), max((z + w), (x - y))) ||
 rewrite((max(x, ((z + y) + w)) - z), max((w + y), (x - z))) ||
 rewrite((max(x, (((y + z) + w) + u)) - z), max(((y + u) + w), (x - z))) ||
 rewrite((max(x, (((z + y) + w) + u)) - z), max(((u + y) + w), (x - z))) ||
 rewrite((max(y, (x*2)) - x), max(x, (y - x))) ||
 rewrite((max((u + (y + (w + z))), x) - z), max((u + (y + w)), (x - z))) ||
 rewrite((max((u + (y + (z + w))), x) - z), max((u + (y + w)), (x - z))) ||
 rewrite((max((u + ((y + z) + w)), x) - z), max((u + (y + w)), (x - z))) ||
 rewrite((max((u + ((z + y) + w)), x) - z), max((u + (y + w)), (x - z))) ||
 rewrite((max((z + (x + y)), w) - x), max((z + y), (w - x))) ||
 rewrite((max((z + (y + x)), w) - x), max((z + y), (w - x))) ||
 rewrite((max(((y + (w + z)) + u), x) - z), max(((y + w) + u), (x - z))) ||
 rewrite((max(((y + (z + w)) + u), x) - z), max(((y + w) + u), (x - z))) ||
 rewrite((max(((z + y) + w), x) - y), max((z + w), (x - y))) ||
 rewrite((max(((z + y) + w), x) - z), max((y + w), (x - z))) ||
 rewrite((max((((y + z) + w) + u), x) - z), max(((y + w) + u), (x - z))) ||
 rewrite((max((((z + y) + w) + u), x) - z), max(((y + w) + u), (x - z))) ||
 rewrite((max((x - y), 0) - x), (0 - min(y, x))) ||
 rewrite((max((x*2), y) - x), max(x, (y - x))) ||
 rewrite((max(max(x, (w + (z + y))), u) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(max(x, (w + (z + y))), u) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(max(x, ((z + y) + w)), u) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(max(x, ((z + y) + w)), u) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(max(z, (y + x)), w) - x), max((max(z, w) - x), y)) ||
 rewrite((max(max(z, (y + x)), w) - y), max(x, (max(z, w) - y))) ||
 rewrite((max(max((z + y), x), w) - y), max(z, (max(x, w) - y))) ||
 rewrite((max(max((z + y), x), w) - z), max(y, (max(x, w) - z))) ||
 rewrite((max(max((z + (x + y)), w), u) - y), max((z + x), (max(w, u) - y))) ||
 rewrite((max(max((z + (y + x)), w), u) - y), max((z + x), (max(w, u) - y))) ||
 rewrite((max(max(((z + y) + w), x), u) - y), max((z + w), (max(x, u) - y))) ||
 rewrite((max(max(((z + y) + w), x), u) - z), max((w + y), (max(x, u) - z))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((max(y, x) - (x + y)), (0 - min(x, y))) ||
 rewrite((max(y, x) - (y + x)), (0 - min(y, x))))))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite((select(x, 0, (y - z)) - y), (0 - select(x, y, z))) ||
 rewrite((select(x, y, (z + w)) - w), select(x, (y - w), z)) ||
 rewrite((select(x, y, (z + w)) - z), select(x, (y - z), w)) ||
 rewrite((select(x, y, (z*2)) - z), select(x, (y - z), z)) ||
 rewrite((select(x, (z + y), w) - y), select(x, z, (w - y))) ||
 rewrite((select(x, (z + y), w) - z), select(x, y, (w - z))) ||
 rewrite((select(x, (y - z), 0) - y), (0 - select(x, z, y))) ||
 rewrite((select(x, (y*2), z) - y), select(x, y, (z - y))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((select(x, y, z) - (y + z)), (0 - select(x, z, y))) ||
 rewrite((select(x, y, z) - (z + y)), (0 - select(x, z, y)))))))