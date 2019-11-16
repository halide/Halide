rewrite((0 % x), 0) ||
rewrite((c0 % c1), fold((c0 % c1))) ||
rewrite((x % 1), 0) ||
rewrite((x % x), 0) ||
rewrite(((y + (x*c0)) % c1), (y % c1), ((c0 % c1) == 0)) ||
rewrite((((x*c0) + y) % c1), (y % c1), ((c0 % c1) == 0)) ||
rewrite(((y - (x*c0)) % c1), (y % c1), ((c0 % c1) == 0)) ||
rewrite((((x*c0) - y) % c1), ((0 - y) % c1), ((c0 % c1) == 0)) ||
rewrite(((x*c0) % c1), ((x % fold((c1/c0)))*c0), ((c0 > 0) && ((c1 % c0) == 0))) ||
