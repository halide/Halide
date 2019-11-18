((b.node_type() == IRNodeType::Add) && (
 rewrite((x + (x + (x - y))), ((x*3) - y)) ||
 rewrite((x + ((x - y) + x)), ((x*3) - y)) ||
 rewrite((y + (w + min(z, (x - y)))), (w + min(x, (z + y)))) ||
 rewrite((y + (w + min((x - y), z))), (w + min((z + y), x))) ||
 rewrite((y + (x + (x - y))), (2*x)) ||
 rewrite((y + (y + (x + y))), (x + (3*y))) ||
 rewrite((y + (y + (y + x))), (x + (3*y))) ||
 rewrite((y + (y + (x*2))), (2*(y + x))) ||
 rewrite((y + (z + (y + (x*2)))), (z + (2*(y + x)))) ||
 rewrite((y + (z + ((x*2) + y))), (z + (2*(x + y)))) ||
 rewrite((y + (z + (y*((x + c0)/c1)))), (z + (((x + fold((c0 + c1)))/c1)*y))) ||
 rewrite((y + (z + (((x + c0)/c1)*y))), (z + (((x + fold((c0 + c1)))/c1)*y))) ||
 rewrite((y + ((x + y) + y)), (x + (3*y))) ||
 rewrite((y + ((y + x) + y)), (x + (3*y))) ||
 rewrite((y + ((y + (x*2)) + z)), (((y + x)*2) + z)) ||
 rewrite((y + (((x*2) + y) + z)), (z + ((x + y)*2))) ||
 rewrite((y + ((x - y) + x)), (2*x)) ||
 rewrite((y + ((x*2) + y)), (2*(x + y))) ||
 rewrite((y + ((y*((x + c0)/c1)) + z)), ((y*((x + fold((c0 + c1)))/c1)) + z)) ||
 rewrite((y + ((((x + c0)/c1)*y) + z)), ((((x + fold((c0 + c1)))/c1)*y) + z)) ||
 rewrite((y + (min(z, (x - y)) + w)), (min(x, (z + y)) + w)) ||
 rewrite((y + (min((x - y), z) + w)), (min((z + y), x) + w)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((y + (x - (y*2))), (x - y)) ||
 rewrite((y + (x - min(y, 0))), (x + max(y, 0))) ||
 rewrite((y + (x - max(y, 0))), (x + min(y, 0))) ||
 rewrite((y + ((x - y) - z)), (x - z)) ||
 rewrite((z + (x - (y + z))), (x - y)) ||
 rewrite((z + (x - (z + y))), (x - y)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((y + (y*((x + c0)/c1))), (y*((x + fold((c0 + c1)))/c1))) ||
 rewrite((y + (((x + c0)/c1)*y)), (((x + fold((c0 + c1)))/c1)*y)) ||
 rewrite((z + (z*min(y, (x + -1)))), (z*min(x, (y + 1)))) ||
 rewrite((z + (z*min((y + -1), x))), (z*min((x + 1), y))) ||
 rewrite((z + (min(y, (x + -1))*z)), (min(x, (y + 1))*z)) ||
 rewrite((z + (min((y + -1), x)*z)), (min((x + 1), y)*z)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((y + min(u, min(w, min(z, (x - y))))), min(x, (min(w, min(z, u)) + y))) ||
 rewrite((y + min(u, min(w, min((x - y), z)))), min(x, (min(w, min(u, z)) + y))) ||
 rewrite((y + min(u, min(min(z, (x - y)), w))), min(x, (min(min(z, w), u) + y))) ||
 rewrite((y + min(u, min(min((x - y), z), w))), min(x, (min(min(u, w), z) + y))) ||
 rewrite((y + min(min(w, min(z, (x - y))), u)), min((min(w, min(z, u)) + y), x)) ||
 rewrite((y + min(min(w, min((x - y), z)), u)), min((y + min(min(w, u), z)), x)) ||
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
 rewrite((z + min(u, min((w + (y - z)), x))), min((z + min(u, x)), (y + w))) ||
 rewrite((z + min(u, min(((y - z) + w), x))), min((z + min(u, x)), (y + w))) ||
 rewrite((z + min(u, min((y - (w + z)), x))), min((z + min(u, x)), (y - w))) ||
 rewrite((z + min(u, min((y - (z + w)), x))), min((z + min(u, x)), (y - w))) ||
 rewrite((z + min(w, min(x, (y - z)))), min(y, (min(x, w) + z))) ||
 rewrite((z + min(w, min((y - z), x))), min((min(w, x) + z), y)) ||
 rewrite((z + min(x, (u + (y - (w + z))))), min((z + x), (u + (y - w)))) ||
 rewrite((z + min(x, (u + (y - (z + w))))), min((z + x), (u + (y - w)))) ||
 rewrite((z + min(x, (w + (y - z)))), min((y + w), (x + z))) ||
 rewrite((z + min(x, ((y - z) + w))), min((z + x), (y + w))) ||
 rewrite((z + min(x, ((y - (w + z)) + u))), min((z + x), ((y - w) + u))) ||
 rewrite((z + min(x, ((y - (z + w)) + u))), min((z + x), ((y - w) + u))) ||
 rewrite((z + min(x, (y - (w + z)))), min((y - w), (x + z))) ||
 rewrite((z + min(x, (y - (z + w)))), min((y - w), (z + x))) ||
 rewrite((z + min((u + (y - (w + z))), x)), min((u + (y - w)), (z + x))) ||
 rewrite((z + min((u + (y - (z + w))), x)), min((u + (y - w)), (z + x))) ||
 rewrite((z + min((w + (y - z)), x)), min((w + y), (z + x))) ||
 rewrite((z + min(((y - z) + w), x)), min((x + z), (w + y))) ||
 rewrite((z + min(((y - (w + z)) + u), x)), min((y - (w - u)), (z + x))) ||
 rewrite((z + min(((y - (z + w)) + u), x)), min(((y - w) + u), (z + x))) ||
 rewrite((z + min((x - (y + (w + z))), u)), min((x - (y + w)), (z + u))) ||
 rewrite((z + min((x - (y + (z + w))), u)), min((x - (y + w)), (z + u))) ||
 rewrite((z + min((x - ((w + z) + y)), u)), min((x - (w + y)), (z + u))) ||
 rewrite((z + min((x - ((z + w) + y)), u)), min((x - (y + w)), (z + u))) ||
 rewrite((z + min((y - (w + z)), x)), min((y - w), (z + x))) ||
 rewrite((z + min((y - (z + w)), x)), min((y - w), (z + x))) ||
 rewrite((z + min(min(x, (w + (y - z))), u)), min((min(x, u) + z), (y + w))) ||
 rewrite((z + min(min(x, ((y - z) + w)), u)), min((min(x, u) + z), (y + w))) ||
 rewrite((z + min(min(x, (y - z)), w)), min((min(x, w) + z), y)) ||
 rewrite((z + min(min(x, (y - (w + z))), u)), min((min(x, u) + z), (y - w))) ||
 rewrite((z + min(min(x, (y - (z + w))), u)), min((min(x, u) + z), (y - w))) ||
 rewrite((z + min(min((w + (y - z)), x), u)), min((min(x, u) + z), (y + w))) ||
 rewrite((z + min(min(((y - z) + w), x), u)), min((min(x, u) + z), (y + w))) ||
 rewrite((z + min(min((y - z), x), w)), min((min(x, w) + z), y)) ||
 rewrite((z + min(min((y - (w + z)), x), u)), min((min(x, u) + z), (y - w))) ||
 rewrite((z + min(min((y - (z + w)), x), u)), min((min(x, u) + z), (y - w))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((y + max(w, (z + (x - y)))), max((x + z), (w + y))) ||
 rewrite((y + max(w, ((x - y) + z))), max((y + w), (x + z))) ||
 rewrite((y + max((z + (x - y)), w)), max((z + x), (y + w))) ||
 rewrite((y + max(((x - y) + z), w)), max((w + y), (z + x))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((y + select(x, y, (z - y))), select(x, (2*y), z)) ||
 rewrite((y + select(x, y, (y*2))), (y*select(x, 2, 3))) ||
 rewrite((y + select(x, (y*2), y)), (y*select(x, 3, 2))) ||
 rewrite((z + select(x, 0, (y - z))), select(x, z, y)) ||
 rewrite((z + select(x, y, (y - z))), select(x, (y + z), y)) ||
 rewrite((z + select(x, (y - z), 0)), select(x, y, z)))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(((w + min(z, (x - y))) + y), (w + min(x, (z + y)))) ||
 rewrite(((w + min((x - y), z)) + y), (w + min((z + y), x))) ||
 rewrite(((x + (x - y)) + x), ((x*3) - y)) ||
 rewrite(((x + (x - y)) + y), (2*x)) ||
 rewrite(((x + ((0 - y)*c1)) + c2), ((x - (y*c1)) + c2)) ||
 rewrite(((x + min(y, ((z*c0) + c1))) + c2), (x + min((z*c0), (y + c2))), ((c1 + c2) == 0)) ||
 rewrite(((y + (x + y)) + y), (x + (3*y))) ||
 rewrite(((y + (y + x)) + y), (x + (y*3))) ||
 rewrite(((y + (x*2)) + y), ((y + x)*2)) ||
 rewrite(((z + (y + (x*2))) + y), ((2*(y + x)) + z)) ||
 rewrite(((z + ((x*2) + y)) + y), (z + (2*(x + y)))) ||
 rewrite(((z + (y*((x + c0)/c1))) + y), (z + (y*((x + fold((c0 + c1)))/c1)))) ||
 rewrite(((z + (((x + c0)/c1)*y)) + y), ((((x + fold((c0 + c1)))/c1)*y) + z)) ||
 rewrite((((y + x) + x) + x), (y + (3*x))) ||
 rewrite((((y + x) + y) + y), ((3*y) + x)) ||
 rewrite((((y + (x*2)) + z) + y), (((y + x)*2) + z)) ||
 rewrite(((((x*2) + y) + z) + y), (((x + y)*2) + z)) ||
 rewrite((((min((x + c0), y) + z) + y) + c1), ((min((y + c1), x) + z) + y), ((c0 + c1) == 0)) ||
 rewrite((((x - y) + x) + x), ((3*x) - y)) ||
 rewrite((((x - y) + x) + y), (2*x)) ||
 rewrite((((x*2) + y) + y), ((x + y)*2)) ||
 rewrite((((y*((x + c0)/c1)) + z) + y), ((y*((x + fold((c0 + c1)))/c1)) + z)) ||
 rewrite((((((0 - x) - y)*c1) + z) + c2), ((z - ((x + y)*c1)) + c2)) ||
 rewrite((((((x + c0)/c1)*y) + z) + y), ((y*((x + fold((c0 + c1)))/c1)) + z)) ||
 rewrite(((((c0 - x)/c1) + y) + c2), (((c2 - x)/c1) + y), ((((1 <= c1) || (c2 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
 rewrite(((((c0 - x)/c1) + y) + c2), (((c1 - x)/c1) + y), ((((1 <= c1) || (c2 == 0)) || (c1 <= -1)) && (((((c1*c2) + c0) <= c1) && (((c1*c2) + c0) == c1)) && (c1 <= max(((c1*c2) + c0), -1))))) ||
 rewrite(((min(x, c0) + min(y, (z + c1))) + c2), (min(x, c0) + min(z, (y + c2))), ((c1 + c2) == 0)) ||
 rewrite(((min(x, (y + c0)) + (z*c1)) + c2), (min(y, (x + c2)) + (z*c1)), ((c0 + c2) == 0)) ||
 rewrite(((min(x, ((y*c0) + c1)) + z) + c2), (min((y*c0), (c2 + x)) + z), ((c1 + c2) == 0)) ||
 rewrite(((min(z, (x - y)) + w) + y), (min(x, (z + y)) + w)) ||
 rewrite(((min(((x*c0) + c1), y) + z) + c2), (min((x*c0), (c2 + y)) + z), ((c1 + c2) == 0)) ||
 rewrite(((min((x - y), z) + w) + y), (min((z + y), x) + w)) ||
 rewrite(((min((x*c0), (y + c0)) + z) + c1), (min(((x*c0) + c1), y) + z), ((((1 <= c0) || (c1 == 0)) || (c0 <= -1)) && (((min(c1, 0) + c0) <= 0) && ((c0 + c1) == 0)))) ||
 rewrite(((min((x*c0), (y + c1)) + z) + c0), (min(((x*c0) + c0), y) + z), ((((1 <= c0) || (c1 == 0)) || (c0 <= -1)) && (((min(c1, 0) + c0) <= 0) && ((c0 + c1) == 0)))) ||
 rewrite(((min((x*c0), (y + c1)) + z) + c2), (min(((x*c0) + c2), y) + z), ((c1 + c2) == 0)) ||
 rewrite(((min(min(x, (y + c0)), c1) + z) + c2), (z + min(y, min(fold(max(max((c1 + c2), ((c1 - c0) + -1)), min((c1 + c2), ((c1 - c0) + 1)))), (x + c2)))), ((c0 + c2) == 0)) ||
 rewrite(((select(x, c0, c1) + y) + c2), (y + select(x, fold((c0 + c2)), fold((c1 + c2))))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((z + (x*y)) + (u + (w*x))), (z + (u + ((y + w)*x)))) ||
 rewrite(((z + (x*y)) + (u + (x*w))), (z + (u + ((y + w)*x)))) ||
 rewrite(((z + (x*y)) + ((w*x) + u)), (z + (((y + w)*x) + u))) ||
 rewrite(((z + (x*y)) + ((x*w) + u)), (z + (((y + w)*x) + u))) ||
 rewrite(((z + (y*x)) + (u + (w*x))), (z + (u + ((y + w)*x)))) ||
 rewrite(((z + (y*x)) + (u + (x*w))), (((y + w)*x) + (u + z))) ||
 rewrite(((z + (y*x)) + ((w*x) + u)), (z + (((y + w)*x) + u))) ||
 rewrite(((z + (y*x)) + ((x*w) + u)), (z + (((y + w)*x) + u))) ||
 rewrite((((x*y) + z) + (u + (w*x))), ((u + z) + ((y + w)*x))) ||
 rewrite((((x*y) + z) + (u + (x*w))), ((x*(y + w)) + (u + z))) ||
 rewrite((((x*y) + z) + ((w*x) + u)), ((x*(y + w)) + (z + u))) ||
 rewrite((((x*y) + z) + ((x*w) + u)), ((u + z) + (x*(y + w)))) ||
 rewrite((((y*x) + z) + (u + (w*x))), ((u + z) + ((y + w)*x))) ||
 rewrite((((y*x) + z) + (u + (x*w))), ((u + z) + ((y + w)*x))) ||
 rewrite((((y*x) + z) + ((w*x) + u)), ((u + z) + (x*(y + w)))) ||
 rewrite((((y*x) + z) + ((x*w) + u)), ((u + z) + (x*(y + w)))))))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite(((x - (z + y)) + y), (x - z)) ||
 rewrite(((x - (z + y)) + z), (x - y)) ||
 rewrite(((x - (y*2)) + y), (x - y)) ||
 rewrite(((x - min(y, 0)) + y), (x + max(y, 0))) ||
 rewrite(((x - max(y, 0)) + y), (x + min(y, 0))) ||
 rewrite((((0 - x) - (y*c1)) + c2), ((c2 - x) - (y*c1))) ||
 rewrite((((x - y) - z) + y), (x - z)) ||
 rewrite(((((x + c0)/c1) - y) + c2), (((x + c2)/c1) - y), ((((1 <= c1) || (c2 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x - y) + (y + z)), (x + z)) ||
 rewrite(((x - y) + (z + y)), (x + z)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x - (y*c0)) + (z - (y*c0))), ((x + z) - (fold((c0*2))*y))))))) ||
((a.node_type() == IRNodeType::Mul) && (
 rewrite(((y*((x + c0)/c1)) + y), (y*((x + fold((c0 + c1)))/c1))) ||
 rewrite(((z*min(y, (x + -1))) + z), (z*min(x, (y + 1)))) ||
 rewrite(((z*min((y + -1), x)) + z), (z*min((x + 1), y))) ||
 rewrite((((x + ((0 - y) - z))*c1) + c2), ((((x - y) - z)*c1) + c2)) ||
 rewrite((((0 - x)*c1) + c2), (c2 - (x*c1))) ||
 rewrite(((((0 - x) - y)*c1) + c2), (c2 - ((x + y)*c1))) ||
 rewrite(((((x + c0)/c1)*y) + y), (y*((x + fold((c0 + c1)))/c1))) ||
 rewrite(((min(y, (x + -1))*z) + z), (min(x, (y + 1))*z)) ||
 rewrite(((min((y + -1), x)*z) + z), (min((x + 1), y)*z)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*y) + (u + (w + (x*z)))), (w + (u + ((z + y)*x)))) ||
 rewrite(((x*y) + (u + (w + (z*x)))), (w + (u + ((z + y)*x)))) ||
 rewrite(((x*y) + (u + ((x*z) + w))), (w + (u + (x*(z + y))))) ||
 rewrite(((x*y) + (u + ((z*x) + w))), (w + (u + (x*(z + y))))) ||
 rewrite(((x*y) + (z + (w*y))), (z + ((x + w)*y))) ||
 rewrite(((x*y) + (z + (y*w))), (z + ((x + w)*y))) ||
 rewrite(((x*y) + ((w + (x*z)) + u)), ((w + ((z + y)*x)) + u)) ||
 rewrite(((x*y) + ((w + (z*x)) + u)), ((w + ((z + y)*x)) + u)) ||
 rewrite(((x*y) + (((x*z) + w) + u)), ((((y + z)*x) + w) + u)) ||
 rewrite(((x*y) + (((z*x) + w) + u)), ((((y + z)*x) + w) + u)) ||
 rewrite(((x*y) + ((y*z) + w)), ((y*(z + x)) + w)) ||
 rewrite(((x*y) + ((z*y) + w)), ((y*(x + z)) + w)) ||
 rewrite(((y*x) + (u + (w + (x*z)))), (w + (u + ((z + y)*x)))) ||
 rewrite(((y*x) + (u + (w + (z*x)))), (w + (u + ((z + y)*x)))) ||
 rewrite(((y*x) + (u + ((x*z) + w))), (w + (u + (x*(z + y))))) ||
 rewrite(((y*x) + (u + ((z*x) + w))), (w + (u + (x*(z + y))))) ||
 rewrite(((y*x) + (z + (w*y))), (z + ((x + w)*y))) ||
 rewrite(((y*x) + (z + (y*w))), (z + ((x + w)*y))) ||
 rewrite(((y*x) + ((w + (x*z)) + u)), ((w + ((z + y)*x)) + u)) ||
 rewrite(((y*x) + ((w + (z*x)) + u)), ((w + ((z + y)*x)) + u)) ||
 rewrite(((y*x) + (((x*z) + w) + u)), (w + ((x*(z + y)) + u))) ||
 rewrite(((y*x) + (((z*x) + w) + u)), ((((z + y)*x) + w) + u)) ||
 rewrite(((y*x) + ((y*z) + w)), ((y*(z + x)) + w)) ||
 rewrite(((y*x) + ((z*y) + w)), ((y*(x + z)) + w)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((x*2) + (y - x)), (x + y)) ||
 rewrite(((x*c0) + ((y + (x*c0)) - z)), (y + ((fold((c0*2))*x) - z))) ||
 rewrite(((x*c0) + (((x*c0) + y) - z)), ((x*fold((c0*2))) + (y - z))) ||
 rewrite(((x*y) + (z - (w*y))), (z + ((x - w)*y))) ||
 rewrite(((x*y) + (z - (y*w))), (z + ((x - w)*y))) ||
 rewrite(((x*y) + ((z - (w*y)) - u)), ((z + ((x - w)*y)) - u)) ||
 rewrite(((x*y) + ((z - (y*w)) - u)), ((z + ((x - w)*y)) - u)) ||
 rewrite(((x*y) + ((w*(y*z)) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((x*y) + ((w*(z*y)) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((x*y) + ((y*z) - w)), ((y*(z + x)) - w)) ||
 rewrite(((x*y) + ((z*y) - w)), ((y*(x + z)) - w)) ||
 rewrite(((x*y) + (((y*z)*w) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((x*y) + (((z*y)*w) - u)), ((y*(x + (z*w))) - u)) ||
 rewrite(((y*x) + (z - (w*y))), (z + ((x - w)*y))) ||
 rewrite(((y*x) + (z - (y*w))), (z + ((x - w)*y))) ||
 rewrite(((y*x) + ((z - (w*y)) - u)), ((z + ((x - w)*y)) - u)) ||
 rewrite(((y*x) + ((z - (y*w)) - u)), ((z + ((x - w)*y)) - u)) ||
 rewrite(((y*x) + ((w*(y*z)) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((y*x) + ((w*(z*y)) - u)), ((y*(x + (w*z))) - u)) ||
 rewrite(((y*x) + ((y*z) - w)), ((y*(z + x)) - w)) ||
 rewrite(((y*x) + ((z*y) - w)), ((y*(x + z)) - w)) ||
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
 rewrite((min(u, min(w, min(z, (x - y)))) + y), min(x, (min(w, min(z, u)) + y))) ||
 rewrite((min(u, min(w, min((x - y), z))) + y), min(x, (min(w, min(u, z)) + y))) ||
 rewrite((min(u, min(x, (w + (y - z)))) + z), min((min(x, u) + z), (y + w))) ||
 rewrite((min(u, min(x, ((y - z) + w))) + z), min((min(x, u) + z), (y + w))) ||
 rewrite((min(u, min(x, (y - (w + z)))) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(u, min(x, (y - (z + w)))) + z), min((z + min(x, u)), (y - w))) ||
 rewrite((min(u, min((z + (x - y)), w)) + y), min((min(w, u) + y), (z + x))) ||
 rewrite((min(u, min(((y - z) + w), x)) + z), min((min(x, u) + z), (y + w))) ||
 rewrite((min(u, min((y - (w + z)), x)) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(u, min((y - (z + w)), x)) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(u, min(min(z, (x - y)), w)) + y), min(x, (min(min(z, u), w) + y))) ||
 rewrite((min(u, min(min((x - y), z), w)) + y), min(x, (min(min(u, z), w) + y))) ||
 rewrite((min(w, min(z, (x - y))) + y), min(x, (min(z, w) + y))) ||
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
 rewrite((min((z + (x - y)), w) + y), min((z + x), (w + y))) ||
 rewrite((min(((y - z) + w), x) + z), min((y + w), (x + z))) ||
 rewrite((min(((y - (w + z)) + u), x) + z), min(((y - w) + u), (x + z))) ||
 rewrite((min(((y - (z + w)) + u), x) + z), min(((y - w) + u), (x + z))) ||
 rewrite((min((x - (y + (w + z))), u) + z), min((x - (y + w)), (u + z))) ||
 rewrite((min((x - (y + (z + w))), u) + z), min((x - (y + w)), (u + z))) ||
 rewrite((min((x - ((w + z) + y)), u) + z), min((x - (w + y)), (u + z))) ||
 rewrite((min((x - ((z + w) + y)), u) + z), min((x - (w + y)), (u + z))) ||
 rewrite((min((y - (w + z)), x) + z), min((y - w), (x + z))) ||
 rewrite((min((y - (z + w)), x) + z), min((y - w), (x + z))) ||
 rewrite((min(min(w, min(z, (x - y))), u) + y), min((min(w, min(z, u)) + y), x)) ||
 rewrite((min(min(w, min((x - y), z)), u) + y), min((min(w, min(u, z)) + y), x)) ||
 rewrite((min(min(x, (w + (y - z))), u) + z), min((min(x, u) + z), (y + w))) ||
 rewrite((min(min(x, (y + c0)), c0) + c1), min(min(y, (x + c1)), 0), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, (y + c0)), c1) + c2), min(min(y, (x + c2)), fold((c1 + c2))), ((c0 + c2) == 0)) ||
 rewrite((min(min(x, (y + c0)), (x + z)) + c1), min((min(0, z) + (x + c1)), y), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z + c1)) + c1), min((min(x, (z + c1)) + c1), y), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z + c1)) + c2), min((min(x, (z + c1)) + c2), y), ((c0 + c2) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z + x)) + c1), min(y, ((min(0, z) + x) + c1)), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, (y + c0)), (z/c1)) + c2), min(y, (min(x, (z/c1)) + c2)), ((c0 + c2) == 0)) ||
 rewrite((min(min(x, ((y + z) + c0)), z) + c1), min((min(x, z) + c1), (z + y)), ((c0 + c1) == 0)) ||
 rewrite((min(min(x, ((y - z) + w)), u) + z), min((min(x, u) + z), (y + w))) ||
 rewrite((min(min(x, ((y*c0) + c1)), z) + c2), min((min(x, z) + c2), (c0*y)), ((c1 + c2) == 0)) ||
 rewrite((min(min(x, ((y*z) + c0)), 0) + c2), min(min((y*z), c2), (x + c2)), (((c0 <= -1) || (c2 <= 0)) && ((c0 + c2) == 0))) ||
 rewrite((min(min(x, ((y/c0) + c1)), z) + c2), min((min(x, z) + c2), (y/c0)), ((c1 + c2) == 0)) ||
 rewrite((min(min(x, (min(y, c0) + c1)), z) + c2), min(min(y, (min(z, x) + c2)), c0), ((c1 + c2) == 0)) ||
 rewrite((min(min(x, (min(y, z) + c0)), c1) + c2), min(min(z, min(y, c0)), (x + c2)), (((c0 + c2) == 0) && ((c1 + c2) == c0))) ||
 rewrite((min(min(x, (max(y, c0) + c1)), z) + c2), min(max(y, c0), (min(x, z) + c2)), ((c1 + c2) == 0)) ||
 rewrite((min(min(x, (y - (w + z))), u) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(min(x, (y - (z + w))), u) + z), min((min(x, u) + z), (y - w))) ||
 rewrite((min(min(x, (y*c0)), (z*c0)) + c1), (min((min(z, y)*c0), x) + c1), (1 <= c0)) ||
 rewrite((min(min(z, (x - y)), w) + y), min((min(z, w) + y), x)) ||
 rewrite((min(min((x + c0), y), c1) + c2), min(min((y + c2), x), fold((c1 + c2))), ((c0 + c2) == 0)) ||
 rewrite((min(min((x + c0), y), (y + z)) + c1), min((min(z, 0) + (y + c1)), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), y), (z + c1)) + c1), min((min((z + c1), y) + c1), x), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), y), (z + c1)) + c2), min((min((z + c1), y) + c2), x), ((c0 + c2) == 0)) ||
 rewrite((min(min((x + c0), y), (z + y)) + c1), min(x, ((min(0, z) + y) + c1)), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), (y + z)), y) + c1), min(x, ((y + min(0, z)) + c1)), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + c0), (y + z)), z) + c1), min(x, ((z + min(y, 0)) + c1)), ((c0 + c1) == 0)) ||
 rewrite((min(min((x + y), (z + c0)), y) + c1), min((min(x, 0) + (y + c1)), z), ((c0 + c1) == 0)) ||
 rewrite((min(min((z + (x - y)), w), u) + y), min((z + x), (min(w, u) + y))) ||
 rewrite((min(min(((x + y) + c0), z), y) + c1), min((min(c1, x) + y), (z + c1)), ((c0 + c1) == 0)) ||
 rewrite((min(min(((y - z) + w), x), u) + z), min((y + w), (min(x, u) + z))) ||
 rewrite((min(min(((x*c0) + c0), y), z) + c1), min((x*c0), (min(y, z) + c1)), ((((0 <= (c0 + c1)) && (1 <= c0)) && (c0 <= 26)) && ((min(c1, 0) + c0) <= 0))) ||
 rewrite((min(min(((x*c0) + c1), y), z) + c2), min((x*c0), (min(y, z) + c2)), ((c1 + c2) == 0)) ||
 rewrite((min(min(((x/c0) + c0), y), z) + c1), min((x/c0), (min(y, z) + c1)), (((0 <= (max(c1, 0) + c0)) && ((c0 + c1) <= 0)) && ((c0 <= -1) && ((c0 + c1) <= 0)))) ||
 rewrite((min(min((max(x, c0) + c1), y), z) + c2), min(max(x, c0), (min(y, z) + c2)), ((c1 + c2) == 0)) ||
 rewrite((min(min((y - z), x), w) + z), min(y, (min(x, w) + z))) ||
 rewrite((min(min((y - (w + z)), x), u) + z), min((y - w), (min(x, u) + z))) ||
 rewrite((min(min((y - (z + w)), x), u) + z), min((y - w), (min(x, u) + z))) ||
 rewrite((min(min((x*c0), y), (z*c0)) + c1), (min(y, (min(z, x)*c0)) + c1), (1 <= c0)) ||
 rewrite((min(min((x*c0), (y + c1)), z) + c2), min((min((x*c0), z) + c2), y), ((1 <= c0) && ((c1 + c2) == 0))) ||
 rewrite((min(min((x/c0), (y + c1)), z) + c2), min((min((x/c0), z) + c2), y), ((c1 + c2) == 0)) ||
 rewrite((min(min(min(x, y), (z + c0)), c1) + c2), min(min((min(x, y) + c2), c0), z), (((c0 + c2) == 0) && ((c1 + c2) == c0))) ||
 rewrite((min(min(min(x, (y + c0)), z), c0) + c1), min(min((min(x, z) + c1), y), 0), ((c0 + c1) == 0)) ||
 rewrite((min(min(min(x, (y + c0)), z), c1) + c2), min(min((min(x, z) + c2), y), fold((c1 + c2))), ((c0 + c2) == 0)) ||
 rewrite((min(min(min(x, (y + c0)), z), c1) + c2), min(min((min(x, z) + c2), y), c0), (((c0 + c2) == 0) && ((c1 + c2) == c0))) ||
 rewrite((min(min(min(z, (x - y)), w), u) + y), min(x, (min(min(z, w), u) + y))) ||
 rewrite((min(min(min((x + c0), y), z), c1) + c2), min(min((min(z, y) + c2), x), fold((c1 + c2))), ((c0 + c2) == 0)) ||
 rewrite((min(min(min((x - y), z), w), u) + y), min(x, (min(min(z, w), u) + y))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(u, (x - (y + (z + w)))) + (w + z)), min((u + (z + w)), (x - y))) ||
 rewrite((min(u, (x - ((z + w) + y))) + (w + z)), min(((z + w) + u), (x - y))) ||
 rewrite((min(w, (x - (y + z))) + (u + z)), (min((x - y), (z + w)) + u)) ||
 rewrite((min(w, (x - (y + z))) + (z + u)), (min((x - y), (z + w)) + u)) ||
 rewrite((min(w, (x - (z + y))) + (u + z)), (min((x - y), (z + w)) + u)) ||
 rewrite((min(w, (x - (z + y))) + (z + u)), (min((x - y), (z + w)) + u)) ||
 rewrite((min(x, (w + (y - z))) + (u + z)), (min((y + w), (x + z)) + u)) ||
 rewrite((min(x, (w + (y - z))) + (z + u)), (min((w + y), (z + x)) + u)) ||
 rewrite((min(x, ((y - z) + w)) + (u + z)), (min((z + x), (y + w)) + u)) ||
 rewrite((min(x, ((y - z) + w)) + (z + u)), (min((z + x), (y + w)) + u)) ||
 rewrite((min(z, (x - y)) + (w + y)), (min(x, (z + y)) + w)) ||
 rewrite((min(z, (x - y)) + (y + w)), (min(x, (z + y)) + w)) ||
 rewrite((min(z, (x - (y*z))) + (w + (z*y))), (min(x, ((1 + y)*z)) + w)) ||
 rewrite((min(z, (x - (y*z))) + ((z*y) + w)), (min(x, (z*(y + 1))) + w)) ||
 rewrite((min(z, (x - (z*y))) + (w + (y*z))), (min(x, (z*(1 + y))) + w)) ||
 rewrite((min(z, (x - (z*y))) + ((y*z) + w)), (min(x, (z*(1 + y))) + w)) ||
 rewrite((min((w + (y - z)), x) + (u + z)), (min((w + y), (z + x)) + u)) ||
 rewrite((min((w + (y - z)), x) + (z + u)), (min((w + y), (z + x)) + u)) ||
 rewrite((min(((y - z) + w), x) + (u + z)), (min((y + w), (z + x)) + u)) ||
 rewrite((min(((y - z) + w), x) + (z + u)), (min((y + w), (z + x)) + u)) ||
 rewrite((min((x - (y + z)), w) + (u + z)), (min((x - y), (z + w)) + u)) ||
 rewrite((min((x - (y + z)), w) + (z + u)), (min((x - y), (z + w)) + u)) ||
 rewrite((min((x - (y + (z + w))), u) + (w + z)), min((u + (z + w)), (x - y))) ||
 rewrite((min((x - (z + y)), w) + (u + z)), (min((x - y), (z + w)) + u)) ||
 rewrite((min((x - (z + y)), w) + (z + u)), (min((x - y), (z + w)) + u)) ||
 rewrite((min((x - ((z + w) + y)), u) + (w + z)), min(((z + w) + u), (x - y))) ||
 rewrite((min((x - (y*z)), z) + (w + (z*y))), (min(((1 + y)*z), x) + w)) ||
 rewrite((min((x - (y*z)), z) + ((z*y) + w)), (min(x, (z*(1 + y))) + w)) ||
 rewrite((min((x - (z*y)), z) + (w + (y*z))), (min((z*(1 + y)), x) + w)) ||
 rewrite((min((x - (z*y)), z) + ((y*z) + w)), (min(x, (z*(1 + y))) + w)) ||
 rewrite((min((y - z), x) + (w + z)), (min((x + z), y) + w)) ||
 rewrite((min((y - z), x) + (z + w)), (min((x + z), y) + w)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((min(u, (x - (y + (z*w)))) + (w*z)), min((x - y), (u + (w*z)))) ||
 rewrite((min(u, (x - ((z*w) + y))) + (w*z)), min((x - y), (u + (w*z)))) ||
 rewrite((min((x - (y + (z*w))), u) + (w*z)), min((x - y), (u + (w*z)))) ||
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
 rewrite((max(w, (z + (x - y))) + (u + y)), (max((z + x), (y + w)) + u)) ||
 rewrite((max(w, (z + (x - y))) + (y + u)), (max((z + x), (y + w)) + u)) ||
 rewrite((max(w, ((x - y) + z)) + (u + y)), (max((y + w), (x + z)) + u)) ||
 rewrite((max(w, ((x - y) + z)) + (y + u)), (max((y + w), (x + z)) + u)) ||
 rewrite((max(y, x) + (z + min(x, y))), (y + (z + x))) ||
 rewrite((max(y, x) + (z + min(y, x))), (y + (z + x))) ||
 rewrite((max(y, x) + (min(x, y) + z)), (y + (x + z))) ||
 rewrite((max(y, x) + (min(y, x) + z)), (y + (x + z))) ||
 rewrite((max((z + (x - y)), w) + (u + y)), (max((z + x), (y + w)) + u)) ||
 rewrite((max((z + (x - y)), w) + (y + u)), (max((z + x), (y + w)) + u)) ||
 rewrite((max(((x - y) + z), w) + (u + y)), (max((x + z), (y + w)) + u)) ||
 rewrite((max(((x - y) + z), w) + (y + u)), (max((x + z), (y + w)) + u)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((max(x, c0) + max(min(x, c0), c1)), (c0 + max(x, c1)), (c1 <= c0)))))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite((select(x, 0, (y - z)) + z), select(x, z, y)) ||
 rewrite((select(x, y, (y + c0)) + c1), (select(x, c1, fold((c0 + c1))) + y)) ||
 rewrite((select(x, y, ((z*c0) + c1)) + c2), select(x, (c2 + y), (z*c0)), ((c1 + c2) == 0)) ||
 rewrite((select(x, y, (y - z)) + z), select(x, (y + z), y)) ||
 rewrite((select(x, y, (z - y)) + y), select(x, (2*y), z)) ||
 rewrite((select(x, y, (y*2)) + y), (select(x, 2, 3)*y)) ||
 rewrite((select(x, (y + c0), (z + c1)) + c2), select(x, (y + fold((c0 + c2))), z), ((c1 + c2) == 0)) ||
 rewrite((select(x, (y - z), 0) + z), select(x, y, z)) ||
 rewrite((select(x, (y*2), y) + y), (select(x, 3, 2)*y))))