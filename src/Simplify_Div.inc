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
 rewrite((min(min(x, y), (x + c0))/c1), (min(x, y)/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(x, y), (y + c0))/c1), (min(x, y)/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(x, (y + (z - w))), (x + c0))/c1), (min(x, (y + (z - w)))/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(x, ((y - z) + w)), (x + c0))/c1), (min(x, ((y - z) + w))/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(x, min(y, z)), (y + c0))/c1), (min(x, min(z, y))/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(x, min(y, z)), (z + c0))/c1), (min(x, min(y, z))/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min((x + (y - z)), w), (w + c0))/c1), (min((x + (y - z)), w)/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(((x - y) + z), w), (w + c0))/c1), (min(((x - y) + z), w)/c1), ((((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0)) && ((min(c0, -1)/c1) == (-1/c1)))) ||
 rewrite((min(min(min(x, y), z), (x + c0))/c1), (min(min(y, z), x)/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(min(x, y), z), (y + c0))/c1), (min(min(x, z), y)/c1), (((min(c0, 0)/c1) == 0) && (((min(c0, -1) + 1)/c1) == 0))) ||
 rewrite((min(min(min(x, y), (z + c0)), (x + c1))/c2), (min(min(x, y), (z + c0))/c2), prove_me((((((((((min(min((c1 + 1), c0), 0)/c2) == (min(c0, 0)/c2)) && ((min(min(c0, c1), 0)/c2) == (min(c0, 0)/c2))) && ((min((min(c0, -1) + 1), c1)/c2) == ((min(c0, -1) + 1)/c2))) && (((min((min(c0, -3) + 3), c1) + 1)/c2) == ((min(c0, -3) + 4)/c2))) && (((min((min(c0, -5) + 5), c1) + 3)/c2) == ((min(c0, -5) + 8)/c2))) && (((min((min(c0, -7) + 7), c1) + 3)/c2) == ((min(c0, -7) + 10)/c2))) && (((min((min(c0, -9) + 9), c1) + 1)/c2) == ((min(c0, -9) + 10)/c2))) && (((min((min(c0, -11) + 11), c1) + 4)/c2) == ((min(c0, -11) + 15)/c2))))) ||
 rewrite((min(min(min(x, y), (z + c0)), (y + c1))/c2), (min(min(x, y), (z + c0))/c2), prove_me((((((((((min(min((c1 + 1), c0), 0)/c2) == (min(c0, 0)/c2)) && ((min(min(c0, c1), 0)/c2) == (min(c0, 0)/c2))) && ((min((min(c0, -1) + 1), c1)/c2) == ((min(c0, -1) + 1)/c2))) && (((min((min(c0, -3) + 3), c1) + 1)/c2) == ((min(c0, -3) + 4)/c2))) && (((min((min(c0, -5) + 5), c1) + 3)/c2) == ((min(c0, -5) + 8)/c2))) && (((min((min(c0, -7) + 7), c1) + 3)/c2) == ((min(c0, -7) + 10)/c2))) && (((min((min(c0, -9) + 9), c1) + 1)/c2) == ((min(c0, -9) + 10)/c2))) && (((min((min(c0, -11) + 11), c1) + 4)/c2) == ((min(c0, -11) + 15)/c2)))))))
