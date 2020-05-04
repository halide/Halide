((b.node_type() == IRNodeType::Add) && (
 rewrite((x < (y + (x + z))), (0 < (y + z))) ||
 rewrite((x < (y + (z + x))), (0 < (y + z))) ||
 rewrite((x < ((x + y) + z)), (0 < (y + z))) ||
 rewrite((x < ((y + x) + z)), (0 < (y + z))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((x < (min(min(y, z), (y + c0))/c1)), (x < (min(y, z)/c1)), prove_me(((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0) && ((min(c0, 0)/c1) <= 1)))) ||
 rewrite((x < (min(min(y, z), (z + c0))/c1)), (x < (min(y, z)/c1)), prove_me(((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0) && ((min(c0, 0)/c1) <= 1)))) ||
 rewrite((x < (min(min(y, (z + w)), (y + c0))/c1)), (x < (min(y, (z + w))/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min(y, min(z, w)), (w + c0))/c1)), (x < (min(y, min(z, w))/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min(y, min(z, w)), (z + c0))/c1)), (x < (min(y, min(w, z))/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min((y + z), w), (w + c0))/c1)), (x < (min((y + z), w)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min(min(y, z), w), (y + c0))/c1)), (x < (min(min(z, w), y)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min(min(y, z), w), (z + c0))/c1)), (x < (min(min(y, w), z)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((x < select(y, min(x, z), x)), false) ||
 rewrite((x < select(y, min(z, x), x)), false))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + (y + max(z, c0))) < y), ((x + max(z, c0)) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite(((x + (y + max(max(z, (c0 - z)), c0))) < y), ((x + max(z, (c0 - min(z, c0)))) < c0), ((c0 <= 0) && ((1 <= c0) == (c0 <= -1)))) ||
 rewrite(((x + (y + max(max((c0 - z), z), c0))) < y), ((x + max(z, (c0 - min(z, c0)))) < c0), ((c0 <= 0) && ((1 <= c0) == (c0 <= -1)))) ||
 rewrite(((x + (max(y, c0) + z)) < z), ((x + max(y, c0)) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite(((x + (max(max(y, (c0 - y)), c0) + z)) < z), ((x + max(y, (c0 - min(y, c0)))) < c0), ((c0 <= 0) && ((1 <= c0) == (c0 <= -1)))) ||
 rewrite(((x + (max(max((c0 - y), y), c0) + z)) < z), ((x + max((c0 - min(y, c0)), y)) < c0), ((c0 <= 0) && ((1 <= c0) == (c0 <= -1)))) ||
 rewrite(((x + select(y, z, c0)) < z), (x < select(y, c0, z)), (((c0 < 1) && (c0 == 0)) && (-1 <= c0))) ||
 rewrite((((x + max(y, c0)) + z) < x), ((max(y, c0) + z) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite((((x + max(max(y, (c0 - y)), c0)) + z) < x), ((z + max(y, (c0 - min(y, c0)))) < c0), ((c0 <= 0) && ((1 <= c0) == (c0 <= -1)))) ||
 rewrite((((x + max(max((c0 - y), y), c0)) + z) < x), ((z + max(y, (c0 - min(y, c0)))) < c0), ((c0 <= 0) && ((1 <= c0) == (c0 <= -1)))) ||
 rewrite((((max(x, c0) + y) + z) < y), ((max(x, c0) + z) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite((((max(max(x, (c0 - x)), c0) + y) + z) < y), ((max(x, (c0 - min(x, c0))) + z) < c0), ((c0 <= 0) && ((1 <= c0) == (c0 <= -1)))) ||
 rewrite((((max(max((c0 - x), x), c0) + y) + z) < y), ((max((c0 - min(x, c0)), x) + z) < c0), ((c0 <= 0) && ((1 <= c0) == (c0 <= -1)))) ||
 rewrite(((max(x, c0) + (y + z)) < y), ((max(x, c0) + z) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite(((max(x, c0) + (y + z)) < z), ((max(x, c0) + y) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite(((max(x, c0) + (y + max(z, c0))) < y), false, (0 <= (max(c0, 0) + max(c0, -1)))) ||
 rewrite(((max(x, c0) + (y + max(max(x, z), c0))) < y), false, (0 <= c0)) ||
 rewrite(((max(x, c0) + (y + max(max(z, w), c0))) < y), false, (0 <= (max(c0, -1) + max(c0, 0)))) ||
 rewrite(((max(x, c0) + (y + max(max(z, x), c0))) < y), false, (0 <= c0)) ||
 rewrite(((max(x, c0) + (max(y, c0) + z)) < z), false, (0 <= (max(c0, 0) + max(c0, -1)))) ||
 rewrite(((max(x, c0) + (max(max(x, y), c0) + z)) < z), false, (0 <= c0)) ||
 rewrite(((max(x, c0) + (max(max(y, x), c0) + z)) < z), false, (0 <= c0)) ||
 rewrite(((max(x, c0) + (max(max(y, z), c0) + w)) < w), false, (0 <= (max(c0, -1) + max(c0, 0)))) ||
 rewrite(((select(x, y, c0) + z) < y), (z < select(x, c0, y)), (((c0 < 1) && (c0 == 0)) && (-1 <= c0))) ||
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
 rewrite(((x + y) < (min(z, (x + min(w, y))) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min(z, (x + min(y, w))) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min(z, (y + min(w, x))) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min(z, (y + min(x, w))) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min(z, (min(w, x) + y)) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min(z, (min(w, y) + x)) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min(z, (min(x, w) + y)) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min(z, (min(y, w) + x)) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min((x + min(y, z)), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min((x + min(z, y)), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min((y + min(x, z)), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min((y + min(z, x)), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min((min(x, z) + y), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min((min(y, z) + x), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min((min(z, x) + y), w) + c0)), false, (c0 <= 0)) ||
 rewrite(((x + y) < (min((min(z, y) + x), w) + c0)), false, (c0 <= 0)) ||
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
 rewrite(((x + (y + (z + w))) < (u + (v + w))), ((x + (y + z)) < (u + v))) ||
 rewrite(((x + (y + (z + w))) < (u + (v + z))), ((x + (y + w)) < (u + v))) ||
 rewrite(((x + (y + (z + w))) < (u + (w + v))), ((x + (y + z)) < (u + v))) ||
 rewrite(((x + (y + (z + w))) < (u + (z + v))), ((x + (y + w)) < (u + v))) ||
 rewrite(((x + (y + (z + w))) < (w + u)), ((x + (y + z)) < u)) ||
 rewrite(((x + (y + (z + w))) < (z + u)), ((x + (y + w)) < u)) ||
 rewrite(((x + (y + (z + w))) < ((u + w) + v)), ((x + (y + z)) < (u + v))) ||
 rewrite(((x + (y + (z + w))) < ((u + z) + v)), ((x + (y + w)) < (u + v))) ||
 rewrite(((x + (y + (z + w))) < ((w + u) + v)), ((x + (y + z)) < (u + v))) ||
 rewrite(((x + (y + (z + w))) < ((z + u) + v)), ((x + (y + w)) < (u + v))) ||
 rewrite(((x + ((y + z) + w)) < (u + y)), ((x + (z + w)) < u)) ||
 rewrite(((x + ((y + z) + w)) < (u + z)), ((x + (y + w)) < u)) ||
 rewrite(((x + ((y + z) + w)) < (u + (v + y))), ((x + (z + w)) < (u + v))) ||
 rewrite(((x + ((y + z) + w)) < (u + (v + z))), ((x + (y + w)) < (u + v))) ||
 rewrite(((x + ((y + z) + w)) < (u + (y + v))), ((x + (z + w)) < (u + v))) ||
 rewrite(((x + ((y + z) + w)) < (u + (z + v))), ((x + (y + w)) < (u + v))) ||
 rewrite(((x + ((y + z) + w)) < (y + u)), ((x + (z + w)) < u)) ||
 rewrite(((x + ((y + z) + w)) < (z + u)), ((x + (y + w)) < u)) ||
 rewrite(((x + ((y + z) + w)) < ((u + y) + v)), ((x + (z + w)) < (u + v))) ||
 rewrite(((x + ((y + z) + w)) < ((u + z) + v)), ((x + (y + w)) < (u + v))) ||
 rewrite(((x + ((y + z) + w)) < ((y + u) + v)), ((x + (z + w)) < (u + v))) ||
 rewrite(((x + ((y + z) + w)) < ((z + u) + v)), ((x + (y + w)) < (u + v))) ||
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
 rewrite((((x + (y + z)) + w) < (u + (v + y))), (((x + z) + w) < (u + v))) ||
 rewrite((((x + (y + z)) + w) < (u + (v + z))), (((x + y) + w) < (u + v))) ||
 rewrite((((x + (y + z)) + w) < (u + (y + v))), (((x + z) + w) < (u + v))) ||
 rewrite((((x + (y + z)) + w) < (u + (z + v))), (((x + y) + w) < (u + v))) ||
 rewrite((((x + (y + z)) + w) < (y + u)), (((x + z) + w) < u)) ||
 rewrite((((x + (y + z)) + w) < (z + u)), (((x + y) + w) < u)) ||
 rewrite((((x + (y + z)) + w) < ((u + y) + v)), (((x + z) + w) < (u + v))) ||
 rewrite((((x + (y + z)) + w) < ((u + z) + v)), (((x + y) + w) < (u + v))) ||
 rewrite((((x + (y + z)) + w) < ((y + u) + v)), (((x + z) + w) < (u + v))) ||
 rewrite((((x + (y + z)) + w) < ((z + u) + v)), (((x + y) + w) < (u + v))) ||
 rewrite(((((x + y) + z) + w) < (u + x)), (((y + z) + w) < u)) ||
 rewrite(((((x + y) + z) + w) < (u + y)), (((x + z) + w) < u)) ||
 rewrite(((((x + y) + z) + w) < (u + (v + x))), (((y + z) + w) < (u + v))) ||
 rewrite(((((x + y) + z) + w) < (u + (v + y))), (((x + z) + w) < (u + v))) ||
 rewrite(((((x + y) + z) + w) < (u + (x + v))), (((y + z) + w) < (u + v))) ||
 rewrite(((((x + y) + z) + w) < (u + (y + v))), (((x + z) + w) < (u + v))) ||
 rewrite(((((x + y) + z) + w) < (x + u)), (((y + z) + w) < u)) ||
 rewrite(((((x + y) + z) + w) < (y + u)), (((x + z) + w) < u)) ||
 rewrite(((((x + y) + z) + w) < ((u + x) + v)), (((y + z) + w) < (u + v))) ||
 rewrite(((((x + y) + z) + w) < ((u + y) + v)), (((x + z) + w) < (u + v))) ||
 rewrite(((((x + y) + z) + w) < ((x + u) + v)), (((y + z) + w) < (u + v))) ||
 rewrite(((((x + y) + z) + w) < ((y + u) + v)), (((x + z) + w) < (u + v))) ||
 rewrite((((((x - y)/c0)*c0) + y) < (x + c1)), false, (((c1 <= 0) && ((c1 + 1) <= ((1/c0)*c0))) && ((c1 + -1) <= ((-1/c0)*c0)))))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((x + (max(y, c0) + (z*w))) < (w*z)), ((x + max(y, c0)) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite((((max(x, c0) + (y*z)) + w) < (z*y)), ((max(x, c0) + w) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite(((max(x, c0) + (y + (z*c1))) < (z*c1)), ((max(x, c0) + y) < c0), (c0 == 0)) ||
 rewrite(((max(x, c0) + (y + (z*w))) < (w*z)), ((max(x, c0) + y) < c0), ((1 <= c0) == (c0 <= -1))) ||
 rewrite(((max(x, c0) + ((y*c1) + z)) < (y*c1)), ((max(x, c0) + z) < c0), (c0 == 0)) ||
 rewrite(((max(x, c0) + ((y*z) + w)) < (z*y)), ((max(x, c0) + w) < c0), ((1 <= c0) == (c0 <= -1))))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Min) && (
 rewrite(((max(x, y)*c0) < min(z, (x*c0))), false, (0 <= c0)) ||
 rewrite(((max(x, y)*c0) < min(z, (y*c0))), false, (0 <= c0)) ||
 rewrite(((max(x, y)*c0) < min((x*c0), z)), false, (0 <= c0)) ||
 rewrite(((max(x, y)*c0) < min((y*c0), z)), false, (0 <= c0)))))) ||
((a.node_type() == IRNodeType::Div) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Div) && (
 rewrite((((x + y)/c0) < (((x + y) + c1)/c0)), false, prove_me((((c1/c0) <= 0) && (((c1 + 1)/c0) <= (1/c0))))) ||
 rewrite((((x + y)/c0) < (((y + x) + c1)/c0)), false, prove_me((((c1/c0) <= 0) && (((c1 + 1)/c0) <= (1/c0))))))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(x, (y + c0)) < (min(x, y) + c1)), false, ((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1)))) ||
 rewrite((min(x, (y + c0)) < (min(y, x) + c1)), false, ((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1)))) ||
 rewrite((min((x + c0), y) < (min(x, y) + c1)), false, ((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1)))) ||
 rewrite((min((x + c0), y) < (min(y, x) + c1)), false, ((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1)))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((min(x, y) < (min(min(z, w), (w + c0))/c1)), (min(x, y) < (min(z, w)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((min(x, y) < (min(min(z, w), (z + c0))/c1)), (min(x, y) < (min(z, w)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((min(x, (y + c0)) < min(y, z)), (x < min(y, z)), (0 <= c0)) ||
 rewrite((min(x, (y + c0)) < min(z, y)), (x < min(z, y)), (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(x, z)), (y < min(x, z)), (0 <= c0)) ||
 rewrite((min((x + c0), y) < min(z, x)), (y < min(z, x)), (0 <= c0)))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite((max((x - y), ((y - x) + c0)) < c1), false, (((c1*2) + -1) <= c0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((max(x, y) < (min(x, z) + c0)), false, (c0 <= 0)) ||
 rewrite((max(x, y) < (min(x, (z + c0)) + c0)), false, (((((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1)) && ((min(c0, -1) + c0) <= -1)) && ((min(c0, 0) + c0) <= 1))) ||
 rewrite((max(x, y) < (min(x, (z + (w*c0))) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(x, ((z*c0) + c1)) + c1)), false, (((((min(c1, 0) + c1) <= 0) && ((min(c1, 1) + c1) <= 1)) && ((min((c0 + c1), 0) + c1) <= 0)) && ((min(c1, 0) + c1) <= 1))) ||
 rewrite((max(x, y) < (min(x, ((z*c0) + w)) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(x, ((z*w) + c0)) + c0)), false, (((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1))) ||
 rewrite((max(x, y) < (min(y, z) + c0)), false, (c0 <= 0)) ||
 rewrite((max(x, y) < (min(y, (z + c0)) + c0)), false, (((((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1)) && ((min(c0, -1) + c0) <= -1)) && ((min(c0, 0) + c0) <= 1))) ||
 rewrite((max(x, y) < (min(y, (z + (w*c0))) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(y, ((z*c0) + c1)) + c1)), false, (((((min(c1, 0) + c1) <= 0) && ((min(c1, 1) + c1) <= 1)) && ((min((c0 + c1), 0) + c1) <= 0)) && ((min(c1, 0) + c1) <= 1))) ||
 rewrite((max(x, y) < (min(y, ((z*c0) + w)) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, y) < (min(y, ((z*w) + c0)) + c0)), false, (((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1))) ||
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
 rewrite((max(x, y) < (min(((z*w) + c0), x) + c0)), false, (((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1))) ||
 rewrite((max(x, y) < (min(((z*w) + c0), y) + c0)), false, (((min(c0, 0) + c0) <= 0) && ((min(c0, 1) + c0) <= 1))) ||
 rewrite((max(x, (y + (z*c0))) < (min(w, x) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, (y + (z*c0))) < (min(x, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, ((y*c0) + z)) < (min(w, x) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, ((y*c0) + z)) < (min(x, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, (y/c0)) < (min(z, (y/c0)) + c1)), false, (((c1 <= 0) && ((min((1/c0), 0) + c1) <= max((1/c0), 0))) && (c1 <= 1))) ||
 rewrite((max(x, (y/c0)) < (min((y/c0), z) + c1)), false, (((c1 <= 0) && ((min((1/c0), 0) + c1) <= max((1/c0), 0))) && (c1 <= 1))) ||
 rewrite((max(x, max(y, (z + w))) < (w + min(u, z))), false) ||
 rewrite((max(x, max(y, (z + w))) < (w + min(z, u))), false) ||
 rewrite((max(x, max(y, (z + w))) < (z + min(u, w))), false) ||
 rewrite((max(x, max(y, (z + w))) < (z + min(w, u))), false) ||
 rewrite((max(x, max(y, (z + w))) < (min(u, w) + z)), false) ||
 rewrite((max(x, max(y, (z + w))) < (min(u, z) + w)), false) ||
 rewrite((max(x, max(y, (z + w))) < (min(w, u) + z)), false) ||
 rewrite((max(x, max(y, (z + w))) < (min(z, u) + w)), false) ||
 rewrite((max(x, max((y + z), w)) < (y + min(u, z))), false) ||
 rewrite((max(x, max((y + z), w)) < (y + min(z, u))), false) ||
 rewrite((max(x, max((y + z), w)) < (z + min(u, y))), false) ||
 rewrite((max(x, max((y + z), w)) < (z + min(y, u))), false) ||
 rewrite((max(x, max((y + z), w)) < (min(u, y) + z)), false) ||
 rewrite((max(x, max((y + z), w)) < (min(u, z) + y)), false) ||
 rewrite((max(x, max((y + z), w)) < (min(y, u) + z)), false) ||
 rewrite((max(x, max((y + z), w)) < (min(z, u) + y)), false) ||
 rewrite((max((x + (y*c0)), z) < (min(w, z) + c1)), false, (c1 <= 0)) ||
 rewrite((max((x + (y*c0)), z) < (min(z, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max(((x*c0) + y), z) < (min(w, z) + c1)), false, (c1 <= 0)) ||
 rewrite((max(((x*c0) + y), z) < (min(z, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max((x/c0), y) < (min(z, (x/c0)) + c1)), false, (((c1 <= 0) && ((min((1/c0), 0) + c1) <= max((1/c0), 0))) && (c1 <= 1))) ||
 rewrite((max((x/c0), y) < (min((x/c0), z) + c1)), false, (((c1 <= 0) && ((min((1/c0), 0) + c1) <= max((1/c0), 0))) && (c1 <= 1))) ||
 rewrite((max(max(x, (y + z)), w) < (y + min(u, z))), false) ||
 rewrite((max(max(x, (y + z)), w) < (y + min(z, u))), false) ||
 rewrite((max(max(x, (y + z)), w) < (z + min(u, y))), false) ||
 rewrite((max(max(x, (y + z)), w) < (z + min(y, u))), false) ||
 rewrite((max(max(x, (y + z)), w) < (min(u, y) + z)), false) ||
 rewrite((max(max(x, (y + z)), w) < (min(u, z) + y)), false) ||
 rewrite((max(max(x, (y + z)), w) < (min(y, u) + z)), false) ||
 rewrite((max(max(x, (y + z)), w) < (min(z, u) + y)), false) ||
 rewrite((max(max((x + y), z), w) < (x + min(u, y))), false) ||
 rewrite((max(max((x + y), z), w) < (x + min(y, u))), false) ||
 rewrite((max(max((x + y), z), w) < (y + min(u, x))), false) ||
 rewrite((max(max((x + y), z), w) < (y + min(x, u))), false) ||
 rewrite((max(max((x + y), z), w) < (min(u, x) + y)), false) ||
 rewrite((max(max((x + y), z), w) < (min(u, y) + x)), false) ||
 rewrite((max(max((x + y), z), w) < (min(x, u) + y)), false) ||
 rewrite((max(max((x + y), z), w) < (min(y, u) + x)), false))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((max(x, min(y, z)) < min(y, z)), false) ||
 rewrite((max(x, min(y, z)) < min(z, y)), false) ||
 rewrite((max(x, min(y, (z + w))) < min(y, (w + z))), false) ||
 rewrite((max(x, min(y, (z + w))) < min((w + z), y)), false) ||
 rewrite((max(x, min((y + z), w)) < min(w, (z + y))), false) ||
 rewrite((max(x, min((y + z), w)) < min((z + y), w)), false) ||
 rewrite((max(min(x, y), z) < min(x, y)), false) ||
 rewrite((max(min(x, y), z) < min(y, x)), false) ||
 rewrite((max(min(x, (y + z)), w) < min(x, (z + y))), false) ||
 rewrite((max(min(x, (y + z)), w) < min((z + y), x)), false) ||
 rewrite((max(min((x + y), z), w) < min(z, (y + x))), false) ||
 rewrite((max(min((x + y), z), w) < min((y + x), z)), false))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((max(x, y) < max(z, min(w, x))), (max(x, y) < z)) ||
 rewrite((max(x, y) < max(z, min(w, y))), (max(x, y) < z)) ||
 rewrite((max(x, y) < max(z, min(x, w))), (max(x, y) < z)) ||
 rewrite((max(x, y) < max(z, min(y, w))), (max(x, y) < z)) ||
 rewrite((max(x, y) < max(min(x, z), w)), (max(x, y) < w)) ||
 rewrite((max(x, y) < max(min(y, z), w)), (max(x, y) < w)) ||
 rewrite((max(x, y) < max(min(z, x), w)), (max(x, y) < w)) ||
 rewrite((max(x, y) < max(min(z, y), w)), (max(x, y) < w)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, y, z) < z), (x && (y < z))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((select(x, max(y, z), min(w, y)) < min(y, w)), false) ||
 rewrite((select(x, max(y, z), min(w, z)) < min(z, w)), false) ||
 rewrite((select(x, max(y, z), min(y, w)) < min(w, y)), false) ||
 rewrite((select(x, max(y, z), min(z, w)) < min(w, z)), false)))))
