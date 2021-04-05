((b.node_type() == IRNodeType::LE) && (
 rewrite((x && ((y + c0) <= z)), (x && (y < z)), ((c0 <= 1) && (0 < c0))) ||
 rewrite((x && ((y + z) <= (w + (u + (v + y))))), (x && (z <= (w + (u + v))))) ||
 rewrite((x && ((y + z) <= (w + (u + (v + z))))), (x && (y <= (w + (u + v))))) ||
 rewrite((x && ((y + z) <= (w + (u + (y + v))))), (x && (z <= (w + (u + v))))) ||
 rewrite((x && ((y + z) <= (w + (u + (z + v))))), (x && (y <= (w + (u + v))))) ||
 rewrite((x && ((y + z) <= (w + ((u + y) + v)))), (x && (z <= (w + (u + v))))) ||
 rewrite((x && ((y + z) <= (w + ((u + z) + v)))), (x && (y <= (w + (u + v))))) ||
 rewrite((x && ((y + z) <= (w + ((y + u) + v)))), (x && (z <= (w + (u + v))))) ||
 rewrite((x && ((y + z) <= (w + ((z + u) + v)))), (x && (y <= (w + (u + v))))) ||
 rewrite((x && ((y + z) <= ((w + (u + y)) + v))), (x && (z <= ((w + u) + v)))) ||
 rewrite((x && ((y + z) <= ((w + (u + z)) + v))), (x && (y <= ((w + u) + v)))) ||
 rewrite((x && ((y + z) <= ((w + (y + u)) + v))), (x && (z <= ((w + u) + v)))) ||
 rewrite((x && ((y + z) <= ((w + (z + u)) + v))), (x && (y <= ((w + u) + v)))) ||
 rewrite((x && ((y + z) <= (((w + y) + u) + v))), (x && (z <= ((w + u) + v)))) ||
 rewrite((x && ((y + z) <= (((w + z) + u) + v))), (x && (y <= ((w + u) + v)))) ||
 rewrite((x && ((y + z) <= (((y + w) + u) + v))), (x && (z <= ((w + u) + v)))) ||
 rewrite((x && ((y + z) <= (((z + w) + u) + v))), (x && (y <= ((w + u) + v)))) ||
 rewrite((x && ((y + z) <= (y*c0))), (x && (z <= y)), (((2 <= c0) && (-6 <= (c0*-1))) && (-4 <= (c0*-2)))) ||
 rewrite((x && ((y + z) <= (z*c0))), (x && (y <= z)), (((2 <= c0) && (-6 <= (c0*-1))) && (-4 <= (c0*-2)))) ||
 rewrite((x && ((y + (z + (w + u))) <= (u + w))), (x && ((y + (z + u)) <= u))) ||
 rewrite((x && ((y + (z + (w + u))) <= (w + u))), (x && ((y + (z + w)) <= w))) ||
 rewrite((x && ((y + ((z + w) + u)) <= (w + z))), (x && ((y + (w + u)) <= w))) ||
 rewrite((x && ((y + ((z + w) + u)) <= (z + w))), (x && ((y + (z + u)) <= z))) ||
 rewrite((x && (((y + (z + w)) + u) <= (w + z))), (x && (((y + z) + u) <= z))) ||
 rewrite((x && (((y + (z + w)) + u) <= (z + w))), (x && (((y + w) + u) <= w))) ||
 rewrite((x && ((((y + z) + w) + u) <= (y + z))), (x && (((z + w) + u) <= z))) ||
 rewrite((x && ((((y + z) + w) + u) <= (z + y))), (x && (((y + w) + u) <= y))) ||
 rewrite((x && (min(y, z) <= min(w, min(u, y)))), (x && (min(y, z) <= min(w, u)))) ||
 rewrite((x && (min(y, z) <= min(w, min(u, z)))), (x && (min(y, z) <= min(w, u)))) ||
 rewrite((x && (min(y, z) <= min(w, min(y, u)))), (x && (min(y, z) <= min(w, u)))) ||
 rewrite((x && (min(y, z) <= min(w, min(z, u)))), (x && (min(y, z) <= min(w, u)))) ||
 rewrite((x && (min(y, z) <= min(min(w, y), u))), (x && (min(y, z) <= min(w, u)))) ||
 rewrite((x && (min(y, z) <= min(min(w, z), u))), (x && (min(y, z) <= min(w, u)))) ||
 rewrite((x && (min(y, z) <= min(min(y, w), u))), (x && (min(y, z) <= min(w, u)))) ||
 rewrite((x && (min(y, z) <= min(min(z, w), u))), (x && (min(y, z) <= min(w, u)))) ||
 rewrite((x && (min(y, min(z, w)) <= w)), x) ||
 rewrite((x && (min(y, min(z, w)) <= z)), x) ||
 rewrite((x && (min(min(y, z), w) <= y)), x) ||
 rewrite((x && (min(min(y, z), w) <= z)), x))) ||
((a.node_type() == IRNodeType::LT) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::LT) && (
 rewrite(((x < (y + c0)) && (y < x)), false, (c0 <= 1)))))) ||
((a.node_type() == IRNodeType::LE) && EVAL_IN_LAMBDA(
 rewrite((((x + c0) <= y) && z), ((x < y) && z), ((c0 <= 1) && (0 < c0))) ||
 rewrite((((x + y) <= (z + (w + (u + x)))) && v), ((y <= (z + (w + u))) && v)) ||
 rewrite((((x + y) <= (z + (w + (u + y)))) && v), ((x <= (z + (w + u))) && v)) ||
 rewrite((((x + y) <= (z + (w + (x + u)))) && v), ((y <= (z + (w + u))) && v)) ||
 rewrite((((x + y) <= (z + (w + (y + u)))) && v), ((x <= (z + (w + u))) && v)) ||
 rewrite((((x + y) <= (z + ((w + x) + u))) && v), ((y <= (z + (w + u))) && v)) ||
 rewrite((((x + y) <= (z + ((w + y) + u))) && v), ((x <= (z + (w + u))) && v)) ||
 rewrite((((x + y) <= (z + ((x + w) + u))) && v), ((y <= (z + (w + u))) && v)) ||
 rewrite((((x + y) <= (z + ((y + w) + u))) && v), ((x <= (z + (w + u))) && v)) ||
 rewrite((((x + y) <= ((z + (w + x)) + u)) && v), ((y <= ((z + w) + u)) && v)) ||
 rewrite((((x + y) <= ((z + (w + y)) + u)) && v), ((x <= ((z + w) + u)) && v)) ||
 rewrite((((x + y) <= ((z + (x + w)) + u)) && v), ((y <= ((z + w) + u)) && v)) ||
 rewrite((((x + y) <= ((z + (y + w)) + u)) && v), ((x <= ((z + w) + u)) && v)) ||
 rewrite((((x + y) <= (((x + z) + w) + u)) && v), ((y <= ((z + w) + u)) && v)) ||
 rewrite((((x + y) <= (((y + z) + w) + u)) && v), ((x <= ((z + w) + u)) && v)) ||
 rewrite((((x + y) <= (((z + x) + w) + u)) && v), ((y <= ((z + w) + u)) && v)) ||
 rewrite((((x + y) <= (((z + y) + w) + u)) && v), ((x <= ((z + w) + u)) && v)) ||
 rewrite((((x + y) <= (x*c0)) && z), ((y <= x) && z), (((2 <= c0) && (-6 <= (c0*-1))) && (-4 <= (c0*-2)))) ||
 rewrite((((x + y) <= (y*c0)) && z), ((x <= y) && z), (((2 <= c0) && (-6 <= (c0*-1))) && (-4 <= (c0*-2)))) ||
 rewrite((((x + (y + (z + w))) <= (w + z)) && u), (((x + (y + z)) <= z) && u)) ||
 rewrite((((x + (y + (z + w))) <= (z + w)) && u), (((x + (y + w)) <= w) && u)) ||
 rewrite((((x + ((y + z) + w)) <= (y + z)) && u), (((x + (y + w)) <= y) && u)) ||
 rewrite((((x + ((y + z) + w)) <= (z + y)) && u), (((x + (z + w)) <= z) && u)) ||
 rewrite(((((x + (y + z)) + w) <= (y + z)) && u), ((((x + y) + w) <= y) && u)) ||
 rewrite(((((x + (y + z)) + w) <= (z + y)) && u), ((((x + y) + w) <= y) && u)) ||
 rewrite((((((x + y) + z) + w) <= (x + y)) && u), ((((x + z) + w) <= x) && u)) ||
 rewrite((((((x + y) + z) + w) <= (y + x)) && u), ((((x + z) + w) <= x) && u)) ||
 rewrite(((min(x, y) <= min(z, min(w, x))) && u), ((min(x, y) <= min(z, w)) && u)) ||
 rewrite(((min(x, y) <= min(z, min(w, y))) && u), ((min(x, y) <= min(z, w)) && u)) ||
 rewrite(((min(x, y) <= min(z, min(x, w))) && u), ((min(x, y) <= min(z, w)) && u)) ||
 rewrite(((min(x, y) <= min(z, min(y, w))) && u), ((min(x, y) <= min(z, w)) && u)) ||
 rewrite(((min(x, y) <= min(min(x, z), w)) && u), ((min(x, y) <= min(z, w)) && u)) ||
 rewrite(((min(x, y) <= min(min(y, z), w)) && u), ((min(x, y) <= min(z, w)) && u)) ||
 rewrite(((min(x, y) <= min(min(z, x), w)) && u), ((min(x, y) <= min(z, w)) && u)) ||
 rewrite(((min(x, y) <= min(min(z, y), w)) && u), ((min(x, y) <= min(z, w)) && u)) ||
 rewrite(((min(x, min(y, z)) <= y) && w), w) ||
 rewrite(((min(x, min(y, z)) <= z) && w), w) ||
 rewrite(((min(min(x, y), z) <= x) && w), w) ||
 rewrite(((min(min(x, y), z) <= y) && w), w))) ||
((a.node_type() == IRNodeType::And) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::LT) && (
 rewrite(((x && (y < z)) && (y < (w + u))), (x && (y < min(z, (w + u))))) ||
 rewrite(((x && (y < (z + w))) && (y < u)), (x && (y < min((z + w), u)))) ||
 rewrite((((x < y) && z) && (x < (w + u))), (z && (x < min(y, (w + u))))) ||
 rewrite((((x < (y + z)) && w) && (x < u)), (w && (x < min((z + y), u)))))) ||
((b.node_type() == IRNodeType::LE) && (
 rewrite(((x && (y <= z)) && (y <= w)), (x && (y <= min(z, w)))) ||
 rewrite((((x <= y) && z) && (x <= w)), (z && (x <= min(y, w))))))))
