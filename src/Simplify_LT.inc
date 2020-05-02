((b.node_type() == IRNodeType::Add) && (
 rewrite((x < (y + (x + z))), (0 < (y + z))) ||
 rewrite((x < (y + (z + x))), (0 < (y + z))) ||
 rewrite((x < ((x + y) + z)), (0 < (y + z))) ||
 rewrite((x < ((y + x) + z)), (0 < (y + z))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((x < (min(min(y, z), (y + c0))/c1)), (x < (min(y, z)/c1)), prove_me(((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0) && ((min(c0, 0)/c1) <= 1)))) ||
 rewrite((x < (min(min(y, z), (z + c0))/c1)), (x < (min(y, z)/c1)), prove_me(((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0) && ((min(c0, 0)/c1) <= 1)))) ||
 rewrite((x < (min(min(y, (z + w)), (y + c0))/c1)), (x < (min(y, (z + w))/c1)), prove_me(((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0) && ((((min(c0, -1) + 46)/c1) <= 3) == ((45/c1) <= 3))))) ||
 rewrite((x < (min(min(y, min(z, w)), (w + c0))/c1)), (x < (min(y, min(z, w))/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min(y, min(z, w)), (z + c0))/c1)), (x < (min(y, min(w, z))/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min((y + z), w), (w + c0))/c1)), (x < (min((y + z), w)/c1)), prove_me(((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0) && ((((min(c0, -1) + 46)/c1) <= 3) == ((45/c1) <= 3))))) ||
 rewrite((x < (min(min(min(y, z), w), (y + c0))/c1)), (x < (min(min(z, w), y)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min(min(y, z), w), (z + c0))/c1)), (x < (min(min(y, w), z)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x + y) < (z + (w + (u + x)))), (y < (z + (w + u)))) ||
 rewrite(((x + y) < (z + (w + (u + y)))), (x < (z + (w + u)))) ||
 rewrite(((x + y) < (z + (w + (x + u)))), (y < (z + (w + u)))) ||
 rewrite(((x + y) < (z + (w + (y + u)))), (x < (z + (w + u)))) ||
 rewrite(((x + y) < (z + ((w + x) + u))), (y < (z + (w + u)))) ||
 rewrite(((x + y) < (z + ((w + y) + u))), (x < (z + (w + u)))) ||
 rewrite(((x + y) < (z + ((x + w) + u))), (y < (z + (w + u)))) ||
 rewrite(((x + y) < (z + ((y + w) + u))), (x < (z + (w + u)))) ||
 rewrite(((x + y) < ((z + (w + x)) + u)), (y < ((z + w) + u))) ||
 rewrite(((x + y) < ((z + (w + y)) + u)), (x < ((z + w) + u))) ||
 rewrite(((x + y) < ((z + (x + w)) + u)), (y < ((z + w) + u))) ||
 rewrite(((x + y) < ((z + (y + w)) + u)), (x < ((z + w) + u))) ||
 rewrite(((x + y) < (((x + z) + w) + u)), (y < ((z + w) + u))) ||
 rewrite(((x + y) < (((y + z) + w) + u)), (x < ((z + w) + u))) ||
 rewrite(((x + y) < (((z + x) + w) + u)), (y < ((z + w) + u))) ||
 rewrite(((x + y) < (((z + y) + w) + u)), (x < ((z + w) + u))) ||
 rewrite(((x + (y + z)) < (w + (u + (v + y)))), ((x + z) < (w + (u + v)))) ||
 rewrite(((x + (y + z)) < (w + (u + (v + z)))), ((x + y) < (w + (u + v)))) ||
 rewrite(((x + (y + z)) < (w + (u + (y + v)))), ((x + z) < (w + (u + v)))) ||
 rewrite(((x + (y + z)) < (w + (u + (z + v)))), ((x + y) < (w + (u + v)))) ||
 rewrite(((x + (y + z)) < (w + ((u + y) + v))), ((x + z) < (w + (u + v)))) ||
 rewrite(((x + (y + z)) < (w + ((u + z) + v))), ((x + y) < (w + (u + v)))) ||
 rewrite(((x + (y + z)) < (w + ((y + u) + v))), ((x + z) < (w + (u + v)))) ||
 rewrite(((x + (y + z)) < (w + ((z + u) + v))), ((x + y) < (w + (u + v)))) ||
 rewrite(((x + (y + z)) < ((w + (u + y)) + v)), ((x + z) < ((w + u) + v))) ||
 rewrite(((x + (y + z)) < ((w + (u + z)) + v)), ((x + y) < ((w + u) + v))) ||
 rewrite(((x + (y + z)) < ((w + (y + u)) + v)), ((x + z) < ((w + u) + v))) ||
 rewrite(((x + (y + z)) < ((w + (z + u)) + v)), ((x + y) < ((w + u) + v))) ||
 rewrite(((x + (y + z)) < (((w + y) + u) + v)), ((x + z) < ((w + u) + v))) ||
 rewrite(((x + (y + z)) < (((w + z) + u) + v)), ((x + y) < ((w + u) + v))) ||
 rewrite(((x + (y + z)) < (((y + w) + u) + v)), ((x + z) < ((w + u) + v))) ||
 rewrite(((x + (y + z)) < (((z + w) + u) + v)), ((x + y) < ((w + u) + v))) ||
 rewrite(((x + (y + (z + w))) < (u + w)), ((x + (y + z)) < u)) ||
 rewrite(((x + (y + (z + w))) < (u + z)), ((x + (y + w)) < u)) ||
 rewrite(((x + (y + (z + w))) < (w + u)), ((x + (y + z)) < u)) ||
 rewrite(((x + (y + (z + w))) < (z + u)), ((x + (y + w)) < u)) ||
 rewrite(((x + ((y + z) + w)) < (u + y)), ((x + (z + w)) < u)) ||
 rewrite(((x + ((y + z) + w)) < (u + z)), ((x + (y + w)) < u)) ||
 rewrite(((x + ((y + z) + w)) < (y + u)), ((x + (z + w)) < u)) ||
 rewrite(((x + ((y + z) + w)) < (z + u)), ((x + (y + w)) < u)) ||
 rewrite(((x + (((y - x)/c0)*c0)) < (y + c1)), false, (((c1 <= 0) && ((c1 + 1) <= ((1/c0)*c0))) && ((c1 + -1) <= ((-1/c0)*c0)))) ||
 rewrite((((x + y) + z) < (w + (u + (v + x)))), ((y + z) < (w + (u + v)))) ||
 rewrite((((x + y) + z) < (w + (u + (v + y)))), ((x + z) < (w + (u + v)))) ||
 rewrite((((x + y) + z) < (w + (u + (x + v)))), ((y + z) < (w + (u + v)))) ||
 rewrite((((x + y) + z) < (w + (u + (y + v)))), ((x + z) < (w + (u + v)))) ||
 rewrite((((x + y) + z) < (w + ((u + x) + v))), ((y + z) < (w + (u + v)))) ||
 rewrite((((x + y) + z) < (w + ((u + y) + v))), ((x + z) < (w + (u + v)))) ||
 rewrite((((x + y) + z) < (w + ((x + u) + v))), ((y + z) < (w + (u + v)))) ||
 rewrite((((x + y) + z) < (w + ((y + u) + v))), ((x + z) < (w + (u + v)))) ||
 rewrite((((x + y) + z) < ((w + (u + x)) + v)), ((y + z) < ((w + u) + v))) ||
 rewrite((((x + y) + z) < ((w + (u + y)) + v)), ((x + z) < ((w + u) + v))) ||
 rewrite((((x + y) + z) < ((w + (x + u)) + v)), ((y + z) < ((w + u) + v))) ||
 rewrite((((x + y) + z) < ((w + (y + u)) + v)), ((x + z) < ((w + u) + v))) ||
 rewrite((((x + y) + z) < (((w + x) + u) + v)), ((y + z) < ((w + u) + v))) ||
 rewrite((((x + y) + z) < (((w + y) + u) + v)), ((x + z) < ((w + u) + v))) ||
 rewrite((((x + y) + z) < (((x + w) + u) + v)), ((y + z) < ((w + u) + v))) ||
 rewrite((((x + y) + z) < (((y + w) + u) + v)), ((x + z) < ((w + u) + v))) ||
 rewrite((((x + (y + z)) + w) < (u + y)), (((x + z) + w) < u)) ||
 rewrite((((x + (y + z)) + w) < (u + z)), (((x + y) + w) < u)) ||
 rewrite((((x + (y + z)) + w) < (y + u)), (((x + z) + w) < u)) ||
 rewrite((((x + (y + z)) + w) < (z + u)), (((x + y) + w) < u)) ||
 rewrite(((((x + y) + z) + w) < (u + x)), (((y + z) + w) < u)) ||
 rewrite(((((x + y) + z) + w) < (u + y)), (((x + z) + w) < u)) ||
 rewrite(((((x + y) + z) + w) < (x + u)), (((y + z) + w) < u)) ||
 rewrite(((((x + y) + z) + w) < (y + u)), (((x + z) + w) < u)) ||
 rewrite((((((x - y)/c0)*c0) + y) < (x + c1)), false, (((c1 <= 0) && ((c1 + 1) <= ((1/c0)*c0))) && ((c1 + -1) <= ((-1/c0)*c0)))))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Div) && (
 rewrite((min(x, y) < (min(min(z, w), (w + c0))/c1)), (min(x, y) < (min(z, w)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((min(x, y) < (min(min(z, w), (z + c0))/c1)), (min(x, y) < (min(z, w)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite((max((x - y), ((y - x) + c0)) < c1), false, (((c1*2) + -1) <= c0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((max(x, y) < (min(x, z) + c0)), false, (c0 <= 0)) ||
 rewrite((max(x, y) < (min(x, (z + c0)) + c0)), false, (((((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1)) && ((min(c0, -1) + c0) <= -1)) && ((min(c0, 0) + c0) <= 1))) ||
 rewrite((max(x, y) < (min(x, (z + (w*c0))) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(x, ((z*c0) + c1)) + c1)), false, (((((min(c1, 0) + c1) <= 0) && ((min(c1, 1) + c1) <= 1)) && ((min((c0 + c1), 0) + c1) <= 0)) && ((min(c1, 0) + c1) <= 1))) ||
 rewrite((max(x, y) < (min(x, ((z*c0) + w)) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(y, z) + c0)), false, (c0 <= 0)) ||
 rewrite((max(x, y) < (min(y, (z + c0)) + c0)), false, (((((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1)) && ((min(c0, -1) + c0) <= -1)) && ((min(c0, 0) + c0) <= 1))) ||
 rewrite((max(x, y) < (min(y, (z + (w*c0))) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(y, ((z*c0) + c1)) + c1)), false, (((((min(c1, 0) + c1) <= 0) && ((min(c1, 1) + c1) <= 1)) && ((min((c0 + c1), 0) + c1) <= 0)) && ((min(c1, 0) + c1) <= 1))) ||
 rewrite((max(x, y) < (min(y, ((z*c0) + w)) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(z, x) + c0)), false, (c0 <= 0)) ||
 rewrite((max(x, y) < (min(z, y) + c0)), false, (c0 <= 0)) ||
 rewrite((max(x, y) < (min((z + c0), x) + c0)), false, (((((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1)) && ((min(c0, -1) + c0) <= -1)) && ((min(c0, 0) + c0) <= 1))) ||
 rewrite((max(x, y) < (min((z + c0), y) + c0)), false, (((((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1)) && ((min(c0, -1) + c0) <= -1)) && ((min(c0, 0) + c0) <= 1))) ||
 rewrite((max(x, y) < (min((z + (w*c0)), x) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min((z + (w*c0)), y) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(((z*c0) + c1), x) + c1)), false, (((((min(c1, 0) + c1) <= 0) && ((min(c1, 1) + c1) <= 1)) && ((min((c0 + c1), 0) + c1) <= 0)) && ((min(c1, 0) + c1) <= 1))) ||
 rewrite((max(x, y) < (min(((z*c0) + c1), y) + c1)), false, (((((min(c1, 0) + c1) <= 0) && ((min(c1, 1) + c1) <= 1)) && ((min((c0 + c1), 0) + c1) <= 0)) && ((min(c1, 0) + c1) <= 1))) ||
 rewrite((max(x, y) < (min(((z*c0) + w), x) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(((z*c0) + w), y) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, (y + (z*c0))) < (min(w, x) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, (y + (z*c0))) < (min(x, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, ((y*c0) + z)) < (min(w, x) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, ((y*c0) + z)) < (min(x, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, (y/c0)) < (min(z, (y/c0)) + c1)), false, (((c1 <= 0) && ((min((1/c0), 0) + c1) <= max((1/c0), 0))) && (c1 <= 1))) ||
 rewrite((max(x, (y/c0)) < (min((y/c0), z) + c1)), false, (((c1 <= 0) && ((min((1/c0), 0) + c1) <= max((1/c0), 0))) && (c1 <= 1))) ||
 rewrite((max((x + (y*c0)), z) < (min(w, z) + c1)), false, (c1 <= 0)) ||
 rewrite((max((x + (y*c0)), z) < (min(z, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max(((x*c0) + y), z) < (min(w, z) + c1)), false, (c1 <= 0)) ||
 rewrite((max(((x*c0) + y), z) < (min(z, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max((x/c0), y) < (min(z, (x/c0)) + c1)), false, (((c1 <= 0) && ((min((1/c0), 0) + c1) <= max((1/c0), 0))) && (c1 <= 1))) ||
 rewrite((max((x/c0), y) < (min((x/c0), z) + c1)), false, (((c1 <= 0) && ((min((1/c0), 0) + c1) <= max((1/c0), 0))) && (c1 <= 1))))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Min) && (
 rewrite((select(x, max(y, z), min(w, y)) < min(w, y)), false) ||
 rewrite((select(x, max(y, z), min(w, y)) < min(y, w)), false) ||
 rewrite((select(x, max(y, z), min(w, z)) < min(w, z)), false) ||
 rewrite((select(x, max(y, z), min(w, z)) < min(z, w)), false) ||
 rewrite((select(x, max(y, z), min(y, w)) < min(w, y)), false) ||
 rewrite((select(x, max(y, z), min(y, w)) < min(y, w)), false) ||
 rewrite((select(x, max(y, z), min(z, w)) < min(w, z)), false) ||
 rewrite((select(x, max(y, z), min(z, w)) < min(z, w)), false)))))
