((b.node_type() == IRNodeType::Add) && (
 rewrite((x + (y + min(z, ((w - (u + x)) - v)))), (y + min((w - (u + v)), (x + z)))) ||
 rewrite((x + (y + min(z, ((w - (x + u)) - v)))), (y + min((w - (v + u)), (z + x)))) ||
 rewrite((x + (y + min(((z - (w + x)) - u), v))), (y + min((z - (w + u)), (x + v)))) ||
 rewrite((x + (y + min(((z - (x + w)) - u), v))), (y + min((z - (u + w)), (x + v)))) ||
 rewrite((x + (min(y, ((z - (w + x)) - u)) + v)), (v + min((z - (w + u)), (y + x)))) ||
 rewrite((x + (min(y, ((z - (x + w)) - u)) + v)), (v + min((z - (u + w)), (y + x)))) ||
 rewrite((x + (min(((y - (x + z)) - w), u) + v)), (v + min(((y - z) - w), (x + u)))) ||
 rewrite((x + (min(((y - (z + x)) - w), u) + v)), (v + min(((y - z) - w), (x + u)))))) ||
((a.node_type() == IRNodeType::Add) && (
 rewrite(((x + min(y, ((z - (w + u)) - v))) + u), (x + min((z - (w + v)), (y + u)))) ||
 rewrite(((x + min(y, ((z - (w + u)) - v))) + w), (x + min((z - (v + u)), (y + w)))) ||
 rewrite(((x + min(((y - (z + w)) - u), v)) + w), (x + min(((y - z) - u), (v + w)))) ||
 rewrite(((x + min(((y - (z + w)) - u), v)) + z), (x + min(((y - w) - u), (v + z)))) ||
 rewrite(((min(x, ((y - (z + w)) - u)) + v) + w), (min((y - (z + u)), (x + w)) + v)) ||
 rewrite(((min(x, ((y - (z + w)) - u)) + v) + z), (min((y - (u + w)), (z + x)) + v)) ||
 rewrite(((min(((x - (y + z)) - w), u) + v) + y), (min(((x - z) - w), (u + y)) + v)) ||
 rewrite(((min(((x - (y + z)) - w), u) + v) + z), (min(((x - y) - w), (u + z)) + v)))) ||
((a.node_type() == IRNodeType::Mul) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*y) + (z + (w + (u*x)))), (w + (z + ((u + y)*x)))) ||
 rewrite(((x*y) + (z + (w + (u*y)))), (w + (z + ((u + x)*y)))) ||
 rewrite(((x*y) + (z + (w + (x*u)))), (w + (z + ((u + y)*x)))) ||
 rewrite(((x*y) + (z + (w + (y*u)))), (w + (z + ((u + x)*y)))) ||
 rewrite(((x*y) + (z + ((w*x) + u))), (u + (z + (x*(w + y))))) ||
 rewrite(((x*y) + (z + ((w*y) + u))), (u + (z + (y*(w + x))))) ||
 rewrite(((x*y) + (z + ((x*w) + u))), (u + (z + (x*(w + y))))) ||
 rewrite(((x*y) + (z + ((y*w) + u))), (u + (z + (y*(w + x))))) ||
 rewrite(((x*y) + ((z + (w*x)) + u)), ((z + ((w + y)*x)) + u)) ||
 rewrite(((x*y) + ((z + (w*y)) + u)), ((z + ((w + x)*y)) + u)) ||
 rewrite(((x*y) + ((z + (x*w)) + u)), ((z + ((w + y)*x)) + u)) ||
 rewrite(((x*y) + ((z + (y*w)) + u)), ((z + ((w + x)*y)) + u)) ||
 rewrite(((x*y) + (((x*z) + w) + u)), ((((y + z)*x) + w) + u)) ||
 rewrite(((x*y) + (((y*z) + w) + u)), (w + ((y*(z + x)) + u))) ||
 rewrite(((x*y) + (((z*x) + w) + u)), ((((y + z)*x) + w) + u)) ||
 rewrite(((x*y) + (((z*y) + w) + u)), ((((z + x)*y) + w) + u)))))) ||
((a.node_type() == IRNodeType::Min) && (
((b.node_type() == IRNodeType::Sub) && (
 rewrite((min(x, (y + (z + (w + u)))) + (z - u)), (min((x - u), (z + (w + y))) + z)) ||
 rewrite((min(x, (y + (z + (w + u)))) + (z - w)), (min((x - w), (z + (y + u))) + z)) ||
 rewrite((min(x, (y + ((z + w) + u))) + (u - w)), (min((x - w), ((z + y) + u)) + u)) ||
 rewrite((min(x, (y + ((z + w) + u))) + (u - z)), (min((x - z), ((y + w) + u)) + u)) ||
 rewrite((min(x, ((y + (z + w)) + u)) + (y - w)), (min((x - w), ((z + y) + u)) + y)) ||
 rewrite((min(x, ((y + (z + w)) + u)) + (y - z)), (min((x - z), ((y + w) + u)) + y)) ||
 rewrite((min(x, (((y + z) + w) + u)) + (w - y)), (min((x - y), ((z + w) + u)) + w)) ||
 rewrite((min(x, (((y + z) + w) + u)) + (w - z)), (min((x - z), ((y + w) + u)) + w)) ||
 rewrite((min((x + (y + (z + w))), u) + (y - w)), (min((u - w), (y + (z + x))) + y)) ||
 rewrite((min((x + (y + (z + w))), u) + (y - z)), (min((u - z), (y + (x + w))) + y)) ||
 rewrite((min((x + ((y + z) + w)), u) + (w - y)), (min((u - y), ((x + z) + w)) + w)) ||
 rewrite((min((x + ((y + z) + w)), u) + (w - z)), (min((u - z), (y + (x + w))) + w)) ||
 rewrite((min(((x + (y + z)) + w), u) + (x - y)), (min((u - y), ((x + z) + w)) + x)) ||
 rewrite((min(((x + (y + z)) + w), u) + (x - z)), (min((u - z), (y + (x + w))) + x)) ||
 rewrite((min((((x + y) + z) + w), u) + (z - x)), (min((u - x), ((y + z) + w)) + z)) ||
 rewrite((min((((x + y) + z) + w), u) + (z - y)), (min((u - y), ((z + w) + x)) + z))))))
