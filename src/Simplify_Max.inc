rewrite(max(x, (max(x, c0)*c1)), (max(x, c0)*c1), ((0 <= c0) && (1 <= c1))) ||
rewrite(max(x, min(z, min(x, y))), x) ||
rewrite(max(x, min(z, min(y, x))), x) ||
rewrite(max(x, min(min(x, y), z)), x) ||
rewrite(max(x, min(min(y, x), z)), x) ||
rewrite(max(x, max(w, min(z, max(x, y)))), max(x, max(w, min(z, y)))) ||
rewrite(max(x, max(w, min(z, max(y, x)))), max(x, max(w, min(z, y)))) ||
rewrite(max(x, max(w, min(max(x, y), z))), max(x, max(w, min(y, z)))) ||
rewrite(max(x, max(w, min(max(y, x), z))), max(x, max(w, min(y, z)))) ||
rewrite(max(x, max(y, (x + c0))), max(y, (x + c0)), (0 <= c0)) ||
rewrite(max(x, max(y, (x + c0))), max(y, x), (c0 <= 0)) ||
rewrite(max(x, max(z, min(x, y))), max(x, z)) ||
rewrite(max(x, max(z, min(y, x))), max(x, z)) ||
rewrite(max(x, max((x + c0), y)), max((x + c0), y), (0 <= c0)) ||
rewrite(max(x, max((x + c0), y)), max(x, y), (c0 <= 0)) ||
rewrite(max(x, max(min(x, y), z)), max(x, z)) ||
rewrite(max(x, max(min(y, x), z)), max(x, z)) ||
rewrite(max(x, max(min(z, max(x, y)), w)), max(x, max(min(z, y), w))) ||
rewrite(max(x, max(min(z, max(y, x)), w)), max(x, max(min(z, y), w))) ||
rewrite(max(x, max(min(max(x, y), z), w)), max(x, max(min(y, z), w))) ||
rewrite(max(x, max(min(max(y, x), z), w)), max(x, max(min(y, z), w))) ||
rewrite(max(y, (max(x, (y + c0)) + c1)), (max(x, (y + c0)) + c1), (0 <= (c0 + c1))) ||
rewrite(max(y, (max(x, y) - max(z, c0))), max(y, (x - max(z, c0))), (0 <= c0)) ||
rewrite(max(y, (max(y, x) - max(z, c0))), max(y, (x - max(z, c0))), (0 <= c0)) ||
rewrite(max(y, max(x, (y + c0))), max(x, (y + c0)), (0 <= c0)) ||
rewrite(max(y, max(x, (max(y, z) + c0))), max(x, (max(y, z) + c0)), (0 <= c0)) ||
rewrite(max(y, max(z, min(x, y))), max(z, y)) ||
rewrite(max(y, max(z, min(y, x))), max(z, y)) ||
rewrite(max(y, max((y + c0), x)), max((y + c0), x), (0 <= c0)) ||
rewrite(max(y, max((max(y, z) + c0), x)), max((max(y, z) + c0), x), (0 <= c0)) ||
rewrite(max(y, max(min(x, y), z)), max(y, z)) ||
rewrite(max(y, max(min(y, x), z)), max(y, z)) ||
rewrite(max(z, (max(x, min(y, (z + c0))) + c1)), max(z, (x + c1)), ((c0 + c1) <= 0)) ||
rewrite(max(z, (select(x, y, (z + c0)) + c1)), select(x, max((y + c1), z), z), ((c0 + c1) <= 0)) ||
rewrite(max((max(x, (y + c0)) + c1), y), (max(x, (y + c0)) + c1), (0 <= (c0 + c1))) ||
rewrite(max((max(x, min(y, (z + c0))) + c1), z), max((x + c1), z), ((c0 + c1) <= 0)) ||
rewrite(max((select(x, y, (z + c0)) + c1), z), select(x, max(z, (y + c1)), z), ((c0 + c1) <= 0)) ||
rewrite(max((x - y), (w + (z - y))), (max(x, (w + z)) - y)) ||
rewrite(max((x - y), ((z - y) + w)), (max((z + w), x) - y)) ||
rewrite(max((x - y), (((y + z) - ((w + c0)/c1)) + c2)), max((x - y), ((y + z) + ((c2 - w)/c1))), ((((1 <= c1) || (c2 == 0)) || (c1 <= -1)) && (((c1 <= -1) || (((c0 + c2) + 1) <= ((c2 + 1)*c1))) && (((1 <= c1) || ((((c0 + c1) + c2) + 1) <= (c1*c2))) && ((((-1 <= (min(((0 - c2)*c1), ((-1 - c2)*c1)) + (c0 + c2))) && (1 <= (((c1*c1)*2) + (((c0 + c2) - (c1*c2))*c1)))) && ((((c0 + c2)*c1) + 1) <= (((c1*c1)*2) + ((c1*c2)*c1)))) && (((c1*c2) + -1) <= ((c0 + c1) + c2))))))) ||
rewrite(max((x - (y/c0)), ((z - ((y + c1)/c0)) + c2)), max((x - (y/c0)), (((c2 - y)/c0) + z)), ((((1 <= c0) || (c2 == 0)) || (c0 <= -1)) && ((((-2 <= (((-1 - c2)*c0) + (c1 + c2))) && (((c1 + c2) + 1) <= ((c2 + 1)*c0))) || (c0 <= -1)) && (((c0 <= -1) || (((c1 + c2) + 1) <= ((c2 + 1)*c0))) && (((1 <= c0) || ((((c0 + c1) + c2) + 1) <= (c0*c2))) && (((-1 <= (min(((0 - c2)*c0), ((-1 - c2)*c0)) + (c1 + c2))) && (1 <= (min((((c1 + c2) - (c0*c2))*c0), (((c0*c2) - (c1 + c2))*c0)) + ((c0*c0)*2)))) && (((c0*c2) + -1) <= ((c0 + c1) + c2)))))))) ||
rewrite(max((x - max(min(x, c0), c1)), c2), (max(c0, x) + fold((c2 - c0))), ((((-2 <= c2) || (c0 <= (c1 + c2))) || ((c1 + -1) <= c0)) && (((-1 <= c2) || (c0 <= (c1 + c2))) && ((((c0 + 1) <= c1) || (c2 == 0)) && ((min(c2, 0) + c1) <= c0))))) ||
rewrite(max((min(x, y) - min(z, (x + c0))), c1), max((min(x, y) - z), c1), (0 <= (c0 + c1))) ||
rewrite(max((max(x, y) - max(y, z)), c0), max((x - max(y, z)), c0), (0 <= c0)) ||
rewrite(max((max(x, y) - max(z, c0)), y), max((x - max(z, c0)), y), (0 <= c0)) ||
rewrite(max((max(y, x) - max(z, c0)), y), max((x - max(z, c0)), y), (0 <= c0)) ||
rewrite(max((y*(x*x)), (x*(x*y))), (y*(x*x))) ||
rewrite(max((y*(x*x)), (x*(y*x))), (y*(x*x))) ||
rewrite(max((y*(x*x)), ((x*y)*x)), (y*(x*x))) ||
rewrite(max((y*(x*x)), ((y*x)*x)), (x*(y*x))) ||
rewrite(max((y*max(x, c0)), (z*max(x, c0))), (max(z, y)*max(x, c0)), (0 <= c0)) ||
rewrite(max((y*max(x, c0)), (max(x, c0)*z)), (max(x, c0)*max(y, z)), (0 <= c0)) ||
rewrite(max((z*min(x, y)), (z*max(x, y))), max((z*x), (z*y))) ||
rewrite(max((z*min(x, y)), (z*max(y, x))), max((z*x), (z*y))) ||
rewrite(max((z*min(x, y)), (max(x, y)*z)), max((z*x), (y*z))) ||
rewrite(max((z*min(x, y)), (max(y, x)*z)), max((z*x), (y*z))) ||
rewrite(max((z*min(y, x)), (z*max(x, y))), max((z*x), (z*y))) ||
rewrite(max((z*min(y, x)), (z*max(y, x))), max((z*x), (z*y))) ||
rewrite(max((z*min(y, x)), (max(x, y)*z)), max((z*x), (y*z))) ||
rewrite(max((z*min(y, x)), (max(y, x)*z)), max((z*x), (y*z))) ||
rewrite(max(((x*x)*y), (x*(x*y))), (x*(x*y))) ||
rewrite(max(((x*x)*y), (x*(y*x))), (x*(x*y))) ||
rewrite(max(((x*x)*y), ((x*y)*x)), (x*(y*x))) ||
rewrite(max(((x*x)*y), ((y*x)*x)), (x*(y*x))) ||
rewrite(max((min(x, c0)*c1), c2), max(min(x, c0), c2), ((((((-1 <= c2) || (0 <= c0)) || (c0 <= c2)) || (c1 <= 1)) || (c2 <= ((c2 + 1)*c1))) && ((((((-1 <= c2) || (0 <= c0)) || (c0 <= c2)) || (c0 <= ((c2 + 1)*c1))) || (c1 <= 1)) && ((((((-1 <= c2) || (0 <= c0)) || (c0 <= c2)) || (c0 <= (c0*c1))) || (c1 <= 1)) && (((((-1 <= c2) || (1 <= c0)) || (c0 <= -1)) || (c1 <= 1)) && (((((c0*c1) <= c2) || ((c1*c2) <= (c1 + c2))) || ((((c0 <= 0) || (c2 <= 1)) || (c2 <= c0)) && (((((c0 <= 0) || (c1 <= 1)) || (c2 <= 1)) || (c2 <= c0)) || (((c0*c1) + -1) <= c0)))) && ((((c0 <= 0) || (c1 <= 1)) || ((c0*c1) <= c2)) && ((((-1 <= c2) || (c0 <= 0)) || (c1 <= 1)) && (1 <= c1))))))))) ||
rewrite(max((min(x, y)*z), (z*max(x, y))), max((x*z), (z*y))) ||
rewrite(max((min(x, y)*z), (z*max(y, x))), max((x*z), (z*y))) ||
rewrite(max((min(x, y)*z), (max(x, y)*z)), max((x*z), (y*z))) ||
rewrite(max((min(x, y)*z), (max(y, x)*z)), max((x*z), (y*z))) ||
rewrite(max((min(y, x)*z), (z*max(x, y))), max((x*z), (z*y))) ||
rewrite(max((min(y, x)*z), (z*max(y, x))), max((x*z), (z*y))) ||
rewrite(max((min(y, x)*z), (max(x, y)*z)), max((x*z), (y*z))) ||
rewrite(max((min(y, x)*z), (max(y, x)*z)), max((x*z), (y*z))) ||
rewrite(max((max(x, c0)*c1), x), (max(x, c0)*c1), ((0 <= c0) && (1 <= c1))) ||
rewrite(max((max(x, c0)*y), (z*max(x, c0))), (max(z, y)*max(x, c0)), (0 <= c0)) ||
rewrite(max((max(x, c0)*y), (max(x, c0)*z)), (max(x, c0)*max(y, z)), (0 <= c0)) ||
rewrite(max(min(w, (x + (z + min(y, c0)))), (w + c0)), (w + c0), (0 <= c0)) ||
rewrite(max(min(w, (x + (min(y, c0) + z))), (w + c0)), (c0 + w), (0 <= c0)) ||
rewrite(max(min(w, (z + (y + min(x, c0)))), (w + c0)), (w + c0), (0 <= c0)) ||
rewrite(max(min(w, (z + (min(x, c0) + y))), (w + c0)), (c0 + w), (0 <= c0)) ||
rewrite(max(min(w, ((y + min(x, c0)) + z)), (w + c0)), (c0 + w), (0 <= c0)) ||
rewrite(max(min(w, ((z + min(y, c0)) + x)), (w + c0)), (c0 + w), (0 <= c0)) ||
rewrite(max(min(w, ((min(x, c0) + y) + z)), (w + c0)), (c0 + w), (0 <= c0)) ||
rewrite(max(min(w, ((min(y, c0) + z) + x)), (w + c0)), (c0 + w), (0 <= c0)) ||
rewrite(max(min(x, c0), (min(x, c1) + c2)), (min(x, c1) + c2), ((0 <= c2) && (c0 <= (c1 + c2)))) ||
rewrite(max(min(x, c0), (max(min(x, c1), c2) + c3)), (max(min(x, c1), c2) + c3), (((((-1 <= c3) || (c0 <= (c2 + c3))) || ((c0 + -2) <= (c1 + c3))) || ((c2 + -1) <= c1)) && ((((c0 <= (c2 + c3)) || ((c0 + -1) <= (c1 + c3))) || (c2 <= c1)) && (((c0 <= (max(c1, c2) + c3)) || ((c1 + 1) <= c2)) && ((((-1 <= c3) || (c0 <= (c2 + c3))) || (c1 <= (c2 + c3))) && (((0 <= c3) || (c0 <= (c2 + c3))) || ((c1 + 1) <= c2))))))) ||
rewrite(max(min(x, y), (x + c0)), (x + c0), (0 <= c0)) ||
rewrite(max(min(x, y), (y + c0)), (y + c0), (0 <= c0)) ||
rewrite(max(min(x, y), (y + min(z, 0))), min(max(x, (y + z)), y)) ||
rewrite(max(min(x, y), (min(z, 0) + y)), min(y, max(x, (z + y)))) ||
rewrite(max(min(x, y), min(w, min(x, z))), min(x, max(y, min(w, z)))) ||
rewrite(max(min(x, y), min(w, min(y, z))), min(y, max(x, min(w, z)))) ||
rewrite(max(min(x, y), min(w, min(z, x))), min(x, max(y, min(z, w)))) ||
rewrite(max(min(x, y), min(w, min(z, y))), min(y, max(x, min(z, w)))) ||
rewrite(max(min(x, y), min(min(x, z), w)), min(x, max(min(w, z), y))) ||
rewrite(max(min(x, y), min(min(y, z), w)), min(y, max(min(w, z), x))) ||
rewrite(max(min(x, y), min(min(z, x), w)), min(x, max(min(z, w), y))) ||
rewrite(max(min(x, y), min(min(z, y), w)), min(y, max(min(z, w), x))) ||
rewrite(max(min(x, (y + c0)), (min(y, c1) + c0)), min(max(fold((c0 + c1)), x), (y + c0))) ||
rewrite(max(min(x, (y + z)), (z + max(w, y))), (z + max(w, y))) ||
rewrite(max(min(x, (y + z)), (z + max(y, w))), (z + max(y, w))) ||
rewrite(max(min(x, (y + z)), (max(w, y) + z)), (max(w, y) + z)) ||
rewrite(max(min(x, (y + z)), (max(y, w) + z)), (max(y, w) + z)) ||
rewrite(max(min(x, (z + y)), (z + max(w, y))), (z + max(w, y))) ||
rewrite(max(min(x, (z + y)), (z + max(y, w))), (z + max(y, w))) ||
rewrite(max(min(x, (z + y)), (max(w, y) + z)), (max(w, y) + z)) ||
rewrite(max(min(x, (z + y)), (max(y, w) + z)), (max(y, w) + z)) ||
rewrite(max(min(y, x), (x + c0)), (x + c0), (0 <= c0)) ||
rewrite(max(min(y, x), (y + c0)), (y + c0), (0 <= c0)) ||
rewrite(max(min(y, x), (y + min(z, 0))), min(y, max((y + z), x))) ||
rewrite(max(min(y, x), (min(z, 0) + y)), min(y, max(x, (z + y)))) ||
rewrite(max(min(y, x), min(w, min(x, z))), min(x, max(y, min(w, z)))) ||
rewrite(max(min(y, x), min(w, min(y, z))), min(y, max(x, min(w, z)))) ||
rewrite(max(min(y, x), min(w, min(z, x))), min(x, max(y, min(z, w)))) ||
rewrite(max(min(y, x), min(w, min(z, y))), min(y, max(x, min(z, w)))) ||
rewrite(max(min(y, x), min(min(x, z), w)), min(x, max(min(w, z), y))) ||
rewrite(max(min(y, x), min(min(y, z), w)), min(y, max(min(w, z), x))) ||
rewrite(max(min(y, x), min(min(z, x), w)), min(x, max(min(z, w), y))) ||
rewrite(max(min(y, x), min(min(z, y), w)), min(y, max(min(z, w), x))) ||
rewrite(max(min(y, (x + c0)), (x + c1)), (x + c1), (c0 <= c1)) ||
rewrite(max(min(y, (x + c0)), (min(x, (y + c1)) + c2)), min(y, (x + c0)), ((c2 <= c0) && ((c1 + c2) <= 0))) ||
rewrite(max(min(y, (x + c0)), (min(x, (y + c1)) + c2)), (min((x + c0), y) + c0), ((((c0 <= 1) || (((c0*2) + c1) <= 0)) || ((c0*2) <= c2)) && ((((-1 <= c1) || (c0 <= 0)) || (c0 <= (c1 + c2))) && (((((-3 <= c1) || (c0 <= 1)) && ((-2 <= c1) || (c0 <= 0))) || ((c0 + -1) <= (c1 + c2))) && (((c0 <= 0) || ((c0*2) <= c2)) && ((((0 <= c0) && (c2 <= (c0*2))) && ((c1 + c2) <= c0)) && (c0 <= max((c1 + c2), 0)))))))) ||
rewrite(max(min(y, (x + c0)), (min(x, (y + c1)) + c2)), (min(y, (x + c0)) + fold(max(max((c2 - c0), (c1 + c2)), 0))), (((((c0 + c1) <= 0) || ((c1 + c2) <= 1)) || ((((c1*2) + c0) + c2) <= 2)) && (((0 <= (c0 + c1)) || (c2 <= c0)) && ((min(c0, c2) + c1) <= 0)))) ||
rewrite(max(min(y, (x + c0)), (min((y + c1), x) + c2)), min(y, (c0 + x)), ((c2 <= c0) && ((c1 + c2) <= 0))) ||
rewrite(max(min(y, (x + c0)), (min((y + c1), x) + c2)), (min(y, (x + c0)) + fold(max(max((c2 - c0), (c1 + c2)), 0))), (((((c0 + c1) <= 0) || ((c1 + c2) <= 1)) || ((((c1*2) + c0) + c2) <= 2)) && (((0 <= (c0 + c1)) || (c2 <= c0)) && ((min(c0, c2) + c1) <= 0)))) ||
rewrite(max(min(z, (x + y)), (y + max(w, x))), (y + max(w, x))) ||
rewrite(max(min(z, (x + y)), (y + max(x, w))), (y + max(x, w))) ||
rewrite(max(min(z, (x + y)), (max(w, x) + y)), (max(w, x) + y)) ||
rewrite(max(min(z, (x + y)), (max(x, w) + y)), (max(x, w) + y)) ||
rewrite(max(min(z, (y + x)), (y + max(w, x))), (y + max(w, x))) ||
rewrite(max(min(z, (y + x)), (y + max(x, w))), (y + max(x, w))) ||
rewrite(max(min(z, (y + x)), (max(w, x) + y)), (y + max(w, x))) ||
rewrite(max(min(z, (y + x)), (max(x, w) + y)), (y + max(x, w))) ||
rewrite(max(min(z, min(x, y)), x), x) ||
rewrite(max(min(z, min(x, y)), min(w, y)), min(max(w, min(x, z)), y)) ||
rewrite(max(min(z, min(x, y)), min(y, w)), min(max(w, min(x, z)), y)) ||
rewrite(max(min(z, min(y, x)), x), x) ||
rewrite(max(min(z, min(y, x)), min(w, y)), min(max(w, min(z, x)), y)) ||
rewrite(max(min(z, min(y, x)), min(y, w)), min(max(w, min(z, x)), y)) ||
rewrite(max(min((x + c0), y), (x + c1)), (x + c1), (c0 <= c1)) ||
rewrite(max(min((x + c0), y), (min(x, (y + c1)) + c2)), min((x + c0), y), ((c2 <= c0) && ((c1 + c2) <= 0))) ||
rewrite(max(min((x + c0), y), (min(x, (y + c1)) + c2)), (min((x + c0), y) + c0), ((((c0 <= 1) || (((c0*2) + c1) <= 0)) || ((c0*2) <= c2)) && ((((-1 <= c1) || (c0 <= 0)) || (c0 <= (c1 + c2))) && (((((-3 <= c1) || (c0 <= 1)) && ((-2 <= c1) || (c0 <= 0))) || ((c0 + -1) <= (c1 + c2))) && (((c0 <= 0) || ((c0*2) <= c2)) && ((((0 <= c0) && (c2 <= (c0*2))) && ((c1 + c2) <= c0)) && (c0 <= max((c1 + c2), 0)))))))) ||
rewrite(max(min((x + c0), y), (min(x, (y + c1)) + c2)), (min((x + c0), y) + fold(max(max((c2 - c0), (c1 + c2)), 0))), (((((c0 + c1) <= 0) || ((c1 + c2) <= 1)) || ((((c1*2) + c0) + c2) <= 2)) && (((0 <= (c0 + c1)) || (c2 <= c0)) && ((min(c0, c2) + c1) <= 0)))) ||
rewrite(max(min((x + c0), y), (min((y + c1), x) + c2)), min((x + c0), y), ((c2 <= c0) && ((c1 + c2) <= 0))) ||
rewrite(max(min((x + c0), y), (min((y + c1), x) + c2)), (min((x + c0), y) + fold(max(max((c2 - c0), (c1 + c2)), 0))), (((((c0 + c1) <= 0) || ((c1 + c2) <= 1)) || ((((c1*2) + c0) + c2) <= 2)) && (((0 <= (c0 + c1)) || (c2 <= c0)) && ((min(c0, c2) + c1) <= 0)))) ||
rewrite(max(min((x + y), z), (y + max(w, x))), (y + max(w, x))) ||
rewrite(max(min((x + y), z), (y + max(x, w))), (y + max(x, w))) ||
rewrite(max(min((x + y), z), (max(w, x) + y)), (max(w, x) + y)) ||
rewrite(max(min((x + y), z), (max(x, w) + y)), (max(x, w) + y)) ||
rewrite(max(min((y + c0), x), (min(y, c1) + c0)), min((y + c0), max(x, fold((c0 + c1))))) ||
rewrite(max(min((y + x), z), (y + max(w, x))), (y + max(w, x))) ||
rewrite(max(min((y + x), z), (y + max(x, w))), (y + max(x, w))) ||
rewrite(max(min((y + x), z), (max(w, x) + y)), (y + max(w, x))) ||
rewrite(max(min((y + x), z), (max(x, w) + y)), (max(x, w) + y)) ||
rewrite(max(min((y + z), x), (z + max(w, y))), (z + max(w, y))) ||
rewrite(max(min((y + z), x), (z + max(y, w))), (z + max(y, w))) ||
rewrite(max(min((y + z), x), (max(w, y) + z)), (max(w, y) + z)) ||
rewrite(max(min((y + z), x), (max(y, w) + z)), (max(y, w) + z)) ||
rewrite(max(min((z + y), x), (z + max(w, y))), (z + max(w, y))) ||
rewrite(max(min((z + y), x), (z + max(y, w))), (z + max(y, w))) ||
rewrite(max(min((z + y), x), (max(w, y) + z)), (max(w, y) + z)) ||
rewrite(max(min((z + y), x), (max(y, w) + z)), (max(y, w) + z)) ||
rewrite(max(min(min(x, y), z), x), x) ||
rewrite(max(min(min(x, y), z), min(w, y)), min(max(min(x, z), w), y)) ||
rewrite(max(min(min(x, y), z), min(y, w)), min(max(min(x, z), w), y)) ||
rewrite(max(min(min(y, x), z), x), x) ||
rewrite(max(min(min(y, x), z), min(w, y)), min(max(min(z, x), w), y)) ||
rewrite(max(min(min(y, x), z), min(y, w)), min(max(min(z, x), w), y)) ||
rewrite(max(max(w, min(z, max(x, y))), x), max(max(w, min(z, y)), x)) ||
rewrite(max(max(w, min(z, max(y, x))), x), max(max(w, min(z, y)), x)) ||
rewrite(max(max(w, min(max(x, y), z)), x), max(max(w, min(y, z)), x)) ||
rewrite(max(max(w, min(max(y, x), z)), x), max(max(w, min(y, z)), x)) ||
rewrite(max(max(x, y), (x + c0)), max(y, (x + c0)), (0 <= c0)) ||
rewrite(max(max(x, y), (x + c0)), max(y, x), (c0 <= 0)) ||
rewrite(max(max(x, y), (y + c0)), max(x, (y + c0)), (0 <= c0)) ||
rewrite(max(max(x, y), (max(y, 0) + c1)), max(max(x, c1), (y + c1)), (0 <= c1)) ||
rewrite(max(max(x, y), min(x, z)), max(y, x)) ||
rewrite(max(max(x, y), min(y, z)), max(x, y)) ||
rewrite(max(max(x, y), min(z, x)), max(y, x)) ||
rewrite(max(max(x, y), min(z, y)), max(x, y)) ||
rewrite(max(max(x, (y + c0)), y), max(x, (y + c0)), (0 <= c0)) ||
rewrite(max(max(x, (max(y, z) + c0)), y), max(x, (max(y, z) + c0)), (0 <= c0)) ||
rewrite(max(max(x, (x*c0)), 0), (max(x, 0)*c0), (1 <= c0)) ||
rewrite(max(max(y, x), (x + c0)), max(y, (x + c0)), (0 <= c0)) ||
rewrite(max(max(y, x), (x + c0)), max(y, x), (c0 <= 0)) ||
rewrite(max(max(y, x), (y + c0)), max(x, (y + c0)), (0 <= c0)) ||
rewrite(max(max(y, x), (max(y, 0) + c1)), max(max(c1, x), (y + c1)), (0 <= c1)) ||
rewrite(max(max(y, x), min(x, z)), max(y, x)) ||
rewrite(max(max(y, x), min(y, z)), max(x, y)) ||
rewrite(max(max(y, x), min(z, x)), max(y, x)) ||
rewrite(max(max(y, x), min(z, y)), max(x, y)) ||
rewrite(max(max(y, (x + c0)), x), max(y, (x + c0)), (0 <= c0)) ||
rewrite(max(max(y, (x + c0)), x), max(y, x), (c0 <= 0)) ||
rewrite(max(max(z, min(x, y)), x), max(z, x)) ||
rewrite(max(max(z, min(x, y)), y), max(z, y)) ||
rewrite(max(max(z, min(x, (y + c0))), min(x, (y + c1))), max(z, min((y + c0), x)), (c1 <= c0)) ||
rewrite(max(max(z, min(x, (y + c0))), min((y + c1), x)), max(z, min(x, (y + c0))), (c1 <= c0)) ||
rewrite(max(max(z, min(y, x)), x), max(z, x)) ||
rewrite(max(max(z, min(y, x)), y), max(z, y)) ||
rewrite(max(max(z, min((y + c0), x)), min(x, (y + c1))), max(z, min((y + c0), x)), (c1 <= c0)) ||
rewrite(max(max(z, min((y + c0), x)), min((y + c1), x)), max(z, min((y + c0), x)), (c1 <= c0)) ||
rewrite(max(max(z, max(x, y)), (y + c0)), max(max(z, x), (y + c0)), (0 <= c0)) ||
rewrite(max(max(z, max(y, x)), (y + c0)), max(max(z, x), (y + c0)), (0 <= c0)) ||
rewrite(max(max((x + c0), y), x), max((x + c0), y), (0 <= c0)) ||
rewrite(max(max((x + c0), y), x), max(x, y), (c0 <= 0)) ||
rewrite(max(max((y + c0), x), y), max((y + c0), x), (0 <= c0)) ||
rewrite(max(max((max(y, z) + c0), x), y), max((max(y, z) + c0), x), (0 <= c0)) ||
rewrite(max(max(min(x, y), z), x), max(z, x)) ||
rewrite(max(max(min(x, y), z), y), max(z, y)) ||
rewrite(max(max(min(x, (y + c0)), z), min(x, (y + c1))), max(min(x, (y + c0)), z), (c1 <= c0)) ||
rewrite(max(max(min(x, (y + c0)), z), min((y + c1), x)), max(min(x, (y + c0)), z), (c1 <= c0)) ||
rewrite(max(max(min(y, x), z), x), max(z, x)) ||
rewrite(max(max(min(y, x), z), y), max(y, z)) ||
rewrite(max(max(min(z, max(x, y)), w), x), max(max(min(z, y), w), x)) ||
rewrite(max(max(min(z, max(y, x)), w), x), max(max(min(z, y), w), x)) ||
rewrite(max(max(min((y + c0), x), z), min(x, (y + c1))), max(min((y + c0), x), z), (c1 <= c0)) ||
rewrite(max(max(min((y + c0), x), z), min((y + c1), x)), max(min((y + c0), x), z), (c1 <= c0)) ||
rewrite(max(max(min(max(x, y), z), w), x), max(max(min(y, z), w), x)) ||
rewrite(max(max(min(max(y, x), z), w), x), max(max(min(y, z), w), x)) ||
rewrite(max(max(max(x, y), z), (y + c0)), max(max(x, z), (y + c0)), (0 <= c0)) ||
rewrite(max(max(max(y, x), z), (y + c0)), max(max(x, z), (y + c0)), (0 <= c0)) ||
rewrite(max(select(x, (y + c0), y), (y + c0)), select(x, (y + c0), y), (c0 <= 0)) ||
rewrite(max(select((c0 < x), max(y, z), y), max(y, z)), max(z, y)) ||
rewrite(max(select((c0 < x), max(y, z), y), max(z, y)), max(z, y)) ||
rewrite(max(select((c0 < x), max(z, y), y), max(y, z)), max(z, y)) ||
rewrite(max(select((c0 < x), max(z, y), y), max(z, y)), max(z, y)) ||
rewrite(max(select((x < y), c0, c1), select((x < z), c0, c1)), select((x < max(y, z)), c0, c1), (c1 <= c0)) ||
