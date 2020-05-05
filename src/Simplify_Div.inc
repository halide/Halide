((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + (x + y))/c0), (x + (y/c0)), ((2/c0) == 1)) ||
 rewrite(((x + (y + x))/c0), (x + (y/c0)), ((2/c0) == 1)) ||
 rewrite(((x + (y + (y + z)))/c0), (y + ((x + z)/c0)), ((2/c0) == 1)) ||
 rewrite(((x + (y + (z + y)))/c0), (y + ((z + x)/c0)), ((2/c0) == 1)) ||
 rewrite(((x + ((y + z) + y))/c0), (y + ((x + z)/c0)), ((2/c0) == 1)) ||
 rewrite(((x + ((y + z) + z))/c0), (z + ((y + x)/c0)), ((2/c0) == 1)) ||
 rewrite((((x + y) + x)/c0), (x + (y/c0)), ((2/c0) == 1)) ||
 rewrite((((x + y) + y)/c0), ((x/c0) + y), ((2/c0) == 1)) ||
 rewrite((((x + (x + y)) + c0)/c1), (x + ((y + c0)/c1)), ((((((c0 + 2)/c1) == ((c0/c1) + 1)) && (((c0 + -4)/c1) == (((c0 + -2)/c1) + -1))) && (((c0 + 1)/c1) == (((c0 + 3)/c1) + -1))) && ((c0/c1) == (((c0 + 6)/c1) + -3)))) ||
 rewrite((((x + (x + y)) + z)/c0), (x + ((y + z)/c0)), ((2/c0) == 1)) ||
 rewrite((((x + (y + x)) + c0)/c1), (x + ((y + c0)/c1)), ((((((c0 + 2)/c1) == ((c0/c1) + 1)) && (((c0 + -4)/c1) == (((c0 + -2)/c1) + -1))) && (((c0 + 1)/c1) == (((c0 + 3)/c1) + -1))) && ((c0/c1) == (((c0 + 6)/c1) + -3)))) ||
 rewrite((((x + (y + x)) + z)/c0), (x + ((y + z)/c0)), ((2/c0) == 1)) ||
 rewrite(((((x + y) + x) + c0)/c1), (x + ((y + c0)/c1)), ((((((c0 + 2)/c1) == ((c0/c1) + 1)) && (((c0 + -4)/c1) == (((c0 + -2)/c1) + -1))) && (((c0 + 1)/c1) == (((c0 + 3)/c1) + -1))) && ((c0/c1) == (((c0 + 6)/c1) + -3)))) ||
 rewrite(((((x + y) + x) + z)/c0), (x + ((y + z)/c0)), ((2/c0) == 1)) ||
 rewrite(((((x + y) + y) + c0)/c1), (y + ((x + c0)/c1)), ((((((c0 + 2)/c1) == ((c0/c1) + 1)) && (((c0 + -4)/c1) == (((c0 + -2)/c1) + -1))) && (((c0 + 1)/c1) == (((c0 + 3)/c1) + -1))) && ((c0/c1) == (((c0 + 6)/c1) + -3)))) ||
 rewrite(((((x + y) + y) + z)/c0), (((x + z)/c0) + y), ((2/c0) == 1)) ||
 rewrite(((((x + (y + (z*c0))) % c1) + c2)/c1), ((((x + y) % c1) + c2)/c1), prove_me(((((c0 % c1) + c2)/c1) == (c2/c1)))) ||
 rewrite(((((x + ((y*c0) + z)) % c1) + c2)/c1), ((((x + z) % c1) + c2)/c1), prove_me(((((c0 % c1) + c2)/c1) == (c2/c1)))) ||
 rewrite((((((x + (y*c0)) + z) % c1) + c2)/c1), ((((x + z) % c1) + c2)/c1), prove_me(((((c0 % c1) + c2)/c1) == (c2/c1)))) ||
 rewrite(((((((x*c0) + y) + z) % c1) + c2)/c1), ((((y + z) % c1) + c2)/c1), prove_me(((((c0 % c1) + c2)/c1) == (c2/c1)))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(((c0 - ((x + (y + (z*c1))) % c2))/c2), ((c0 - ((x + y) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == (c0/c2)))) ||
 rewrite(((c0 - ((x + ((y*c1) + z)) % c2))/c2), ((c0 - ((x + z) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == (c0/c2)))) ||
 rewrite(((c0 - (((x + (y*c1)) + z) % c2))/c2), ((c0 - ((x + z) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == (c0/c2)))) ||
 rewrite(((c0 - ((((x*c1) + y) + z) % c2))/c2), ((c0 - ((y + z) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == (c0/c2)))) ||
 rewrite(((x - ((y + (z + (w*c0))) % c1))/c1), ((x - ((y + z) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == 0))) ||
 rewrite(((x - ((y + ((z*c0) + w)) % c1))/c1), ((x - ((y + w) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == 0))) ||
 rewrite(((x - (((y + (z*c0)) + w) % c1))/c1), ((x - ((y + w) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == 0))) ||
 rewrite(((x - ((((y*c0) + z) + w) % c1))/c1), ((x - ((z + w) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == 0))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite((min(min(x, y), (x + c0))/c1), (min(y, x)/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(x, y), (y + c0))/c1), (min(x, y)/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(x, (y + (z - w))), (x + c0))/c1), (min(x, (y + (z - w)))/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(x, ((y - z) + w)), (x + c0))/c1), (min(x, ((y - z) + w))/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(x, (y - z)), (x + c0))/c1), (min(x, (y - z))/c1), ((((min(c0, 0)/c1) == 0) && ((min(c0, -1)/c1) == (-1/c1))) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(x, (y - z)), (min(w, x) + c0))/c1), (min(min(x, (y - z)), (w + c0))/c1), prove_me(((min(c0, 0)/c1) == ((min(c0, -1) + 1)/c1)))) ||
 rewrite((min(min(x, (y - z)), (min(x, w) + c0))/c1), (min(min(x, (y - z)), (w + c0))/c1), prove_me(((min(c0, 0)/c1) == ((min(c0, -1) + 1)/c1)))) ||
 rewrite((min(min(x, ((y + z) - w)), (x + c0))/c1), (min(x, ((y + z) - w))/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(x, min(y, z)), (y + c0))/c1), (min(x, min(z, y))/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(x, min(y, z)), (z + c0))/c1), (min(x, min(y, z))/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min((x + (y - z)), w), (w + c0))/c1), (min((x + (y - z)), w)/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(((x - y) + z), w), (w + c0))/c1), (min(((x - y) + z), w)/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min((x - y), z), (z + c0))/c1), (min((x - y), z)/c1), ((((min(c0, 0)/c1) == 0) && ((min(c0, -1)/c1) == (-1/c1))) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min((x - y), z), (min(w, z) + c0))/c1), (min(min((x - y), z), (w + c0))/c1), prove_me(((min(c0, 0)/c1) == ((min(c0, -1) + 1)/c1)))) ||
 rewrite((min(min((x - y), z), (min(z, w) + c0))/c1), (min(min((x - y), z), (w + c0))/c1), prove_me(((min(c0, 0)/c1) == ((min(c0, -1) + 1)/c1)))) ||
 rewrite((min(min(((x + y) - z), w), (w + c0))/c1), (min(((x + y) - z), w)/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(min(x, y), z), (x + c0))/c1), (min(min(y, z), x)/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(min(x, y), z), (y + c0))/c1), (min(min(x, z), y)/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)))))
