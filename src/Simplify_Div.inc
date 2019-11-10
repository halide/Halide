rewrite((((((x*c0) + y) - z) + c1)/c0), (x + (((y - z) + c1)/c0))) ||
rewrite((max((x*c0), c1)/c0), max(x, 0), ((0 <= c1) && ((max(c1, 0) + 1) <= c0))) ||
