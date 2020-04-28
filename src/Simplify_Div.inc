((a.node_type() == IRNodeType::Add) && (
 rewrite(((x + (y + (z - (w*c0))))/c0), (((x + (y + z))/c0) - w), prove_me((((0 - c0)/c0) == -1))) ||
 rewrite(((x + (y + (z - ((w + u)*c0))))/c0), (((x + (y + z))/c0) - (w + u))) ||
 rewrite(((x + (y + (z - ((w/c0)*c0))))/c0), (((x + (y + z))/c0) - (w/c0)), prove_me(((((0 - ((1/c0)*c0))/c0) + (1/c0)) == 0))) ||
 rewrite(((x + (y + (z - ((w/c0)*c1))))/c1), (((x + (y + z))/c1) - (w/c0)), prove_me(((((0 - ((1/c0)*c1))/c1) + (1/c0)) == 0))) ||
 rewrite(((x + (y + (z - (min(w, u)*c0))))/c0), (((x + (y + z))/c0) - min(w, u))) ||
 rewrite(((x + ((y - (z*c0)) + w))/c0), (((x + (y + w))/c0) - z), prove_me((((0 - c0)/c0) == -1))) ||
 rewrite(((x + ((y - ((z + w)*c0)) + u))/c0), ((((y + (x + u))/c0) - z) - w)) ||
 rewrite(((x + ((y - ((z/c0)*c0)) + w))/c0), (((x + (y + w))/c0) - (z/c0)), prove_me(((((0 - ((1/c0)*c0))/c0) + (1/c0)) == 0))) ||
 rewrite(((x + ((y - ((z/c0)*c1)) + w))/c1), (((x + (y + w))/c1) - (z/c0)), prove_me(((((0 - ((1/c0)*c1))/c1) + (1/c0)) == 0))) ||
 rewrite(((x + ((y - (min(z, w)*c0)) + u))/c0), (((x + (y + u))/c0) - min(z, w))) ||
 rewrite(((x + ((y*c0) + (z - (w*c1))))/c1), (((x + ((y*c0) + z))/c1) - w), prove_me((((c0 - c1)/c1) == ((c0/c1) + -1)))) ||
 rewrite(((x + (y - (z + (w*c0))))/c0), (((x + (y - z))/c0) - w), prove_me((((0 - c0)/c0) == -1))) ||
 rewrite(((x + (y - (z + (min(w, c0)*c1))))/c1), (((x + (y - z))/c1) - min(w, c0)), prove_me(((min(c0, 0) + ((0 - (min(c0, 0)*c1))/c1)) == 0))) ||
 rewrite(((x + (y - (z + (min(w, u)*c0))))/c0), (((x + (y - z))/c0) - min(w, u))) ||
 rewrite(((x + (y - ((z*c0) + w)))/c0), (((x + (y - w))/c0) - z), prove_me((((0 - c0)/c0) == -1))) ||
 rewrite(((x + (y - ((min(z, c0)*c1) + w)))/c1), (((x + (y - w))/c1) - min(z, c0)), prove_me(((min(c0, 0) + ((0 - (min(c0, 0)*c1))/c1)) == 0))) ||
 rewrite(((x + (y - ((min(z, w)*c0) + u)))/c0), (((x + (y - u))/c0) - min(w, z))) ||
 rewrite((((x + (y - (z*c0))) + c1)/c0), (((x + (y + c1))/c0) - z), prove_me((((c1 - c0)/c0) == ((c1/c0) + -1)))) ||
 rewrite((((x + (y - (z*c0))) + w)/c0), (((x + (y + w))/c0) - z), prove_me((((0 - c0)/c0) == -1))) ||
 rewrite((((x + (y - ((z + w)*c0))) + u)/c0), (((x + (y + u))/c0) - (z + w))) ||
 rewrite((((x + (y - ((z/c0)*c0))) + c1)/c0), (((x + (y + c1))/c0) - (z/c0)), prove_me(((((c1 - ((1/c0)*c0))/c0) + ((1/c0) - (c1/c0))) == 0))) ||
 rewrite((((x + (y - ((z/c0)*c0))) + w)/c0), (((x + (y + w))/c0) - (z/c0)), prove_me(((((0 - ((1/c0)*c0))/c0) + (1/c0)) == 0))) ||
 rewrite((((x + (y - ((z/c0)*c1))) + c2)/c1), (((x + (y + c2))/c1) - (z/c0)), prove_me(((((c2 - ((1/c0)*c1))/c1) + ((1/c0) - (c2/c1))) == 0))) ||
 rewrite((((x + (y - ((z/c0)*c1))) + w)/c1), (((x + (y + w))/c1) - (z/c0)), prove_me(((((0 - ((1/c0)*c1))/c1) + (1/c0)) == 0))) ||
 rewrite((((x + (y - (min(z, w)*c0))) + u)/c0), (((x + (y + u))/c0) - min(z, w))) ||
 rewrite(((((x - (y*c0)) + z) + c1)/c0), ((((x + c1) + z)/c0) - y), prove_me((((c1 - c0)/c0) == ((c1/c0) + -1)))) ||
 rewrite(((((x - (y*c0)) + z) + w)/c0), ((((x + w) + z)/c0) - y), prove_me((((0 - c0)/c0) == -1))) ||
 rewrite(((((x - ((y + z)*c0)) + w) + u)/c0), ((((x + u) + w)/c0) - (y + z))) ||
 rewrite(((((x - ((y/c0)*c0)) + z) + c1)/c0), ((((x + z) + c1)/c0) - (y/c0)), prove_me(((((c1 - ((1/c0)*c0))/c0) + ((1/c0) - (c1/c0))) == 0))) ||
 rewrite(((((x - ((y/c0)*c0)) + z) + w)/c0), ((((x + w) + z)/c0) - (y/c0)), prove_me(((((0 - ((1/c0)*c0))/c0) + (1/c0)) == 0))) ||
 rewrite(((((x - ((y/c0)*c1)) + z) + c2)/c1), ((((x + z) + c2)/c1) - (y/c0)), prove_me(((((c2 - ((1/c0)*c1))/c1) + ((1/c0) - (c2/c1))) == 0))) ||
 rewrite(((((x - ((y/c0)*c1)) + z) + w)/c1), ((((x + w) + z)/c1) - (y/c0)), prove_me(((((0 - ((1/c0)*c1))/c1) + (1/c0)) == 0))) ||
 rewrite(((((x - (min(y, z)*c0)) + w) + u)/c0), ((((x + u) + w)/c0) - min(y, z))) ||
 rewrite(((((x*c0) + (y - (z*c1))) + c0)/c1), ((((x*c0) + (y + c0))/c1) - z), prove_me(((((c0 - c1) + c0)/c1) == (((c0*2)/c1) + -1)))) ||
 rewrite(((((x*c0) + (y - (z*c1))) + c2)/c1), ((((x*c0) + (y + c2))/c1) - z), prove_me(((((c0 - c1) + c2)/c1) == (((c0 + c2)/c1) + -1)))) ||
 rewrite(((((x*c0) + (y - (z*c1))) + w)/c1), ((((x*c0) + (y + w))/c1) - z), prove_me((((c0 - c1)/c1) == ((c0/c1) + -1)))) ||
 rewrite((((x - (y + (z*c0))) + c1)/c0), (((x - (y - c1))/c0) - z), prove_me((((c1 - c0)/c0) == ((c1/c0) + -1)))) ||
 rewrite((((x - (y + (z*c0))) + w)/c0), (((x - (y - w))/c0) - z), prove_me((((0 - c0)/c0) == -1))) ||
 rewrite((((x - (y + (min(z, c0)*c1))) + c2)/c1), (((x - (y - c2))/c1) - min(z, c0)), prove_me(((((c2 - (min(c0, 0)*c1))/c1) + (min(c0, 0) - (c2/c1))) == 0))) ||
 rewrite((((x - (y + (min(z, c0)*c1))) + w)/c1), (((x - (y - w))/c1) - min(z, c0)), prove_me(((min(c0, 0) + ((0 - (min(c0, 0)*c1))/c1)) == 0))) ||
 rewrite((((x - (y + (min(z, w)*c0))) + u)/c0), (((x - (y - u))/c0) - min(w, z))) ||
 rewrite((((x - ((y*c0) + z)) + c1)/c0), (((x + (c1 - z))/c0) - y), prove_me((((c1 - c0)/c0) == ((c1/c0) + -1)))) ||
 rewrite((((x - ((y*c0) + z)) + w)/c0), (((x + (w - z))/c0) - y), prove_me((((0 - c0)/c0) == -1))) ||
 rewrite((((x - ((min(y, c0)*c1) + z)) + c2)/c1), (((x - (z - c2))/c1) - min(y, c0)), prove_me(((((c2 - (min(c0, 0)*c1))/c1) + (min(c0, 0) - (c2/c1))) == 0))) ||
 rewrite((((x - ((min(y, c0)*c1) + z)) + w)/c1), (((x - (z - w))/c1) - min(y, c0)), prove_me(((min(c0, 0) + ((0 - (min(c0, 0)*c1))/c1)) == 0))) ||
 rewrite((((x - ((min(y, z)*c0) + w)) + u)/c0), (((x - (w - u))/c0) - min(y, z))))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite(((c0 - (x % c1))/c1), (c0 - (x % c1)), ((((((c0/c1) == c0) && ((((c0 - (-5 % c1))/c1) + ((-5 % c1) - c0)) == 0)) && ((((c0 - (-15 % c1))/c1) + ((-15 % c1) - c0)) == 0)) && ((((c0 - (-14 % c1))/c1) + ((-14 % c1) - c0)) == 0)) && ((((c0 - (-22 % c1))/c1) + ((-22 % c1) - c0)) == 0))) ||
 rewrite(((c0 - (x % c1))/c1), ((x % c1)*0), (((c1 <= -1) || (c0 < c1)) && (((1 <= c1) || ((c0 + c1) == -1)) && ((c1 + -1) <= c0)))) ||
 rewrite(((c0 - (x % c1))/c1), ((x % c1)*0), (((c1 <= -1) || ((c0 + 1) <= c1)) && (((1 <= c1) || ((c0 + c1) == -1)) && ((c1 + -1) <= c0)))) ||
 rewrite(((c0 - ((x + (y + (z*c1))) % c2))/c2), ((c0 - ((x + y) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == (c0/c2)))) ||
 rewrite(((c0 - ((x + (y + (z*c1))) % c2))/c2), ((c0 - ((x + (y + z)) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == ((c0 - (1 % c2))/c2)))) ||
 rewrite(((c0 - ((x + ((y*c1) + z)) % c2))/c2), ((c0 - ((x + z) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == (c0/c2)))) ||
 rewrite(((c0 - ((x + ((y*c1) + z)) % c2))/c2), ((c0 - ((x + (y + z)) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == ((c0 - (1 % c2))/c2)))) ||
 rewrite(((c0 - (((x + (y*c1)) + z) % c2))/c2), ((c0 - ((x + z) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == (c0/c2)))) ||
 rewrite(((c0 - (((x + (y*c1)) + z) % c2))/c2), ((c0 - (((x + y) + z) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == ((c0 - (1 % c2))/c2)))) ||
 rewrite(((c0 - ((((x*c1) + y) + z) % c2))/c2), ((c0 - ((y + z) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == (c0/c2)))) ||
 rewrite(((c0 - ((((x*c1) + y) + z) % c2))/c2), ((c0 - (((x + y) + z) % c2))/c2), prove_me((((c0 - (c1 % c2))/c2) == ((c0 - (1 % c2))/c2)))) ||
 rewrite(((x - ((y + (z + (w*c0))) % c1))/c1), ((x - ((y + z) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == 0))) ||
 rewrite(((x - ((y + (z + (w*c0))) % c1))/c1), ((x - ((y + (z + w)) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == ((0 - (1 % c1))/c1)))) ||
 rewrite(((x - ((y + ((z*c0) + w)) % c1))/c1), ((x - ((y + w) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == 0))) ||
 rewrite(((x - ((y + ((z*c0) + w)) % c1))/c1), ((x - ((y + (z + w)) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == ((0 - (1 % c1))/c1)))) ||
 rewrite(((x - (((y + (z*c0)) + w) % c1))/c1), ((x - ((y + w) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == 0))) ||
 rewrite(((x - (((y + (z*c0)) + w) % c1))/c1), ((x - (((y + z) + w) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == ((0 - (1 % c1))/c1)))) ||
 rewrite(((x - ((((y*c0) + z) + w) % c1))/c1), ((x - ((z + w) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == 0))) ||
 rewrite(((x - ((((y*c0) + z) + w) % c1))/c1), ((x - (((y + z) + w) % c1))/c1), prove_me((((0 - (c0 % c1))/c1) == ((0 - (1 % c1))/c1))))))
