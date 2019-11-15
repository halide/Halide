((b.node_type() == IRNodeType::Min) && (
 rewrite((x - min(max(y, z), min(u, max(w, z)))), (x - min(max(z, min(y, w)), u))) ||
 rewrite((x - min(max(y, z), min(u, max(z, w)))), (x - min(max(z, min(y, w)), u))) ||
 rewrite((x - min(max(y, z), min(max(w, z), u))), (x - min(max(z, min(w, y)), u))) ||
 rewrite((x - min(max(y, z), min(max(z, w), u))), (x - min(max(z, min(y, w)), u))) ||
 rewrite((x - min(max(z, y), min(u, max(w, z)))), (x - min(max(z, min(y, w)), u))) ||
 rewrite((x - min(max(z, y), min(u, max(z, w)))), (x - min(max(z, min(y, w)), u))) ||
 rewrite((x - min(max(z, y), min(max(w, z), u))), (x - min(max(z, min(w, y)), u))) ||
 rewrite((x - min(max(z, y), min(max(z, w), u))), (x - min(max(z, min(y, w)), u))))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(((x + (y + (w + z))) - z), (x + (y + w))) ||
 rewrite(((x + (y + (z + w))) - z), (x + (y + w))) ||
 rewrite(((x + ((w + z) + y)) - z), (x + (w + y))) ||
 rewrite(((x + ((z + w) + y)) - z), (x + (w + y))) ||
 rewrite((((y + (w + z)) + x) - z), (y + (w + x))) ||
 rewrite((((y + (z + w)) + x) - z), (y + (w + x))) ||
 rewrite(((((w + z) + y) + x) - z), ((w + y) + x)) ||
 rewrite(((((z + w) + y) + x) - z), ((w + y) + x)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((w + min(z, (x + y))) - (u + x)), ((w + min(y, (z - x))) - u)) ||
 rewrite(((w + min(z, (x + y))) - (x + u)), ((w + min(y, (z - x))) - u)) ||
 rewrite(((w + min(z, (y + x))) - (u + x)), ((w + min(y, (z - x))) - u)) ||
 rewrite(((w + min(z, (y + x))) - (x + u)), ((w + min(y, (z - x))) - u)) ||
 rewrite(((w + min((x + y), z)) - (u + x)), ((w + min((z - x), y)) - u)) ||
 rewrite(((w + min((x + y), z)) - (x + u)), ((w + min((z - x), y)) - u)) ||
 rewrite(((w + min((y + x), z)) - (u + x)), ((w + min((z - x), y)) - u)) ||
 rewrite(((w + min((y + x), z)) - (x + u)), ((w + min((z - x), y)) - u)) ||
 rewrite(((x + y) - (z + (w + x))), (y - (z + w))) ||
 rewrite(((x + y) - (z + (w + y))), (x - (z + w))) ||
 rewrite(((x + y) - (z + (x + w))), (y - (z + w))) ||
 rewrite(((x + y) - (z + (y + w))), (x - (z + w))) ||
 rewrite(((x + y) - ((w + x) + z)), (y - (w + z))) ||
 rewrite(((x + y) - ((w + y) + z)), (x - (w + z))) ||
 rewrite(((x + y) - ((x + w) + z)), (y - (w + z))) ||
 rewrite(((x + y) - ((y + w) + z)), (x - (w + z))) ||
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
 rewrite(((x + (y*z)) - (w*z)), (x + (z*(y - w)))) ||
 rewrite(((x + (y*z)) - (z*w)), (x + (z*(y - w)))) ||
 rewrite(((x + (z*y)) - (w*z)), (x + (z*(y - w)))) ||
 rewrite(((x + (z*y)) - (z*w)), (x + (z*(y - w)))) ||
 rewrite(((z + (x*y)) - (w*y)), (z + (y*(x - w)))) ||
 rewrite(((z + (x*y)) - (y*w)), (z + (y*(x - w)))) ||
 rewrite(((z + (y*x)) - (w*y)), (z + (y*(x - w)))) ||
 rewrite(((z + (y*x)) - (y*w)), (z + (y*(x - w)))) ||
 rewrite((((y - (z*c0)) + x) - (z*c0)), ((y - (z*fold((c0*2)))) + x)) ||
 rewrite((((x*y) + z) - (w*y)), (z + ((x - w)*y))) ||
 rewrite((((x*y) + z) - (y*w)), (z + ((x - w)*y))) ||
 rewrite((((y*x) + z) - (w*y)), (z + ((x - w)*y))) ||
 rewrite((((y*x) + z) - (y*w)), (z + ((x - w)*y))) ||
 rewrite((((y*z) + x) - (w*z)), (x + ((y - w)*z))) ||
 rewrite((((y*z) + x) - (z*w)), (x + ((y - w)*z))) ||
 rewrite((((z*y) + x) - (w*z)), (x + ((y - w)*z))) ||
 rewrite((((z*y) + x) - (z*w)), (x + ((y - w)*z))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite(((x + ((y + c0)/c1)) - (y/c1)), (x + (((y % c1) + c0)/c1))) ||
 rewrite(((x + ((y + z)/c0)) - (y/c0)), (x + (((y % c0) + z)/c0))) ||
 rewrite(((x + ((z + y)/c0)) - (y/c0)), (x + ((z + (y % c0))/c0))) ||
 rewrite(((y + ((x + c0)/c1)) - (x/c1)), (y + (((x % c1) + c0)/c1))) ||
 rewrite(((z + ((x + y)/c0)) - (x/c0)), (z + (((x % c0) + y)/c0))) ||
 rewrite(((z + ((y + x)/c0)) - (x/c0)), (z + ((y + (x % c0))/c0))) ||
 rewrite(((((x + c0)/c1) + y) - (x/c1)), (y + (((x % c1) + c0)/c1))) ||
 rewrite(((((x + y)/c0) + z) - (x/c0)), (z + (((x % c0) + y)/c0))) ||
 rewrite(((((y + c0)/c1) + x) - (y/c1)), (x + (((y % c1) + c0)/c1))) ||
 rewrite(((((y + x)/c0) + z) - (x/c0)), (z + (((x % c0) + y)/c0))) ||
 rewrite(((((y + z)/c0) + x) - (y/c0)), (x + (((y % c0) + z)/c0))) ||
 rewrite(((((z + y)/c0) + x) - (y/c0)), (x + (((y % c0) + z)/c0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(((x + y) - min(w, (y + z))), (x - min(z, (w - y)))) ||
 rewrite(((x + y) - min(w, (z + y))), (x - min(z, (w - y)))) ||
 rewrite(((x + y) - min((y + z), w)), (x - min((w - y), z))) ||
 rewrite(((x + y) - min((z + y), w)), (x - min((w - y), z))) ||
 rewrite(((y + x) - min(w, (y + z))), (x - min(z, (w - y)))) ||
 rewrite(((y + x) - min(w, (z + y))), (x - min(z, (w - y)))) ||
 rewrite(((y + x) - min((y + z), w)), (x - min((w - y), z))) ||
 rewrite(((y + x) - min((z + y), w)), (x - min((w - y), z))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(((x + y) - max(u, (w + (y + z)))), (x - max((u - y), (w + z)))) ||
 rewrite(((x + y) - max(u, (w + (z + y)))), (x - max((z + w), (u - y)))) ||
 rewrite(((x + y) - max(u, ((y + z) + w))), (x - max((u - y), (z + w)))) ||
 rewrite(((x + y) - max(u, ((z + y) + w))), (x - max((u - y), (z + w)))) ||
 rewrite(((x + y) - max(y, select(z, (w + y), y))), (x + min(0, select(z, (0 - w), 0)))) ||
 rewrite(((x + y) - max(y, select(z, (y + w), y))), (x + min(0, select(z, (0 - w), 0)))) ||
 rewrite(((x + y) - max(z, (u + (w + y)))), (x - max((w + u), (z - y)))) ||
 rewrite(((x + y) - max(z, (u + (y + w)))), (x - max((z - y), (u + w)))) ||
 rewrite(((x + y) - max(z, (w + x))), (y - max(w, (z - x)))) ||
 rewrite(((x + y) - max(z, (w + y))), (x - max(w, (z - y)))) ||
 rewrite(((x + y) - max(z, (x + w))), (y - max(w, (z - x)))) ||
 rewrite(((x + y) - max(z, (y + c0))), (x + min((y - z), fold((0 - c0))))) ||
 rewrite(((x + y) - max(z, (y + w))), (x - max(w, (z - y)))) ||
 rewrite(((x + y) - max(z, ((w + y) + u))), (x - max((z - y), (w + u)))) ||
 rewrite(((x + y) - max(z, ((y + w) + u))), (x - max((z - y), (w + u)))) ||
 rewrite(((x + y) - max((u + (w + y)), z)), (x - max((u + w), (z - y)))) ||
 rewrite(((x + y) - max((u + (y + w)), z)), (x - max((u + w), (z - y)))) ||
 rewrite(((x + y) - max((w + x), z)), (y - max((z - x), w))) ||
 rewrite(((x + y) - max((w + y), z)), (x - max((z - y), w))) ||
 rewrite(((x + y) - max((w + (y + z)), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((x + y) - max((w + (z + y)), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((x + y) - max((x + w), z)), (y - max((z - x), w))) ||
 rewrite(((x + y) - max((y + c0), z)), (x + min(fold((0 - c0)), (y - z)))) ||
 rewrite(((x + y) - max((y + w), z)), (x - max((z - y), w))) ||
 rewrite(((x + y) - max(((w + y) + u), z)), (x - max((w + u), (z - y)))) ||
 rewrite(((x + y) - max(((y + w) + u), z)), (x - max((u + w), (z - y)))) ||
 rewrite(((x + y) - max(((y + z) + w), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((x + y) - max(((z + y) + w), u)), (x - max((z + w), (u - y)))) ||
 rewrite(((x + y) - max(select(z, (w + y), y), y)), (x + min(select(z, (0 - w), 0), 0))) ||
 rewrite(((x + y) - max(select(z, (y + w), y), y)), (x + min(select(z, (0 - w), 0), 0))) ||
 rewrite(((x + (y*z)) - max(w, (u + (z*y)))), (x - max(u, (w - (z*y))))) ||
 rewrite(((x + (y*z)) - max(w, ((z*y) + u))), (x - max((w - (z*y)), u))) ||
 rewrite(((x + (y*z)) - max((u + (z*y)), w)), (x - max((w - (z*y)), u))) ||
 rewrite(((x + (y*z)) - max(((z*y) + u), w)), (x - max((w - (z*y)), u))) ||
 rewrite(((x + (z*y)) - max(w, (u + (y*z)))), (x - max(u, (w - (y*z))))) ||
 rewrite(((x + (z*y)) - max(w, ((y*z) + u))), (x - max((w - (y*z)), u))) ||
 rewrite(((x + (z*y)) - max((u + (y*z)), w)), (x - max((w - (y*z)), u))) ||
 rewrite(((x + (z*y)) - max(((y*z) + u), w)), (x - max((w - (y*z)), u))) ||
 rewrite(((x + min(y, z)) - max(w, (u + min(z, y)))), (x - max(u, (w - min(z, y))))) ||
 rewrite(((x + min(y, z)) - max(w, (min(z, y) + u))), (x - max(u, (w - min(z, y))))) ||
 rewrite(((x + min(y, z)) - max((u + min(z, y)), w)), (x - max((w - min(z, y)), u))) ||
 rewrite(((x + min(y, z)) - max((min(z, y) + u), w)), (x - max(u, (w - min(z, y))))) ||
 rewrite(((x + min(z, y)) - max(w, (u + min(y, z)))), (x - max(u, (w - min(y, z))))) ||
 rewrite(((x + min(z, y)) - max(w, (min(y, z) + u))), (x - max(u, (w - min(y, z))))) ||
 rewrite(((x + min(z, y)) - max((u + min(y, z)), w)), (x - max((w - min(y, z)), u))) ||
 rewrite(((x + min(z, y)) - max((min(y, z) + u), w)), (x - max(u, (w - min(y, z))))) ||
 rewrite(((y + x) - max(u, (w + (y + z)))), (x - max((u - y), (w + z)))) ||
 rewrite(((y + x) - max(u, (w + (z + y)))), (x - max((z + w), (u - y)))) ||
 rewrite(((y + x) - max(u, ((y + z) + w))), (x - max((u - y), (z + w)))) ||
 rewrite(((y + x) - max(u, ((z + y) + w))), (x - max((u - y), (z + w)))) ||
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
 rewrite(((y + x) - max((u + (w + y)), z)), (x - max((u + w), (z - y)))) ||
 rewrite(((y + x) - max((u + (y + w)), z)), (x - max((u + w), (z - y)))) ||
 rewrite(((y + x) - max((w + x), z)), (y - max((z - x), w))) ||
 rewrite(((y + x) - max((w + y), z)), (x - max((z - y), w))) ||
 rewrite(((y + x) - max((w + (y + z)), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((y + x) - max((w + (z + y)), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((y + x) - max((x + w), z)), (y - max((z - x), w))) ||
 rewrite(((y + x) - max((y + w), z)), (x - max((z - y), w))) ||
 rewrite(((y + x) - max(((w + y) + u), z)), (x - max((w + u), (z - y)))) ||
 rewrite(((y + x) - max(((y + w) + u), z)), (x - max((u + w), (z - y)))) ||
 rewrite(((y + x) - max(((y + z) + w), u)), (x - max((w + z), (u - y)))) ||
 rewrite(((y + x) - max(((z + y) + w), u)), (x - max((z + w), (u - y)))) ||
 rewrite(((y + x) - max(select(z, (w + y), y), y)), (x + min(select(z, (0 - w), 0), 0))) ||
 rewrite(((y + x) - max(select(z, (y + w), y), y)), (x + min(select(z, (0 - w), 0), 0))) ||
 rewrite((((y*z) + x) - max(w, (u + (z*y)))), (x - max(u, (w - (z*y))))) ||
 rewrite((((y*z) + x) - max(w, ((z*y) + u))), (x - max((w - (z*y)), u))) ||
 rewrite((((y*z) + x) - max((u + (z*y)), w)), (x - max((w - (z*y)), u))) ||
 rewrite((((y*z) + x) - max(((z*y) + u), w)), (x - max((w - (z*y)), u))) ||
 rewrite((((z*y) + x) - max(w, (u + (y*z)))), (x - max(u, (w - (y*z))))) ||
 rewrite((((z*y) + x) - max(w, ((y*z) + u))), (x - max((w - (y*z)), u))) ||
 rewrite((((z*y) + x) - max((u + (y*z)), w)), (x - max((w - (y*z)), u))) ||
 rewrite((((z*y) + x) - max(((y*z) + u), w)), (x - max((w - (y*z)), u))) ||
 rewrite(((min(y, z) + x) - max(w, (u + min(z, y)))), (x - max(u, (w - min(z, y))))) ||
 rewrite(((min(y, z) + x) - max(w, (min(z, y) + u))), (x - max((w - min(z, y)), u))) ||
 rewrite(((min(y, z) + x) - max((u + min(z, y)), w)), (x - max((w - min(z, y)), u))) ||
 rewrite(((min(y, z) + x) - max((min(z, y) + u), w)), (x - max((w - min(z, y)), u))) ||
 rewrite(((min(z, y) + x) - max(w, (u + min(y, z)))), (x - max(u, (w - min(y, z))))) ||
 rewrite(((min(z, y) + x) - max(w, (min(y, z) + u))), (x - max((w - min(y, z)), u))) ||
 rewrite(((min(z, y) + x) - max((u + min(y, z)), w)), (x - max((w - min(y, z)), u))) ||
 rewrite(((min(z, y) + x) - max((min(y, z) + u), w)), (x - max((w - min(y, z)), u))))))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite((((x + y) - z) - y), (x - z)) ||
 rewrite((((y + x) - z) - y), (x - z)) ||
 rewrite((((x - y) - y) - y), (x - (3*y))) ||
 rewrite((((x - y) - z) - x), ((0 - y) - z)) ||
 rewrite(((((x + y) - z) - w) - y), ((x - z) - w)) ||
 rewrite(((((y + x) - z) - w) - y), ((x - z) - w)) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((x - (y*c0)) - (y*c0)), (x - (y*fold((c0*2))))))))) ||
((a.node_type() == IRNodeType::Mul) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*y) - (w + (y*z))), ((y*(x - z)) - w)) ||
 rewrite(((x*y) - (w + (z*y))), ((y*(x - z)) - w)) ||
 rewrite(((x*y) - (z + (w*y))), ((y*(x - w)) - z)) ||
 rewrite(((x*y) - (z + (y*w))), ((y*(x - w)) - z)) ||
 rewrite(((x*y) - ((w*y) + z)), ((y*(x - w)) - z)) ||
 rewrite(((x*y) - ((y*w) + z)), ((y*(x - w)) - z)) ||
 rewrite(((x*y) - ((y*z) + w)), ((y*(x - z)) - w)) ||
 rewrite(((x*y) - ((z*y) + w)), ((y*(x - z)) - w)) ||
 rewrite(((y*x) - (w + (y*z))), ((y*(x - z)) - w)) ||
 rewrite(((y*x) - (w + (z*y))), ((y*(x - z)) - w)) ||
 rewrite(((y*x) - (z + (w*y))), ((y*(x - w)) - z)) ||
 rewrite(((y*x) - (z + (y*w))), ((y*(x - w)) - z)) ||
 rewrite(((y*x) - ((w*y) + z)), ((y*(x - w)) - z)) ||
 rewrite(((y*x) - ((y*w) + z)), ((y*(x - w)) - z)) ||
 rewrite(((y*x) - ((y*z) + w)), ((y*(x - z)) - w)) ||
 rewrite(((y*x) - ((z*y) + w)), ((y*(x - z)) - w)))))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite((min(w, (z + (x + y))) - y), min((z + x), (w - y))) ||
 rewrite((min(w, (z + (y + x))) - y), min((z + x), (w - y))) ||
 rewrite((min(w, ((x + y) + z)) - y), min((x + z), (w - y))) ||
 rewrite((min(w, ((y + x) + z)) - y), min((z + x), (w - y))) ||
 rewrite((min((z + (x + y)), w) - y), min((z + x), (w - y))) ||
 rewrite((min((z + (y + x)), w) - y), min((z + x), (w - y))) ||
 rewrite((min(((x + y) + z), w) - y), min((x + z), (w - y))) ||
 rewrite((min(((y + x) + z), w) - y), min((x + z), (w - y))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(w, (z + (x + y))) - (u + (x + y))), (min(z, (w - (x + y))) - u)) ||
 rewrite((min(w, (z + (x + y))) - (u + (y + x))), (min(z, (w - (x + y))) - u)) ||
 rewrite((min(w, (z + (x + y))) - ((x + y) + u)), (min(z, (w - (x + y))) - u)) ||
 rewrite((min(w, (z + (x + y))) - ((y + x) + u)), (min(z, (w - (x + y))) - u)) ||
 rewrite((min(w, (z + (y + x))) - (u + (x + y))), (min(z, (w - (y + x))) - u)) ||
 rewrite((min(w, (z + (y + x))) - (u + (y + x))), (min(z, (w - (y + x))) - u)) ||
 rewrite((min(w, (z + (y + x))) - ((x + y) + u)), (min(z, (w - (y + x))) - u)) ||
 rewrite((min(w, (z + (y + x))) - ((y + x) + u)), (min(z, (w - (y + x))) - u)) ||
 rewrite((min(w, ((x + y) + z)) - (u + (x + y))), (min(z, ((w - y) - x)) - u)) ||
 rewrite((min(w, ((x + y) + z)) - (u + (y + x))), (min(z, ((w - y) - x)) - u)) ||
 rewrite((min(w, ((x + y) + z)) - ((x + y) + u)), (min(z, ((w - y) - x)) - u)) ||
 rewrite((min(w, ((x + y) + z)) - ((y + x) + u)), (min(z, ((w - y) - x)) - u)) ||
 rewrite((min(w, ((y + x) + z)) - (u + (x + y))), (min(z, ((w - x) - y)) - u)) ||
 rewrite((min(w, ((y + x) + z)) - (u + (y + x))), (min(z, ((w - x) - y)) - u)) ||
 rewrite((min(w, ((y + x) + z)) - ((x + y) + u)), (min(z, ((w - x) - y)) - u)) ||
 rewrite((min(w, ((y + x) + z)) - ((y + x) + u)), (min(z, ((w - x) - y)) - u)) ||
 rewrite((min(x, (w + (y + z))) - (u + y)), (min((x - y), (w + z)) - u)) ||
 rewrite((min(x, (w + (y + z))) - (y + u)), (min((x - y), (w + z)) - u)) ||
 rewrite((min(x, (w + (z + y))) - (u + y)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(x, (w + (z + y))) - (y + u)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(x, (w + (y*z))) - (u + (y*z))), (min(w, (x - (y*z))) - u)) ||
 rewrite((min(x, (w + (y*z))) - (u + (z*y))), (min(w, (x - (y*z))) - u)) ||
 rewrite((min(x, (w + (y*z))) - ((y*z) + u)), (min(w, (x - (y*z))) - u)) ||
 rewrite((min(x, (w + (y*z))) - ((z*y) + u)), (min(w, (x - (y*z))) - u)) ||
 rewrite((min(x, (w + (z*y))) - (u + (y*z))), (min(w, (x - (z*y))) - u)) ||
 rewrite((min(x, (w + (z*y))) - (u + (z*y))), (min(w, (x - (z*y))) - u)) ||
 rewrite((min(x, (w + (z*y))) - ((y*z) + u)), (min(w, (x - (z*y))) - u)) ||
 rewrite((min(x, (w + (z*y))) - ((z*y) + u)), (min(w, (x - (z*y))) - u)) ||
 rewrite((min(x, (y + c0)) - (y + z)), (min(c0, (x - y)) - z)) ||
 rewrite((min(x, (y + c0)) - (z + y)), (min(c0, (x - y)) - z)) ||
 rewrite((min(x, (y + z)) - (w + y)), (min(z, (x - y)) - w)) ||
 rewrite((min(x, (y + z)) - (y + w)), (min(z, (x - y)) - w)) ||
 rewrite((min(x, (z + y)) - (w + y)), (min(z, (x - y)) - w)) ||
 rewrite((min(x, (z + y)) - (y + w)), (min(z, (x - y)) - w)) ||
 rewrite((min(x, ((y + z) + w)) - (u + y)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((y + z) + w)) - (y + u)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((z + y) + w)) - (u + y)), (min((x - y), (z + w)) - u)) ||
 rewrite((min(x, ((z + y) + w)) - (y + u)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(x, ((y*z) + w)) - (u + (z*y))), (min((x - (y*z)), w) - u)) ||
 rewrite((min(x, ((y*z) + w)) - ((z*y) + u)), (min(w, (x - (z*y))) - u)) ||
 rewrite((min(x, ((z*y) + w)) - (u + (y*z))), (min((x - (z*y)), w) - u)) ||
 rewrite((min(x, ((z*y) + w)) - ((y*z) + u)), (min(w, (x - (y*z))) - u)) ||
 rewrite((min(y, (x + c0)) - (x + z)), (min(c0, (y - x)) - z)) ||
 rewrite((min(y, (x + c0)) - (z + x)), (min(c0, (y - x)) - z)) ||
 rewrite((min(z, (x + y)) - (w + x)), (min(y, (z - x)) - w)) ||
 rewrite((min(z, (x + y)) - (x + w)), (min(y, (z - x)) - w)) ||
 rewrite((min(z, (y + x)) - (w + x)), (min(y, (z - x)) - w)) ||
 rewrite((min(z, (y + x)) - (x + w)), (min(y, (z - x)) - w)) ||
 rewrite((min((w + (y + z)), x) - (u + y)), (min((w + z), (x - y)) - u)) ||
 rewrite((min((w + (y + z)), x) - (y + u)), (min((w + z), (x - y)) - u)) ||
 rewrite((min((w + (z + y)), x) - (u + y)), (min((w + z), (x - y)) - u)) ||
 rewrite((min((w + (z + y)), x) - (y + u)), (min((w + z), (x - y)) - u)) ||
 rewrite((min((w + (y*z)), x) - (u + (y*z))), (min((x - (y*z)), w) - u)) ||
 rewrite((min((w + (y*z)), x) - (u + (z*y))), (min((x - (y*z)), w) - u)) ||
 rewrite((min((w + (y*z)), x) - ((y*z) + u)), (min((x - (y*z)), w) - u)) ||
 rewrite((min((w + (y*z)), x) - ((z*y) + u)), (min((x - (y*z)), w) - u)) ||
 rewrite((min((w + (z*y)), x) - (u + (y*z))), (min((x - (z*y)), w) - u)) ||
 rewrite((min((w + (z*y)), x) - (u + (z*y))), (min((x - (z*y)), w) - u)) ||
 rewrite((min((w + (z*y)), x) - ((y*z) + u)), (min((x - (z*y)), w) - u)) ||
 rewrite((min((w + (z*y)), x) - ((z*y) + u)), (min((x - (z*y)), w) - u)) ||
 rewrite((min((x + c0), y) - (x + z)), (min((y - x), c0) - z)) ||
 rewrite((min((x + c0), y) - (z + x)), (min((y - x), c0) - z)) ||
 rewrite((min((x + y), z) - (w + x)), (min((z - x), y) - w)) ||
 rewrite((min((x + y), z) - (x + w)), (min((z - x), y) - w)) ||
 rewrite((min((y + c0), x) - (y + z)), (min((x - y), c0) - z)) ||
 rewrite((min((y + c0), x) - (z + y)), (min((x - y), c0) - z)) ||
 rewrite((min((y + x), z) - (w + x)), (min((z - x), y) - w)) ||
 rewrite((min((y + x), z) - (x + w)), (min((z - x), y) - w)) ||
 rewrite((min((y + z), x) - (w + y)), (min((x - y), z) - w)) ||
 rewrite((min((y + z), x) - (y + w)), (min((x - y), z) - w)) ||
 rewrite((min((z + y), x) - (w + y)), (min((x - y), z) - w)) ||
 rewrite((min((z + y), x) - (y + w)), (min((x - y), z) - w)) ||
 rewrite((min((z + (x + y)), w) - (u + (y + x))), (min((w - (x + y)), z) - u)) ||
 rewrite((min((z + (x + y)), w) - ((y + x) + u)), (min((w - (x + y)), z) - u)) ||
 rewrite((min((z + (y + x)), w) - (u + (x + y))), (min((w - (y + x)), z) - u)) ||
 rewrite((min((z + (y + x)), w) - ((x + y) + u)), (min((w - (y + x)), z) - u)) ||
 rewrite((min(((x + y) + z), w) - (u + (y + x))), (min(((w - y) - x), z) - u)) ||
 rewrite((min(((x + y) + z), w) - ((y + x) + u)), (min(((w - y) - x), z) - u)) ||
 rewrite((min(((y + x) + z), w) - (u + (x + y))), (min(((w - x) - y), z) - u)) ||
 rewrite((min(((y + x) + z), w) - ((x + y) + u)), (min(((w - x) - y), z) - u)) ||
 rewrite((min(((y + z) + w), x) - (u + y)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(((y + z) + w), x) - (y + u)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(((z + y) + w), x) - (u + y)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(((z + y) + w), x) - (y + u)), (min((z + w), (x - y)) - u)) ||
 rewrite((min(((y*z) + w), x) - (u + (z*y))), (min((x - (y*z)), w) - u)) ||
 rewrite((min(((y*z) + w), x) - ((z*y) + u)), (min(w, (x - (z*y))) - u)) ||
 rewrite((min(((z*y) + w), x) - (u + (y*z))), (min((x - (z*y)), w) - u)) ||
 rewrite((min(((z*y) + w), x) - ((y*z) + u)), (min(w, (x - (y*z))) - u)))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max(u, max(w, (z + (x + y)))) - y), max((z + x), (max(w, u) - y))) ||
 rewrite((max(u, max(w, (z + (y + x)))) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(u, max(w, ((x + y) + z))) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(u, max(w, ((y + x) + z))) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(u, max(x, (w + (y + z)))) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(x, (w + (y + z)))) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(u, max(x, (w + (z + y)))) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(x, (w + (z + y)))) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(u, max(x, ((y + z) + w))) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(x, ((y + z) + w))) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(u, max(x, ((z + y) + w))) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(x, ((z + y) + w))) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(u, max((w + (y + z)), x)) - y), max((z + w), (max(u, x) - y))) ||
 rewrite((max(u, max((w + (y + z)), x)) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(u, max((w + (z + y)), x)) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max((w + (z + y)), x)) - z), max((y + w), (max(u, x) - z))) ||
 rewrite((max(u, max((z + (x + y)), w)) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(u, max((z + (y + x)), w)) - y), max((z + x), (max(u, w) - y))) ||
 rewrite((max(u, max(((x + y) + z), w)) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(u, max(((y + x) + z), w)) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(u, max(((y + z) + w), x)) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(((y + z) + w), x)) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(u, max(((z + y) + w), x)) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(u, max(((z + y) + w), x)) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(w, (z + (x + y))) - x), max((z + y), (w - x))) ||
 rewrite((max(w, (z + (y + x))) - x), max((z + y), (w - x))) ||
 rewrite((max(w, ((x + y) + z)) - x), max((z + y), (w - x))) ||
 rewrite((max(w, ((y + x) + z)) - x), max((y + z), (w - x))) ||
 rewrite((max(w, max(x, (y + z))) - y), max(z, (max(x, w) - y))) ||
 rewrite((max(w, max(x, (y + z))) - z), max(y, (max(x, w) - z))) ||
 rewrite((max(w, max(x, (z + y))) - y), max(z, (max(x, w) - y))) ||
 rewrite((max(w, max(x, (z + y))) - z), max(y, (max(x, w) - z))) ||
 rewrite((max(w, max(z, (x + y))) - x), max(y, (max(z, w) - x))) ||
 rewrite((max(w, max(z, (x + y))) - y), max(x, (max(z, w) - y))) ||
 rewrite((max(w, max(z, (y + x))) - x), max(y, (max(z, w) - x))) ||
 rewrite((max(w, max(z, (y + x))) - y), max(x, (max(z, w) - y))) ||
 rewrite((max(w, max((x + y), z)) - x), max(y, (max(w, z) - x))) ||
 rewrite((max(w, max((x + y), z)) - y), max(x, (max(w, z) - y))) ||
 rewrite((max(w, max((y + x), z)) - x), max(y, (max(w, z) - x))) ||
 rewrite((max(w, max((y + x), z)) - y), max(x, (max(w, z) - y))) ||
 rewrite((max(w, max((y + z), x)) - y), max(z, (max(w, x) - y))) ||
 rewrite((max(w, max((y + z), x)) - z), max(y, (max(w, x) - z))) ||
 rewrite((max(w, max((z + y), x)) - y), max(z, (max(w, x) - y))) ||
 rewrite((max(w, max((z + y), x)) - z), max(y, (max(w, x) - z))) ||
 rewrite((max(x, (w + (y + z))) - y), max((w + z), (x - y))) ||
 rewrite((max(x, (w + (y + z))) - z), max((w + y), (x - z))) ||
 rewrite((max(x, (w + (z + y))) - y), max((w + z), (x - y))) ||
 rewrite((max(x, (w + (z + y))) - z), max((w + y), (x - z))) ||
 rewrite((max(x, ((y + z) + w)) - y), max((w + z), (x - y))) ||
 rewrite((max(x, ((y + z) + w)) - z), max((y + w), (x - z))) ||
 rewrite((max(x, ((z + y) + w)) - y), max((z + w), (x - y))) ||
 rewrite((max(x, ((z + y) + w)) - z), max((w + y), (x - z))) ||
 rewrite((max((w + (y + z)), x) - y), max((w + z), (x - y))) ||
 rewrite((max((w + (y + z)), x) - z), max((w + y), (x - z))) ||
 rewrite((max((w + (z + y)), x) - y), max((w + z), (x - y))) ||
 rewrite((max((w + (z + y)), x) - z), max((w + y), (x - z))) ||
 rewrite((max((z + (x + y)), w) - x), max((z + y), (w - x))) ||
 rewrite((max((z + (y + x)), w) - x), max((z + y), (w - x))) ||
 rewrite((max(((x + y) + z), w) - x), max((y + z), (w - x))) ||
 rewrite((max(((y + x) + z), w) - x), max((y + z), (w - x))) ||
 rewrite((max(((y + z) + w), x) - y), max((z + w), (x - y))) ||
 rewrite((max(((y + z) + w), x) - z), max((y + w), (x - z))) ||
 rewrite((max(((z + y) + w), x) - y), max((z + w), (x - y))) ||
 rewrite((max(((z + y) + w), x) - z), max((y + w), (x - z))) ||
 rewrite((max(max(w, (z + (x + y))), u) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(max(w, (z + (y + x))), u) - y), max((x + z), (max(w, u) - y))) ||
 rewrite((max(max(w, ((x + y) + z)), u) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(max(w, ((y + x) + z)), u) - y), max((max(w, u) - y), (z + x))) ||
 rewrite((max(max(x, (w + (y + z))), u) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(max(x, (w + (y + z))), u) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(max(x, (w + (z + y))), u) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(max(x, (w + (z + y))), u) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(max(x, (y + z)), w) - y), max((max(x, w) - y), z)) ||
 rewrite((max(max(x, (y + z)), w) - z), max((max(x, w) - z), y)) ||
 rewrite((max(max(x, (z + y)), w) - y), max((max(x, w) - y), z)) ||
 rewrite((max(max(x, (z + y)), w) - z), max((max(x, w) - z), y)) ||
 rewrite((max(max(x, ((y + z) + w)), u) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(max(x, ((y + z) + w)), u) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(max(x, ((z + y) + w)), u) - y), max((max(x, u) - y), (z + w))) ||
 rewrite((max(max(x, ((z + y) + w)), u) - z), max((max(x, u) - z), (y + w))) ||
 rewrite((max(max(z, (x + y)), w) - x), max((max(z, w) - x), y)) ||
 rewrite((max(max(z, (x + y)), w) - y), max((max(z, w) - y), x)) ||
 rewrite((max(max(z, (y + x)), w) - x), max((max(z, w) - x), y)) ||
 rewrite((max(max(z, (y + x)), w) - y), max(x, (max(z, w) - y))) ||
 rewrite((max(max((w + (y + z)), x), u) - y), max((w + z), (max(x, u) - y))) ||
 rewrite((max(max((w + (y + z)), x), u) - z), max((w + y), (max(x, u) - z))) ||
 rewrite((max(max((w + (z + y)), x), u) - y), max((w + z), (max(x, u) - y))) ||
 rewrite((max(max((w + (z + y)), x), u) - z), max((w + y), (max(x, u) - z))) ||
 rewrite((max(max((x + y), z), w) - x), max(y, (max(z, w) - x))) ||
 rewrite((max(max((x + y), z), w) - y), max(x, (max(z, w) - y))) ||
 rewrite((max(max((y + x), z), w) - x), max(y, (max(z, w) - x))) ||
 rewrite((max(max((y + x), z), w) - y), max(x, (max(z, w) - y))) ||
 rewrite((max(max((y + z), x), w) - y), max(z, (max(x, w) - y))) ||
 rewrite((max(max((y + z), x), w) - z), max(y, (max(x, w) - z))) ||
 rewrite((max(max((z + y), x), w) - y), max(z, (max(x, w) - y))) ||
 rewrite((max(max((z + y), x), w) - z), max(y, (max(x, w) - z))) ||
 rewrite((max(max((z + (x + y)), w), u) - y), max((z + x), (max(w, u) - y))) ||
 rewrite((max(max((z + (y + x)), w), u) - y), max((z + x), (max(w, u) - y))) ||
 rewrite((max(max(((x + y) + z), w), u) - y), max((x + z), (max(w, u) - y))) ||
 rewrite((max(max(((y + x) + z), w), u) - y), max((z + x), (max(w, u) - y))) ||
 rewrite((max(max(((y + z) + w), x), u) - y), max((w + z), (max(x, u) - y))) ||
 rewrite((max(max(((y + z) + w), x), u) - z), max((y + w), (max(x, u) - z))) ||
 rewrite((max(max(((z + y) + w), x), u) - y), max((z + w), (max(x, u) - y))) ||
 rewrite((max(max(((z + y) + w), x), u) - z), max((w + y), (max(x, u) - z)))))