rewrite((x - min(y, ((x + z) + c0))), max((x - y), (fold((0 - c0)) - z))) ||
rewrite((x - min(z, min(y, (x + c0)))), max((x - min(y, z)), fold((0 - c0)))) ||
rewrite((x - min(z, min((x + c0), y))), max((x - min(y, z)), fold((0 - c0)))) ||
rewrite((x - min(((x + z) + c0), y)), max((x - y), (fold((0 - c0)) - z))) ||
rewrite((x - min(min(y, (x + c0)), z)), max((x - min(y, z)), fold((0 - c0)))) ||
rewrite((x - min(min((x + c0), y), z)), max((x - min(y, z)), fold((0 - c0)))) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (w + min(x, (y + z)))))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (w + min(x, (z + y)))))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (w + min((y + z), x))))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (w + min((z + y), x))))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (min(x, (y + z)) + w)))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (min(x, (z + y)) + w)))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (min((y + z), x) + w)))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (min((z + y), x) + w)))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + ((w + min(x, (y + z))) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + ((w + min(x, (z + y))) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + ((w + min((y + z), x)) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + ((w + min((z + y), x)) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + ((min(x, (y + z)) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + ((min(x, (z + y)) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + ((min((y + z), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (u + ((min((z + y), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - ((v + (w + min(x, (y + z)))) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - ((v + (w + min(x, (z + y)))) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - ((v + (w + min((y + z), x))) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - ((v + (w + min((z + y), x))) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - ((v + (min(x, (y + z)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - ((v + (min(x, (z + y)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - ((v + (min((y + z), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - ((v + (min((z + y), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (((w + min(x, (y + z))) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (((w + min(x, (z + y))) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (((w + min((y + z), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (((w + min((z + y), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (((min(x, (y + z)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (((min(x, (z + y)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (((min((y + z), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (y + z))) - (((min((z + y), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (w + min(x, (y + z)))))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (w + min(x, (z + y)))))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (w + min((y + z), x))))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (w + min((z + y), x))))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (min(x, (y + z)) + w)))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (min(x, (z + y)) + w)))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (min((y + z), x) + w)))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (min((z + y), x) + w)))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + ((w + min(x, (y + z))) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + ((w + min(x, (z + y))) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + ((w + min((y + z), x)) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + ((w + min((z + y), x)) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + ((min(x, (y + z)) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + ((min(x, (z + y)) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + ((min((y + z), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (u + ((min((z + y), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - ((v + (w + min(x, (y + z)))) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - ((v + (w + min(x, (z + y)))) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - ((v + (w + min((y + z), x))) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - ((v + (w + min((z + y), x))) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - ((v + (min(x, (y + z)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - ((v + (min(x, (z + y)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - ((v + (min((y + z), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - ((v + (min((z + y), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (((w + min(x, (y + z))) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (((w + min(x, (z + y))) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (((w + min((y + z), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (((w + min((z + y), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (((min(x, (y + z)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (((min(x, (z + y)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (((min((y + z), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min(x, (z + y))) - (((min((z + y), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + (v + (w + min(x, (y + z)))))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + (v + (w + min(x, (z + y)))))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + (v + (w + min((y + z), x))))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + (v + (w + min((z + y), x))))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + (v + (min(x, (y + z)) + w)))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + (v + (min(x, (z + y)) + w)))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + (v + (min((y + z), x) + w)))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + (v + (min((z + y), x) + w)))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + ((w + min(x, (y + z))) + v))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + ((w + min(x, (z + y))) + v))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + ((w + min((y + z), x)) + v))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + ((w + min((z + y), x)) + v))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + ((min(x, (y + z)) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + ((min(x, (z + y)) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + ((min((y + z), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (u + ((min((z + y), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - ((v + (w + min(x, (y + z)))) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - ((v + (w + min(x, (z + y)))) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - ((v + (w + min((y + z), x))) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - ((v + (w + min((z + y), x))) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - ((v + (min(x, (y + z)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - ((v + (min(x, (z + y)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - ((v + (min((y + z), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - ((v + (min((z + y), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (((w + min(x, (y + z))) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (((w + min(x, (z + y))) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (((w + min((y + z), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (((w + min((z + y), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (((min(x, (y + z)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (((min(x, (z + y)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (((min((y + z), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((y + z), x)) - (((min((z + y), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + (v + (w + min(x, (y + z)))))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + (v + (w + min(x, (z + y)))))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + (v + (w + min((y + z), x))))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + (v + (w + min((z + y), x))))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + (v + (min(x, (y + z)) + w)))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + (v + (min(x, (z + y)) + w)))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + (v + (min((y + z), x) + w)))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + (v + (min((z + y), x) + w)))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + ((w + min(x, (y + z))) + v))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + ((w + min(x, (z + y))) + v))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + ((w + min((y + z), x)) + v))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + ((w + min((z + y), x)) + v))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + ((min(x, (y + z)) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + ((min(x, (z + y)) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + ((min((y + z), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (u + ((min((z + y), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - ((v + (w + min(x, (y + z)))) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - ((v + (w + min(x, (z + y)))) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - ((v + (w + min((y + z), x))) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - ((v + (w + min((z + y), x))) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - ((v + (min(x, (y + z)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - ((v + (min(x, (z + y)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - ((v + (min((y + z), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - ((v + (min((z + y), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (((w + min(x, (y + z))) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (((w + min(x, (z + y))) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (((w + min((y + z), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (((w + min((z + y), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (((min(x, (y + z)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (((min(x, (z + y)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (((min((y + z), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((w + min((z + y), x)) - (((min((z + y), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((x + (y - (z*c0))) - (z*c0)), ((x + y) - (z*fold(max((c0*2), 1)))), (1 <= c0)) ||
rewrite(((y + (x*c0)) - (((x*c1) + z)*c2)), (y - (z*c2)), ((((1 <= max(c1, c2)) || (c0 <= -1)) || (c2 <= -1)) && ((((1 <= max(c1, c2)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= max(c0, c2)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= c2) || (c1 <= -1)) || (c2 <= -1)) && ((1 <= max(max(c0, c1), c2)) && ((c1*c2) == c0))))))) ||
rewrite(((z + (x*y)) - (w*y)), (((x - w)*y) + z)) ||
rewrite(((z + (x*y)) - (y*w)), (((x - w)*y) + z)) ||
rewrite(((z + (y*x)) - (w*y)), (((x - w)*y) + z)) ||
rewrite(((z + (y*x)) - (y*w)), (((x - w)*y) + z)) ||
rewrite((((y - (z*c0)) + x) - (z*c0)), ((x + y) - (z*fold(max((c0*2), 1)))), (1 <= c0)) ||
rewrite((((x*c0) + y) - (((x*c1) + z)*c2)), (y - (z*c2)), ((((1 <= max(c1, c2)) || (c0 <= -1)) || (c2 <= -1)) && ((((1 <= max(c1, c2)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= max(c0, c2)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= c2) || (c1 <= -1)) || (c2 <= -1)) && ((1 <= max(max(c0, c1), c2)) && ((c1*c2) == c0))))))) ||
rewrite((((x*y) + z) - (w*y)), (((x - w)*y) + z)) ||
rewrite((((x*y) + z) - (y*w)), (((x - w)*y) + z)) ||
rewrite((((y*x) + z) - (w*y)), (((x - w)*y) + z)) ||
rewrite((((y*x) + z) - (y*w)), (((x - w)*y) + z)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (w + min(x, (y + z)))))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (w + min(x, (z + y)))))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (w + min((y + z), x))))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (w + min((z + y), x))))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (min(x, (y + z)) + w)))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (min(x, (z + y)) + w)))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (min((y + z), x) + w)))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (min((z + y), x) + w)))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + ((w + min(x, (y + z))) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + ((w + min(x, (z + y))) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + ((w + min((y + z), x)) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + ((w + min((z + y), x)) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + ((min(x, (y + z)) + w) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + ((min(x, (z + y)) + w) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + ((min((y + z), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (u + ((min((z + y), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - ((v + (w + min(x, (y + z)))) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - ((v + (w + min(x, (z + y)))) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - ((v + (w + min((y + z), x))) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - ((v + (w + min((z + y), x))) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - ((v + (min(x, (y + z)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - ((v + (min(x, (z + y)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - ((v + (min((y + z), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - ((v + (min((z + y), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (((w + min(x, (y + z))) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (((w + min(x, (z + y))) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (((w + min((y + z), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (((w + min((z + y), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (((min(x, (y + z)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (((min(x, (z + y)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (((min((y + z), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (y + z)) + w) - (((min((z + y), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (w + min(x, (y + z)))))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (w + min(x, (z + y)))))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (w + min((y + z), x))))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (w + min((z + y), x))))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (min(x, (y + z)) + w)))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (min(x, (z + y)) + w)))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (min((y + z), x) + w)))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (min((z + y), x) + w)))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + ((w + min(x, (y + z))) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + ((w + min(x, (z + y))) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + ((w + min((y + z), x)) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + ((w + min((z + y), x)) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + ((min(x, (y + z)) + w) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + ((min(x, (z + y)) + w) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + ((min((y + z), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (u + ((min((z + y), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - ((v + (w + min(x, (y + z)))) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - ((v + (w + min(x, (z + y)))) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - ((v + (w + min((y + z), x))) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - ((v + (w + min((z + y), x))) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - ((v + (min(x, (y + z)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - ((v + (min(x, (z + y)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - ((v + (min((y + z), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - ((v + (min((z + y), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (((w + min(x, (y + z))) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (((w + min(x, (z + y))) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (((w + min((y + z), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (((w + min((z + y), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (((min(x, (y + z)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (((min(x, (z + y)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (((min((y + z), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min(x, (z + y)) + w) - (((min((z + y), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + (v + (w + min(x, (y + z)))))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + (v + (w + min(x, (z + y)))))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + (v + (w + min((y + z), x))))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + (v + (w + min((z + y), x))))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + (v + (min(x, (y + z)) + w)))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + (v + (min(x, (z + y)) + w)))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + (v + (min((y + z), x) + w)))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + (v + (min((z + y), x) + w)))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + ((w + min(x, (y + z))) + v))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + ((w + min(x, (z + y))) + v))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + ((w + min((y + z), x)) + v))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + ((w + min((z + y), x)) + v))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + ((min(x, (y + z)) + w) + v))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + ((min(x, (z + y)) + w) + v))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + ((min((y + z), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (u + ((min((z + y), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - ((v + (w + min(x, (y + z)))) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - ((v + (w + min(x, (z + y)))) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - ((v + (w + min((y + z), x))) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - ((v + (w + min((z + y), x))) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - ((v + (min(x, (y + z)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - ((v + (min(x, (z + y)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - ((v + (min((y + z), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - ((v + (min((z + y), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (((w + min(x, (y + z))) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (((w + min(x, (z + y))) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (((w + min((y + z), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (((w + min((z + y), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (((min(x, (y + z)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (((min(x, (z + y)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (((min((y + z), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((y + z), x) + w) - (((min((z + y), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + (v + (w + min(x, (y + z)))))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + (v + (w + min(x, (z + y)))))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + (v + (w + min((y + z), x))))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + (v + (w + min((z + y), x))))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + (v + (min(x, (y + z)) + w)))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + (v + (min(x, (z + y)) + w)))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + (v + (min((y + z), x) + w)))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + (v + (min((z + y), x) + w)))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + ((w + min(x, (y + z))) + v))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + ((w + min(x, (z + y))) + v))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + ((w + min((y + z), x)) + v))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + ((w + min((z + y), x)) + v))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + ((min(x, (y + z)) + w) + v))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + ((min(x, (z + y)) + w) + v))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + ((min((y + z), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (u + ((min((z + y), x) + w) + v))), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - ((v + (w + min(x, (y + z)))) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - ((v + (w + min(x, (z + y)))) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - ((v + (w + min((y + z), x))) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - ((v + (w + min((z + y), x))) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - ((v + (min(x, (y + z)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - ((v + (min(x, (z + y)) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - ((v + (min((y + z), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - ((v + (min((z + y), x) + w)) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (((w + min(x, (y + z))) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (((w + min(x, (z + y))) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (((w + min((y + z), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (((w + min((z + y), x)) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (((min(x, (y + z)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (((min(x, (z + y)) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (((min((y + z), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((min((z + y), x) + w) - (((min((z + y), x) + w) + v) + u)), ((0 - u) - v)) ||
rewrite(((max(z, (x + y)) - w) - y), (max((z - y), x) - w)) ||
rewrite(((max(z, (y + x)) - w) - y), (max((z - y), x) - w)) ||
rewrite(((max((x + y), z) - w) - y), (max((z - y), x) - w)) ||
rewrite(((max((y + x), z) - w) - y), (max((z - y), x) - w)) ||
rewrite(((x*y) - ((y*z)*c0)), ((x - (z*c0))*y)) ||
rewrite(((y*x) - ((y*z)*c0)), ((x - (z*c0))*y)) ||
rewrite((min(w, (z + (x + y))) - y), min((w - y), (x + z))) ||
rewrite((min(w, (z + (y + x))) - y), min((w - y), (x + z))) ||
rewrite((min(w, ((x + y) + z)) - y), min((w - y), (x + z))) ||
rewrite((min(w, ((y + x) + z)) - y), min((w - y), (x + z))) ||
rewrite((min(w, min(x, (y + z))) - y), min((min(w, x) - y), z)) ||
rewrite((min(w, min(x, (z + y))) - y), min((min(w, x) - y), z)) ||
rewrite((min(w, min((y + z), x)) - y), min((min(w, x) - y), z)) ||
rewrite((min(w, min((z + y), x)) - y), min((min(w, x) - y), z)) ||
rewrite((min((z + (x + y)), w) - y), min((w - y), (x + z))) ||
rewrite((min((z + (y + x)), w) - y), min((w - y), (x + z))) ||
rewrite((min(((x + y) + z), w) - y), min((w - y), (x + z))) ||
rewrite((min(((y + x) + z), w) - y), min((w - y), (x + z))) ||
rewrite((min(min(x, (y + z)), w) - y), min((min(w, x) - y), z)) ||
rewrite((min(min(x, (z + y)), w) - y), min((min(w, x) - y), z)) ||
rewrite((min(min((y + z), x), w) - y), min((min(w, x) - y), z)) ||
rewrite((min(min((z + y), x), w) - y), min((min(w, x) - y), z)) ||
rewrite((max(w, max(z, (x + y))) - x), max((max(w, z) - x), y)) ||
rewrite((max(w, max(z, (y + x))) - x), max((max(w, z) - x), y)) ||
rewrite((max(w, max((x + y), z)) - x), max((max(w, z) - x), y)) ||
rewrite((max(w, max((y + x), z)) - x), max((max(w, z) - x), y)) ||
rewrite((max(z, (x + y)) - (w + y)), (max((z - y), x) - w)) ||
rewrite((max(z, (x + y)) - (y + w)), (max((z - y), x) - w)) ||
rewrite((max(z, (y + x)) - (w + y)), (max((z - y), x) - w)) ||
rewrite((max(z, (y + x)) - (y + w)), (max((z - y), x) - w)) ||
rewrite((max(z, ((x + y) + c0)) - x), max((z - x), (y + c0))) ||
rewrite((max((x + y), z) - (w + y)), (max((z - y), x) - w)) ||
rewrite((max((x + y), z) - (y + w)), (max((z - y), x) - w)) ||
rewrite((max((y + x), z) - (w + y)), (max((z - y), x) - w)) ||
rewrite((max((y + x), z) - (y + w)), (max((z - y), x) - w)) ||
rewrite((max(((x + y) + c0), z) - x), max((z - x), (y + c0))) ||
rewrite((max(max(z, (x + y)), w) - x), max((max(w, z) - x), y)) ||
rewrite((max(max(z, (y + x)), w) - x), max((max(w, z) - x), y)) ||
rewrite((max(max((x + y), z), w) - x), max((max(w, z) - x), y)) ||
rewrite((max(max((y + x), z), w) - x), max((max(w, z) - x), y)) ||
