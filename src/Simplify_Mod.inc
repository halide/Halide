((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + (y + (z*c0))) % c0), ((x + y) % c0), prove_me(IRMatcher::Const(1))) ||
 rewrite(((x + (y + (z*c0))) % c1), ((x + y) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((x + ((y*c0) + z)) % c0), ((x + z) % c0), prove_me(IRMatcher::Const(1))) ||
 rewrite(((x + ((y*c0) + z)) % c1), ((x + z) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((x + ((y*c0) + (z*c1))) % c2), ((x + (y*c0)) % c2), prove_me(((c1 % c2) == 0))) ||
 rewrite(((x + ((y*c0) + (z*c1))) % c2), ((x + y) % c2), prove_me((((c0 % c2) == (1 % c2)) && ((c1 % c2) == 0)))) ||
 rewrite(((x + (min(y, c0) + (z*c1))) % c2), ((x + min(y, c0)) % c2), prove_me((((min(c0, 0) + c1) % c2) == (min(c0, 0) % c2)))) ||
 rewrite(((x + (min(y, (z*c0)) + (w*c1))) % c2), ((x + min(y, (z*c0))) % c2), prove_me(((c1 % c2) == 0))) ||
 rewrite(((x + (min((y*c0), c1) + (z*c2))) % c3), ((x + min((y*c0), c1)) % c3), prove_me((((min(c1, 0) + c2) % c3) == (min(c1, 0) % c3)))) ||
 rewrite(((x + (min((y*c0), z) + (w*c1))) % c2), ((x + min((y*c0), z)) % c2), prove_me(((c1 % c2) == 0))) ||
 rewrite((((x + (y*c0)) + z) % c0), ((x + z) % c0), prove_me(IRMatcher::Const(1))) ||
 rewrite((((x + (y*c0)) + z) % c1), ((x + z) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((((x*c0) + y) + z) % c0), ((y + z) % c0), prove_me(IRMatcher::Const(1))) ||
 rewrite(((((x*c0) + y) + z) % c1), ((y + z) % c1), prove_me(((c0 % c1) == 0))) ||
 rewrite(((((x*c0) + (y*c1)) + z) % c2), (((x*c0) + z) % c2), prove_me(((c1 % c2) == 0))) ||
 rewrite(((((x*c0) + (y*c1)) + z) % c2), ((x + z) % c2), prove_me((((c0 % c2) == (1 % c2)) && ((c1 % c2) == 0)))) ||
 rewrite((((min(x, c0) + (y*c1)) + z) % c2), ((min(x, c0) + z) % c2), prove_me((((min(c0, 0) + c1) % c2) == (min(c0, 0) % c2)))) ||
 rewrite((((min(x, (y*c0)) + (z*c1)) + w) % c2), ((min(x, (y*c0)) + w) % c2), prove_me(((c1 % c2) == 0))) ||
 rewrite((((min((x*c0), c1) + (y*c2)) + z) % c3), ((min((x*c0), c1) + z) % c3), prove_me((((min(c1, 0) + c2) % c3) == (min(c1, 0) % c3)))) ||
 rewrite((((min((x*c0), y) + (z*c1)) + w) % c2), ((min((x*c0), y) + w) % c2), prove_me(((c1 % c2) == 0)))))
