rewrite(min(w, min(z, select((x < y), z, max(w, c0)))), min(w, z)) ||
rewrite(min(w, min(select((x < y), z, max(w, c0)), z)), min(w, z)) ||
rewrite(min(x, (y + ((((x - y) + c0)/c1)*c1))), x, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
rewrite(min(x, (((((x - y)/c0)*c0) + y) + c1)), x, ((-1 <= (c0 + c1)) && ((c0 + -1) <= c1))) ||
rewrite(min(x, (((((x - y) + c0)/c1)*c1) + y)), x, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
rewrite(min(x, ((((min(x, c0) + c1)/c2)*c2) + c2)), min(x, c0), (((-1 <= ((c2*2) + c1)) && (c1 <= 1)) && (c2 <= -1))) ||
rewrite(min(x, ((min((x/c0), y)*c0) + c1)), min(((y*c0) + c1), x), ((1 <= c0) && ((max(c0, 1) + -1) <= c1))) ||
rewrite(min(x, (min(y, (x + c0)) + c1)), min((y + c1), x), (0 <= (c0 + c1))) ||
rewrite(min(x, (min(y, (x + c0)) + c1)), (min((x + c0), y) + c1), ((c0 + c1) <= 0)) ||
rewrite(min(x, (min((x + c0), y) + c1)), min((y + c1), x), (0 <= (c0 + c1))) ||
rewrite(min(x, (min((x + c0), y) + c1)), (min((x + c0), y) + c1), ((c0 + c1) <= 0)) ||
rewrite(min(x, (min(min(y, (x + c0)), z) + c1)), min((min(y, z) + c1), x), (0 <= (c0 + c1))) ||
rewrite(min(x, (min(min((x + c0), y), z) + c1)), min((min(y, z) + c1), x), (0 <= (c0 + c1))) ||
rewrite(min(x, (max(min(x, y), c0) + c0)), min((max(y, c0) + c0), x), (0 <= c0)) ||
rewrite(min(x, (min(((x + c0)/c1), y)*c1)), min((y*c1), x), ((1 <= c1) && ((c1 + -1) <= c0))) ||
rewrite(min(x, min(w, select((x < y), max(x, z), w))), min(w, x)) ||
rewrite(min(x, min(w, select((x < y), max(z, x), w))), min(w, x)) ||
rewrite(min(x, min(x, y)), min(x, y)) ||
rewrite(min(x, min(y, x)), min(x, y)) ||
rewrite(min(x, min(y, (x + c0))), min(x, y), (0 <= c0)) ||
rewrite(min(x, min(y, (x + c0))), min((x + c0), y), (c0 <= 0)) ||
rewrite(min(x, min(z, (min((x + c0), y) + c1))), min(min((y + c1), z), x), (0 <= (c0 + c1))) ||
rewrite(min(x, min(z, min(y, (x + c0)))), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(x, min(z, min(y, (x + c0)))), min(min((x + c0), y), z), (c0 <= 0)) ||
rewrite(min(x, min(z, min((x + c0), y))), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(x, min(z, min((x + c0), y))), min(min((x + c0), y), z), (c0 <= 0)) ||
rewrite(min(x, min((x + c0), y)), min(x, y), (0 <= c0)) ||
rewrite(min(x, min((x + c0), y)), min((x + c0), y), (c0 <= 0)) ||
rewrite(min(x, min((min((x + c0), y) + c1), z)), min(min((y + c1), z), x), (0 <= (c0 + c1))) ||
rewrite(min(x, min(min(y, (x + c0)), z)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(x, min(min(y, (x + c0)), z)), min(min((x + c0), y), z), (c0 <= 0)) ||
rewrite(min(x, min(min((x + c0), y), z)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(x, min(min((x + c0), y), z)), min(min((x + c0), y), z), (c0 <= 0)) ||
rewrite(min(x, min(select((x < y), max(x, z), w), w)), min(w, x)) ||
rewrite(min(x, min(select((x < y), max(z, x), w), w)), min(w, x)) ||
rewrite(min(y, (min(x, (y + c0)) + c1)), (min((x + fold(max((-1 - c0), (c1 - max(min(min((c1 - max((0 - c0), c1)), (c0 + c1)), 0), (c1 - max((0 - c0), c1))))))), y) + fold(max(min(min((c1 - max((0 - c0), c1)), (c0 + c1)), 0), (c1 - max((0 - c0), c1)))))) ||
rewrite(min(y, (min(x, (y + c0)) + c1)), min((x + c1), y), (0 <= (c0 + c1))) ||
rewrite(min(y, (min(min(x, y), z) + c0)), min((min(x, z) + c0), y), (0 <= c0)) ||
rewrite(min(y, (min(min((x + c0), y), c0) + c1)), min(min((x + fold((max(c1, 0) + c0))), y), fold((max(c1, 0) + c0))), (0 <= c1)) ||
rewrite(min(y, min(x, (y + c0))), min(x, y), (0 <= c0)) ||
rewrite(min(y, min(x, (y + c0))), min((y + c0), x), (c0 <= 0)) ||
rewrite(min(y, min(x, (z + min(x, y)))), (min(x, y) + min(z, 0))) ||
rewrite(min(y, min(x, (z + min(y, x)))), (min(x, y) + min(z, 0))) ||
rewrite(min(y, min(x, (min(x, y) + z))), (min(x, y) + min(z, 0))) ||
rewrite(min(y, min(x, (min(y, x) + z))), (min(x, y) + min(z, 0))) ||
rewrite(min(y, min(x, (min((y + c0), z) + c1))), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
rewrite(min(y, min(z, (y + min(x, c0)))), min((min(x, fold(min(c0, 0))) + y), z)) ||
rewrite(min(y, min(z, (min(x, c0) + y))), min((min(x, fold(min(c0, 0))) + y), z)) ||
rewrite(min(y, min(z, (min(x, y) + c0))), min(min((x + c0), z), y), (0 <= c0)) ||
rewrite(min(y, min(z, min(x, (y + c0)))), min(min(x, z), (y + c0)), (c0 <= 0)) ||
rewrite(min(y, min(z, min((y + c0), x))), min(min(x, z), (y + c0)), (c0 <= 0)) ||
rewrite(min(y, min((y + c0), x)), min(x, y), (0 <= c0)) ||
rewrite(min(y, min((y + c0), x)), min((y + c0), x), (c0 <= 0)) ||
rewrite(min(y, min((y + min(x, c0)), z)), min((min(x, fold(min(c0, 0))) + y), z)) ||
rewrite(min(y, min((z + min(x, y)), x)), (min(x, y) + min(z, 0))) ||
rewrite(min(y, min((z + min(y, x)), x)), (min(x, y) + min(z, 0))) ||
rewrite(min(y, min((min(x, c0) + y), z)), min((min(x, fold(min(c0, 0))) + y), z)) ||
rewrite(min(y, min((min(x, y) + c0), z)), min(min((x + c0), z), y), (0 <= c0)) ||
rewrite(min(y, min((min(x, y) + z), x)), (min(x, y) + min(z, 0))) ||
rewrite(min(y, min((min(y, x) + z), x)), (min(x, y) + min(z, 0))) ||
rewrite(min(y, min((min((y + c0), z) + c1), x)), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
rewrite(min(y, min(min(x, (y + c0)), z)), min(min(x, z), (y + c0)), (c0 <= 0)) ||
rewrite(min(y, min(min((y + c0), x), z)), min(min(x, z), (y + c0)), (c0 <= 0)) ||
rewrite(min(z, min(x, (min(y, z) + c0))), min(min(x, z), (y + c0)), (0 <= c0)) ||
rewrite(min(z, min(x, (min((y + c0), z) + c1))), min(min(x, z), (y + fold((max(c1, 0) + c0)))), (0 <= c1)) ||
rewrite(min(z, min(y, (x + min(y, z)))), (min(y, z) + min(x, 0))) ||
rewrite(min(z, min(y, (x + min(z, y)))), (min(y, z) + min(x, 0))) ||
rewrite(min(z, min(y, (min(y, z) + x))), (min(y, z) + min(x, 0))) ||
rewrite(min(z, min(y, (min(z, y) + x))), (min(y, z) + min(x, 0))) ||
rewrite(min(z, min((x + min(y, z)), y)), (min(y, z) + min(x, 0))) ||
rewrite(min(z, min((x + min(z, y)), y)), (min(y, z) + min(x, 0))) ||
rewrite(min(z, min((min(y, z) + c0), x)), min(min(x, z), (y + c0)), (0 <= c0)) ||
rewrite(min(z, min((min(y, z) + x), y)), (min(y, z) + min(x, 0))) ||
rewrite(min(z, min((min(z, y) + x), y)), (min(y, z) + min(x, 0))) ||
rewrite(min(z, min((min((y + c0), z) + c1), x)), min(min(x, z), (y + fold((max(c1, 0) + c0)))), (0 <= c1)) ||
rewrite(min((w + (x + (y + z))), (u + z)), (min(((x + y) + w), u) + z)) ||
rewrite(min((w + (x + (y + z))), (z + u)), (min(((x + y) + w), u) + z)) ||
rewrite(min((w + (x + (z + y))), (u + z)), (min(((x + y) + w), u) + z)) ||
rewrite(min((w + (x + (z + y))), (z + u)), (min(((x + y) + w), u) + z)) ||
rewrite(min((w + ((y + z) + x)), (u + z)), (min(((x + y) + w), u) + z)) ||
rewrite(min((w + ((y + z) + x)), (z + u)), (min(((x + y) + w), u) + z)) ||
rewrite(min((w + ((z + y) + x)), (u + z)), (min(((x + y) + w), u) + z)) ||
rewrite(min((w + ((z + y) + x)), (z + u)), (min(((x + y) + w), u) + z)) ||
rewrite(min((x + y), (z + (w + y))), (min((w + z), x) + y)) ||
rewrite(min((x + y), (z + (y + w))), (min((w + z), x) + y)) ||
rewrite(min((x + y), ((w + y) + z)), (min((w + z), x) + y)) ||
rewrite(min((x + y), ((y + w) + z)), (min((w + z), x) + y)) ||
rewrite(min((x + y), (min(y, c0) + c1)), min((min(x, c1) + y), fold((c0 + c1)))) ||
rewrite(min((x + (y + z)), (w + z)), (min((x + y), w) + z)) ||
rewrite(min((x + (y + z)), (z + w)), (min((x + y), w) + z)) ||
rewrite(min((x + (z + y)), (w + z)), (min((x + y), w) + z)) ||
rewrite(min((x + (z + y)), (z + w)), (min((x + y), w) + z)) ||
rewrite(min((y + x), (z + (w + y))), (min((w + z), x) + y)) ||
rewrite(min((y + x), (z + (y + w))), (min((w + z), x) + y)) ||
rewrite(min((y + x), ((w + y) + z)), (min((w + z), x) + y)) ||
rewrite(min((y + x), ((y + w) + z)), (min((w + z), x) + y)) ||
rewrite(min((y + x), (min(y, c0) + c1)), min((min(x, c1) + y), fold((c0 + c1)))) ||
rewrite(min((y + ((((x - y) + c0)/c1)*c1)), x), x, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
rewrite(min(((x + (y + z)) + w), (u + z)), (min(((x + y) + w), u) + z)) ||
rewrite(min(((x + (y + z)) + w), (z + u)), (min(((x + y) + w), u) + z)) ||
rewrite(min(((x + (z + y)) + w), (u + z)), (min(((x + y) + w), u) + z)) ||
rewrite(min(((x + (z + y)) + w), (z + u)), (min(((x + y) + w), u) + z)) ||
rewrite(min(((y + z) + x), (w + z)), (min((x + y), w) + z)) ||
rewrite(min(((y + z) + x), (z + w)), (min((x + y), w) + z)) ||
rewrite(min(((z + y) + x), (w + z)), (min((x + y), w) + z)) ||
rewrite(min(((z + y) + x), (z + w)), (min((x + y), w) + z)) ||
rewrite(min((((y + z) + x) + w), (u + z)), (min(((x + y) + w), u) + z)) ||
rewrite(min((((y + z) + x) + w), (z + u)), (min(((x + y) + w), u) + z)) ||
rewrite(min((((z + y) + x) + w), (u + z)), (min(((x + y) + w), u) + z)) ||
rewrite(min((((z + y) + x) + w), (z + u)), (min(((x + y) + w), u) + z)) ||
rewrite(min((((((x - y)/c0)*c0) + y) + c1), x), x, ((-1 <= (c0 + c1)) && ((c0 + -1) <= c1))) ||
rewrite(min((((((x - y) + c0)/c1)*c1) + y), x), x, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
rewrite(min(((((min(x, c0) + c1)/c2)*c2) + c2), x), min(x, c0), (((-1 <= ((c2*2) + c1)) && (c1 <= 1)) && (c2 <= -1))) ||
rewrite(min(((min((x/c0), y)*c0) + c1), x), min(((y*c0) + c1), x), ((1 <= c0) && ((max(c0, 1) + -1) <= c1))) ||
rewrite(min((min(x, (y + c0)) + c1), y), (min((x + fold(max((-1 - c0), (c1 - max(min(min((c1 - max((0 - c0), c1)), (c0 + c1)), 0), (c1 - max((0 - c0), c1))))))), y) + fold(max(min(min((c1 - max((0 - c0), c1)), (c0 + c1)), 0), (c1 - max((0 - c0), c1)))))) ||
rewrite(min((min(x, (y + z)) + c0), (y + z)), min((x + c0), (y + z)), (0 <= c0)) ||
rewrite(min((min(x, (y + z)) + c0), (z + y)), min((x + c0), (y + z)), (0 <= c0)) ||
rewrite(min((min((x + c0), y) + c1), x), min((y + c1), x), (0 <= (c0 + c1))) ||
rewrite(min((min((x + c0), y) + c1), x), (min((x + c0), y) + c1), ((c0 + c1) <= 0)) ||
rewrite(min((min((x + y), (z + c0)) + c1), (x + y)), min((z + fold((max(c1, -1) + c0))), (x + y)), (0 <= c1)) ||
rewrite(min((min((x + y), (z + c0)) + c1), (y + x)), min((z + fold((max(c1, -1) + c0))), (x + y)), (0 <= c1)) ||
rewrite(min((min(min(x, y), z) + c0), y), min((min(x, z) + c0), y), (0 <= c0)) ||
rewrite(min((min(min(y, (x + c0)), z) + c1), x), min((min(y, z) + c1), x), (0 <= (c0 + c1))) ||
rewrite(min((min(min((x + c0), y), c0) + c1), y), min(min((x + fold((max(c1, 0) + c0))), y), fold((max(c1, 0) + c0))), (0 <= c1)) ||
rewrite(min((min(min((x + c0), y), z) + c1), x), min((min(y, z) + c1), x), (0 <= (c0 + c1))) ||
rewrite(min((max(min(x, y), c0) + c0), x), min((max(y, c0) + c0), x), (0 <= c0)) ||
rewrite(min((x*c0), ((min(x, y)*c0) + c1)), min((x*c0), ((y*c0) + c1)), ((0 <= c1) && (1 <= c0))) ||
rewrite(min((x*c0), ((min(x, y)*c0) + c1)), ((min(x, y)*c0) + c1), ((1 <= c0) && (c1 <= 0))) ||
rewrite(min((x*c0), ((min(y, x)*c0) + c1)), min((x*c0), ((y*c0) + c1)), ((0 <= c1) && (1 <= c0))) ||
rewrite(min((x*c0), ((min(y, x)*c0) + c1)), ((min(x, y)*c0) + c1), ((1 <= c0) && (c1 <= 0))) ||
rewrite(min((x*c0), ((min((x + c0), y)*c0) + c1)), min((x*c0), ((y*c0) + c1)), ((0 <= ((c0*c0) + c1)) && (1 <= c0))) ||
rewrite(min(((x + y)*c0), ((y*c0) + c1)), (min((x*c0), c1) + (y*c0))) ||
rewrite(min(((x/c0)*c0), (x + c1)), (x + c1), (((c1 <= 0) && ((c1 + -1) <= c0)) && ((c0 + c1) <= 1))) ||
rewrite(min((((x + c0)/c1)*c1), (x + c2)), (x + c2), ((0 <= c1) && ((c1 + c2) < (c0 + 2)))) ||
rewrite(min((((x + c0)/c1)*c1), (min(x, y) + c2)), (min(x, y) + c2), ((0 <= c1) && ((c1 + c2) < (c0 + 2)))) ||
rewrite(min((((c0 - x)/c1)*c1), (c2 - x)), (c2 - x), (((c2 + -1) <= (c0 + c1)) && (((c1 + c2) + -1) <= c0))) ||
rewrite(min((min(x, y)*c0), ((x*c0) + c1)), (min(x, y)*c0), ((0 <= c1) && (1 <= c0))) ||
rewrite(min((min(((x + c0)/c1), y)*c1), x), min((y*c1), x), ((1 <= c1) && ((c1 + -1) <= c0))) ||
rewrite(min(min(w, (z + min(x, y))), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
rewrite(min(min(w, (z + min(y, x))), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
rewrite(min(min(w, (min(x, y) + z)), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
rewrite(min(min(w, (min(y, x) + z)), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
rewrite(min(min(w, min(z, min(x, y))), (x + c0)), min(min(min(w, y), x), z), (0 <= c0)) ||
rewrite(min(min(w, min(z, min(y, x))), (x + c0)), min(min(min(w, y), x), z), (0 <= c0)) ||
rewrite(min(min(w, min(z, min(y, (x + c0)))), (x + c1)), min(min(min(w, y), z), (x + c1)), (c1 <= c0)) ||
rewrite(min(min(w, min(z, min((x + c0), y))), (x + c1)), min(min(min(w, y), z), (x + c1)), (c1 <= c0)) ||
rewrite(min(min(w, min(min(x, y), z)), (x + c0)), min(min(min(w, y), x), z), (0 <= c0)) ||
rewrite(min(min(w, min(min(y, x), z)), (x + c0)), min(min(min(w, y), x), z), (0 <= c0)) ||
rewrite(min(min(w, min(min(y, (x + c0)), z)), (x + c1)), min(min(min(w, y), z), (x + c1)), (c1 <= c0)) ||
rewrite(min(min(w, min(min((x + c0), y), z)), (x + c1)), min(min(min(w, y), z), (x + c1)), (c1 <= c0)) ||
rewrite(min(min(w, select((x < y), max(x, z), w)), x), min(w, x)) ||
rewrite(min(min(w, select((x < y), max(z, x), w)), x), min(w, x)) ||
rewrite(min(min(x, y), x), min(x, y)) ||
rewrite(min(min(x, y), (x + c0)), min(x, y), (0 <= c0)) ||
rewrite(min(min(x, y), (x + c0)), min((x + c0), y), (c0 <= 0)) ||
rewrite(min(min(x, y), (y + c0)), min(x, y), (0 <= c0)) ||
rewrite(min(min(x, y), (y + c0)), min((y + c0), x), (c0 <= 0)) ||
rewrite(min(min(x, y), (z + min(x, y))), (min(x, y) + min(z, 0))) ||
rewrite(min(min(x, y), (z + min(y, x))), (min(x, y) + min(z, 0))) ||
rewrite(min(min(x, y), (min(x, y) + z)), (min(x, y) + min(z, 0))) ||
rewrite(min(min(x, y), (min(x, z) + c0)), min((min(x, z) + c0), y), (c0 <= 0)) ||
rewrite(min(min(x, y), (min(y, x) + z)), (min(x, y) + min(z, 0))) ||
rewrite(min(min(x, y), (min(y, z) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
rewrite(min(min(x, y), (min(y, z) + c0)), min((min(y, z) + c0), x), (c0 <= 0)) ||
rewrite(min(min(x, y), (min(z, x) + c0)), min((min(x, z) + c0), y), (c0 <= 0)) ||
rewrite(min(min(x, y), (min(z, y) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
rewrite(min(min(x, y), (min(z, y) + c0)), min((min(y, z) + c0), x), (c0 <= 0)) ||
rewrite(min(min(x, y), (min((x + c0), z) + c1)), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
rewrite(min(min(x, y), (min(min(x, z), w) + c0)), min((min(min(w, x), z) + c0), y), (c0 <= 0)) ||
rewrite(min(min(x, y), (min(min((y + c0), x), z) + c1)), min((min(x, z) + c1), y), (((-1 <= c0) && (0 <= (c0 + c1))) && (c1 <= 0))) ||
rewrite(min(min(x, (w + min(y, z))), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
rewrite(min(min(x, (w + min(z, y))), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
rewrite(min(min(x, (y + c0)), y), min(x, y), (0 <= c0)) ||
rewrite(min(min(x, (y + c0)), y), min((y + c0), x), (c0 <= 0)) ||
rewrite(min(min(x, (y + c0)), (min(x, (y + c1)) + c2)), min((y + fold(min((c1 + c2), c0))), x), (0 <= c2)) ||
rewrite(min(min(x, (y + c0)), (min(x, (y + c1)) + c2)), (min((y + fold(min((c0 - c2), c1))), x) + c2), (c2 <= 0)) ||
rewrite(min(min(x, (y + c0)), (min(x, (y + c1)) + c2)), min((y + c0), x), ((0 <= c2) && (c0 <= (c1 + c2)))) ||
rewrite(min(min(x, (y + c0)), (min(z, y) + c1)), min((min(y, z) + c1), x), (c1 <= c0)) ||
rewrite(min(min(x, (z + min(x, y))), y), (min(x, y) + min(z, 0))) ||
rewrite(min(min(x, (z + min(y, c0))), z), min(min(x, z), (y + z)), (0 <= c0)) ||
rewrite(min(min(x, (z + min(y, x))), y), (min(x, y) + min(z, 0))) ||
rewrite(min(min(x, ((y + z) + c0)), (y + min(w, z))), min((min(w, z) + y), x), (0 <= c0)) ||
rewrite(min(min(x, ((y + z) + c0)), (y + min(z, w))), min((min(w, z) + y), x), (0 <= c0)) ||
rewrite(min(min(x, ((y + z) + c0)), (min(w, z) + y)), min((min(w, z) + y), x), (0 <= c0)) ||
rewrite(min(min(x, ((y + z) + c0)), (min(z, w) + y)), min((min(w, z) + y), x), (0 <= c0)) ||
rewrite(min(min(x, (min(x, y) + z)), y), (min(x, y) + min(z, 0))) ||
rewrite(min(min(x, (min(y, c0) + c1)), (min(y, c2) + c3)), min((min(y, c0) + c1), x), ((c1 <= c3) && ((c0 + c1) <= (c2 + c3)))) ||
rewrite(min(min(x, (min(y, c0) + z)), z), min(min(x, z), (y + z)), (0 <= c0)) ||
rewrite(min(min(x, (min(y, x) + z)), y), (min(x, y) + min(z, 0))) ||
rewrite(min(min(x, (min(y, z) + c0)), z), min(min(x, z), (y + c0)), (0 <= c0)) ||
rewrite(min(min(x, (min(y, z) + c0)), (z + c1)), min((min(y, z) + c0), x), (c0 <= c1)) ||
rewrite(min(min(x, (min(y, z) + w)), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
rewrite(min(min(x, (min(z, y) + w)), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
rewrite(min(min(x, (min((y + c0), z) + c1)), y), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
rewrite(min(min(x, (min((y + c0), z) + c1)), z), min(min(x, z), (y + fold((max(c1, 0) + c0)))), (0 <= c1)) ||
rewrite(min(min(y, x), x), min(x, y)) ||
rewrite(min(min(y, x), (x + c0)), min(x, y), (0 <= c0)) ||
rewrite(min(min(y, x), (x + c0)), min((x + c0), y), (c0 <= 0)) ||
rewrite(min(min(y, x), (y + c0)), min(x, y), (0 <= c0)) ||
rewrite(min(min(y, x), (y + c0)), min((y + c0), x), (c0 <= 0)) ||
rewrite(min(min(y, x), (z + min(x, y))), (min(x, y) + min(z, 0))) ||
rewrite(min(min(y, x), (z + min(y, x))), (min(x, y) + min(z, 0))) ||
rewrite(min(min(y, x), (min(x, y) + z)), (min(x, y) + min(z, 0))) ||
rewrite(min(min(y, x), (min(x, z) + c0)), min((min(x, z) + c0), y), (c0 <= 0)) ||
rewrite(min(min(y, x), (min(y, x) + z)), (min(x, y) + min(z, 0))) ||
rewrite(min(min(y, x), (min(y, z) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
rewrite(min(min(y, x), (min(y, z) + c0)), min((min(y, z) + c0), x), (c0 <= 0)) ||
rewrite(min(min(y, x), (min(z, x) + c0)), min((min(x, z) + c0), y), (c0 <= 0)) ||
rewrite(min(min(y, x), (min(z, y) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
rewrite(min(min(y, x), (min(z, y) + c0)), min((min(y, z) + c0), x), (c0 <= 0)) ||
rewrite(min(min(y, x), (min((x + c0), z) + c1)), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
rewrite(min(min(y, x), (min(min(x, z), w) + c0)), min((min(min(w, x), z) + c0), y), (c0 <= 0)) ||
rewrite(min(min(y, x), (min(min((y + c0), x), z) + c1)), min((min(x, z) + c1), y), (((-1 <= c0) && (0 <= (c0 + c1))) && (c1 <= 0))) ||
rewrite(min(min(y, (x + c0)), x), min(x, y), (0 <= c0)) ||
rewrite(min(min(y, (x + c0)), x), min((x + c0), y), (c0 <= 0)) ||
rewrite(min(min(y, (x + c0)), (min(x, z) + c1)), min((min(x, z) + c1), y), (c1 <= c0)) ||
rewrite(min(min(y, (x + c0)), (min(z, x) + c1)), min((min(x, z) + c1), y), (c1 <= c0)) ||
rewrite(min(min(y, (x + c0)), (min((x + c1), y) + c2)), (min((x + fold(min((c0 - c2), c1))), y) + c2), (c2 <= 0)) ||
rewrite(min(min(y, (x + min(y, z))), z), (min(y, z) + min(x, 0))) ||
rewrite(min(min(y, (x + min(z, y))), z), (min(y, z) + min(x, 0))) ||
rewrite(min(min(y, (min(y, z) + x)), z), (min(y, z) + min(x, 0))) ||
rewrite(min(min(y, (min(z, y) + x)), z), (min(y, z) + min(x, 0))) ||
rewrite(min(min(z, (x + y)), ((y + x) + c0)), min(((x + y) + c0), z), (c0 <= 0)) ||
rewrite(min(min(z, (y + min(x, c0))), y), min((min(x, fold(min(c0, 0))) + y), z)) ||
rewrite(min(min(z, ((x + y) + c0)), (x + min(w, y))), min((min(w, y) + x), z), (0 <= c0)) ||
rewrite(min(min(z, ((x + y) + c0)), (x + min(y, w))), min((min(w, y) + x), z), (0 <= c0)) ||
rewrite(min(min(z, ((x + y) + c0)), (y + x)), min((x + y), z), (0 <= c0)) ||
rewrite(min(min(z, ((x + y) + c0)), (min(w, y) + x)), min((min(w, y) + x), z), (0 <= c0)) ||
rewrite(min(min(z, ((x + y) + c0)), (min(y, w) + x)), min((min(w, y) + x), z), (0 <= c0)) ||
rewrite(min(min(z, (min(x, c0) + y)), y), min((min(x, fold(min(c0, 0))) + y), z)) ||
rewrite(min(min(z, (min(x, y) + c0)), y), min(min((x + c0), z), y), (0 <= c0)) ||
rewrite(min(min(z, (min((x + c0), y) + c1)), x), min(min((y + c1), z), x), (0 <= (c0 + c1))) ||
rewrite(min(min(z, min(x, y)), (x + c0)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(z, min(x, y)), (y + c0)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(z, min(x, (y + c0))), y), min(min(x, z), (y + c0)), (c0 <= 0)) ||
rewrite(min(min(z, min(y, x)), (x + c0)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(z, min(y, x)), (y + c0)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(z, min(y, (x + c0))), x), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(z, min(y, (x + c0))), x), min(min((x + c0), y), z), (c0 <= 0)) ||
rewrite(min(min(z, min(y, (x + c0))), (x + c1)), min(min((x + c1), z), y), (c1 <= c0)) ||
rewrite(min(min(z, min((x + c0), y)), x), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(z, min((x + c0), y)), x), min(min((x + c0), y), z), (c0 <= 0)) ||
rewrite(min(min(z, min((x + c0), y)), (x + c1)), min(min((x + c1), z), y), (c1 <= c0)) ||
rewrite(min(min(z, min((y + c0), x)), y), min(min(x, z), (y + c0)), (c0 <= 0)) ||
rewrite(min(min(z, select((x < y), z, max(w, c0))), w), min(w, z)) ||
rewrite(min(min((w + min(y, z)), x), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
rewrite(min(min((w + min(z, y)), x), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
rewrite(min(min((x + c0), y), x), min(x, y), (0 <= c0)) ||
rewrite(min(min((x + c0), y), x), min((x + c0), y), (c0 <= 0)) ||
rewrite(min(min((x + c0), y), (min(x, z) + c1)), min((min(x, z) + c1), y), (c1 <= c0)) ||
rewrite(min(min((x + c0), y), (min(z, x) + c1)), min((min(x, z) + c1), y), (c1 <= c0)) ||
rewrite(min(min((x + c0), y), (min((x + c1), y) + c2)), (min((x + fold(min((c0 - c2), c1))), y) + c2), (c2 <= 0)) ||
rewrite(min(min((x + y), z), ((y + x) + c0)), min(((x + y) + c0), z), (c0 <= 0)) ||
rewrite(min(min((x + min(y, z)), y), z), (min(y, z) + min(x, 0))) ||
rewrite(min(min((x + min(z, y)), y), z), (min(y, z) + min(x, 0))) ||
rewrite(min(min((y + c0), x), y), min(x, y), (0 <= c0)) ||
rewrite(min(min((y + c0), x), y), min((y + c0), x), (c0 <= 0)) ||
rewrite(min(min((y + c0), x), (min(x, (y + c1)) + c2)), min((y + fold(min((c1 + c2), c0))), x), (0 <= c2)) ||
rewrite(min(min((y + c0), x), (min(x, (y + c1)) + c2)), (min((y + fold(min((c0 - c2), c1))), x) + c2), (c2 <= 0)) ||
rewrite(min(min((y + c0), x), (min(x, (y + c1)) + c2)), min((y + c0), x), ((0 <= c2) && (c0 <= (c1 + c2)))) ||
rewrite(min(min((y + c0), x), (min(z, y) + c1)), min((min(y, z) + c1), x), (c1 <= c0)) ||
rewrite(min(min((y + min(x, c0)), z), y), min((min(x, fold(min(c0, 0))) + y), z)) ||
rewrite(min(min((z + min(x, y)), w), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
rewrite(min(min((z + min(x, y)), x), y), (min(x, y) + min(z, 0))) ||
rewrite(min(min((z + min(y, x)), w), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
rewrite(min(min((z + min(y, x)), x), y), (min(x, y) + min(z, 0))) ||
rewrite(min(min(((x + y) + c0), z), (x + min(w, y))), min((min(w, y) + x), z), (0 <= c0)) ||
rewrite(min(min(((x + y) + c0), z), (x + min(y, w))), min((min(w, y) + x), z), (0 <= c0)) ||
rewrite(min(min(((x + y) + c0), z), (y + x)), min((x + y), z), (0 <= c0)) ||
rewrite(min(min(((x + y) + c0), z), (min(w, y) + x)), min((min(w, y) + x), z), (0 <= c0)) ||
rewrite(min(min(((x + y) + c0), z), (min(y, w) + x)), min((min(w, y) + x), z), (0 <= c0)) ||
rewrite(min(min(((y + z) + c0), x), (y + min(w, z))), min((min(w, z) + y), x), (0 <= c0)) ||
rewrite(min(min(((y + z) + c0), x), (y + min(z, w))), min((min(w, z) + y), x), (0 <= c0)) ||
rewrite(min(min(((y + z) + c0), x), (min(w, z) + y)), min((min(w, z) + y), x), (0 <= c0)) ||
rewrite(min(min(((y + z) + c0), x), (min(z, w) + y)), min((min(w, z) + y), x), (0 <= c0)) ||
rewrite(min(min(((min(x, c0) + y) + c1), x), c0), ((min(y, fold((0 - c1))) + min(x, c0)) + c1)) ||
rewrite(min(min((min(x, c0) + y), z), y), min((min(x, fold(min(c0, 0))) + y), z)) ||
rewrite(min(min((min(x, y) + c0), z), y), min(min((x + c0), z), y), (0 <= c0)) ||
rewrite(min(min((min(x, y) + z), w), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
rewrite(min(min((min(x, y) + z), x), y), (min(x, y) + min(z, 0))) ||
rewrite(min(min((min(y, c0) + c1), x), (min(y, c2) + c3)), min((min(y, c0) + c1), x), ((c1 <= c3) && ((c0 + c1) <= (c2 + c3)))) ||
rewrite(min(min((min(y, x) + z), w), ((z + x) + c0)), min((min(x, y) + z), w), (0 <= c0)) ||
rewrite(min(min((min(y, x) + z), x), y), (min(x, y) + min(z, 0))) ||
rewrite(min(min((min(y, z) + c0), x), z), min(min(x, z), (y + c0)), (0 <= c0)) ||
rewrite(min(min((min(y, z) + c0), x), (z + c1)), min((min(y, z) + c0), x), (c0 <= c1)) ||
rewrite(min(min((min(y, z) + w), x), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
rewrite(min(min((min(y, z) + x), y), z), (min(y, z) + min(x, 0))) ||
rewrite(min(min((min(z, y) + w), x), ((w + z) + c0)), min((min(y, z) + w), x), (0 <= c0)) ||
rewrite(min(min((min(z, y) + x), y), z), (min(y, z) + min(x, 0))) ||
rewrite(min(min((min((x + c0), y) + c1), z), x), min(min((y + c1), z), x), (0 <= (c0 + c1))) ||
rewrite(min(min((min((y + c0), z) + c1), x), y), min(min(x, y), (z + c1)), (0 <= (c0 + c1))) ||
rewrite(min(min((min((y + c0), z) + c1), x), z), min(min(x, z), (y + fold((max(c1, 0) + c0)))), (0 <= c1)) ||
rewrite(min(min(min(x, y), z), (x + c0)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(min(x, y), z), (y + c0)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(min(x, (y + c0)), z), y), min(min(x, z), (y + c0)), (c0 <= 0)) ||
rewrite(min(min(min(y, x), z), (x + c0)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(min(y, x), z), (y + c0)), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(min(y, (x + c0)), z), x), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(min(y, (x + c0)), z), x), min(min((x + c0), y), z), (c0 <= 0)) ||
rewrite(min(min(min(y, (x + c0)), z), (x + c1)), min(min((x + c1), z), y), (c1 <= c0)) ||
rewrite(min(min(min(z, min(x, y)), w), (x + c0)), min(min(min(w, y), x), z), (0 <= c0)) ||
rewrite(min(min(min(z, min(y, x)), w), (x + c0)), min(min(min(w, y), x), z), (0 <= c0)) ||
rewrite(min(min(min(z, min(y, (x + c0))), w), (x + c1)), min(min(min(w, y), z), (x + c1)), (c1 <= c0)) ||
rewrite(min(min(min(z, min((x + c0), y)), w), (x + c1)), min(min(min(w, y), z), (x + c1)), (c1 <= c0)) ||
rewrite(min(min(min((x + c0), y), z), x), min(min(x, y), z), (0 <= c0)) ||
rewrite(min(min(min((x + c0), y), z), x), min(min((x + c0), y), z), (c0 <= 0)) ||
rewrite(min(min(min((x + c0), y), z), (x + c1)), min(min((x + c1), z), y), (c1 <= c0)) ||
rewrite(min(min(min((y + c0), x), z), y), min(min(x, z), (y + c0)), (c0 <= 0)) ||
rewrite(min(min(min(min(x, y), z), w), (x + c0)), min(min(min(w, y), x), z), (0 <= c0)) ||
rewrite(min(min(min(min(y, x), z), w), (x + c0)), min(min(min(w, y), x), z), (0 <= c0)) ||
rewrite(min(min(min(min(y, (x + c0)), z), w), (x + c1)), min(min(min(w, y), z), (x + c1)), (c1 <= c0)) ||
rewrite(min(min(min(min((x + c0), y), z), w), (x + c1)), min(min(min(w, y), z), (x + c1)), (c1 <= c0)) ||
rewrite(min(min(select((x < y), z, max(w, c0)), z), w), min(w, z)) ||
rewrite(min(min(select((x < y), max(x, z), w), w), x), min(w, x)) ||
rewrite(min(min(select((x < y), max(z, x), w), w), x), min(w, x)) ||
rewrite(min(max(x, y), min(w, max(y, z))), min(max(min(x, z), y), w)) ||
rewrite(min(max(x, y), min(w, max(z, y))), min(max(min(x, z), y), w)) ||
rewrite(min(max(x, y), min(max(y, z), w)), min(max(min(x, z), y), w)) ||
rewrite(min(max(x, y), min(max(z, y), w)), min(max(min(x, z), y), w)) ||
rewrite(min(max(y, x), min(w, max(y, z))), min(max(min(x, z), y), w)) ||
rewrite(min(max(y, x), min(w, max(z, y))), min(max(min(x, z), y), w)) ||
rewrite(min(max(y, x), min(max(y, z), w)), min(max(min(x, z), y), w)) ||
rewrite(min(max(y, x), min(max(z, y), w)), min(max(min(x, z), y), w)) ||
rewrite(min(max(y, (x + c0)), (max((y + c1), x) + c0)), max((x + c0), y), (0 <= (c0 + c1))) ||
rewrite(min(max(y, min(x, c0)), (min(x, c1) + c2)), (min(x, c1) + c2), ((c2 <= 0) && ((c1 + c2) <= c0))) ||
rewrite(min(max((x + c0), y), (max((y + c1), x) + c0)), max((x + c0), y), (0 <= (c0 + c1))) ||
rewrite(min(max(min(x, c0), c1), (min(x, y) + c2)), (min(min(x, y), fold((max(c0, c1) - c2))) + c2), (((c2 <= 0) && (c1 <= c0)) || (c0 <= c1))) ||
rewrite(min(max(min(x, c0), c1), (min(y, x) + c2)), (min(min(x, y), fold((max(c0, c1) - c2))) + c2), (((c2 <= 0) && (c1 <= c0)) || (c0 <= c1))) ||
rewrite(min(max(min(x, c0), c1), max(y, min(x, c2))), max(min(x, c0), min(y, c1)), ((((c1 <= c0) || (c2 <= c1)) || ((c2 + -1) <= c0)) && (((c0 <= c2) && (min(c1, c2) <= c0)) && ((min((c2 + 1), c1) + -2) <= c0)))) ||
rewrite(min(max(min(x, c0), c1), max(min(x, c2), y)), max(min(x, c0), min(y, c1)), ((((c1 <= c0) || (c2 <= c1)) || ((c2 + -1) <= c0)) && (((c0 <= c2) && (min(c1, c2) <= c0)) && ((min((c2 + 1), c1) + -2) <= c0)))) ||
rewrite(min(max(min(x, c0), y), (min(x, c1) + c2)), (min(x, c1) + c2), ((c2 <= 0) && ((c1 + c2) <= c0))) ||
rewrite(min(max(min(x, y), c0), max(min(x, c1), c2)), max(min(min(x, y), c1), c0), (((((c0 <= c1) || (c2 <= c0)) && (min((c2 + -2), c1) <= c0)) || ((c2 + -1) <= c1)) && (((((c1 <= c0) || (c2 <= c1)) && (min((c2 + -2), c0) <= c1)) || ((c2 + -1) <= c0)) && ((c0 <= c2) && (c2 <= max(c0, c1)))))) ||
rewrite(min(select((x < c0), c1, c2), select((x < c3), c1, c2)), select((x < c3), c1, c2), (((c0 <= c3) || (c2 <= c1)) && ((c1 <= c2) || (c3 <= c0)))) ||

rewrite(min((min(x, y)*c0), ((y*c0) + c1)), (min(x, y)*c0), ((0 <= c1) && (1 <= c0))) ||
