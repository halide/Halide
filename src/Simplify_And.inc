((b.node_type() == IRNodeType::LE) && (
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
 rewrite((x && ((y + (z + (w + u))) <= (u + w))), (x && ((y + (z + u)) <= u))) ||
 rewrite((x && ((y + (z + (w + u))) <= (w + u))), (x && ((y + (z + u)) <= u))) ||
 rewrite((x && ((y + ((z + w) + u)) <= (w + z))), (x && ((y + (z + u)) <= z))) ||
 rewrite((x && ((y + ((z + w) + u)) <= (z + w))), (x && ((y + (w + u)) <= w))) ||
 rewrite((x && (((y + (z + w)) + u) <= (w + z))), (x && (((y + z) + u) <= z))) ||
 rewrite((x && (((y + (z + w)) + u) <= (z + w))), (x && (((y + z) + u) <= z))) ||
 rewrite((x && ((((y + z) + w) + u) <= (y + z))), (x && (((z + w) + u) <= z))) ||
 rewrite((x && ((((y + z) + w) + u) <= (z + y))), (x && (((y + w) + u) <= y))))) ||
((a.node_type() == IRNodeType::LE) && EVAL_IN_LAMBDA(
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
 rewrite((((x + (y + (z + w))) <= (w + z)) && u), (((x + (y + z)) <= z) && u)) ||
 rewrite((((x + (y + (z + w))) <= (z + w)) && u), (((x + (y + w)) <= w) && u)) ||
 rewrite((((x + ((y + z) + w)) <= (y + z)) && u), (((x + (y + w)) <= y) && u)) ||
 rewrite((((x + ((y + z) + w)) <= (z + y)) && u), (((x + (y + w)) <= y) && u)) ||
 rewrite(((((x + (y + z)) + w) <= (y + z)) && u), ((((x + z) + w) <= z) && u)) ||
 rewrite(((((x + (y + z)) + w) <= (z + y)) && u), ((((x + y) + w) <= y) && u)) ||
 rewrite((((((x + y) + z) + w) <= (x + y)) && u), ((((y + z) + w) <= y) && u)) ||
 rewrite((((((x + y) + z) + w) <= (y + x)) && u), ((((x + z) + w) <= x) && u)))) ||
((a.node_type() == IRNodeType::And) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::LT) && (
 rewrite(((x && (y < z)) && (y < (w + u))), (x && (y < min(z, (w + u))))) ||
 rewrite(((x && (y < (z + w))) && (y < u)), (x && (y < min((w + z), u)))) ||
 rewrite((((x < y) && z) && (x < (w + u))), (z && (x < min(y, (w + u))))) ||
 rewrite((((x < (y + z)) && w) && (x < u)), ((x < min(u, (y + z))) && w))))))
