rewrite((u + (x - (y + (z + (u + w))))), (x - (y + (z + w)))) ||
rewrite((u + (x - (y + (z + (w + u))))), (x - (y + (z + w)))) ||
rewrite((u + (x - (y + ((u + w) + z)))), (x - (y + (w + z)))) ||
rewrite((u + (x - (y + ((w + u) + z)))), (x - (y + (w + z)))) ||
rewrite((u + (x - ((z + (u + w)) + y))), (x - (z + (w + y)))) ||
rewrite((u + (x - ((z + (w + u)) + y))), (x - (z + (w + y)))) ||
rewrite((u + (x - (((u + w) + z) + y))), (x - ((w + z) + y))) ||
rewrite((u + (x - (((w + u) + z) + y))), (x - ((w + z) + y))) ||
rewrite((u + (x - select(y, (x + z), w))), (u - select(y, z, (w - x)))) ||
rewrite((u + (x - select(y, (z + x), w))), (u - select(y, z, (w - x)))) ||
rewrite((w + (x - select(y, z, (w + x)))), select(y, (x + (w - z)), 0)) ||
rewrite((w + (x - select(y, z, (x + w)))), select(y, (x - (z - w)), 0)) ||
rewrite((w + (x - select(y, (w + z), u))), (x - select(y, z, (u - w)))) ||
rewrite((w + (x - select(y, (z + w), u))), (x - select(y, z, (u - w)))) ||
rewrite((x + (w + (y - (x + z)))), (w + (y - z))) ||
rewrite((x + (w + (y - (z + x)))), (w + (y - z))) ||
rewrite((x + (y + (z - x))), (y + z)) ||
rewrite((x + (y + (z - (w + (u + x))))), (y + (z - (w + u)))) ||
rewrite((x + (y + (z - (w + (x + u))))), (y + (z - (w + u)))) ||
rewrite((x + (y + (z - ((u + x) + w)))), (y + (z - (u + w)))) ||
rewrite((x + (y + (z - ((x + u) + w)))), (y + (z - (u + w)))) ||
rewrite((x + (y + (x*c0))), (y + (fold((c0 + 1))*x))) ||
rewrite((x + ((y - (x + z)) + w)), ((y - z) + w)) ||
rewrite((x + ((y - (z + x)) + w)), ((y - z) + w)) ||
rewrite((x + ((z - x) + y)), (z + y)) ||
rewrite((x + ((z - (w + (u + x))) + y)), ((z - (w + u)) + y)) ||
rewrite((x + ((z - (w + (x + u))) + y)), ((z - (w + u)) + y)) ||
rewrite((x + ((z - ((u + x) + w)) + y)), ((z - (u + w)) + y)) ||
rewrite((x + ((z - ((x + u) + w)) + y)), ((z - (u + w)) + y)) ||
rewrite((x + ((x*c0) + y)), (y + (x*fold((c0 + 1))))) ||
rewrite((x + (y - (x + z))), (y - z)) ||
rewrite((x + (y - (z + x))), (y - z)) ||
rewrite((x + (y - (z + (w + x)))), (y - (z + w))) ||
rewrite((x + (y - (z + (x + w)))), (y - (z + w))) ||
rewrite((x + (y - ((w + x) + z))), (y - (w + z))) ||
rewrite((x + (y - ((x + w) + z))), (y - (w + z))) ||
rewrite((x + (y - select(z, w, (u + x)))), (y - select(z, (w - x), u))) ||
rewrite((x + (y - select(z, w, (u + y)))), (x - select(z, (w - y), u))) ||
rewrite((x + (y - select(z, w, (x + u)))), (y - select(z, (w - x), u))) ||
rewrite((x + (y - select(z, w, (y + u)))), (x - select(z, (w - y), u))) ||
rewrite((x + (y - select(z, (w + x), u))), (y - select(z, w, (u - x)))) ||
rewrite((x + (y - select(z, (w + y), u))), (x - select(z, w, (u - y)))) ||
rewrite((x + (y - select(z, (x + w), u))), (y - select(z, w, (u - x)))) ||
rewrite((x + (y - select(z, (y + w), u))), (x - select(z, w, (u - y)))) ||
rewrite((x + ((y - x) - z)), (y - z)) ||
rewrite((x + ((y - (x + z)) - w)), ((y - z) - w)) ||
rewrite((x + ((y - (z + x)) - w)), (y - (z + w))) ||
rewrite((x + min(y, (c0 - max(x, c1)))), min((x + min(y, fold((c0 - c1)))), c0)) ||
rewrite((x + min((c0 - max(x, c1)), y)), min((x + min(fold((c0 - c1)), y)), c0)) ||
rewrite((y + (w + min(z, (x - y)))), (w + min(x, (z + y)))) ||
rewrite((y + (w + min((x - y), z))), (w + min((z + y), x))) ||
rewrite((y + (y + (x*2))), (2*(y + x))) ||
rewrite((y + ((x*2) + y)), (2*(x + y))) ||
rewrite((y + (min(z, (x - y)) + w)), (min(x, (z + y)) + w)) ||
rewrite((y + (min((x - y), z) + w)), (min((z + y), x) + w)) ||
rewrite((y + (x - min(w, (y + z)))), (x - min(z, (w - y)))) ||
rewrite((y + (x - min(w, (z + y)))), (x - min(z, (w - y)))) ||
rewrite((y + (x - min((y + z), w))), (x - min((w - y), z))) ||
rewrite((y + (x - min((z + y), w))), (x - min((w - y), z))) ||
rewrite((y + (y*((x + c0)/c1))), (y*((x + fold((c0 + c1)))/c1))) ||
rewrite((y + (((x + c0)/c1)*y)), (((x + fold((c0 + c1)))/c1)*y)) ||
rewrite((y + (min(((x - y)*c0), c1)/c0)), min((0 + y), x), (((-1 <= (c0 + c1)) && (0 <= c1)) && ((max(c1, 0) + 1) <= c0))) ||
rewrite((y + min(w, (z + (x - y)))), min((x + z), (w + y))) ||
rewrite((y + min(w, ((x - y) + z))), min((y + w), (x + z))) ||
rewrite((y + min(w, ((x - y) - z))), min((w + y), (x - z))) ||
rewrite((y + min((z + (x - y)), w)), min((z + x), (y + w))) ||
rewrite((y + min(((x - y) + z), w)), min((w + y), (z + x))) ||
rewrite((y + min(((x - y) - z), w)), min((x - z), (y + w))) ||
rewrite((y + max(w, (z + (x - y)))), max((x + z), (w + y))) ||
rewrite((y + max(w, ((x - y) + z))), max((y + w), (x + z))) ||
rewrite((y + max((z + (x - y)), w)), max((z + x), (y + w))) ||
rewrite((y + max(((x - y) + z), w)), max((w + y), (z + x))) ||
rewrite((z + (x + (z*((y + c0)/c1)))), (x + (((y + fold((c0 + c1)))/c1)*z))) ||
rewrite((z + (x + (((y + c0)/c1)*z))), (x + (((y + fold((c0 + c1)))/c1)*z))) ||
rewrite((z + ((z*((y + c0)/c1)) + x)), ((z*((y + fold((c0 + c1)))/c1)) + x)) ||
rewrite((z + ((((y + c0)/c1)*z) + x)), ((((y + fold((c0 + c1)))/c1)*z) + x)) ||
rewrite((z + (x - (y + z))), (x - y)) ||
rewrite((z + (x - (z + y))), (x - y)) ||
rewrite((z + (z*min(y, (x + -1)))), (z*min(x, (y + 1)))) ||
rewrite((z + (z*min((x + -1), y))), (z*min((y + 1), x))) ||
rewrite((z + (min(y, (x + -1))*z)), (min(x, (y + 1))*z)) ||
rewrite((z + (min((x + -1), y)*z)), (min((y + 1), x)*z)) ||
rewrite((z + min(w, min(x, ((y - z) + c0)))), min((z + min(w, x)), (y + c0))) ||
rewrite((z + min(w, min(((y - z) + c0), x))), min((z + min(w, x)), (y + c0))) ||
rewrite((z + min(min(x, ((y - z) + c0)), w)), min((min(x, w) + z), (c0 + y))) ||
rewrite((z + min(min(((y - z) + c0), x), w)), min((min(x, w) + z), (c0 + y))) ||
rewrite((z + max(x, (y - (w + z)))), max((z + x), (y - w))) ||
rewrite((z + max(x, (y - (z + w)))), max((y - w), (z + x))) ||
rewrite((z + max((y - (w + z)), x)), max((y - w), (z + x))) ||
rewrite((z + max((y - (z + w)), x)), max((y - w), (z + x))) ||
rewrite(((w + (x - (y + z))) + (u + z)), ((w + (x - y)) + u)) ||
rewrite(((w + (x - (y + z))) + (z + u)), ((w + (x - y)) + u)) ||
rewrite(((w + (x - (z + y))) + (u + z)), ((w + (x - y)) + u)) ||
rewrite(((w + (x - (z + y))) + (z + u)), ((w + (x - y)) + u)) ||
rewrite(((w + (y - (x + z))) + x), (w + (y - z))) ||
rewrite(((w + (y - (z + x))) + x), (w + (y - z))) ||
rewrite(((w + min(z, (x - y))) + y), (w + min(x, (z + y)))) ||
rewrite(((w + min((x - y), z)) + y), (w + min((z + y), x))) ||
rewrite(((x + (z*((y + c0)/c1))) + z), (x + (z*((y + fold((c0 + c1)))/c1)))) ||
rewrite(((x + (((y + c0)/c1)*z)) + z), ((((y + fold((c0 + c1)))/c1)*z) + x)) ||
rewrite(((y + (z - x)) + x), (y + z)) ||
rewrite(((y + (z - (w + (u + x)))) + x), (y + (z - (w + u)))) ||
rewrite(((y + (z - (w + (x + u)))) + x), (y + (z - (w + u)))) ||
rewrite(((y + (z - ((u + x) + w))) + x), (y + (z - (u + w)))) ||
rewrite(((y + (z - ((x + u) + w))) + x), (y + (z - (u + w)))) ||
rewrite(((y + (x*2)) + y), ((y + x)*2)) ||
rewrite(((y + (x*c0)) + x), (y + (x*fold((c0 + 1))))) ||
rewrite((((x - (y + z)) + w) + (u + z)), (((x - y) + w) + u)) ||
rewrite((((x - (y + z)) + w) + (z + u)), (((x - y) + w) + u)) ||
rewrite((((x - (z + y)) + w) + (u + z)), (((x - y) + w) + u)) ||
rewrite((((x - (z + y)) + w) + (z + u)), (((x - y) + w) + u)) ||
rewrite((((y - (x + z)) + w) + x), ((y - z) + w)) ||
rewrite((((y - (z + x)) + w) + x), ((y - z) + w)) ||
rewrite((((z - x) + y) + x), (z + y)) ||
rewrite((((z - (w + (u + x))) + y) + x), ((z - (w + u)) + y)) ||
rewrite((((z - (w + (x + u))) + y) + x), ((z - (w + u)) + y)) ||
rewrite((((z - ((u + x) + w)) + y) + x), ((z - (u + w)) + y)) ||
rewrite((((z - ((x + u) + w)) + y) + x), ((z - (u + w)) + y)) ||
rewrite((((x*2) + y) + y), ((x + y)*2)) ||
rewrite((((x*c0) + y) + x), ((x*fold((c0 + 1))) + y)) ||
rewrite((((x*y) + ((z*y) + w)) + c0), (((y*(x + z)) + w) + c0)) ||
rewrite((((z*((y + c0)/c1)) + x) + z), ((z*((y + fold((c0 + c1)))/c1)) + x)) ||
rewrite(((((x + c0)*y) + (x*c1)) + c2), ((x + c0)*(c1 + y)), (((((c0 == 0) || (c2 == 0)) || (1 <= c1)) || (c1 <= -1)) && ((((1 <= c0) || (c2 == 0)) || (c0 <= -1)) && ((c0*c1) == c2)))) ||
rewrite((((((y + c0)/c1)*z) + x) + z), ((z*((y + fold((c0 + c1)))/c1)) + x)) ||
rewrite(((((c0 - x)/c1) + y) + c2), (((fold(((c1*c2) + c0)) - x)/c1) + y), (((1 <= c1) || (c2 == 0)) || (c1 <= -1))) ||
rewrite(((min(z, (x - y)) + w) + y), (min(x, (z + y)) + w)) ||
rewrite(((min((x - y), z) + w) + y), (min((z + y), x) + w)) ||
rewrite(((min((x*c0), (y + c1)) + z) + c2), (min(((x*c0) + c2), y) + z), ((c1 + c2) == 0)) ||
rewrite(((select(x, (y + c0), y) + z) + c1), ((select(x, fold((c0 + c1)), c1) + z) + y)) ||
rewrite(((x - y) + (y + z)), (x + z)) ||
rewrite(((x - y) + (z + y)), (x + z)) ||
rewrite(((x - y) + (z + (w + y))), (x + (z + w))) ||
rewrite(((x - y) + (z + (y + w))), (x + (z + w))) ||
rewrite(((x - y) + ((w + y) + z)), (x + (w + z))) ||
rewrite(((x - y) + ((y + w) + z)), (x + (w + z))) ||
rewrite(((x - (u + (y + (w + z)))) + (w + z)), (x - (u + y))) ||
rewrite(((x - (u + (y + (w + z)))) + (z + w)), (x - (u + y))) ||
rewrite(((x - (u + (y + (z + w)))) + (w + z)), (x - (u + y))) ||
rewrite(((x - (u + (y + (z + w)))) + (z + w)), (x - (u + y))) ||
rewrite(((x - (u + ((w + z) + y))) + (w + z)), (x - (u + y))) ||
rewrite(((x - (u + ((w + z) + y))) + (z + w)), (x - (u + y))) ||
rewrite(((x - (u + ((z + w) + y))) + (w + z)), (x - (u + y))) ||
rewrite(((x - (u + ((z + w) + y))) + (z + w)), (x - (u + y))) ||
rewrite(((x - (y + z)) + z), (x - y)) ||
rewrite(((x - (y + z)) + (w + z)), ((x - y) + w)) ||
rewrite(((x - (y + z)) + (w + (u + y))), ((x - z) + (w + u))) ||
rewrite(((x - (y + z)) + (w + (y + u))), ((x - z) + (w + u))) ||
rewrite(((x - (y + z)) + (z + w)), ((x - y) + w)) ||
rewrite(((x - (y + z)) + ((u + y) + w)), ((x - z) + (u + w))) ||
rewrite(((x - (y + z)) + ((y + u) + w)), ((x - z) + (u + w))) ||
rewrite(((x - (y + (z + (u + w)))) + u), (x - (y + (z + w)))) ||
rewrite(((x - (y + (z + (w + u)))) + u), (x - (y + (z + w)))) ||
rewrite(((x - (y + ((u + w) + z))) + u), (x - (y + (w + z)))) ||
rewrite(((x - (y + ((w + u) + z))) + u), (x - (y + (w + z)))) ||
rewrite(((x - (z + y)) + z), (x - y)) ||
rewrite(((x - (z + y)) + (w + z)), ((x - y) + w)) ||
rewrite(((x - (z + y)) + (w + (u + y))), ((x - z) + (w + u))) ||
rewrite(((x - (z + y)) + (w + (y + u))), ((x - z) + (w + u))) ||
rewrite(((x - (z + y)) + (z + w)), ((x - y) + w)) ||
rewrite(((x - (z + y)) + ((u + y) + w)), ((x - z) + (u + w))) ||
rewrite(((x - (z + y)) + ((y + u) + w)), ((x - z) + (u + w))) ||
rewrite(((x - ((y + (w + z)) + u)) + (w + z)), (x - (y + u))) ||
rewrite(((x - ((y + (w + z)) + u)) + (z + w)), (x - (y + u))) ||
rewrite(((x - ((y + (z + w)) + u)) + (w + z)), (x - (y + u))) ||
rewrite(((x - ((y + (z + w)) + u)) + (z + w)), (x - (y + u))) ||
rewrite(((x - ((z + (u + w)) + y)) + u), (x - (z + (w + y)))) ||
rewrite(((x - ((z + (w + u)) + y)) + u), (x - (z + (w + y)))) ||
rewrite(((x - (((u + w) + z) + y)) + u), (x - ((w + z) + y))) ||
rewrite(((x - (((w + u) + z) + y)) + u), (x - ((w + z) + y))) ||
rewrite(((x - (((w + z) + y) + u)) + (w + z)), (x - (y + u))) ||
rewrite(((x - (((w + z) + y) + u)) + (z + w)), (x - (y + u))) ||
rewrite(((x - (((z + w) + y) + u)) + (w + z)), (x - (y + u))) ||
rewrite(((x - (((z + w) + y) + u)) + (z + w)), (x - (y + u))) ||
rewrite(((x - ((y + c0)/c1)) + c2), (x + ((fold(max((-1 - (((-1 - c2)*c1) + c0)), (((c1*c2) - (c0 + c1)) + -1))) - y)/c1)), ((((-2 <= c1) || (1 <= c2)) || (c2 <= -1)) && ((((1 <= c1) || (c2 == 0)) || (c1 <= -1)) && (((0 <= c2) || (c1 <= 3)) && ((c1 <= 2) || (c2 <= 0)))))) ||
rewrite(((x - min(w, (y + z))) + y), (x - min(z, (w - y)))) ||
rewrite(((x - min(w, (z + y))) + y), (x - min(z, (w - y)))) ||
rewrite(((x - min((y + z), w)) + y), (x - min((w - y), z))) ||
rewrite(((x - min((z + y), w)) + y), (x - min((w - y), z))) ||
rewrite(((x - select(y, z, (w + x))) + w), select(y, ((w + x) - z), 0)) ||
rewrite(((x - select(y, z, (x + w))) + w), select(y, ((x + w) - z), 0)) ||
rewrite(((x - select(y, (w + z), u)) + w), (x - select(y, z, (u - w)))) ||
rewrite(((x - select(y, (x + z), w)) + u), (u - select(y, z, (w - x)))) ||
rewrite(((x - select(y, (z + w), u)) + w), (x - select(y, z, (u - w)))) ||
rewrite(((x - select(y, (z + x), w)) + u), (u - select(y, z, (w - x)))) ||
rewrite(((y - (x + z)) + x), (y - z)) ||
rewrite(((y - (z + x)) + x), (y - z)) ||
rewrite(((y - (z + (w + x))) + x), (y - (z + w))) ||
rewrite(((y - (z + (x + w))) + x), (y - (z + w))) ||
rewrite(((y - ((w + x) + z)) + x), (y - (w + z))) ||
rewrite(((y - ((x + w) + z)) + x), (y - (w + z))) ||
rewrite(((y - select(z, w, (u + x))) + x), (y - select(z, (w - x), u))) ||
rewrite(((y - select(z, w, (u + y))) + x), (x - select(z, (w - y), u))) ||
rewrite(((y - select(z, w, (x + u))) + x), (y - select(z, (w - x), u))) ||
rewrite(((y - select(z, w, (y + u))) + x), (x - select(z, (w - y), u))) ||
rewrite(((y - select(z, (w + x), u)) + x), (y - select(z, w, (u - x)))) ||
rewrite(((y - select(z, (w + y), u)) + x), (x - select(z, w, (u - y)))) ||
rewrite(((y - select(z, (x + w), u)) + x), (y - select(z, w, (u - x)))) ||
rewrite(((y - select(z, (y + w), u)) + x), (x - select(z, w, (u - y)))) ||
rewrite((((x + y) - min(y, c0)) + c1), ((fold((c1 - c0)) + max(y, c0)) + x)) ||
rewrite((((((x + c0)/c1) + y) - ((x + z)/c1)) + c2), ((((x + fold(((c1*c2) + c0)))/c1) - ((x + z)/c1)) + y), (((1 <= c1) || (c2 == 0)) || (c1 <= -1))) ||
rewrite((((0 - x) - (y*c1)) + c2), ((c2 - x) - (y*c1))) ||
rewrite((((x - (y + z)) - w) + (u + y)), (((x - z) - w) + u)) ||
rewrite((((x - (y + z)) - w) + (y + u)), (((x - z) - w) + u)) ||
rewrite((((x - (z + y)) - w) + (u + y)), (((x - z) - w) + u)) ||
rewrite((((x - (z + y)) - w) + (y + u)), (((x - z) - w) + u)) ||
rewrite((((y - x) - z) + x), (y - z)) ||
rewrite((((y - (x + z)) - w) + x), ((y - z) - w)) ||
rewrite((((y - (z + x)) - w) + x), (y - (z + w))) ||
rewrite(((((x + c0)/c1) - y) + c2), (((x + fold(((c1*c2) + c0)))/c1) - y), (((1 <= c1) || (c2 == 0)) || (c1 <= -1))) ||
rewrite(((x*c0) + (y + (x*c1))), ((x*fold((c0 + c1))) + y)) ||
rewrite(((x*c0) + ((x*c1) + y)), ((x*fold((c0 + c1))) + y)) ||
rewrite(((x*c0) + (((y - (x*c1))*c2) - z)), ((y*c2) - z), ((c1*c2) == c0)) ||
rewrite(((x*y) + (z + (w*y))), (z + ((w + x)*y))) ||
rewrite(((x*y) + (z + (y*w))), (z + ((x + w)*y))) ||
rewrite(((x*y) + ((w*y) + z)), ((y*(x + w)) + z)) ||
rewrite(((x*y) + ((y*w) + z)), ((y*(x + w)) + z)) ||
rewrite(((x*y) + (z - (w + (y*(u + x))))), (z - (w + (y*u)))) ||
rewrite(((x*y) + (z - (w + (y*(x + u))))), (z - (w + (y*u)))) ||
rewrite(((x*y) + (z - (w + ((u + x)*y)))), (z - (w + (u*y)))) ||
rewrite(((x*y) + (z - (w + ((x + u)*y)))), (z - (w + (u*y)))) ||
rewrite(((x*y) + (z - ((y*(u + x)) + w))), (z - ((y*u) + w))) ||
rewrite(((x*y) + (z - ((y*(x + u)) + w))), (z - ((y*u) + w))) ||
rewrite(((x*y) + (z - (((u + x)*y) + w))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - (((x + u)*y) + w))), (z - ((u*y) + w))) ||
rewrite(((x*y) + (z - (w*y))), (z + ((x - w)*y))) ||
rewrite(((x*y) + (z - (y*w))), (z + ((x - w)*y))) ||
rewrite(((x*y) + ((z - (w*y)) - u)), ((z + ((x - w)*y)) - u)) ||
rewrite(((x*y) + ((z - (y*w)) - u)), ((z + ((x - w)*y)) - u)) ||
rewrite(((x*y) + ((w*(y*z)) - u)), ((y*(x + (w*z))) - u)) ||
rewrite(((x*y) + ((w*(z*y)) - u)), ((y*(x + (w*z))) - u)) ||
rewrite(((x*y) + ((y*z) - w)), ((y*(z + x)) - w)) ||
rewrite(((x*y) + ((z*y) - w)), ((y*(x + z)) - w)) ||
rewrite(((x*y) + (((y*z)*w) - u)), ((y*(x + (w*z))) - u)) ||
rewrite(((x*y) + (((z*y)*w) - u)), ((y*(x + (z*w))) - u)) ||
rewrite(((y*x) + (z + (w*y))), (z + ((x + w)*y))) ||
rewrite(((y*x) + (z + (y*w))), ((y*(x + w)) + z)) ||
rewrite(((y*x) + ((w*y) + z)), (((x + w)*y) + z)) ||
rewrite(((y*x) + ((y*w) + z)), ((y*(w + x)) + z)) ||
rewrite(((y*x) + (z - (w + (y*(u + x))))), (z - (w + (y*u)))) ||
rewrite(((y*x) + (z - (w + (y*(x + u))))), (z - (w + (y*u)))) ||
rewrite(((y*x) + (z - (w + ((u + x)*y)))), (z - (w + (u*y)))) ||
rewrite(((y*x) + (z - (w + ((x + u)*y)))), (z - (w + (u*y)))) ||
rewrite(((y*x) + (z - ((y*(u + x)) + w))), (z - ((y*u) + w))) ||
rewrite(((y*x) + (z - ((y*(x + u)) + w))), (z - ((y*u) + w))) ||
rewrite(((y*x) + (z - (((u + x)*y) + w))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - (((x + u)*y) + w))), (z - ((u*y) + w))) ||
rewrite(((y*x) + (z - (w*y))), (z + ((x - w)*y))) ||
rewrite(((y*x) + (z - (y*w))), (z + ((x - w)*y))) ||
rewrite(((y*x) + ((z - (w*y)) - u)), ((z + ((x - w)*y)) - u)) ||
rewrite(((y*x) + ((z - (y*w)) - u)), ((z + ((x - w)*y)) - u)) ||
rewrite(((y*x) + ((w*(y*z)) - u)), ((y*(x + (w*z))) - u)) ||
rewrite(((y*x) + ((w*(z*y)) - u)), ((y*(x + (w*z))) - u)) ||
rewrite(((y*x) + ((y*z) - w)), ((y*(z + x)) - w)) ||
rewrite(((y*x) + ((z*y) - w)), ((y*(x + z)) - w)) ||
rewrite(((y*x) + (((y*z)*w) - u)), ((y*(x + (w*z))) - u)) ||
rewrite(((y*x) + (((z*y)*w) - u)), ((y*(x + (z*w))) - u)) ||
rewrite(((y*((x + c0)/c1)) + y), (y*((x + fold((c0 + c1)))/c1))) ||
rewrite(((z*(x*y)) + (w*y)), (((x*z) + w)*y)) ||
rewrite(((z*(x*y)) + (y*w)), (y*((x*z) + w))) ||
rewrite(((z*(y*x)) + (w*y)), (((z*x) + w)*y)) ||
rewrite(((z*(y*x)) + (y*w)), (y*((z*x) + w))) ||
rewrite(((z*min(y, (x + -1))) + z), (z*min(x, (y + 1)))) ||
rewrite(((z*min((x + -1), y)) + z), (z*min((y + 1), x))) ||
rewrite((((x + ((y*-1) - z))*c1) + c2), (((x - (y + z))*c1) + c2)) ||
rewrite((((x - (y*c0))*c1) + (z + (y*c2))), ((x*c1) + z), ((c0*c1) == c2)) ||
rewrite((((x - (y*c0))*c1) + ((y*c2) + z)), ((x*c1) + z), ((c0*c1) == c2)) ||
rewrite((((x*y)*z) + (w*y)), (((x*z) + w)*y)) ||
rewrite((((x*y)*z) + (y*w)), (((x*z) + w)*y)) ||
rewrite((((y*x)*z) + (w*y)), (((x*z) + w)*y)) ||
rewrite((((y*x)*z) + (y*w)), (((x*z) + w)*y)) ||
rewrite(((((x + c0)/c1)*y) + y), (y*((x + fold((c0 + c1)))/c1))) ||
rewrite(((min(y, (x + -1))*z) + z), (min(x, (y + 1))*z)) ||
rewrite(((min((x + -1), y)*z) + z), (min((y + 1), x)*z)) ||
rewrite(((min(((x - y)*c0), c1)/c0) + y), min((y + 0), x), (((-1 <= (c0 + c1)) && (0 <= c1)) && ((max(c1, 0) + 1) <= c0))) ||
rewrite((min(w, (z + (x - y))) + y), min((z + x), (w + y))) ||
rewrite((min(w, ((x - y) + z)) + y), min((x + z), (w + y))) ||
rewrite((min(w, ((x - y) - z)) + y), min((x - z), (w + y))) ||
rewrite((min(w, min(x, ((y - z) + c0))) + z), min((min(x, w) + z), (c0 + y))) ||
rewrite((min(w, min(((y - z) + c0), x)) + z), min((min(x, w) + z), (c0 + y))) ||
rewrite((min(x, (y - z)) + (w + z)), (min(y, (x + z)) + w)) ||
rewrite((min(x, (y - z)) + (z + w)), (min(y, (x + z)) + w)) ||
rewrite((min(y, (c0 - max(x, c1))) + x), min(c0, (min(y, fold((c0 - c1))) + x))) ||
rewrite((min((z + (x - y)), w) + y), min((z + x), (w + y))) ||
rewrite((min(((x - y) + z), w) + y), min((x + z), (w + y))) ||
rewrite((min((min(min(min(x, y), z), c0) + c1), y) + c1), (min(min(min(x, y), z), c0) + fold((c1*2))), (c1 <= max((min(c1, 0)*2), -1))) ||
rewrite((min((c0 - max(x, c1)), y) + x), min(c0, (min(fold((c0 - c1)), y) + x))) ||
rewrite((min((y - z), x) + (w + z)), (min((x + z), y) + w)) ||
rewrite((min((y - z), x) + (z + w)), (min((x + z), y) + w)) ||
rewrite((min(((x - y) - z), w) + y), min((x - z), (w + y))) ||
rewrite((min(((x - y)*z), c0) + (y*z)), min((x*z), (c0 + (y*z)))) ||
rewrite((min(((x - y)*z), c0) + (z*y)), min((x*z), (c0 + (z*y)))) ||
rewrite((min(((x - (y*c0))*c0), c1) + (y*c2)), min((x*c0), (c1 + (y*c2))), ((1 <= c2) && ((c0*c0) == c2))) ||
rewrite((min(((x - (y*c0))*c1), c2) + (y*c3)), min((x*c1), (c2 + (y*c3))), (((((((1 <= max(c1, c3)) || (c0 <= -1)) || (c2 <= 2)) || (c3 <= -1)) || ((c1 + c2) <= 0)) || ((c1 + c2) <= 1)) && (((((((0 <= c2) || (1 <= c0)) || (1 <= c1)) || (1 <= c3)) || (c0 <= -1)) || ((c1 + 1) <= c2)) && (((((1 <= max(c1, c3)) || (c0 <= -1)) || (c2 <= 1)) || (c3 <= -1)) && (((1 <= max(max(c0, c1), c3)) || (c2 <= 1)) && ((c0*c1) == c3)))))) ||
rewrite((min(min(x, ((y - z) + c0)), w) + z), min((min(x, w) + z), (y + c0))) ||
rewrite((min(min((x + c0), (y + z)), y) + c1), min((min(z, 0) + (y + c1)), x), ((c0 + c1) == 0)) ||
rewrite((min(min((x + c0), (y + z)), z) + c1), min((min(y, 0) + (c1 + z)), x), ((c0 + c1) == 0)) ||
rewrite((min(min((x + y), (z + c0)), y) + c1), min((min(x, 0) + (y + c1)), z), ((c0 + c1) == 0)) ||
rewrite((min(min(((y - z) + c0), x), w) + z), min((y + c0), (min(x, w) + z))) ||
rewrite((min(select((x < y), c0, 0), select((x < z), c0, 0)) + c2), select((x < max(z, y)), 0, c2), (((c0 <= -1) || (c2 == 0)) && (((max(c2, 0) + c0) <= 0) && ((c0 + c2) == 0)))) ||
rewrite((max(w, (z + (x - y))) + y), max((z + x), (w + y))) ||
rewrite((max(w, ((x - y) + z)) + y), max((x + z), (w + y))) ||
rewrite((max(x, c0) + max(min(x, c0), c1)), (c0 + max(x, c1)), (c1 <= c0)) ||
rewrite((max(x, y) + (z + min(x, y))), (y + (z + x))) ||
rewrite((max(x, y) + (z + min(y, x))), (y + (z + x))) ||
rewrite((max(x, y) + (min(x, y) + z)), (y + (x + z))) ||
rewrite((max(x, y) + (min(y, x) + z)), (y + (x + z))) ||
rewrite((max(x, (y - (w + z))) + z), max((y - w), (x + z))) ||
rewrite((max(x, (y - (z + w))) + z), max((y - w), (x + z))) ||
rewrite((max(y, x) + (z + min(x, y))), (y + (z + x))) ||
rewrite((max(y, x) + (z + min(y, x))), (y + (z + x))) ||
rewrite((max(y, x) + (min(x, y) + z)), (y + (x + z))) ||
rewrite((max(y, x) + (min(y, x) + z)), (y + (x + z))) ||
rewrite((max((z + (x - y)), w) + y), max((z + x), (w + y))) ||
rewrite((max(((x - y) + z), w) + y), max((x + z), (w + y))) ||
rewrite((max((y - (w + z)), x) + z), max((y - w), (x + z))) ||
rewrite((max((y - (z + w)), x) + z), max((y - w), (x + z))) ||
rewrite((max((min((x + c0), y) - min(x, c1)), c0) + c2), max(min((y + fold((c2 - c1))), x), c1), ((c0 + c2) == c1)) ||
rewrite((select(x, (y + c0), y) + c1), (select(x, fold((c0 + c1)), c1) + y)) ||
