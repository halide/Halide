((b.node_type() == IRNodeType::Min) && (
 rewrite((x*min(min(y, (z/c0)), (w/c0))), (x*min((min(w, z)/c0), y)), prove_me((((((min((9/c0), -2) <= (24/c0)) && (min((5/c0), 0) <= (8/c0))) && (min((-27/c0), 2) <= (-9/c0))) && ((-3/c0) <= (-1/c0))) && ((-28/c0) <= 0)))) ||
 rewrite((x*min(min(y, ((z + w)/c0)), (u/c0))), (x*min((min(u, (z + w))/c0), y)), prove_me(((0 <= (2283/c0)) && ((-1/c0) <= (1591/c0))))) ||
 rewrite((x*min(min((y/c0), z), (w/c0))), (x*min(z, (min(w, y)/c0))), prove_me((((((min((9/c0), -2) <= (24/c0)) && (min((5/c0), 0) <= (8/c0))) && (min((-27/c0), 2) <= (-9/c0))) && ((-3/c0) <= (-1/c0))) && ((-28/c0) <= 0)))) ||
 rewrite((x*min(min(((y + z)/c0), w), (u/c0))), (x*min(w, (min((y + z), u)/c0))), prove_me(((0 <= (2283/c0)) && ((-1/c0) <= (1591/c0))))))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + select(y, c0, c1))*c2), (select(y, c1, c0) - x), ((((c1*c2) == c0) && ((c0*c2) == c1)) && (((c1 + 1)*c2) == (c0 + -1)))) ||
 rewrite(((select(x, c0, c1) + y)*c2), (select(x, c1, c0) - y), ((((c1*c2) == c0) && ((c0*c2) == c1)) && (((c1 + 1)*c2) == (c0 + -1)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite((min(x, min(y, min(z, min(w, (z + c0)))))*c1), (min(x, min(y, min(w, z)))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(y, min(z, min((z + c0), w))))*c1), (min(x, min(y, min(z, w)))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(y, min(min(z, (w + c0)), w)))*c1), (min(x, min(y, min(z, w)))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(y, min(min((z + c0), w), z)))*c1), (min(x, min(y, min(w, z)))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(min(y, (z/c0)), (w/c0)))*c0), (min(x, min((min(w, z)/c0), y))*c0), prove_me((((0 <= min((1/c0), (2/c0))) && ((-1/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(x, min(min(y, min(z, (y + c0))), w))*c1), (min(x, min(min(z, y), w))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(min(y, min((y + c0), z)), w))*c1), (min(x, min(min(y, z), w))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(min((y/c0), z), (w/c0)))*c0), (min(x, min((min(w, y)/c0), z))*c0), prove_me((((0 <= min((1/c0), (2/c0))) && ((-1/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(x, min(min(min(y, (z + c0)), z), w))*c1), (min(x, min(min(y, z), w))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(min(min((y + c0), z), y), w))*c1), (min(x, min(min(z, y), w))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(x, (y/c0)), (z/c0))*c0), (min((min(z, y)/c0), x)*c0), prove_me(((((((0 <= (1/c0)) && (min((6/c0), -4) <= (11/c0))) && (min((1/c0), 0) <= (7/c0))) && (min((-2964/c0), 51) <= (-2838/c0))) && ((-16/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(x, (y/c0)), (z/c0))*c1), (min((min(z, y)/c0), x)*c1), (c1 == 0)) ||
 rewrite((min(min(x, (y/c0)), (z/c0))*w), (min((min(z, y)/c0), x)*w), prove_me((((((min((9/c0), -2) <= (24/c0)) && (min((5/c0), 0) <= (8/c0))) && (min((-27/c0), 2) <= (-9/c0))) && ((-3/c0) <= (-1/c0))) && ((-28/c0) <= 0)))) ||
 rewrite((min(min(x, (y/c0)), ((z + c1)/c0))*c0), (min(x, (min(y, (z + c1))/c0))*c0), prove_me((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) || (c0 == 0)))) ||
 rewrite((min(min(x, (y/c0)), ((z + w)/c0))*c0), (min(x, (min(y, (z + w))/c0))*c0), prove_me(((((((0 <= (1/c0)) && (min((2/c0), 0) <= (5/c0))) && (min((-22/c0), 3) <= (-1/c0))) && (min((-70/c0), 3) <= (-23/c0))) && (min((-70/c0), 2) <= (1/c0))) || (c0 == 0)))) ||
 rewrite((min(min(x, ((y + z)/c0)), (w/c0))*c0), (min((min(w, (y + z))/c0), x)*c0), prove_me(((((((0 <= (1/c0)) && (min((5918/c0), -2958) <= (5920/c0))) && (min((-4/c0), 3) <= (-3/c0))) && (-1 <= (8/c0))) && ((-11836/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(x, ((y + z)/c0)), (w/c0))*c1), (min((min(w, (y + z))/c0), x)*c1), prove_me(((((0 <= (1/c0)) && (min((5918/c0), -2958) <= (5920/c0))) && ((-1/c0) <= (2/c0))) || (c1 == 0)))) ||
 rewrite((min(min(x, ((y + z)/c0)), (w/c0))*u), (min((min(w, (y + z))/c0), x)*u), prove_me(((0 <= (2283/c0)) && ((-1/c0) <= (1591/c0))))) ||
 rewrite((min(min(x, min(y, (z/c0))), (w/c0))*c0), (min(min(x, (min(z, w)/c0)), y)*c0), prove_me((((0 <= (1/c0)) && ((-6/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(x, min(y, (z/c0))), (w/c0))*c0), (min(min(x, (min(z, w)/c0)), y)*c0), prove_me(((((((min((-2/c0), -1) <= (12/c0)) && ((-21/c0) <= 0)) && (0 <= (1/c0))) && (-1 <= (4/c0))) && (-2 <= min((11/c0), (15/c0)))) || (c0 == 0)))) ||
 rewrite((min(min(x, min(y, min(z, (y + c0)))), w)*c1), (min(min(x, min(z, y)), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(x, min(y, min((y + c0), z))), w)*c1), (min(min(x, min(y, z)), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(x, min((y/c0), z)), (w/c0))*c0), (min(min(x, (min(y, w)/c0)), z)*c0), prove_me((((0 <= (1/c0)) && ((-6/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(x, min((y/c0), z)), (w/c0))*c0), (min(min(x, (min(y, w)/c0)), z)*c0), prove_me(((((((min((-2/c0), -1) <= (12/c0)) && ((-21/c0) <= 0)) && (0 <= (1/c0))) && (-1 <= (4/c0))) && (-2 <= min((11/c0), (15/c0)))) || (c0 == 0)))) ||
 rewrite((min(min(x, min(min(y, (z + c0)), z)), w)*c1), (min(min(x, min(y, z)), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(x, min(min((y + c0), z), y)), w)*c1), (min(min(x, min(z, y)), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min((x/c0), y), (z/c0))*c0), (min(y, (min(z, x)/c0))*c0), prove_me(((((((0 <= (1/c0)) && (min((6/c0), -4) <= (11/c0))) && (min((1/c0), 0) <= (7/c0))) && (min((-2964/c0), 51) <= (-2838/c0))) && ((-16/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min((x/c0), y), (z/c0))*c1), (min(y, (min(z, x)/c0))*c1), (c1 == 0)) ||
 rewrite((min(min((x/c0), y), (z/c0))*w), (min(y, (min(z, x)/c0))*w), prove_me((((((min((9/c0), -2) <= (24/c0)) && (min((5/c0), 0) <= (8/c0))) && (min((-27/c0), 2) <= (-9/c0))) && ((-3/c0) <= (-1/c0))) && ((-28/c0) <= 0)))) ||
 rewrite((min(min((x/c0), y), ((z + c1)/c0))*c0), (min(y, (min(x, (z + c1))/c0))*c0), prove_me((((((min((c1/c0), 0) == min((min(c1, 0)/c0), 0)) && (min((c1/c0), 0) == min((min(c1, 0)/c0), 1))) && (min(min((1/c0), (c1/c0)), 0) == min((min(c1, 1)/c0), 0))) && (min(((c1 + 1)/c0), 0) == min(((min(c1, -1) + 1)/c0), 0))) || (c0 == 0)))) ||
 rewrite((min(min((x/c0), y), ((z + w)/c0))*c0), (min(y, (min(x, (z + w))/c0))*c0), prove_me(((((((0 <= (1/c0)) && (min((2/c0), 0) <= (5/c0))) && (min((-22/c0), 3) <= (-1/c0))) && (min((-70/c0), 3) <= (-23/c0))) && (min((-70/c0), 2) <= (1/c0))) || (c0 == 0)))) ||
 rewrite((min(min(((x + y)/c0), z), (w/c0))*c0), (min(z, (min((x + y), w)/c0))*c0), prove_me(((((((0 <= (1/c0)) && (min((5918/c0), -2958) <= (5920/c0))) && (min((-4/c0), 3) <= (-3/c0))) && (-1 <= (8/c0))) && ((-11836/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(((x + y)/c0), z), (w/c0))*c1), (min(z, (min((x + y), w)/c0))*c1), prove_me(((((0 <= (1/c0)) && (min((5918/c0), -2958) <= (5920/c0))) && ((-1/c0) <= (2/c0))) || (c1 == 0)))) ||
 rewrite((min(min(((x + y)/c0), z), (w/c0))*u), (min(z, (min((x + y), w)/c0))*u), prove_me(((0 <= (2283/c0)) && ((-1/c0) <= (1591/c0))))) ||
 rewrite((min(min(min(x, y), (z/c0)), (w/c0))*c0), (min(min(x, y), (min(z, w)/c0))*c0), prove_me(((((((0 <= (1/c0)) && (min((-284/c0), 141) <= (-280/c0))) && (min((-493551/c0), 1770) <= (-493272/c0))) && (min((-11928/c0), 85) <= (-11786/c0))) && ((-60/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(min(x, (y/c0)), z), (w/c0))*c0), (min(min((min(y, w)/c0), z), x)*c0), prove_me((((0 <= (1/c0)) && ((-6/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(min(x, (y/c0)), z), (w/c0))*c0), (min(min((min(y, w)/c0), z), x)*c0), prove_me(((((((min((-2/c0), -1) <= (12/c0)) && ((-21/c0) <= 0)) && (0 <= (1/c0))) && (-1 <= (4/c0))) && (-2 <= min((11/c0), (15/c0)))) || (c0 == 0)))) ||
 rewrite((min(min(min(x, (y/c0)), (z/c0)), w)*c0), (min(min((min(z, y)/c0), x), w)*c0), prove_me((((0 <= min((1/c0), (2/c0))) && ((-1/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(min(x, min(y, (x + c0))), z), w)*c1), (min(min(min(y, x), z), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(min(x, min((x + c0), y)), z), w)*c1), (min(min(min(x, y), z), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(min((x/c0), y), z), (w/c0))*c0), (min(min((min(x, w)/c0), z), y)*c0), prove_me((((0 <= (1/c0)) && ((-6/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(min((x/c0), y), z), (w/c0))*c0), (min(min((min(x, w)/c0), z), y)*c0), prove_me(((((((min((-2/c0), -1) <= (12/c0)) && ((-21/c0) <= 0)) && (0 <= (1/c0))) && (-1 <= (4/c0))) && (-2 <= min((11/c0), (15/c0)))) || (c0 == 0)))) ||
 rewrite((min(min(min((x/c0), y), (z/c0)), w)*c0), (min(min((min(z, x)/c0), y), w)*c0), prove_me((((0 <= min((1/c0), (2/c0))) && ((-1/c0) <= 0)) || (c0 == 0)))) ||
 rewrite((min(min(min(min(x, (y + c0)), y), z), w)*c1), (min(min(min(x, y), z), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(min(min((x + c0), y), x), z), w)*c1), (min(min(min(y, x), z), w)*c1), ((0 <= c0) || (c1 == 0)))))
