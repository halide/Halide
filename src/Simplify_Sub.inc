rewrite((0 - (min(x, 0)*c1)), (fold((0 - c1))*min(x, 0))) ||
rewrite((c0 - (y + ((x + c1)/c2))), (((fold(max((-1 - (((-1 - c0)*c2) + c1)), (((c0*c2) - (c1 + c2)) + -1))) - x)/c2) - y), ((((-2 <= c2) || (1 <= c0)) || (c0 <= -1)) && ((((1 <= c2) || (c0 == 0)) || (c2 <= -1)) && (((0 <= c0) || (c2 <= 3)) && ((c0 <= 0) || (c2 <= 2)))))) ||
rewrite((c0 - (((x + c1)/c2) + y)), (((fold(max((-1 - (((-1 - c0)*c2) + c1)), (((c0*c2) - (c1 + c2)) + -1))) - x)/c2) - y), ((((-2 <= c2) || (1 <= c0)) || (c0 <= -1)) && ((((1 <= c2) || (c0 == 0)) || (c2 <= -1)) && (((0 <= c0) || (c2 <= 3)) && ((c0 <= 0) || (c2 <= 2)))))) ||
rewrite((x - (w + select(y, z, (x + c0)))), (select(y, (x - z), fold((0 - c0))) - w)) ||
rewrite((x - (w + select(y, (x + c0), z))), (select(y, fold((0 - c0)), (x - z)) - w)) ||
rewrite((x - (y + (x + z))), (0 - (y + z))) ||
rewrite((x - (y + (z + x))), (0 - (y + z))) ||
rewrite((x - ((x + z) + y)), (0 - (z + y))) ||
rewrite((x - ((z + x) + y)), (0 - (z + y))) ||
rewrite((x - (select(y, z, (x + c0)) + w)), (select(y, (x - z), fold((0 - c0))) - w)) ||
rewrite((x - (select(y, (x + c0), z) + w)), (select(y, fold((0 - c0)), (x - z)) - w)) ||
rewrite((x - min(y, ((x + z) + c0))), max((x - y), (fold((0 - c0)) - z))) ||
rewrite((x - min(z, min(y, (x + c0)))), max((x - min(z, y)), fold((0 - c0)))) ||
rewrite((x - min(z, min((x + c0), y))), max((x - min(y, z)), fold((0 - c0)))) ||
rewrite((x - min(((x + z) + c0), y)), max((x - y), (fold((0 - c0)) - z))) ||
rewrite((x - min(min(y, (x + c0)), z)), max((x - min(y, z)), fold((0 - c0)))) ||
rewrite((x - min(min((x + c0), y), z)), max((x - min(y, z)), fold((0 - c0)))) ||
rewrite((x - select(y, z, (x + c0))), select(y, (x - z), fold((0 - c0)))) ||
rewrite((x - select(y, z, (x + (w + (u*c0))))), select(y, (x - z), ((fold((0 - c0))*u) - w))) ||
rewrite((x - select(y, z, (x + ((u*c0) + w)))), select(y, (x - z), ((fold((0 - c0))*u) - w))) ||
rewrite((x - select(y, z, ((w + x) + c0))), select(y, (x - z), (fold((0 - c0)) - w))) ||
rewrite((x - select(y, z, ((w + (u*c0)) + x))), select(y, (x - z), ((fold((0 - c0))*u) - w))) ||
rewrite((x - select(y, z, ((x + w) + c0))), select(y, (x - z), (fold((0 - c0)) - w))) ||
rewrite((x - select(y, z, (((u*c0) + w) + x))), select(y, (x - z), ((fold((0 - c0))*u) - w))) ||
rewrite((x - select(y, (x + c0), z)), select(y, fold((0 - c0)), (x - z))) ||
rewrite((x - select(y, ((x + z) + c0), w)), select(y, (fold((0 - c0)) - z), (x - w))) ||
rewrite((x - select(y, ((z + x) + c0), w)), select(y, (fold((0 - c0)) - z), (x - w))) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (w + min(x, (z + y)))))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (w + min((y + z), x))))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (w + min((z + y), x))))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (min(x, (y + z)) + w)))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (min(x, (z + y)) + w)))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (min((y + z), x) + w)))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + (v + (min((z + y), x) + w)))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + ((w + min(x, (z + y))) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + ((w + min((y + z), x)) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + ((w + min((z + y), x)) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + ((min(x, (y + z)) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + ((min(x, (z + y)) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + ((min((y + z), x) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - (u + ((min((z + y), x) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (y + z))) - ((v + (w + min(x, (z + y)))) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - ((v + (w + min((y + z), x))) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - ((v + (w + min((z + y), x))) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - ((v + (min(x, (y + z)) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - ((v + (min(x, (z + y)) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - ((v + (min((y + z), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - ((v + (min((z + y), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - (((w + min(x, (z + y))) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - (((w + min((y + z), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - (((w + min((z + y), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - (((min(x, (y + z)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - (((min(x, (z + y)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - (((min((y + z), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (y + z))) - (((min((z + y), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (w + min(x, (y + z)))))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (w + min((y + z), x))))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (w + min((z + y), x))))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (min(x, (y + z)) + w)))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (min(x, (z + y)) + w)))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (min((y + z), x) + w)))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + (v + (min((z + y), x) + w)))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + ((w + min(x, (y + z))) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + ((w + min((y + z), x)) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + ((w + min((z + y), x)) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + ((min(x, (y + z)) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + ((min(x, (z + y)) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + ((min((y + z), x) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - (u + ((min((z + y), x) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min(x, (z + y))) - ((v + (w + min(x, (y + z)))) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - ((v + (w + min((y + z), x))) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - ((v + (w + min((z + y), x))) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - ((v + (min(x, (y + z)) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - ((v + (min(x, (z + y)) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - ((v + (min((y + z), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - ((v + (min((z + y), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - (((w + min(x, (y + z))) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - (((w + min((y + z), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - (((w + min((z + y), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - (((min(x, (y + z)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - (((min(x, (z + y)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - (((min((y + z), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min(x, (z + y))) - (((min((z + y), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - (u + (v + (w + min(x, (y + z)))))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + (v + (w + min(x, (z + y)))))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + (v + (w + min((z + y), x))))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + (v + (min(x, (y + z)) + w)))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + (v + (min(x, (z + y)) + w)))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + (v + (min((y + z), x) + w)))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + (v + (min((z + y), x) + w)))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + ((w + min(x, (y + z))) + v))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + ((w + min(x, (z + y))) + v))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + ((w + min((z + y), x)) + v))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + ((min(x, (y + z)) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + ((min(x, (z + y)) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + ((min((y + z), x) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - (u + ((min((z + y), x) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min((y + z), x)) - ((v + (w + min(x, (y + z)))) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - ((v + (w + min(x, (z + y)))) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - ((v + (w + min((z + y), x))) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - ((v + (min(x, (y + z)) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - ((v + (min(x, (z + y)) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - ((v + (min((y + z), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - ((v + (min((z + y), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - (((w + min(x, (y + z))) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - (((w + min(x, (z + y))) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - (((w + min((z + y), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - (((min(x, (y + z)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - (((min(x, (z + y)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - (((min((y + z), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((y + z), x)) - (((min((z + y), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - (u + (v + (w + min(x, (y + z)))))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + (v + (w + min(x, (z + y)))))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + (v + (w + min((y + z), x))))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + (v + (min(x, (y + z)) + w)))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + (v + (min(x, (z + y)) + w)))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + (v + (min((y + z), x) + w)))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + (v + (min((z + y), x) + w)))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + ((w + min(x, (y + z))) + v))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + ((w + min(x, (z + y))) + v))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + ((w + min((y + z), x)) + v))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + ((min(x, (y + z)) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + ((min(x, (z + y)) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + ((min((y + z), x) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - (u + ((min((z + y), x) + w) + v))), (0 - (u + v))) ||
rewrite(((w + min((z + y), x)) - ((v + (w + min(x, (y + z)))) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - ((v + (w + min(x, (z + y)))) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - ((v + (w + min((y + z), x))) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - ((v + (min(x, (y + z)) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - ((v + (min(x, (z + y)) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - ((v + (min((y + z), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - ((v + (min((z + y), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - (((w + min(x, (y + z))) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - (((w + min(x, (z + y))) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - (((w + min((y + z), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - (((min(x, (y + z)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - (((min(x, (z + y)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - (((min((y + z), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((w + min((z + y), x)) - (((min((z + y), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((x + y) - (w + (x + z))), (y - (w + z))) ||
rewrite(((x + y) - (w + (y + z))), (x - (w + z))) ||
rewrite(((x + y) - (w + (z + x))), (y - (w + z))) ||
rewrite(((x + y) - (w + (z + y))), (x - (w + z))) ||
rewrite(((x + y) - (z + (w + x))), (y - (z + w))) ||
rewrite(((x + y) - (z + (w + y))), (x - (z + w))) ||
rewrite(((x + y) - (z + (x + w))), (y - (z + w))) ||
rewrite(((x + y) - (z + (y + w))), (x - (z + w))) ||
rewrite(((x + y) - ((w + x) + z)), (y - (w + z))) ||
rewrite(((x + y) - ((w + y) + z)), (x - (w + z))) ||
rewrite(((x + y) - ((x + w) + z)), (y - (w + z))) ||
rewrite(((x + y) - ((x + z) + w)), (y - (z + w))) ||
rewrite(((x + y) - ((y + w) + z)), (x - (w + z))) ||
rewrite(((x + y) - ((y + z) + w)), (x - (z + w))) ||
rewrite(((x + y) - ((z + x) + w)), (y - (z + w))) ||
rewrite(((x + y) - ((z + y) + w)), (x - (z + w))) ||
rewrite(((x + y) - min(u, (w + (x + z)))), (y - min((u - x), (w + z)))) ||
rewrite(((x + y) - min(u, (w + (z + x)))), (y - min((u - x), (z + w)))) ||
rewrite(((x + y) - min(u, ((x + z) + w))), (y - min((u - x), (z + w)))) ||
rewrite(((x + y) - min(u, ((z + x) + w))), (y - min((u - x), (z + w)))) ||
rewrite(((x + y) - min(w, (y + z))), (x - min(z, (w - y)))) ||
rewrite(((x + y) - min(w, (z + y))), (x - min(z, (w - y)))) ||
rewrite(((x + y) - min((w + (x + z)), u)), (y - min((w + z), (u - x)))) ||
rewrite(((x + y) - min((w + (z + x)), u)), (y - min((w + z), (u - x)))) ||
rewrite(((x + y) - min((y + z), w)), (x - min((w - y), z))) ||
rewrite(((x + y) - min((z + y), w)), (x - min((w - y), z))) ||
rewrite(((x + y) - min(((x + z) + w), u)), (y - min((w + z), (u - x)))) ||
rewrite(((x + y) - min(((z + x) + w), u)), (y - min((u - x), (z + w)))) ||
rewrite(((x + y) - select(z, w, (u + y))), (x - select(z, (w - y), u))) ||
rewrite(((x + y) - select(z, w, (y + u))), (x - select(z, (w - y), u))) ||
rewrite(((x + y) - select(z, (w + y), u)), (x - select(z, w, (u - y)))) ||
rewrite(((x + y) - select(z, (y + w), u)), (x - select(z, w, (u - y)))) ||
rewrite(((x + (y + z)) - (u + (w + z))), (x + (y - (u + w)))) ||
rewrite(((x + (y + z)) - (u + (z + w))), (x + (y - (u + w)))) ||
rewrite(((x + (y + z)) - (w + z)), (x + (y - w))) ||
rewrite(((x + (y + z)) - (z + w)), (x + (y - w))) ||
rewrite(((x + (y + z)) - ((w + z) + u)), ((x + y) - (w + u))) ||
rewrite(((x + (y + z)) - ((z + w) + u)), ((x + y) - (w + u))) ||
rewrite(((x + (y + (w + z))) - z), (x + (y + w))) ||
rewrite(((x + (y + (z + w))) - z), (x + (y + w))) ||
rewrite(((x + (z + y)) - (u + (w + z))), (x + (y - (u + w)))) ||
rewrite(((x + (z + y)) - (u + (z + w))), (x + (y - (u + w)))) ||
rewrite(((x + (z + y)) - (w + z)), (x + (y - w))) ||
rewrite(((x + (z + y)) - (z + w)), (x + (y - w))) ||
rewrite(((x + (z + y)) - ((w + z) + u)), ((x + y) - (w + u))) ||
rewrite(((x + (z + y)) - ((z + w) + u)), ((x + y) - (w + u))) ||
rewrite(((x + ((w + z) + y)) - z), (x + (w + y))) ||
rewrite(((x + ((z + w) + y)) - z), (x + (w + y))) ||
rewrite(((x + (y - (z*c0))) - (z*c0)), (x + (y - (fold(max((c0*2), 1))*z))), (1 <= c0)) ||
rewrite(((y + x) - (w + (x + z))), (y - (w + z))) ||
rewrite(((y + x) - (w + (y + z))), (x - (w + z))) ||
rewrite(((y + x) - (w + (z + x))), (y - (w + z))) ||
rewrite(((y + x) - (w + (z + y))), (x - (w + z))) ||
rewrite(((y + x) - (z + (w + x))), (y - (z + w))) ||
rewrite(((y + x) - (z + (w + y))), (x - (z + w))) ||
rewrite(((y + x) - (z + (x + w))), (y - (z + w))) ||
rewrite(((y + x) - (z + (y + w))), (x - (z + w))) ||
rewrite(((y + x) - ((w + x) + z)), (y - (w + z))) ||
rewrite(((y + x) - ((w + y) + z)), (x - (w + z))) ||
rewrite(((y + x) - ((x + w) + z)), (y - (w + z))) ||
rewrite(((y + x) - ((x + z) + w)), (y - (z + w))) ||
rewrite(((y + x) - ((y + w) + z)), (x - (w + z))) ||
rewrite(((y + x) - ((y + z) + w)), (x - (z + w))) ||
rewrite(((y + x) - ((z + x) + w)), (y - (z + w))) ||
rewrite(((y + x) - ((z + y) + w)), (x - (z + w))) ||
rewrite(((y + x) - min(u, (w + (x + z)))), (y - min((u - x), (w + z)))) ||
rewrite(((y + x) - min(u, (w + (z + x)))), (y - min((z + w), (u - x)))) ||
rewrite(((y + x) - min(u, ((x + z) + w))), (y - min((u - x), (z + w)))) ||
rewrite(((y + x) - min(u, ((z + x) + w))), (y - min((u - x), (z + w)))) ||
rewrite(((y + x) - min(w, (y + z))), (x - min(z, (w - y)))) ||
rewrite(((y + x) - min(w, (z + y))), (x - min(z, (w - y)))) ||
rewrite(((y + x) - min((w + (x + z)), u)), (y - min((w + z), (u - x)))) ||
rewrite(((y + x) - min((w + (z + x)), u)), (y - min((w + z), (u - x)))) ||
rewrite(((y + x) - min((y + z), w)), (x - min((w - y), z))) ||
rewrite(((y + x) - min((z + y), w)), (x - min((w - y), z))) ||
rewrite(((y + x) - min(((x + z) + w), u)), (y - min((w + z), (u - x)))) ||
rewrite(((y + x) - min(((z + x) + w), u)), (y - min((u - x), (w + z)))) ||
rewrite(((y + x) - select(z, w, (u + y))), (x - select(z, (w - y), u))) ||
rewrite(((y + x) - select(z, w, (y + u))), (x - select(z, (w - y), u))) ||
rewrite(((y + x) - select(z, (w + y), u)), (x - select(z, w, (u - y)))) ||
rewrite(((y + x) - select(z, (y + w), u)), (x - select(z, w, (u - y)))) ||
rewrite(((y + (x*c0)) - (((x*c1) + z)*c2)), (y - (z*c2)), ((((1 <= max(c1, c2)) || (c0 <= -1)) || (c2 <= -1)) && ((((1 <= max(c1, c2)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= max(c0, c2)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= c2) || (c1 <= -1)) || (c2 <= -1)) && ((1 <= max(max(c0, c1), c2)) && ((c1*c2) == c0))))))) ||
rewrite(((z + (x - y)) - x), (z - y)) ||
rewrite(((z + (x*y)) - (w*y)), (z + (y*(x - w)))) ||
rewrite(((z + (x*y)) - (y*w)), (z + (y*(x - w)))) ||
rewrite(((z + (y*x)) - (w*y)), (z + (y*(x - w)))) ||
rewrite(((z + (y*x)) - (y*w)), (z + (y*(x - w)))) ||
rewrite(((z + max(y, (x + c0))) - x), (z + max(c0, (y - x)))) ||
rewrite(((z + max((x + c0), y)) - x), (z + max((y - x), c0))) ||
rewrite((((y + z) + x) - (u + (w + z))), ((y + x) - (u + w))) ||
rewrite((((y + z) + x) - (u + (z + w))), ((y + x) - (u + w))) ||
rewrite((((y + z) + x) - (w + z)), ((y + x) - w)) ||
rewrite((((y + z) + x) - (z + w)), ((y + x) - w)) ||
rewrite((((y + z) + x) - ((w + z) + u)), ((y + x) - (w + u))) ||
rewrite((((y + z) + x) - ((z + w) + u)), ((y + x) - (w + u))) ||
rewrite((((y + (w + z)) + x) - z), (y + (w + x))) ||
rewrite((((y + (z + w)) + x) - z), (y + (w + x))) ||
rewrite((((z + y) + x) - (u + (w + z))), ((y + x) - (u + w))) ||
rewrite((((z + y) + x) - (u + (z + w))), ((y + x) - (u + w))) ||
rewrite((((z + y) + x) - (w + z)), ((y + x) - w)) ||
rewrite((((z + y) + x) - (z + w)), ((y + x) - w)) ||
rewrite((((z + y) + x) - ((w + z) + u)), ((y + x) - (w + u))) ||
rewrite((((z + y) + x) - ((z + w) + u)), ((y + x) - (w + u))) ||
rewrite(((((w + z) + y) + x) - z), ((w + y) + x)) ||
rewrite(((((z + w) + y) + x) - z), ((w + y) + x)) ||
rewrite((((x - y) + z) - x), (z - y)) ||
rewrite((((y - (z*c0)) + x) - (z*c0)), (y - ((z*fold(max((c0*2), 1))) - x)), (1 <= c0)) ||
rewrite((((x*c0) + y) - (((x*c1) + z)*c2)), (y - (z*c2)), ((((1 <= max(c1, c2)) || (c0 <= -1)) || (c2 <= -1)) && ((((1 <= max(c1, c2)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= max(c0, c2)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= c2) || (c1 <= -1)) || (c2 <= -1)) && ((1 <= max(max(c0, c1), c2)) && ((c1*c2) == c0))))))) ||
rewrite((((x*y) + z) - (w*y)), (z + ((x - w)*y))) ||
rewrite((((x*y) + z) - (y*w)), (z + ((x - w)*y))) ||
rewrite((((y*x) + z) - (w*y)), (z + ((x - w)*y))) ||
rewrite((((y*x) + z) - (y*w)), ((y*(x - w)) + z)) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (w + min(x, (y + z)))))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (w + min(x, (z + y)))))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (w + min((y + z), x))))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (w + min((z + y), x))))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (min(x, (z + y)) + w)))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (min((y + z), x) + w)))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + (v + (min((z + y), x) + w)))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + ((w + min(x, (y + z))) + v))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + ((w + min(x, (z + y))) + v))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + ((w + min((y + z), x)) + v))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + ((w + min((z + y), x)) + v))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + ((min(x, (z + y)) + w) + v))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + ((min((y + z), x) + w) + v))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - (u + ((min((z + y), x) + w) + v))), (0 - (u + v))) ||
rewrite(((min(x, (y + z)) + w) - ((v + (w + min(x, (y + z)))) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - ((v + (w + min(x, (z + y)))) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - ((v + (w + min((y + z), x))) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - ((v + (w + min((z + y), x))) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - ((v + (min(x, (z + y)) + w)) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - ((v + (min((y + z), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - ((v + (min((z + y), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - (((w + min(x, (y + z))) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - (((w + min(x, (z + y))) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - (((w + min((y + z), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - (((w + min((z + y), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - (((min(x, (z + y)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - (((min((y + z), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (y + z)) + w) - (((min((z + y), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (w + min(x, (y + z)))))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (w + min(x, (z + y)))))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (w + min((y + z), x))))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (w + min((z + y), x))))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (min(x, (y + z)) + w)))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (min((y + z), x) + w)))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + (v + (min((z + y), x) + w)))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + ((w + min(x, (y + z))) + v))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + ((w + min(x, (z + y))) + v))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + ((w + min((y + z), x)) + v))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + ((w + min((z + y), x)) + v))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + ((min(x, (y + z)) + w) + v))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + ((min((y + z), x) + w) + v))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - (u + ((min((z + y), x) + w) + v))), (0 - (u + v))) ||
rewrite(((min(x, (z + y)) + w) - ((v + (w + min(x, (y + z)))) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - ((v + (w + min(x, (z + y)))) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - ((v + (w + min((y + z), x))) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - ((v + (w + min((z + y), x))) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - ((v + (min(x, (y + z)) + w)) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - ((v + (min((y + z), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - ((v + (min((z + y), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - (((w + min(x, (y + z))) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - (((w + min(x, (z + y))) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - (((w + min((y + z), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - (((w + min((z + y), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - (((min(x, (y + z)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - (((min((y + z), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min(x, (z + y)) + w) - (((min((z + y), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - (u + (v + (w + min(x, (y + z)))))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + (v + (w + min(x, (z + y)))))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + (v + (w + min((y + z), x))))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + (v + (w + min((z + y), x))))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + (v + (min(x, (y + z)) + w)))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + (v + (min(x, (z + y)) + w)))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + (v + (min((z + y), x) + w)))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + ((w + min(x, (y + z))) + v))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + ((w + min(x, (z + y))) + v))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + ((w + min((y + z), x)) + v))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + ((w + min((z + y), x)) + v))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + ((min(x, (y + z)) + w) + v))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + ((min(x, (z + y)) + w) + v))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - (u + ((min((z + y), x) + w) + v))), (0 - (u + v))) ||
rewrite(((min((y + z), x) + w) - ((v + (w + min(x, (y + z)))) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - ((v + (w + min(x, (z + y)))) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - ((v + (w + min((y + z), x))) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - ((v + (w + min((z + y), x))) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - ((v + (min(x, (y + z)) + w)) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - ((v + (min(x, (z + y)) + w)) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - ((v + (min((z + y), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - (((w + min(x, (y + z))) + v) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - (((w + min(x, (z + y))) + v) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - (((w + min((y + z), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - (((w + min((z + y), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - (((min(x, (y + z)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - (((min(x, (z + y)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min((y + z), x) + w) - (((min((z + y), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - (u + (v + (w + min(x, (y + z)))))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + (v + (w + min(x, (z + y)))))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + (v + (w + min((y + z), x))))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + (v + (w + min((z + y), x))))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + (v + (min(x, (y + z)) + w)))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + (v + (min(x, (z + y)) + w)))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + (v + (min((y + z), x) + w)))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + ((w + min(x, (y + z))) + v))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + ((w + min(x, (z + y))) + v))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + ((w + min((y + z), x)) + v))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + ((w + min((z + y), x)) + v))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + ((min(x, (y + z)) + w) + v))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + ((min(x, (z + y)) + w) + v))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - (u + ((min((y + z), x) + w) + v))), (0 - (u + v))) ||
rewrite(((min((z + y), x) + w) - ((v + (w + min(x, (y + z)))) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - ((v + (w + min(x, (z + y)))) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - ((v + (w + min((y + z), x))) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - ((v + (w + min((z + y), x))) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - ((v + (min(x, (y + z)) + w)) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - ((v + (min(x, (z + y)) + w)) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - ((v + (min((y + z), x) + w)) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - (((w + min(x, (y + z))) + v) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - (((w + min(x, (z + y))) + v) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - (((w + min((y + z), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - (((w + min((z + y), x)) + v) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - (((min(x, (y + z)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - (((min(x, (z + y)) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((min((z + y), x) + w) - (((min((y + z), x) + w) + v) + u)), (0 - (v + u))) ||
rewrite(((max(y, (x + c0)) + z) - x), (max(c0, (y - x)) + z)) ||
rewrite(((max((x + c0), y) + z) - x), (max((y - x), c0) + z)) ||
rewrite(((x - y) - select(z, (w + x), x)), ((0 - y) - select(z, w, 0))) ||
rewrite(((x - y) - select(z, (x + w), x)), ((0 - y) - select(z, w, 0))) ||
rewrite((((x + y) - z) - y), (x - z)) ||
rewrite((((y + x) - z) - y), (x - z)) ||
rewrite((((x - y) - z) - x), ((0 - y) - z)) ||
rewrite(((((x + y) - z) - w) - y), ((x - z) - w)) ||
rewrite(((((y + x) - z) - w) - y), ((x - z) - w)) ||
rewrite(((max(z, (x + y)) - w) - y), (max(x, (z - y)) - w)) ||
rewrite(((max(z, (y + x)) - w) - y), (max(x, (z - y)) - w)) ||
rewrite(((max((x + y), z) - w) - y), (max((z - y), x) - w)) ||
rewrite(((max((y + x), z) - w) - y), (max((z - y), x) - w)) ||
rewrite(((x*y) - ((y*z)*c0)), (y*(x - (z*c0)))) ||
rewrite(((y*x) - ((y*z)*c0)), (y*(x - (c0*z)))) ||
rewrite(((y*(x + 1)) - (y + z)), ((y*x) - z)) ||
rewrite(((y*(x + 1)) - (z + y)), ((y*x) - z)) ||
rewrite(((z*(x*y)) - (w*x)), (((z*y) - w)*x)) ||
rewrite(((z*(x*y)) - (w*y)), (((x*z) - w)*y)) ||
rewrite(((z*(x*y)) - (x*w)), (((z*y) - w)*x)) ||
rewrite(((z*(x*y)) - (y*w)), (((x*z) - w)*y)) ||
rewrite(((z*(y*x)) - (w*x)), (((y*z) - w)*x)) ||
rewrite(((z*(y*x)) - (w*y)), (((z*x) - w)*y)) ||
rewrite(((z*(y*x)) - (x*w)), (x*((y*z) - w))) ||
rewrite(((z*(y*x)) - (y*w)), (((z*x) - w)*y)) ||
rewrite((((x + 1)*y) - (y + z)), ((x*y) - z)) ||
rewrite((((x + 1)*y) - (z + y)), ((x*y) - z)) ||
rewrite((((x*y)*z) - (w*x)), (((y*z) - w)*x)) ||
rewrite((((x*y)*z) - (w*y)), (((x*z) - w)*y)) ||
rewrite((((x*y)*z) - (x*w)), (((y*z) - w)*x)) ||
rewrite((((x*y)*z) - (y*w)), (((x*z) - w)*y)) ||
rewrite((((y*x)*z) - (w*x)), (((y*z) - w)*x)) ||
rewrite((((y*x)*z) - (w*y)), (((x*z) - w)*y)) ||
rewrite((((y*x)*z) - (x*w)), (((y*z) - w)*x)) ||
rewrite((((y*x)*z) - (y*w)), (((x*z) - w)*y)) ||
rewrite((min(w, (z + (x + y))) - y), min((z + x), (w - y))) ||
rewrite((min(w, (z + (y + x))) - y), min((z + x), (w - y))) ||
rewrite((min(w, ((x + y) + z)) - y), min((x + z), (w - y))) ||
rewrite((min(w, ((y + x) + z)) - y), min((z + x), (w - y))) ||
rewrite((min(w, min(x, (y + z))) - y), min(z, (min(x, w) - y))) ||
rewrite((min(w, min(x, (z + y))) - y), min(z, (min(x, w) - y))) ||
rewrite((min(w, min((y + z), x)) - y), min(z, (min(w, x) - y))) ||
rewrite((min(w, min((z + y), x)) - y), min(z, (min(w, x) - y))) ||
rewrite((min((z + (x + y)), w) - y), min((z + x), (w - y))) ||
rewrite((min((z + (y + x)), w) - y), min((z + x), (w - y))) ||
rewrite((min(((x + y) + z), w) - y), min((x + z), (w - y))) ||
rewrite((min(((y + x) + z), w) - y), min((x + z), (w - y))) ||
rewrite((min(min(x, (y + z)), w) - y), min((min(x, w) - y), z)) ||
rewrite((min(min(x, (z + y)), w) - y), min((min(x, w) - y), z)) ||
rewrite((min(min((y + z), x), w) - y), min(z, (min(x, w) - y))) ||
rewrite((min(min((z + y), x), w) - y), min((min(w, x) - y), z)) ||
rewrite((max(u, (w + (z + (x + y)))) - y), max((w + (z + x)), (u - y))) ||
rewrite((max(u, (w + (z + (y + x)))) - y), max((w + (z + x)), (u - y))) ||
rewrite((max(u, (w + ((x + y) + z))) - y), max((w + (x + z)), (u - y))) ||
rewrite((max(u, (w + ((y + x) + z))) - y), max((w + (z + x)), (u - y))) ||
rewrite((max(u, ((z + (x + y)) + w)) - y), max((z + (x + w)), (u - y))) ||
rewrite((max(u, ((z + (y + x)) + w)) - y), max((z + (w + x)), (u - y))) ||
rewrite((max(u, (((x + y) + z) + w)) - y), max(((x + w) + z), (u - y))) ||
rewrite((max(u, (((y + x) + z) + w)) - y), max(((w + x) + z), (u - y))) ||
rewrite((max(u, max(w, (z + (x + y)))) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(u, max(w, (z + (x + y)))) - y), max((max(w, u) - y), (x + z))) ||
rewrite((max(u, max(w, (z + (y + x)))) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(u, max(w, (z + (y + x)))) - y), max((max(w, u) - y), (x + z))) ||
rewrite((max(u, max(w, ((x + y) + z))) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(u, max(w, ((x + y) + z))) - y), max((max(w, u) - y), (x + z))) ||
rewrite((max(u, max(w, ((y + x) + z))) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(u, max(w, ((y + x) + z))) - y), max((max(w, u) - y), (x + z))) ||
rewrite((max(u, max(x, (w + (y + z)))) - z), max((max(x, u) - z), (w + y))) ||
rewrite((max(u, max(x, (w + (z + y)))) - z), max((max(x, u) - z), (w + y))) ||
rewrite((max(u, max(x, ((y + z) + w))) - z), max((max(x, u) - z), (w + y))) ||
rewrite((max(u, max(x, ((z + y) + w))) - z), max((max(x, u) - z), (w + y))) ||
rewrite((max(u, max((w + (y + z)), x)) - z), max((max(u, x) - z), (w + y))) ||
rewrite((max(u, max((w + (z + y)), x)) - z), max((w + y), (max(u, x) - z))) ||
rewrite((max(u, max((z + (x + y)), w)) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(u, max((z + (x + y)), w)) - y), max((max(u, w) - y), (z + x))) ||
rewrite((max(u, max((z + (y + x)), w)) - x), max((y + z), (max(u, w) - x))) ||
rewrite((max(u, max((z + (y + x)), w)) - y), max((x + z), (max(u, w) - y))) ||
rewrite((max(u, max(((x + y) + z), w)) - x), max((max(u, w) - x), (y + z))) ||
rewrite((max(u, max(((x + y) + z), w)) - y), max((max(u, w) - y), (x + z))) ||
rewrite((max(u, max(((y + x) + z), w)) - x), max((max(u, w) - x), (y + z))) ||
rewrite((max(u, max(((y + x) + z), w)) - y), max((max(u, w) - y), (x + z))) ||
rewrite((max(u, max(((y + z) + w), x)) - z), max((max(u, x) - z), (w + y))) ||
rewrite((max(u, max(((z + y) + w), x)) - z), max((max(u, x) - z), (w + y))) ||
rewrite((max(w, max(z, (x + y))) - x), max(y, (max(z, w) - x))) ||
rewrite((max(w, max(z, (y + x))) - x), max((max(z, w) - x), y)) ||
rewrite((max(w, max((x + y), z)) - x), max(y, (max(w, z) - x))) ||
rewrite((max(w, max((y + x), z)) - x), max(y, (max(w, z) - x))) ||
rewrite((max(x, ((y + z) + c0)) - z), max((y + c0), (x - z))) ||
rewrite((max(z, (x + y)) - (w + y)), (max(x, (z - y)) - w)) ||
rewrite((max(z, (x + y)) - (y + w)), (max(x, (z - y)) - w)) ||
rewrite((max(z, (y + x)) - (w + y)), (max(x, (z - y)) - w)) ||
rewrite((max(z, (y + x)) - (y + w)), (max(x, (z - y)) - w)) ||
rewrite((max(z, ((x + y) + c0)) - x), max((c0 + y), (z - x))) ||
rewrite((max((w + (z + (x + y))), u) - y), max((w + (z + x)), (u - y))) ||
rewrite((max((w + (z + (y + x))), u) - y), max((w + (z + x)), (u - y))) ||
rewrite((max((w + ((x + y) + z)), u) - y), max((w + (x + z)), (u - y))) ||
rewrite((max((w + ((y + x) + z)), u) - y), max((w + (x + z)), (u - y))) ||
rewrite((max((x + y), z) - (w + y)), (max((z - y), x) - w)) ||
rewrite((max((x + y), z) - (y + w)), (max((z - y), x) - w)) ||
rewrite((max((y + x), z) - (w + y)), (max((z - y), x) - w)) ||
rewrite((max((y + x), z) - (y + w)), (max((z - y), x) - w)) ||
rewrite((max(((x + y) + c0), z) - x), max((y + c0), (z - x))) ||
rewrite((max(((y + z) + c0), x) - z), max((y + c0), (x - z))) ||
rewrite((max(((z + (x + y)) + w), u) - y), max(((z + x) + w), (u - y))) ||
rewrite((max(((z + (y + x)) + w), u) - y), max(((z + x) + w), (u - y))) ||
rewrite((max((((x + y) + z) + w), u) - y), max(((x + z) + w), (u - y))) ||
rewrite((max((((y + x) + z) + w), u) - y), max(((x + z) + w), (u - y))) ||
rewrite((max(max(w, (z + (x + y))), u) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(max(w, (z + (x + y))), u) - y), max((max(w, u) - y), (x + z))) ||
rewrite((max(max(w, (z + (y + x))), u) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(max(w, (z + (y + x))), u) - y), max((x + z), (max(w, u) - y))) ||
rewrite((max(max(w, ((x + y) + z)), u) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(max(w, ((x + y) + z)), u) - y), max((max(w, u) - y), (x + z))) ||
rewrite((max(max(w, ((y + x) + z)), u) - x), max((max(w, u) - x), (y + z))) ||
rewrite((max(max(w, ((y + x) + z)), u) - y), max((max(w, u) - y), (x + z))) ||
rewrite((max(max(x, (w + (y + z))), u) - z), max((max(x, u) - z), (w + y))) ||
rewrite((max(max(x, (w + (z + y))), u) - z), max((max(x, u) - z), (w + y))) ||
rewrite((max(max(x, ((y + z) + w)), u) - z), max((max(x, u) - z), (w + y))) ||
rewrite((max(max(x, ((z + y) + w)), u) - z), max((max(x, u) - z), (w + y))) ||
rewrite((max(max(z, (x + y)), w) - x), max((max(z, w) - x), y)) ||
rewrite((max(max(z, (y + x)), w) - x), max((max(z, w) - x), y)) ||
rewrite((max(max((w + (y + z)), x), u) - z), max((w + y), (max(x, u) - z))) ||
rewrite((max(max((w + (z + y)), x), u) - z), max((w + y), (max(x, u) - z))) ||
rewrite((max(max((x + y), z), w) - x), max(y, (max(z, w) - x))) ||
rewrite((max(max((y + x), z), w) - x), max(y, (max(z, w) - x))) ||
rewrite((max(max((z + (x + y)), w), u) - x), max((z + y), (max(w, u) - x))) ||
rewrite((max(max((z + (x + y)), w), u) - y), max((z + x), (max(w, u) - y))) ||
rewrite((max(max((z + (y + x)), w), u) - x), max((z + y), (max(w, u) - x))) ||
rewrite((max(max((z + (y + x)), w), u) - y), max((z + x), (max(w, u) - y))) ||
rewrite((max(max(((x + y) + z), w), u) - x), max((z + y), (max(w, u) - x))) ||
rewrite((max(max(((x + y) + z), w), u) - y), max((x + z), (max(w, u) - y))) ||
rewrite((max(max(((y + x) + z), w), u) - x), max((y + z), (max(w, u) - x))) ||
rewrite((max(max(((y + x) + z), w), u) - y), max((z + x), (max(w, u) - y))) ||
rewrite((max(max(((y + z) + w), x), u) - z), max((y + w), (max(x, u) - z))) ||
rewrite((max(max(((z + y) + w), x), u) - z), max((w + y), (max(x, u) - z))) ||
