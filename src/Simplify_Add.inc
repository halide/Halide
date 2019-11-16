((b.node_type() == IRNodeType::Add) && (
 rewrite((y + (w + min(z, (x - y)))), (w + min(x, (z + y)))) ||
 rewrite((y + (w + min((x - y), z))), (w + min((z + y), x))) ||
 rewrite((y + (z + (y + (x*2)))), (z + ((y + x)*2))) ||
 rewrite((y + (z + ((x*2) + y))), (z + (2*(x + y)))) ||
 rewrite((y + (z + (y*((x + c0)/c1)))), (z + (((x + fold((c0 + c1)))/c1)*y))) ||
 rewrite((y + (z + (((x + c0)/c1)*y))), (z + (((x + fold((c0 + c1)))/c1)*y))) ||
 rewrite((y + ((y + (x*2)) + z)), (((y + x)*2) + z)) ||
 rewrite((y + (((x*2) + y) + z)), (z + ((x + y)*2))) ||
 rewrite((y + ((y*((x + c0)/c1)) + z)), ((y*((x + fold((c0 + c1)))/c1)) + z)) ||
 rewrite((y + ((((x + c0)/c1)*y) + z)), ((((x + fold((c0 + c1)))/c1)*y) + z)) ||
 rewrite((y + (min(z, (x - y)) + w)), (min(x, (z + y)) + w)) ||
 rewrite((y + (min((x - y), c0) + z)), (z + min((y + c0), x))) ||
 rewrite((y + (min((x - y), z) + w)), (min((z + y), x) + w)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((u + (min(x, (y + z)) - (w + y))), (u + (min(z, (x - y)) - w))) ||
 rewrite((u + (min(x, (y + z)) - (y + w))), (u + (min(z, (x - y)) - w))) ||
 rewrite((u + (min(x, (z + y)) - (w + y))), (u + (min(z, (x - y)) - w))) ||
 rewrite((u + (min(x, (z + y)) - (y + w))), (u + (min(z, (x - y)) - w))) ||
 rewrite((u + (min((y + z), x) - (w + y))), (u + (min((x - y), z) - w))) ||
 rewrite((u + (min((y + z), x) - (y + w))), (u + (min((x - y), z) - w))) ||
 rewrite((u + (min((z + y), x) - (w + y))), (u + (min((x - y), z) - w))) ||
 rewrite((u + (min((z + y), x) - (y + w))), (u + (min((x - y), z) - w))))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((y + (y*((x + c0)/c1))), (((x + fold((c0 + c1)))/c1)*y)) ||
 rewrite((y + (((x + c0)/c1)*y)), (((x + fold((c0 + c1)))/c1)*y)) ||
 rewrite((z + (z*min(x, (y + -1)))), (z*min(y, (x + 1)))) ||
 rewrite((z + (z*min(y, (x + -1)))), (z*min(x, (y + 1)))) ||
 rewrite((z + (z*min((x + -1), y))), (z*min((y + 1), x))) ||
 rewrite((z + (z*min((y + -1), x))), (z*min((x + 1), y))) ||
 rewrite((z + (min(x, (y + -1))*z)), (min(y, (x + 1))*z)) ||
 rewrite((z + (min(y, (x + -1))*z)), (min(x, (y + 1))*z)) ||
 rewrite((z + (min((x + -1), y)*z)), (min((y + 1), x)*z)) ||
 rewrite((z + (min((y + -1), x)*z)), (min((x + 1), y)*z)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((u + min(x, ((y - z) + (w + z)))), (u + min(x, (y + w)))) ||
 rewrite((u + min(x, ((y - z) + (z + w)))), (u + min(x, (y + w)))) ||
 rewrite((u + min(((y - z) + (w + z)), x)), (u + min((y + w), x))) ||
 rewrite((u + min(((y - z) + (z + w)), x)), (u + min((y + w), x))) ||
 rewrite((w + min(u, ((x + y) - (w + z)))), min(((x + y) - z), (w + u))) ||
 rewrite((w + min(u, ((x + y) - (z + w)))), min(((x + y) - z), (u + w))) ||
 rewrite((w + min(u, ((y + x) - (w + z)))), min(((y + x) - z), (w + u))) ||
 rewrite((w + min(u, ((y + x) - (z + w)))), min(((y + x) - z), (u + w))) ||
 rewrite((w + min(u, ((x*y) - (w + z)))), min(((x*y) - z), (w + u))) ||
 rewrite((w + min(u, ((x*y) - (z + w)))), min(((x*y) - z), (u + w))) ||
 rewrite((w + min(u, ((y*x) - (w + z)))), min(((y*x) - z), (w + u))) ||
 rewrite((w + min(u, ((y*x) - (z + w)))), min(((y*x) - z), (u + w))) ||
 rewrite((w + min(u, (min(x, y) - (w + z)))), min((min(x, y) - z), (w + u))) ||
 rewrite((w + min(u, (min(x, y) - (z + w)))), min((min(x, y) - z), (u + w))) ||
 rewrite((w + min(u, (min(y, x) - (w + z)))), min((min(y, x) - z), (w + u))) ||
 rewrite((w + min(u, (min(y, x) - (z + w)))), min((min(y, x) - z), (u + w))) ||
 rewrite((w + min(z, (x - (w + (y*z))))), min((w + z), (x - (y*z)))) ||
 rewrite((w + min(z, (x - (w + (z*y))))), min((w + z), (x - (z*y)))) ||
 rewrite((w + min(z, (x - ((y*z) + w)))), min((x - (y*z)), (w + z))) ||
 rewrite((w + min(z, (x - ((z*y) + w)))), min((w + z), (x - (z*y)))) ||
 rewrite((w + min((x - (w + (y*z))), z)), min((x - (y*z)), (z + w))) ||
 rewrite((w + min((x - (w + (z*y))), z)), min((x - (z*y)), (z + w))) ||
 rewrite((w + min((x - ((y*z) + w)), z)), min((x - (y*z)), (w + z))) ||
 rewrite((w + min((x - ((z*y) + w)), z)), min((x - (z*y)), (w + z))) ||
 rewrite((w + min(((x + y) - (w + z)), u)), min(((x + y) - z), (w + u))) ||
 rewrite((w + min(((x + y) - (z + w)), u)), min(((x + y) - z), (w + u))) ||
 rewrite((w + min(((y + x) - (w + z)), u)), min(((y + x) - z), (w + u))) ||
 rewrite((w + min(((y + x) - (z + w)), u)), min(((y + x) - z), (w + u))) ||
 rewrite((w + min(((x*y) - (w + z)), u)), min(((x*y) - z), (w + u))) ||
 rewrite((w + min(((x*y) - (z + w)), u)), min(((x*y) - z), (w + u))) ||
 rewrite((w + min(((y*x) - (w + z)), u)), min(((y*x) - z), (w + u))) ||
 rewrite((w + min(((y*x) - (z + w)), u)), min(((y*x) - z), (w + u))) ||
 rewrite((w + min((min(x, y) - (w + z)), u)), min((min(x, y) - z), (w + u))) ||
 rewrite((w + min((min(x, y) - (z + w)), u)), min((min(x, y) - z), (w + u))) ||
 rewrite((w + min((min(y, x) - (w + z)), u)), min((min(y, x) - z), (w + u))) ||
 rewrite((w + min((min(y, x) - (z + w)), u)), min((min(y, x) - z), (w + u))) ||
 rewrite((y + min(u, min(w, (z + (x - y))))), min((y + min(u, w)), (z + x))) ||
 rewrite((y + min(u, min(w, ((x - y) + z)))), min((y + min(u, w)), (x + z))) ||
 rewrite((y + min(u, min(w, (x - (y + z))))), min((y + min(u, w)), (x - z))) ||
 rewrite((y + min(u, min(w, (x - (z + y))))), min((y + min(u, w)), (x - z))) ||
 rewrite((y + min(u, min(w, min(z, (x - y))))), min(x, (min(w, min(z, u)) + y))) ||
 rewrite((y + min(u, min(w, min((x - y), z)))), min((min(u, min(w, z)) + y), x)) ||
 rewrite((y + min(u, min((z + (x - y)), w))), min((y + min(u, w)), (x + z))) ||
 rewrite((y + min(u, min(((x - y) + z), w))), min((y + min(u, w)), (x + z))) ||
 rewrite((y + min(u, min((x - (y + z)), w))), min((x - z), (y + min(u, w)))) ||
 rewrite((y + min(u, min((x - (z + y)), w))), min((y + min(u, w)), (x - z))) ||
 rewrite((y + min(u, min(min(z, (x - y)), w))), min(x, (min(min(z, u), w) + y))) ||
 rewrite((y + min(u, min(min((x - y), z), w))), min(x, (min(min(u, w), z) + y))) ||
 rewrite((y + min(w, (z + (x - y)))), min((x + z), (w + y))) ||
 rewrite((y + min(w, ((x - y) + z))), min((y + w), (x + z))) ||
 rewrite((y + min(w, (x - (y + z)))), min((x - z), (y + w))) ||
 rewrite((y + min(w, (x - (z + y)))), min((x - z), (w + y))) ||
 rewrite((y + min(w, min(z, (x - y)))), min(x, (min(z, w) + y))) ||
 rewrite((y + min(w, min((x - y), z))), min((min(w, z) + y), x)) ||
 rewrite((y + min((z + (x - y)), w)), min((z + x), (y + w))) ||
 rewrite((y + min(((x - y) + z), w)), min((w + y), (z + x))) ||
 rewrite((y + min((x - (y + z)), c0)), min((x - z), (y + c0))) ||
 rewrite((y + min((x - (y + z)), w)), min((x - z), (y + w))) ||
 rewrite((y + min((x - (z + y)), w)), min((x - z), (y + w))) ||
 rewrite((y + min(min(w, (z + (x - y))), u)), min((min(w, u) + y), (x + z))) ||
 rewrite((y + min(min(w, ((x - y) + z)), u)), min((min(w, u) + y), (x + z))) ||
 rewrite((y + min(min(w, (x - (y + z))), u)), min((min(w, u) + y), (x - z))) ||
 rewrite((y + min(min(w, (x - (z + y))), u)), min((min(w, u) + y), (x - z))) ||
 rewrite((y + min(min(w, min(z, (x - y))), u)), min((min(w, min(z, u)) + y), x)) ||
 rewrite((y + min(min(w, min((x - y), z)), u)), min((y + min(min(w, u), z)), x)) ||
 rewrite((y + min(min(z, (x - y)), w)), min((min(z, w) + y), x)) ||
 rewrite((y + min(min((z + (x - y)), w), u)), min((min(w, u) + y), (z + x))) ||
 rewrite((y + min(min(((x - y) + z), w), u)), min((min(u, w) + y), (x + z))) ||
 rewrite((y + min(min((x - y), z), w)), min((min(w, z) + y), x)) ||
 rewrite((y + min(min((x - (y + z)), w), u)), min((min(u, w) + y), (x - z))) ||
 rewrite((y + min(min((x - (z + y)), w), u)), min((min(u, w) + y), (x - z))) ||
 rewrite((y + min(min(min(z, (x - y)), w), u)), min((min(min(z, w), u) + y), x)) ||
 rewrite((y + min(min(min((x - y), z), w), u)), min((min(min(u, w), z) + y), x)) ||
 rewrite((z + min(u, (x - (y + (w + z))))), min((z + u), (x - (y + w)))) ||
 rewrite((z + min(u, (x - (y + (z + w))))), min((z + u), (x - (y + w)))) ||
 rewrite((z + min(u, (x - ((w + z) + y)))), min((z + u), (x - (w + y)))) ||
 rewrite((z + min(u, (x - ((z + w) + y)))), min((z + u), (x - (w + y)))) ||
 rewrite((z + min(u, min(x, (w + (y - z))))), min((z + min(u, x)), (w + y))) ||
 rewrite((z + min(u, min(x, ((y - z) + w)))), min((z + min(u, x)), (y + w))) ||
 rewrite((z + min(u, min(x, (y - (w + z))))), min((z + min(u, x)), (y - w))) ||
 rewrite((z + min(u, min(x, (y - (z + w))))), min((z + min(u, x)), (y - w))) ||
 rewrite((z + min(u, min((w + (y - z)), x))), min((z + min(u, x)), (w + y))) ||
 rewrite((z + min(u, min(((y - z) + w), x))), min((z + min(u, x)), (y + w))) ||
 rewrite((z + min(u, min((y - (w + z)), x))), min((z + min(u, x)), (y - w))) ||
 rewrite((z + min(u, min((y - (z + w)), x))), min((y - w), (z + min(u, x)))) ||
 rewrite((z + min(w, (x - (y + z)))), min((x - y), (w + z))) ||
 rewrite((z + min(w, (x - (z + y)))), min((x - y), (z + w))) ||
 rewrite((z + min(w, min(x, (y - z)))), min(y, (min(x, w) + z))) ||
 rewrite((z + min(w, min((y - z), x))), min((min(w, x) + z), y)) ||
 rewrite((z + min(x, (u + (y - (w + z))))), min((z + x), (u + (y - w)))) ||
 rewrite((z + min(x, (u + (y - (z + w))))), min((z + x), (u + (y - w)))) ||
 rewrite((z + min(x, (w + (y - z)))), min((y + w), (x + z))) ||
 rewrite((z + min(x, ((y - z) + w))), min((w + y), (z + x))) ||
 rewrite((z + min(x, ((y - (w + z)) + u))), min((z + x), ((y - w) + u))) ||
 rewrite((z + min(x, ((y - (z + w)) + u))), min((z + x), ((y - w) + u))) ||
 rewrite((z + min(x, (y - (w + z)))), min((y - w), (x + z))) ||
 rewrite((z + min(x, (y - (z + w)))), min((y - w), (z + x))) ||
 rewrite((z + min((u + (y - (w + z))), x)), min((u + (y - w)), (z + x))) ||
 rewrite((z + min((u + (y - (z + w))), x)), min((u + (y - w)), (z + x))) ||
 rewrite((z + min((w + (y - z)), x)), min((w + y), (z + x))) ||
 rewrite((z + min(((y - z) + w), x)), min((y + w), (z + x))) ||
 rewrite((z + min(((y - (w + z)) + u), x)), min((y - (w - u)), (z + x))) ||
 rewrite((z + min(((y - (z + w)) + u), x)), min(((y - w) + u), (z + x))) ||
 rewrite((z + min((x - (y + z)), w)), min((x - y), (z + w))) ||
 rewrite((z + min((x - (y + (w + z))), u)), min((x - (y + w)), (z + u))) ||
 rewrite((z + min((x - (y + (z + w))), u)), min((x - (y + w)), (z + u))) ||
 rewrite((z + min((x - (z + y)), w)), min((x - y), (z + w))) ||
 rewrite((z + min((x - ((w + z) + y)), u)), min((x - (w + y)), (z + u))) ||
 rewrite((z + min((x - ((z + w) + y)), u)), min((x - (y + w)), (z + u))) ||
 rewrite((z + min((y - (w + z)), x)), min((y - w), (z + x))) ||
 rewrite((z + min((y - (z + w)), x)), min((y - w), (z + x))) ||
 rewrite((z + min(min(x, (w + (y - z))), u)), min((min(x, u) + z), (w + y))) ||
 rewrite((z + min(min(x, ((y - z) + w)), u)), min((min(x, u) + z), (w + y))) ||
 rewrite((z + min(min(x, (y - z)), w)), min((min(x, w) + z), y)) ||
 rewrite((z + min(min(x, (y - (w + z))), u)), min((min(x, u) + z), (y - w))) ||
 rewrite((z + min(min(x, (y - (z + w))), u)), min((min(x, u) + z), (y - w))) ||
 rewrite((z + min(min((w + (y - z)), x), u)), min((min(u, x) + z), (w + y))) ||
 rewrite((z + min(min(((y - z) + w), x), u)), min((min(x, u) + z), (w + y))) ||
 rewrite((z + min(min((y - z), x), w)), min((min(w, x) + z), y)) ||
 rewrite((z + min(min((y - (w + z)), x), u)), min((min(u, x) + z), (y - w))) ||
 rewrite((z + min(min((y - (z + w)), x), u)), min((min(u, x) + z), (y - w))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((y + max(w, (z + (x - y)))), max((x + z), (w + y))) ||
 rewrite((y + max(w, ((x - y) + z))), max((y + w), (x + z))) ||
 rewrite((y + max((z + (x - y)), w)), max((z + x), (y + w))) ||
 rewrite((y + max(((x - y) + z), w)), max((w + y), (z + x))))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(((u + min(w, ((x + y) - z))) + z), (u + min((x + y), (w + z)))) ||
 rewrite(((u + min(w, ((y + x) - z))) + z), (u + min((y + x), (w + z)))) ||
 rewrite(((u + min(((x + y) - z), w)) + z), (u + min((x + y), (z + w)))) ||
 rewrite(((u + min(((y + x) - z), w)) + z), (u + min((y + x), (z + w)))) ||
 rewrite(((w + min(z, (x - y))) + y), (w + min(x, (z + y)))) ||
 rewrite(((w + min((x - y), z)) + y), (w + min((z + y), x))) ||
 rewrite(((x + ((0 - y)*c1)) + c2), ((x - (y*c1)) + c2)) ||
 rewrite(((x + min(y, ((z*c0) + c1))) + c2), (x + min((z*c0), (y + c2))), ((c1 + c2) == 0)) ||
 rewrite(((z + (y + (x*2))) + y), ((2*(y + x)) + z)) ||
 rewrite(((z + ((x*2) + y)) + y), ((2*(x + y)) + z)) ||
 rewrite(((z + (y*((x + c0)/c1))) + y), (z + (y*((x + fold((c0 + c1)))/c1)))) ||
 rewrite(((z + (((x + c0)/c1)*y)) + y), ((((x + fold((c0 + c1)))/c1)*y) + z)) ||
 rewrite((((y + (x*2)) + z) + y), (((y + x)*2) + z)) ||
 rewrite(((((x*2) + y) + z) + y), (((x + y)*2) + z)) ||
 rewrite((((min((x + c0), y) + z) + y) + c1), ((min((y + c1), x) + z) + y), ((c0 + c1) == 0)) ||
 rewrite((((min((x - y), z) + w) + y) + c0), ((min((z + y), x) + w) + c0)) ||
 rewrite((((x*c0) + ((y*c0) + z)) + c0), ((((x + y)*c0) + z) + c0)) ||
 rewrite((((x*c0) + ((y*c0) + z)) + c1), ((((x + y)*c0) + z) + c1)) ||
 rewrite((((x*y) + (z + (w*y))) + c0), ((z + ((w + x)*y)) + c0)) ||
 rewrite((((x*y) + ((z*y) + w)) + c0), ((((z + x)*y) + w) + c0)) ||
 rewrite((((y*((x + c0)/c1)) + z) + y), ((y*((x + fold((c0 + c1)))/c1)) + z)) ||
 rewrite((((((0 - x) - y)*c1) + z) + c2), ((z - ((x + y)*c1)) + c2)) ||
 rewrite((((((x + c0)/c1)*y) + z) + y), ((y*((x + fold((c0 + c1)))/c1)) + z)) ||
 rewrite(((((c0 - x)/c1) + y) + c2), (((c2 - x)/c1) + y), ((((1 <= c1) || (c2 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
 rewrite(((((c0 - x)/c1) + y) + c2), (((c1 - x)/c1) + y), ((((1 <= c1) || (c2 == 0)) || (c1 <= -1)) && (((((c1*c2) + c0) <= c1) && (((c1*c2) + c0) == c1)) && (c1 <= max(((c1*c2) + c0), -1))))) ||
 rewrite(((min(w, ((x + y) - z)) + u) + z), (min((x + y), (w + z)) + u)) ||
 rewrite(((min(w, ((y + x) - z)) + u) + z), (min((y + x), (z + w)) + u)) ||
 rewrite(((min(x, c0) + min(y, (z + c1))) + c2), (min(x, c0) + min(z, (y + c2))), ((c1 + c2) == 0)) ||
 rewrite(((min(x, (y + c0)) + (z*c1)) + c2), (min(y, (x + c2)) + (z*c1)), ((c0 + c2) == 0)) ||
 rewrite(((min(x, ((y*c0) + c1)) + z) + c2), (min((y*c0), (c2 + x)) + z), ((c1 + c2) == 0)) ||
 rewrite(((min(z, (x - y)) + w) + y), (min(x, (z + y)) + w)) ||
 rewrite(((min(((x*c0) + c1), y) + z) + c2), (min((x*c0), (c2 + y)) + z), ((c1 + c2) == 0)) ||
 rewrite(((min((x - y), c0) + z) + y), (min((y + c0), x) + z)) ||
 rewrite(((min((x - y), z) + w) + y), (min((z + y), x) + w)) ||
 rewrite(((min((x - y), z) + (w + y)) + c0), ((min((z + y), x) + w) + c0)) ||
 rewrite(((min((x - y), z) + (y + w)) + c0), ((min((z + y), x) + w) + c0)) ||
 rewrite(((min((x - (y + z)), w) + y) + c0), (min((x - z), (w + y)) + c0)) ||
 rewrite(((min((x - (y + z)), w) + z) + c0), (min((x - y), (w + z)) + c0)) ||
 rewrite(((min((x*c0), (y + c0)) + z) + c1), (min(((x*c0) + c1), y) + z), ((((1 <= c0) || (c1 == 0)) || (c0 <= -1)) && (((min(c1, 0) + c0) <= 0) && ((c0 + c1) == 0)))) ||
 rewrite(((min((x*c0), (y + c1)) + z) + c0), (min(((x*c0) + c0), y) + z), ((((1 <= c0) || (c1 == 0)) || (c0 <= -1)) && (((min(c1, 0) + c0) <= 0) && ((c0 + c1) == 0)))) ||
 rewrite(((min((x*c0), (y + c1)) + z) + c2), (min(((x*c0) + c2), y) + z), ((c1 + c2) == 0)) ||
 rewrite(((min(min(x, (y + c0)), 0) + z) + c2), (min(min(y, (x + c2)), c2) + z), ((c0 + c2) == 0)) ||
 rewrite(((min(min(x, (y + c0)), c1) + z) + c2), (z + min(min(fold(max(max((c1 + c2), ((c1 - c0) + -1)), min((c1 + c2), ((c1 - c0) + 1)))), (x + c2)), y)), ((c0 + c2) == 0)) ||
 rewrite(((max(x, y) + (min(x, y) + z)) + c0), ((x + (y + z)) + c0)) ||
 rewrite(((select(x, c0, c1) + y) + c2), (y + select(x, fold((c0 + c2)), fold((c1 + c2))))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x + (y*c0)) + (z + (y*c0))), ((y*fold((c0*2))) + (z + x))) ||
 rewrite(((x + (y*c0)) + ((y*c0) + z)), (x + ((y*fold((c0*2))) + z))) ||
 rewrite(((y + (x*c0)) + (z + (x*c0))), ((x*fold((c0*2))) + (z + y))) ||
 rewrite(((y + (x*c0)) + ((x*c0) + z)), (y + ((x*fold((c0*2))) + z))) ||
 rewrite(((z + (x*y)) + (u + (w*x))), (z + (u + ((w + y)*x)))) ||
 rewrite(((z + (x*y)) + (u + (x*w))), (((w + y)*x) + (u + z))) ||
 rewrite(((z + (x*y)) + ((w*x) + u)), (z + (((y + w)*x) + u))) ||
 rewrite(((z + (x*y)) + ((x*w) + u)), (z + (((y + w)*x) + u))) ||
 rewrite(((z + (y*x)) + (u + (w*x))), (((w + y)*x) + (u + z))) ||
 rewrite(((z + (y*x)) + (u + (x*w))), (((w + y)*x) + (u + z))) ||
 rewrite(((z + (y*x)) + ((w*x) + u)), (z + (((w + y)*x) + u))) ||
 rewrite(((z + (y*x)) + ((x*w) + u)), (z + (((w + y)*x) + u))) ||
 rewrite((((x*c0) + y) + (z + (x*c0))), ((z + y) + (x*fold((c0*2))))) ||
 rewrite((((x*c0) + y) + ((x*c0) + z)), ((fold((c0*2))*x) + (y + z))) ||
 rewrite((((x*y) + z) + (u + (w*x))), ((x*(w + y)) + (u + z))) ||
 rewrite((((x*y) + z) + (u + (x*w))), ((x*(w + y)) + (u + z))) ||
 rewrite((((x*y) + z) + ((w*x) + u)), ((x*(w + y)) + (z + u))) ||
 rewrite((((x*y) + z) + ((x*w) + u)), ((u + z) + (x*(w + y)))) ||
 rewrite((((y*c0) + x) + (z + (y*c0))), ((z + x) + (y*fold((c0*2))))) ||
 rewrite((((y*c0) + x) + ((y*c0) + z)), ((y*fold((c0*2))) + (x + z))) ||
 rewrite((((y*x) + z) + (u + (w*x))), ((u + z) + ((w + y)*x))) ||
 rewrite((((y*x) + z) + (u + (x*w))), ((x*(w + y)) + (u + z))) ||
 rewrite((((y*x) + z) + ((w*x) + u)), ((x*(w + y)) + (z + u))) ||
 rewrite((((y*x) + z) + ((x*w) + u)), ((u + z) + (x*(w + y)))))))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite((((0 - x) - (y*c1)) + c2), ((c2 - x) - (y*c1))) ||
 rewrite(((((x + c0)/c1) - y) + c2), (((x + c2)/c1) - y), ((((1 <= c1) || (c2 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
 rewrite(((min(x, (y + z)) - (w + y)) + u), ((min(z, (x - y)) - w) + u)) ||
 rewrite(((min(x, (y + z)) - (y + w)) + u), ((min(z, (x - y)) - w) + u)) ||
 rewrite(((min(x, (z + y)) - (w + y)) + u), ((min(z, (x - y)) - w) + u)) ||
 rewrite(((min(x, (z + y)) - (y + w)) + u), ((min(z, (x - y)) - w) + u)) ||
 rewrite(((min((y + z), x) - (w + y)) + u), ((min((x - y), z) - w) + u)) ||
 rewrite(((min((y + z), x) - (y + w)) + u), ((min((x - y), z) - w) + u)) ||
 rewrite(((min((z + y), x) - (w + y)) + u), ((min((x - y), z) - w) + u)) ||
 rewrite(((min((z + y), x) - (y + w)) + u), ((min((x - y), z) - w) + u)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x - y) + (y + z)), (x + z)) ||
 rewrite(((x - y) + (z + y)), (x + z)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x - (y*c0)) + (z - (y*c0))), ((x + z) - (fold((c0*2))*y))))))) ||
((a.node_type() == IRNodeType::Mul) && (
 rewrite(((y*((x + c0)/c1)) + y), (y*((x + fold((c0 + c1)))/c1))) ||
 rewrite(((z*min(x, (y + -1))) + z), (z*min(y, (x + 1)))) ||
 rewrite(((z*min(y, (x + -1))) + z), (z*min(x, (y + 1)))) ||
 rewrite(((z*min((x + -1), y)) + z), (z*min((y + 1), x))) ||
 rewrite(((z*min((y + -1), x)) + z), (z*min((x + 1), y))) ||
 rewrite((((x + ((0 - y) - z))*c1) + c2), ((((x - y) - z)*c1) + c2)) ||
 rewrite((((0 - x)*c1) + c2), (c2 - (x*c1))) ||
 rewrite(((((0 - x) - y)*c1) + c2), (c2 - ((x + y)*c1))) ||
 rewrite(((((x + c0)/c1)*y) + y), (((x + fold((c0 + c1)))/c1)*y)) ||
 rewrite(((min(x, (y + -1))*z) + z), (min(y, (x + 1))*z)) ||
 rewrite(((min(y, (x + -1))*z) + z), (min(x, (y + 1))*z)) ||
 rewrite(((min((x + -1), y)*z) + z), (min((y + 1), x)*z)) ||
 rewrite(((min((y + -1), x)*z) + z), (min((x + 1), y)*z)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*c0) + (y + (x*c0))), ((x*fold((c0*2))) + y)) ||
 rewrite(((x*c0) + (z + (min(y, c1)*c0))), (z + ((x + min(y, c1))*c0))) ||
 rewrite(((x*c0) + ((x*c0) + y)), ((x*fold((c0*2))) + y)) ||
 rewrite(((x*c0) + ((min(y, c1)*c0) + z)), (((x + min(y, c1))*c0) + z)) ||
 rewrite(((x*y) + (u + (w + (x*z)))), (w + (u + ((z + y)*x)))) ||
 rewrite(((x*y) + (u + (w + (z*x)))), (w + (u + ((z + y)*x)))) ||
 rewrite(((x*y) + (u + ((x*z) + w))), (w + (u + (x*(z + y))))) ||
 rewrite(((x*y) + (u + ((z*x) + w))), (w + (u + (x*(z + y))))) ||
 rewrite(((x*y) + (w + (y*z))), (w + ((z + x)*y))) ||
 rewrite(((x*y) + (w + (z*y))), (w + ((z + x)*y))) ||
 rewrite(((x*y) + (z + (w*y))), (z + ((w + x)*y))) ||
 rewrite(((x*y) + (z + (y*w))), (z + ((w + x)*y))) ||
 rewrite(((x*y) + ((w + (x*z)) + u)), ((w + ((z + y)*x)) + u)) ||
 rewrite(((x*y) + ((w + (z*x)) + u)), ((w + ((z + y)*x)) + u)) ||
 rewrite(((x*y) + (((x*z) + w) + u)), ((((y + z)*x) + w) + u)) ||
 rewrite(((x*y) + (((z*x) + w) + u)), ((((y + z)*x) + w) + u)) ||
 rewrite(((x*y) + ((w*y) + z)), ((y*(x + w)) + z)) ||
 rewrite(((x*y) + ((y*w) + z)), ((y*(w + x)) + z)) ||
 rewrite(((x*y) + ((y*z) + w)), (((z + x)*y) + w)) ||
 rewrite(((x*y) + ((z*y) + w)), ((y*(x + z)) + w)) ||
 rewrite(((y*x) + (u + (w + (x*z)))), (w + (u + ((z + y)*x)))) ||
 rewrite(((y*x) + (u + (w + (z*x)))), (w + (u + ((z + y)*x)))) ||
 rewrite(((y*x) + (u + ((x*z) + w))), (w + (u + (x*(z + y))))) ||
 rewrite(((y*x) + (u + ((z*x) + w))), (w + (u + (x*(z + y))))) ||
 rewrite(((y*x) + (w + (y*z))), ((y*(x + z)) + w)) ||
 rewrite(((y*x) + (w + (z*y))), (w + ((z + x)*y))) ||
 rewrite(((y*x) + (z + (w*y))), (z + ((w + x)*y))) ||
 rewrite(((y*x) + (z + (y*w))), (z + ((w + x)*y))) ||
 rewrite(((y*x) + ((w + (x*z)) + u)), ((w + ((z + y)*x)) + u)) ||
 rewrite(((y*x) + ((w + (z*x)) + u)), ((w + ((z + y)*x)) + u)) ||
 rewrite(((y*x) + (((x*z) + w) + u)), (w + ((x*(z + y)) + u))) ||
 rewrite(((y*x) + (((z*x) + w) + u)), ((((z + y)*x) + w) + u)) ||
 rewrite(((y*x) + ((w*y) + z)), (((x + w)*y) + z)) ||
 rewrite(((y*x) + ((y*w) + z)), ((y*(w + x)) + z)) ||
 rewrite(((y*x) + ((y*z) + w)), (((x + z)*y) + w)) ||
 rewrite(((y*x) + ((z*y) + w)), ((y*(x + z)) + w)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x*c0) + ((y + (x*c0)) - z)), (y + ((fold((c0*2))*x) - z))) ||
 rewrite(((x*c0) + (((x*c0) + y) - z)), ((x*fold((c0*2))) + (y - z))) ||
 rewrite(((x*y) + (z - (w*y))), (z + ((x - w)*y))) ||
 rewrite(((x*y) + (z - (y*w))), (z + ((x - w)*y))) ||
 rewrite(((x*y) + ((z - (w*y)) - u)), ((z + ((x - w)*y)) - u)) ||
 rewrite(((x*y) + ((z - (y*w)) - u)), ((z + ((x - w)*y)) - u)) ||
 rewrite(((x*y) + ((w*(y*z)) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((x*y) + ((w*(z*y)) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((x*y) + ((y*z) - w)), (((x + z)*y) - w)) ||
 rewrite(((x*y) + ((z*y) - w)), (((x + z)*y) - w)) ||
 rewrite(((x*y) + (((y*z)*w) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((x*y) + (((z*y)*w) - u)), ((y*(x + (z*w))) - u)) ||
 rewrite(((y*x) + (z - (w*y))), (z + ((x - w)*y))) ||
 rewrite(((y*x) + (z - (y*w))), (z + ((x - w)*y))) ||
 rewrite(((y*x) + ((z - (w*y)) - u)), ((z + ((x - w)*y)) - u)) ||
 rewrite(((y*x) + ((z - (y*w)) - u)), ((z + ((x - w)*y)) - u)) ||
 rewrite(((y*x) + ((w*(y*z)) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((y*x) + ((w*(z*y)) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((y*x) + ((y*z) - w)), (((x + z)*y) - w)) ||
 rewrite(((y*x) + ((z*y) - w)), (((x + z)*y) - w)) ||
 rewrite(((y*x) + (((y*z)*w) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((y*x) + (((z*y)*w) - u)), ((y*(x + (z*w))) - u)))))) ||
((a.node_type() == IRNodeType::Div) && (
((b.node_type() == IRNodeType::Div) && (
 rewrite((((c0 - x)/c1) + ((x + c2)/c1)), 0, (((c1 <= -1) || (((c0 + c2) + 1) <= c1)) && (((1 <= c1) || (((c0 + c1) + c2) == -1)) && ((c1 + -1) <= (c0 + c2))))))))) ||
((a.node_type() == IRNodeType::Min) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite((min(max(x, c0), (y + c1)) + 0), min(max(x, c0), (y + c1))))) ||
 rewrite((min(u, (x - (y + (w + z)))) + z), min((x - (y + w)), (u + z))) ||
 rewrite((min(u, (x - (y + (z + w)))) + z), min((x - (y + w)), (u + z))) ||
 rewrite((min(u, (x - ((w + z) + y))) + z), min((x - (w + y)), (u + z))) ||
 rewrite((min(u, (x - ((z + w) + y))) + z), min((x - (y + w)), (u + z))) ||
 rewrite((min(u, ((x + y) - (w + z))) + w), min(((x + y) - z), (u + w))) ||
 rewrite((min(u, ((x + y) - (z + w))) + w), min(((x + y) - z), (u + w))) ||
 rewrite((min(u, ((y + x) - (w + z))) + w), min(((y + x) - z), (u + w))) ||
 rewrite((min(u, ((y + x) - (z + w))) + w), min(((y + x) - z), (u + w))) ||
 rewrite((min(u, ((x*y) - (w + z))) + w), min(((x*y) - z), (u + w))) ||
 rewrite((min(u, ((x*y) - (z + w))) + w), min(((x*y) - z), (u + w))) ||
 rewrite((min(u, ((y*x) - (w + z))) + w), min(((y*x) - z), (u + w))) ||
 rewrite((min(u, ((y*x) - (z + w))) + w), min(((y*x) - z), (u + w))) ||
 rewrite((min(u, (min(x, y) - (w + z))) + w), min((min(x, y) - z), (u + w))) ||
 rewrite((min(u, (min(x, y) - (z + w))) + w), min((min(x, y) - z), (u + w))) ||
 rewrite((min(u, (min(y, x) - (w + z))) + w), min((min(y, x) - z), (u + w))) ||
 rewrite((min(u, (min(y, x) - (z + w))) + w), min((min(y, x) - z), (u + w))) ||
 rewrite((min(u, min(w, (z + (x - y)))) + y), min((min(w, u) + y), (x + z))) ||
 rewrite((min(u, min(w, ((x - y) + z))) + y), min((min(w, u) + y), (x + z))) ||
 rewrite((min(u, min(w, (x - (y + z)))) + y), min((y + min(w, u)), (x - z))) ||
 rewrite((min(u, min(w, (x - (z + y)))) + y), min((min(w, u) + y), (x - z))) ||
 rewrite((min(u, min(w, min(z, (x - y)))) + y), min(x, (min(w, min(z, u)) + y))) ||
 rewrite((min(u, min(w, min((x - y), z))) + y), min(x, (min(w, min(u, z)) + y))) ||
 rewrite((min(u, min(x, (w + (y - z)))) + z), min((min(x, u) + z), (w + y))) ||
 rewrite((min(u, min(x, ((y - z) + w))) + z), min((min(x, u) + z), (w + y))) ||
 rewrite((min(u, min(x, (y - (w + z)))) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(u, min(x, (y - (z + w)))) + z), min((z + min(x, u)), (y - w))) ||
 rewrite((min(u, min((w + (y - z)), x)) + z), min((min(x, u) + z), (w + y))) ||
 rewrite((min(u, min((z + (x - y)), w)) + y), min((min(u, w) + y), (x + z))) ||
 rewrite((min(u, min(((x - y) + z), w)) + y), min((min(u, w) + y), (x + z))) ||
 rewrite((min(u, min(((y - z) + w), x)) + z), min((min(x, u) + z), (y + w))) ||
 rewrite((min(u, min((x - (y + z)), w)) + y), min((min(u, w) + y), (x - z))) ||
 rewrite((min(u, min((x - (z + y)), w)) + y), min((min(u, w) + y), (x - z))) ||
 rewrite((min(u, min((y - (w + z)), x)) + z), min((min(u, x) + z), (y - w))) ||
 rewrite((min(u, min((y - (z + w)), x)) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(u, min(min(z, (x - y)), w)) + y), min(x, (min(min(z, u), w) + y))) ||
 rewrite((min(u, min(min((x - y), z), w)) + y), min(x, (min(min(u, z), w) + y))) ||
 rewrite((min(w, (z + (x - y))) + y), min((z + x), (w + y))) ||
 rewrite((min(w, ((x - y) + z)) + y), min((x + z), (w + y))) ||
 rewrite((min(w, (x - (y + z))) + y), min((x - z), (w + y))) ||
 rewrite((min(w, (x - (y + z))) + z), min((x - y), (w + z))) ||
 rewrite((min(w, (x - (z + y))) + y), min((x - z), (w + y))) ||
 rewrite((min(w, (x - (z + y))) + z), min((x - y), (w + z))) ||
 rewrite((min(w, min(x, (y - z))) + z), min((min(x, w) + z), y)) ||
 rewrite((min(w, min(z, (x - y))) + y), min((min(z, w) + y), x)) ||
 rewrite((min(w, min((x - y), z)) + y), min(x, (min(w, z) + y))) ||
 rewrite((min(w, min((y - z), x)) + z), min(y, (min(w, x) + z))) ||
 rewrite((min(x, (u + (y - (w + z)))) + z), min((u + (y - w)), (x + z))) ||
 rewrite((min(x, (u + (y - (z + w)))) + z), min((u + (y - w)), (x + z))) ||
 rewrite((min(x, (w + (y - z))) + z), min((w + y), (x + z))) ||
 rewrite((min(x, ((y - z) + w)) + z), min((y + w), (x + z))) ||
 rewrite((min(x, ((y - (w + z)) + u)) + z), min((y - (w - u)), (x + z))) ||
 rewrite((min(x, ((y - (z + w)) + u)) + z), min((y + (u - w)), (x + z))) ||
 rewrite((min(x, (y - (w + z))) + z), min((y - w), (x + z))) ||
 rewrite((min(x, (y - (z + w))) + z), min((y - w), (x + z))) ||
 rewrite((min((u + (y - (w + z))), x) + z), min((u + (y - w)), (x + z))) ||
 rewrite((min((u + (y - (z + w))), x) + z), min((u + (y - w)), (x + z))) ||
 rewrite((min((w + (y - z)), x) + z), min((w + y), (x + z))) ||
 rewrite((min((z + (x - y)), w) + y), min((z + x), (w + y))) ||
 rewrite((min(((x - y) + z), w) + y), min((x + z), (w + y))) ||
 rewrite((min(((y - z) + w), x) + z), min((y + w), (x + z))) ||
 rewrite((min(((y - (w + z)) + u), x) + z), min(((y - w) + u), (x + z))) ||
 rewrite((min(((y - (z + w)) + u), x) + z), min(((y - w) + u), (x + z))) ||
 rewrite((min((x - (w + (y*z))), z) + w), min((x - (y*z)), (z + w))) ||
 rewrite((min((x - (w + (z*y))), z) + w), min((x - (z*y)), (z + w))) ||
 rewrite((min((x - (y + z)), c0) + y), min((x - z), (c0 + y))) ||
 rewrite((min((x - (y + z)), c0) + z), min((x - y), (c0 + z))) ||
 rewrite((min((x - (y + z)), w) + y), min((x - z), (w + y))) ||
 rewrite((min((x - (y + z)), w) + z), min((x - y), (w + z))) ||
 rewrite((min((x - (y + (w + z))), u) + z), min((x - (y + w)), (u + z))) ||
 rewrite((min((x - (y + (z + w))), u) + z), min((x - (y + w)), (u + z))) ||
 rewrite((min((x - (z + y)), w) + y), min((x - z), (w + y))) ||
 rewrite((min((x - (z + y)), w) + z), min((x - y), (w + z))) ||
 rewrite((min((x - ((w + z) + y)), u) + z), min((x - (w + y)), (u + z))) ||
 rewrite((min((x - ((z + w) + y)), u) + z), min((x - (w + y)), (u + z))) ||
 rewrite((min((y - (w + z)), x) + z), min((y - w), (x + z))) ||
 rewrite((min((y - (z + w)), x) + z), min((y - w), (x + z))) ||
 rewrite((min(min(w, (z + (x - y))), u) + y), min((min(w, u) + y), (z + x))) ||
 rewrite((min(min(w, ((x - y) + z)), u) + y), min((min(w, u) + y), (x + z))) ||
 rewrite((min(min(w, (x - (y + z))), u) + y), min((min(w, u) + y), (x - z))) ||
 rewrite((min(min(w, (x - (z + y))), u) + y), min((min(w, u) + y), (x - z))) ||
 rewrite((min(min(w, min(z, (x - y))), u) + y), min((min(w, min(z, u)) + y), x)) ||
 rewrite((min(min(w, min((x - y), z)), u) + y), min((min(w, min(u, z)) + y), x)) ||
 rewrite((min(min(x, (w + (y - z))), u) + z), min((min(x, u) + z), (w + y))) ||
 rewrite((min(min(x, (y + c0)), 0) + c2), min(min(y, (x + c2)), c2), ((c0 + c2) == 0)) ||
 rewrite((min(min(x, (y + c0)), c0) + c1), min(min(y, (x + c1)), 0), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, (y + c0)), c1) + c2), min(min(y, (x + c2)), fold((c1 + c2))), ((c0 + c2) == 0)) ||
 rewrite((min(min(x, (y + c0)), (x + z)) + c1), min((min(z, 0) + (x + c1)), y), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z + c1)) + c1), min((min(x, (z + c1)) + c1), y), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z + c1)) + c2), min((min(x, (z + c1)) + c2), y), ((c0 + c2) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z + x)) + c1), min((min(z, 0) + (c1 + x)), y), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z*c1)) + c2), min(y, (min(x, (z*c1)) + c2)), ((c0 + c2) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z/c1)) + c2), min(y, (min(x, (z/c1)) + c2)), ((c0 + c2) == 0)) ||
 rewrite((min(min(x, ((y + z) + c0)), z) + c1), min((min(x, z) + c1), (y + z)), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, ((y - z) + w)), u) + z), min((min(x, u) + z), (w + y))) ||
 rewrite((min(min(x, ((y*c0) + c1)), z) + c2), min((y*c0), (min(x, z) + c2)), ((c1 + c2) == 0)) ||
 rewrite((min(min(x, ((y*z) + c0)), 0) + c2), min(min((y*z), c2), (x + c2)), (((c0 <= -1) || (c2 <= 0)) && ((c0 + c2) == 0))) ||
 rewrite((min(min(x, ((y*z) + c0)), c1) + c2), min(min((y*z), (x + c2)), fold((c1 + c2))), (((min(c0, c1) + c2) <= 0) && ((c0 + c2) == 0))) ||
 rewrite((min(min(x, ((y/c0) + c1)), z) + c2), min((min(x, z) + c2), (y/c0)), ((c1 + c2) == 0)) ||
 rewrite((min(min(x, (min(y, c0) + c1)), z) + c2), min(min(y, (min(x, z) + c2)), c0), ((c1 + c2) == 0)) ||
 rewrite((min(min(x, (min(y, z) + c0)), c1) + c2), min(min(z, min(y, c0)), (x + c2)), (((c0 + c2) == 0) && ((c1 + c2) == c0))) ||
 rewrite((min(min(x, (max(y, c0) + c1)), z) + c2), min(max(y, c0), (min(x, z) + c2)), ((c1 + c2) == 0)) ||
 rewrite((min(min(x, (y - z)), w) + z), min((min(x, w) + z), y)) ||
 rewrite((min(min(x, (y - (w + z))), u) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(min(x, (y - (z + w))), u) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(min(x, (y*c0)), (z*c0)) + c1), (min((min(y, z)*c0), x) + c1), (1 <= c0)) ||
 rewrite((min(min(z, (x - y)), w) + y), min((min(z, w) + y), x)) ||
 rewrite((min(min((w + (y - z)), x), u) + z), min((w + y), (min(x, u) + z))) ||
 rewrite((min(min((x + c0), y), 0) + c2), min(min((y + c2), x), c2), ((c0 + c2) == 0)) ||
 rewrite((min(min((x + c0), y), c1) + c2), min(min((y + c2), x), fold((c1 + c2))), ((c0 + c2) == 0)) ||
 rewrite((min(min((x + c0), y), (y + z)) + c1), min((min(z, 0) + (y + c1)), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), y), (z + c1)) + c1), min((min((z + c1), y) + c1), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), y), (z + c1)) + c2), min((min((z + c1), y) + c2), x), ((c0 + c2) == 0)) ||
 rewrite((min(min((x + c0), y), (z + y)) + c1), min((min(z, 0) + (c1 + y)), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), y), (z*c1)) + c2), min(x, (min(y, (z*c1)) + c2)), ((c0 + c2) == 0)) ||
 rewrite((min(min((x + c0), y), (z/c0)) + c1), min(x, (min(y, (z/c0)) + c1)), ((((1 <= c0) || (c1 == 0)) || (c0 <= -1)) && (((min(c1, 0) + c0) <= 0) && ((c0 + c1) == 0)))) ||
 rewrite((min(min((x + c0), (y + z)), y) + c1), min((min(z, 0) + (y + c1)), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), (y + z)), z) + c1), min((min(y, 0) + (c1 + z)), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + y), (z + c0)), y) + c1), min((min(x, 0) + (y + c1)), z), ((c0 + c1) == 0)) ||
 rewrite((min(min((z + (x - y)), w), u) + y), min((z + x), (min(w, u) + y))) ||
 rewrite((min(min(((x + y) + c0), z), y) + c1), min((min(x, c1) + y), (z + c1)), ((c0 + c1) == 0)) ||
 rewrite((min(min(((x - y) + z), w), u) + y), min((x + z), (min(w, u) + y))) ||
 rewrite((min(min(((y - z) + w), x), u) + z), min((y + w), (min(x, u) + z))) ||
 rewrite((min(min(((x*c0) + c0), y), z) + c1), min((x*c0), (min(y, z) + c1)), ((((0 <= (c0 + c1)) && (1 <= c0)) && (c0 <= 26)) && ((min(c1, 0) + c0) <= 0))) ||
 rewrite((min(min(((x*c0) + c1), y), z) + c2), min((x*c0), (min(y, z) + c2)), ((c1 + c2) == 0)) ||
 rewrite((min(min(((x/c0) + c0), y), z) + c1), min((x/c0), (min(y, z) + c1)), ((((1 <= c0) || (c1 == 0)) || (c0 <= -1)) && (((min(c1, 0) + c0) <= 0) && ((c0 + c1) == 0)))) ||
 rewrite((min(min((max(x, c0) + c1), y), z) + c2), min(max(x, c0), (min(y, z) + c2)), ((c1 + c2) == 0)) ||
 rewrite((min(min((x - y), z), w) + y), min(x, (min(z, w) + y))) ||
 rewrite((min(min((x - (y + z)), w), u) + y), min((x - z), (min(w, u) + y))) ||
 rewrite((min(min((x - (z + y)), w), u) + y), min((x - z), (min(w, u) + y))) ||
 rewrite((min(min((y - z), x), w) + z), min((z + min(x, w)), y)) ||
 rewrite((min(min((y - (w + z)), x), u) + z), min((y - w), (min(x, u) + z))) ||
 rewrite((min(min((y - (z + w)), x), u) + z), min((y - w), (min(x, u) + z))) ||
 rewrite((min(min((x*c0), y), (z*c0)) + c1), (min((min(x, z)*c0), y) + c1), (1 <= c0)) ||
 rewrite((min(min((x*c0), (y + c1)), z) + c2), min((min((x*c0), z) + c2), y), ((1 <= c0) && ((c1 + c2) == 0))) ||
 rewrite((min(min((x/c0), (y + c1)), z) + c2), min((min((x/c0), z) + c2), y), ((c1 + c2) == 0)) ||
 rewrite((min(min(min(x, y), (z + c0)), c1) + c2), min(min((min(x, y) + c2), z), c0), (((c0 + c2) == 0) && ((c1 + c2) == c0))) ||
 rewrite((min(min(min(x, (y + c0)), z), 0) + c2), min(min((min(x, z) + c2), y), c2), ((c0 + c2) == 0)) ||
 rewrite((min(min(min(x, (y + c0)), z), c0) + c1), min(min((min(x, z) + c1), y), 0), ((c0 + c1) == 0)) ||
 rewrite((min(min(min(x, (y + c0)), z), c1) + c2), min(min((min(x, z) + c2), y), fold((c1 + c2))), ((c0 + c2) == 0)) ||
 rewrite((min(min(min(x, (y + c0)), z), c1) + c2), min(min((min(x, z) + c2), y), c0), (((c0 + c2) == 0) && ((c1 + c2) == c0))) ||
 rewrite((min(min(min(z, (x - y)), w), u) + y), min((min(min(z, u), w) + y), x)) ||
 rewrite((min(min(min((x + c0), y), z), 0) + c2), min(min((c2 + min(y, z)), x), c2), ((c0 + c2) == 0)) ||
 rewrite((min(min(min((x + c0), y), z), c1) + c2), min(min((min(y, z) + c2), x), fold((c1 + c2))), ((c0 + c2) == 0)) ||
 rewrite((min(min(min((x - y), z), w), u) + y), min(x, (min(min(z, w), u) + y))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(u, (x - (y + (w + z)))) + (z + w)), min((x - y), (u + (z + w)))) ||
 rewrite((min(u, (x - (y + (z + w)))) + (w + z)), min((x - y), (w + (u + z)))) ||
 rewrite((min(u, (x - ((w + z) + y))) + (z + w)), min(((w + z) + u), (x - y))) ||
 rewrite((min(u, (x - ((z + w) + y))) + (w + z)), min(((z + w) + u), (x - y))) ||
 rewrite((min(w, (x - (y + z))) + (u + z)), (min((x - y), (w + z)) + u)) ||
 rewrite((min(w, (x - (y + z))) + (z + u)), (min((x - y), (w + z)) + u)) ||
 rewrite((min(w, (x - (z + y))) + (u + z)), (min((x - y), (z + w)) + u)) ||
 rewrite((min(w, (x - (z + y))) + (z + u)), (min((x - y), (w + z)) + u)) ||
 rewrite((min(w, ((x - y) - z)) + (y + z)), min(x, ((w + z) + y))) ||
 rewrite((min(w, ((x - y) - z)) + (z + y)), min(x, ((w + z) + y))) ||
 rewrite((min(w, (min(x, y) - z)) + (u + z)), (min(y, min(x, (w + z))) + u)) ||
 rewrite((min(w, (min(x, y) - z)) + (z + u)), (min(y, min(x, (w + z))) + u)) ||
 rewrite((min(w, (min(y, x) - z)) + (u + z)), (min(x, min(y, (w + z))) + u)) ||
 rewrite((min(w, (min(y, x) - z)) + (z + u)), (min(x, min(y, (w + z))) + u)) ||
 rewrite((min(x, (w + (y - z))) + (u + z)), (min((w + y), (x + z)) + u)) ||
 rewrite((min(x, (w + (y - z))) + (z + u)), (min((w + y), (x + z)) + u)) ||
 rewrite((min(x, ((y - z) + w)) + (u + z)), (min((z + x), (y + w)) + u)) ||
 rewrite((min(x, ((y - z) + w)) + (z + u)), (min((x + z), (y + w)) + u)) ||
 rewrite((min(x, (y - z)) + (w + z)), (min(y, (x + z)) + w)) ||
 rewrite((min(x, (y - z)) + (z + w)), (min(y, (x + z)) + w)) ||
 rewrite((min(z, (x - y)) + (w + y)), (min(x, (z + y)) + w)) ||
 rewrite((min(z, (x - y)) + (y + w)), (min(x, (z + y)) + w)) ||
 rewrite((min(z, (x - (y*z))) + (w + (z*y))), (min(x, ((y + 1)*z)) + w)) ||
 rewrite((min(z, (x - (y*z))) + ((z*y) + w)), (min(x, ((y + 1)*z)) + w)) ||
 rewrite((min(z, (x - (z*y))) + (w + (y*z))), (min(x, ((y + 1)*z)) + w)) ||
 rewrite((min(z, (x - (z*y))) + ((y*z) + w)), (min(x, ((y + 1)*z)) + w)) ||
 rewrite((min((w + (y - z)), x) + (u + z)), (min((w + y), (x + z)) + u)) ||
 rewrite((min((w + (y - z)), x) + (z + u)), (min((w + y), (x + z)) + u)) ||
 rewrite((min(((y - z) + w), x) + (u + z)), (min((y + w), (x + z)) + u)) ||
 rewrite((min(((y - z) + w), x) + (z + u)), (min((y + w), (x + z)) + u)) ||
 rewrite((min((x - y), c0) + (y + z)), (min((y + c0), x) + z)) ||
 rewrite((min((x - y), c0) + (z + y)), (min((c0 + y), x) + z)) ||
 rewrite((min((x - y), z) + (w + y)), (min((z + y), x) + w)) ||
 rewrite((min((x - y), z) + (y + w)), (min((z + y), x) + w)) ||
 rewrite((min((x - (y + z)), c0) + (w + z)), (min((x - y), (c0 + z)) + w)) ||
 rewrite((min((x - (y + z)), c0) + (z + w)), (min((x - y), (c0 + z)) + w)) ||
 rewrite((min((x - (y + z)), w) + (u + z)), (min((x - y), (w + z)) + u)) ||
 rewrite((min((x - (y + z)), w) + (z + u)), (min((x - y), (w + z)) + u)) ||
 rewrite((min((x - (y + (w + z))), u) + (z + w)), min((x - y), (u + (z + w)))) ||
 rewrite((min((x - (y + (z + w))), u) + (w + z)), min((x - y), (u + (w + z)))) ||
 rewrite((min((x - (z + y)), w) + (u + z)), (min((x - y), (w + z)) + u)) ||
 rewrite((min((x - (z + y)), w) + (z + u)), (min((x - y), (w + z)) + u)) ||
 rewrite((min((x - ((w + z) + y)), u) + (z + w)), min((x - y), (u + (z + w)))) ||
 rewrite((min((x - ((z + w) + y)), u) + (w + z)), min((x - y), (u + (w + z)))) ||
 rewrite((min((x - (y*z)), z) + (w + (z*y))), (min(((y + 1)*z), x) + w)) ||
 rewrite((min((x - (y*z)), z) + ((z*y) + w)), (min(x, (z*(y + 1))) + w)) ||
 rewrite((min((x - (z*y)), z) + (w + (y*z))), (min(((y + 1)*z), x) + w)) ||
 rewrite((min((x - (z*y)), z) + ((y*z) + w)), (min(x, (z*(1 + y))) + w)) ||
 rewrite((min((y - z), x) + (w + z)), (min((x + z), y) + w)) ||
 rewrite((min((y - z), x) + (z + w)), (min((x + z), y) + w)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((min(u, (x - (y + (w*z)))) + (z*w)), min((x - y), ((w*z) + u))) ||
 rewrite((min(u, (x - (y + (z*w)))) + (w*z)), min((x - y), ((z*w) + u))) ||
 rewrite((min(u, (x - ((w*z) + y))) + (z*w)), min((x - y), (u + (z*w)))) ||
 rewrite((min(u, (x - ((z*w) + y))) + (w*z)), min((x - y), (u + (w*z)))) ||
 rewrite((min((x - (y + (w*z))), u) + (z*w)), min((x - y), (u + (z*w)))) ||
 rewrite((min((x - (y + (z*w))), u) + (w*z)), min((x - y), (u + (w*z)))) ||
 rewrite((min((x - ((w*z) + y)), u) + (z*w)), min((x - y), (u + (z*w)))) ||
 rewrite((min((x - ((z*w) + y)), u) + (w*z)), min((x - y), (u + (w*z)))))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max(w, (z + (x - y))) + y), max((z + x), (w + y))) ||
 rewrite((max(w, ((x - y) + z)) + y), max((x + z), (w + y))) ||
 rewrite((max((z + (x - y)), w) + y), max((z + x), (w + y))) ||
 rewrite((max(((x - y) + z), w) + y), max((x + z), (w + y))) ||
 rewrite((max(max(x, (y + c0)), (z + c1)) + c2), max((max(x, (z + c1)) + c2), y), ((c0 + c2) == 0)) ||
 rewrite((max(max(x, ((y*c0) + c1)), z) + c2), max((max(x, z) + c2), (c0*y)), ((c1 + c2) == 0)) ||
 rewrite((max(max((x*c0), (y + c1)), z) + c2), max((max((x*c0), z) + c2), y), ((1 <= c0) && ((c1 + c2) == 0))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((max(w, (z + (x - y))) + (u + y)), (max((x + z), (w + y)) + u)) ||
 rewrite((max(w, (z + (x - y))) + (y + u)), (max((z + x), (w + y)) + u)) ||
 rewrite((max(w, ((x - y) + z)) + (u + y)), (max((w + y), (x + z)) + u)) ||
 rewrite((max(w, ((x - y) + z)) + (y + u)), (max((z + x), (w + y)) + u)) ||
 rewrite((max(x, c0) + (y + min(x, c0))), (x + (y + c0))) ||
 rewrite((max(x, c0) + (min(x, c0) + y)), (x + (c0 + y))) ||
 rewrite((max(x, y) + (z + min(x, y))), (x + (z + y))) ||
 rewrite((max(x, y) + (z + min(y, x))), (x + (z + y))) ||
 rewrite((max(x, y) + (min(x, y) + z)), (x + (y + z))) ||
 rewrite((max(x, y) + (min(y, x) + z)), (x + (y + z))) ||
 rewrite((max(y, x) + (z + min(x, y))), (x + (z + y))) ||
 rewrite((max(y, x) + (z + min(y, x))), (x + (z + y))) ||
 rewrite((max(y, x) + (min(x, y) + z)), (x + (y + z))) ||
 rewrite((max(y, x) + (min(y, x) + z)), (x + (y + z))) ||
 rewrite((max((z + (x - y)), w) + (u + y)), (max((z + x), (w + y)) + u)) ||
 rewrite((max((z + (x - y)), w) + (y + u)), (max((z + x), (w + y)) + u)) ||
 rewrite((max(((x - y) + z), w) + (u + y)), (max((x + z), (w + y)) + u)) ||
 rewrite((max(((x - y) + z), w) + (y + u)), (max((x + z), (w + y)) + u)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((max(x, c0) + max(min(x, c0), c1)), (c0 + max(x, c1)), (c1 <= c0)))))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite((select(x, y, (y + c0)) + c1), (select(x, c1, fold((c0 + c1))) + y)) ||
 rewrite((select(x, y, ((z*c0) + c1)) + c2), select(x, (y + c2), (z*c0)), ((c1 + c2) == 0)) ||
 rewrite((select(x, (y + c0), (z + c1)) + c2), select(x, (y + fold((c0 + c2))), z), ((c1 + c2) == 0)) ||
 rewrite((select(x, ((y + z) + w), (y + u)) + c0), ((select(x, (z + w), u) + y) + c0))))