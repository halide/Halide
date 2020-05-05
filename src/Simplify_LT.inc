((b.node_type() == IRNodeType::Add) && (
 rewrite((x < (y + (x + z))), (0 < (y + z))) ||
 rewrite((x < (y + (z + x))), (0 < (y + z))) ||
 rewrite((x < ((x + y) + z)), (0 < (y + z))) ||
 rewrite((x < ((y + x) + z)), (0 < (y + z))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((x < (min(min(y, z), (min(w, y) + c0))/c1)), (x < (min(min(y, z), (w + c0))/c1)), prove_me((((min(c0, 0)/c1) <= 0) == (((min(c0, -1) + 1)/c1) <= 0)))) ||
 rewrite((x < (min(min(y, z), (min(w, z) + c0))/c1)), (x < (min(min(y, z), (w + c0))/c1)), prove_me((((min(c0, 0)/c1) <= 0) == (((min(c0, -1) + 1)/c1) <= 0)))) ||
 rewrite((x < (min(min(y, z), (min(y, w) + c0))/c1)), (x < (min(min(y, z), (w + c0))/c1)), prove_me((((min(c0, 0)/c1) <= 0) == (((min(c0, -1) + 1)/c1) <= 0)))) ||
 rewrite((x < (min(min(y, z), (min(z, w) + c0))/c1)), (x < (min(min(y, z), (w + c0))/c1)), prove_me((((min(c0, 0)/c1) <= 0) == (((min(c0, -1) + 1)/c1) <= 0)))) ||
 rewrite((x < (min(min(y, (z + w)), (y + c0))/c1)), (x < (min(y, (z + w))/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((x < (min(min((y + z), w), (w + c0))/c1)), (x < (min((y + z), w)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((x < min(y, min(x, z))), false) ||
 rewrite((x < min(y, min(z, x))), false) ||
 rewrite((x < min(min(x, y), z)), false) ||
 rewrite((x < min(min(y, x), z)), false))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + min(y, c0)) < y), (x < max(y, c0)), (((c0 < 1) && ((0 <= c0) == (c0 <= 0))) && ((-1 <= c0) == (c0 <= 1)))) ||
 rewrite(((min(x, c0) + y) < x), (y < max(x, c0)), (((c0 < 1) && ((0 <= c0) == (c0 <= 0))) && ((-1 <= c0) == (c0 <= 1)))) ||
 rewrite(((max(x, (y*y)) + max(z, (w*w))) < c0), false, (c0 <= 0)) ||
 rewrite(((max(x, (y*y)) + max((z*z), w)) < c0), false, (c0 <= 0)) ||
 rewrite(((max((x*x), y) + max(z, (w*w))) < c0), false, (c0 <= 0)) ||
 rewrite(((max((x*x), y) + max((z*z), w)) < c0), false, (c0 <= 0)) ||
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
 rewrite(((x + (((y - x)/c0)*c0)) < (y + c1)), false, ((c1 <= -1) && ((c1 + -1) <= ((-1/c0)*c0)))) ||
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
 rewrite((((((x - y)/c0)*c0) + y) < (x + c1)), false, ((c1 <= -1) && ((c1 + -1) <= ((-1/c0)*c0)))))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
 rewrite(((x*x) < c0), false, (c0 <= 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x*c0) < (x + y)), (x < y), ((1 <= c0) && (-4 <= (c0*-2)))) ||
 rewrite(((x*c0) < (y + x)), (x < y), ((1 <= c0) && (-4 <= (c0*-2)))))))) ||
((a.node_type() == IRNodeType::Div) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Div) && (
 rewrite(((x/c0) < (((x + min(y, c1)) + c2)/c0)), false, prove_me((max(max(((min(c1, 0) + c2)/c0), ((min(c1, 1) + c2)/c0)), (((min(c1, 0) + c2) + 1)/c0)) <= 0))) ||
 rewrite(((x/c0) < (((min(y, c1) + x) + c2)/c0)), false, prove_me((max(max(((min(c1, 0) + c2)/c0), ((min(c1, 1) + c2)/c0)), (((min(c1, 0) + c2) + 1)/c0)) <= 0))) ||
 rewrite((((x + y)/c0) < ((x + min(y, z))/c0)), false, prove_me(((-1236/c0) <= 0))) ||
 rewrite((((x + y)/c0) < ((x + min(z, y))/c0)), false, prove_me(((-1236/c0) <= 0))) ||
 rewrite((((x + y)/c0) < ((y + min(x, z))/c0)), false, prove_me(((-1236/c0) <= 0))) ||
 rewrite((((x + y)/c0) < ((y + min(z, x))/c0)), false, prove_me(((-1236/c0) <= 0))) ||
 rewrite((((x + y)/c0) < (((x + y) + c1)/c0)), false, prove_me((max((c1/c0), ((c1 + 1)/c0)) <= 0))) ||
 rewrite((((x + y)/c0) < (((y + x) + c1)/c0)), false, prove_me((max((c1/c0), ((c1 + 1)/c0)) <= 0))) ||
 rewrite((((x + y)/c0) < ((min(x, z) + y)/c0)), false, prove_me(((-1236/c0) <= 0))) ||
 rewrite((((x + y)/c0) < ((min(y, z) + x)/c0)), false, prove_me(((-1236/c0) <= 0))) ||
 rewrite((((x + y)/c0) < ((min(z, x) + y)/c0)), false, prove_me(((-1236/c0) <= 0))) ||
 rewrite((((x + y)/c0) < ((min(z, y) + x)/c0)), false, prove_me(((-1236/c0) <= 0))))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite((min(x, (y + c0)) < (min(x, y) + c1)), false, ((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1)))) ||
 rewrite((min(x, (y + c0)) < (min(x, y) + c1)), false, (((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1))) && (c1 <= min(c0, 1)))) ||
 rewrite((min(x, (y + c0)) < (min(y, x) + c1)), false, ((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1)))) ||
 rewrite((min(x, (y + c0)) < (min(y, x) + c1)), false, (((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1))) && (c1 <= min(c0, 1)))) ||
 rewrite((min((x + c0), y) < (min(x, y) + c1)), false, ((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1)))) ||
 rewrite((min((x + c0), y) < (min(x, y) + c1)), false, (((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1))) && (c1 <= min(c0, 1)))) ||
 rewrite((min((x + c0), y) < (min(y, x) + c1)), false, ((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1)))) ||
 rewrite((min((x + c0), y) < (min(y, x) + c1)), false, (((c1 <= min(c0, 0)) && ((c1 + -1) <= min(c0, -1))) && (c1 <= min(c0, 1)))))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((min(x, y) < (min(min(z, w), (w + c0))/c1)), (min(x, y) < (min(z, w)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))) ||
 rewrite((min(x, y) < (min(min(z, w), (z + c0))/c1)), (min(x, y) < (min(z, w)/c1)), prove_me((max((min(c0, 0)/c1), ((min(c0, -1) + 1)/c1)) <= 0))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((min(x, y) < min(z, min(w, x))), (y < min(z, min(w, x)))) ||
 rewrite((min(x, y) < min(z, min(w, y))), (x < min(z, min(w, y)))) ||
 rewrite((min(x, y) < min(z, min(x, w))), (y < min(z, min(x, w)))) ||
 rewrite((min(x, y) < min(z, min(y, w))), (x < min(z, min(y, w)))) ||
 rewrite((min(x, y) < min(min(x, z), w)), (y < min(min(x, z), w))) ||
 rewrite((min(x, y) < min(min(y, z), w)), (x < min(min(y, z), w))) ||
 rewrite((min(x, y) < min(min(z, x), w)), (y < min(min(z, x), w))) ||
 rewrite((min(x, y) < min(min(z, y), w)), (x < min(min(z, y), w))) ||
 rewrite((min(x, (y + z)) < min(w, min(u, (z + y)))), (x < min(w, min(u, (z + y))))) ||
 rewrite((min(x, (y + z)) < min(w, min((z + y), u))), (x < min(w, min((z + y), u)))) ||
 rewrite((min(x, (y + z)) < min(min(w, (z + y)), u)), (x < min(min(w, (z + y)), u))) ||
 rewrite((min(x, (y + z)) < min(min((z + y), w), u)), (x < min(min((z + y), w), u))) ||
 rewrite((min(x, min(y, z)) < min(w, y)), (min(x, z) < min(w, y))) ||
 rewrite((min(x, min(y, z)) < min(w, z)), (min(x, y) < min(w, z))) ||
 rewrite((min(x, min(y, z)) < min(y, w)), (min(x, z) < min(y, w))) ||
 rewrite((min(x, min(y, z)) < min(z, w)), (min(x, y) < min(z, w))) ||
 rewrite((min(x, min(y, min(z, w))) < min(u, min(w, z))), (min(x, min(y, z)) < min(u, min(w, z)))) ||
 rewrite((min(x, min(y, min(z, w))) < min(min(w, z), u)), (min(x, min(y, z)) < min(min(w, z), u))) ||
 rewrite((min(x, min(min(y, z), w)) < min(u, min(z, y))), (min(x, min(y, w)) < min(u, min(z, y)))) ||
 rewrite((min(x, min(min(y, z), w)) < min(min(z, y), u)), (min(x, min(y, w)) < min(min(z, y), u))) ||
 rewrite((min((x + y), z) < min(w, min(u, (y + x)))), (z < min(w, min(u, (y + x))))) ||
 rewrite((min((x + y), z) < min(w, min((y + x), u))), (z < min(w, min((y + x), u)))) ||
 rewrite((min((x + y), z) < min(min(w, (y + x)), u)), (z < min(min(w, (y + x)), u))) ||
 rewrite((min((x + y), z) < min(min((y + x), w), u)), (z < min(min((y + x), w), u))) ||
 rewrite((min(min(x, y), z) < min(w, x)), (min(y, z) < min(w, x))) ||
 rewrite((min(min(x, y), z) < min(w, y)), (min(x, z) < min(w, y))) ||
 rewrite((min(min(x, y), z) < min(x, w)), (min(y, z) < min(x, w))) ||
 rewrite((min(min(x, y), z) < min(y, w)), (min(x, z) < min(y, w))) ||
 rewrite((min(min(x, min(y, z)), w) < min(u, min(z, y))), (min(min(x, y), w) < min(u, min(z, y)))) ||
 rewrite((min(min(x, min(y, z)), w) < min(min(z, y), u)), (min(min(x, y), w) < min(min(z, y), u))) ||
 rewrite((min(min(min(x, y), z), w) < min(u, min(y, x))), (min(min(y, z), w) < min(u, min(y, x)))) ||
 rewrite((min(min(min(x, y), z), w) < min(min(y, x), u)), (min(min(y, z), w) < min(min(y, x), u))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
 rewrite((max(x, (y*y)) < c0), false, (c0 <= 0)) ||
 rewrite((max(x, max(y, (z*z))) < c0), false, (c0 <= 0)) ||
 rewrite((max(x, max((y*y), z)) < c0), false, (c0 <= 0)) ||
 rewrite((max((x - y), ((y - x) + c0)) < c1), false, (((c1*2) + -1) <= c0)) ||
 rewrite((max((x*x), y) < c0), false, (c0 <= 0)) ||
 rewrite((max(max(x, (y*y)), z) < c0), false, (c0 <= 0)) ||
 rewrite((max(max((x*x), y), z) < c0), false, (c0 <= 0)) ||
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
 rewrite((max(x, (y/c0)) < (min(z, (y/c0)) + c1)), false, (c1 <= 0)) ||
 rewrite((max(x, (y/c0)) < (min((y/c0), z) + c1)), false, (c1 <= 0)) ||
 rewrite((max((x + (y*c0)), z) < (min(w, z) + c1)), false, (c1 <= 0)) ||
 rewrite((max((x + (y*c0)), z) < (min(z, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max(((x*c0) + y), z) < (min(w, z) + c1)), false, (c1 <= 0)) ||
 rewrite((max(((x*c0) + y), z) < (min(z, w) + c1)), false, (c1 <= 0)) ||
 rewrite((max((x/c0), y) < (min(z, (x/c0)) + c1)), false, (c1 <= 0)) ||
 rewrite((max((x/c0), y) < (min((x/c0), z) + c1)), false, (c1 <= 0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((max(x, max(y, max(z, w))) < min(u, min(v, w))), false) ||
 rewrite((max(x, max(y, max(z, w))) < min(u, min(v, z))), false) ||
 rewrite((max(x, max(y, max(z, w))) < min(u, min(w, v))), false) ||
 rewrite((max(x, max(y, max(z, w))) < min(u, min(z, v))), false) ||
 rewrite((max(x, max(y, max(z, w))) < min(min(u, w), v)), false) ||
 rewrite((max(x, max(y, max(z, w))) < min(min(u, z), v)), false) ||
 rewrite((max(x, max(y, max(z, w))) < min(min(w, u), v)), false) ||
 rewrite((max(x, max(y, max(z, w))) < min(min(z, u), v)), false) ||
 rewrite((max(x, max(max(y, z), w)) < min(u, min(v, y))), false) ||
 rewrite((max(x, max(max(y, z), w)) < min(u, min(v, z))), false) ||
 rewrite((max(x, max(max(y, z), w)) < min(u, min(y, v))), false) ||
 rewrite((max(x, max(max(y, z), w)) < min(u, min(z, v))), false) ||
 rewrite((max(x, max(max(y, z), w)) < min(min(u, y), v)), false) ||
 rewrite((max(x, max(max(y, z), w)) < min(min(u, z), v)), false) ||
 rewrite((max(x, max(max(y, z), w)) < min(min(y, u), v)), false) ||
 rewrite((max(x, max(max(y, z), w)) < min(min(z, u), v)), false) ||
 rewrite((max(max(x, max(y, z)), w) < min(u, min(v, y))), false) ||
 rewrite((max(max(x, max(y, z)), w) < min(u, min(v, z))), false) ||
 rewrite((max(max(x, max(y, z)), w) < min(u, min(y, v))), false) ||
 rewrite((max(max(x, max(y, z)), w) < min(u, min(z, v))), false) ||
 rewrite((max(max(x, max(y, z)), w) < min(min(u, y), v)), false) ||
 rewrite((max(max(x, max(y, z)), w) < min(min(u, z), v)), false) ||
 rewrite((max(max(x, max(y, z)), w) < min(min(y, u), v)), false) ||
 rewrite((max(max(x, max(y, z)), w) < min(min(z, u), v)), false) ||
 rewrite((max(max(max(x, y), z), w) < min(u, min(v, x))), false) ||
 rewrite((max(max(max(x, y), z), w) < min(u, min(v, y))), false) ||
 rewrite((max(max(max(x, y), z), w) < min(u, min(x, v))), false) ||
 rewrite((max(max(max(x, y), z), w) < min(u, min(y, v))), false) ||
 rewrite((max(max(max(x, y), z), w) < min(min(u, x), v)), false) ||
 rewrite((max(max(max(x, y), z), w) < min(min(u, y), v)), false) ||
 rewrite((max(max(max(x, y), z), w) < min(min(x, u), v)), false) ||
 rewrite((max(max(max(x, y), z), w) < min(min(y, u), v)), false))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((max(((x + c0)/c1), c2) < max((x/c1), c3)), false, prove_me((max(c3, 0) <= max(min((c0/c1), ((c0 + 1)/c1)), c2)))))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Min) && (
 rewrite((select(x, c0, y) < min(select(x, c1, y), c2)), false, (min(c1, c2) <= c0)) ||
 rewrite((select(x, max(y, z), min(w, y)) < min(w, y)), false) ||
 rewrite((select(x, max(y, z), min(w, y)) < min(y, w)), false) ||
 rewrite((select(x, max(y, z), min(w, z)) < min(w, z)), false) ||
 rewrite((select(x, max(y, z), min(w, z)) < min(z, w)), false) ||
 rewrite((select(x, max(y, z), min(y, w)) < min(w, y)), false) ||
 rewrite((select(x, max(y, z), min(y, w)) < min(y, w)), false) ||
 rewrite((select(x, max(y, z), min(z, w)) < min(w, z)), false) ||
 rewrite((select(x, max(y, z), min(z, w)) < min(z, w)), false))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((select(x, c0, y) < select(x, c1, y)), false, (c1 <= c0))))))
