((b.node_type() == IRNodeType::Min) && (
 rewrite(min(x, min(y, (min(x, z) + c0))), min(y, (min(x, z) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(z, x) + c0))), min(y, (min(z, x) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min((min(x, y) + c0), z)), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(x, min((min(y, x) + c0), z)), min((min(y, x) + c0), z), (c0 <= 0)) ||
 rewrite(min(x, min(min(y, (z/c0)), ((z + c1)/c0))), min(x, min(y, ((z + c1)/c0))), /* unproven */ ((((((min(((c1 + 1)/c0), 0) <= (1/c0)) && (min(((c1 + -1)/c0), 0) <= (-1/c0))) && (min(((c1 + 4)/c0), 0) <= (4/c0))) && (min(((c1 + 10)/c0), 0) <= (10/c0))) && (min(((c1 + -46046)/c0), 4188) <= (-46046/c0))))) ||
 rewrite(min(x, min(min(y, ((z + c0)/c1)), (z/c1))), min(x, min(y, ((z + c0)/c1))), /* unproven */ ((((min(((c0 + 1797)/c1), 1) <= (1797/c1)) && (min(((c0 + -5990)/c1), 2997) <= (-5990/c1))) && (min(((c0 + -1)/c1), 1) <= (-1/c1))))) ||
 rewrite(min(x, min(min((y/c0), z), ((y + c1)/c0))), min(x, min(z, ((y + c1)/c0))), /* unproven */ ((((((min(((c1 + 1)/c0), 0) <= (1/c0)) && (min(((c1 + -1)/c0), 0) <= (-1/c0))) && (min(((c1 + 4)/c0), 0) <= (4/c0))) && (min(((c1 + 10)/c0), 0) <= (10/c0))) && (min(((c1 + -46046)/c0), 4188) <= (-46046/c0))))) ||
 rewrite(min(x, min(min(((y + c0)/c1), z), (y/c1))), min(x, min(((y + c0)/c1), z)), /* unproven */ ((((min(((c0 + 1797)/c1), 1) <= (1797/c1)) && (min(((c0 + -5990)/c1), 2997) <= (-5990/c1))) && (min(((c0 + -1)/c1), 1) <= (-1/c1))))))) ||
((a.node_type() == IRNodeType::Sub) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((x - y), (z + (w - y))), (min(x, (z + w)) - y)) ||
 rewrite(min((x - y), (z + (x - (w + u)))), (x - max(y, (w - (z - u))))) ||
 rewrite(min((x - y), ((x - (z + w)) + u)), (x - max((z - (u - w)), y))) ||
 rewrite(min((x - y), ((z - y) + w)), (min((z + w), x) - y)) ||
 rewrite(min(((x + y) - z), (w + ((x + y) - u))), (min(((x + w) - u), (x - z)) + y)) ||
 rewrite(min(((x + y) - z), (w + ((y + x) - u))), (min(((w + y) - u), (y - z)) + x)) ||
 rewrite(min(((x + y) - z), (((x + y) - w) + u)), (min((y - z), ((u + y) - w)) + x)) ||
 rewrite(min(((x + y) - z), (((y + x) - w) + u)), (min((x - z), ((u + x) - w)) + y)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min((x - y), (z - (w + y))), (min((z - w), x) - y)) ||
 rewrite(min((x - y), (z - (y + w))), (min((z - w), x) - y)) ||
 rewrite(min((x - (y*z)), (w - (u + (z*y)))), (min((w - u), x) - (z*y))) ||
 rewrite(min((x - (y*z)), (w - ((z*y) + u))), (min((w - u), x) - (z*y))))))) ||
((a.node_type() == IRNodeType::Mul) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((((x + c0)/c1)*c1), (x + c2)), (x + c2), /* unproven */ (((((((((c2 <= ((c0/c1)*c1)) && ((c2 + 1) <= (((c0 + 1)/c1)*c1))) && ((c2 + 12296) <= (((c0 + 12296)/c1)*c1))) && ((c2 + 12297) <= (((c0 + 12297)/c1)*c1))) && ((c2 + 1843) <= (((c0 + 1843)/c1)*c1))) && ((c2 + 4) <= (((c0 + 4)/c1)*c1))) && ((c2 + 12294) <= (((c0 + 12294)/c1)*c1))) && ((c2 + 12295) <= (((c0 + 12295)/c1)*c1))))))))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite(min(min(x, (min(y, z) + c0)), y), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(y, z) + c0)), z), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min((min(x, y) + c0), z), x), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(min((min(x, y) + c0), z), y), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(min(min(x, (y/c0)), ((y + c1)/c0)), z), min(min(x, ((y + c1)/c0)), z), /* unproven */ ((((((min(((c1 + 1)/c0), 0) <= (1/c0)) && (min(((c1 + -1)/c0), 0) <= (-1/c0))) && (min(((c1 + 4)/c0), 0) <= (4/c0))) && (min(((c1 + 10)/c0), 0) <= (10/c0))) && (min(((c1 + -46046)/c0), 4188) <= (-46046/c0))))) ||
 rewrite(min(min(min(x, ((y + c0)/c1)), (y/c1)), z), min(min(x, ((y + c0)/c1)), z), /* unproven */ ((((min(((c0 + 1797)/c1), 1) <= (1797/c1)) && (min(((c0 + -5990)/c1), 2997) <= (-5990/c1))) && (min(((c0 + -1)/c1), 1) <= (-1/c1))))) ||
 rewrite(min(min(min((x/c0), y), ((x + c1)/c0)), z), min(min(y, ((x + c1)/c0)), z), /* unproven */ ((((((min(((c1 + 1)/c0), 0) <= (1/c0)) && (min(((c1 + -1)/c0), 0) <= (-1/c0))) && (min(((c1 + 4)/c0), 0) <= (4/c0))) && (min(((c1 + 10)/c0), 0) <= (10/c0))) && (min(((c1 + -46046)/c0), 4188) <= (-46046/c0))))) ||
 rewrite(min(min(min(((x + c0)/c1), y), (x/c1)), z), min(min(((x + c0)/c1), y), z), /* unproven */ ((((min(((c0 + 1797)/c1), 1) <= (1797/c1)) && (min(((c0 + -5990)/c1), 2997) <= (-5990/c1))) && (min(((c0 + -1)/c1), 1) <= (-1/c1))))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(min(x, y), (min(z, min(w, x)) + c0)), min(y, (min(z, min(w, x)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(z, min(w, y)) + c0)), min(x, (min(z, min(w, y)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(z, min(x, w)) + c0)), min(y, (min(z, min(x, w)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(z, min(y, w)) + c0)), min(x, (min(z, min(y, w)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(min(x, z), w) + c0)), min(y, (min(min(x, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(min(y, z), w) + c0)), min(x, (min(min(y, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(min(z, x), w) + c0)), min(y, (min(min(z, x), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(min(z, y), w) + c0)), min(x, (min(min(z, y), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (y + (z - w))), (u + (v - w))), min(x, (min((z + y), (u + v)) - w))) ||
 rewrite(min(min(x, (y + (z - w))), ((u - w) + v)), min(x, (min((z + y), (u + v)) - w))) ||
 rewrite(min(min(x, ((y - z) + w)), (u + (v - z))), min(x, (min((y + w), (u + v)) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), ((u - z) + v)), min(x, (min((y + w), (u + v)) - z))) ||
 rewrite(min(min(x, (y - z)), (w + (u - z))), min(x, (min(y, (w + u)) - z))) ||
 rewrite(min(min(x, (y - z)), ((w - z) + u)), min((min(y, (w + u)) - z), x)) ||
 rewrite(min(min(x, min(y, z)), (min(w, y) + c0)), min(min(x, min(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (min(w, z) + c0)), min(min(x, min(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (min(y, w) + c0)), min(min(x, min(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (min(z, w) + c0)), min(min(x, min(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + w)), min(min(x, (min(z, u) + w)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + z)), min(min(x, (min(w, u) + z)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (w + u)), min(min(x, (min(z, u) + w)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (z + u)), min(min(x, (min(w, u) + z)), y)) ||
 rewrite(min(min(x, min(y, (z - w))), (u + (v - w))), min(x, min(y, (min(z, (u + v)) - w)))) ||
 rewrite(min(min(x, min(y, (z - w))), ((u - w) + v)), min(min(x, (min(z, (v + u)) - w)), y)) ||
 rewrite(min(min(x, min(y, min(z, w))), (w + c0)), min(x, min(y, min(z, w))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (z + c0)), min(x, min(y, min(z, w))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (u + v)), min(min(x, min(y, (min(w, v) + u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + u)), min(min(x, min(y, (min(w, v) + u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + w)), min(min(x, min(y, (min(u, v) + w))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (w + v)), min(min(x, min(y, (min(u, v) + w))), z)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + w)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + z)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (w + v)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (z + v)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min((y + z), w)), (u + y)), min(min(x, w), (min(z, u) + y))) ||
 rewrite(min(min(x, min((y + z), w)), (u + z)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (y + u)), min(min(x, (min(u, z) + y)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (z + u)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y - z), w)), (u + (v - z))), min(x, min(w, (min(y, (u + v)) - z)))) ||
 rewrite(min(min(x, min((y - z), w)), ((u - z) + v)), min(min(x, (min((v + u), y) - z)), w)) ||
 rewrite(min(min(x, min(min(y, z), w)), (y + c0)), min(x, min(min(y, z), w)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (z + c0)), min(x, min(min(y, z), w)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + w)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + z)), min(min(x, min((min(v, w) + z), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (w + v)), min(min(x, min((min(z, v) + w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (z + v)), min(min(x, min((min(v, w) + z), u)), y)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + y)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + z)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (y + v)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (z + v)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min((x + (y - z)), w), (u + (v - z))), min((min((x + y), (u + v)) - z), w)) ||
 rewrite(min(min((x + (y - z)), w), ((u - z) + v)), min((min((x + y), (v + u)) - z), w)) ||
 rewrite(min(min(((x - y) + z), w), (u + (v - y))), min((min((x + z), (u + v)) - y), w)) ||
 rewrite(min(min(((x - y) + z), w), ((u - y) + v)), min((min((x + z), (v + u)) - y), w)) ||
 rewrite(min(min((x - y), z), (w + (u - y))), min((min(x, (w + u)) - y), z)) ||
 rewrite(min(min((x - y), z), ((w - y) + u)), min((min((w + u), x) - y), z)) ||
 rewrite(min(min(min(x, y), z), (min(w, x) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(w, y) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(x, w) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(y, w) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + y)), min(min((min(z, u) + y), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + z)), min(min((min(y, u) + z), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (y + u)), min(min((min(z, u) + y), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (z + u)), min(min((min(y, u) + z), w), x)) ||
 rewrite(min(min(min(x, (y - z)), w), (u + (v - z))), min(w, min(x, (min(y, (u + v)) - z)))) ||
 rewrite(min(min(min(x, (y - z)), w), ((u - z) + v)), min(min((min(y, (v + u)) - z), w), x)) ||
 rewrite(min(min(min(x, min(y, z)), w), (y + c0)), min(min(min(x, z), w), y), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (z + c0)), min(min(min(x, y), w), z), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + w)), min(min(min(x, (min(z, v) + w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + z)), min(min(min(x, (min(v, w) + z)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (w + v)), min(min(min(x, (min(z, v) + w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (z + v)), min(min(min(x, (min(w, v) + z)), u), y)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + y)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + z)), min(min(min((min(v, y) + z), w), u), x)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (y + v)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (z + v)), min(min(min((min(v, y) + z), w), u), x)) ||
 rewrite(min(min(min((x + y), z), w), (u + x)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (u + y)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (x + u)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (y + u)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x - y), z), w), (u + (v - y))), min(min(z, w), (min(x, (u + v)) - y))) ||
 rewrite(min(min(min((x - y), z), w), ((u - y) + v)), min(min((min((v + u), x) - y), w), z)) ||
 rewrite(min(min(min(min(x, y), z), w), (x + c0)), min(min(min(y, z), w), x), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (y + c0)), min(min(min(x, z), w), y), (0 <= c0)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + y)), min(min(min((min(v, z) + y), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + z)), min(min(min((min(y, v) + z), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (y + v)), min(min(min((min(v, z) + y), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (z + v)), min(min(min((min(y, v) + z), w), u), x)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + x)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + y)), min(min(min((min(x, v) + y), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (x + v)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (y + v)), min(min(min((min(x, v) + y), w), u), z)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(min(x, (y + (z - w))), (u - w)), min(x, (min((z + y), u) - w))) ||
 rewrite(min(min(x, ((y - z) + w)), (u - z)), min(x, (min((y + w), u) - z))) ||
 rewrite(min(min(x, (y - z)), (w - z)), min((min(y, w) - z), x)) ||
 rewrite(min(min(x, min(y, (z - w))), (u - w)), min(min(x, (min(z, u) - w)), y)) ||
 rewrite(min(min(x, min((y - z), w)), (u - z)), min(min(x, (min(y, u) - z)), w)) ||
 rewrite(min(min((x + (y - z)), w), (u - z)), min(w, (min((x + y), u) - z))) ||
 rewrite(min(min(((x - y) + z), w), (u - y)), min(w, (min((x + z), u) - y))) ||
 rewrite(min(min((x - y), z), (w - y)), min(z, (min(x, w) - y))) ||
 rewrite(min(min(min(x, (y - z)), w), (u - z)), min(min((min(y, u) - z), w), x)) ||
 rewrite(min(min(min((x - y), z), w), (u - y)), min(min((min(x, u) - y), w), z)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(min(min(x, ((y*z)*c0)), ((w*y)*c0)), min(x, (min((y*z), (w*y))*c0)), (min(c0, -2) <= (c0*3))) ||
 rewrite(min(min(x, ((y*z)*c0)), ((w*z)*c0)), min(x, (min((y*z), (w*z))*c0)), (min(c0, -2) <= (c0*3))) ||
 rewrite(min(min(x, ((y*z)*c0)), ((y*w)*c0)), min(x, (min((y*z), (y*w))*c0)), (min(c0, -2) <= (c0*3))) ||
 rewrite(min(min(x, ((y*z)*c0)), ((z*w)*c0)), min(x, (min((y*z), (z*w))*c0)), (min(c0, -2) <= (c0*3))) ||
 rewrite(min(min((x*max(y, z)), (y*c0)), (z*c0)), min((x*max(y, z)), (min(z, y)*c0)), ((min((c0*2), -7) <= (c0*7)) && (min((c0*-5), 2) <= (min(c0, 1)*2)))) ||
 rewrite(min(min((x*max(y, z)), (z*c0)), (y*c0)), min((x*max(y, z)), (min(y, z)*c0)), ((min((c0*2), -7) <= (c0*7)) && (min((c0*-5), 2) <= (min(c0, 1)*2)))) ||
 rewrite(min(min(((x*y)*c0), z), ((w*x)*c0)), min(z, (min((x*y), (w*x))*c0)), (min(c0, -2) <= (c0*3))) ||
 rewrite(min(min(((x*y)*c0), z), ((w*y)*c0)), min(z, (min((x*y), (w*y))*c0)), (min(c0, -2) <= (c0*3))) ||
 rewrite(min(min(((x*y)*c0), z), ((x*w)*c0)), min(z, (min((x*y), (x*w))*c0)), (min(c0, -2) <= (c0*3))) ||
 rewrite(min(min(((x*y)*c0), z), ((y*w)*c0)), min(z, (min((x*y), (y*w))*c0)), (min(c0, -2) <= (c0*3))) ||
 rewrite(min(min((max(x, y)*c0), (x*c1)), (y*c1)), min((max(x, y)*c0), (min(y, x)*c1)), ((c1 <= max((c1*3), c0)) && (min((c0*3), c1) <= (c1*3)))) ||
 rewrite(min(min((max(x, y)*c0), (y*c1)), (x*c1)), min((max(x, y)*c0), (min(x, y)*c1)), ((c1 <= max((c1*3), c0)) && (min((c0*3), c1) <= (c1*3)))) ||
 rewrite(min(min((max(x, y)*z), (x*c0)), (y*c0)), min((max(x, y)*z), (min(y, x)*c0)), ((min((c0*2), -7) <= (c0*7)) && (min((c0*-5), 2) <= (min(c0, 1)*2)))) ||
 rewrite(min(min((max(x, y)*z), (y*c0)), (x*c0)), min((max(x, y)*z), (min(x, y)*c0)), ((min((c0*2), -7) <= (c0*7)) && (min((c0*-5), 2) <= (min(c0, 1)*2)))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite(min(min(x, (y/c0)), ((y + c1)/c0)), min(x, ((y + c1)/c0)), /* unproven */ ((((min(((c1 + 1)/c0), 0) <= (1/c0)) && ((c1/c0) <= 0)) && (min(((c1 + 35)/c0), 7) <= (35/c0))))) ||
 rewrite(min(min(x, ((y + c0)/c1)), (y/c1)), min(x, ((y + c0)/c1)), /* unproven */ ((((min(((c0 + 1797)/c1), 1) <= (1797/c1)) && (min(((c0 + 4)/c1), 0) <= (4/c1))) && ((c0/c1) <= 0)))) ||
 rewrite(min(min((x/c0), y), ((x + c1)/c0)), min(y, ((x + c1)/c0)), /* unproven */ ((((min(((c1 + 1)/c0), 0) <= (1/c0)) && ((c1/c0) <= 0)) && (min(((c1 + 35)/c0), 7) <= (35/c0))))) ||
 rewrite(min(min(((x + c0)/c1), y), (x/c1)), min(((x + c0)/c1), y), /* unproven */ ((((min(((c0 + 1797)/c1), 1) <= (1797/c1)) && (min(((c0 + 4)/c1), 0) <= (4/c1))) && ((c0/c1) <= 0)))) ||
 rewrite(min(min(((x + c0)/c1), (y + z)), (x/c1)), min(((x + c0)/c1), (y + z)), /* unproven */ (((min(((c0 + -1795)/c1), 1797) <= (-1795/c1)) && ((c0/c1) <= 0)))) ||
 rewrite(min(min(min(x, y), (z/c0)), ((z + c1)/c0)), min(min(x, y), ((z + c1)/c0)), /* unproven */ (((min(((c1 + 1)/c0), 0) <= (1/c0)) && ((c1/c0) <= 0))))))))
