((a.node_type() == IRNodeType::Add) && (
 rewrite(((x + (y + (z*c0))) % c1), ((x + y) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((x + (y + (z*c0))) % c1), ((x + (y + z)) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((x + ((y*c0) + z)) % c1), ((x + z) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((x + ((y*c0) + z)) % c1), ((x + (y + z)) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((x + ((y*c0) + (z*c1))) % c2), ((x + (y*c0)) % c2), prove_me((((c0 + c1) % c2) == (c0 % c2)))) ||
 rewrite(((x + ((y*c0) + (z*w))) % c1), ((x + (y + (z*w))) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((x + ((y*z) + (w*c0))) % c1), ((x + (y*z)) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((x + (y*c0)) % c1), ((x + y) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite((((x + y) + (z + (w*c0))) % c1), (((x + y) + z) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite((((x + y) + ((z*c0) + w)) % c1), (((x + y) + w) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite((((x + y) + ((z*c0) + (w*c1))) % c2), ((y + ((z*c0) + x)) % c2), prove_me((((c0 + c1) % c2) == (c0 % c2)))) ||
 rewrite((((x + y) + ((z*c0) + (w*u))) % c1), (((x + y) + (z + (w*u))) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite((((x + y) + ((z*w) + (u*c0))) % c1), ((y + ((z*w) + x)) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite((((x + (y*c0)) + z) % c1), ((x + z) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite((((x + (y*c0)) + z) % c1), (((x + y) + z) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite((((x + min(y, z)) + (w + (u*c0))) % c1), (((x + min(y, z)) + w) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite((((x + min(y, z)) + ((w*c0) + u)) % c1), (((x + min(y, z)) + u) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((((x*c0) + y) + z) % c1), ((y + z) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((((x*c0) + y) + z) % c1), (((x + y) + z) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((((x*c0) + (y*c1)) + z) % c2), (((x*c0) + z) % c2), prove_me((((c0 + c1) % c2) == (c0 % c2)))) ||
 rewrite(((((x*c0) + (y*z)) + w) % c1), (((x + (y*z)) + w) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((((x*y) + (z*c0)) + w) % c1), (((x*y) + w) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite((((min(x, y) + z) + (w + (u*c0))) % c1), (((min(x, y) + z) + w) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite((((min(x, y) + z) + ((w*c0) + u)) % c1), (((min(x, y) + z) + u) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite((((x*c0) + y) % c1), ((x + y) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((min(x, (y + c0)) + (z + (w*c1))) % c2), ((min(x, (y + c0)) + (z + w)) % c2), prove_me((((min(c0, 0) + c1) % c2) == ((min(c0, 0) + 1) % c2)))) ||
 rewrite(((min(x, (y + c0)) + ((z*c1) + w)) % c2), ((min(x, (y + c0)) + (z + w)) % c2), prove_me((((min(c0, 0) + c1) % c2) == ((min(c0, 0) + 1) % c2)))) ||
 rewrite(((min(x, (y + z)) + (w + (u*c0))) % c1), ((min(x, (y + z)) + (w + u)) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((min(x, (y + z)) + ((w*c0) + u)) % c1), ((min(x, (y + z)) + (w + u)) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((min((x + c0), y) + (z + (w*c1))) % c2), ((min((x + c0), y) + (z + w)) % c2), prove_me((((min(c0, 0) + c1) % c2) == ((min(c0, 0) + 1) % c2)))) ||
 rewrite(((min((x + c0), y) + ((z*c1) + w)) % c2), ((min((x + c0), y) + (z + w)) % c2), prove_me((((min(c0, 0) + c1) % c2) == ((min(c0, 0) + 1) % c2)))) ||
 rewrite(((min((x + y), z) + (w + (u*c0))) % c1), ((min((x + y), z) + (w + u)) % c1), prove_me(((c0 % c1) == (1 % c1)))) ||
 rewrite(((min((x + y), z) + ((w*c0) + u)) % c1), ((min((x + y), z) + (w + u)) % c1), prove_me(((c0 % c1) == (1 % c1))))))
