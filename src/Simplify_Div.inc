((a.node_type() == IRNodeType::Mod) && EVAL_IN_LAMBDA(
 rewrite(((x % c0)/c1), 0, ((((((5 % c0)/c1) == 0) && (((8371 % c0)/c1) == 0)) && (((1402 % c0)/c1) == 0)) && (((-1 % c0)/c1) == 0))) ||
 rewrite((((x + (y*(z + w))) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((x + ((y + z)*w)) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((x + (y/c0)) % c1)/c2), 0, (((-1 % c1)/c2) == 0)) ||
 rewrite((((x + min(y, (z + w))) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((x + min(y, (z*w))) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((x + min((y + z), w)) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((x + min((y*z), w)) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite(((((x*(y + z)) + w) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((((x + y)*z) + w) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite(((((x/c0) + y) % c1)/c2), 0, (((-1 % c1)/c2) == 0)) ||
 rewrite((((min(x, (y + z)) + w) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((min(x, (y*z)) + w) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((min((x + y), z) + w) % c0)/c1), 0, (((-1 % c0)/c1) == 0)) ||
 rewrite((((min((x*y), z) + w) % c0)/c1), 0, (((-1 % c0)/c1) == 0))))
