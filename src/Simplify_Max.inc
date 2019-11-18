((b.node_type() == IRNodeType::Add) && (
 rewrite(max(y, (max(min(x, (y + c0)), z) + c1)), max(y, (z + c1)), ((c0 + c1) <= 0)) ||
 rewrite(max(z, (max(x, min(y, (z + c0))) + c1)), max(z, (x + c1)), ((c0 + c1) <= 0)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(max(x, (x*x)), (x*x)) ||
 rewrite(max(x, (x*(x + c0))), (x*(x + c0)), ((0 <= c0) && (c0 <= 2))) ||
 rewrite(max(x, (x*(c0 - x))), x, ((0 <= c0) && (c0 <= 2))) ||
 rewrite(max(x, ((x + c0)*x)), ((x + c0)*x), ((0 <= c0) && (c0 <= 2))) ||
 rewrite(max(x, ((c0 - x)*x)), x, ((0 <= c0) && (c0 <= 2))) ||
 rewrite(max(x, ((x*x)*c0)), ((x*x)*c0), (1 <= c0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(x, min(z, max(x, y))), max(x, min(z, y))) ||
 rewrite(max(x, min(z, max(y, x))), max(x, min(z, y))) ||
 rewrite(max(x, min(max(x, y), z)), max(x, min(y, z))) ||
 rewrite(max(x, min(max(y, x), z)), max(x, min(y, z))) ||
 rewrite(max(y, min(z, min(x, y))), y) ||
 rewrite(max(y, min(z, min(y, x))), y) ||
 rewrite(max(y, min(z, select(x, y, z))), select(x, y, max(z, y))) ||
 rewrite(max(y, min(min(x, y), z)), y) ||
 rewrite(max(y, min(min(y, x), z)), y) ||
 rewrite(max(y, min(select(x, y, z), z)), select(x, y, max(y, z))) ||
 rewrite(max(z, min(y, select(x, y, z))), max(z, select(x, y, z))) ||
 rewrite(max(z, min(select(x, y, z), y)), max(z, select(x, y, z))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(max(x, max(w, min(z, max(x, y)))), max(x, max(w, min(z, y)))) ||
 rewrite(max(x, max(w, min(z, max(y, x)))), max(x, max(w, min(z, y)))) ||
 rewrite(max(x, max(w, min(max(x, y), z))), max(x, max(w, min(y, z)))) ||
 rewrite(max(x, max(w, min(max(y, x), z))), max(x, max(w, min(y, z)))) ||
 rewrite(max(x, max(y, (x + c0))), max(y, x), (c0 <= 0)) ||
 rewrite(max(x, max(y, (x*x))), max(y, (x*x))) ||
 rewrite(max(x, max((x + c0), y)), max(x, y), (c0 <= 0)) ||
 rewrite(max(x, max((x*x), y)), max((x*x), y)) ||
 rewrite(max(x, max(min(z, max(x, y)), w)), max(x, max(min(z, y), w))) ||
 rewrite(max(x, max(min(z, max(y, x)), w)), max(x, max(min(z, y), w))) ||
 rewrite(max(x, max(min(max(x, y), z), w)), max(x, max(min(y, z), w))) ||
 rewrite(max(x, max(min(max(y, x), z), w)), max(x, max(min(y, z), w))) ||
 rewrite(max(y, max(x, (((y + c0)/c1)*c1))), max(x, (((y + c0)/c1)*c1)), ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(max(y, max(z, min(x, y))), max(z, y)) ||
 rewrite(max(y, max(z, min(y, x))), max(z, y)) ||
 rewrite(max(y, max(z, select(x, y, z))), max(z, y)) ||
 rewrite(max(y, max((((y + c0)/c1)*c1), x)), max((((y + c0)/c1)*c1), x), ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(max(y, max(min(x, y), z)), max(y, z)) ||
 rewrite(max(y, max(min(y, x), z)), max(y, z)) ||
 rewrite(max(y, max(select(x, y, z), z)), max(y, z)) ||
 rewrite(max(z, max(y, select(x, y, z))), max(z, y)) ||
 rewrite(max(z, max(select(x, y, z), y)), max(z, y)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(max(y, select(x, y, (y*y))), select(x, y, (y*y))) ||
 rewrite(max(y, select(x, y, min(y, z))), y) ||
 rewrite(max(y, select(x, y, min(z, y))), y) ||
 rewrite(max(y, select(x, y, max(y, z))), select(x, y, max(y, z))) ||
 rewrite(max(y, select(x, y, max(z, y))), select(x, y, max(z, y))) ||
 rewrite(max(y, select(x, (y*y), y)), select(x, (y*y), y)) ||
 rewrite(max(y, select(!x, y, z)), max(y, select(x, z, y))) ||
 rewrite(max(z, select(x, y, min(y, z))), select(x, max(y, z), z)) ||
 rewrite(max(z, select(x, y, min(z, y))), select(x, max(z, y), z)) ||
 rewrite(max(z, select(x, y, max(y, z))), max(z, y)) ||
 rewrite(max(z, select(x, y, max(z, y))), max(z, y)) ||
 rewrite(max(z, select(x, min(y, z), w)), max(z, select(x, z, w))) ||
 rewrite(max(z, select(x, min(z, y), w)), max(z, select(x, z, w))) ||
 rewrite(max(z, select(x, max(y, z), y)), max(z, y)) ||
 rewrite(max(z, select(x, max(y, z), z)), max(z, select(x, y, z))) ||
 rewrite(max(z, select(x, max(z, y), y)), max(z, y)) ||
 rewrite(max(z, select(x, max(z, y), z)), max(z, select(x, y, z))))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(max((max(x, min(y, (z + c0))) + c1), z), max((x + c1), z), ((c0 + c1) <= 0)) ||
 rewrite(max((max(min(x, (y + c0)), z) + c1), y), max((z + c1), y), ((c0 + c1) <= 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x + y), (u + (w + (y + z)))), (y + max(x, (w + (u + z))))) ||
 rewrite(max((x + y), (u + (w + (z + y)))), (y + max(x, (w + (z + u))))) ||
 rewrite(max((x + y), (u + ((y + z) + w))), (y + max(x, ((u + z) + w)))) ||
 rewrite(max((x + y), (u + ((z + y) + w))), (y + max(x, ((z + u) + w)))) ||
 rewrite(max((x + y), ((w + (y + z)) + u)), (max(x, ((w + u) + z)) + y)) ||
 rewrite(max((x + y), ((w + (z + y)) + u)), (max(x, (w + (z + u))) + y)) ||
 rewrite(max((x + y), (((y + z) + w) + u)), (max(x, ((u + z) + w)) + y)) ||
 rewrite(max((x + y), (((z + y) + w) + u)), (max(x, ((z + u) + w)) + y)) ||
 rewrite(max((x + (y + (w + z))), (u + w)), (max((x + (y + z)), u) + w)) ||
 rewrite(max((x + (y + (w + z))), (w + u)), (max((x + (y + z)), u) + w)) ||
 rewrite(max((x + (y + (z + w))), (u + w)), (max((x + (y + z)), u) + w)) ||
 rewrite(max((x + (y + (z + w))), (w + u)), (max((x + (y + z)), u) + w)) ||
 rewrite(max((x + (z + y)), (w + y)), (max((x + z), w) + y)) ||
 rewrite(max((x + (z + y)), (w + z)), (max((x + y), w) + z)) ||
 rewrite(max((x + (z + y)), (y + w)), (max((x + z), w) + y)) ||
 rewrite(max((x + (z + y)), (z + w)), (max((x + y), w) + z)) ||
 rewrite(max((x + ((w + z) + y)), (u + w)), (max((x + (z + y)), u) + w)) ||
 rewrite(max((x + ((w + z) + y)), (w + u)), (max((x + (z + y)), u) + w)) ||
 rewrite(max((x + ((z + w) + y)), (u + w)), (max((x + (z + y)), u) + w)) ||
 rewrite(max((x + ((z + w) + y)), (w + u)), (max((x + (z + y)), u) + w)) ||
 rewrite(max((y + x), (u + (w + (y + z)))), (y + max(x, (w + (u + z))))) ||
 rewrite(max((y + x), (u + (w + (z + y)))), (y + max(x, (w + (z + u))))) ||
 rewrite(max((y + x), (u + ((y + z) + w))), (y + max(x, ((u + z) + w)))) ||
 rewrite(max((y + x), (u + ((z + y) + w))), (y + max(x, ((z + u) + w)))) ||
 rewrite(max((y + x), ((w + (y + z)) + u)), (max(x, ((w + u) + z)) + y)) ||
 rewrite(max((y + x), ((w + (z + y)) + u)), (y + max((w + (z + u)), x))) ||
 rewrite(max((y + x), (((y + z) + w) + u)), (y + max(((u + z) + w), x))) ||
 rewrite(max((y + x), (((z + y) + w) + u)), (y + max(((z + u) + w), x))) ||
 rewrite(max((z + (y + x)), (w + (u + x))), (max((z + y), (w + u)) + x)) ||
 rewrite(max((z + (y + x)), (w + (u + y))), (max((z + x), (w + u)) + y)) ||
 rewrite(max((z + (y + x)), (w + (x + u))), (max((z + y), (w + u)) + x)) ||
 rewrite(max((z + (y + x)), (w + (y + u))), (max((z + x), (w + u)) + y)) ||
 rewrite(max((z + (y + x)), ((u + y) + w)), (max((z + x), (u + w)) + y)) ||
 rewrite(max((z + (y + x)), ((w + x) + u)), (max((z + y), (w + u)) + x)) ||
 rewrite(max((z + (y + x)), ((x + w) + u)), (max((z + y), (u + w)) + x)) ||
 rewrite(max((z + (y + x)), ((y + u) + w)), (max((z + x), (w + u)) + y)) ||
 rewrite(max((z + (x - y)), (w + x)), (max((z - y), w) + x)) ||
 rewrite(max((z + (x - y)), (x + w)), (max((z - y), w) + x)) ||
 rewrite(max(((y + z) + x), (u + (w + z))), (max((y + x), (u + w)) + z)) ||
 rewrite(max(((y + z) + x), (u + (z + w))), (max((y + x), (u + w)) + z)) ||
 rewrite(max(((y + z) + x), ((w + z) + u)), (max((y + x), (w + u)) + z)) ||
 rewrite(max(((y + z) + x), ((z + w) + u)), (max((y + x), (u + w)) + z)) ||
 rewrite(max(((y + (w + z)) + x), (u + w)), (max(((y + z) + x), u) + w)) ||
 rewrite(max(((y + (w + z)) + x), (w + u)), (max(((y + z) + x), u) + w)) ||
 rewrite(max(((y + (z + w)) + x), (u + w)), (max(((y + z) + x), u) + w)) ||
 rewrite(max(((y + (z + w)) + x), (w + u)), (max(((y + z) + x), u) + w)) ||
 rewrite(max(((z + y) + x), (u + (w + z))), (max((y + x), (u + w)) + z)) ||
 rewrite(max(((z + y) + x), (u + (z + w))), (max((y + x), (u + w)) + z)) ||
 rewrite(max(((z + y) + x), (w + y)), (max((z + x), w) + y)) ||
 rewrite(max(((z + y) + x), (w + z)), (max((y + x), w) + z)) ||
 rewrite(max(((z + y) + x), (y + w)), (max((z + x), w) + y)) ||
 rewrite(max(((z + y) + x), (z + w)), (max((y + x), w) + z)) ||
 rewrite(max(((z + y) + x), ((w + z) + u)), (max((y + x), (w + u)) + z)) ||
 rewrite(max(((z + y) + x), ((z + w) + u)), (max((y + x), (u + w)) + z)) ||
 rewrite(max((((z + w) + y) + x), (u + w)), (max(((z + y) + x), u) + w)) ||
 rewrite(max((((z + w) + y) + x), (w + u)), (max(((z + y) + x), u) + w)) ||
 rewrite(max((((z + y) + x) + w), (u + z)), (max(((y + x) + w), u) + z)) ||
 rewrite(max((((z + y) + x) + w), (z + u)), (max(((y + x) + w), u) + z)) ||
 rewrite(max(((x - y) + z), (w + x)), (max((z - y), w) + x)) ||
 rewrite(max(((x - y) + z), (x + w)), (max((z - y), w) + x)))))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite(max((x - max(min(x, c0), c1)), c2), (max(c0, x) + fold((c2 - c0))), ((((-2 <= c2) || (c0 <= (c1 + c2))) || ((c1 + -1) <= c0)) && (((-1 <= c2) || (c0 <= (c1 + c2))) && ((((c0 + 1) <= c1) || (c2 == 0)) && ((min(c2, 0) + c1) <= c0))))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x - y), (w + (z - y))), (max(x, (w + z)) - y)) ||
 rewrite(max((x - y), ((z - y) + w)), (max((z + w), x) - y)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(max((x - y), (z - min(y, z))), max((max(z, x) - y), 0)) ||
 rewrite(max((x - y), (z - min(z, y))), max((max(z, x) - y), 0)) ||
 rewrite(max((x - (y + z)), (w - (u + y))), (max((x - z), (w - u)) - y)) ||
 rewrite(max((x - (y + z)), (w - (y + u))), (max((x - z), (w - u)) - y)) ||
 rewrite(max((x - (z + y)), (w - (u + y))), (max((x - z), (w - u)) - y)) ||
 rewrite(max((x - (z + y)), (w - (y + u))), (max((x - z), (w - u)) - y)))))) ||
((a.node_type() == IRNodeType::Mul) && (
 rewrite(max((x*x), c0), (x*x), (c0 <= 0)) ||
 rewrite(max((x*x), x), (x*x)) ||
 rewrite(max((x*(x + c0)), x), (x*(x + c0)), ((0 <= c0) && (c0 <= 2))) ||
 rewrite(max((x*(c0 - x)), x), x, ((0 <= c0) && (c0 <= 2))) ||
 rewrite(max(((x + 0)*x), c1), ((x + 0)*x), (c1 <= 0)) ||
 rewrite(max(((x + c0)*x), x), ((x + c0)*x), ((0 <= c0) && (c0 <= 2))) ||
 rewrite(max(((c0 - x)*x), x), x, ((0 <= c0) && (c0 <= 2))) ||
 rewrite(max(((x*x)*c0), x), ((x*x)*c0), (1 <= c0)) ||
 rewrite(max((min(x, c0)*c1), c2), max(min(x, c0), c2), ((((((-1 <= c2) || (0 <= c0)) || (c0 <= c2)) || (c1 <= 1)) || (c2 <= ((c2 + 1)*c1))) && ((((((-1 <= c2) || (0 <= c0)) || (c0 <= c2)) || (c0 <= ((c2 + 1)*c1))) || (c1 <= 1)) && ((((((-1 <= c2) || (0 <= c0)) || (c0 <= c2)) || (c0 <= (c0*c1))) || (c1 <= 1)) && (((((-1 <= c2) || (1 <= c0)) || (c0 <= -1)) || (c1 <= 1)) && (((((c0*c1) <= c2) || ((c1*c2) <= (c1 + c2))) || ((((c0 <= 0) || (c2 <= 1)) || (c2 <= c0)) && (((((c0 <= 0) || (c1 <= 1)) || (c2 <= 1)) || (c2 <= c0)) || (((c0*c1) + -1) <= c0)))) && ((((c0 <= 0) || (c1 <= 1)) || ((c0*c1) <= c2)) && ((((-1 <= c2) || (c0 <= 0)) || (c1 <= 1)) && (1 <= c1))))))))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max((x*2), (x + y)), (max(x, y) + x)) ||
 rewrite(max((x*2), (y + x)), (max(x, y) + x)) ||
 rewrite(max((min(x, y)*c0), ((x*c0) + c1)), ((x*c0) + c1), ((0 <= c1) && (1 <= c0))))))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite(max(min(y, select(x, y, z)), z), max(select(x, y, z), z)) ||
 rewrite(max(min(z, min(y, x)), x), x) ||
 rewrite(max(min(z, min(y, x)), y), y) ||
 rewrite(max(min(z, max(x, y)), x), max(min(z, y), x)) ||
 rewrite(max(min(z, max(y, x)), x), max(min(z, y), x)) ||
 rewrite(max(min(z, select(x, y, z)), y), select(x, y, max(z, y))) ||
 rewrite(max(min((x*c0), c1), c2), max(min(x, c1), c2), ((((((c0 <= 1) || (c1 <= 2)) || (c1 <= c2)) || (c2 <= 1)) || ((c0*c2) <= (c0 + c2))) && ((((min(c0, c1) <= 1) || (c1 <= c2)) || ((c0*c1) <= (c0 + c2))) && (((((-1 <= c2) || (c0 <= 1)) || (c1 <= c2)) || (c2 <= ((c2 + 1)*c0))) && (((1 <= c0) || (c1 <= c2)) && ((((-1 <= c2) || (c0 <= 1)) || (c1 <= c2)) || (c1 <= ((c2 + 1)*c0)))))))) ||
 rewrite(max(min(min(y, x), z), x), x) ||
 rewrite(max(min(min(y, x), z), y), y) ||
 rewrite(max(min(max(x, y), z), x), max(min(y, z), x)) ||
 rewrite(max(min(max(y, x), z), x), max(min(y, z), x)) ||
 rewrite(max(min(select(x, y, z), y), z), max(select(x, y, z), z)) ||
 rewrite(max(min(select(x, y, z), z), y), select(x, y, max(z, y))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max(min(x, c0), (min(x, c1) + c2)), (min(x, c1) + c2), ((0 <= c2) && (c0 <= (c1 + c2)))) ||
 rewrite(max(min(x, y), (x + c0)), (x + c0), (0 <= c0)) ||
 rewrite(max(min(x, y), (max(x, z) + c0)), (max(x, z) + c0), (0 <= c0)) ||
 rewrite(max(min(x, (y + c0)), (y + c1)), (y + c1), (c0 <= c1)) ||
 rewrite(max(min(x, (y + z)), (z + max(w, y))), (z + max(w, y))) ||
 rewrite(max(min(x, (y + z)), (z + max(y, w))), (z + max(y, w))) ||
 rewrite(max(min(x, (y + z)), (max(w, y) + z)), (max(w, y) + z)) ||
 rewrite(max(min(x, (y + z)), (max(y, w) + z)), (max(y, w) + z)) ||
 rewrite(max(min(x, (z + y)), (z + max(w, y))), (z + max(w, y))) ||
 rewrite(max(min(x, (z + y)), (z + max(y, w))), (z + max(y, w))) ||
 rewrite(max(min(x, (z + y)), (max(w, y) + z)), (z + max(w, y))) ||
 rewrite(max(min(x, (z + y)), (max(y, w) + z)), (z + max(y, w))) ||
 rewrite(max(min(y, x), (x + c0)), (x + c0), (0 <= c0)) ||
 rewrite(max(min(y, x), (max(x, z) + c0)), (max(x, z) + c0), (0 <= c0)) ||
 rewrite(max(min((y + c0), x), (y + c1)), (y + c1), (c0 <= c1)) ||
 rewrite(max(min((y + z), x), (z + max(w, y))), (z + max(w, y))) ||
 rewrite(max(min((y + z), x), (z + max(y, w))), (z + max(y, w))) ||
 rewrite(max(min((y + z), x), (max(w, y) + z)), (max(w, y) + z)) ||
 rewrite(max(min((y + z), x), (max(y, w) + z)), (max(y, w) + z)) ||
 rewrite(max(min((z + y), x), (z + max(w, y))), (z + max(w, y))) ||
 rewrite(max(min((z + y), x), (z + max(y, w))), (z + max(y, w))) ||
 rewrite(max(min((z + y), x), (max(w, y) + z)), (z + max(w, y))) ||
 rewrite(max(min((z + y), x), (max(y, w) + z)), (z + max(y, w))))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(max(min(y, x), (x*x)), (x*x)) ||
 rewrite(max(min(y, x), (y*y)), (y*y)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(min(x, (y + c0)), min(y, (x + c1))), min(y, (x + c1)), ((0 <= c1) && (c0 <= 0))) ||
 rewrite(max(min(x, (y + c0)), min((x + c1), y)), min(y, (x + c1)), ((0 <= c1) && (c0 <= 0))) ||
 rewrite(max(min(y, (x + c0)), min(x, (y + c1))), min(y, (x + c0)), ((0 <= c0) && (c1 <= 0))) ||
 rewrite(max(min(y, (x + c0)), min((y + c1), x)), min(y, (c0 + x)), ((0 <= c0) && (c1 <= 0))) ||
 rewrite(max(min((x + c0), y), min(x, (y + c1))), min((x + c0), y), ((0 <= c0) && (c1 <= 0))) ||
 rewrite(max(min((x + c0), y), min((y + c1), x)), min((x + c0), y), ((0 <= c0) && (c1 <= 0))) ||
 rewrite(max(min((y + c0), x), min(y, (x + c1))), min(y, (x + c1)), ((0 <= c1) && (c0 <= 0))) ||
 rewrite(max(min((y + c0), x), min((x + c1), y)), min(y, (x + c1)), ((0 <= c1) && (c0 <= 0))))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite(max(max(w, min(z, max(x, y))), x), max(max(w, min(z, y)), x)) ||
 rewrite(max(max(w, min(z, max(y, x))), x), max(max(w, min(z, y)), x)) ||
 rewrite(max(max(w, min(max(x, y), z)), x), max(max(w, min(y, z)), x)) ||
 rewrite(max(max(w, min(max(y, x), z)), x), max(max(w, min(y, z)), x)) ||
 rewrite(max(max(x, (((y + c0)/c1)*c1)), y), max(x, (((y + c0)/c1)*c1)), ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(max(max(y, (x + c0)), x), max(y, x), (c0 <= 0)) ||
 rewrite(max(max(y, (x*x)), x), max(y, (x*x))) ||
 rewrite(max(max(y, select(x, y, z)), z), max(y, z)) ||
 rewrite(max(max(z, min(y, x)), x), max(z, x)) ||
 rewrite(max(max(z, min(y, x)), y), max(z, y)) ||
 rewrite(max(max(z, select(x, y, z)), y), max(z, y)) ||
 rewrite(max(max((x + c0), y), x), max(y, x), (c0 <= 0)) ||
 rewrite(max(max((x*x), y), x), max((x*x), y)) ||
 rewrite(max(max((((y + c0)/c1)*c1), x), y), max((((y + c0)/c1)*c1), x), ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
 rewrite(max(max(min(y, x), z), x), max(z, x)) ||
 rewrite(max(max(min(y, x), z), y), max(z, y)) ||
 rewrite(max(max(min(z, max(x, y)), w), x), max(max(min(z, y), w), x)) ||
 rewrite(max(max(min(z, max(y, x)), w), x), max(max(min(z, y), w), x)) ||
 rewrite(max(max(min(max(x, y), z), w), x), max(max(min(y, z), w), x)) ||
 rewrite(max(max(min(max(y, x), z), w), x), max(max(min(y, z), w), x)) ||
 rewrite(max(max(select(x, y, z), y), z), max(z, y)) ||
 rewrite(max(max(select(x, y, z), z), y), max(z, y)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(max(max(w, max(x, (y + z))), (u + z)), max(max(w, (max(y, u) + z)), x)) ||
 rewrite(max(max(w, max(x, (y + z))), (z + u)), max(max(w, (max(y, u) + z)), x)) ||
 rewrite(max(max(w, max(x, (z + y))), (u + z)), max(max(w, (max(y, u) + z)), x)) ||
 rewrite(max(max(w, max(x, (z + y))), (z + u)), max(max(w, (max(y, u) + z)), x)) ||
 rewrite(max(max(w, max((y + z), x)), (u + z)), max(max((max(u, y) + z), x), w)) ||
 rewrite(max(max(w, max((y + z), x)), (z + u)), max(max((max(u, y) + z), x), w)) ||
 rewrite(max(max(w, max((z + y), x)), (u + z)), max(x, max((z + max(y, u)), w))) ||
 rewrite(max(max(w, max((z + y), x)), (z + u)), max(x, max((z + max(y, u)), w))) ||
 rewrite(max(max(x, y), ((max(y, c0) + y) + c1)), max(x, ((max(y, c0) + y) + c1)), (0 <= (c0 + c1))) ||
 rewrite(max(max(x, y), (max(y, z) + c0)), max(x, (max(y, z) + c0)), (0 <= c0)) ||
 rewrite(max(max(x, (w + (y + z))), (u + y)), max(x, (max((w + z), u) + y))) ||
 rewrite(max(max(x, (w + (y + z))), (y + u)), max(x, (max((w + z), u) + y))) ||
 rewrite(max(max(x, (w + (z + y))), (u + y)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(x, (w + (z + y))), (y + u)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), (u + y)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(x, ((y + z) + w)), (y + u)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(x, ((z + y) + w)), (u + y)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(x, ((z + y) + w)), (y + u)), max((max(u, (z + w)) + y), x)) ||
 rewrite(max(max(y, x), (x + c0)), max(y, (x + c0)), (0 <= c0)) ||
 rewrite(max(max(y, x), (y + c0)), max(x, (y + c0)), (0 <= c0)) ||
 rewrite(max(max(y, x), ((max(y, c0) + y) + c1)), max(x, ((max(y, c0) + y) + c1)), (0 <= (c0 + c1))) ||
 rewrite(max(max(y, x), (max(y, z) + c0)), max(x, (max(y, z) + c0)), (0 <= c0)) ||
 rewrite(max(max(y, (x*c0)), ((z*c0) + c1)), max((max(x, (2 + z))*c0), y), (((1 <= c0) && ((c0*2) == c1)) && (c1 <= (c0*3)))) ||
 rewrite(max(max(z, (y + x)), (u + (w + x))), max(z, (x + max(y, (u + w))))) ||
 rewrite(max(max(z, (y + x)), (u + (w + y))), max(z, (y + max(x, (u + w))))) ||
 rewrite(max(max(z, (y + x)), (u + (x + w))), max((max(y, (w + u)) + x), z)) ||
 rewrite(max(max(z, (y + x)), (u + (y + w))), max(z, (y + max(x, (u + w))))) ||
 rewrite(max(max(z, (y + x)), ((w + x) + u)), max((max(y, (w + u)) + x), z)) ||
 rewrite(max(max(z, (y + x)), ((w + y) + u)), max((max(x, (w + u)) + y), z)) ||
 rewrite(max(max(z, (y + x)), ((x + w) + u)), max(z, (max(y, (u + w)) + x))) ||
 rewrite(max(max(z, (y + x)), ((y + w) + u)), max(z, (y + max(x, (u + w))))) ||
 rewrite(max(max((w + (y + z)), x), (u + y)), max(x, (max((w + z), u) + y))) ||
 rewrite(max(max((w + (y + z)), x), (y + u)), max((max(u, (w + z)) + y), x)) ||
 rewrite(max(max((w + (z + y)), x), (u + y)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max((w + (z + y)), x), (y + u)), max((max(u, (z + w)) + y), x)) ||
 rewrite(max(max((y + x), z), (u + (w + x))), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((y + x), z), (u + (w + y))), max(z, (y + max(x, (u + w))))) ||
 rewrite(max(max((y + x), z), (u + (x + w))), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((y + x), z), (u + (y + w))), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((y + x), z), ((w + x) + u)), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((y + x), z), ((w + y) + u)), max((max((w + u), x) + y), z)) ||
 rewrite(max(max((y + x), z), ((x + w) + u)), max((max((w + u), y) + x), z)) ||
 rewrite(max(max((y + x), z), ((y + w) + u)), max((max((w + u), x) + y), z)) ||
 rewrite(max(max(((y + z) + w), x), (u + y)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(((y + z) + w), x), (y + u)), max((max((z + w), u) + y), x)) ||
 rewrite(max(max(((z + y) + w), x), (u + y)), max(x, (max((z + w), u) + y))) ||
 rewrite(max(max(((z + y) + w), x), (y + u)), max((max((z + w), u) + y), x)) ||
 rewrite(max(max((x*c0), y), ((z*c0) + c1)), max((max((2 + z), x)*c0), y), (((1 <= c0) && ((c0*2) == c1)) && (c1 <= (c0*3)))) ||
 rewrite(max(max(max(x, (y + z)), w), (u + z)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max(x, (y + z)), w), (z + u)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max(x, (z + y)), w), (u + z)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max(x, (z + y)), w), (z + u)), max(max((max(y, u) + z), w), x)) ||
 rewrite(max(max(max((y + z), x), w), (u + z)), max(max((max(u, y) + z), x), w)) ||
 rewrite(max(max(max((y + z), x), w), (z + u)), max(max((max(u, y) + z), x), w)) ||
 rewrite(max(max(max((z + y), x), w), (u + z)), max(max((max(u, y) + z), w), x)) ||
 rewrite(max(max(max((z + y), x), w), (z + u)), max(max((max(u, y) + z), w), x)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(max(max(y, x), (x*x)), max(y, (x*x))) ||
 rewrite(max(max(y, x), (y*y)), max(x, (y*y))) ||
 rewrite(max(max(y, (x*c0)), (max(z, x)*c0)), max(y, (max(z, x)*c0)), (1 <= c0)) ||
 rewrite(max(max((x*c0), y), (max(z, x)*c0)), max(y, (max(z, x)*c0)), (1 <= c0)) ||
 rewrite(max(max((x*c0), (y + c1)), (z*c0)), max((max(z, x)*c0), (y + c1)), (1 <= c0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(max(y, x), min(x, z)), max(y, x)) ||
 rewrite(max(max(y, x), min(y, z)), max(y, x)) ||
 rewrite(max(max(y, x), min(z, x)), max(y, x)) ||
 rewrite(max(max(y, x), min(z, y)), max(y, x)) ||
 rewrite(max(max(z, min(x, y)), min(w, x)), max(z, min(x, max(w, y)))) ||
 rewrite(max(max(z, min(x, y)), min(x, w)), max(z, min(x, max(y, w)))) ||
 rewrite(max(max(z, min(y, x)), min(w, x)), max(z, min(x, max(w, y)))) ||
 rewrite(max(max(z, min(y, x)), min(x, w)), max(z, min(x, max(y, w)))) ||
 rewrite(max(max(z, max(x, y)), min(w, y)), max(z, max(x, y))) ||
 rewrite(max(max(z, max(x, y)), min(y, w)), max(z, max(x, y))) ||
 rewrite(max(max(z, max(y, x)), min(w, y)), max(z, max(x, y))) ||
 rewrite(max(max(z, max(y, x)), min(y, w)), max(z, max(x, y))) ||
 rewrite(max(max(min(x, y), z), min(w, x)), max(min(max(w, y), x), z)) ||
 rewrite(max(max(min(x, y), z), min(x, w)), max(min(max(w, y), x), z)) ||
 rewrite(max(max(min(y, x), z), min(w, x)), max(min(max(y, w), x), z)) ||
 rewrite(max(max(min(y, x), z), min(x, w)), max(min(max(y, w), x), z)) ||
 rewrite(max(max(max(x, y), z), min(w, y)), max(max(x, z), y)) ||
 rewrite(max(max(max(x, y), z), min(y, w)), max(max(x, z), y)) ||
 rewrite(max(max(max(y, x), z), min(w, y)), max(max(x, z), y)) ||
 rewrite(max(max(max(y, x), z), min(y, w)), max(max(x, z), y)))))) ||
((a.node_type() == IRNodeType::Select) && (
 rewrite(max(select(x, y, (y*y)), y), select(x, y, (y*y))) ||
 rewrite(max(select(x, y, min(y, z)), y), y) ||
 rewrite(max(select(x, y, min(y, z)), z), select(x, max(y, z), z)) ||
 rewrite(max(select(x, y, min(z, y)), y), y) ||
 rewrite(max(select(x, y, min(z, y)), z), select(x, max(z, y), z)) ||
 rewrite(max(select(x, y, max(y, z)), y), select(x, y, max(y, z))) ||
 rewrite(max(select(x, y, max(y, z)), z), max(y, z)) ||
 rewrite(max(select(x, y, max(z, y)), y), select(x, y, max(z, y))) ||
 rewrite(max(select(x, y, max(z, y)), z), max(z, y)) ||
 rewrite(max(select(x, (y*y), y), y), select(x, (y*y), y)) ||
 rewrite(max(select(x, min(y, z), w), z), max(select(x, z, w), z)) ||
 rewrite(max(select(x, min(z, y), w), z), max(select(x, z, w), z)) ||
 rewrite(max(select(x, max(z, y), y), y), select(x, max(z, y), y)) ||
 rewrite(max(select(x, max(z, y), y), z), max(z, y)) ||
 rewrite(max(select(x, max(z, y), z), y), max(z, y)) ||
 rewrite(max(select(x, max(z, y), z), z), max(select(x, y, z), z)) ||
 rewrite(max(select(!x, y, z), y), max(select(x, z, y), y)) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(max(select(x, y, z), min(y, z)), select(x, y, z)) ||
 rewrite(max(select(x, y, z), min(z, y)), select(x, y, z)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite(max(select(x, y, z), max(y, z)), max(z, y)) ||
 rewrite(max(select(x, y, z), max(z, y)), max(z, y)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(max(select(x, c0, c1), select(y, c0, c1)), select((y || x), c0, c1), (c1 <= c0))))))