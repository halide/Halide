rewrite(((x < y) && (y < (x + c0))), false, (c0 <= 1)) ||
rewrite(((x < (y + c0)) && (y < (x + c1))), false, ((c0 + c1) <= 1)) ||
rewrite(((x <= y) && ((min(w, (z*c0))/c0) <= z)), (x <= y), (1 <= c0)) ||
rewrite(((x <= y) && ((min((z*c0), w)/c0) <= z)), (x <= y), (1 <= c0)) ||
