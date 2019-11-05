rewrite((u + (x - (y + (z + (u + w))))), ((x - (w + z)) - y)) ||
rewrite((u + (x - (y + (z + (w + u))))), ((x - (w + z)) - y)) ||
rewrite((u + (x - (y + ((u + w) + z)))), ((x - (w + z)) - y)) ||
rewrite((u + (x - (y + ((w + u) + z)))), ((x - (w + z)) - y)) ||
rewrite((u + (x - ((z + (u + w)) + y))), ((x - (w + z)) - y)) ||
rewrite((u + (x - ((z + (w + u)) + y))), ((x - (w + z)) - y)) ||
rewrite((u + (x - (((u + w) + z) + y))), ((x - (w + z)) - y)) ||
rewrite((u + (x - (((w + u) + z) + y))), ((x - (w + z)) - y)) ||
rewrite((x + (w + (y - (x + z)))), ((w - z) + y)) ||
rewrite((x + (w + (y - (z + x)))), ((w - z) + y)) ||
rewrite((x + ((y - (x + z)) + w)), ((w - z) + y)) ||
rewrite((x + ((y - (z + x)) + w)), ((w - z) + y)) ||
rewrite((x + min(y, (c0 - max(x, c1)))), min((min(y, fold((c0 - c1))) + x), c0)) ||
rewrite((x + min((c0 - max(x, c1)), y)), min((min(y, fold((c0 - c1))) + x), c0)) ||
rewrite((y + (w + min(z, (x - y)))), (min((y + z), x) + w)) ||
rewrite((y + (w + min((x - y), z))), (min((y + z), x) + w)) ||
rewrite((y + (min(z, (x - y)) + w)), (min((y + z), x) + w)) ||
rewrite((y + (min((x - y), z) + w)), (min((y + z), x) + w)) ||
rewrite((y + (min(((x - y)*c0), c1)/c0)), min((y + 0), x), (((-1 <= (c0 + c1)) && (0 <= c1)) && ((max(c1, 0) + 1) <= c0))) ||
rewrite((y + min(w, ((x - y) - z))), min((x - z), (w + y))) ||
rewrite((y + min(((x - y) - z), w)), min((x - z), (w + y))) ||
rewrite((y + max(w, (z + (x - y)))), max((x + z), (w + y))) ||
rewrite((y + max(w, ((x - y) + z))), max((x + z), (w + y))) ||
rewrite((y + max((z + (x - y)), w)), max((x + z), (w + y))) ||
rewrite((y + max(((x - y) + z), w)), max((x + z), (w + y))) ||
rewrite((z + (z*min(y, (x + -1)))), (min((y + 1), x)*z)) ||
rewrite((z + (z*min((x + -1), y))), (min((y + 1), x)*z)) ||
rewrite((z + (min(y, (x + -1))*z)), (min((y + 1), x)*z)) ||
rewrite((z + (min((x + -1), y)*z)), (min((y + 1), x)*z)) ||
rewrite((z + min(w, min(x, ((y - z) + c0)))), min((y + c0), (min(w, x) + z))) ||
rewrite((z + min(w, min(((y - z) + c0), x))), min((y + c0), (min(w, x) + z))) ||
rewrite((z + min(min(x, ((y - z) + c0)), w)), min((y + c0), (min(w, x) + z))) ||
rewrite((z + min(min(((y - z) + c0), x), w)), min((y + c0), (min(w, x) + z))) ||
rewrite((z + max(x, (y - (w + z)))), max((y - w), (x + z))) ||
rewrite((z + max(x, (y - (z + w)))), max((y - w), (x + z))) ||
rewrite((z + max((y - (w + z)), x)), max((y - w), (x + z))) ||
rewrite((z + max((y - (z + w)), x)), max((y - w), (x + z))) ||
rewrite(((w + (x - (y + z))) + (u + z)), ((u - y) + (w + x))) ||
rewrite(((w + (x - (y + z))) + (z + u)), ((u - y) + (w + x))) ||
rewrite(((w + (x - (z + y))) + (u + z)), ((u - y) + (w + x))) ||
rewrite(((w + (x - (z + y))) + (z + u)), ((u - y) + (w + x))) ||
rewrite(((w + (y - (x + z))) + x), ((w - z) + y)) ||
rewrite(((w + (y - (z + x))) + x), ((w - z) + y)) ||
rewrite(((w + min(z, (x - y))) + y), (min((y + z), x) + w)) ||
rewrite(((w + min((x - y), z)) + y), (min((y + z), x) + w)) ||
rewrite((((x - (y + z)) + w) + (u + z)), ((u - y) + (w + x))) ||
rewrite((((x - (y + z)) + w) + (z + u)), ((u - y) + (w + x))) ||
rewrite((((x - (z + y)) + w) + (u + z)), ((u - y) + (w + x))) ||
rewrite((((x - (z + y)) + w) + (z + u)), ((u - y) + (w + x))) ||
rewrite((((y - (x + z)) + w) + x), ((w - z) + y)) ||
rewrite((((y - (z + x)) + w) + x), ((w - z) + y)) ||
rewrite(((((x + c0)*y) + (x*c1)) + c2), ((y + c1)*(x + c0)), (((((c0 == 0) || (c2 == 0)) || (1 <= c1)) || (c1 <= -1)) && ((((1 <= c0) || (c2 == 0)) || (c0 <= -1)) && ((c0*c1) == c2)))) ||
rewrite(((min(z, (x - y)) + w) + y), (min((y + z), x) + w)) ||
rewrite(((min((x - y), z) + w) + y), (min((y + z), x) + w)) ||
rewrite(((x - y) + (z + (w + y))), ((x + z) + w)) ||
rewrite(((x - y) + (z + (y + w))), ((x + z) + w)) ||
rewrite(((x - y) + ((w + y) + z)), ((x + z) + w)) ||
rewrite(((x - y) + ((y + w) + z)), ((x + z) + w)) ||
rewrite(((x - (u + (y + (w + z)))) + (w + z)), (x - (u + y))) ||
rewrite(((x - (u + (y + (w + z)))) + (z + w)), (x - (u + y))) ||
rewrite(((x - (u + (y + (z + w)))) + (w + z)), (x - (u + y))) ||
rewrite(((x - (u + (y + (z + w)))) + (z + w)), (x - (u + y))) ||
rewrite(((x - (u + ((w + z) + y))) + (w + z)), (x - (u + y))) ||
rewrite(((x - (u + ((w + z) + y))) + (z + w)), (x - (u + y))) ||
rewrite(((x - (u + ((z + w) + y))) + (w + z)), (x - (u + y))) ||
rewrite(((x - (u + ((z + w) + y))) + (z + w)), (x - (u + y))) ||
rewrite(((x - (y + z)) + (w + z)), ((w - y) + x)) ||
rewrite(((x - (y + z)) + (z + w)), ((w - y) + x)) ||
rewrite(((x - (y + (z + (u + w)))) + u), ((x - (w + z)) - y)) ||
rewrite(((x - (y + (z + (w + u)))) + u), ((x - (w + z)) - y)) ||
rewrite(((x - (y + ((u + w) + z))) + u), ((x - (w + z)) - y)) ||
rewrite(((x - (y + ((w + u) + z))) + u), ((x - (w + z)) - y)) ||
rewrite(((x - (z + y)) + (w + z)), ((w - y) + x)) ||
rewrite(((x - (z + y)) + (z + w)), ((w - y) + x)) ||
rewrite(((x - ((y + (w + z)) + u)) + (w + z)), (x - (u + y))) ||
rewrite(((x - ((y + (w + z)) + u)) + (z + w)), (x - (u + y))) ||
rewrite(((x - ((y + (z + w)) + u)) + (w + z)), (x - (u + y))) ||
rewrite(((x - ((y + (z + w)) + u)) + (z + w)), (x - (u + y))) ||
rewrite(((x - ((z + (u + w)) + y)) + u), ((x - (w + z)) - y)) ||
rewrite(((x - ((z + (w + u)) + y)) + u), ((x - (w + z)) - y)) ||
rewrite(((x - (((u + w) + z) + y)) + u), ((x - (w + z)) - y)) ||
rewrite(((x - (((w + u) + z) + y)) + u), ((x - (w + z)) - y)) ||
rewrite(((x - (((w + z) + y) + u)) + (w + z)), (x - (u + y))) ||
rewrite(((x - (((w + z) + y) + u)) + (z + w)), (x - (u + y))) ||
rewrite(((x - (((z + w) + y) + u)) + (w + z)), (x - (u + y))) ||
rewrite(((x - (((z + w) + y) + u)) + (z + w)), (x - (u + y))) ||
rewrite((((x - (y + z)) - w) + (u + y)), ((u - (w + z)) + x)) ||
rewrite((((x - (y + z)) - w) + (y + u)), ((u - (w + z)) + x)) ||
rewrite((((x - (z + y)) - w) + (u + y)), ((u - (w + z)) + x)) ||
rewrite((((x - (z + y)) - w) + (y + u)), ((u - (w + z)) + x)) ||
rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((c1*c2) == c0)) ||
rewrite(((x*y) + (z - (w + (y*(u + x))))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - (w + (y*(x + u))))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - (w + ((u + x)*y)))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - (w + ((x + u)*y)))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - ((y*(u + x)) + w))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - ((y*(x + u)) + w))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - (((u + x)*y) + w))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - (((x + u)*y) + w))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - (w*y))), (((x - w)*y) + z)) ||
rewrite(((x*y) + (z - (y*w))), (((x - w)*y) + z)) ||
rewrite(((x*y) + ((y*z) - w)), (((x + z)*y) - w)) ||
rewrite(((x*y) + ((z*y) - w)), (((x + z)*y) - w)) ||
rewrite(((y*x) + (z - (w + (y*(u + x))))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - (w + (y*(x + u))))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - (w + ((u + x)*y)))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - (w + ((x + u)*y)))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - ((y*(u + x)) + w))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - ((y*(x + u)) + w))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - (((u + x)*y) + w))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - (((x + u)*y) + w))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - (w*y))), (((x - w)*y) + z)) ||
rewrite(((y*x) + (z - (y*w))), (((x - w)*y) + z)) ||
rewrite(((y*x) + ((y*z) - w)), (((x + z)*y) - w)) ||
rewrite(((y*x) + ((z*y) - w)), (((x + z)*y) - w)) ||
rewrite(((z*min(y, (x + -1))) + z), (min((y + 1), x)*z)) ||
rewrite(((z*min((x + -1), y)) + z), (min((y + 1), x)*z)) ||
rewrite((((x - (y*c0))*c1) + (z + (y*c2))), ((x*c1) + z), ((c0*c1) == c2)) ||
rewrite((((x - (y*c0))*c1) + ((y*c2) + z)), ((x*c1) + z), ((c0*c1) == c2)) ||
rewrite(((min(y, (x + -1))*z) + z), (min((y + 1), x)*z)) ||
rewrite(((min((x + -1), y)*z) + z), (min((y + 1), x)*z)) ||
rewrite(((min(((x - y)*c0), c1)/c0) + y), min((y + 0), x), (((-1 <= (c0 + c1)) && (0 <= c1)) && ((max(c1, 0) + 1) <= c0))) ||
rewrite((min(w, ((x - y) - z)) + y), min((x - z), (w + y))) ||
rewrite((min(w, min(x, ((y - z) + c0))) + z), min((y + c0), (min(w, x) + z))) ||
rewrite((min(w, min(((y - z) + c0), x)) + z), min((y + c0), (min(w, x) + z))) ||
rewrite((min(x, (y - z)) + (w + z)), (min((x + z), y) + w)) ||
rewrite((min(x, (y - z)) + (z + w)), (min((x + z), y) + w)) ||
rewrite((min(y, (c0 - max(x, c1))) + x), min((min(y, fold((c0 - c1))) + x), c0)) ||
rewrite((min((min(min(min(x, y), z), c0) + c1), y) + c1), (min(min(min(x, z), y), c0) + fold((c1*2))), (c1 <= max((min(c1, 0)*2), -1))) ||
rewrite((min((c0 - max(x, c1)), y) + x), min((min(y, fold((c0 - c1))) + x), c0)) ||
rewrite((min((y - z), x) + (w + z)), (min((x + z), y) + w)) ||
rewrite((min((y - z), x) + (z + w)), (min((x + z), y) + w)) ||
rewrite((min(((x - y) - z), w) + y), min((x - z), (w + y))) ||
rewrite((min(((x - y)*z), c0) + (y*z)), min((x*z), ((y*z) + c0))) ||
rewrite((min(((x - y)*z), c0) + (z*y)), min((x*z), ((y*z) + c0))) ||
rewrite((min(((x - (y*c0))*c0), c1) + (y*c2)), min((x*c0), ((y*c2) + c1)), ((1 <= c2) && ((c0*c0) == c2))) ||
rewrite((min(((x - (y*c0))*c1), c2) + (y*c3)), min((x*c1), ((y*c3) + c2)), (((((((1 <= max(c1, c3)) || (c0 <= -1)) || (c2 <= 2)) || (c3 <= -1)) || ((c1 + c2) <= 0)) || ((c1 + c2) <= 1)) && (((((((0 <= c2) || (1 <= c0)) || (1 <= c1)) || (1 <= c3)) || (c0 <= -1)) || ((c1 + 1) <= c2)) && (((((1 <= max(c1, c3)) || (c0 <= -1)) || (c2 <= 1)) || (c3 <= -1)) && (((1 <= max(max(c0, c1), c3)) || (c2 <= 1)) && ((c0*c1) == c3)))))) ||
rewrite((min(min(x, ((y - z) + c0)), w) + z), min((y + c0), (min(w, x) + z))) ||
rewrite((min(min((x + c0), (y + z)), y) + c1), min(((min(z, 0) + y) + c1), x), ((c0 + c1) == 0)) ||
rewrite((min(min((x + c0), (y + z)), z) + c1), min(((min(y, 0) + z) + c1), x), ((c0 + c1) == 0)) ||
rewrite((min(min((x + y), (z + c0)), y) + c1), min(((min(x, 0) + y) + c1), z), ((c0 + c1) == 0)) ||
rewrite((min(min(((y - z) + c0), x), w) + z), min((y + c0), (min(w, x) + z))) ||
rewrite((min(select((x < y), c0, 0), select((x < z), c0, 0)) + c2), select((x < max(y, z)), 0, c2), (((c0 <= -1) || (c2 == 0)) && (((max(c2, 0) + c0) <= 0) && ((c0 + c2) == 0)))) ||
rewrite((max(w, (z + (x - y))) + y), max((x + z), (w + y))) ||
rewrite((max(w, ((x - y) + z)) + y), max((x + z), (w + y))) ||
rewrite((max(x, c0) + max(min(x, c0), c1)), (max(x, c1) + c0), (c1 <= c0)) ||
rewrite((max(x, y) + (z + min(x, y))), ((y + z) + x)) ||
rewrite((max(x, y) + (z + min(y, x))), ((y + z) + x)) ||
rewrite((max(x, y) + (min(x, y) + z)), ((y + z) + x)) ||
rewrite((max(x, y) + (min(y, x) + z)), ((y + z) + x)) ||
rewrite((max(x, (y - (w + z))) + z), max((y - w), (x + z))) ||
rewrite((max(x, (y - (z + w))) + z), max((y - w), (x + z))) ||
rewrite((max(y, x) + (z + min(x, y))), ((y + z) + x)) ||
rewrite((max(y, x) + (z + min(y, x))), ((y + z) + x)) ||
rewrite((max(y, x) + (min(x, y) + z)), ((y + z) + x)) ||
rewrite((max(y, x) + (min(y, x) + z)), ((y + z) + x)) ||
rewrite((max((z + (x - y)), w) + y), max((x + z), (w + y))) ||
rewrite((max(((x - y) + z), w) + y), max((x + z), (w + y))) ||
rewrite((max((y - (w + z)), x) + z), max((y - w), (x + z))) ||
rewrite((max((y - (z + w)), x) + z), max((y - w), (x + z))) ||
rewrite((max((min((x + c0), y) - min(x, c1)), c0) + c2), max(min((y + fold((c2 - c1))), x), c1), ((c0 + c2) == c1)) ||
