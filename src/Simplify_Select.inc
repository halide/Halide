rewrite(select((c0 < x), (y + min(x, c1)), c0), select((x < c1), c0, (y + c1)), (((c1 + -1) <= c0) && ((c0 + 1) <= c1))) ||
rewrite(select((c0 < x), (min(x, c1) + y), c0), select((x < c1), c0, (y + c1)), (((c1 + -1) <= c0) && ((c0 + 1) <= c1))) ||
rewrite(select((c0 < x), (x*c1), min((x*c1), c2)), (x*c1), ((((0 <= c2) || (c0 <= -1)) || (c1 <= -1)) && ((1 <= c1) && ((c0*c1) <= c2)))) ||
rewrite(select((c0 < min(x, y)), (z + min(y, 0)), z), z, (-1 <= c0)) ||
rewrite(select((c0 < min(x, y)), (min(y, 0) + z), z), z, (-1 <= c0)) ||
rewrite(select((c0 < min(y, x)), (z + min(y, 0)), z), z, (-1 <= c0)) ||
rewrite(select((c0 < min(y, x)), (min(y, 0) + z), z), z, (-1 <= c0)) ||
rewrite(select((x < c0), c1, (c2 - max((x*c3), c4))), (c2 - max((x*c3), c4)), (((((1 <= c4) || (c2 == c1)) || (c0 <= 0)) || (c3 <= -1)) && (((1 <= c3) && ((c1 + c4) == c2)) && (((c0*c3) + c1) <= (c2 + c3))))) ||
rewrite(select((x < c0), (x*c1), min((x*c1), c2)), min((x*c1), c2), ((((0 <= c2) || (c0 <= 0)) || (c1 <= -1)) && ((1 <= c1) && ((c0*c1) <= (c1 + c2))))) ||
rewrite(select((x < y), c0, min((x - y), c1)), select((y < x), c1, c0), ((c1 <= 1) && (min(c1, 0) == c0))) ||
rewrite(select((x < y), min(x, (y + c0)), x), x, (-1 <= c0)) ||
rewrite(select((x < y), min((y + c0), x), x), x, (-1 <= c0)) ||
rewrite(select((x < (y + c0)), (z + min((y - x), 0)), z), z, (c0 <= 1)) ||
rewrite(select((x < (y + c0)), (min((y - x), 0) + z), z), z, (c0 <= 1)) ||
rewrite(select((x < (y + c0)), min(x, y), x), x, (c0 <= 1)) ||
rewrite(select((x < (y + c0)), min(x, (y + c1)), x), x, ((c0 + -1) <= c1)) ||
rewrite(select((x < (y + c0)), min(y, x), x), x, (c0 <= 1)) ||
rewrite(select((x < (y + c0)), min((y + c1), x), x), x, ((c0 + -1) <= c1)) ||
rewrite(select((x < (y + c0)), min((y - x), c1), z), select((x < (y + c0)), c1, z), ((c0 + c1) <= 1)) ||

/*
// From local laplacian
rewrite(select(x, (y + z), ((w + z) + c0)), (select(x, y, (w + c0)) + z)) ||
rewrite(select(x, (y + z), ((y + w) + c0)), (select(x, z, (w + c0)) + y)) ||
rewrite(select(x, (y + (w + z)), ((u + w) + c0)), (select(x, (y + z), (u + c0)) + w)) ||
rewrite(select(x, (y + (z + w)), ((u + w) + c0)), (select(x, (y + z), (u + c0)) + w)) ||
rewrite(select(x, (z + y), ((w + z) + c0)), (select(x, y, (w + c0)) + z)) ||
rewrite(select(x, (z + y), ((y + w) + c0)), (select(x, z, (w + c0)) + y)) ||
rewrite(select(x, ((w + z) + y), ((u + w) + c0)), (select(x, (y + z), (u + c0)) + w)) ||
rewrite(select(x, ((y + z) + c0), (w + y)), (select(x, (z + c0), w) + y)) ||
rewrite(select(x, ((y + z) + c0), (w + z)), (select(x, (y + c0), w) + z)) ||
rewrite(select(x, ((y + z) + c0), (y + w)), (select(x, (z + c0), w) + y)) ||
rewrite(select(x, ((y + z) + c0), (z + w)), (select(x, (y + c0), w) + z)) ||
rewrite(select(x, ((z + w) + y), ((u + w) + c0)), (select(x, (y + z), (u + c0)) + w)) ||

rewrite(select(x, ((y/c0) + 1), ((y + c2)/c0)), ((select(x, c0, c2) + y)/c0)) ||

rewrite(select(x, ((y/c0) + -1), ((y + c2)/c0)), ((y - select(x, c0, fold((0 - c2))))/c0)) ||
rewrite(select(x, ((y + c0)/c1), ((y/c1) + -1)), ((y - select(x, fold((0 - c0)), c1))/c1)) ||
*/