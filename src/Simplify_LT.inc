rewrite((c2 < max(((((x + y) % c0) + c1)/c0), (y + z))), (c2 < (y + z)), ((((0 <= c2) || (1 <= c0)) || (c0 <= -1)) && (((1 <= c0) || ((c0*c2) <= c1)) && ((c0 <= -1) || (c1 <= (c0*c2)))))) ||
rewrite((c2 < max(((((x + y) % c0) + c1)/c0), (z + y))), (c2 < (z + y)), ((((0 <= c2) || (1 <= c0)) || (c0 <= -1)) && (((1 <= c0) || ((c0*c2) <= c1)) && ((c0 <= -1) || (c1 <= (c0*c2)))))) ||
rewrite((w < ((min((x - (y + z)), c0) + w) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((w < ((min(((x + y) - z), c0) + w) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((w < (min(x, (min(min(y, z), w) + c0)) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((x < (y + min(x, 1))), (max(x, 1) <= y)) ||
rewrite((x < (z + (x + y))), (1 <= (z + y))) ||
rewrite((x < (z + (y + x))), (1 <= (y + z))) ||
rewrite((x < ((x + y) + z)), (1 <= (y + z))) ||
rewrite((x < ((y + x) + z)), (1 <= (y + z))) ||
rewrite((x < (min(x, 1) + y)), (max(x, 1) <= y)) ||
rewrite((x < (min((min(x, y) + c0), z) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((x < (min(min((x + c0), y), z) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((x < (min(min(min((x + c0), y), z), w) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((x < ((min((x*c0), c1) + c2)/c0)), false, ((((1 <= c1) || (c2 <= 0)) || (((c1 + c2) + 1) <= c0)) && ((max(c2, 0) + 1) <= c0))) ||
rewrite((x < ((min((x*c0), y) + c1)/c0)), false, ((max(c1, 0) + 1) <= c0)) ||
rewrite((x < min(y, min(x, (z + min(u, w))))), false) ||
rewrite((x < min(y, min(x, (z + min(w, u))))), false) ||
rewrite((x < min(y, min(x, (min(u, w) + z)))), false) ||
rewrite((x < min(y, min(x, (min(w, u) + z)))), false) ||
rewrite((x < min(y, min((z + min(u, w)), x))), false) ||
rewrite((x < min(y, min((z + min(w, u)), x))), false) ||
rewrite((x < min(y, min((min(u, w) + z), x))), false) ||
rewrite((x < min(y, min((min(w, u) + z), x))), false) ||
rewrite((x < min(z, (min(x, y) + c0))), false, (c0 <= 0)) ||
rewrite((x < min(z, min(x, y))), false) ||
rewrite((x < min(z, min(y, x))), false) ||
rewrite((x < min((min(x, y) + c0), z)), false, (c0 <= 0)) ||
rewrite((x < min(min(x, y), z)), false) ||
rewrite((x < min(min(y, x), z)), false) ||
rewrite((x < min(min(min(x, y), (z + w)), c0)), false) ||
rewrite((y < (x + min(y, 1))), (max(1, y) <= x)) ||
rewrite((y < (min(x, (min(y, z) + c0)) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((y < (min(x, (min((y + c0), z) + c1)) + c2)), false, (((c0 + c1) + c2) <= 0)) ||
rewrite((y < (min(y, 1) + x)), (max(y, 1) <= x)) ||
rewrite((y < (min((min(x, c0) + y), z) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((y < (min((min(x, y) + c0), z) + c0)), false, (c0 <= 0)) ||
rewrite((y < (min((min(x, y) + c0), z) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((y < (min(min(x, (y + c0)), z) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((y < (min(min(min(x, (y + c0)), z), w) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((y < (max(min(x, (y + c0)), z) + c1)), ((y + fold((1 - c1))) <= z), ((c0 + c1) <= 0)) ||
rewrite((y < min(u, min(min(z, min(x, y)), (w*c0)))), false) ||
rewrite((y < min(u, min(min(z, min(y, x)), (w*c0)))), false) ||
rewrite((y < min(u, min(min(min(x, y), z), (w*c0)))), false) ||
rewrite((y < min(u, min(min(min(y, x), z), (w*c0)))), false) ||
rewrite((y < min(w, min(z, min(x, y)))), false) ||
rewrite((y < min(w, min(z, min(y, x)))), false) ||
rewrite((y < min(w, min(min(x, y), z))), false) ||
rewrite((y < min(w, min(min(y, x), z))), false) ||
rewrite((y < min(z, (y + (min(x, c0)*c1)))), false, ((1 <= c1) && (c0 <= 0))) ||
rewrite((y < min(z, (((min(x, c0)*c1) + y) + c2))), false, ((((1 <= c0) || (c0 <= -1)) || (c2 <= 0)) && (((1 <= c1) && (((c0*c1) + c2) <= (min(c1, 0)*2))) && (min((max((c0*c1), 0) + c2), c0) <= 0)))) ||
rewrite((y < min(z, ((min(x, c0)*c1) + y))), false, ((1 <= c1) && (c0 <= 0))) ||
rewrite((y < min(z, min(u, min(min(x, y), (w + z))))), false) ||
rewrite((y < min(z, min(u, min(min(x, y), (z + w))))), false) ||
rewrite((y < min(z, min(u, min(min(y, x), (w + z))))), false) ||
rewrite((y < min(z, min(u, min(min(y, x), (z + w))))), false) ||
rewrite((y < min(z, min(x, y))), false) ||
rewrite((y < min(z, min(y, x))), false) ||
rewrite((y < min(z, min(min(min(x, y), (w + z)), u))), false) ||
rewrite((y < min(z, min(min(min(x, y), (z + w)), u))), false) ||
rewrite((y < min(z, min(min(min(y, x), (w + z)), u))), false) ||
rewrite((y < min(z, min(min(min(y, x), (z + w)), u))), false) ||
rewrite((y < min((y + (min(x, c0)*c1)), z)), false, ((1 <= c1) && (c0 <= 0))) ||
rewrite((y < min((((min(x, c0)*c1) + y) + c2), z)), false, ((((1 <= c0) || (c0 <= -1)) || (c2 <= 0)) && (((1 <= c1) && (((c0*c1) + c2) <= (min(c1, 0)*2))) && (min((max((c0*c1), 0) + c2), c0) <= 0)))) ||
rewrite((y < min(((min(x, c0)*c1) + y), z)), false, ((1 <= c1) && (c0 <= 0))) ||
rewrite((y < min(min(u, min(min(x, y), (w + z))), z)), false) ||
rewrite((y < min(min(u, min(min(x, y), (z + w))), z)), false) ||
rewrite((y < min(min(u, min(min(y, x), (w + z))), z)), false) ||
rewrite((y < min(min(u, min(min(y, x), (z + w))), z)), false) ||
rewrite((y < min(min(w, min(z, min(x, y))), (u*c0))), false) ||
rewrite((y < min(min(w, min(z, min(y, x))), (u*c0))), false) ||
rewrite((y < min(min(w, min(min(x, y), z)), (u*c0))), false) ||
rewrite((y < min(min(w, min(min(y, x), z)), (u*c0))), false) ||
rewrite((y < min(min(x, y), z)), false) ||
rewrite((y < min(min(y, x), z)), false) ||
rewrite((y < min(min(z, min(x, y)), w)), false) ||
rewrite((y < min(min(z, min(y, x)), w)), false) ||
rewrite((y < min(min(min(x, y), z), w)), false) ||
rewrite((y < min(min(min(y, x), z), w)), false) ||
rewrite((y < min(min(min(z, min(x, y)), w), (u*c0))), false) ||
rewrite((y < min(min(min(z, min(x, y)), (w*c0)), u)), false) ||
rewrite((y < min(min(min(z, min(y, x)), w), (u*c0))), false) ||
rewrite((y < min(min(min(z, min(y, x)), (w*c0)), u)), false) ||
rewrite((y < min(min(min(min(x, y), z), w), (u*c0))), false) ||
rewrite((y < min(min(min(min(x, y), z), (w*c0)), u)), false) ||
rewrite((y < min(min(min(min(x, y), (w + z)), u), z)), false) ||
rewrite((y < min(min(min(min(x, y), (z + w)), u), z)), false) ||
rewrite((y < min(min(min(min(y, x), z), w), (u*c0))), false) ||
rewrite((y < min(min(min(min(y, x), z), (w*c0)), u)), false) ||
rewrite((y < min(min(min(min(y, x), (w + z)), u), z)), false) ||
rewrite((y < min(min(min(min(y, x), (z + w)), u), z)), false) ||
rewrite((y < max(z, min(x, y))), (y < z)) ||
rewrite((y < max(z, min(y, x))), (y < z)) ||
rewrite((y < max(min(x, y), z)), (y < z)) ||
rewrite((y < max(min(y, x), z)), (y < z)) ||
rewrite((z < (min(x, y) + min(w, (z - x)))), false) ||
rewrite((z < (min(x, y) + min(w, (z - y)))), false) ||
rewrite((z < (min(x, y) + min((z - x), w))), false) ||
rewrite((z < (min(x, y) + min((z - y), w))), false) ||
rewrite((z < (min(x, (min(y, z) + c0)) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((z < (min(x, (min(y, (z + c0)) + c1)) + c2)), false, (((c0 + c1) + c2) <= 0)) ||
rewrite((z < (min(y, x) + min(w, (z - x)))), false) ||
rewrite((z < (min(y, x) + min(w, (z - y)))), false) ||
rewrite((z < (min(y, x) + min((z - x), w))), false) ||
rewrite((z < (min(y, x) + min((z - y), w))), false) ||
rewrite((z < (min((min((x*y), c0) + z), w) + c1)), false, (((((c0 + c1) <= 0) && ((min(c1, 0) + c0) <= 1)) && ((min(c0, 0) + c1) <= 1)) && (min(c0, c1) <= 0))) ||
rewrite((z < min(x, (min(y, z) + c0))), false, (c0 <= 0)) ||
rewrite((z < min((min(y, z) + c0), x)), false, (c0 <= 0)) ||
rewrite((z < select((x < y), z, min(w, z))), false) ||
rewrite((z < select((x < y), z, min(z, w))), false) ||
rewrite(((u + (w*(x + y))) < (w*(z + (x + y)))), (u < (w*z))) ||
rewrite(((u + (w*(x + y))) < (w*(z + (y + x)))), (u < (w*z))) ||
rewrite(((u + (w*(x + y))) < (w*((x + y) + z))), (u < (w*z))) ||
rewrite(((u + (w*(x + y))) < (w*((y + x) + z))), (u < (w*z))) ||
rewrite(((u + (w*(x + y))) < ((z + (x + y))*w)), (u < (z*w))) ||
rewrite(((u + (w*(x + y))) < ((z + (y + x))*w)), (u < (z*w))) ||
rewrite(((u + (w*(x + y))) < (((x + y) + z)*w)), (u < (z*w))) ||
rewrite(((u + (w*(x + y))) < (((y + x) + z)*w)), (u < (z*w))) ||
rewrite(((u + (w*(y + x))) < (w*(z + (x + y)))), (u < (w*z))) ||
rewrite(((u + (w*(y + x))) < (w*(z + (y + x)))), (u < (w*z))) ||
rewrite(((u + (w*(y + x))) < (w*((x + y) + z))), (u < (w*z))) ||
rewrite(((u + (w*(y + x))) < (w*((y + x) + z))), (u < (w*z))) ||
rewrite(((u + (w*(y + x))) < ((z + (x + y))*w)), (u < (z*w))) ||
rewrite(((u + (w*(y + x))) < ((z + (y + x))*w)), (u < (z*w))) ||
rewrite(((u + (w*(y + x))) < (((x + y) + z)*w)), (u < (z*w))) ||
rewrite(((u + (w*(y + x))) < (((y + x) + z)*w)), (u < (z*w))) ||
rewrite(((u + ((x + y)*w)) < (w*(z + (x + y)))), (u < (w*z))) ||
rewrite(((u + ((x + y)*w)) < (w*(z + (y + x)))), (u < (w*z))) ||
rewrite(((u + ((x + y)*w)) < (w*((x + y) + z))), (u < (w*z))) ||
rewrite(((u + ((x + y)*w)) < (w*((y + x) + z))), (u < (w*z))) ||
rewrite(((u + ((x + y)*w)) < ((z + (x + y))*w)), (u < (z*w))) ||
rewrite(((u + ((x + y)*w)) < ((z + (y + x))*w)), (u < (z*w))) ||
rewrite(((u + ((x + y)*w)) < (((x + y) + z)*w)), (u < (z*w))) ||
rewrite(((u + ((x + y)*w)) < (((y + x) + z)*w)), (u < (z*w))) ||
rewrite(((u + ((y + x)*w)) < (w*(z + (x + y)))), (u < (w*z))) ||
rewrite(((u + ((y + x)*w)) < (w*(z + (y + x)))), (u < (w*z))) ||
rewrite(((u + ((y + x)*w)) < (w*((x + y) + z))), (u < (w*z))) ||
rewrite(((u + ((y + x)*w)) < (w*((y + x) + z))), (u < (w*z))) ||
rewrite(((u + ((y + x)*w)) < ((z + (x + y))*w)), (u < (z*w))) ||
rewrite(((u + ((y + x)*w)) < ((z + (y + x))*w)), (u < (z*w))) ||
rewrite(((u + ((y + x)*w)) < (((x + y) + z)*w)), (u < (z*w))) ||
rewrite(((u + ((y + x)*w)) < (((y + x) + z)*w)), (u < (z*w))) ||
rewrite(((w + y) < (min(x, (min(y, z) + w)) + c0)), false, (c0 <= 0)) ||
rewrite(((w + y) < min(x, (w + min(y, z)))), false) ||
rewrite(((w + y) < min(x, (w + min(z, y)))), false) ||
rewrite(((w + y) < min(x, (min(y, z) + w))), false) ||
rewrite(((w + y) < min(x, (min(z, y) + w))), false) ||
rewrite(((w + y) < min((w + min(y, z)), x)), false) ||
rewrite(((w + y) < min((w + min(z, y)), x)), false) ||
rewrite(((w + y) < min((min(y, z) + w), x)), false) ||
rewrite(((w + y) < min((min(z, y) + w), x)), false) ||
rewrite(((w + z) < ((x + (y + z)) + c0)), ((w + fold((1 - c0))) <= (y + x))) ||
rewrite(((w + z) < min(x, (w + min(y, z)))), false) ||
rewrite(((w + z) < min(x, (w + min(z, y)))), false) ||
rewrite(((w + z) < min(x, (min(y, z) + w))), false) ||
rewrite(((w + z) < min(x, (min(z, y) + w))), false) ||
rewrite(((w + z) < min((w + min(y, z)), x)), false) ||
rewrite(((w + z) < min((w + min(z, y)), x)), false) ||
rewrite(((w + z) < min((min(y, z) + w), x)), false) ||
rewrite(((w + z) < min((min(z, y) + w), x)), false) ||
rewrite(((w + (x*z)) < (z*(x + y))), (w < (z*y))) ||
rewrite(((w + (x*z)) < (z*(y + x))), (w < (z*y))) ||
rewrite(((w + (x*z)) < ((x + y)*z)), (w < (y*z))) ||
rewrite(((w + (x*z)) < ((y + x)*z)), (w < (y*z))) ||
rewrite(((w + (y*z)) < (((x + y)*z) + 1)), (w <= (x*z))) ||
rewrite(((w + (z*x)) < (z*(x + y))), (w < (z*y))) ||
rewrite(((w + (z*x)) < (z*(y + x))), (w < (z*y))) ||
rewrite(((w + (z*x)) < ((x + y)*z)), (w < (y*z))) ||
rewrite(((w + (z*x)) < ((y + x)*z)), (w < (y*z))) ||
rewrite(((w + (z*y)) < (((x + y)*z) + 1)), (w <= (x*z))) ||
rewrite(((x + y) < (w + (z + (y + x)))), (0 < (w + z))) ||
rewrite(((x + y) < (w + ((y + x) + z))), (0 < (w + z))) ||
rewrite(((x + y) < ((z + (y + x)) + w)), (0 < (z + w))) ||
rewrite(((x + y) < (((y + x) + z) + w)), (0 < (z + w))) ||
rewrite(((x + y) < min(z, (x + y))), false) ||
rewrite(((x + y) < min(z, (y + x))), false) ||
rewrite(((x + y) < min((x + y), z)), false) ||
rewrite(((x + y) < min((y + x), z)), false) ||
rewrite(((x + z) < (min((min(x, y) + z), w) + c0)), false, (c0 <= 0)) ||
rewrite(((x + z) < min(w, (z + min(x, y)))), false) ||
rewrite(((x + z) < min(w, (z + min(y, x)))), false) ||
rewrite(((x + z) < min(w, (min(x, y) + z))), false) ||
rewrite(((x + z) < min(w, (min(y, x) + z))), false) ||
rewrite(((x + z) < min((z + min(x, y)), w)), false) ||
rewrite(((x + z) < min((z + min(y, x)), w)), false) ||
rewrite(((x + z) < min((min(x, y) + z), w)), false) ||
rewrite(((x + z) < min((min(y, x) + z), w)), false) ||
rewrite(((x + select((z < w), c1, c2)) < min(y, (x + c0))), false, (c0 <= min(c1, c2))) ||
rewrite(((x + select((z < w), c1, c2)) < min((x + c0), y)), false, (c0 <= min(c1, c2))) ||
rewrite(((y + w) < (min(x, (min(y, z) + w)) + c0)), false, (c0 <= 0)) ||
rewrite(((y + w) < min(x, (w + min(y, z)))), false) ||
rewrite(((y + w) < min(x, (w + min(z, y)))), false) ||
rewrite(((y + w) < min(x, (min(y, z) + w))), false) ||
rewrite(((y + w) < min(x, (min(z, y) + w))), false) ||
rewrite(((y + w) < min((w + min(y, z)), x)), false) ||
rewrite(((y + w) < min((w + min(z, y)), x)), false) ||
rewrite(((y + w) < min((min(y, z) + w), x)), false) ||
rewrite(((y + w) < min((min(z, y) + w), x)), false) ||
rewrite(((y + x) < (w + (z + (x + y)))), (0 < (w + z))) ||
rewrite(((y + x) < (w + ((x + y) + z))), (0 < (w + z))) ||
rewrite(((y + x) < ((z + (x + y)) + w)), (0 < (z + w))) ||
rewrite(((y + x) < (((x + y) + z) + w)), (0 < (z + w))) ||
rewrite(((y + x) < min(z, (x + y))), false) ||
rewrite(((y + x) < min(z, (y + x))), false) ||
rewrite(((y + x) < min((x + y), z)), false) ||
rewrite(((y + x) < min((y + x), z)), false) ||
rewrite(((y + z) < (w + (x + (z + y)))), (0 < (w + x))) ||
rewrite(((y + z) < (w + ((z + y) + x))), (0 < (w + x))) ||
rewrite(((y + z) < ((x + (z + y)) + w)), (0 < (x + w))) ||
rewrite(((y + z) < (((z + y) + x) + w)), (0 < (x + w))) ||
rewrite(((y + z) < min(w, (z + min(x, y)))), false) ||
rewrite(((y + z) < min(w, (z + min(y, x)))), false) ||
rewrite(((y + z) < min(w, (min(x, y) + z))), false) ||
rewrite(((y + z) < min(w, (min(y, x) + z))), false) ||
rewrite(((y + z) < min((z + min(x, y)), w)), false) ||
rewrite(((y + z) < min((z + min(y, x)), w)), false) ||
rewrite(((y + z) < min((min(x, y) + z), w)), false) ||
rewrite(((y + z) < min((min(y, x) + z), w)), false) ||
rewrite(((y + (x*c0)) < (((z + x)*c0) + c1)), (y < ((z*c0) + c1))) ||
rewrite(((y + ((((x - y) + c0)/c1)*c1)) < x), false, ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
rewrite(((y + ((((x - y) + c0)/c1)*c1)) < x), false, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
rewrite(((y + (((x - y)/c1)*c1)) < (x + c0)), false, ((0 <= c1) && ((c0 + c1) <= 1))) ||
rewrite(((y + (((x - y)/c1)*c1)) < (x + c0)), false, (((c0 + -1) <= c1) && ((c0 + c1) <= 1))) ||
rewrite(((y + min(x, c1)) < (x + c0)), ((y + fold(((c1 - c0) + 1))) <= max(x, c1))) ||
rewrite(((y + max(w, x)) < min(z, (x + y))), false) ||
rewrite(((y + max(w, x)) < min(z, (y + x))), false) ||
rewrite(((y + max(w, x)) < min((x + y), z)), false) ||
rewrite(((y + max(w, x)) < min((y + x), z)), false) ||
rewrite(((y + max(w, z)) < min(x, (y + z))), false) ||
rewrite(((y + max(w, z)) < min(x, (z + y))), false) ||
rewrite(((y + max(w, z)) < min((y + z), x)), false) ||
rewrite(((y + max(w, z)) < min((z + y), x)), false) ||
rewrite(((y + max(x, w)) < min(z, (x + y))), false) ||
rewrite(((y + max(x, w)) < min(z, (y + x))), false) ||
rewrite(((y + max(x, w)) < min((x + y), z)), false) ||
rewrite(((y + max(x, w)) < min((y + x), z)), false) ||
rewrite(((y + max(z, w)) < min(x, (y + z))), false) ||
rewrite(((y + max(z, w)) < min(x, (z + y))), false) ||
rewrite(((y + max(z, w)) < min((y + z), x)), false) ||
rewrite(((y + max(z, w)) < min((z + y), x)), false) ||
rewrite(((y + select((z < w), c0, c1)) < min(x, y)), false, (0 <= min(c0, c1))) ||
rewrite(((y + select((z < w), c0, c1)) < min(y, x)), false, (0 <= min(c0, c1))) ||
rewrite(((y + select((z < w), c1, c2)) < (min(x, y) + c0)), false, (((c0 + -1) <= c2) && (c0 <= min(c1, c2)))) ||
rewrite(((y + select((z < w), c1, c2)) < min(x, (y + c0))), false, (c0 <= min(c1, c2))) ||
rewrite(((y + select((z < w), c1, c2)) < min((y + c0), x)), false, (c0 <= min(c1, c2))) ||
rewrite(((z + w) < ((x + (y + z)) + c0)), ((w + fold((1 - c0))) <= (y + x))) ||
rewrite(((z + w) < min(x, (w + min(y, z)))), false) ||
rewrite(((z + w) < min(x, (w + min(z, y)))), false) ||
rewrite(((z + w) < min(x, (min(y, z) + w))), false) ||
rewrite(((z + w) < min(x, (min(z, y) + w))), false) ||
rewrite(((z + w) < min((w + min(y, z)), x)), false) ||
rewrite(((z + w) < min((w + min(z, y)), x)), false) ||
rewrite(((z + w) < min((min(y, z) + w), x)), false) ||
rewrite(((z + w) < min((min(z, y) + w), x)), false) ||
rewrite(((z + x) < (min((min(x, y) + z), w) + c0)), false, (c0 <= 0)) ||
rewrite(((z + x) < min(w, (z + min(x, y)))), false) ||
rewrite(((z + x) < min(w, (z + min(y, x)))), false) ||
rewrite(((z + x) < min(w, (min(x, y) + z))), false) ||
rewrite(((z + x) < min(w, (min(y, x) + z))), false) ||
rewrite(((z + x) < min((z + min(x, y)), w)), false) ||
rewrite(((z + x) < min((z + min(y, x)), w)), false) ||
rewrite(((z + x) < min((min(x, y) + z), w)), false) ||
rewrite(((z + x) < min((min(y, x) + z), w)), false) ||
rewrite(((z + y) < (w + (x + (y + z)))), (0 < (w + x))) ||
rewrite(((z + y) < (w + ((y + z) + x))), (0 < (w + x))) ||
rewrite(((z + y) < ((x + (y + z)) + w)), (0 < (x + w))) ||
rewrite(((z + y) < (((y + z) + x) + w)), (0 < (x + w))) ||
rewrite(((z + y) < min(w, (z + min(x, y)))), false) ||
rewrite(((z + y) < min(w, (z + min(y, x)))), false) ||
rewrite(((z + y) < min(w, (min(x, y) + z))), false) ||
rewrite(((z + y) < min(w, (min(y, x) + z))), false) ||
rewrite(((z + y) < min((z + min(x, y)), w)), false) ||
rewrite(((z + y) < min((z + min(y, x)), w)), false) ||
rewrite(((z + y) < min((min(x, y) + z), w)), false) ||
rewrite(((z + y) < min((min(y, x) + z), w)), false) ||
rewrite(((z + (x*c0)) < (((x + y)*c0) + c1)), ((z + fold((1 - c1))) <= (y*c0))) ||
rewrite(((z + (y*c0)) < (((x + y)*c0) + c1)), ((z + fold((1 - c1))) <= (x*c0))) ||
rewrite(((z + ((x + c2)/c1)) < (y + ((x + c0)/c1))), (z < (y + 0)), (c2 == c0)) ||
rewrite(((z + ((x + c2)/c1)) < (((x + c0)/c1) + y)), (z < (0 + y)), (c2 == c0)) ||
rewrite((((w*(x + y)) + u) < (w*(z + (x + y)))), (u < (w*z))) ||
rewrite((((w*(x + y)) + u) < (w*(z + (y + x)))), (u < (w*z))) ||
rewrite((((w*(x + y)) + u) < (w*((x + y) + z))), (u < (w*z))) ||
rewrite((((w*(x + y)) + u) < (w*((y + x) + z))), (u < (w*z))) ||
rewrite((((w*(x + y)) + u) < ((z + (x + y))*w)), (u < (z*w))) ||
rewrite((((w*(x + y)) + u) < ((z + (y + x))*w)), (u < (z*w))) ||
rewrite((((w*(x + y)) + u) < (((x + y) + z)*w)), (u < (z*w))) ||
rewrite((((w*(x + y)) + u) < (((y + x) + z)*w)), (u < (z*w))) ||
rewrite((((w*(y + x)) + u) < (w*(z + (x + y)))), (u < (w*z))) ||
rewrite((((w*(y + x)) + u) < (w*(z + (y + x)))), (u < (w*z))) ||
rewrite((((w*(y + x)) + u) < (w*((x + y) + z))), (u < (w*z))) ||
rewrite((((w*(y + x)) + u) < (w*((y + x) + z))), (u < (w*z))) ||
rewrite((((w*(y + x)) + u) < ((z + (x + y))*w)), (u < (z*w))) ||
rewrite((((w*(y + x)) + u) < ((z + (y + x))*w)), (u < (z*w))) ||
rewrite((((w*(y + x)) + u) < (((x + y) + z)*w)), (u < (z*w))) ||
rewrite((((w*(y + x)) + u) < (((y + x) + z)*w)), (u < (z*w))) ||
rewrite((((x*c0) + y) < (((z + x)*c0) + c1)), (y < ((z*c0) + c1))) ||
rewrite((((x*c0) + z) < (((x + y)*c0) + c1)), ((fold((1 - c1)) + z) <= (y*c0))) ||
rewrite((((x*z) + w) < (z*(x + y))), (w < (z*y))) ||
rewrite((((x*z) + w) < (z*(y + x))), (w < (z*y))) ||
rewrite((((x*z) + w) < ((x + y)*z)), (w < (y*z))) ||
rewrite((((x*z) + w) < ((y + x)*z)), (w < (y*z))) ||
rewrite((((y*c0) + z) < (((x + y)*c0) + c1)), ((fold((1 - c1)) + z) <= (x*c0))) ||
rewrite((((y*z) + w) < (((x + y)*z) + 1)), (w <= (x*z))) ||
rewrite((((z*x) + w) < (z*(x + y))), (w < (z*y))) ||
rewrite((((z*x) + w) < (z*(y + x))), (w < (z*y))) ||
rewrite((((z*x) + w) < ((x + y)*z)), (w < (y*z))) ||
rewrite((((z*x) + w) < ((y + x)*z)), (w < (y*z))) ||
rewrite((((z*y) + w) < (((x + y)*z) + 1)), (w <= (x*z))) ||
rewrite(((((x + y)*w) + u) < (w*(z + (y + x)))), (u < (w*z))) ||
rewrite(((((x + y)*w) + u) < (w*((y + x) + z))), (u < (w*z))) ||
rewrite(((((x + y)*w) + u) < ((z + (y + x))*w)), (u < (z*w))) ||
rewrite(((((x + y)*w) + u) < (((y + x) + z)*w)), (u < (z*w))) ||
rewrite(((((y + x)*w) + u) < (w*(z + (x + y)))), (u < (w*z))) ||
rewrite(((((y + x)*w) + u) < (w*((x + y) + z))), (u < (w*z))) ||
rewrite(((((y + x)*w) + u) < ((z + (x + y))*w)), (u < (z*w))) ||
rewrite(((((y + x)*w) + u) < (((x + y) + z)*w)), (u < (z*w))) ||
rewrite(((((((x - y) + c0)/c1)*c1) + y) < x), false, ((-1 <= (c0 + c1)) && ((c1 + -1) <= c0))) ||
rewrite(((((((x - y) + c0)/c1)*c1) + y) < x), false, ((0 <= c1) && (c1 < (max(c0, -1) + 2)))) ||
rewrite((((((x - y)/c1)*c1) + y) < (x + c0)), false, ((0 <= c1) && ((c0 + c1) <= 1))) ||
rewrite((((((x - y)/c1)*c1) + y) < (x + c0)), false, (((c0 + -1) <= c1) && ((c0 + c1) <= 1))) ||
rewrite(((((x + c2)/c1) + z) < (y + ((x + c0)/c1))), (z < (y + 0)), (c2 == c0)) ||
rewrite(((((x + c2)/c1) + z) < (((x + c0)/c1) + y)), (z < (0 + y)), (c2 == c0)) ||
rewrite(((min(x, c0) + ((max(x, c0)/c1)*c1)) < x), false, (((1 <= c0) || (c1 <= 1)) && (((-1 <= c1) || (1 <= c0)) && ((-1 <= (c0 + c1)) && ((max(c1, 1) + -1) <= c0))))) ||
rewrite(((min(x, c1) + y) < (x + c0)), ((fold(((c1 - c0) + 1)) + y) <= max(x, c1))) ||
rewrite(((min((x*c0), c1) + (y*c0)) < (x*c0)), ((min(0, x) + y) < x), ((0 <= c1) && ((max(c1, 0) + 1) <= c0))) ||
rewrite(((min((x*c0), c1) + (y*c0)) < (x*c0)), (y <= max(-1, x)), (((0 <= (c0 + c1)) && (1 <= c0)) && (c1 <= -1))) ||
rewrite(((max(u, x) + (y + z)) < min(w, (x + (y + z)))), false) ||
rewrite(((max(u, x) + (y + z)) < min(w, (x + (z + y)))), false) ||
rewrite(((max(u, x) + (y + z)) < min(w, ((y + z) + x))), false) ||
rewrite(((max(u, x) + (y + z)) < min(w, ((z + y) + x))), false) ||
rewrite(((max(u, x) + (y + z)) < min((x + (y + z)), w)), false) ||
rewrite(((max(u, x) + (y + z)) < min((x + (z + y)), w)), false) ||
rewrite(((max(u, x) + (y + z)) < min(((y + z) + x), w)), false) ||
rewrite(((max(u, x) + (y + z)) < min(((z + y) + x), w)), false) ||
rewrite(((max(u, x) + (z + y)) < min(w, (x + (y + z)))), false) ||
rewrite(((max(u, x) + (z + y)) < min(w, (x + (z + y)))), false) ||
rewrite(((max(u, x) + (z + y)) < min(w, ((y + z) + x))), false) ||
rewrite(((max(u, x) + (z + y)) < min(w, ((z + y) + x))), false) ||
rewrite(((max(u, x) + (z + y)) < min((x + (y + z)), w)), false) ||
rewrite(((max(u, x) + (z + y)) < min((x + (z + y)), w)), false) ||
rewrite(((max(u, x) + (z + y)) < min(((y + z) + x), w)), false) ||
rewrite(((max(u, x) + (z + y)) < min(((z + y) + x), w)), false) ||
rewrite(((max(w, x) + y) < min(z, (x + y))), false) ||
rewrite(((max(w, x) + y) < min(z, (y + x))), false) ||
rewrite(((max(w, x) + y) < min((x + y), z)), false) ||
rewrite(((max(w, x) + y) < min((y + x), z)), false) ||
rewrite(((max(w, z) + y) < min(x, (y + z))), false) ||
rewrite(((max(w, z) + y) < min(x, (z + y))), false) ||
rewrite(((max(w, z) + y) < min((y + z), x)), false) ||
rewrite(((max(w, z) + y) < min((z + y), x)), false) ||
rewrite(((max(x, u) + (y + z)) < min(w, (x + (y + z)))), false) ||
rewrite(((max(x, u) + (y + z)) < min(w, (x + (z + y)))), false) ||
rewrite(((max(x, u) + (y + z)) < min(w, ((y + z) + x))), false) ||
rewrite(((max(x, u) + (y + z)) < min(w, ((z + y) + x))), false) ||
rewrite(((max(x, u) + (y + z)) < min((x + (y + z)), w)), false) ||
rewrite(((max(x, u) + (y + z)) < min((x + (z + y)), w)), false) ||
rewrite(((max(x, u) + (y + z)) < min(((y + z) + x), w)), false) ||
rewrite(((max(x, u) + (y + z)) < min(((z + y) + x), w)), false) ||
rewrite(((max(x, u) + (z + y)) < min(w, (x + (y + z)))), false) ||
rewrite(((max(x, u) + (z + y)) < min(w, (x + (z + y)))), false) ||
rewrite(((max(x, u) + (z + y)) < min(w, ((y + z) + x))), false) ||
rewrite(((max(x, u) + (z + y)) < min(w, ((z + y) + x))), false) ||
rewrite(((max(x, u) + (z + y)) < min((x + (y + z)), w)), false) ||
rewrite(((max(x, u) + (z + y)) < min((x + (z + y)), w)), false) ||
rewrite(((max(x, u) + (z + y)) < min(((y + z) + x), w)), false) ||
rewrite(((max(x, u) + (z + y)) < min(((z + y) + x), w)), false) ||
rewrite(((max(x, w) + y) < min(z, (x + y))), false) ||
rewrite(((max(x, w) + y) < min(z, (y + x))), false) ||
rewrite(((max(x, w) + y) < min((x + y), z)), false) ||
rewrite(((max(x, w) + y) < min((y + x), z)), false) ||
rewrite(((max(z, w) + y) < min(x, (y + z))), false) ||
rewrite(((max(z, w) + y) < min(x, (z + y))), false) ||
rewrite(((max(z, w) + y) < min((y + z), x)), false) ||
rewrite(((max(z, w) + y) < min((z + y), x)), false) ||
rewrite(((select((z < w), c0, c1) + y) < min(x, y)), false, (0 <= min(c0, c1))) ||
rewrite(((select((z < w), c0, c1) + y) < min(y, x)), false, (0 <= min(c0, c1))) ||
rewrite(((select((z < w), c1, c2) + x) < min(y, (x + c0))), false, (c0 <= min(c1, c2))) ||
rewrite(((select((z < w), c1, c2) + x) < min((x + c0), y)), false, (c0 <= min(c1, c2))) ||
rewrite(((select((z < w), c1, c2) + y) < (min(x, y) + c0)), false, (((c0 + -1) <= c2) && (c0 <= min(c1, c2)))) ||
rewrite(((select((z < w), c1, c2) + y) < min(x, (y + c0))), false, (c0 <= min(c1, c2))) ||
rewrite(((select((z < w), c1, c2) + y) < min((y + c0), x)), false, (c0 <= min(c1, c2))) ||
rewrite(((w*(u + (y + z))) < (x + (w*(y + z)))), ((w*u) < x)) ||
rewrite(((w*(u + (y + z))) < (x + (w*(z + y)))), ((w*u) < x)) ||
rewrite(((w*(u + (y + z))) < (x + ((y + z)*w))), ((w*u) < x)) ||
rewrite(((w*(u + (y + z))) < (x + ((z + y)*w))), ((w*u) < x)) ||
rewrite(((w*(u + (y + z))) < ((w*(y + z)) + x)), ((w*u) < x)) ||
rewrite(((w*(u + (y + z))) < ((w*(z + y)) + x)), ((w*u) < x)) ||
rewrite(((w*(u + (y + z))) < (((y + z)*w) + x)), ((w*u) < x)) ||
rewrite(((w*(u + (y + z))) < (((z + y)*w) + x)), ((w*u) < x)) ||
rewrite(((w*(u + (z + y))) < (x + (w*(y + z)))), ((w*u) < x)) ||
rewrite(((w*(u + (z + y))) < (x + (w*(z + y)))), ((w*u) < x)) ||
rewrite(((w*(u + (z + y))) < (x + ((y + z)*w))), ((w*u) < x)) ||
rewrite(((w*(u + (z + y))) < (x + ((z + y)*w))), ((w*u) < x)) ||
rewrite(((w*(u + (z + y))) < ((w*(y + z)) + x)), ((w*u) < x)) ||
rewrite(((w*(u + (z + y))) < ((w*(z + y)) + x)), ((w*u) < x)) ||
rewrite(((w*(u + (z + y))) < (((y + z)*w) + x)), ((w*u) < x)) ||
rewrite(((w*(u + (z + y))) < (((z + y)*w) + x)), ((w*u) < x)) ||
rewrite(((w*(y + z)) < ((x + ((y + z)*w)) + c0)), (fold((0 - c0)) < x)) ||
rewrite(((w*(z + y)) < ((x + ((y + z)*w)) + c0)), (fold((0 - c0)) < x)) ||
rewrite(((w*((y + z) + u)) < (x + (w*(y + z)))), ((w*u) < x)) ||
rewrite(((w*((y + z) + u)) < (x + (w*(z + y)))), ((w*u) < x)) ||
rewrite(((w*((y + z) + u)) < (x + ((y + z)*w))), ((w*u) < x)) ||
rewrite(((w*((y + z) + u)) < (x + ((z + y)*w))), ((w*u) < x)) ||
rewrite(((w*((y + z) + u)) < ((w*(y + z)) + x)), ((w*u) < x)) ||
rewrite(((w*((y + z) + u)) < ((w*(z + y)) + x)), ((w*u) < x)) ||
rewrite(((w*((y + z) + u)) < (((y + z)*w) + x)), ((w*u) < x)) ||
rewrite(((w*((y + z) + u)) < (((z + y)*w) + x)), ((w*u) < x)) ||
rewrite(((w*((z + y) + u)) < (x + (w*(y + z)))), ((w*u) < x)) ||
rewrite(((w*((z + y) + u)) < (x + (w*(z + y)))), ((w*u) < x)) ||
rewrite(((w*((z + y) + u)) < (x + ((y + z)*w))), ((w*u) < x)) ||
rewrite(((w*((z + y) + u)) < (x + ((z + y)*w))), ((w*u) < x)) ||
rewrite(((w*((z + y) + u)) < ((w*(y + z)) + x)), ((w*u) < x)) ||
rewrite(((w*((z + y) + u)) < ((w*(z + y)) + x)), ((w*u) < x)) ||
rewrite(((w*((z + y) + u)) < (((y + z)*w) + x)), ((w*u) < x)) ||
rewrite(((w*((z + y) + u)) < (((z + y)*w) + x)), ((w*u) < x)) ||
rewrite(((y*c0) < min(z, (min(x, y)*c0))), false, (1 <= c0)) ||
rewrite(((y*c0) < min((min(x, y)*c0), z)), false, (1 <= c0)) ||
rewrite(((y*min(x, c1)) < min((x*y), c0)), false, ((((1 <= c1) || (c0 <= 0)) || (c1 <= -1)) && (max(c0, 0) <= c1))) ||
rewrite(((y*min(x, y)) < min((x*y), c0)), false, (c0 <= 1)) ||
rewrite(((y*min(y, x)) < min((x*y), c0)), false, (c0 <= 1)) ||
rewrite(((z*(w + y)) < (x + (y*z))), ((z*w) < x)) ||
rewrite(((z*(w + y)) < (x + (z*y))), ((z*w) < x)) ||
rewrite(((z*(w + y)) < ((x + (y*z)) + c0)), (((z*w) + fold((1 - c0))) <= x)) ||
rewrite(((z*(w + y)) < ((y*z) + x)), ((z*w) < x)) ||
rewrite(((z*(w + y)) < ((z*y) + x)), ((z*w) < x)) ||
rewrite(((z*(y + w)) < (x + (y*z))), ((z*w) < x)) ||
rewrite(((z*(y + w)) < (x + (z*y))), ((z*w) < x)) ||
rewrite(((z*(y + w)) < ((x + (y*z)) + c0)), (((z*w) + fold((1 - c0))) <= x)) ||
rewrite(((z*(y + w)) < ((y*z) + x)), ((z*w) < x)) ||
rewrite(((z*(y + w)) < ((z*y) + x)), ((z*w) < x)) ||
rewrite(((z*min(x, y)) < min(u, min(w, (z*min(y, x))))), false) ||
rewrite(((z*min(x, y)) < min(u, min(w, (min(x, y)*z)))), false) ||
rewrite(((z*min(x, y)) < min(u, min(w, (min(y, x)*z)))), false) ||
rewrite(((z*min(x, y)) < min(u, min((z*min(y, x)), w))), false) ||
rewrite(((z*min(x, y)) < min(u, min((min(x, y)*z), w))), false) ||
rewrite(((z*min(x, y)) < min(u, min((min(y, x)*z), w))), false) ||
rewrite(((z*min(x, y)) < min(min(w, (z*min(y, x))), u)), false) ||
rewrite(((z*min(x, y)) < min(min(w, (min(x, y)*z)), u)), false) ||
rewrite(((z*min(x, y)) < min(min(w, (min(y, x)*z)), u)), false) ||
rewrite(((z*min(x, y)) < min(min((z*min(y, x)), w), u)), false) ||
rewrite(((z*min(x, y)) < min(min((min(x, y)*z), w), u)), false) ||
rewrite(((z*min(x, y)) < min(min((min(y, x)*z), w), u)), false) ||
rewrite(((z*min(y, x)) < min(u, min(w, (z*min(x, y))))), false) ||
rewrite(((z*min(y, x)) < min(u, min(w, (min(x, y)*z)))), false) ||
rewrite(((z*min(y, x)) < min(u, min(w, (min(y, x)*z)))), false) ||
rewrite(((z*min(y, x)) < min(u, min((z*min(x, y)), w))), false) ||
rewrite(((z*min(y, x)) < min(u, min((min(x, y)*z), w))), false) ||
rewrite(((z*min(y, x)) < min(u, min((min(y, x)*z), w))), false) ||
rewrite(((z*min(y, x)) < min(min(w, (z*min(x, y))), u)), false) ||
rewrite(((z*min(y, x)) < min(min(w, (min(x, y)*z)), u)), false) ||
rewrite(((z*min(y, x)) < min(min(w, (min(y, x)*z)), u)), false) ||
rewrite(((z*min(y, x)) < min(min((z*min(x, y)), w), u)), false) ||
rewrite(((z*min(y, x)) < min(min((min(x, y)*z), w), u)), false) ||
rewrite(((z*min(y, x)) < min(min((min(y, x)*z), w), u)), false) ||
rewrite((((u + (y + z))*w) < (x + (w*(y + z)))), ((u*w) < x)) ||
rewrite((((u + (y + z))*w) < (x + (w*(z + y)))), ((u*w) < x)) ||
rewrite((((u + (y + z))*w) < (x + ((y + z)*w))), ((u*w) < x)) ||
rewrite((((u + (y + z))*w) < (x + ((z + y)*w))), ((u*w) < x)) ||
rewrite((((u + (y + z))*w) < ((w*(y + z)) + x)), ((u*w) < x)) ||
rewrite((((u + (y + z))*w) < ((w*(z + y)) + x)), ((u*w) < x)) ||
rewrite((((u + (y + z))*w) < (((y + z)*w) + x)), ((u*w) < x)) ||
rewrite((((u + (y + z))*w) < (((z + y)*w) + x)), ((u*w) < x)) ||
rewrite((((u + (z + y))*w) < (x + (w*(y + z)))), ((u*w) < x)) ||
rewrite((((u + (z + y))*w) < (x + (w*(z + y)))), ((u*w) < x)) ||
rewrite((((u + (z + y))*w) < (x + ((y + z)*w))), ((u*w) < x)) ||
rewrite((((u + (z + y))*w) < (x + ((z + y)*w))), ((u*w) < x)) ||
rewrite((((u + (z + y))*w) < ((w*(y + z)) + x)), ((u*w) < x)) ||
rewrite((((u + (z + y))*w) < ((w*(z + y)) + x)), ((u*w) < x)) ||
rewrite((((u + (z + y))*w) < (((y + z)*w) + x)), ((u*w) < x)) ||
rewrite((((u + (z + y))*w) < (((z + y)*w) + x)), ((u*w) < x)) ||
rewrite((((w + y)*z) < (x + (y*z))), ((w*z) < x)) ||
rewrite((((w + y)*z) < (x + (z*y))), ((w*z) < x)) ||
rewrite((((w + y)*z) < ((x + (y*z)) + c0)), ((fold((1 - c0)) + (w*z)) <= x)) ||
rewrite((((w + y)*z) < ((y*z) + x)), ((w*z) < x)) ||
rewrite((((w + y)*z) < ((z*y) + x)), ((w*z) < x)) ||
rewrite((((y + w)*z) < (x + (y*z))), ((w*z) < x)) ||
rewrite((((y + w)*z) < (x + (z*y))), ((w*z) < x)) ||
rewrite((((y + w)*z) < ((x + (y*z)) + c0)), ((fold((1 - c0)) + (w*z)) <= x)) ||
rewrite((((y + w)*z) < ((y*z) + x)), ((w*z) < x)) ||
rewrite((((y + w)*z) < ((z*y) + x)), ((w*z) < x)) ||
rewrite((((z + y)*w) < ((x + ((y + z)*w)) + c0)), (fold((0 - c0)) < x)) ||
rewrite((((z + (x*c1))*c2) < (y + (x*c0))), ((z*c2) < y), ((c1*c2) == c0)) ||
rewrite((((z + (x*c1))*c2) < ((x*c0) + y)), ((z*c2) < y), ((c1*c2) == c0)) ||
rewrite((((z + (y*c1))*c1) < (x + (y*c0))), ((z*c1) < x), ((1 <= c0) && ((c1*c1) == c0))) ||
rewrite((((z + (y*c1))*c1) < ((y*c0) + x)), ((z*c1) < x), ((1 <= c0) && ((c1*c1) == c0))) ||
rewrite(((((y + z) + u)*w) < (x + (w*(z + y)))), ((u*w) < x)) ||
rewrite(((((y + z) + u)*w) < (x + ((z + y)*w))), ((u*w) < x)) ||
rewrite(((((y + z) + u)*w) < ((w*(z + y)) + x)), ((u*w) < x)) ||
rewrite(((((y + z) + u)*w) < (((z + y)*w) + x)), ((u*w) < x)) ||
rewrite(((((z + y) + u)*w) < (x + (w*(y + z)))), ((u*w) < x)) ||
rewrite(((((z + y) + u)*w) < (x + ((y + z)*w))), ((u*w) < x)) ||
rewrite(((((z + y) + u)*w) < ((w*(y + z)) + x)), ((u*w) < x)) ||
rewrite(((((z + y) + u)*w) < (((y + z)*w) + x)), ((u*w) < x)) ||
rewrite(((((x*c1) + z)*c2) < (y + (x*c0))), ((z*c2) < y), ((c1*c2) == c0)) ||
rewrite(((((x*c1) + z)*c2) < ((x*c0) + y)), ((z*c2) < y), ((c1*c2) == c0)) ||
rewrite(((((y*c1) + z)*c2) < (x + (y*c0))), ((z*c2) < x), ((c1*c2) == c0)) ||
rewrite(((((y*c1) + z)*c2) < ((y*c0) + x)), ((z*c2) < x), ((c1*c2) == c0)) ||
rewrite(((((x + c0)/c1)*c2) < min((x*c1), c3)), (x <= c2), ((((((((((((((((((-3 <= ((c0 + c1) + c2)) || (-2 <= (c0 + c1))) || (-2 <= (c0 + c2))) || (-2 <= ((c0 + c1) + c2))) || (-1 <= c0)) || (1 <= c2)) || ((((c0 + c1)*c2) + (c1 + c2)) <= (c1*c1))) || ((((c0 + c1)*c2) + (c1 + c2)) <= (c1*c3))) || ((((c0 + c1)*c2) + (c1 + c2)) <= (((c2 + 2)*c1)*c1))) || (((((c0 + c1) + c2)*c2) + ((c2*2) + c1)) <= (c1*c3))) || (((((c0 + c1) + c2)*c2) + ((c2*2) + c1)) <= (((c2 + 2)*c1)*c1))) || (((((c0 + c1)*c2) + ((c2*2) + c1)) + 1) <= (c1*c1))) || (((((c0 + c1)*c2) + ((c2*2) + c1)) + 1) <= (c1*c3))) || ((((((c0 + c1) + c2)*c2) + ((c2*3) + c1)) + 1) <= (c1*c3))) || ((((((c0 + c1) + c2)*c2) + ((c2*3) + c1)) + 1) <= (((c2 + 2)*c1)*c1))) || ((((c1 + 1)*c1) + -2) <= (c0 + c2))) || ((((c1 + 1)*c1) + -2) <= ((c0 + c1) + c2))) && (((((((((1 <= max(((c1*c1) + ((c0 + -1)*c2)), c2)) || (c0 <= 1)) || (c0 <= (c1*c1))) || ((((c1*c2)*c1) + 1) <= ((c0 + c2)*c2))) || ((c0 + c1) <= 0)) || ((c0 + c2) <= 0)) || (((c0 + c2) + 1) <= (c1*c1))) || (((c0 + c1) + c2) <= -1)) && (((((((-1 <= c2) || ((((((c0 + c1) + c2)*c2) + ((c2*2) + c1)) + 1) <= (c1*c3))) || ((((((c0 + c1) + c2)*c2) + ((c2*2) + c1)) + 1) <= (((c2 + 1)*c1)*c1))) || (((c1*c1) + -1) <= (c0 + c2))) || (((c1*c1) + -1) <= ((c0 + c1) + c2))) || (((c1*c1) + -2) <= ((c0 + c1) + c2))) && ((((((-1 <= (c0 + c1)) || (0 <= c0)) || (1 <= c2)) || ((((c0 + c1)*c2) + (c1 + c2)) <= -1)) || (((((c0 + c1)*c2) + (c1 + c2)) + 1) <= (c1*c3))) && ((((1 <= c2) || (((c1*c3) + 1) <= ((c0 + -1)*c2))) || (((c1*c3) + 1) <= ((c0 + c2)*c2))) && (max(c1, c2) <= -1))))))) ||
rewrite(((min(x, c1)*y) < min((x*y), c0)), false, ((((1 <= c1) || (c0 <= 0)) || (c1 <= -1)) && (max(c0, 0) <= c1))) ||
rewrite(((min(x, c2)*c0) < min((x*c0), c1)), false, ((((((c0 <= -1) || (c1 <= 1)) || (c1 <= c2)) || (c1 <= (c0*c2))) || (c2 <= 0)) && ((((((-1 <= c2) || (0 <= ((c2 + 1)*c0))) && (0 <= c2)) || (c0 <= -1)) || (c1 <= (c0*c2))) && ((((1 <= c2) || (c0 <= -1)) || (c1 <= 0)) || (c2 <= -1))))) ||
rewrite(((min(x, y)*c0) < min((x*c0), c1)), (y < min(0, x)), ((1 <= min((min(c1, 0) + c0), ((c0*2) + c1))) && (c1 <= 0))) ||
rewrite(((min(x, y)*y) < min((x*y), c0)), false, (c0 <= 1)) ||
rewrite(((min(x, y)*z) < min(u, min(w, (z*min(x, y))))), false) ||
rewrite(((min(x, y)*z) < min(u, min(w, (z*min(y, x))))), false) ||
rewrite(((min(x, y)*z) < min(u, min(w, (min(y, x)*z)))), false) ||
rewrite(((min(x, y)*z) < min(u, min((z*min(x, y)), w))), false) ||
rewrite(((min(x, y)*z) < min(u, min((z*min(y, x)), w))), false) ||
rewrite(((min(x, y)*z) < min(u, min((min(y, x)*z), w))), false) ||
rewrite(((min(x, y)*z) < min(min(w, (z*min(x, y))), u)), false) ||
rewrite(((min(x, y)*z) < min(min(w, (z*min(y, x))), u)), false) ||
rewrite(((min(x, y)*z) < min(min(w, (min(y, x)*z)), u)), false) ||
rewrite(((min(x, y)*z) < min(min((z*min(x, y)), w), u)), false) ||
rewrite(((min(x, y)*z) < min(min((z*min(y, x)), w), u)), false) ||
rewrite(((min(x, y)*z) < min(min((min(y, x)*z), w), u)), false) ||
rewrite(((min(y, x)*y) < min((x*y), c0)), false, (c0 <= 1)) ||
rewrite(((min(y, x)*z) < min(u, min(w, (z*min(x, y))))), false) ||
rewrite(((min(y, x)*z) < min(u, min(w, (z*min(y, x))))), false) ||
rewrite(((min(y, x)*z) < min(u, min(w, (min(x, y)*z)))), false) ||
rewrite(((min(y, x)*z) < min(u, min((z*min(x, y)), w))), false) ||
rewrite(((min(y, x)*z) < min(u, min((z*min(y, x)), w))), false) ||
rewrite(((min(y, x)*z) < min(u, min((min(x, y)*z), w))), false) ||
rewrite(((min(y, x)*z) < min(min(w, (z*min(x, y))), u)), false) ||
rewrite(((min(y, x)*z) < min(min(w, (z*min(y, x))), u)), false) ||
rewrite(((min(y, x)*z) < min(min(w, (min(x, y)*z)), u)), false) ||
rewrite(((min(y, x)*z) < min(min((z*min(x, y)), w), u)), false) ||
rewrite(((min(y, x)*z) < min(min((z*min(y, x)), w), u)), false) ||
rewrite(((min(y, x)*z) < min(min((min(x, y)*z), w), u)), false) ||
rewrite(((min(((x + c0)/c1), y)*c1) < x), ((y*c1) < x), (((1 <= c1) || (c0 <= -1)) && ((c1 <= -1) || ((c1 + -1) <= c0)))) ||
rewrite(((max(x, z)*c0) < min(y, (x*c0))), false, (1 <= c0)) ||
rewrite(((max(x, z)*c0) < min((x*c0), y)), false, (1 <= c0)) ||
rewrite(((max(z, x)*c0) < min(y, (x*c0))), false, (1 <= c0)) ||
rewrite(((max(z, x)*c0) < min((x*c0), y)), false, (1 <= c0)) ||
rewrite(((x/c0) < (min(x, y)/c0)), false, (1 <= c0)) ||
rewrite(((x/c0) < (min(y, x)/c0)), false, (1 <= c0)) ||
rewrite(((x/c1) < (((x + c0)/c1) + (y*c2))), (1 <= y), ((((((((c0 <= 1) || (c1 <= -1)) || (c2 <= -1)) || ((c0 + 1) <= (c1*2))) || ((c0 + 1) <= ((c2 + 1)*c1))) || ((c0 + 1) <= c1)) || ((((c1*c2) + c0) + 1) <= ((c2 + 1)*c1))) && (((((c0 <= max((c1*c2), 1)) || (c1 <= -1)) || (c2 <= -1)) || (c0 <= 0)) && ((((((-1 <= c0) || (1 <= c1)) || (c2 <= -1)) || ((c1*c2) <= c0)) || (0 <= c0)) && ((((((1 <= c1) || (c0 <= 0)) || (c2 <= -1)) || ((c0 + c1) <= -1)) || (((c1*c2) + c0) <= -1)) && (((c1 <= max(((c1*c2) + c0), -1)) || (c2 <= -1)) && ((((c0 <= 0) || (c1 <= -1)) || (c2 <= -1)) && ((((1 <= c1) || (c2 <= -1)) || (((c1*c2) + c0) <= c1)) && ((((0 <= c0) || (1 <= c1)) || (c2 <= -1)) && (1 <= c2)))))))))) ||
rewrite(((x/c1) < (((x - select(y, c0, c1))/c1) + c2)), false, ((((0 <= (((0 - c2)*c1) + c0)) || (c1 <= -1)) || ((c1*c2) <= c0)) && (((c1 <= -1) || (((c1*c2) + 1) <= (c0 + c1))) && (((1 <= c1) || (c0 <= (c1*c2))) && (c2 <= 1))))) ||
rewrite(((y/c0) < (min(x, y)/c0)), false, (0 <= c0)) ||
rewrite(((y/c0) < (min(y, x)/c0)), false, (0 <= c0)) ||
rewrite(((y/c2) < (((min(x, c0) + y) + c1)/c2)), false, (((max((c0 + c1), 0) + 1) <= c2) && ((c0 + c1) <= 0))) ||
rewrite((((x + c2)/c1) < (y + ((x + c0)/c1))), (c2 < y), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
rewrite((((x + c2)/c1) < (((x + c0)/c1) + y)), (c2 < y), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
rewrite((((x + z)/c0) < ((z + min(x, y))/c0)), false, (1 <= c0)) ||
rewrite((((x + z)/c0) < ((z + min(y, x))/c0)), false, (1 <= c0)) ||
rewrite((((x + z)/c0) < ((min(x, y) + z)/c0)), false, (1 <= c0)) ||
rewrite((((x + z)/c0) < ((min(y, x) + z)/c0)), false, (1 <= c0)) ||
rewrite((((x + select(y, c2, c3))/c1) < ((x + c0)/c1)), false, (((1 <= c1) || (max(c2, c3) <= c0)) && ((c1 <= -1) || (c0 <= min(c2, c3))))) ||
rewrite((((y + c0)/c1) < ((min(x, y) + c0)/c1)), false, (1 <= c1)) ||
rewrite((((y + c2)/c1) < (x + ((y + c0)/c1))), (c2 < x), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
rewrite((((y + c2)/c1) < (x + ((y + c0)/c1))), (c0 < x), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1 + 1)*c0) == c2))) ||
rewrite((((y + c2)/c1) < (((y + c0)/c1) + x)), (c2 < x), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1*c2) + c0) == c2))) ||
rewrite((((y + c2)/c1) < (((y + c0)/c1) + x)), (c0 < x), ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c1 + 1)*c0) == c2))) ||
rewrite((((y + z)/c0) < ((z + min(x, y))/c0)), false, (1 <= c0)) ||
rewrite((((y + z)/c0) < ((z + min(y, x))/c0)), false, (1 <= c0)) ||
rewrite((((y + z)/c0) < ((min(x, y) + z)/c0)), false, (1 <= c0)) ||
rewrite((((y + z)/c0) < ((min(y, x) + z)/c0)), false, (1 <= c0)) ||
rewrite((((y + min(x, c0))/c1) < (min(z, (y + min(x, c0)))/c1)), false, (1 <= c1)) ||
rewrite((((y + min(x, c0))/c1) < (min(z, (min(x, c0) + y))/c1)), false, (1 <= c1)) ||
rewrite((((y + min(x, c0))/c1) < (min((y + min(x, c0)), z)/c1)), false, (1 <= c1)) ||
rewrite((((y + min(x, c0))/c1) < (min((min(x, c0) + y), z)/c1)), false, (1 <= c1)) ||
rewrite((((z + x)/c0) < ((z + min(x, y))/c0)), false, (1 <= c0)) ||
rewrite((((z + x)/c0) < ((z + min(y, x))/c0)), false, (1 <= c0)) ||
rewrite((((z + x)/c0) < ((min(x, y) + z)/c0)), false, (1 <= c0)) ||
rewrite((((z + x)/c0) < ((min(y, x) + z)/c0)), false, (1 <= c0)) ||
rewrite((((z + y)/c0) < ((z + min(x, y))/c0)), false, (1 <= c0)) ||
rewrite((((z + y)/c0) < ((z + min(y, x))/c0)), false, (1 <= c0)) ||
rewrite((((z + y)/c0) < ((min(x, y) + z)/c0)), false, (1 <= c0)) ||
rewrite((((z + y)/c0) < ((min(y, x) + z)/c0)), false, (1 <= c0)) ||
rewrite((((z + min(x, y))/c0) < (min(w, (z + min(y, x)))/c0)), false, (1 <= c0)) ||
rewrite((((z + min(x, y))/c0) < (min(w, (min(x, y) + z))/c0)), false, (1 <= c0)) ||
rewrite((((z + min(x, y))/c0) < (min(w, (min(y, x) + z))/c0)), false, (1 <= c0)) ||
rewrite((((z + min(x, y))/c0) < (min((z + min(y, x)), w)/c0)), false, (1 <= c0)) ||
rewrite((((z + min(x, y))/c0) < (min((min(x, y) + z), w)/c0)), false, (1 <= c0)) ||
rewrite((((z + min(x, y))/c0) < (min((min(y, x) + z), w)/c0)), false, (1 <= c0)) ||
rewrite((((z + min(y, x))/c0) < (min(w, (z + min(x, y)))/c0)), false, (1 <= c0)) ||
rewrite((((z + min(y, x))/c0) < (min(w, (min(x, y) + z))/c0)), false, (1 <= c0)) ||
rewrite((((z + min(y, x))/c0) < (min(w, (min(y, x) + z))/c0)), false, (1 <= c0)) ||
rewrite((((z + min(y, x))/c0) < (min((z + min(x, y)), w)/c0)), false, (1 <= c0)) ||
rewrite((((z + min(y, x))/c0) < (min((min(x, y) + z), w)/c0)), false, (1 <= c0)) ||
rewrite((((z + min(y, x))/c0) < (min((min(y, x) + z), w)/c0)), false, (1 <= c0)) ||
rewrite(((((x*c0) + c3)/c2) < (((x*c0) + c1)/c2)), false, (((1 <= c2) || (c3 <= c1)) && ((c1 <= c3) || (c2 <= -1)))) ||
rewrite((((min(x, c0) + y)/c1) < (min(z, (y + min(x, c0)))/c1)), false, (1 <= c1)) ||
rewrite((((min(x, c0) + y)/c1) < (min(z, (min(x, c0) + y))/c1)), false, (1 <= c1)) ||
rewrite((((min(x, c0) + y)/c1) < (min((y + min(x, c0)), z)/c1)), false, (1 <= c1)) ||
rewrite((((min(x, c0) + y)/c1) < (min((min(x, c0) + y), z)/c1)), false, (1 <= c1)) ||
rewrite((((min(x, y) + z)/c0) < (min(w, (z + min(x, y)))/c0)), false, (1 <= c0)) ||
rewrite((((min(x, y) + z)/c0) < (min(w, (z + min(y, x)))/c0)), false, (1 <= c0)) ||
rewrite((((min(x, y) + z)/c0) < (min(w, (min(y, x) + z))/c0)), false, (1 <= c0)) ||
rewrite((((min(x, y) + z)/c0) < (min((z + min(x, y)), w)/c0)), false, (1 <= c0)) ||
rewrite((((min(x, y) + z)/c0) < (min((z + min(y, x)), w)/c0)), false, (1 <= c0)) ||
rewrite((((min(x, y) + z)/c0) < (min((min(y, x) + z), w)/c0)), false, (1 <= c0)) ||
rewrite((((min(y, x) + z)/c0) < (min(w, (z + min(x, y)))/c0)), false, (1 <= c0)) ||
rewrite((((min(y, x) + z)/c0) < (min(w, (z + min(y, x)))/c0)), false, (1 <= c0)) ||
rewrite((((min(y, x) + z)/c0) < (min(w, (min(x, y) + z))/c0)), false, (1 <= c0)) ||
rewrite((((min(y, x) + z)/c0) < (min((z + min(x, y)), w)/c0)), false, (1 <= c0)) ||
rewrite((((min(y, x) + z)/c0) < (min((z + min(y, x)), w)/c0)), false, (1 <= c0)) ||
rewrite((((min(y, x) + z)/c0) < (min((min(x, y) + z), w)/c0)), false, (1 <= c0)) ||
rewrite((((select(y, c2, c3) + x)/c1) < ((x + c0)/c1)), false, (((1 <= c1) || (max(c2, c3) <= c0)) && ((c1 <= -1) || (c0 <= min(c2, c3))))) ||
rewrite((((c2 - x)/c1) < ((c0 - x)/c1)), false, (((1 <= c1) || (c2 <= c0)) && ((c0 <= c2) || (c1 <= -1)))) ||
rewrite((((x*c2)/c3) < ((x*c0)/c1)), false, ((1 <= c1) && ((c1*c2) == (c0*c3)))) ||
rewrite(((max(x, y)/c0) < (x/c0)), false, (1 <= c0)) ||
rewrite(((max(y, x)/c0) < (x/c0)), false, (1 <= c0)) ||
rewrite((min(w, z) < min(w, min(z, min(x, y)))), false) ||
rewrite((min(w, z) < min(w, min(z, min(y, x)))), false) ||
rewrite((min(w, z) < min(w, min(min(x, y), z))), false) ||
rewrite((min(w, z) < min(w, min(min(y, x), z))), false) ||
rewrite((min(w, z) < min(min(z, min(x, y)), w)), false) ||
rewrite((min(w, z) < min(min(z, min(y, x)), w)), false) ||
rewrite((min(w, z) < min(min(min(x, y), z), w)), false) ||
rewrite((min(w, z) < min(min(min(y, x), z), w)), false) ||
rewrite((min(w, min(x, (min(y, z) + c0))) < y), true, (c0 <= -1)) ||
rewrite((min(w, min(z, (min(x, y) + c0))) < x), true, (c0 <= -1)) ||
rewrite((min(w, min((min(x, y) + c0), z)) < x), true, (c0 <= -1)) ||
rewrite((min(w, min((min(y, z) + c0), x)) < y), true, (c0 <= -1)) ||
rewrite((min(x, c1) < min(min(x, y), c0)), false, (c0 < (c1 + 1))) ||
rewrite((min(x, c1) < min(min(min(x, y), z), c0)), false, (c0 <= c1)) ||
rewrite((min(x, c2) < (((min(x, c0) + c1)/c2)*c2)), false, (((((c2 <= -1) || ((c0 + c1) <= 1)) || ((c0 + c1) <= c2)) || (((c0 + c1) + 1) <= (c2*2))) && ((((1 <= c2) || ((c0 + c1) <= -1)) || ((c0 + c1) <= c2)) && (c1 <= 0)))) ||
rewrite((min(x, c3) < ((((x + c0)/c1)*c1) + c2)), (fold(max((c3 - (c0 + c2)), ((c3 - ((c0 + c1) + c2)) + -1))) < x), ((((c1 <= -1) && ((c3 + -1) <= (max((c3 - (c0 + c2)), ((c3 - ((c0 + c1) + c2)) + -1)) + ((c0 + c1) + c2)))) && ((c0 + c2) <= 0)) && (-1 <= c1))) ||
rewrite((min(x, c3) < (((min(x, c0)/c1)*c1) + c2)), false, (((((1 <= max(c0, c1)) || (c0 <= -1)) || ((c2 + -1) <= c3)) || ((c1 + c2) <= c3)) && ((((((0 <= c0) || (1 <= c1)) || ((c2 + -1) <= c3)) || ((c0 + c2) <= c3)) || ((c1 + c2) <= c3)) && (((((c1 <= -1) || ((c2 + -1) <= c3)) || (c2 <= (c1 + c3))) || ((((c0 + c2) <= c3) || ((min(c1, 1) + -1) <= c0)) && (((0 <= c0) || ((c0 + c2) <= c3)) && ((1 <= c0) || (c0 <= -1))))) && ((((1 <= max(c0, c1)) || (c0 <= -1)) || (c1 <= -1)) && ((((1 <= c0) || (c0 <= -1)) || (c2 <= c3)) && ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && ((((((1 <= c1) || ((c2 + -1) <= c3)) || ((c1 + c2) <= c3)) && (((c1 <= -1) || ((c2 + -1) <= c3)) || (c2 <= (c1 + c3)))) || (((c0 + c2) + -2) <= c3)) && (((c0 <= 0) || ((c0 + c2) <= c3)) && (c2 <= 0)))))))))) ||
rewrite((min(x, c3) < (((min(x, c0)/c1)*c1) + c2)), false, (((((c1 <= -1) || ((c2 + -1) <= c3)) || (c2 <= (c1 + c3))) || (((0 <= c0) || ((c0 + c2) <= c3)) && ((1 <= c0) || (c0 <= -1)))) && (((((1 <= c1) || ((c2 + -1) <= c3)) || ((c1 + c2) <= c3)) || (((0 <= c0) || ((c0 + c2) <= c3)) && ((1 <= c0) || (c0 <= -1)))) && ((((1 <= max(c0, c1)) || (c0 <= -1)) || (c1 <= -1)) && ((((c0 <= 0) || (((c0 + c2) + -2) <= c3)) || ((((1 <= c1) || ((c2 + -1) <= c3)) || ((c1 + c2) <= c3)) && (((c1 <= -1) || ((c2 + -1) <= c3)) || (c2 <= (c1 + c3))))) && ((((1 <= c0) || (c0 <= -1)) || (c2 <= c3)) && ((((1 <= c1) || (c0 == 0)) || (c1 <= -1)) && (((c0 <= 0) || ((c0 + c2) <= c3)) && (c2 <= 0))))))))) ||
rewrite((min(x, y) < min(x, y)), false) ||
rewrite((min(x, y) < min(x, (y + c0))), false, (c0 <= 0)) ||
rewrite((min(x, y) < min(y, x)), false) ||
rewrite((min(x, y) < min((y + c0), x)), false, (c0 <= 0)) ||
rewrite((min(x, z) < (min(min((x + c0), y), z) + c1)), false, ((max(c0, 0) + c1) <= 0)) ||
rewrite((min(x, z) < min(x, y)), (z < min(x, y))) ||
rewrite((min(x, z) < min(y, x)), (z < min(y, x))) ||
rewrite((min(x, (y + c1)) < (min((x + c0), y) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((min(x, (y + c2)) < (min(x, (y + c0)) + c1)), false, ((c1 <= 0) && ((c0 + c1) <= c2))) ||
rewrite((min(x, (z + c0)) < min(z, min(x, y))), false, (0 <= c0)) ||
rewrite((min(x, (z + c0)) < min(z, min(y, x))), false, (0 <= c0)) ||
rewrite((min(x, (z + c0)) < min(min(x, y), z)), false, (0 <= c0)) ||
rewrite((min(x, (z + c0)) < min(min(y, x), z)), false, (0 <= c0)) ||
rewrite((min(x, (z + c1)) < min(min(x, y), (z + c0))), false, (c0 <= c1)) ||
rewrite((min(x, (z + c1)) < min(min(y, x), (z + c0))), false, (c0 <= c1)) ||
rewrite((min(y, c1) < min(min(x, y), c0)), false, (c0 < (c1 + 1))) ||
rewrite((min(y, c1) < min(min(min(x, y), z), c0)), false, (c0 <= c1)) ||
rewrite((min(y, c3) < (min(min(x, (y + c0)), c1) + c2)), false, (((c0 + c2) <= 0) && ((c1 + c2) < (c3 + 1)))) ||
rewrite((min(y, x) < min(x, y)), false) ||
rewrite((min(y, x) < min(x, (y + c0))), false, (c0 <= 0)) ||
rewrite((min(y, x) < min(y, x)), false) ||
rewrite((min(y, x) < min((y + c0), x)), false, (c0 <= 0)) ||
rewrite((min(y, z) < (min(x, y) + c0)), (z < (min(x, y) + c0)), (c0 <= 0)) ||
rewrite((min(y, z) < min(x, y)), (z < min(x, y))) ||
rewrite((min(y, z) < min(y, x)), (z < min(y, x))) ||
rewrite((min(y, (x + c0)) < min(w, min(z, min(x, y)))), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(w, min(z, min(y, x)))), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(w, min(min(x, y), z))), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(w, min(min(y, x), z))), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(z, min(x, y))), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(z, min(y, x))), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(min(x, y), z)), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(min(y, x), z)), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(min(z, min(x, y)), w)), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(min(z, min(y, x)), w)), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(min(min(x, y), z), w)), false, (0 <= c0)) ||
rewrite((min(y, (x + c0)) < min(min(min(y, x), z), w)), false, (0 <= c0)) ||
rewrite((min(y, (x + c1)) < (min(x, (y + c0)) + c1)), false, ((c0 + c1) <= 0)) ||
rewrite((min(y, (x + c1)) < min(min(x, y), c0)), false, (0 <= c1)) ||
rewrite((min(y, (x + c2)) < (min((x + c0), y) + c1)), false, ((c1 <= 0) && ((c0 + c1) < (c2 + 1)))) ||
rewrite((min(y, (z + c0)) < min(w, min(z, min(x, y)))), false, (0 <= c0)) ||
rewrite((min(y, (z + c0)) < min(w, min(z, min(y, x)))), false, (0 <= c0)) ||
rewrite((min(y, (z + c0)) < min(w, min(min(x, y), z))), false, (0 <= c0)) ||
rewrite((min(y, (z + c0)) < min(w, min(min(y, x), z))), false, (0 <= c0)) ||
rewrite((min(y, (z + c0)) < min(min(z, min(x, y)), w)), false, (0 <= c0)) ||
rewrite((min(y, (z + c0)) < min(min(z, min(y, x)), w)), false, (0 <= c0)) ||
rewrite((min(y, (z + c0)) < min(min(min(x, y), z), w)), false, (0 <= c0)) ||
rewrite((min(y, (z + c0)) < min(min(min(y, x), z), w)), false, (0 <= c0)) ||
rewrite((min(y, (z + c1)) < min(min(x, y), (z + c0))), false, (c0 <= c1)) ||
rewrite((min(y, (z + c1)) < min(min(y, x), (z + c0))), false, (c0 <= c1)) ||
rewrite((min(y, (((x + c1)/c2)*c2)) < (x + c0)), ((fold((1 - c0)) + y) <= x), (((c0 + -1) <= (c1 + c2)) && (((c0 + c2) + -1) <= c1))) ||
rewrite((min(z, w) < min(w, min(z, min(x, y)))), false) ||
rewrite((min(z, w) < min(w, min(z, min(y, x)))), false) ||
rewrite((min(z, w) < min(w, min(min(x, y), z))), false) ||
rewrite((min(z, w) < min(w, min(min(y, x), z))), false) ||
rewrite((min(z, w) < min(min(z, min(x, y)), w)), false) ||
rewrite((min(z, w) < min(min(z, min(y, x)), w)), false) ||
rewrite((min(z, w) < min(min(min(x, y), z), w)), false) ||
rewrite((min(z, w) < min(min(min(y, x), z), w)), false) ||
rewrite((min(z, x) < (min(min((x + c0), y), z) + c1)), false, ((max(c0, 0) + c1) <= 0)) ||
rewrite((min(z, x) < min(x, y)), (z < min(x, y))) ||
rewrite((min(z, x) < min(y, x)), (z < min(y, x))) ||
rewrite((min(z, y) < (min(x, y) + c0)), (z < (min(x, y) + c0)), (c0 <= 0)) ||
rewrite((min(z, y) < min(x, y)), (z < min(x, y))) ||
rewrite((min(z, y) < min(y, x)), (z < min(y, x))) ||
rewrite((min(z, (w + y)) < (x + y)), (min(w, (z - y)) < x)) ||
rewrite((min(z, (w + y)) < (y + x)), (min(w, (z - y)) < x)) ||
rewrite((min(z, (y + c0)) < min(x, y)), (z < min(x, y)), (0 <= c0)) ||
rewrite((min(z, (y + c0)) < min(y, x)), (z < min(y, x)), (0 <= c0)) ||
rewrite((min(z, (y + c1)) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 < (min(c1, 1) + 1)))) ||
rewrite((min(z, (y + c1)) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 <= min(c1, 1)))) ||
rewrite((min(z, (y + w)) < (x + y)), (min(w, (z - y)) < x)) ||
rewrite((min(z, (y + w)) < (y + x)), (min(w, (z - y)) < x)) ||
rewrite((min(z, min(x, y)) < x), (min(z, y) < x)) ||
rewrite((min(z, min(x, (y + c0))) < y), true, (c0 <= -1)) ||
rewrite((min(z, min(y, x)) < x), (min(z, y) < x)) ||
rewrite((min(z, min(y, (x + c0))) < x), true, (c0 <= -1)) ||
rewrite((min(z, min((x + c0), y)) < x), true, (c0 <= -1)) ||
rewrite((min(z, min((y + c0), x)) < y), true, (c0 <= -1)) ||
rewrite((min((w + y), z) < (x + y)), (min((z - y), w) < x)) ||
rewrite((min((w + y), z) < (y + x)), (min((z - y), w) < x)) ||
rewrite((min((x + c0), y) < min(w, min(z, min(x, y)))), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(w, min(z, min(y, x)))), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(w, min(min(x, y), z))), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(w, min(min(y, x), z))), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(z, min(x, y))), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(z, min(y, x))), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(min(x, y), z)), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(min(y, x), z)), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(min(z, min(x, y)), w)), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(min(z, min(y, x)), w)), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(min(min(x, y), z), w)), false, (0 <= c0)) ||
rewrite((min((x + c0), y) < min(min(min(y, x), z), w)), false, (0 <= c0)) ||
rewrite((min((x + c1), y) < min(min(x, y), c0)), false, (0 <= c1)) ||
rewrite((min((x + c2), y) < (min((x + c0), y) + c1)), false, ((c1 <= 0) && ((c0 + c1) < (c2 + 1)))) ||
rewrite((min((y + c0), z) < min(x, y)), (z < min(x, y)), (0 <= c0)) ||
rewrite((min((y + c0), z) < min(y, x)), (z < min(y, x)), (0 <= c0)) ||
rewrite((min((y + c1), z) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 < (min(c1, 1) + 1)))) ||
rewrite((min((y + c1), z) < (min(x, y) + c0)), (z <= min(x, y)), ((1 <= c0) && (c0 <= min(c1, 1)))) ||
rewrite((min((y + c2), x) < (min(x, (y + c0)) + c1)), false, ((c1 <= 0) && ((c0 + c1) <= c2))) ||
rewrite((min((y + w), z) < (x + y)), (min((z - y), w) < x)) ||
rewrite((min((y + w), z) < (y + x)), (min((z - y), w) < x)) ||
rewrite((min((z + c0), y) < min(w, min(z, min(x, y)))), false, (0 <= c0)) ||
rewrite((min((z + c0), y) < min(w, min(z, min(y, x)))), false, (0 <= c0)) ||
rewrite((min((z + c0), y) < min(w, min(min(x, y), z))), false, (0 <= c0)) ||
rewrite((min((z + c0), y) < min(w, min(min(y, x), z))), false, (0 <= c0)) ||
rewrite((min((z + c0), y) < min(min(z, min(x, y)), w)), false, (0 <= c0)) ||
rewrite((min((z + c0), y) < min(min(z, min(y, x)), w)), false, (0 <= c0)) ||
rewrite((min((z + c0), y) < min(min(min(x, y), z), w)), false, (0 <= c0)) ||
rewrite((min((z + c0), y) < min(min(min(y, x), z), w)), false, (0 <= c0)) ||
rewrite((min((z + c1), x) < min(min(x, y), (z + c0))), false, (c0 <= c1)) ||
rewrite((min((z + c1), x) < min(min(y, x), (z + c0))), false, (c0 <= c1)) ||
rewrite((min((z + c1), y) < min(min(x, y), (z + c0))), false, (c0 <= c1)) ||
rewrite((min((z + c1), y) < min(min(y, x), (z + c0))), false, (c0 <= c1)) ||
rewrite((min((x*c1), c3) < ((min(x, c0)*c1) + c2)), false, ((((1 <= c0) || (c0 <= -1)) || (c2 <= (min(c1, 0) + c3))) && (((c0 <= 0) || (c2 <= c3)) && (((1 <= c1) && (c2 <= 0)) && (((c0*c1) + c2) <= ((min(c1, 0)*2) + c3)))))) ||
rewrite((min((x*c2), c3) < (min((x*c0), c1)*c1)), false, (((((c0 <= -1) || (c1 <= 1)) || (c1 <= c0)) || (c2 <= -1)) && ((((1 <= max(c0, c2)) || (c1 <= 1)) || ((c0 + c1) <= 0)) && ((((c0 <= -1) || (c2 <= 1)) || (c2 <= (c0*c1))) && (((min(c0, c2) <= -1) || ((c1*c1) <= c2)) && (((1 <= max(c0, c2)) || (((c1*c1) + c2) <= 0)) && ((((-1 <= c2) || (1 <= c0)) || ((c0*c1) <= c2)) && (((1 <= c2) || (c0 <= -1)) && (((1 <= c0) || (c2 <= -1)) && (((0 <= c3) && (1 <= c1)) && ((c1*c1) <= c3))))))))))) ||
rewrite((min((x*c2), c3) < (min((x*c0), y)*c1)), (fold((1 - min(c0, 0))) <= min(x, y)), (((((((0 <= c3) && (1 <= c0)) && (1 <= c2)) && (c2 <= (c0*c1))) && ((c3 + 1) <= (c0*c1))) && ((c3 + 1) <= (c1*2))) && ((max(c3, 0) + 1) <= c1))) ||
rewrite((min((x*c2), c3) < (min((x*c0), y)*c1)), (0 < min(x, y)), (((0 <= c3) || (c0 <= -1)) && ((((((0 <= (min((c0*c1), c1) + c3)) && (1 <= c0)) && (1 <= c2)) && (c2 <= (c0*c1))) && ((c3 + 1) <= (c0*c1))) && ((max(c3, 0) + 1) <= c1)))) ||
rewrite((min((((x + c1)/c2)*c2), y) < (x + c0)), ((fold((1 - c0)) + y) <= x), (((c0 + -1) <= (c1 + c2)) && (((c0 + c2) + -1) <= c1))) ||
rewrite((min(min(x, y), c0) < (x + c1)), true, (1 <= c1)) ||
rewrite((min(min(x, y), z) < x), (min(y, z) < x)) ||
rewrite((min(min(x, (y + c0)), z) < y), true, (c0 <= -1)) ||
rewrite((min(min(x, (min(y, z) + c0)), w) < y), true, (c0 <= -1)) ||
rewrite((min(min(y, x), z) < x), (min(y, z) < x)) ||
rewrite((min(min(y, (x + c0)), z) < x), true, (c0 <= -1)) ||
rewrite((min(min(z, (min(x, y) + c0)), w) < x), true, (c0 <= -1)) ||
rewrite((min(min((x + c0), y), z) < x), true, (c0 <= -1)) ||
rewrite((min(min((y + c0), x), z) < y), true, (c0 <= -1)) ||
rewrite((min(min((min(x, y) + c0), z), w) < x), true, (c0 <= -1)) ||
rewrite((min(min((min(y, z) + c0), x), w) < y), true, (c0 <= -1)) ||
rewrite((max(u, (w + z)) < (w + min(z, min(x, y)))), false) ||
rewrite((max(u, (w + z)) < (w + min(z, min(y, x)))), false) ||
rewrite((max(u, (w + z)) < (w + min(min(x, y), z))), false) ||
rewrite((max(u, (w + z)) < (w + min(min(y, x), z))), false) ||
rewrite((max(u, (w + z)) < (min(z, min(x, y)) + w)), false) ||
rewrite((max(u, (w + z)) < (min(z, min(y, x)) + w)), false) ||
rewrite((max(u, (w + z)) < (min(min(x, y), z) + w)), false) ||
rewrite((max(u, (w + z)) < (min(min(y, x), z) + w)), false) ||
rewrite((max(u, (z + w)) < (w + min(z, min(x, y)))), false) ||
rewrite((max(u, (z + w)) < (w + min(z, min(y, x)))), false) ||
rewrite((max(u, (z + w)) < (w + min(min(x, y), z))), false) ||
rewrite((max(u, (z + w)) < (w + min(min(y, x), z))), false) ||
rewrite((max(u, (z + w)) < (min(z, min(x, y)) + w)), false) ||
rewrite((max(u, (z + w)) < (min(z, min(y, x)) + w)), false) ||
rewrite((max(u, (z + w)) < (min(min(x, y), z) + w)), false) ||
rewrite((max(u, (z + w)) < (min(min(y, x), z) + w)), false) ||
rewrite((max(u, (z + max(w, (x + y)))) < (z + (x + y))), false) ||
rewrite((max(u, (z + max(w, (x + y)))) < (z + (y + x))), false) ||
rewrite((max(u, (z + max(w, (x + y)))) < ((x + y) + z)), false) ||
rewrite((max(u, (z + max(w, (x + y)))) < ((y + x) + z)), false) ||
rewrite((max(u, (z + max(w, (y + x)))) < (z + (x + y))), false) ||
rewrite((max(u, (z + max(w, (y + x)))) < (z + (y + x))), false) ||
rewrite((max(u, (z + max(w, (y + x)))) < ((x + y) + z)), false) ||
rewrite((max(u, (z + max(w, (y + x)))) < ((y + x) + z)), false) ||
rewrite((max(u, (z + max((x + y), w))) < (z + (x + y))), false) ||
rewrite((max(u, (z + max((x + y), w))) < (z + (y + x))), false) ||
rewrite((max(u, (z + max((x + y), w))) < ((x + y) + z)), false) ||
rewrite((max(u, (z + max((x + y), w))) < ((y + x) + z)), false) ||
rewrite((max(u, (z + max((y + x), w))) < (z + (x + y))), false) ||
rewrite((max(u, (z + max((y + x), w))) < (z + (y + x))), false) ||
rewrite((max(u, (z + max((y + x), w))) < ((x + y) + z)), false) ||
rewrite((max(u, (z + max((y + x), w))) < ((y + x) + z)), false) ||
rewrite((max(u, (max(w, z) + (x + y))) < (z + (x + y))), false) ||
rewrite((max(u, (max(w, z) + (x + y))) < (z + (y + x))), false) ||
rewrite((max(u, (max(w, z) + (x + y))) < ((x + y) + z)), false) ||
rewrite((max(u, (max(w, z) + (x + y))) < ((y + x) + z)), false) ||
rewrite((max(u, (max(w, z) + (y + x))) < (z + (x + y))), false) ||
rewrite((max(u, (max(w, z) + (y + x))) < (z + (y + x))), false) ||
rewrite((max(u, (max(w, z) + (y + x))) < ((x + y) + z)), false) ||
rewrite((max(u, (max(w, z) + (y + x))) < ((y + x) + z)), false) ||
rewrite((max(u, (max(w, (x + y)) + z)) < (z + (x + y))), false) ||
rewrite((max(u, (max(w, (x + y)) + z)) < (z + (y + x))), false) ||
rewrite((max(u, (max(w, (x + y)) + z)) < ((x + y) + z)), false) ||
rewrite((max(u, (max(w, (x + y)) + z)) < ((y + x) + z)), false) ||
rewrite((max(u, (max(w, (y + x)) + z)) < (z + (x + y))), false) ||
rewrite((max(u, (max(w, (y + x)) + z)) < (z + (y + x))), false) ||
rewrite((max(u, (max(w, (y + x)) + z)) < ((x + y) + z)), false) ||
rewrite((max(u, (max(w, (y + x)) + z)) < ((y + x) + z)), false) ||
rewrite((max(u, (max(z, w) + (x + y))) < (z + (x + y))), false) ||
rewrite((max(u, (max(z, w) + (x + y))) < (z + (y + x))), false) ||
rewrite((max(u, (max(z, w) + (x + y))) < ((x + y) + z)), false) ||
rewrite((max(u, (max(z, w) + (x + y))) < ((y + x) + z)), false) ||
rewrite((max(u, (max(z, w) + (y + x))) < (z + (x + y))), false) ||
rewrite((max(u, (max(z, w) + (y + x))) < (z + (y + x))), false) ||
rewrite((max(u, (max(z, w) + (y + x))) < ((x + y) + z)), false) ||
rewrite((max(u, (max(z, w) + (y + x))) < ((y + x) + z)), false) ||
rewrite((max(u, (max((x + y), w) + z)) < (z + (x + y))), false) ||
rewrite((max(u, (max((x + y), w) + z)) < (z + (y + x))), false) ||
rewrite((max(u, (max((x + y), w) + z)) < ((x + y) + z)), false) ||
rewrite((max(u, (max((x + y), w) + z)) < ((y + x) + z)), false) ||
rewrite((max(u, (max((y + x), w) + z)) < (z + (x + y))), false) ||
rewrite((max(u, (max((y + x), w) + z)) < (z + (y + x))), false) ||
rewrite((max(u, (max((y + x), w) + z)) < ((x + y) + z)), false) ||
rewrite((max(u, (max((y + x), w) + z)) < ((y + x) + z)), false) ||
rewrite((max(w, (x + max(y, z))) < (x + y)), false) ||
rewrite((max(w, (x + max(y, z))) < (y + x)), false) ||
rewrite((max(w, (x + max(z, y))) < (x + y)), false) ||
rewrite((max(w, (x + max(z, y))) < (y + x)), false) ||
rewrite((max(w, (y + z)) < (z + (y + min(x, c0)))), false, (c0 <= 0)) ||
rewrite((max(w, (y + z)) < (z + (min(x, c0) + y))), false, (c0 <= 0)) ||
rewrite((max(w, (y + z)) < (z + min(x, y))), false) ||
rewrite((max(w, (y + z)) < (z + min(y, x))), false) ||
rewrite((max(w, (y + z)) < ((y + min(x, c0)) + z)), false, (c0 <= 0)) ||
rewrite((max(w, (y + z)) < ((min(x, c0) + y) + z)), false, (c0 <= 0)) ||
rewrite((max(w, (y + z)) < (min(x, y) + z)), false) ||
rewrite((max(w, (y + z)) < (min(y, x) + z)), false) ||
rewrite((max(w, (y + max(x, z))) < (x + y)), false) ||
rewrite((max(w, (y + max(x, z))) < (y + x)), false) ||
rewrite((max(w, (y + max(z, x))) < (x + y)), false) ||
rewrite((max(w, (y + max(z, x))) < (y + x)), false) ||
rewrite((max(w, (z + y)) < (z + (y + min(x, c0)))), false, (c0 <= 0)) ||
rewrite((max(w, (z + y)) < (z + (min(x, c0) + y))), false, (c0 <= 0)) ||
rewrite((max(w, (z + y)) < (z + min(x, y))), false) ||
rewrite((max(w, (z + y)) < (z + min(y, x))), false) ||
rewrite((max(w, (z + y)) < ((y + min(x, c0)) + z)), false, (c0 <= 0)) ||
rewrite((max(w, (z + y)) < ((min(x, c0) + y) + z)), false, (c0 <= 0)) ||
rewrite((max(w, (z + y)) < (min(x, y) + z)), false) ||
rewrite((max(w, (z + y)) < (min(y, x) + z)), false) ||
rewrite((max(w, (max(x, z) + y)) < (x + y)), false) ||
rewrite((max(w, (max(x, z) + y)) < (y + x)), false) ||
rewrite((max(w, (max(y, z) + x)) < (x + y)), false) ||
rewrite((max(w, (max(y, z) + x)) < (y + x)), false) ||
rewrite((max(w, (max(z, x) + y)) < (x + y)), false) ||
rewrite((max(w, (max(z, x) + y)) < (y + x)), false) ||
rewrite((max(w, (max(z, y) + x)) < (x + y)), false) ||
rewrite((max(w, (max(z, y) + x)) < (y + x)), false) ||
rewrite((max(x, c0) < ((max((x*c1), c2) + c3)/c1)), true, (((c1 <= (c2 + c3)) || (c2 <= 0)) && (((1 <= c2) || (((c0 + 1)*c1) <= c3)) && (((1 <= c1) && (c1 <= c3)) && (((c0 + 1)*c1) <= (c2 + c3)))))) ||
rewrite((max(x, z) < min(x, y)), false) ||
rewrite((max(x, z) < min(y, x)), false) ||
rewrite((max(x, z) < max(x, y)), (max(x, z) < y)) ||
rewrite((max(x, z) < max(y, x)), (max(x, z) < y)) ||
rewrite((max(y, z) < (min(x, y) + c0)), false, (c0 <= 0)) ||
rewrite((max(y, z) < (max(x, y) + c0)), ((max(y, z) + fold(max((0 - min(c0, 0)), (1 - c0)))) <= x), (c0 <= 0)) ||
rewrite((max(y, z) < min(x, y)), false) ||
rewrite((max(y, z) < min(y, x)), false) ||
rewrite((max(y, z) < max(x, y)), (max(y, z) < x)) ||
rewrite((max(y, z) < max(y, x)), (max(y, z) < x)) ||
rewrite((max(y, (x + c2)) < (max((x + c0), y) + c1)), false, ((c1 <= 0) && ((c0 + c1) <= c2))) ||
rewrite((max(z, x) < min(x, y)), false) ||
rewrite((max(z, x) < min(y, x)), false) ||
rewrite((max(z, x) < max(x, y)), (max(z, x) < y)) ||
rewrite((max(z, x) < max(y, x)), (max(z, x) < y)) ||
rewrite((max(z, y) < (min(x, y) + c0)), false, (c0 <= 0)) ||
rewrite((max(z, y) < (max(x, y) + c0)), ((max(z, y) + fold(max((0 - min(c0, 0)), (1 - c0)))) <= x), (c0 <= 0)) ||
rewrite((max(z, y) < min(x, y)), false) ||
rewrite((max(z, y) < min(y, x)), false) ||
rewrite((max(z, y) < max(x, y)), (max(z, y) < x)) ||
rewrite((max(z, y) < max(y, x)), (max(z, y) < x)) ||
rewrite((max(z, (x + max(w, y))) < (x + y)), false) ||
rewrite((max(z, (x + max(w, y))) < (y + x)), false) ||
rewrite((max(z, (x + max(y, w))) < (x + y)), false) ||
rewrite((max(z, (x + max(y, w))) < (y + x)), false) ||
rewrite((max(z, (y + max(w, x))) < (x + y)), false) ||
rewrite((max(z, (y + max(w, x))) < (y + x)), false) ||
rewrite((max(z, (y + max(x, w))) < (x + y)), false) ||
rewrite((max(z, (y + max(x, w))) < (y + x)), false) ||
rewrite((max(z, (max(w, x) + y)) < (x + y)), false) ||
rewrite((max(z, (max(w, x) + y)) < (y + x)), false) ||
rewrite((max(z, (max(w, y) + x)) < (x + y)), false) ||
rewrite((max(z, (max(w, y) + x)) < (y + x)), false) ||
rewrite((max(z, (max(x, w) + y)) < (x + y)), false) ||
rewrite((max(z, (max(x, w) + y)) < (y + x)), false) ||
rewrite((max(z, (max(y, w) + x)) < (x + y)), false) ||
rewrite((max(z, (max(y, w) + x)) < (y + x)), false) ||
rewrite((max(z, min(x, (y + c0))) < min(x, (y + c0))), false) ||
rewrite((max(z, min(x, (y + c0))) < min((y + c0), x)), false) ||
rewrite((max(z, min((y + c0), x)) < min(x, (y + c0))), false) ||
rewrite((max(z, min((y + c0), x)) < min((y + c0), x)), false) ||
rewrite((max(z, max(x, y)) < x), false) ||
rewrite((max(z, max(y, x)) < x), false) ||
rewrite((max((w + z), u) < (w + min(z, min(x, y)))), false) ||
rewrite((max((w + z), u) < (w + min(z, min(y, x)))), false) ||
rewrite((max((w + z), u) < (w + min(min(x, y), z))), false) ||
rewrite((max((w + z), u) < (w + min(min(y, x), z))), false) ||
rewrite((max((w + z), u) < (min(z, min(x, y)) + w)), false) ||
rewrite((max((w + z), u) < (min(z, min(y, x)) + w)), false) ||
rewrite((max((w + z), u) < (min(min(x, y), z) + w)), false) ||
rewrite((max((w + z), u) < (min(min(y, x), z) + w)), false) ||
rewrite((max((x + c2), y) < (max((x + c0), y) + c1)), false, ((c1 <= 0) && ((c0 + c1) <= c2))) ||
rewrite((max((x + max(w, y)), z) < (x + y)), false) ||
rewrite((max((x + max(w, y)), z) < (y + x)), false) ||
rewrite((max((x + max(y, w)), z) < (x + y)), false) ||
rewrite((max((x + max(y, w)), z) < (y + x)), false) ||
rewrite((max((x + max(y, z)), w) < (x + y)), false) ||
rewrite((max((x + max(y, z)), w) < (y + x)), false) ||
rewrite((max((x + max(z, y)), w) < (x + y)), false) ||
rewrite((max((x + max(z, y)), w) < (y + x)), false) ||
rewrite((max((y + c0), z) < (min(x, y) + c0)), false) ||
rewrite((max((y + z), w) < (z + (y + min(x, c0)))), false, (c0 <= 0)) ||
rewrite((max((y + z), w) < (z + (min(x, c0) + y))), false, (c0 <= 0)) ||
rewrite((max((y + z), w) < (z + min(x, y))), false) ||
rewrite((max((y + z), w) < (z + min(y, x))), false) ||
rewrite((max((y + z), w) < ((y + min(x, c0)) + z)), false, (c0 <= 0)) ||
rewrite((max((y + z), w) < ((min(x, c0) + y) + z)), false, (c0 <= 0)) ||
rewrite((max((y + z), w) < (min(x, y) + z)), false) ||
rewrite((max((y + z), w) < (min(y, x) + z)), false) ||
rewrite((max((y + max(w, x)), z) < (x + y)), false) ||
rewrite((max((y + max(w, x)), z) < (y + x)), false) ||
rewrite((max((y + max(x, w)), z) < (x + y)), false) ||
rewrite((max((y + max(x, w)), z) < (y + x)), false) ||
rewrite((max((y + max(x, z)), w) < (x + y)), false) ||
rewrite((max((y + max(x, z)), w) < (y + x)), false) ||
rewrite((max((y + max(z, x)), w) < (x + y)), false) ||
rewrite((max((y + max(z, x)), w) < (y + x)), false) ||
rewrite((max((z + y), w) < (z + (y + min(x, c0)))), false, (c0 <= 0)) ||
rewrite((max((z + y), w) < (z + (min(x, c0) + y))), false, (c0 <= 0)) ||
rewrite((max((z + y), w) < (z + min(x, y))), false) ||
rewrite((max((z + y), w) < (z + min(y, x))), false) ||
rewrite((max((z + y), w) < ((y + min(x, c0)) + z)), false, (c0 <= 0)) ||
rewrite((max((z + y), w) < ((min(x, c0) + y) + z)), false, (c0 <= 0)) ||
rewrite((max((z + y), w) < (min(x, y) + z)), false) ||
rewrite((max((z + y), w) < (min(y, x) + z)), false) ||
rewrite((max((z + max(w, (x + y))), u) < (z + (y + x))), false) ||
rewrite((max((z + max(w, (x + y))), u) < ((y + x) + z)), false) ||
rewrite((max((z + max(w, (y + x))), u) < (z + (x + y))), false) ||
rewrite((max((z + max(w, (y + x))), u) < ((x + y) + z)), false) ||
rewrite((max((z + max((x + y), w)), u) < (z + (y + x))), false) ||
rewrite((max((z + max((x + y), w)), u) < ((y + x) + z)), false) ||
rewrite((max((z + max((y + x), w)), u) < (z + (x + y))), false) ||
rewrite((max((z + max((y + x), w)), u) < ((x + y) + z)), false) ||
rewrite((max((max(w, x) + y), z) < (x + y)), false) ||
rewrite((max((max(w, x) + y), z) < (y + x)), false) ||
rewrite((max((max(w, y) + x), z) < (x + y)), false) ||
rewrite((max((max(w, y) + x), z) < (y + x)), false) ||
rewrite((max((max(w, z) + (x + y)), u) < (z + (y + x))), false) ||
rewrite((max((max(w, z) + (x + y)), u) < ((y + x) + z)), false) ||
rewrite((max((max(w, z) + (y + x)), u) < (z + (x + y))), false) ||
rewrite((max((max(w, z) + (y + x)), u) < ((x + y) + z)), false) ||
rewrite((max((max(w, (x + y)) + z), u) < (z + (y + x))), false) ||
rewrite((max((max(w, (x + y)) + z), u) < ((y + x) + z)), false) ||
rewrite((max((max(w, (y + x)) + z), u) < (z + (x + y))), false) ||
rewrite((max((max(w, (y + x)) + z), u) < ((x + y) + z)), false) ||
rewrite((max((max(x, w) + y), z) < (x + y)), false) ||
rewrite((max((max(x, w) + y), z) < (y + x)), false) ||
rewrite((max((max(x, z) + y), w) < (x + y)), false) ||
rewrite((max((max(x, z) + y), w) < (y + x)), false) ||
rewrite((max((max(y, w) + x), z) < (x + y)), false) ||
rewrite((max((max(y, w) + x), z) < (y + x)), false) ||
rewrite((max((max(y, z) + x), w) < (x + y)), false) ||
rewrite((max((max(y, z) + x), w) < (y + x)), false) ||
rewrite((max((max(z, w) + (x + y)), u) < (z + (y + x))), false) ||
rewrite((max((max(z, w) + (x + y)), u) < ((y + x) + z)), false) ||
rewrite((max((max(z, w) + (y + x)), u) < (z + (x + y))), false) ||
rewrite((max((max(z, w) + (y + x)), u) < ((x + y) + z)), false) ||
rewrite((max((max(z, x) + y), w) < (x + y)), false) ||
rewrite((max((max(z, x) + y), w) < (y + x)), false) ||
rewrite((max((max(z, y) + x), w) < (x + y)), false) ||
rewrite((max((max(z, y) + x), w) < (y + x)), false) ||
rewrite((max((max((x + y), w) + z), u) < (z + (y + x))), false) ||
rewrite((max((max((x + y), w) + z), u) < ((y + x) + z)), false) ||
rewrite((max((max((y + x), w) + z), u) < (z + (x + y))), false) ||
rewrite((max((max((y + x), w) + z), u) < ((x + y) + z)), false) ||
rewrite((max((z - y), c2) < (max((x - y), c0) + 1)), (max(z, y) <= x), ((max(c0, c2) <= 0) && ((max(c0, -1) + 1) <= c2))) ||
rewrite((max((x*c0), c1) < (max(x, y)*c0)), (max(0, x) < y), (((0 <= c1) && ((c1 + 1) <= (c0*2))) && ((max(c1, 0) + 1) <= c0))) ||
rewrite((max((x*c1), c2) < (max(x, c0)*c1)), false, (((((((-1 <= c2) || (0 <= c0)) || (c0 <= c2)) || (c1 <= -1)) && (((c0 <= 0) || (c1 <= -1)) && (((c0 <= 1) || (c1 <= -1)) || ((c0*c1) <= c1)))) || ((c0*c1) <= c2)) && ((((0 <= c2) || (1 <= c0)) || (c0 <= -1)) || (c1 <= -1)))) ||
rewrite((max(((x + c2)/c0), c3) < max((x/c0), c1)), false, ((((c0 < ((c0 + c2) + 1)) || (c1 < (c3 + 1))) || (0 <= c2)) && (((0 <= c2) || (c0 < ((c0 + c2) + 1))) && (((c1 < (c3 + 1)) && (((c1 + 1)*c0) < (((c0*c3) + ((c0*2) + c2)) + 1))) && (0 <= c0))))) ||
rewrite((max(((x*c0)/c2), c3) < (((x*c0) + c1)/c2)), false, (((0 <= c1) || (1 <= c2)) && ((c1 <= 0) || (c2 <= -1)))) ||
rewrite((max(min(x, y), (w + min(z, c0))) < min(x, y)), false) ||
rewrite((max(min(x, y), (w + min(z, c0))) < min(y, x)), false) ||
rewrite((max(min(x, y), (min(z, c0) + w)) < min(x, y)), false) ||
rewrite((max(min(x, y), (min(z, c0) + w)) < min(y, x)), false) ||
rewrite((max(min(x, (y + c0)), z) < min(x, (y + c0))), false) ||
rewrite((max(min(x, (y + c0)), z) < min((y + c0), x)), false) ||
rewrite((max(min(y, x), (w + min(z, c0))) < min(x, y)), false) ||
rewrite((max(min(y, x), (w + min(z, c0))) < min(y, x)), false) ||
rewrite((max(min(y, x), (min(z, c0) + w)) < min(x, y)), false) ||
rewrite((max(min(y, x), (min(z, c0) + w)) < min(y, x)), false) ||
rewrite((max(min((y + c0), x), z) < min(x, (y + c0))), false) ||
rewrite((max(min((y + c0), x), z) < min((y + c0), x)), false) ||
rewrite((max(max(x, y), z) < x), false) ||
rewrite((max(max(y, x), z) < x), false) ||
rewrite((select((x < (y + c1)), min((y - x), c2), c0) < c0), false, ((c0 <= c2) && ((c0 + c1) <= 1))) ||
rewrite((select((y < z), c1, min(x, c0)) < min(x, c0)), false, (c0 <= c1)) ||
rewrite((select((y < z), min(x, c0), c1) < min(x, c0)), false, (c0 <= c1)) ||
rewrite((select((z < x), y, (y + c0)) < (y + min(x, c0))), false, (c0 <= 0)) ||
rewrite((select((z < x), y, (y + c0)) < (min(x, c0) + y)), false, (c0 <= 0)) ||
rewrite((select((min(x, c0) < (y + c1)), c2, min(x, c0)) < min(x, c0)), false, (c0 <= c2)) ||
