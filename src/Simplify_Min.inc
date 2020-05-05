((b.node_type() == IRNodeType::Add) && (
 rewrite(min(x, (y + (z + ((w*c0) + (u - w))))), min(x, (y + (z + (w + u)))), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(x, (y + (((z*c0) + (w - z)) + u))), min(x, (y + ((z + w) + u))), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(x, (y + ((z*c0) + (w - z)))), min(x, (y + (z + w))), ((((1 <= c0) && (c0 <= 6)) && (-22 <= (c0*-7))) && ((min(c0, 3)*7) == 14))) ||
 rewrite(min(x, (y + (z - ((w + y)/c0)))), min(x, (z - ((w - y)/c0))), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-6/c0), -3) + 4)))) ||
 rewrite(min(x, (y + (z - ((y + w)/c0)))), min(x, (z - ((w - y)/c0))), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-6/c0), -3) + 4)))) ||
 rewrite(min(x, (y + ((z/c0) - ((w + y)/c0)))), min(x, ((z/c0) - ((w - y)/c0))), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-10/c0), -5) + 6)))) ||
 rewrite(min(x, (y + ((z/c0) - ((y + w)/c0)))), min(x, ((z/c0) - ((w - y)/c0))), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-10/c0), -5) + 6)))) ||
 rewrite(min(x, (y + ((((x - y) + c0)/c1)*c1))), x, (((0 <= ((c0/c1)*c1)) && (1 <= (((c0 + 1)/c1)*c1))) && (-1 <= (((c0 + -1)/c1)*c1)))) ||
 rewrite(min(x, (y + (min(min(z, w), (w + c0))/c1))), min(x, (y + (min(z, w)/c1))), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(x, (y + (min(min(z, w), (z + c0))/c1))), min(x, (y + (min(w, z)/c1))), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(x, ((y + ((z*c0) + (w - z))) + u)), min(x, ((y + (z + w)) + u)), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(x, ((((y*c0) + (z - y)) + w) + u)), min(x, (((y + z) + w) + u)), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(x, (((y*c0) + (z - y)) + w)), min(x, ((y + z) + w)), ((((1 <= c0) && (c0 <= 6)) && (-22 <= (c0*-7))) && ((min(c0, 3)*7) == 14))) ||
 rewrite(min(x, ((y - ((z + w)/c0)) + w)), min(x, (y - ((z - w)/c0))), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-6/c0), -3) + 4)))) ||
 rewrite(min(x, ((y - ((z + w)/c0)) + z)), min(x, (y - ((w - z)/c0))), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-6/c0), -3) + 4)))) ||
 rewrite(min(x, (((y/c0) - ((z + w)/c0)) + w)), min(x, ((y/c0) - ((z - w)/c0))), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-10/c0), -5) + 6)))) ||
 rewrite(min(x, (((y/c0) - ((z + w)/c0)) + z)), min(x, ((y/c0) - ((w - z)/c0))), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-10/c0), -5) + 6)))) ||
 rewrite(min(x, (((((x - y) + c0)/c1)*c1) + y)), x, (((0 <= ((c0/c1)*c1)) && (1 <= (((c0 + 1)/c1)*c1))) && (-1 <= (((c0 + -1)/c1)*c1)))) ||
 rewrite(min(x, ((y/c0) + ((y + c1)/c0))), min(x, y), ((((((0 <= (c1/c0)) && ((c1/c0) == 0)) && (0 <= ((c1 + 1)/c0))) && (((-4/c0) + ((c1 + -4)/c0)) == -4)) && (((-6/c0) + ((c1 + -6)/c0)) == -6)) && (((-7/c0) + ((c1 + -7)/c0)) == -7))) ||
 rewrite(min(x, ((min(min(y, z), (y + c0))/c1) + w)), min(x, ((min(z, y)/c1) + w)), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(x, ((min(min(y, z), (z + c0))/c1) + w)), min(x, ((min(y, z)/c1) + w)), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(x, (min(y, min(x, z)) + c0)), min(x, (min(y, z) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(y, min(x, z)) + c0)), (min(y, min(x, z)) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, x)) + c0)), min(x, (min(y, z) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(y, min(z, x)) + c0)), (min(y, min(z, x)) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, min(w, x))) + c0)), min(x, (min(y, min(z, w)) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(y, min(z, min(w, min(u, x)))) + c0)), (min(y, min(z, min(w, min(u, x)))) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, min(w, min(x, u)))) + c0)), (min(y, min(z, min(w, min(x, u)))) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, min(x, w))) + c0)), min(x, (min(y, min(z, w)) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(y, min(z, min(min(w, x), u))) + c0)), (min(y, min(z, min(min(w, x), u))) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(z, min(min(x, w), u))) + c0)), (min(y, min(z, min(min(x, w), u))) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(min(x, z), w)) + c0)), min(x, (min(y, min(z, w)) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(y, min(min(z, x), w)) + c0)), min(x, (min(y, min(z, w)) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(y, min(min(z, min(w, x)), u)) + c0)), (min(y, min(min(z, min(w, x)), u)) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(min(z, min(x, w)), u)) + c0)), (min(y, min(min(z, min(x, w)), u)) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(min(min(x, z), w), u)) + c0)), (min(y, min(min(min(x, z), w), u)) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(y, min(min(min(z, x), w), u)) + c0)), (min(y, min(min(min(z, x), w), u)) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(x, y), z) + c0)), min(x, (min(y, z) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(min(x, y), z) + c0)), (min(min(x, y), z) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, x), z) + c0)), min(x, (min(y, z) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(min(y, x), z) + c0)), (min(min(y, x), z) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, min(x, z)), w) + c0)), min(x, (min(min(y, z), w) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(min(y, min(z, x)), w) + c0)), min(x, (min(min(y, z), w) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(min(y, min(z, min(w, x))), u) + c0)), (min(min(y, min(z, min(w, x))), u) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, min(z, min(x, w))), u) + c0)), (min(min(y, min(z, min(x, w))), u) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, min(min(x, z), w)), u) + c0)), (min(min(y, min(min(x, z), w)), u) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(y, min(min(z, x), w)), u) + c0)), (min(min(y, min(min(z, x), w)), u) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(x, y), z), w) + c0)), min(x, (min(min(y, z), w) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(min(min(y, x), z), w) + c0)), min(x, (min(min(y, z), w) + c0)), (0 <= c0)) ||
 rewrite(min(x, (min(min(min(y, min(x, z)), w), u) + c0)), (min(min(min(y, min(x, z)), w), u) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(y, min(z, x)), w), u) + c0)), (min(min(min(y, min(z, x)), w), u) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(min(x, y), z), w), u) + c0)), (min(min(min(min(x, y), z), w), u) + c0), (c0 <= 0)) ||
 rewrite(min(x, (min(min(min(min(y, x), z), w), u) + c0)), (min(min(min(min(y, x), z), w), u) + c0), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite(min(x, (min(y, min(z, (y + c0)))/c1)), min(x, (min(z, y)/c1)), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(x, (min(y, min((y + c0), z))/c1)), min(x, (min(y, z)/c1)), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(x, (min(min(y, (z + c0)), z)/c1)), min(x, (min(y, z)/c1)), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(x, (min(min((y + c0), z), y)/c1)), min(x, (min(z, y)/c1)), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(x, min(x, y)), min(x, y)) ||
 rewrite(min(x, min(y, x)), min(y, x)) ||
 rewrite(min(x, min(y, (x + c0))), min(y, x), (0 <= c0)) ||
 rewrite(min(x, min(y, (z + (w - ((u + z)/c0))))), min(x, min(y, (w - ((u - z)/c0)))), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(x, min(y, (z + (w - ((z + u)/c0))))), min(x, min(y, (w - ((u - z)/c0)))), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(x, min(y, ((z - ((w + u)/c0)) + u))), min(x, min(y, (z - ((w - u)/c0)))), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(x, min(y, ((z - ((w + u)/c0)) + w))), min(x, min(y, (z - ((u - w)/c0)))), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(x, min(y, (min(x, z) + c0))), min(x, min(y, (z + c0))), (0 <= c0)) ||
 rewrite(min(x, min(y, (min(x, z) + c0))), min(y, (min(x, z) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(z, x) + c0))), min(x, min(y, (z + c0))), (0 <= c0)) ||
 rewrite(min(x, min(y, (min(z, x) + c0))), min(y, (min(z, x) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(z, min(w, x)) + c0))), min(y, (min(z, min(w, x)) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(z, min(x, w)) + c0))), min(y, (min(z, min(x, w)) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(min(x, z), w) + c0))), min(y, (min(min(x, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(min(x, z), (x + c0)) + c1))), min(x, min(y, (min(x, z) + c1))), ((min(c1, 0) <= (min(c0, 0) + c1)) && (min((min(c0, -1) + c1), -1) == (min(c1, 0) + -1)))) ||
 rewrite(min(x, min(y, (min(min(z, x), w) + c0))), min(y, (min(min(z, x), w) + c0)), (c0 <= 0)) ||
 rewrite(min(x, min(y, (min(min(z, x), (x + c0)) + c1))), min(x, min(y, (min(z, x) + c1))), ((min(c1, 0) <= (min(c0, 0) + c1)) && (min((min(c0, -1) + c1), -1) == (min(c1, 0) + -1)))) ||
 rewrite(min(x, min(y, min(x, z))), min(y, min(x, z))) ||
 rewrite(min(x, min(y, min(z, x))), min(y, min(z, x))) ||
 rewrite(min(x, min(y, min(z, (min(w, x) + c0)))), min(x, min(y, min(z, (w + c0)))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(z, (min(w, min(u, x)) + c0)))), min(x, min(y, min(z, (min(w, u) + c0)))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(z, (min(w, min(x, u)) + c0)))), min(x, min(y, min(z, (min(w, u) + c0)))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(z, (min(x, w) + c0)))), min(x, min(y, min(z, (w + c0)))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(z, (min(min(w, x), u) + c0)))), min(x, min(y, min(z, (min(w, u) + c0)))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(z, (min(min(x, w), u) + c0)))), min(x, min(y, min(z, (min(w, u) + c0)))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(z, min(w, min(u, min(v, x)))))), min(y, min(z, min(w, min(u, min(v, x)))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(u, min(x, v)))))), min(y, min(z, min(w, min(u, min(x, v)))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(min(u, x), v))))), min(y, min(z, min(w, min(min(u, x), v))))) ||
 rewrite(min(x, min(y, min(z, min(w, min(min(x, u), v))))), min(y, min(z, min(w, min(min(x, u), v))))) ||
 rewrite(min(x, min(y, min(z, min(min(w, min(u, x)), v)))), min(y, min(z, min(min(w, min(u, x)), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(w, min(x, u)), v)))), min(y, min(z, min(min(w, min(x, u)), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(min(w, x), u), v)))), min(y, min(z, min(min(min(w, x), u), v)))) ||
 rewrite(min(x, min(y, min(z, min(min(min(x, w), u), v)))), min(y, min(z, min(min(min(x, w), u), v)))) ||
 rewrite(min(x, min(y, min((min(x, z) + c0), w))), min(x, min(y, min((z + c0), w))), (0 <= c0)) ||
 rewrite(min(x, min(y, min((min(z, x) + c0), w))), min(x, min(y, min((z + c0), w))), (0 <= c0)) ||
 rewrite(min(x, min(y, min((min(z, min(w, x)) + c0), u))), min(x, min(y, min((min(z, w) + c0), u))), (0 <= c0)) ||
 rewrite(min(x, min(y, min((min(z, min(x, w)) + c0), u))), min(x, min(y, min((min(z, w) + c0), u))), (0 <= c0)) ||
 rewrite(min(x, min(y, min((min(min(x, z), w) + c0), u))), min(x, min(y, min((min(z, w) + c0), u))), (0 <= c0)) ||
 rewrite(min(x, min(y, min((min(min(z, x), w) + c0), u))), min(x, min(y, min((min(z, w) + c0), u))), (0 <= c0)) ||
 rewrite(min(x, min(y, min(min(z, min(w, min(u, x))), v))), min(y, min(min(z, min(w, min(u, x))), v))) ||
 rewrite(min(x, min(y, min(min(z, min(w, min(x, u))), v))), min(y, min(min(z, min(w, min(x, u))), v))) ||
 rewrite(min(x, min(y, min(min(z, min(min(w, x), u)), v))), min(y, min(min(z, min(min(w, x), u)), v))) ||
 rewrite(min(x, min(y, min(min(z, min(min(x, w), u)), v))), min(y, min(min(z, min(min(x, w), u)), v))) ||
 rewrite(min(x, min(y, min(min(min(z, min(w, x)), u), v))), min(y, min(min(min(z, min(w, x)), u), v))) ||
 rewrite(min(x, min(y, min(min(min(z, min(x, w)), u), v))), min(y, min(min(min(z, min(x, w)), u), v))) ||
 rewrite(min(x, min(y, min(min(min(min(x, z), w), u), v))), min(y, min(min(min(min(x, z), w), u), v))) ||
 rewrite(min(x, min(y, min(min(min(min(z, x), w), u), v))), min(y, min(min(min(min(z, x), w), u), v))) ||
 rewrite(min(x, min((x + c0), y)), min(x, y), (0 <= c0)) ||
 rewrite(min(x, min((y + (z - ((w + y)/c0))), u)), min(x, min((z - ((w - y)/c0)), u)), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(x, min((y + (z - ((y + w)/c0))), u)), min(x, min((z - ((w - y)/c0)), u)), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(x, min(((y - ((z + w)/c0)) + w), u)), min(x, min((y - ((z - w)/c0)), u)), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(x, min(((y - ((z + w)/c0)) + z), u)), min(x, min((y - ((w - z)/c0)), u)), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(x, min((min(x, y) + c0), z)), min(x, min((y + c0), z)), (0 <= c0)) ||
 rewrite(min(x, min((min(x, y) + c0), z)), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(x, min((min(y, x) + c0), z)), min(x, min((y + c0), z)), (0 <= c0)) ||
 rewrite(min(x, min((min(y, x) + c0), z)), min((min(y, x) + c0), z), (c0 <= 0)) ||
 rewrite(min(x, min((min(y, min(x, z)) + c0), w)), min((min(y, min(x, z)) + c0), w), (c0 <= 0)) ||
 rewrite(min(x, min((min(y, min(z, x)) + c0), w)), min((min(y, min(z, x)) + c0), w), (c0 <= 0)) ||
 rewrite(min(x, min((min(min(x, y), z) + c0), w)), min((min(min(x, y), z) + c0), w), (c0 <= 0)) ||
 rewrite(min(x, min((min(min(x, y), (x + c0)) + c1), z)), min(x, min((min(x, y) + c1), z)), ((min(c1, 0) <= (min(c0, 0) + c1)) && (min((min(c0, -1) + c1), -1) == (min(c1, 0) + -1)))) ||
 rewrite(min(x, min((min(min(y, x), z) + c0), w)), min((min(min(y, x), z) + c0), w), (c0 <= 0)) ||
 rewrite(min(x, min((min(min(y, x), (x + c0)) + c1), z)), min(x, min((min(y, x) + c1), z)), ((min(c1, 0) <= (min(c0, 0) + c1)) && (min((min(c0, -1) + c1), -1) == (min(c1, 0) + -1)))) ||
 rewrite(min(x, min(min(x, y), z)), min(x, min(y, z))) ||
 rewrite(min(x, min(min(y, x), z)), min(x, min(y, z))) ||
 rewrite(min(x, min(min(y, (min(x, z) + c0)), w)), min(x, min(min(y, (z + c0)), w)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, (min(z, x) + c0)), w)), min(x, min(min(y, (z + c0)), w)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, (min(z, min(w, x)) + c0)), u)), min(x, min(min(y, (min(z, w) + c0)), u)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, (min(z, min(x, w)) + c0)), u)), min(x, min(min(y, (min(z, w) + c0)), u)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, (min(min(x, z), w) + c0)), u)), min(x, min(min(y, (min(z, w) + c0)), u)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, (min(min(z, x), w) + c0)), u)), min(x, min(min(y, (min(z, w) + c0)), u)), (0 <= c0)) ||
 rewrite(min(x, min(min(y, min(z, min(w, min(u, x)))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(w, min(x, u)))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(min(w, x), u))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(z, min(min(x, w), u))), v)), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(x, min(min(y, min(min(z, min(w, x)), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(z, min(x, w)), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(min(x, z), w), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min(y, min(min(min(z, x), w), u)), v)), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(x, min(min((min(x, y) + c0), z), w)), min(x, min(min((y + c0), z), w)), (0 <= c0)) ||
 rewrite(min(x, min(min((min(y, x) + c0), z), w)), min(x, min(min((y + c0), z), w)), (0 <= c0)) ||
 rewrite(min(x, min(min((min(y, min(x, z)) + c0), w), u)), min(x, min(min((min(y, z) + c0), w), u)), (0 <= c0)) ||
 rewrite(min(x, min(min((min(y, min(z, x)) + c0), w), u)), min(x, min(min((min(y, z) + c0), w), u)), (0 <= c0)) ||
 rewrite(min(x, min(min((min(min(x, y), z) + c0), w), u)), min(x, min(min((min(y, z) + c0), w), u)), (0 <= c0)) ||
 rewrite(min(x, min(min((min(min(y, x), z) + c0), w), u)), min(x, min(min((min(y, z) + c0), w), u)), (0 <= c0)) ||
 rewrite(min(x, min(min(min(y, min(z, min(w, x))), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(z, min(x, w))), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(min(x, z), w)), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(y, min(min(z, x), w)), u), v)), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(x, min(min(min(min(y, min(x, z)), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(y, min(z, x)), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(min(x, y), z), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(min(min(min(min(y, x), z), w), u), v)), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(x, min(select(y, z, (w/c0)), (x + c1))), min(x, select(y, z, (w/c0))), (0 <= c1)))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(min((x + (y + ((z*c0) + (w - z)))), u), min((x + (y + (z + w))), u), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min((x + (((y*c0) + (z - y)) + w)), u), min((x + ((y + z) + w)), u), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min((x + ((y*c0) + (z - y))), w), min((x + (y + z)), w), ((((1 <= c0) && (c0 <= 6)) && (-22 <= (c0*-7))) && ((min(c0, 3)*7) == 14))) ||
 rewrite(min((x + (y - ((x + z)/c0))), w), min((y - ((z - x)/c0)), w), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-6/c0), -3) + 4)))) ||
 rewrite(min((x + (y - ((z + x)/c0))), w), min((y - ((z - x)/c0)), w), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-6/c0), -3) + 4)))) ||
 rewrite(min((x + ((y/c0) - ((x + z)/c0))), w), min(((y/c0) - ((z - x)/c0)), w), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-10/c0), -5) + 6)))) ||
 rewrite(min((x + ((y/c0) - ((z + x)/c0))), w), min(((y/c0) - ((z - x)/c0)), w), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-10/c0), -5) + 6)))) ||
 rewrite(min((x + ((((y - x) + c0)/c1)*c1)), y), y, (((0 <= ((c0/c1)*c1)) && (1 <= (((c0 + 1)/c1)*c1))) && (-1 <= (((c0 + -1)/c1)*c1)))) ||
 rewrite(min((x + (min(min(y, z), (y + c0))/c1)), w), min((x + (min(z, y)/c1)), w), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min((x + (min(min(y, z), (z + c0))/c1)), w), min((x + (min(y, z)/c1)), w), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(((x + ((y*c0) + (z - y))) + w), u), min(((x + (y + z)) + w), u), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min(((((x*c0) + (y - x)) + z) + w), u), min((((x + y) + z) + w), u), (((1 <= c0) && (c0 <= 5)) && ((c0*2) == 4))) ||
 rewrite(min((((x*c0) + (y - x)) + z), w), min(((x + y) + z), w), ((((1 <= c0) && (c0 <= 6)) && (-22 <= (c0*-7))) && ((min(c0, 3)*7) == 14))) ||
 rewrite(min(((x - ((y + z)/c0)) + y), w), min((x - ((z - y)/c0)), w), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-6/c0), -3) + 4)))) ||
 rewrite(min(((x - ((y + z)/c0)) + z), w), min((x - ((y - z)/c0)), w), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-6/c0), -3) + 4)))) ||
 rewrite(min((((x/c0) - ((y + z)/c0)) + y), w), min(((x/c0) - ((z - y)/c0)), w), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-10/c0), -5) + 6)))) ||
 rewrite(min((((x/c0) - ((y + z)/c0)) + z), w), min(((x/c0) - ((y - z)/c0)), w), (((-1/c0) <= 0) && (max((2/c0), 1) == (max((-10/c0), -5) + 6)))) ||
 rewrite(min((((((x - y) + c0)/c1)*c1) + y), x), x, (((0 <= ((c0/c1)*c1)) && (1 <= (((c0 + 1)/c1)*c1))) && (-1 <= (((c0 + -1)/c1)*c1)))) ||
 rewrite(min(((x/c0) + ((x + c1)/c0)), y), min(x, y), ((((((0 <= (c1/c0)) && ((c1/c0) == 0)) && (0 <= ((c1 + 1)/c0))) && (((-4/c0) + ((c1 + -4)/c0)) == -4)) && (((-6/c0) + ((c1 + -6)/c0)) == -6)) && (((-7/c0) + ((c1 + -7)/c0)) == -7))) ||
 rewrite(min(((min(min(x, y), (x + c0))/c1) + z), w), min(((min(y, x)/c1) + z), w), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min(((min(min(x, y), (y + c0))/c1) + z), w), min(((min(x, y)/c1) + z), w), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min((min(x, min(y, z)) + c0), y), min((min(x, z) + c0), y), (0 <= c0)) ||
 rewrite(min((min(x, min(y, z)) + c0), y), (min(x, min(y, z)) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, z)) + c0), z), min((min(x, y) + c0), z), (0 <= c0)) ||
 rewrite(min((min(x, min(y, z)) + c0), z), (min(x, min(y, z)) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, min(z, w))) + c0), w), min((min(x, min(y, z)) + c0), w), (0 <= c0)) ||
 rewrite(min((min(x, min(y, min(z, w))) + c0), z), min((min(x, min(y, w)) + c0), z), (0 <= c0)) ||
 rewrite(min((min(x, min(y, min(z, min(w, u)))) + c0), u), (min(x, min(y, min(z, min(w, u)))) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, min(z, min(w, u)))) + c0), w), (min(x, min(y, min(z, min(w, u)))) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, min(min(z, w), u))) + c0), w), (min(x, min(y, min(min(z, w), u))) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(y, min(min(z, w), u))) + c0), z), (min(x, min(y, min(min(z, w), u))) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(min(y, z), w)) + c0), y), min((min(x, min(z, w)) + c0), y), (0 <= c0)) ||
 rewrite(min((min(x, min(min(y, z), w)) + c0), z), min((min(x, min(y, w)) + c0), z), (0 <= c0)) ||
 rewrite(min((min(x, min(min(y, min(z, w)), u)) + c0), w), (min(x, min(min(y, min(z, w)), u)) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(min(y, min(z, w)), u)) + c0), z), (min(x, min(min(y, min(z, w)), u)) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(min(min(y, z), w), u)) + c0), y), (min(x, min(min(min(y, z), w), u)) + c0), (c0 <= 0)) ||
 rewrite(min((min(x, min(min(min(y, z), w), u)) + c0), z), (min(x, min(min(min(y, z), w), u)) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(x, y), z) + c0), x), min((min(y, z) + c0), x), (0 <= c0)) ||
 rewrite(min((min(min(x, y), z) + c0), x), (min(min(x, y), z) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(x, y), z) + c0), y), min((min(x, z) + c0), y), (0 <= c0)) ||
 rewrite(min((min(min(x, y), z) + c0), y), (min(min(x, y), z) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(x, min(y, z)), w) + c0), y), min((min(min(x, z), w) + c0), y), (0 <= c0)) ||
 rewrite(min((min(min(x, min(y, z)), w) + c0), z), min((min(min(x, y), w) + c0), z), (0 <= c0)) ||
 rewrite(min((min(min(x, min(y, min(z, w))), u) + c0), w), (min(min(x, min(y, min(z, w))), u) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(x, min(y, min(z, w))), u) + c0), z), (min(min(x, min(y, min(z, w))), u) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(x, min(min(y, z), w)), u) + c0), y), (min(min(x, min(min(y, z), w)), u) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(x, min(min(y, z), w)), u) + c0), z), (min(min(x, min(min(y, z), w)), u) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(min(x, y), z), w) + c0), x), min((min(min(y, z), w) + c0), x), (0 <= c0)) ||
 rewrite(min((min(min(min(x, y), z), w) + c0), y), min((min(min(x, z), w) + c0), y), (0 <= c0)) ||
 rewrite(min((min(min(min(x, min(y, z)), w), u) + c0), y), (min(min(min(x, min(y, z)), w), u) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(min(x, min(y, z)), w), u) + c0), z), (min(min(min(x, min(y, z)), w), u) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(min(min(x, y), z), w), u) + c0), x), (min(min(min(min(x, y), z), w), u) + c0), (c0 <= 0)) ||
 rewrite(min((min(min(min(min(x, y), z), w), u) + c0), y), (min(min(min(min(x, y), z), w), u) + c0), (c0 <= 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((x + y), (z + (w + x))), (x + min(y, (w + z)))) ||
 rewrite(min((x + y), (z + (w + y))), (min(x, (z + w)) + y)) ||
 rewrite(min((x + y), (z + (w + (u + x)))), (x + min(y, (w + (u + z))))) ||
 rewrite(min((x + y), (z + (w + (u + y)))), (y + min(x, (w + (u + z))))) ||
 rewrite(min((x + y), (z + (w + (u + (v + x))))), (x + min(y, (w + (u + (v + z)))))) ||
 rewrite(min((x + y), (z + (w + (u + (v + y))))), (y + min(x, (w + (u + (v + z)))))) ||
 rewrite(min((x + y), (z + (w + (u + (x + v))))), (x + min(y, (w + (u + (z + v)))))) ||
 rewrite(min((x + y), (z + (w + (u + (y + v))))), (y + min(x, (w + (u + (z + v)))))) ||
 rewrite(min((x + y), (z + (w + (x + u)))), (x + min(y, (w + (z + u))))) ||
 rewrite(min((x + y), (z + (w + (y + u)))), (y + min(x, (w + (z + u))))) ||
 rewrite(min((x + y), (z + (w + ((u + x) + v)))), (x + min(y, (w + ((u + z) + v))))) ||
 rewrite(min((x + y), (z + (w + ((u + y) + v)))), (y + min(x, (w + ((u + z) + v))))) ||
 rewrite(min((x + y), (z + (w + ((x + u) + v)))), (x + min(y, (w + ((z + u) + v))))) ||
 rewrite(min((x + y), (z + (w + ((y + u) + v)))), (y + min(x, (w + ((z + u) + v))))) ||
 rewrite(min((x + y), (z + (x + w))), (x + min(y, (z + w)))) ||
 rewrite(min((x + y), (z + (y + w))), (min((z + w), x) + y)) ||
 rewrite(min((x + y), (z + ((w + x) + u))), (x + min(y, ((w + z) + u)))) ||
 rewrite(min((x + y), (z + ((w + y) + u))), (y + min(x, ((w + z) + u)))) ||
 rewrite(min((x + y), (z + ((w + (u + x)) + v))), (x + min(y, ((w + (u + z)) + v)))) ||
 rewrite(min((x + y), (z + ((w + (u + y)) + v))), (y + min(x, ((w + (u + z)) + v)))) ||
 rewrite(min((x + y), (z + ((w + (x + u)) + v))), (x + min(y, ((w + (z + u)) + v)))) ||
 rewrite(min((x + y), (z + ((w + (y + u)) + v))), (y + min(x, ((w + (z + u)) + v)))) ||
 rewrite(min((x + y), (z + ((x + w) + u))), (x + min(y, ((z + w) + u)))) ||
 rewrite(min((x + y), (z + ((y + w) + u))), (y + min(x, ((z + w) + u)))) ||
 rewrite(min((x + y), (z + (((w + x) + u) + v))), (x + min(y, (((w + z) + u) + v)))) ||
 rewrite(min((x + y), (z + (((w + y) + u) + v))), (y + min(x, (((w + z) + u) + v)))) ||
 rewrite(min((x + y), (z + (((x + w) + u) + v))), (x + min(y, (((z + w) + u) + v)))) ||
 rewrite(min((x + y), (z + (((y + w) + u) + v))), (y + min(x, (((z + w) + u) + v)))) ||
 rewrite(min((x + y), (z + (x - w))), (x + min(y, (z - w)))) ||
 rewrite(min((x + y), (z + (y - w))), (y + min(x, (z - w)))) ||
 rewrite(min((x + y), ((x + z) + w)), (x + min((w + z), y))) ||
 rewrite(min((x + y), ((y + z) + w)), (min(x, (w + z)) + y)) ||
 rewrite(min((x + y), ((z + x) + w)), (x + min((z + w), y))) ||
 rewrite(min((x + y), ((z + y) + w)), (min(x, (z + w)) + y)) ||
 rewrite(min((x + y), ((z + (w + x)) + u)), (x + min((z + (w + u)), y))) ||
 rewrite(min((x + y), ((z + (w + y)) + u)), (min(x, (z + (w + u))) + y)) ||
 rewrite(min((x + y), ((z + (w + (u + x))) + v)), (x + min((z + (w + (u + v))), y))) ||
 rewrite(min((x + y), ((z + (w + (u + y))) + v)), (min(x, (z + (w + (u + v)))) + y)) ||
 rewrite(min((x + y), ((z + (w + (x + u))) + v)), (x + min((z + (w + (v + u))), y))) ||
 rewrite(min((x + y), ((z + (w + (y + u))) + v)), (min(x, (z + (w + (v + u)))) + y)) ||
 rewrite(min((x + y), ((z + (x + w)) + u)), (min(y, ((z + u) + w)) + x)) ||
 rewrite(min((x + y), ((z + (y + w)) + u)), (min(x, ((z + u) + w)) + y)) ||
 rewrite(min((x + y), ((z + ((w + x) + u)) + v)), (x + min((z + ((w + v) + u)), y))) ||
 rewrite(min((x + y), ((z + ((w + y) + u)) + v)), (min(x, (z + ((w + v) + u))) + y)) ||
 rewrite(min((x + y), ((z + ((x + w) + u)) + v)), (x + min((z + ((v + w) + u)), y))) ||
 rewrite(min((x + y), ((z + ((y + w) + u)) + v)), (min(x, (z + ((v + w) + u))) + y)) ||
 rewrite(min((x + y), (((x + z) + w) + u)), (x + min(((u + z) + w), y))) ||
 rewrite(min((x + y), (((y + z) + w) + u)), (min(x, ((u + z) + w)) + y)) ||
 rewrite(min((x + y), (((z + x) + w) + u)), (x + min(((z + u) + w), y))) ||
 rewrite(min((x + y), (((z + y) + w) + u)), (min(x, ((z + u) + w)) + y)) ||
 rewrite(min((x + y), (((z + (w + x)) + u) + v)), (x + min(((z + (w + v)) + u), y))) ||
 rewrite(min((x + y), (((z + (w + y)) + u) + v)), (min(x, ((z + (w + v)) + u)) + y)) ||
 rewrite(min((x + y), (((z + (x + w)) + u) + v)), (x + min(((z + (v + w)) + u), y))) ||
 rewrite(min((x + y), (((z + (y + w)) + u) + v)), (min(x, ((z + (v + w)) + u)) + y)) ||
 rewrite(min((x + y), ((((x + z) + w) + u) + v)), (x + min((((v + z) + w) + u), y))) ||
 rewrite(min((x + y), ((((y + z) + w) + u) + v)), (min(x, (((v + z) + w) + u)) + y)) ||
 rewrite(min((x + y), ((((z + x) + w) + u) + v)), (x + min((((z + v) + w) + u), y))) ||
 rewrite(min((x + y), ((((z + y) + w) + u) + v)), (min(x, (((z + v) + w) + u)) + y)) ||
 rewrite(min((x + y), ((x - z) + w)), (x + min((w - z), y))) ||
 rewrite(min((x + y), ((y - z) + w)), (min(x, (w - z)) + y)) ||
 rewrite(min((x + y), ((z/c0) + ((z + c1)/c0))), min((x + y), z), (((((0 <= (c1/c0)) && ((c1/c0) == 0)) && (0 <= ((c1 + 1)/c0))) && (((-1072/c0) + ((c1 + -1072)/c0)) == -1072)) && (((-1/c0) + ((c1 + -1)/c0)) == -1))) ||
 rewrite(min((x + (y + z)), (w + y)), (min((x + z), w) + y)) ||
 rewrite(min((x + (y + z)), (w + z)), (min((x + y), w) + z)) ||
 rewrite(min((x + (y + z)), (w + (u + y))), (min((x + z), (w + u)) + y)) ||
 rewrite(min((x + (y + z)), (w + (u + z))), (min((x + y), (w + u)) + z)) ||
 rewrite(min((x + (y + z)), (w + (u + (v + y)))), (min((x + z), (w + (u + v))) + y)) ||
 rewrite(min((x + (y + z)), (w + (u + (v + z)))), (min((x + y), (w + (u + v))) + z)) ||
 rewrite(min((x + (y + z)), (w + (u + (y + v)))), (min((x + z), (w + (u + v))) + y)) ||
 rewrite(min((x + (y + z)), (w + (u + (z + v)))), (min((x + y), (w + (u + v))) + z)) ||
 rewrite(min((x + (y + z)), (w + (y + u))), (min((x + z), (w + u)) + y)) ||
 rewrite(min((x + (y + z)), (w + (z + u))), (min((x + y), (w + u)) + z)) ||
 rewrite(min((x + (y + z)), (w + ((u + y) + v))), (min((x + z), (w + (u + v))) + y)) ||
 rewrite(min((x + (y + z)), (w + ((u + z) + v))), (min((x + y), (w + (u + v))) + z)) ||
 rewrite(min((x + (y + z)), (w + ((y + u) + v))), (min((x + z), (w + (v + u))) + y)) ||
 rewrite(min((x + (y + z)), (w + ((z + u) + v))), (min((x + y), (w + (v + u))) + z)) ||
 rewrite(min((x + (y + z)), (w + ((z + y) - u))), (min((x + z), (w + (z - u))) + y)) ||
 rewrite(min((x + (y + z)), (y + w)), (min((x + z), w) + y)) ||
 rewrite(min((x + (y + z)), (z + w)), (min((x + y), w) + z)) ||
 rewrite(min((x + (y + z)), ((w + y) + u)), (min((x + z), (w + u)) + y)) ||
 rewrite(min((x + (y + z)), ((w + z) + u)), (min((x + y), (w + u)) + z)) ||
 rewrite(min((x + (y + z)), ((w + (u + y)) + v)), (min((x + z), (w + (u + v))) + y)) ||
 rewrite(min((x + (y + z)), ((w + (u + z)) + v)), (min((x + y), (w + (u + v))) + z)) ||
 rewrite(min((x + (y + z)), ((w + (y + u)) + v)), (min((x + z), ((w + v) + u)) + y)) ||
 rewrite(min((x + (y + z)), ((w + (z + u)) + v)), (min((x + y), ((w + v) + u)) + z)) ||
 rewrite(min((x + (y + z)), ((y + w) + u)), (min((x + z), (u + w)) + y)) ||
 rewrite(min((x + (y + z)), ((z + w) + u)), (min((x + y), (u + w)) + z)) ||
 rewrite(min((x + (y + z)), (((w + y) + u) + v)), (min((x + z), ((w + v) + u)) + y)) ||
 rewrite(min((x + (y + z)), (((w + z) + u) + v)), (min((x + y), ((w + v) + u)) + z)) ||
 rewrite(min((x + (y + z)), (((y + w) + u) + v)), (min((x + z), ((v + w) + u)) + y)) ||
 rewrite(min((x + (y + z)), (((z + w) + u) + v)), (min((x + y), ((v + w) + u)) + z)) ||
 rewrite(min((x + (y + z)), (((z + y) - w) + u)), (min((u + (y - w)), (x + y)) + z)) ||
 rewrite(min((x + (y + (z + w))), (u + w)), (min((x + (y + z)), u) + w)) ||
 rewrite(min((x + (y + (z + w))), (u + z)), (min((x + (y + w)), u) + z)) ||
 rewrite(min((x + (y + (z + w))), (u + (w + z))), (min((x + y), u) + (w + z))) ||
 rewrite(min((x + (y + (z + w))), (w + u)), (min((x + (y + z)), u) + w)) ||
 rewrite(min((x + (y + (z + w))), (z + u)), (min((x + (y + w)), u) + z)) ||
 rewrite(min((x + (y + (z + w))), ((w + z) + u)), (min((x + y), u) + (w + z))) ||
 rewrite(min((x + (y + (z + (w + u)))), (u + v)), (min((x + (y + (z + w))), v) + u)) ||
 rewrite(min((x + (y + (z + (w + u)))), (v + u)), (min((x + (y + (z + w))), v) + u)) ||
 rewrite(min((x + (y + (z + (w + u)))), (v + w)), (min((x + (y + (z + u))), v) + w)) ||
 rewrite(min((x + (y + (z + (w + u)))), (w + v)), (min((x + (y + (z + u))), v) + w)) ||
 rewrite(min((x + (y + ((z + w) + u))), (v + w)), (min((x + (y + (z + u))), v) + w)) ||
 rewrite(min((x + (y + ((z + w) + u))), (v + z)), (min((x + (y + (w + u))), v) + z)) ||
 rewrite(min((x + (y + ((z + w) + u))), (w + v)), (min((x + (y + (z + u))), v) + w)) ||
 rewrite(min((x + (y + ((z + w) + u))), (z + v)), (min((x + (y + (w + u))), v) + z)) ||
 rewrite(min((x + (y + (z - w))), (u + (v + z))), (min((x + (y - w)), (u + v)) + z)) ||
 rewrite(min((x + (y + (z - w))), (u + (z + v))), (min((x + (y - w)), (u + v)) + z)) ||
 rewrite(min((x + (y + (z - w))), (u + (z - v))), (min(((y + x) - w), (u - v)) + z)) ||
 rewrite(min((x + (y + (z - w))), ((u + z) + v)), (min((x + (y - w)), (u + v)) + z)) ||
 rewrite(min((x + (y + (z - w))), ((z + u) + v)), (min((x + (y - w)), (v + u)) + z)) ||
 rewrite(min((x + (y + (z - w))), ((z - u) + v)), (min(((y + x) - w), (v - u)) + z)) ||
 rewrite(min((x + (y + (z*w))), (u + (w*z))), (min((x + y), u) + (w*z))) ||
 rewrite(min((x + (y + (z*w))), ((w*z) + u)), (min(u, (y + x)) + (w*z))) ||
 rewrite(min((x + ((y + z) + w)), (u + y)), (min((x + (z + w)), u) + y)) ||
 rewrite(min((x + ((y + z) + w)), (u + z)), (min((x + (y + w)), u) + z)) ||
 rewrite(min((x + ((y + z) + w)), (u + (z + y))), (min((x + w), u) + (z + y))) ||
 rewrite(min((x + ((y + z) + w)), (y + u)), (min((x + (z + w)), u) + y)) ||
 rewrite(min((x + ((y + z) + w)), (z + u)), (min((x + (y + w)), u) + z)) ||
 rewrite(min((x + ((y + z) + w)), ((z + y) + u)), (min((x + w), u) + (z + y))) ||
 rewrite(min((x + ((y + (z + w)) + u)), (v + w)), (min((x + ((y + z) + u)), v) + w)) ||
 rewrite(min((x + ((y + (z + w)) + u)), (v + z)), (min((x + ((y + w) + u)), v) + z)) ||
 rewrite(min((x + ((y + (z + w)) + u)), (w + v)), (min((x + ((y + z) + u)), v) + w)) ||
 rewrite(min((x + ((y + (z + w)) + u)), (z + v)), (min((x + ((y + w) + u)), v) + z)) ||
 rewrite(min((x + (((y + z) + w) + u)), (v + y)), (min((x + ((z + w) + u)), v) + y)) ||
 rewrite(min((x + (((y + z) + w) + u)), (v + z)), (min((x + ((y + w) + u)), v) + z)) ||
 rewrite(min((x + (((y + z) + w) + u)), (y + v)), (min((x + ((z + w) + u)), v) + y)) ||
 rewrite(min((x + (((y + z) + w) + u)), (z + v)), (min((x + ((y + w) + u)), v) + z)) ||
 rewrite(min((x + ((y - z) + w)), (u + (v + y))), (min((x + (w - z)), (u + v)) + y)) ||
 rewrite(min((x + ((y - z) + w)), (u + (y + v))), (min((x + (w - z)), (u + v)) + y)) ||
 rewrite(min((x + ((y - z) + w)), (u + (y - v))), (y + min((x - (z - w)), (u - v)))) ||
 rewrite(min((x + ((y - z) + w)), ((u + y) + v)), (min((x + (w - z)), (u + v)) + y)) ||
 rewrite(min((x + ((y - z) + w)), ((y + u) + v)), (min((x + (w - z)), (v + u)) + y)) ||
 rewrite(min((x + ((y - z) + w)), ((y - u) + v)), (min((x - (z - w)), (v - u)) + y)) ||
 rewrite(min((x + ((y*z) + w)), (u + (z*y))), (min((x + w), u) + (z*y))) ||
 rewrite(min((x + ((y*z) + w)), ((z*y) + u)), (min((x + w), u) + (z*y))) ||
 rewrite(min((x + (y - z)), (w + (u + y))), (min((x - z), (w + u)) + y)) ||
 rewrite(min((x + (y - z)), (w + (u + (v + y)))), (min((x - z), (w + (u + v))) + y)) ||
 rewrite(min((x + (y - z)), (w + (u + (y + v)))), (min((x - z), (w + (u + v))) + y)) ||
 rewrite(min((x + (y - z)), (w + (u + (y - v)))), (y + min((x - z), (w + (u - v))))) ||
 rewrite(min((x + (y - z)), (w + (y + u))), (min((x - z), (w + u)) + y)) ||
 rewrite(min((x + (y - z)), (w + ((u + y) + v))), (min((x - z), (w + (u + v))) + y)) ||
 rewrite(min((x + (y - z)), (w + ((y + u) + v))), (min((x - z), (w + (v + u))) + y)) ||
 rewrite(min((x + (y - z)), (w + ((y - u) + v))), (min((x - z), (w + (v - u))) + y)) ||
 rewrite(min((x + (y - z)), (w + (u - z))), (min((x + y), (w + u)) - z)) ||
 rewrite(min((x + (y - z)), (w + (y - u))), (min((x - z), (w - u)) + y)) ||
 rewrite(min((x + (y - z)), ((w + y) + u)), (min((x - z), (w + u)) + y)) ||
 rewrite(min((x + (y - z)), ((w + (u + y)) + v)), (min((x - z), (w + (u + v))) + y)) ||
 rewrite(min((x + (y - z)), ((w + (y + u)) + v)), (min((x - z), ((w + v) + u)) + y)) ||
 rewrite(min((x + (y - z)), ((w + (y - u)) + v)), (min((x - z), ((w + v) - u)) + y)) ||
 rewrite(min((x + (y - z)), ((y + w) + u)), (min((x - z), (u + w)) + y)) ||
 rewrite(min((x + (y - z)), (((w + y) + u) + v)), (min((x - z), ((w + v) + u)) + y)) ||
 rewrite(min((x + (y - z)), (((y + w) + u) + v)), (min((x - z), ((v + w) + u)) + y)) ||
 rewrite(min((x + (y - z)), (((y - w) + u) + v)), (min((x - z), ((v - w) + u)) + y)) ||
 rewrite(min((x + (y - z)), ((w - z) + u)), (min((x + y), (w + u)) - z)) ||
 rewrite(min((x + (y - z)), ((y - w) + u)), (min((x - z), (u - w)) + y)) ||
 rewrite(min((x + ((y + z) - w)), (u + (y - v))), (y + min(((x + z) - w), (u - v)))) ||
 rewrite(min((x + ((y + z) - w)), (u + (z - v))), (z + min(((y + x) - w), (u - v)))) ||
 rewrite(min((x + ((y + z) - w)), (w + (u + y))), (min((x + (z - w)), (w + u)) + y)) ||
 rewrite(min((x + ((y + z) - w)), (w + (u + z))), (min((x + (y - w)), (w + u)) + z)) ||
 rewrite(min((x + ((y + z) - w)), (w + (y + u))), (min((x + (z - w)), (w + u)) + y)) ||
 rewrite(min((x + ((y + z) - w)), (w + (z + u))), (min((x + (y - w)), (w + u)) + z)) ||
 rewrite(min((x + ((y + z) - w)), ((u + y) + w)), (min((x + (z - w)), (u + w)) + y)) ||
 rewrite(min((x + ((y + z) - w)), ((u + z) + w)), (min((x + (y - w)), (u + w)) + z)) ||
 rewrite(min((x + ((y + z) - w)), ((y + u) + w)), (min((x + (z - w)), (w + u)) + y)) ||
 rewrite(min((x + ((y + z) - w)), ((z + u) + w)), (min((x + (y - w)), (w + u)) + z)) ||
 rewrite(min((x + ((y + z) - w)), ((y - u) + v)), (min(((x + z) - w), (v - u)) + y)) ||
 rewrite(min((x + ((y + z) - w)), ((z - u) + v)), (min(((y + x) - w), (v - u)) + z)) ||
 rewrite(min((x + (y*z)), (w + (u + (z*y)))), (min(x, (w + u)) + (z*y))) ||
 rewrite(min((x + (y*z)), (w + ((z*y) + u))), (min((w + u), x) + (z*y))) ||
 rewrite(min((x + (y*z)), ((w + (z*y)) + u)), (min((u + w), x) + (z*y))) ||
 rewrite(min((x + (y*z)), (((z*y) + w) + u)), (min((u + w), x) + (y*z))) ||
 rewrite(min((x + (((y - x)/c0)*c0)), (y + c1)), (y + c1), ((c1 <= -1) && ((c1 + -1) <= ((-1/c0)*c0)))) ||
 rewrite(min(((x + y) + z), (w + x)), (min((y + z), w) + x)) ||
 rewrite(min(((x + y) + z), (w + y)), (min((x + z), w) + y)) ||
 rewrite(min(((x + y) + z), (w + (u + x))), (min((y + z), (w + u)) + x)) ||
 rewrite(min(((x + y) + z), (w + (u + y))), (min((x + z), (w + u)) + y)) ||
 rewrite(min(((x + y) + z), (w + (u + (v + x)))), (min((y + z), (w + (u + v))) + x)) ||
 rewrite(min(((x + y) + z), (w + (u + (v + y)))), (min((x + z), (w + (u + v))) + y)) ||
 rewrite(min(((x + y) + z), (w + (u + (x + v)))), (min((y + z), (w + (u + v))) + x)) ||
 rewrite(min(((x + y) + z), (w + (u + (y + v)))), (min((x + z), (w + (u + v))) + y)) ||
 rewrite(min(((x + y) + z), (w + (x + u))), (min((y + z), (w + u)) + x)) ||
 rewrite(min(((x + y) + z), (w + (y + u))), (min((x + z), (w + u)) + y)) ||
 rewrite(min(((x + y) + z), (w + ((u + x) + v))), (min((y + z), (w + (u + v))) + x)) ||
 rewrite(min(((x + y) + z), (w + ((u + y) + v))), (min((x + z), (w + (u + v))) + y)) ||
 rewrite(min(((x + y) + z), (w + ((x + u) + v))), (min((y + z), (w + (v + u))) + x)) ||
 rewrite(min(((x + y) + z), (w + ((y + u) + v))), (min((x + z), (w + (v + u))) + y)) ||
 rewrite(min(((x + y) + z), (w + ((y + x) - u))), (min((y + z), (w + (y - u))) + x)) ||
 rewrite(min(((x + y) + z), (x + w)), (min((y + z), w) + x)) ||
 rewrite(min(((x + y) + z), (y + w)), (min((x + z), w) + y)) ||
 rewrite(min(((x + y) + z), ((w + x) + u)), (min((y + z), (w + u)) + x)) ||
 rewrite(min(((x + y) + z), ((w + y) + u)), (min((x + z), (w + u)) + y)) ||
 rewrite(min(((x + y) + z), ((w + (u + x)) + v)), (min((y + z), (w + (u + v))) + x)) ||
 rewrite(min(((x + y) + z), ((w + (u + y)) + v)), (min((x + z), (w + (u + v))) + y)) ||
 rewrite(min(((x + y) + z), ((w + (x + u)) + v)), (min((y + z), ((w + v) + u)) + x)) ||
 rewrite(min(((x + y) + z), ((w + (y + u)) + v)), (min((x + z), ((w + v) + u)) + y)) ||
 rewrite(min(((x + y) + z), ((x + w) + u)), (min((y + z), (u + w)) + x)) ||
 rewrite(min(((x + y) + z), ((y + w) + u)), (min((x + z), (u + w)) + y)) ||
 rewrite(min(((x + y) + z), (((w + x) + u) + v)), (min((y + z), ((w + v) + u)) + x)) ||
 rewrite(min(((x + y) + z), (((w + y) + u) + v)), (min((x + z), ((w + v) + u)) + y)) ||
 rewrite(min(((x + y) + z), (((x + w) + u) + v)), (min((y + z), ((v + w) + u)) + x)) ||
 rewrite(min(((x + y) + z), (((y + w) + u) + v)), (min((x + z), ((v + w) + u)) + y)) ||
 rewrite(min(((x + y) + z), (((y + x) - w) + u)), (min(((u + y) - w), (y + z)) + x)) ||
 rewrite(min(((x + (y + z)) + w), (u + y)), (min(((x + z) + w), u) + y)) ||
 rewrite(min(((x + (y + z)) + w), (u + z)), (min(((x + y) + w), u) + z)) ||
 rewrite(min(((x + (y + z)) + w), (u + (z + y))), (min((x + w), u) + (z + y))) ||
 rewrite(min(((x + (y + z)) + w), (y + u)), (min(((x + z) + w), u) + y)) ||
 rewrite(min(((x + (y + z)) + w), (z + u)), (min(((x + y) + w), u) + z)) ||
 rewrite(min(((x + (y + z)) + w), ((z + y) + u)), (min(u, (x + w)) + (z + y))) ||
 rewrite(min(((x + (y + (z + w))) + u), (v + w)), (min(((x + (y + z)) + u), v) + w)) ||
 rewrite(min(((x + (y + (z + w))) + u), (v + z)), (min(((x + (y + w)) + u), v) + z)) ||
 rewrite(min(((x + (y + (z + w))) + u), (w + v)), (min(((x + (y + z)) + u), v) + w)) ||
 rewrite(min(((x + (y + (z + w))) + u), (z + v)), (min(((x + (y + w)) + u), v) + z)) ||
 rewrite(min(((x + ((y + z) + w)) + u), (v + y)), (min(((x + (z + w)) + u), v) + y)) ||
 rewrite(min(((x + ((y + z) + w)) + u), (v + z)), (min(((x + (y + w)) + u), v) + z)) ||
 rewrite(min(((x + ((y + z) + w)) + u), (y + v)), (min(((x + (z + w)) + u), v) + y)) ||
 rewrite(min(((x + ((y + z) + w)) + u), (z + v)), (min(((x + (y + w)) + u), v) + z)) ||
 rewrite(min(((x + (y - z)) + w), (u + (v + y))), (min(((x - z) + w), (u + v)) + y)) ||
 rewrite(min(((x + (y - z)) + w), (u + (y + v))), (min(((x - z) + w), (u + v)) + y)) ||
 rewrite(min(((x + (y - z)) + w), (u + (y - v))), (min(((x + w) - z), (u - v)) + y)) ||
 rewrite(min(((x + (y - z)) + w), ((u + y) + v)), (min(((x - z) + w), (u + v)) + y)) ||
 rewrite(min(((x + (y - z)) + w), ((y + u) + v)), (min(((x - z) + w), (v + u)) + y)) ||
 rewrite(min(((x + (y - z)) + w), ((y - u) + v)), (min(((x + w) - z), (v - u)) + y)) ||
 rewrite(min(((x + (y*z)) + w), (u + (z*y))), (min((x + w), u) + (z*y))) ||
 rewrite(min(((x + (y*z)) + w), ((z*y) + u)), (min((x + w), u) + (z*y))) ||
 rewrite(min((((x + y) + z) + w), (u + x)), (min(((y + z) + w), u) + x)) ||
 rewrite(min((((x + y) + z) + w), (u + y)), (min(((x + z) + w), u) + y)) ||
 rewrite(min((((x + y) + z) + w), (u + (y + x))), (min((z + w), u) + (y + x))) ||
 rewrite(min((((x + y) + z) + w), (x + u)), (min(((y + z) + w), u) + x)) ||
 rewrite(min((((x + y) + z) + w), (y + u)), (min(((x + z) + w), u) + y)) ||
 rewrite(min((((x + y) + z) + w), ((y + x) + u)), (min((z + w), u) + (y + x))) ||
 rewrite(min((((x + (y + z)) + w) + u), (v + y)), (min((((x + z) + w) + u), v) + y)) ||
 rewrite(min((((x + (y + z)) + w) + u), (v + z)), (min((((x + y) + w) + u), v) + z)) ||
 rewrite(min((((x + (y + z)) + w) + u), (y + v)), (min((((x + z) + w) + u), v) + y)) ||
 rewrite(min((((x + (y + z)) + w) + u), (z + v)), (min((((x + y) + w) + u), v) + z)) ||
 rewrite(min(((((x + y) + z) + w) + u), (v + x)), (min((((y + z) + w) + u), v) + x)) ||
 rewrite(min(((((x + y) + z) + w) + u), (v + y)), (min((((x + z) + w) + u), v) + y)) ||
 rewrite(min(((((x + y) + z) + w) + u), (x + v)), (min((((y + z) + w) + u), v) + x)) ||
 rewrite(min(((((x + y) + z) + w) + u), (y + v)), (min((((x + z) + w) + u), v) + y)) ||
 rewrite(min((((x - y) + z) + w), (u + (v + x))), (min(((w - y) + z), (u + v)) + x)) ||
 rewrite(min((((x - y) + z) + w), (u + (x + v))), (min(((w - y) + z), (u + v)) + x)) ||
 rewrite(min((((x - y) + z) + w), (u + (x - v))), (min(((w + z) - y), (u - v)) + x)) ||
 rewrite(min((((x - y) + z) + w), ((u + x) + v)), (min(((w - y) + z), (u + v)) + x)) ||
 rewrite(min((((x - y) + z) + w), ((x + u) + v)), (min(((w - y) + z), (v + u)) + x)) ||
 rewrite(min((((x - y) + z) + w), ((x - u) + v)), (min(((w + z) - y), (v - u)) + x)) ||
 rewrite(min((((x*y) + z) + w), (u + (y*x))), (min((z + w), u) + (y*x))) ||
 rewrite(min((((x*y) + z) + w), ((y*x) + u)), (min((w + z), u) + (y*x))) ||
 rewrite(min(((x - y) + z), (w + (u + x))), (min((z - y), (w + u)) + x)) ||
 rewrite(min(((x - y) + z), (w + (u + (v + x)))), (min((z - y), (w + (u + v))) + x)) ||
 rewrite(min(((x - y) + z), (w + (u + (x + v)))), (min((z - y), (w + (u + v))) + x)) ||
 rewrite(min(((x - y) + z), (w + (u + (x - v)))), (x + min((z - y), (w + (u - v))))) ||
 rewrite(min(((x - y) + z), (w + (x + u))), (min((z - y), (w + u)) + x)) ||
 rewrite(min(((x - y) + z), (w + ((u + x) + v))), (min((z - y), (w + (u + v))) + x)) ||
 rewrite(min(((x - y) + z), (w + ((x + u) + v))), (min((z - y), (w + (v + u))) + x)) ||
 rewrite(min(((x - y) + z), (w + ((x - u) + v))), (min((z - y), (w + (v - u))) + x)) ||
 rewrite(min(((x - y) + z), (w + (u - y))), (min((x + z), (w + u)) - y)) ||
 rewrite(min(((x - y) + z), (w + (x - u))), (min((z - y), (w - u)) + x)) ||
 rewrite(min(((x - y) + z), ((w + x) + u)), (min((z - y), (w + u)) + x)) ||
 rewrite(min(((x - y) + z), ((w + (u + x)) + v)), (min((z - y), (w + (u + v))) + x)) ||
 rewrite(min(((x - y) + z), ((w + (x + u)) + v)), (min((z - y), ((w + v) + u)) + x)) ||
 rewrite(min(((x - y) + z), ((w + (x - u)) + v)), (min((z - y), ((w + v) - u)) + x)) ||
 rewrite(min(((x - y) + z), ((x + w) + u)), (min((z - y), (u + w)) + x)) ||
 rewrite(min(((x - y) + z), (((w + x) + u) + v)), (min((z - y), ((w + v) + u)) + x)) ||
 rewrite(min(((x - y) + z), (((x + w) + u) + v)), (min((z - y), ((v + w) + u)) + x)) ||
 rewrite(min(((x - y) + z), (((x - w) + u) + v)), (min((z - y), ((v - w) + u)) + x)) ||
 rewrite(min(((x - y) + z), ((w - y) + u)), (min((x + z), (w + u)) - y)) ||
 rewrite(min(((x - y) + z), ((x - w) + u)), (min((z - y), (u - w)) + x)) ||
 rewrite(min((((x + y) - z) + w), (u + (x - v))), (min(((w + y) - z), (u - v)) + x)) ||
 rewrite(min((((x + y) - z) + w), (u + (y - v))), (min(((x + w) - z), (u - v)) + y)) ||
 rewrite(min((((x + y) - z) + w), (z + (u + x))), (min(((y - z) + w), (z + u)) + x)) ||
 rewrite(min((((x + y) - z) + w), (z + (u + y))), (min(((x - z) + w), (z + u)) + y)) ||
 rewrite(min((((x + y) - z) + w), (z + (x + u))), (min(((y - z) + w), (z + u)) + x)) ||
 rewrite(min((((x + y) - z) + w), (z + (y + u))), (min(((x - z) + w), (z + u)) + y)) ||
 rewrite(min((((x + y) - z) + w), ((u + x) + z)), (min(((y - z) + w), (u + z)) + x)) ||
 rewrite(min((((x + y) - z) + w), ((u + y) + z)), (min(((x - z) + w), (u + z)) + y)) ||
 rewrite(min((((x + y) - z) + w), ((x + u) + z)), (min(((y - z) + w), (z + u)) + x)) ||
 rewrite(min((((x + y) - z) + w), ((y + u) + z)), (min(((x - z) + w), (z + u)) + y)) ||
 rewrite(min((((x + y) - z) + w), ((x - u) + v)), (min(((w + y) - z), (v - u)) + x)) ||
 rewrite(min((((x + y) - z) + w), ((y - u) + v)), (min(((x + w) - z), (v - u)) + y)) ||
 rewrite(min(((x*y) + z), (w + (u + (y*x)))), (min(z, (w + u)) + (y*x))) ||
 rewrite(min(((x*y) + z), (w + ((y*x) + u))), (min((w + u), z) + (y*x))) ||
 rewrite(min(((x*y) + z), ((w + (y*x)) + u)), (min((u + w), z) + (y*x))) ||
 rewrite(min(((x*y) + z), (((y*x) + w) + u)), (min((u + w), z) + (x*y))) ||
 rewrite(min(((((x - y)/c0)*c0) + y), (x + c1)), (x + c1), ((c1 <= -1) && ((c1 + -1) <= ((-1/c0)*c0)))))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(min((min(x, (min(x, y) - z)) - min(x, y)), c0), (c0 - max(z, c0)), (c0 == 0)) ||
 rewrite(min((min(x, (min(x, y) - z)) - min(y, x)), c0), (c0 - max(z, c0)), (c0 == 0)) ||
 rewrite(min((min(x, (min(y, x) - z)) - min(x, y)), c0), (c0 - max(z, c0)), (c0 == 0)) ||
 rewrite(min((min(x, (min(y, x) - z)) - min(y, x)), c0), (c0 - max(z, c0)), (c0 == 0)) ||
 rewrite(min((min((min(x, y) - z), x) - min(x, y)), c0), (c0 - max(z, c0)), (c0 == 0)) ||
 rewrite(min((min((min(x, y) - z), x) - min(y, x)), c0), (c0 - max(z, c0)), (c0 == 0)) ||
 rewrite(min((min((min(x, y) - z), y) - min(x, y)), c0), (c0 - max(z, c0)), (c0 == 0)) ||
 rewrite(min((min((min(x, y) - z), y) - min(y, x)), c0), (c0 - max(z, c0)), (c0 == 0)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((x - y), (z + (w + (u - y)))), (min(x, (z + (w + u))) - y)) ||
 rewrite(min((x - y), (z + ((w - y) + u))), (min(x, (z + (w + u))) - y)) ||
 rewrite(min((x - y), (z + (w - y))), (min(x, (z + w)) - y)) ||
 rewrite(min((x - y), (z + (w - (u + y)))), (min(x, (z + (w - u))) - y)) ||
 rewrite(min((x - y), (z + (w - (y + u)))), (min(x, (z + (w - u))) - y)) ||
 rewrite(min((x - y), (z + (x - (w + u)))), (x - max(y, (w - (z - u))))) ||
 rewrite(min((x - y), ((z + (w - y)) + u)), (min(x, (z + (w + u))) - y)) ||
 rewrite(min((x - y), (((z - y) + w) + u)), (min(x, ((z + u) + w)) - y)) ||
 rewrite(min((x - y), ((x - (z + w)) + u)), (x - max((z - (u - w)), y))) ||
 rewrite(min((x - y), ((z - y) + w)), (min((z + w), x) - y)) ||
 rewrite(min((x - y), ((z - (w + y)) + u)), (min(x, ((z - w) + u)) - y)) ||
 rewrite(min((x - y), ((z - (y + w)) + u)), (min(x, ((z - w) + u)) - y)) ||
 rewrite(min((x - (y + z)), (w + (u - y))), (min((x - z), (w + u)) - y)) ||
 rewrite(min((x - (y + z)), (w + (u - z))), (min((x - y), (w + u)) - z)) ||
 rewrite(min((x - (y + z)), (w + (u - (v + y)))), (min((x - z), (w + (u - v))) - y)) ||
 rewrite(min((x - (y + z)), (w + (u - (v + z)))), (min((x - y), (w + (u - v))) - z)) ||
 rewrite(min((x - (y + z)), (w + (u - (y + v)))), (min((x - z), (w + (u - v))) - y)) ||
 rewrite(min((x - (y + z)), (w + (u - (z + v)))), (min((x - y), (w + (u - v))) - z)) ||
 rewrite(min((x - (y + z)), ((w - y) + u)), (min((x - z), (w + u)) - y)) ||
 rewrite(min((x - (y + z)), ((w - z) + u)), (min((x - y), (w + u)) - z)) ||
 rewrite(min((x - (y + z)), ((w - (u + y)) + v)), (min((x - z), ((w - u) + v)) - y)) ||
 rewrite(min((x - (y + z)), ((w - (u + z)) + v)), (min((x - y), ((w - u) + v)) - z)) ||
 rewrite(min((x - (y + z)), ((w - (y + u)) + v)), (min((x - z), ((w - u) + v)) - y)) ||
 rewrite(min((x - (y + z)), ((w - (z + u)) + v)), (min((x - y), ((w - u) + v)) - z)) ||
 rewrite(min((x - (y + (z - w))), (u + (w - v))), (min((x - (y + z)), (u - v)) + w)) ||
 rewrite(min((x - (y + (z - w))), ((w - u) + v)), (min((x - (y + z)), (v - u)) + w)) ||
 rewrite(min((x - ((y - z) + w)), (u + (z - v))), (min((x - (y + w)), (u - v)) + z)) ||
 rewrite(min((x - ((y - z) + w)), ((z - u) + v)), (min((x - (y + w)), (v - u)) + z)) ||
 rewrite(min((x - (y*z)), (w + (u - (z*y)))), (min(x, (w + u)) - (z*y))) ||
 rewrite(min((x - (y*z)), ((w - (z*y)) + u)), (min((w + u), x) - (z*y))) ||
 rewrite(min(((x + y) - z), (w + x)), (min((y - z), w) + x)) ||
 rewrite(min(((x + y) - z), (w + y)), (min((x - z), w) + y)) ||
 rewrite(min(((x + y) - z), (w + (u + (v + x)))), (min((y - z), (w + (u + v))) + x)) ||
 rewrite(min(((x + y) - z), (w + (u + (v + y)))), (min((x - z), (w + (u + v))) + y)) ||
 rewrite(min(((x + y) - z), (w + (u + (x + v)))), (min((y - z), (w + (u + v))) + x)) ||
 rewrite(min(((x + y) - z), (w + (u + (y + v)))), (min((x - z), (w + (u + v))) + y)) ||
 rewrite(min(((x + y) - z), (w + (u + (x - v)))), (x + min((y - z), (w + (u - v))))) ||
 rewrite(min(((x + y) - z), (w + (u + (y - v)))), (y + min((x - z), (w + (u - v))))) ||
 rewrite(min(((x + y) - z), (w + (x + y))), (min((y - z), (w + y)) + x)) ||
 rewrite(min(((x + y) - z), (w + (y + x))), (min((x - z), (w + x)) + y)) ||
 rewrite(min(((x + y) - z), (w + ((u + x) + v))), (min((y - z), (w + (u + v))) + x)) ||
 rewrite(min(((x + y) - z), (w + ((u + y) + v))), (min((x - z), (w + (u + v))) + y)) ||
 rewrite(min(((x + y) - z), (w + ((x + u) + v))), (min((y - z), (w + (v + u))) + x)) ||
 rewrite(min(((x + y) - z), (w + ((y + u) + v))), (min((x - z), (w + (v + u))) + y)) ||
 rewrite(min(((x + y) - z), (w + ((x - u) + v))), (min((y - z), (w + (v - u))) + x)) ||
 rewrite(min(((x + y) - z), (w + ((y - u) + v))), (min((x - z), (w + (v - u))) + y)) ||
 rewrite(min(((x + y) - z), (w + ((x + y) - u))), (min(((w + y) - u), (y - z)) + x)) ||
 rewrite(min(((x + y) - z), (w + ((y + x) - u))), (min(((w + y) - u), (y - z)) + x)) ||
 rewrite(min(((x + y) - z), (x + w)), (min((y - z), w) + x)) ||
 rewrite(min(((x + y) - z), (y + w)), (min((x - z), w) + y)) ||
 rewrite(min(((x + y) - z), ((w + (u + x)) + v)), (min((y - z), (w + (u + v))) + x)) ||
 rewrite(min(((x + y) - z), ((w + (u + y)) + v)), (min((x - z), (w + (u + v))) + y)) ||
 rewrite(min(((x + y) - z), ((w + (x + u)) + v)), (min((y - z), ((w + v) + u)) + x)) ||
 rewrite(min(((x + y) - z), ((w + (y + u)) + v)), (min((x - z), ((w + v) + u)) + y)) ||
 rewrite(min(((x + y) - z), ((w + (x - u)) + v)), (min((y - z), ((w + v) - u)) + x)) ||
 rewrite(min(((x + y) - z), ((w + (y - u)) + v)), (min((x - z), ((w + v) - u)) + y)) ||
 rewrite(min(((x + y) - z), ((x + y) + w)), (min((y - z), (w + y)) + x)) ||
 rewrite(min(((x + y) - z), ((y + x) + w)), (min((x - z), (w + x)) + y)) ||
 rewrite(min(((x + y) - z), (((w + x) + u) + v)), (min((y - z), ((w + v) + u)) + x)) ||
 rewrite(min(((x + y) - z), (((w + y) + u) + v)), (min((x - z), ((w + v) + u)) + y)) ||
 rewrite(min(((x + y) - z), (((x + w) + u) + v)), (min((y - z), ((v + w) + u)) + x)) ||
 rewrite(min(((x + y) - z), (((y + w) + u) + v)), (min((x - z), ((v + w) + u)) + y)) ||
 rewrite(min(((x + y) - z), (((x - w) + u) + v)), (min((y - z), ((v - w) + u)) + x)) ||
 rewrite(min(((x + y) - z), (((y - w) + u) + v)), (min((x - z), ((v - w) + u)) + y)) ||
 rewrite(min(((x + y) - z), (((x + y) - w) + u)), (min((y - z), ((u + y) - w)) + x)) ||
 rewrite(min(((x + y) - z), (((y + x) - w) + u)), (min((x - z), ((u + x) - w)) + y)) ||
 rewrite(min(((x + (y + z)) - w), (u + y)), (min(((x + z) - w), u) + y)) ||
 rewrite(min(((x + (y + z)) - w), (u + z)), (min(((x + y) - w), u) + z)) ||
 rewrite(min(((x + (y + z)) - w), (y + u)), (min(((x + z) - w), u) + y)) ||
 rewrite(min(((x + (y + z)) - w), (z + u)), (min(((x + y) - w), u) + z)) ||
 rewrite(min((((x + y) + z) - w), (u + x)), (min(((y + z) - w), u) + x)) ||
 rewrite(min((((x + y) + z) - w), (u + y)), (min(((x + z) - w), u) + y)) ||
 rewrite(min((((x + y) + z) - w), (x + u)), (min(((y + z) - w), u) + x)) ||
 rewrite(min((((x + y) + z) - w), (y + u)), (min(((x + z) - w), u) + y)) ||
 rewrite(min(((x - y) - z), (w + (u - y))), (min((x - z), (w + u)) - y)) ||
 rewrite(min(((x - y) - z), ((w - y) + u)), (min((x - z), (w + u)) - y)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min((x - y), (z - (w + y))), (min((z - w), x) - y)) ||
 rewrite(min((x - y), (z - (y + w))), (min((z - w), x) - y)) ||
 rewrite(min((x - (y + z)), (w - y)), (min((x - z), w) - y)) ||
 rewrite(min((x - (y + z)), (w - z)), (min((x - y), w) - z)) ||
 rewrite(min((x - (y*z)), (w - (u + (z*y)))), (min((w - u), x) - (z*y))) ||
 rewrite(min((x - (y*z)), (w - ((z*y) + u))), (min((w - u), x) - (z*y))) ||
 rewrite(min(((x - y) - z), (x - w)), (x - max(w, (z + y)))) ||
 rewrite(min((((x + y) - z) - w), (x - u)), (x - max((z - (y - w)), u))) ||
 rewrite(min((((x + y) - z) - w), (y - u)), (y - max(((w + z) - x), u))))))) ||
((a.node_type() == IRNodeType::Mul) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(min((((x + c0)/c1)*c1), (x + c2)), (x + c2), (((c2 + -1) <= (c0 + c1)) && (((c1 + c2) + -1) <= c0))))))) ||
((a.node_type() == IRNodeType::Div) && EVAL_IN_LAMBDA(
 rewrite(min((min(x, min(y, (x + c0)))/c1), z), min((min(y, x)/c1), z), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min((min(x, min((x + c0), y))/c1), z), min((min(x, y)/c1), z), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min((min(min(x, (y + c0)), y)/c1), z), min((min(x, y)/c1), z), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))) ||
 rewrite(min((min(min((x + c0), y), x)/c1), z), min((min(y, x)/c1), z), (((0 <= (min(c0, 0)/c1)) && ((min(c0, 0)/c1) == 0)) && (0 <= ((min(c0, -1) + 1)/c1)))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite(min(min(x, y), x), min(y, x)) ||
 rewrite(min(min(x, y), y), min(x, y)) ||
 rewrite(min(min(x, (y + c0)), y), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, (y + (z - ((w + y)/c0)))), u), min(min(x, (z - ((w - y)/c0))), u), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(min(x, (y + (z - ((y + w)/c0)))), u), min(min(x, (z - ((w - y)/c0))), u), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(min(x, ((y - ((z + w)/c0)) + w)), u), min(min(x, (y - ((z - w)/c0))), u), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(min(x, ((y - ((z + w)/c0)) + z)), u), min(min(x, (y - ((w - z)/c0))), u), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(min(x, (min(y, z) + c0)), y), min(min(x, (z + c0)), y), (0 <= c0)) ||
 rewrite(min(min(x, (min(y, z) + c0)), y), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(y, z) + c0)), z), min(min(x, (y + c0)), z), (0 <= c0)) ||
 rewrite(min(min(x, (min(y, z) + c0)), z), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(y, min(z, w)) + c0)), w), min(x, (min(y, min(z, w)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(y, min(z, w)) + c0)), z), min(x, (min(y, min(z, w)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(min(y, z), w) + c0)), y), min(x, (min(min(y, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(min(y, z), w) + c0)), z), min(x, (min(min(y, z), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (min(min(y, z), (y + c0)) + c1)), y), min(min(x, (min(y, z) + c1)), y), ((min(c1, 0) <= (min(c0, 0) + c1)) && (min((min(c0, -1) + c1), -1) == (min(c1, 0) + -1)))) ||
 rewrite(min(min(x, (min(min(y, z), (z + c0)) + c1)), z), min(min(x, (min(y, z) + c1)), z), ((min(c1, 0) <= (min(c0, 0) + c1)) && (min((min(c0, -1) + c1), -1) == (min(c1, 0) + -1)))) ||
 rewrite(min(min(x, min(y, z)), y), min(x, min(y, z))) ||
 rewrite(min(min(x, min(y, z)), z), min(x, min(y, z))) ||
 rewrite(min(min(x, min(y, (min(z, w) + c0))), w), min(min(x, min(y, (z + c0))), w), (0 <= c0)) ||
 rewrite(min(min(x, min(y, (min(z, w) + c0))), z), min(min(x, min(y, (w + c0))), z), (0 <= c0)) ||
 rewrite(min(min(x, min(y, (min(z, min(w, u)) + c0))), u), min(min(x, min(y, (min(z, w) + c0))), u), (0 <= c0)) ||
 rewrite(min(min(x, min(y, (min(z, min(w, u)) + c0))), w), min(min(x, min(y, (min(z, u) + c0))), w), (0 <= c0)) ||
 rewrite(min(min(x, min(y, (min(min(z, w), u) + c0))), w), min(min(x, min(y, (min(z, u) + c0))), w), (0 <= c0)) ||
 rewrite(min(min(x, min(y, (min(min(z, w), u) + c0))), z), min(min(x, min(y, (min(w, u) + c0))), z), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, min(w, min(u, v))))), u), min(x, min(y, min(z, min(w, min(u, v)))))) ||
 rewrite(min(min(x, min(y, min(z, min(w, min(u, v))))), v), min(x, min(y, min(z, min(w, min(u, v)))))) ||
 rewrite(min(min(x, min(y, min(z, min(min(w, u), v)))), u), min(x, min(y, min(z, min(min(w, u), v))))) ||
 rewrite(min(min(x, min(y, min(z, min(min(w, u), v)))), w), min(x, min(y, min(z, min(min(w, u), v))))) ||
 rewrite(min(min(x, min(y, min(min(z, min(w, u)), v))), u), min(x, min(y, min(min(z, min(w, u)), v)))) ||
 rewrite(min(min(x, min(y, min(min(z, min(w, u)), v))), w), min(x, min(y, min(min(z, min(w, u)), v)))) ||
 rewrite(min(min(x, min(y, min(min(min(z, w), u), v))), w), min(x, min(y, min(min(min(z, w), u), v)))) ||
 rewrite(min(min(x, min(y, min(min(min(z, w), u), v))), z), min(x, min(y, min(min(min(z, w), u), v)))) ||
 rewrite(min(min(x, min((min(y, z) + c0), w)), y), min(min(x, min((z + c0), w)), y), (0 <= c0)) ||
 rewrite(min(min(x, min((min(y, z) + c0), w)), z), min(min(x, min((y + c0), w)), z), (0 <= c0)) ||
 rewrite(min(min(x, min((min(y, min(z, w)) + c0), u)), w), min(min(x, min((min(y, z) + c0), u)), w), (0 <= c0)) ||
 rewrite(min(min(x, min((min(y, min(z, w)) + c0), u)), z), min(min(x, min((min(y, w) + c0), u)), z), (0 <= c0)) ||
 rewrite(min(min(x, min((min(min(y, z), w) + c0), u)), y), min(min(x, min((min(z, w) + c0), u)), y), (0 <= c0)) ||
 rewrite(min(min(x, min((min(min(y, z), w) + c0), u)), z), min(min(x, min((min(y, w) + c0), u)), z), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, min(z, min(w, u))), v)), u), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(min(x, min(min(y, min(z, min(w, u))), v)), w), min(x, min(min(y, min(z, min(w, u))), v))) ||
 rewrite(min(min(x, min(min(y, min(min(z, w), u)), v)), w), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(min(x, min(min(y, min(min(z, w), u)), v)), z), min(x, min(min(y, min(min(z, w), u)), v))) ||
 rewrite(min(min(x, min(min(min(y, min(z, w)), u), v)), w), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(min(x, min(min(min(y, min(z, w)), u), v)), z), min(x, min(min(min(y, min(z, w)), u), v))) ||
 rewrite(min(min(x, min(min(min(min(y, z), w), u), v)), y), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(min(x, min(min(min(min(y, z), w), u), v)), z), min(x, min(min(min(min(y, z), w), u), v))) ||
 rewrite(min(min((x + c0), y), x), min(y, x), (0 <= c0)) ||
 rewrite(min(min((x + (y - ((x + z)/c0))), w), u), min(min((y - ((z - x)/c0)), w), u), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(min((x + (y - ((z + x)/c0))), w), u), min(min((y - ((z - x)/c0)), w), u), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(min(((x - ((y + z)/c0)) + y), w), u), min(min((x - ((z - y)/c0)), w), u), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(min(((x - ((y + z)/c0)) + z), w), u), min(min((x - ((y - z)/c0)), w), u), (((-1/c0) <= 0) && ((-12/c0) <= -5))) ||
 rewrite(min(min((min(x, y) + c0), z), x), min(min((y + c0), z), x), (0 <= c0)) ||
 rewrite(min(min((min(x, y) + c0), z), x), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(min((min(x, y) + c0), z), y), min(min((x + c0), z), y), (0 <= c0)) ||
 rewrite(min(min((min(x, y) + c0), z), y), min((min(x, y) + c0), z), (c0 <= 0)) ||
 rewrite(min(min((min(x, min(y, z)) + c0), w), y), min((min(x, min(y, z)) + c0), w), (c0 <= 0)) ||
 rewrite(min(min((min(x, min(y, z)) + c0), w), z), min((min(x, min(y, z)) + c0), w), (c0 <= 0)) ||
 rewrite(min(min((min(min(x, y), z) + c0), w), x), min((min(min(x, y), z) + c0), w), (c0 <= 0)) ||
 rewrite(min(min((min(min(x, y), z) + c0), w), y), min((min(min(x, y), z) + c0), w), (c0 <= 0)) ||
 rewrite(min(min((min(min(x, y), (x + c0)) + c1), z), x), min(min((min(x, y) + c1), z), x), ((min(c1, 0) <= (min(c0, 0) + c1)) && (min((min(c0, -1) + c1), -1) == (min(c1, 0) + -1)))) ||
 rewrite(min(min((min(min(x, y), (y + c0)) + c1), z), y), min(min((min(x, y) + c1), z), y), ((min(c1, 0) <= (min(c0, 0) + c1)) && (min((min(c0, -1) + c1), -1) == (min(c1, 0) + -1)))) ||
 rewrite(min(min(min(x, y), z), x), min(min(y, z), x)) ||
 rewrite(min(min(min(x, y), z), y), min(min(x, z), y)) ||
 rewrite(min(min(min(x, (min(y, z) + c0)), w), y), min(min(min(x, (z + c0)), w), y), (0 <= c0)) ||
 rewrite(min(min(min(x, (min(y, z) + c0)), w), z), min(min(min(x, (y + c0)), w), z), (0 <= c0)) ||
 rewrite(min(min(min(x, (min(y, min(z, w)) + c0)), u), w), min(min(min(x, (min(y, z) + c0)), u), w), (0 <= c0)) ||
 rewrite(min(min(min(x, (min(y, min(z, w)) + c0)), u), z), min(min(min(x, (min(y, w) + c0)), u), z), (0 <= c0)) ||
 rewrite(min(min(min(x, (min(min(y, z), w) + c0)), u), y), min(min(min(x, (min(z, w) + c0)), u), y), (0 <= c0)) ||
 rewrite(min(min(min(x, (min(min(y, z), w) + c0)), u), z), min(min(min(x, (min(y, w) + c0)), u), z), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, min(z, min(w, u)))), v), u), min(min(min(x, min(y, min(z, w))), v), u)) ||
 rewrite(min(min(min(x, min(y, min(z, min(w, u)))), v), w), min(min(min(x, min(y, min(z, u))), v), w)) ||
 rewrite(min(min(min(x, min(y, min(min(z, w), u))), v), w), min(min(min(x, min(y, min(z, u))), v), w)) ||
 rewrite(min(min(min(x, min(y, min(min(z, w), u))), v), z), min(min(min(x, min(y, min(w, u))), v), z)) ||
 rewrite(min(min(min(x, min(min(y, min(z, w)), u)), v), w), min(min(min(x, min(min(y, z), u)), v), w)) ||
 rewrite(min(min(min(x, min(min(y, min(z, w)), u)), v), z), min(min(min(x, min(min(y, w), u)), v), z)) ||
 rewrite(min(min(min(x, min(min(min(y, z), w), u)), v), y), min(min(min(x, min(min(z, w), u)), v), y)) ||
 rewrite(min(min(min(x, min(min(min(y, z), w), u)), v), z), min(min(min(x, min(min(y, w), u)), v), z)) ||
 rewrite(min(min(min((min(x, y) + c0), z), w), x), min(min(min((y + c0), z), w), x), (0 <= c0)) ||
 rewrite(min(min(min((min(x, y) + c0), z), w), y), min(min(min((x + c0), z), w), y), (0 <= c0)) ||
 rewrite(min(min(min((min(x, min(y, z)) + c0), w), u), y), min(min(min((min(x, z) + c0), w), u), y), (0 <= c0)) ||
 rewrite(min(min(min((min(x, min(y, z)) + c0), w), u), z), min(min(min((min(x, y) + c0), w), u), z), (0 <= c0)) ||
 rewrite(min(min(min((min(min(x, y), z) + c0), w), u), x), min(min(min((min(y, z) + c0), w), u), x), (0 <= c0)) ||
 rewrite(min(min(min((min(min(x, y), z) + c0), w), u), y), min(min(min((min(x, z) + c0), w), u), y), (0 <= c0)) ||
 rewrite(min(min(min(min(x, min(y, min(z, w))), u), v), w), min(min(min(min(x, min(y, z)), u), v), w)) ||
 rewrite(min(min(min(min(x, min(y, min(z, w))), u), v), z), min(min(min(min(x, min(y, w)), u), v), z)) ||
 rewrite(min(min(min(min(x, min(min(y, z), w)), u), v), y), min(min(min(min(x, min(z, w)), u), v), y)) ||
 rewrite(min(min(min(min(x, min(min(y, z), w)), u), v), z), min(min(min(min(x, min(y, w)), u), v), z)) ||
 rewrite(min(min(min(min(min(x, min(y, z)), w), u), v), y), min(min(min(min(min(x, z), w), u), v), y)) ||
 rewrite(min(min(min(min(min(x, min(y, z)), w), u), v), z), min(min(min(min(min(x, y), w), u), v), z)) ||
 rewrite(min(min(min(min(min(min(x, y), z), w), u), v), x), min(min(min(min(min(y, z), w), u), v), x)) ||
 rewrite(min(min(min(min(min(min(x, y), z), w), u), v), y), min(min(min(min(min(x, z), w), u), v), y)) ||
 rewrite(min(min(select(x, y, (z/c0)), (w + c1)), w), min(select(x, y, (z/c0)), w), (0 <= c1)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(min(x, y), (x + c0)), min(y, x), (0 <= c0)) ||
 rewrite(min(min(x, y), (x + c0)), min(y, (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (y + c0)), min(x, y), (0 <= c0)) ||
 rewrite(min(min(x, y), (y + c0)), min(x, (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(x, z) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(x, z) + c0)), min(y, (min(x, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(x, (z + (w*c0))) + c1)), min(y, (min(x, (z + (w*c0))) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(x, (z + (w/c0))) + c1)), min(min(x, y), ((z + (w/c0)) + c1)), (0 <= c1)) ||
 rewrite(min(min(x, y), (min(x, ((z*c0) + w)) + c1)), min(y, (min(x, ((z*c0) + w)) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(x, ((z/c0) + w)) + c1)), min(min(x, y), (((z/c0) + w) + c1)), (0 <= c1)) ||
 rewrite(min(min(x, y), (min(y, z) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(y, z) + c0)), min(x, (min(y, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(y, (z + (w*c0))) + c1)), min(x, (min(y, (z + (w*c0))) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(y, (z + (w/c0))) + c1)), min(min(x, y), ((z + (w/c0)) + c1)), (0 <= c1)) ||
 rewrite(min(min(x, y), (min(y, ((z*c0) + w)) + c1)), min(x, (min(y, ((z*c0) + w)) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(y, ((z/c0) + w)) + c1)), min(min(x, y), (((z/c0) + w) + c1)), (0 <= c1)) ||
 rewrite(min(min(x, y), (min(z, x) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, x) + c0)), min(y, (min(z, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(z, y) + c0)), min(min(x, y), (z + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, y) + c0)), min(x, (min(z, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, y), (min(z, min(w, min(u, x))) + c0)), min(min(x, y), (min(z, min(w, u)) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, min(w, min(u, y))) + c0)), min(min(x, y), (min(z, min(w, u)) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, min(w, min(x, u))) + c0)), min(min(x, y), (min(z, min(w, u)) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, min(w, min(y, u))) + c0)), min(min(x, y), (min(z, min(w, u)) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, min(min(w, x), u)) + c0)), min(min(x, y), (min(z, min(w, u)) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, min(min(w, y), u)) + c0)), min(min(x, y), (min(z, min(w, u)) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, min(min(x, w), u)) + c0)), min(min(x, y), (min(z, min(w, u)) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(z, min(min(y, w), u)) + c0)), min(min(x, y), (min(z, min(w, u)) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min((z + (w*c0)), x) + c1)), min(y, (min((z + (w*c0)), x) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min((z + (w*c0)), y) + c1)), min(x, (min((z + (w*c0)), y) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min((z + (w/c0)), x) + c1)), min(min(x, y), ((z + (w/c0)) + c1)), (0 <= c1)) ||
 rewrite(min(min(x, y), (min((z + (w/c0)), y) + c1)), min(min(x, y), ((z + (w/c0)) + c1)), (0 <= c1)) ||
 rewrite(min(min(x, y), (min(((z*c0) + w), x) + c1)), min(y, (min(((z*c0) + w), x) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(((z*c0) + w), y) + c1)), min(x, (min(((z*c0) + w), y) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, y), (min(((z/c0) + w), x) + c1)), min(min(x, y), (((z/c0) + w) + c1)), (0 <= c1)) ||
 rewrite(min(min(x, y), (min(((z/c0) + w), y) + c1)), min(min(x, y), (((z/c0) + w) + c1)), (0 <= c1)) ||
 rewrite(min(min(x, y), (min(min(z, min(w, x)), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(min(z, min(w, y)), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(min(z, min(x, w)), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(min(z, min(y, w)), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(min(min(x, z), w), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(min(min(y, z), w), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(min(min(z, x), w), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, y), (min(min(min(z, y), w), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (0 <= c0)) ||
 rewrite(min(min(x, (y + z)), (w + (u + (v + y)))), min(x, (y + min(z, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + (u + (v + z)))), min(x, (z + min(y, (w + (u + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + (u + (y + v)))), min(x, (y + min(z, (u + (w + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + (u + (z + v)))), min(x, (z + min(y, (u + (w + v)))))) ||
 rewrite(min(min(x, (y + z)), (w + ((u + y) + v))), min(x, (y + min(z, ((u + w) + v))))) ||
 rewrite(min(min(x, (y + z)), (w + ((u + z) + v))), min(x, (z + min(y, ((u + w) + v))))) ||
 rewrite(min(min(x, (y + z)), (w + ((y + u) + v))), min(x, (y + min(z, ((w + u) + v))))) ||
 rewrite(min(min(x, (y + z)), (w + ((z + u) + v))), min(x, (z + min(y, ((w + u) + v))))) ||
 rewrite(min(min(x, (y + z)), (w + ((u + y) - v))), min(x, (y + min(z, ((u + w) - v))))) ||
 rewrite(min(min(x, (y + z)), (w + ((u + z) - v))), min(x, (z + min(y, ((u + w) - v))))) ||
 rewrite(min(min(x, (y + z)), (w + ((y + u) - v))), min(x, (y + min(z, ((w + u) - v))))) ||
 rewrite(min(min(x, (y + z)), (w + ((z + u) - v))), min(x, (z + min(y, ((w + u) - v))))) ||
 rewrite(min(min(x, (y + z)), ((w + (u + y)) + v)), min(x, (y + min((w + (u + v)), z)))) ||
 rewrite(min(min(x, (y + z)), ((w + (u + z)) + v)), min(x, (z + min((w + (u + v)), y)))) ||
 rewrite(min(min(x, (y + z)), ((w + (y + u)) + v)), min(x, (min(z, ((w + v) + u)) + y))) ||
 rewrite(min(min(x, (y + z)), ((w + (z + u)) + v)), min(x, (min(y, ((w + v) + u)) + z))) ||
 rewrite(min(min(x, (y + z)), (((w + y) + u) + v)), min(x, (y + min(((w + v) + u), z)))) ||
 rewrite(min(min(x, (y + z)), (((w + z) + u) + v)), min(x, (z + min(((w + v) + u), y)))) ||
 rewrite(min(min(x, (y + z)), (((y + w) + u) + v)), min(x, (y + min(((v + w) + u), z)))) ||
 rewrite(min(min(x, (y + z)), (((z + w) + u) + v)), min(x, (z + min(((v + w) + u), y)))) ||
 rewrite(min(min(x, (y + z)), (((w + y) - u) + v)), min(x, (y + min(((w + v) - u), z)))) ||
 rewrite(min(min(x, (y + z)), (((w + z) - u) + v)), min(x, (z + min(((w + v) - u), y)))) ||
 rewrite(min(min(x, (y + z)), (((y + w) - u) + v)), min(x, (y + min(((v + w) - u), z)))) ||
 rewrite(min(min(x, (y + z)), (((z + w) - u) + v)), min(x, (z + min(((v + w) - u), y)))) ||
 rewrite(min(min(x, (y + z)), (min(w, (z + y)) + c0)), min(min(x, (y + z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, (y + z)), (min(w, (z + y)) + c0)), min(x, (min(w, (z + y)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (y + z)), (min((z + y), w) + c0)), min(min(x, (y + z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, (y + z)), (min((z + y), w) + c0)), min(x, (min((z + y), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, (y + (z + w))), (u + w)), min(x, (min((z + y), u) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + z)), min(x, (min((y + w), u) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (v + w))), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (v + z))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (w + v))), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (z + v))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (w - v))), min(x, (min((u - v), (z + y)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (u + (z - v))), min(x, (min((u - v), (y + w)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), (w + u)), min(x, (min((z + y), u) + w))) ||
 rewrite(min(min(x, (y + (z + w))), (z + u)), min(x, (min((y + w), u) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + w) + v)), min(x, (min((z + y), (u + v)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + z) + v)), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((w + u) + v)), min(x, (min((z + y), (v + u)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((z + u) + v)), min(x, (min((y + w), (v + u)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((w - u) + v)), min(x, (min((v - u), (z + y)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((z - u) + v)), min(x, (min((v - u), (w + y)) + z))) ||
 rewrite(min(min(x, (y + (z + (w + u)))), (u + v)), min(x, (min((z + (w + y)), v) + u))) ||
 rewrite(min(min(x, (y + (z + (w + u)))), (v + u)), min(x, (min((z + (w + y)), v) + u))) ||
 rewrite(min(min(x, (y + (z + (w + u)))), (v + w)), min(x, (min((z + (y + u)), v) + w))) ||
 rewrite(min(min(x, (y + (z + (w + u)))), (w + v)), min(x, (min((z + (y + u)), v) + w))) ||
 rewrite(min(min(x, (y + ((z + w) + u))), (v + w)), min(x, (min((z + (y + u)), v) + w))) ||
 rewrite(min(min(x, (y + ((z + w) + u))), (v + z)), min(x, (min(((y + w) + u), v) + z))) ||
 rewrite(min(min(x, (y + ((z + w) + u))), (w + v)), min(x, (min((z + (y + u)), v) + w))) ||
 rewrite(min(min(x, (y + ((z + w) + u))), (z + v)), min(x, (min(((y + w) + u), v) + z))) ||
 rewrite(min(min(x, (y + (z - w))), (u + z)), min(x, (min((y - w), u) + z))) ||
 rewrite(min(min(x, (y + (z - w))), (u + (v - w))), min(x, (min((z + y), (u + v)) - w))) ||
 rewrite(min(min(x, (y + (z - w))), (z + u)), min(x, (min((y - w), u) + z))) ||
 rewrite(min(min(x, (y + (z - w))), ((u - w) + v)), min(x, (min((z + y), (u + v)) - w))) ||
 rewrite(min(min(x, (y + (z*c0))), (min(w, x) + c1)), min((y + (z*c0)), (min(w, x) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, (y + (z*c0))), (min(x, w) + c1)), min((y + (z*c0)), (min(x, w) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, ((y + z) + w)), (u + y)), min(x, (min((z + w), u) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + z)), min(x, (min((y + w), u) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (v + y))), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (v + z))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (y + v))), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (z + v))), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (y - v))), min((min((u - v), (z + w)) + y), x)) ||
 rewrite(min(min(x, ((y + z) + w)), (u + (z - v))), min((min((u - v), (y + w)) + z), x)) ||
 rewrite(min(min(x, ((y + z) + w)), (y + u)), min(x, (min((z + w), u) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), (z + u)), min((min(u, (y + w)) + z), x)) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + y) + v)), min(x, (min((z + w), (u + v)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + z) + v)), min(x, (min((y + w), (u + v)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((y + u) + v)), min(x, (min((z + w), (v + u)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z + u) + v)), min(x, (min((y + w), (v + u)) + z))) ||
 rewrite(min(min(x, ((y + z) + w)), ((y - u) + v)), min(x, (min((v - u), (w + z)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z - u) + v)), min(x, (min((v - u), (y + w)) + z))) ||
 rewrite(min(min(x, ((y + (z + w)) + u)), (v + w)), min(x, (min((z + (y + u)), v) + w))) ||
 rewrite(min(min(x, ((y + (z + w)) + u)), (v + z)), min((min(v, ((y + u) + w)) + z), x)) ||
 rewrite(min(min(x, ((y + (z + w)) + u)), (w + v)), min((min(v, (y + (z + u))) + w), x)) ||
 rewrite(min(min(x, ((y + (z + w)) + u)), (z + v)), min((min(v, ((y + u) + w)) + z), x)) ||
 rewrite(min(min(x, (((y + z) + w) + u)), (v + y)), min(x, (min(((z + w) + u), v) + y))) ||
 rewrite(min(min(x, (((y + z) + w) + u)), (v + z)), min(x, (min(((y + w) + u), v) + z))) ||
 rewrite(min(min(x, (((y + z) + w) + u)), (y + v)), min((min(v, ((u + z) + w)) + y), x)) ||
 rewrite(min(min(x, (((y + z) + w) + u)), (z + v)), min((min(v, ((y + u) + w)) + z), x)) ||
 rewrite(min(min(x, ((y - z) + w)), (u + y)), min((min(u, (w - z)) + y), x)) ||
 rewrite(min(min(x, ((y - z) + w)), (u + (v - z))), min(x, (min((y + w), (u + v)) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), (y + u)), min((min(u, (w - z)) + y), x)) ||
 rewrite(min(min(x, ((y - z) + w)), ((u - z) + v)), min(x, (min((y + w), (u + v)) - z))) ||
 rewrite(min(min(x, ((y*c0) + z)), (min(w, x) + c1)), min(((y*c0) + z), (min(w, x) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, ((y*c0) + z)), (min(x, w) + c1)), min(((y*c0) + z), (min(x, w) + c1)), (c1 <= 0)) ||
 rewrite(min(min(x, (min(y, (z + w)) + c0)), (w + z)), min(min(x, (y + c0)), (w + z)), (0 <= c0)) ||
 rewrite(min(min(x, (min((y + z), w) + c0)), (z + y)), min(min(x, (w + c0)), (z + y)), (0 <= c0)) ||
 rewrite(min(min(x, (y - z)), (w + (u + (v - z)))), min(x, (min(y, (w + (u + v))) - z))) ||
 rewrite(min(min(x, (y - z)), (w + ((u - z) + v))), min((min(y, (u + (w + v))) - z), x)) ||
 rewrite(min(min(x, (y - z)), (w + (u - z))), min(x, (min(y, (w + u)) - z))) ||
 rewrite(min(min(x, (y - z)), (w + (u - (v + z)))), min(x, (min(y, (w + (u - v))) - z))) ||
 rewrite(min(min(x, (y - z)), (w + (u - (z + v)))), min(x, (min(y, (w + (u - v))) - z))) ||
 rewrite(min(min(x, (y - z)), ((w + (u - z)) + v)), min(x, (min(y, (w + (u + v))) - z))) ||
 rewrite(min(min(x, (y - z)), (((w - z) + u) + v)), min(x, (min(y, ((w + v) + u)) - z))) ||
 rewrite(min(min(x, (y - z)), ((w - z) + u)), min((min(y, (w + u)) - z), x)) ||
 rewrite(min(min(x, (y - z)), ((w - (u + z)) + v)), min(x, (min(y, ((w - u) + v)) - z))) ||
 rewrite(min(min(x, (y - z)), ((w - (z + u)) + v)), min(x, (min(y, ((w - u) + v)) - z))) ||
 rewrite(min(min(x, (y - (z + w))), (u + (v - w))), min(x, (min((y - z), (u + v)) - w))) ||
 rewrite(min(min(x, (y - (z + w))), (u + (v - z))), min(x, (min((y - w), (u + v)) - z))) ||
 rewrite(min(min(x, (y - (z + w))), ((u - w) + v)), min(x, (min((y - z), (u + v)) - w))) ||
 rewrite(min(min(x, (y - (z + w))), ((u - z) + v)), min(x, (min((y - w), (u + v)) - z))) ||
 rewrite(min(min(x, min(y, z)), (y + c0)), min(x, min(z, y)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (y + c0)), min(min(x, z), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (z + c0)), min(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (z + c0)), min(min(x, y), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(w, y) + c0)), min(min(x, min(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (min(w, z) + c0)), min(min(x, min(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (min(w, min(u, y)) + c0)), min(min(x, z), (min(w, min(u, y)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(w, min(u, z)) + c0)), min(min(x, y), (min(w, min(u, z)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(w, min(y, u)) + c0)), min(min(x, z), (min(w, min(y, u)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(w, min(z, u)) + c0)), min(min(x, y), (min(w, min(z, u)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(y, w) + c0)), min(min(x, min(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (min(y, (w + u)) + c0)), min(min(x, z), (min(y, (w + u)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(z, w) + c0)), min(min(x, min(y, z)), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, z)), (min(z, (w + u)) + c0)), min(min(x, y), (min(z, (w + u)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min((w + u), y) + c0)), min(min(x, z), (min((w + u), y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min((w + u), z) + c0)), min(min(x, y), (min((w + u), z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(min(w, y), u) + c0)), min(min(x, z), (min(min(w, y), u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(min(w, z), u) + c0)), min(min(x, y), (min(min(w, z), u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(min(y, w), u) + c0)), min(min(x, z), (min(min(y, w), u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, z)), (min(min(z, w), u) + c0)), min(min(x, y), (min(min(z, w), u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + w)), min(min(x, (min(z, u) + w)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (u + z)), min(min(x, (min(w, u) + z)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (w + u)), min(min(x, (min(z, u) + w)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), (z + u)), min(min(x, (min(w, u) + z)), y)) ||
 rewrite(min(min(x, min(y, (z + w))), ((w + z) + c0)), min(min(x, y), ((w + z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, (z + w))), (min(u, y) + c0)), min(min(x, (z + w)), (min(u, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, (z + w))), (min(y, u) + c0)), min(min(x, (z + w)), (min(y, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, (z - w))), (u + (v - w))), min(x, min(y, (min(z, (u + v)) - w)))) ||
 rewrite(min(min(x, min(y, (z - w))), ((u - w) + v)), min(min(x, (min(z, (v + u)) - w)), y)) ||
 rewrite(min(min(x, min(y, (z*w))), ((w*z) + c0)), min(min(x, y), ((w*z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (w + c0)), min(x, min(y, min(z, w))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (w + c0)), min(min(x, min(y, z)), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (z + c0)), min(x, min(y, min(z, w))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (z + c0)), min(min(x, min(y, w)), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(u, w) + c0)), min(min(x, min(y, min(z, w))), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(u, w) + c0)), min(min(x, min(y, z)), (min(u, w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(u, z) + c0)), min(min(x, min(y, min(z, w))), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(u, z) + c0)), min(min(x, min(y, w)), (min(u, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(w, u) + c0)), min(min(x, min(y, min(z, w))), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(w, u) + c0)), min(min(x, min(y, z)), (min(w, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(z, u) + c0)), min(min(x, min(y, min(z, w))), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, w))), (min(z, u) + c0)), min(min(x, min(y, w)), (min(z, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (u + v)), min(min(x, min(y, (min(w, v) + u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + u)), min(min(x, min(y, (min(w, v) + u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (v + w)), min(min(x, min(y, (min(u, v) + w))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w + u)))), (w + v)), min(min(x, min(y, (min(u, v) + w))), z)) ||
 rewrite(min(min(x, min(y, min(z, min(w, u)))), (u + c0)), min(x, min(y, min(z, min(w, u)))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, min(w, u)))), (u + c0)), min(min(x, min(y, min(z, w))), (u + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(z, min(w, u)))), (w + c0)), min(x, min(y, min(z, min(w, u)))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(z, min(w, u)))), (w + c0)), min(min(x, min(y, min(z, u))), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + w)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (v + z)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (w + v)), min(min(x, min((min(v, z) + w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z + w), u))), (z + v)), min(min(x, min(y, (min(v, w) + z))), u)) ||
 rewrite(min(min(x, min(y, min(min(z, w), u))), (w + c0)), min(x, min(y, min(min(z, w), u))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(min(z, w), u))), (w + c0)), min(min(x, min(y, min(z, u))), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(y, min(min(z, w), u))), (z + c0)), min(x, min(y, min(min(z, w), u))), (0 <= c0)) ||
 rewrite(min(min(x, min(y, min(min(z, w), u))), (z + c0)), min(min(x, min(y, min(w, u))), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min((y + z), w)), (u + y)), min(min(x, w), (min(z, u) + y))) ||
 rewrite(min(min(x, min((y + z), w)), (u + z)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (y + u)), min(min(x, (min(u, z) + y)), w)) ||
 rewrite(min(min(x, min((y + z), w)), (z + u)), min(min(x, (min(y, u) + z)), w)) ||
 rewrite(min(min(x, min((y + z), w)), ((z + y) + c0)), min(min(x, w), ((z + y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min((y + z), w)), (min(u, w) + c0)), min(min(x, (y + z)), (min(u, w) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min((y + z), w)), (min(w, u) + c0)), min(min(x, (y + z)), (min(w, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min((y - z), w)), (u + (v - z))), min(x, min(w, (min(y, (u + v)) - z)))) ||
 rewrite(min(min(x, min((y - z), w)), ((u - z) + v)), min(min(x, (min((v + u), y) - z)), w)) ||
 rewrite(min(min(x, min((y*z), w)), ((z*y) + c0)), min(min(x, w), ((z*y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (y + c0)), min(x, min(min(y, z), w)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (y + c0)), min(min(x, min(z, w)), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (z + c0)), min(x, min(min(y, z), w)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (z + c0)), min(min(x, min(y, w)), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(u, y) + c0)), min(min(x, min(min(y, z), w)), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(u, y) + c0)), min(min(x, min(z, w)), (min(u, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(u, z) + c0)), min(min(x, min(min(y, z), w)), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(u, z) + c0)), min(min(x, min(y, w)), (min(u, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(y, u) + c0)), min(min(x, min(min(y, z), w)), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(y, u) + c0)), min(min(x, min(z, w)), (min(y, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(z, u) + c0)), min(min(x, min(min(y, z), w)), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, z), w)), (min(z, u) + c0)), min(min(x, min(y, w)), (min(z, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + w)), min(min(x, min((min(z, v) + w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (v + z)), min(min(x, min((min(w, v) + z), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (w + v)), min(min(x, min((min(z, v) + w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z + w)), u)), (z + v)), min(min(x, min((min(w, v) + z), u)), y)) ||
 rewrite(min(min(x, min(min(y, min(z, w)), u)), (w + c0)), min(x, min(min(y, min(z, w)), u)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, min(z, w)), u)), (w + c0)), min(min(x, min(min(y, z), u)), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(y, min(z, w)), u)), (z + c0)), min(x, min(min(y, min(w, u)), z)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(y, min(z, w)), u)), (z + c0)), min(min(x, min(min(y, w), u)), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + y)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (v + z)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (y + v)), min(min(x, min((min(v, z) + y), u)), w)) ||
 rewrite(min(min(x, min(min((y + z), w), u)), (z + v)), min(min(x, min((min(y, v) + z), u)), w)) ||
 rewrite(min(min(x, min(min(min(y, z), w), u)), (y + c0)), min(x, min(min(min(z, w), u), y)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(min(y, z), w), u)), (y + c0)), min(min(x, min(min(z, w), u)), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(x, min(min(min(y, z), w), u)), (z + c0)), min(x, min(min(min(y, z), w), u)), (0 <= c0)) ||
 rewrite(min(min(x, min(min(min(y, z), w), u)), (z + c0)), min(min(x, min(min(y, w), u)), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min((x + y), z), (w + (u + (v + x)))), min(z, (x + min(y, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + (u + (v + y)))), min(z, (y + min(x, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + (u + (x + v)))), min(z, (x + min(y, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + (u + (y + v)))), min(z, (y + min(x, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + ((u + x) + v))), min(z, (x + min(y, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + ((u + y) + v))), min(z, (y + min(x, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), (w + ((x + u) + v))), min(z, (x + min(y, (w + (v + u)))))) ||
 rewrite(min(min((x + y), z), (w + ((y + u) + v))), min(z, (y + min(x, (w + (v + u)))))) ||
 rewrite(min(min((x + y), z), (w + ((u + x) - v))), min(z, (x + min(y, (w + (u - v)))))) ||
 rewrite(min(min((x + y), z), (w + ((u + y) - v))), min(z, (y + min(x, (w + (u - v)))))) ||
 rewrite(min(min((x + y), z), (w + ((x + u) - v))), min(z, (x + min(y, ((w + u) - v))))) ||
 rewrite(min(min((x + y), z), (w + ((y + u) - v))), min(z, (y + min(x, ((w + u) - v))))) ||
 rewrite(min(min((x + y), z), ((w + (u + x)) + v)), min(z, (x + min(y, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), ((w + (u + y)) + v)), min(z, (y + min(x, (w + (u + v)))))) ||
 rewrite(min(min((x + y), z), ((w + (x + u)) + v)), min(z, (x + min(y, (w + (v + u)))))) ||
 rewrite(min(min((x + y), z), ((w + (y + u)) + v)), min(z, (y + min(x, (w + (v + u)))))) ||
 rewrite(min(min((x + y), z), (((w + x) + u) + v)), min(z, (x + min(y, ((w + v) + u))))) ||
 rewrite(min(min((x + y), z), (((w + y) + u) + v)), min(z, (y + min(x, ((w + v) + u))))) ||
 rewrite(min(min((x + y), z), (((x + w) + u) + v)), min(z, (x + min(y, ((v + w) + u))))) ||
 rewrite(min(min((x + y), z), (((y + w) + u) + v)), min(z, (y + min(x, ((v + w) + u))))) ||
 rewrite(min(min((x + y), z), (((w + x) - u) + v)), min(z, (x + min(y, ((w + v) - u))))) ||
 rewrite(min(min((x + y), z), (((w + y) - u) + v)), min(z, (y + min(x, ((w + v) - u))))) ||
 rewrite(min(min((x + y), z), (((x + w) - u) + v)), min(z, (x + min(y, ((v + w) - u))))) ||
 rewrite(min(min((x + y), z), (((y + w) - u) + v)), min(z, (y + min(x, ((v + w) - u))))) ||
 rewrite(min(min((x + y), z), (min(w, (y + x)) + c0)), min(min((x + y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min((x + y), z), (min(w, (y + x)) + c0)), min(z, (min(w, (y + x)) + c0)), (c0 <= 0)) ||
 rewrite(min(min((x + y), z), (min((y + x), w) + c0)), min(min((x + y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min((x + y), z), (min((y + x), w) + c0)), min(z, (min((y + x), w) + c0)), (c0 <= 0)) ||
 rewrite(min(min((x + (y + z)), w), (u + y)), min(w, (min((x + z), u) + y))) ||
 rewrite(min(min((x + (y + z)), w), (u + z)), min(w, (min((x + y), u) + z))) ||
 rewrite(min(min((x + (y + z)), w), (u + (v + y))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (v + z))), min((min((x + y), (u + v)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (y + v))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (z + v))), min((min((x + y), (u + v)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), (u + (y - v))), min(w, (y + min((u - v), (x + z))))) ||
 rewrite(min(min((x + (y + z)), w), (u + (z - v))), min(w, (z + min((u - v), (x + y))))) ||
 rewrite(min(min((x + (y + z)), w), (y + u)), min((min(u, (x + z)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), (z + u)), min((min(u, (y + x)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), ((u + y) + v)), min(w, (min((x + z), (u + v)) + y))) ||
 rewrite(min(min((x + (y + z)), w), ((u + z) + v)), min(w, (min((y + x), (u + v)) + z))) ||
 rewrite(min(min((x + (y + z)), w), ((y + u) + v)), min(w, (min((x + z), (v + u)) + y))) ||
 rewrite(min(min((x + (y + z)), w), ((z + u) + v)), min(w, (min((y + x), (v + u)) + z))) ||
 rewrite(min(min((x + (y + z)), w), ((y - u) + v)), min(w, (min((v - u), (z + x)) + y))) ||
 rewrite(min(min((x + (y + z)), w), ((z - u) + v)), min(w, (min((v - u), (x + y)) + z))) ||
 rewrite(min(min((x + (y + (z + w))), u), (v + w)), min(u, (min((x + (y + z)), v) + w))) ||
 rewrite(min(min((x + (y + (z + w))), u), (v + z)), min(u, (min((x + (y + w)), v) + z))) ||
 rewrite(min(min((x + (y + (z + w))), u), (w + v)), min((min(v, (y + (z + x))) + w), u)) ||
 rewrite(min(min((x + (y + (z + w))), u), (z + v)), min((min(v, (y + (x + w))) + z), u)) ||
 rewrite(min(min((x + ((y + z) + w)), u), (v + y)), min(u, (min((x + (z + w)), v) + y))) ||
 rewrite(min(min((x + ((y + z) + w)), u), (v + z)), min(u, (min((x + (y + w)), v) + z))) ||
 rewrite(min(min((x + ((y + z) + w)), u), (y + v)), min((min(v, ((x + z) + w)) + y), u)) ||
 rewrite(min(min((x + ((y + z) + w)), u), (z + v)), min((min(v, ((y + x) + w)) + z), u)) ||
 rewrite(min(min((x + (y - z)), w), (u + y)), min(w, (min((x - z), u) + y))) ||
 rewrite(min(min((x + (y - z)), w), (u + (v - z))), min((min((x + y), (u + v)) - z), w)) ||
 rewrite(min(min((x + (y - z)), w), (y + u)), min((min(u, (x - z)) + y), w)) ||
 rewrite(min(min((x + (y - z)), w), ((u - z) + v)), min((min((x + y), (v + u)) - z), w)) ||
 rewrite(min(min((x + (y*c0)), z), (min(w, z) + c1)), min((x + (y*c0)), (min(w, z) + c1)), (c1 <= 0)) ||
 rewrite(min(min((x + (y*c0)), z), (min(z, w) + c1)), min((x + (y*c0)), (min(z, w) + c1)), (c1 <= 0)) ||
 rewrite(min(min(((x + y) + z), w), (u + x)), min(w, (min((y + z), u) + x))) ||
 rewrite(min(min(((x + y) + z), w), (u + y)), min(w, (min((x + z), u) + y))) ||
 rewrite(min(min(((x + y) + z), w), (u + (v + x))), min((min((y + z), (u + v)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (v + y))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (x + v))), min((min((y + z), (u + v)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (y + v))), min((min((x + z), (u + v)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), (u + (x - v))), min(w, (x + min((u - v), (z + y))))) ||
 rewrite(min(min(((x + y) + z), w), (u + (y - v))), min((min((u - v), (z + x)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), (x + u)), min((min((z + y), u) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), (y + u)), min((min((x + z), u) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), ((u + x) + v)), min(w, (min((y + z), (u + v)) + x))) ||
 rewrite(min(min(((x + y) + z), w), ((u + y) + v)), min(w, (min((x + z), (u + v)) + y))) ||
 rewrite(min(min(((x + y) + z), w), ((x + u) + v)), min(w, (min((y + z), (v + u)) + x))) ||
 rewrite(min(min(((x + y) + z), w), ((y + u) + v)), min(w, (min((x + z), (v + u)) + y))) ||
 rewrite(min(min(((x + y) + z), w), ((x - u) + v)), min(w, (x + min((v - u), (z + y))))) ||
 rewrite(min(min(((x + y) + z), w), ((y - u) + v)), min((min((v - u), (x + z)) + y), w)) ||
 rewrite(min(min(((x + (y + z)) + w), u), (v + y)), min(u, (min(((x + z) + w), v) + y))) ||
 rewrite(min(min(((x + (y + z)) + w), u), (v + z)), min(u, (min(((x + y) + w), v) + z))) ||
 rewrite(min(min(((x + (y + z)) + w), u), (y + v)), min((min((x + (w + z)), v) + y), u)) ||
 rewrite(min(min(((x + (y + z)) + w), u), (z + v)), min((min((x + (y + w)), v) + z), u)) ||
 rewrite(min(min((((x + y) + z) + w), u), (v + x)), min(u, (min(((y + z) + w), v) + x))) ||
 rewrite(min(min((((x + y) + z) + w), u), (v + y)), min(u, (min(((x + z) + w), v) + y))) ||
 rewrite(min(min((((x + y) + z) + w), u), (x + v)), min((min(((w + y) + z), v) + x), u)) ||
 rewrite(min(min((((x + y) + z) + w), u), (y + v)), min((min(((x + w) + z), v) + y), u)) ||
 rewrite(min(min(((x - y) + z), w), (u + x)), min(w, (min((z - y), u) + x))) ||
 rewrite(min(min(((x - y) + z), w), (u + (v - y))), min((min((x + z), (u + v)) - y), w)) ||
 rewrite(min(min(((x - y) + z), w), (x + u)), min((min((z - y), u) + x), w)) ||
 rewrite(min(min(((x - y) + z), w), ((u - y) + v)), min((min((x + z), (v + u)) - y), w)) ||
 rewrite(min(min(((x*c0) + y), z), (min(w, z) + c1)), min(((x*c0) + y), (min(w, z) + c1)), (c1 <= 0)) ||
 rewrite(min(min(((x*c0) + y), z), (min(z, w) + c1)), min(((x*c0) + y), (min(z, w) + c1)), (c1 <= 0)) ||
 rewrite(min(min((min(x, (y + z)) + c0), w), (z + y)), min(min((x + c0), w), (z + y)), (0 <= c0)) ||
 rewrite(min(min((min((x + y), z) + c0), w), (y + x)), min(min((z + c0), w), (y + x)), (0 <= c0)) ||
 rewrite(min(min((x - y), z), (w + (u + (v - y)))), min((min(x, (w + (u + v))) - y), z)) ||
 rewrite(min(min((x - y), z), (w + ((u - y) + v))), min((min(x, (u + (w + v))) - y), z)) ||
 rewrite(min(min((x - y), z), (w + (u - y))), min((min(x, (w + u)) - y), z)) ||
 rewrite(min(min((x - y), z), (w + (u - (v + y)))), min((min(x, (w + (u - v))) - y), z)) ||
 rewrite(min(min((x - y), z), (w + (u - (y + v)))), min((min(x, (w + (u - v))) - y), z)) ||
 rewrite(min(min((x - y), z), ((w + (u - y)) + v)), min(z, (min(x, (w + (u + v))) - y))) ||
 rewrite(min(min((x - y), z), (((w - y) + u) + v)), min(z, (min(x, ((w + v) + u)) - y))) ||
 rewrite(min(min((x - y), z), ((w - y) + u)), min((min((u + w), x) - y), z)) ||
 rewrite(min(min((x - y), z), ((w - (u + y)) + v)), min(z, (min(x, ((w - u) + v)) - y))) ||
 rewrite(min(min((x - y), z), ((w - (y + u)) + v)), min(z, (min(x, ((w - u) + v)) - y))) ||
 rewrite(min(min((x - (y + z)), w), (u + (v - y))), min((min((x - z), (u + v)) - y), w)) ||
 rewrite(min(min((x - (y + z)), w), (u + (v - z))), min((min((x - y), (u + v)) - z), w)) ||
 rewrite(min(min((x - (y + z)), w), ((u - y) + v)), min((min((x - z), (v + u)) - y), w)) ||
 rewrite(min(min((x - (y + z)), w), ((u - z) + v)), min((min((x - y), (v + u)) - z), w)) ||
 rewrite(min(min(min(x, y), z), (x + c0)), min(min(y, z), x), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (x + c0)), min(min(y, z), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (y + c0)), min(min(x, z), y), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (y + c0)), min(min(x, z), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(w, x) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(w, y) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(w, min(u, x)) + c0)), min(min(y, z), (min(w, min(u, x)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(w, min(u, y)) + c0)), min(min(x, z), (min(w, min(u, y)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(w, min(x, u)) + c0)), min(min(y, z), (min(w, min(x, u)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(w, min(y, u)) + c0)), min(min(x, z), (min(w, min(y, u)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(x, w) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(x, (w + u)) + c0)), min(min(y, z), (min(x, (w + u)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(y, w) + c0)), min(min(min(x, y), z), (w + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, y), z), (min(y, (w + u)) + c0)), min(min(x, z), (min(y, (w + u)) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min((w + u), x) + c0)), min(min(y, z), (min((w + u), x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min((w + u), y) + c0)), min(min(x, z), (min((w + u), y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(min(w, x), u) + c0)), min(min(y, z), (min(min(w, x), u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(min(w, y), u) + c0)), min(min(x, z), (min(min(w, y), u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(min(x, w), u) + c0)), min(min(y, z), (min(min(x, w), u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), z), (min(min(y, w), u) + c0)), min(min(x, z), (min(min(y, w), u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), (z + w)), (min(u, x) + c0)), min(min(y, (z + w)), (min(u, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), (z + w)), (min(u, y) + c0)), min(min(x, (z + w)), (min(u, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), (z + w)), (min(x, u) + c0)), min(min(y, (z + w)), (min(x, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, y), (z + w)), (min(y, u) + c0)), min(min(x, (z + w)), (min(y, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + y)), min(min((min(z, u) + y), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (u + z)), min(min((min(y, u) + z), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (y + u)), min(min((min(z, u) + y), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), (z + u)), min(min((min(y, u) + z), w), x)) ||
 rewrite(min(min(min(x, (y + z)), w), ((z + y) + c0)), min(min(x, w), ((z + y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, (y + z)), w), (min(u, x) + c0)), min(min((y + z), w), (min(u, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, (y + z)), w), (min(x, u) + c0)), min(min((y + z), w), (min(x, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, (y - z)), w), (u + (v - z))), min(w, min(x, (min(y, (u + v)) - z)))) ||
 rewrite(min(min(min(x, (y - z)), w), ((u - z) + v)), min(min((min(y, (v + u)) - z), w), x)) ||
 rewrite(min(min(min(x, (y*z)), w), ((z*y) + c0)), min(min(x, w), ((z*y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (y + c0)), min(min(min(x, z), w), y), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (y + c0)), min(min(min(x, z), w), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (z + c0)), min(min(min(x, y), w), z), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (z + c0)), min(min(min(x, y), w), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(u, y) + c0)), min(min(min(x, min(y, z)), w), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(u, y) + c0)), min(min(min(x, z), w), (min(u, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(u, z) + c0)), min(min(min(x, min(y, z)), w), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(u, z) + c0)), min(min(min(x, y), w), (min(u, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(y, u) + c0)), min(min(min(x, min(y, z)), w), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(y, u) + c0)), min(min(min(x, z), w), (min(y, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(z, u) + c0)), min(min(min(x, min(y, z)), w), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, z)), w), (min(z, u) + c0)), min(min(min(x, y), w), (min(z, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + w)), min(min(min(x, (min(z, v) + w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (v + z)), min(min(min(x, (min(w, v) + z)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (w + v)), min(min(min(x, (min(z, v) + w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z + w))), u), (z + v)), min(min(min(x, (min(w, v) + z)), u), y)) ||
 rewrite(min(min(min(x, min(y, min(z, w))), u), (w + c0)), min(min(min(x, min(y, z)), u), w), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, min(z, w))), u), (w + c0)), min(min(min(x, min(y, z)), u), (w + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(y, min(z, w))), u), (z + c0)), min(min(min(x, min(y, w)), u), z), (0 <= c0)) ||
 rewrite(min(min(min(x, min(y, min(z, w))), u), (z + c0)), min(min(min(x, min(y, w)), u), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + y)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (v + z)), min(min(min((min(v, y) + z), w), u), x)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (y + v)), min(min(min(x, (min(v, z) + y)), u), w)) ||
 rewrite(min(min(min(x, min((y + z), w)), u), (z + v)), min(min(min((min(v, y) + z), w), u), x)) ||
 rewrite(min(min(min(x, min(min(y, z), w)), u), (y + c0)), min(min(min(x, min(z, w)), u), y), (0 <= c0)) ||
 rewrite(min(min(min(x, min(min(y, z), w)), u), (y + c0)), min(min(min(x, min(z, w)), u), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(x, min(min(y, z), w)), u), (z + c0)), min(min(min(x, min(y, w)), u), z), (0 <= c0)) ||
 rewrite(min(min(min(x, min(min(y, z), w)), u), (z + c0)), min(min(min(x, min(y, w)), u), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(min((x + y), z), w), (u + x)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (u + y)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (x + u)), min(min((min(u, y) + x), w), z)) ||
 rewrite(min(min(min((x + y), z), w), (y + u)), min(min((min(x, u) + y), w), z)) ||
 rewrite(min(min(min((x + y), z), w), ((y + x) + c0)), min(min(z, w), ((y + x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min((x + y), z), w), (min(u, z) + c0)), min(min((x + y), w), (min(u, z) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min((x + y), z), w), (min(z, u) + c0)), min(min((x + y), w), (min(z, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min((x - y), z), w), (u + (v - y))), min(min(z, w), (min(x, (u + v)) - y))) ||
 rewrite(min(min(min((x - y), z), w), ((u - y) + v)), min(min((min((v + u), x) - y), w), z)) ||
 rewrite(min(min(min((x*y), z), w), ((y*x) + c0)), min(min(z, w), ((y*x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (x + c0)), min(min(min(y, z), w), x), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (x + c0)), min(min(min(y, z), w), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (y + c0)), min(min(min(x, z), w), y), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (y + c0)), min(min(min(x, z), w), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(u, x) + c0)), min(min(min(min(x, y), z), w), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(u, x) + c0)), min(min(min(y, z), w), (min(u, x) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(u, y) + c0)), min(min(min(min(x, y), z), w), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(u, y) + c0)), min(min(min(x, z), w), (min(u, y) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(x, u) + c0)), min(min(min(min(x, y), z), w), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(x, u) + c0)), min(min(min(y, z), w), (min(x, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(y, u) + c0)), min(min(min(min(x, y), z), w), (u + c0)), (0 <= c0)) ||
 rewrite(min(min(min(min(x, y), z), w), (min(y, u) + c0)), min(min(min(x, z), w), (min(y, u) + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + y)), min(min(min((min(z, v) + y), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (v + z)), min(min(min((min(y, v) + z), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (y + v)), min(min(min((min(z, v) + y), w), u), x)) ||
 rewrite(min(min(min(min(x, (y + z)), w), u), (z + v)), min(min(min((min(y, v) + z), w), u), x)) ||
 rewrite(min(min(min(min(x, min(y, z)), w), u), (y + c0)), min(min(min(min(x, z), w), u), y), (0 <= c0)) ||
 rewrite(min(min(min(min(x, min(y, z)), w), u), (y + c0)), min(min(min(min(x, z), w), u), (y + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(x, min(y, z)), w), u), (z + c0)), min(min(min(min(x, y), w), u), z), (0 <= c0)) ||
 rewrite(min(min(min(min(x, min(y, z)), w), u), (z + c0)), min(min(min(min(x, y), w), u), (z + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + x)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (v + y)), min(min(min((min(x, v) + y), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (x + v)), min(min(min((min(v, y) + x), w), u), z)) ||
 rewrite(min(min(min(min((x + y), z), w), u), (y + v)), min(min(min((min(x, v) + y), w), u), z)) ||
 rewrite(min(min(min(min(min(x, y), z), w), u), (x + c0)), min(min(min(min(y, z), w), u), x), (0 <= c0)) ||
 rewrite(min(min(min(min(min(x, y), z), w), u), (x + c0)), min(min(min(min(y, z), w), u), (x + c0)), (c0 <= 0)) ||
 rewrite(min(min(min(min(min(x, y), z), w), u), (y + c0)), min(min(min(min(x, z), w), u), y), (0 <= c0)) ||
 rewrite(min(min(min(min(min(x, y), z), w), u), (y + c0)), min(min(min(min(x, z), w), u), (y + c0)), (c0 <= 0)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(min(min(x, (y + z)), (((w + y) - u) - v)), min(x, (y + min(((w - v) - u), z)))) ||
 rewrite(min(min(x, (y + z)), (((w + z) - u) - v)), min(x, (z + min(((w - v) - u), y)))) ||
 rewrite(min(min(x, (y + z)), (((y + w) - u) - v)), min(x, (y + min(((w - v) - u), z)))) ||
 rewrite(min(min(x, (y + z)), (((z + w) - u) - v)), min(x, (z + min(((w - v) - u), y)))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + w) - v)), min(x, (min((u - v), (z + y)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((u + z) - v)), min(x, (min((u - v), (w + y)) + z))) ||
 rewrite(min(min(x, (y + (z + w))), ((w + u) - v)), min(x, (min((u - v), (y + z)) + w))) ||
 rewrite(min(min(x, (y + (z + w))), ((z + u) - v)), min(x, (min((u - v), (y + w)) + z))) ||
 rewrite(min(min(x, (y + (z - w))), (u - w)), min(x, (min((z + y), u) - w))) ||
 rewrite(min(min(x, (y + (z - w))), (y - (w + z))), min(x, (min((y - z), (y + z)) - w))) ||
 rewrite(min(min(x, (y + (z - w))), (y - (z + w))), min(x, (min((y - z), (y + z)) - w))) ||
 rewrite(min(min(x, (y + (z - w))), ((z - w) - u)), min(x, (min((z - u), (z + y)) - w))) ||
 rewrite(min(min(x, (y + (z - (w + u)))), (z - v)), min(x, (z - max(((w + u) - y), v)))) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + y) - v)), min((min((u - v), (z + w)) + y), x)) ||
 rewrite(min(min(x, ((y + z) + w)), ((u + z) - v)), min((min((u - v), (y + w)) + z), x)) ||
 rewrite(min(min(x, ((y + z) + w)), ((y + u) - v)), min(x, (min((u - v), (w + z)) + y))) ||
 rewrite(min(min(x, ((y + z) + w)), ((z + u) - v)), min(x, (min((u - v), (y + w)) + z))) ||
 rewrite(min(min(x, ((y - z) + w)), (u - z)), min(x, (min((y + w), u) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), (w - (y + z))), min(x, (min((w - y), (w + y)) - z))) ||
 rewrite(min(min(x, ((y - z) + w)), (w - (z + y))), min((min((w - y), (y + w)) - z), x)) ||
 rewrite(min(min(x, ((y - z) + w)), ((y - z) - u)), min(x, (min((y - u), (y + w)) - z))) ||
 rewrite(min(min(x, ((y - (z + w)) + u)), (y - v)), min(x, (y - max((z + (w - u)), v)))) ||
 rewrite(min(min(x, (y - z)), (w - z)), min((min(y, w) - z), x)) ||
 rewrite(min(min(x, (y - z)), (y - w)), min(x, (y - max(z, w)))) ||
 rewrite(min(min(x, (y - z)), (((w + y) - u) - v)), min(x, (y - max(((v + u) - w), z)))) ||
 rewrite(min(min(x, (y - z)), (((y + w) - u) - v)), min(x, (y - max(((v - w) + u), z)))) ||
 rewrite(min(min(x, (y - (z + w))), (u - w)), min(x, (min(u, (y - z)) - w))) ||
 rewrite(min(min(x, (y - (z + w))), (u - z)), min(x, (min((y - w), u) - z))) ||
 rewrite(min(min(x, ((y + z) - w)), ((z + y) - u)), min(x, ((y + z) - max(w, u)))) ||
 rewrite(min(min(x, min(y, (z + (w - u)))), (v - u)), min(min(x, (min(v, (z + w)) - u)), y)) ||
 rewrite(min(min(x, min(y, ((z - w) + u))), (v - w)), min(x, min(y, (min((z + u), v) - w)))) ||
 rewrite(min(min(x, min(y, (z - w))), (u - w)), min(min(x, (min(z, u) - w)), y)) ||
 rewrite(min(min(x, min(y, (z - w))), (z - u)), min(x, min(y, (z - max(w, u))))) ||
 rewrite(min(min(x, min(y, min(z, (w - u)))), (v - u)), min(min(x, min(y, (min(w, v) - u))), z)) ||
 rewrite(min(min(x, min(y, min(z, (w - u)))), (w - v)), min(x, min(y, min(z, (w - max(u, v)))))) ||
 rewrite(min(min(x, min(y, min((z - w), u))), (v - w)), min(min(x, min((min(v, z) - w), u)), y)) ||
 rewrite(min(min(x, min(y, min((z - w), u))), (z - v)), min(x, min(y, min((z - max(w, v)), u)))) ||
 rewrite(min(min(x, min((y + (z - w)), u)), (v - w)), min(min(x, (min((y + z), v) - w)), u)) ||
 rewrite(min(min(x, min(((y - z) + w), u)), (v - z)), min(min(x, (min((y + w), v) - z)), u)) ||
 rewrite(min(min(x, min((y - z), w)), (u - z)), min(min(x, (min(y, u) - z)), w)) ||
 rewrite(min(min(x, min((y - z), w)), (y - u)), min(x, min((y - max(z, u)), w))) ||
 rewrite(min(min(x, min(min(y, (z - w)), u)), (v - w)), min(min(x, min((min(z, v) - w), u)), y)) ||
 rewrite(min(min(x, min(min(y, (z - w)), u)), (z - v)), min(x, min(min(y, (z - max(w, v))), u))) ||
 rewrite(min(min(x, min(min((y - z), w), u)), (v - z)), min(min(x, min((min(y, v) - z), u)), w)) ||
 rewrite(min(min(x, min(min((y - z), w), u)), (y - v)), min(x, min(min((y - max(z, v)), u), w))) ||
 rewrite(min(min((x + y), z), (((w + x) - u) - v)), min(z, (x + min(y, ((w - v) - u))))) ||
 rewrite(min(min((x + y), z), (((w + y) - u) - v)), min(z, (y + min(x, ((w - v) - u))))) ||
 rewrite(min(min((x + y), z), (((x + w) - u) - v)), min(z, (x + min(y, ((w - u) - v))))) ||
 rewrite(min(min((x + y), z), (((y + w) - u) - v)), min(z, (y + min(x, ((w - u) - v))))) ||
 rewrite(min(min((x + (y + z)), w), ((u + y) - v)), min((min((u - v), (x + z)) + y), w)) ||
 rewrite(min(min((x + (y + z)), w), ((u + z) - v)), min((min((u - v), (x + y)) + z), w)) ||
 rewrite(min(min((x + (y + z)), w), ((y + u) - v)), min(w, (y + min((u - v), (x + z))))) ||
 rewrite(min(min((x + (y + z)), w), ((z + u) - v)), min(w, (z + min((u - v), (x + y))))) ||
 rewrite(min(min((x + (y - z)), w), (u - z)), min(w, (min((x + y), u) - z))) ||
 rewrite(min(min((x + (y - z)), w), (x - (y + z))), min((min((x - y), (x + y)) - z), w)) ||
 rewrite(min(min((x + (y - z)), w), (x - (z + y))), min((min((x - y), (y + x)) - z), w)) ||
 rewrite(min(min((x + (y - z)), w), ((y - z) - u)), min(w, (min((y - u), (y + x)) - z))) ||
 rewrite(min(min((x + (y - (z + w))), u), (y - v)), min(u, (y - max(((z + w) - x), v)))) ||
 rewrite(min(min(((x + y) + z), w), ((u + x) - v)), min((min((u - v), (y + z)) + x), w)) ||
 rewrite(min(min(((x + y) + z), w), ((u + y) - v)), min((min((u - v), (x + z)) + y), w)) ||
 rewrite(min(min(((x + y) + z), w), ((x + u) - v)), min(w, (x + min((u - v), (z + y))))) ||
 rewrite(min(min(((x + y) + z), w), ((y + u) - v)), min((min((u - v), (z + x)) + y), w)) ||
 rewrite(min(min(((x - y) + z), w), (u - y)), min(w, (min((x + z), u) - y))) ||
 rewrite(min(min(((x - y) + z), w), (z - (x + y))), min((min((z - x), (z + x)) - y), w)) ||
 rewrite(min(min(((x - y) + z), w), (z - (y + x))), min((min((z - x), (z + x)) - y), w)) ||
 rewrite(min(min(((x - y) + z), w), ((x - y) - u)), min((min((x - u), (x + z)) - y), w)) ||
 rewrite(min(min(((x - (y + z)) + w), u), (x - v)), min(u, (x - max(((y + z) - w), v)))) ||
 rewrite(min(min((x - y), z), (w - y)), min(z, (min(x, w) - y))) ||
 rewrite(min(min((x - y), z), (x - w)), min(z, (x - max(y, w)))) ||
 rewrite(min(min((x - y), z), (((w + x) - u) - v)), min(z, (x - max(y, ((v + u) - w))))) ||
 rewrite(min(min((x - y), z), (((x + w) - u) - v)), min(z, (x - max(y, ((v - w) + u))))) ||
 rewrite(min(min((x - (y + z)), w), (u - y)), min(w, (min((x - z), u) - y))) ||
 rewrite(min(min((x - (y + z)), w), (u - z)), min(w, (min((x - y), u) - z))) ||
 rewrite(min(min(((x + y) - z), w), ((y + x) - u)), min(w, ((x + y) - max(z, u)))) ||
 rewrite(min(min(min(x, (y + (z - w))), u), (v - w)), min(min((min(v, (y + z)) - w), u), x)) ||
 rewrite(min(min(min(x, ((y - z) + w)), u), (v - z)), min(min((min(v, (y + w)) - z), u), x)) ||
 rewrite(min(min(min(x, (y - z)), w), (u - z)), min(min((min(y, u) - z), w), x)) ||
 rewrite(min(min(min(x, (y - z)), w), (y - u)), min(w, min(x, (y - max(z, u))))) ||
 rewrite(min(min(min(x, min(y, (z - w))), u), (v - w)), min(min(min(x, (min(z, v) - w)), u), y)) ||
 rewrite(min(min(min(x, min(y, (z - w))), u), (z - v)), min(u, min(x, min(y, (z - max(w, v)))))) ||
 rewrite(min(min(min(x, min((y - z), w)), u), (v - z)), min(min(min((min(v, y) - z), w), u), x)) ||
 rewrite(min(min(min(x, min((y - z), w)), u), (y - v)), min(u, min(x, min((y - max(z, v)), w)))) ||
 rewrite(min(min(min((x + (y - z)), w), u), (v - z)), min(min((min((x + y), v) - z), u), w)) ||
 rewrite(min(min(min(((x - y) + z), w), u), (v - y)), min(min((min((x + z), v) - y), u), w)) ||
 rewrite(min(min(min((x - y), z), w), (u - y)), min(min((min(x, u) - y), w), z)) ||
 rewrite(min(min(min((x - y), z), w), (x - u)), min(w, min((x - max(y, u)), z))) ||
 rewrite(min(min(min(min(x, (y - z)), w), u), (v - z)), min(min(min((min(y, v) - z), w), u), x)) ||
 rewrite(min(min(min(min(x, (y - z)), w), u), (y - v)), min(min(min((y - max(v, z)), w), u), x)) ||
 rewrite(min(min(min(min((x - y), z), w), u), (v - y)), min(min(min((min(x, v) - y), w), u), z)) ||
 rewrite(min(min(min(min((x - y), z), w), u), (x - v)), min(z, min(min((x - max(y, v)), u), w))))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(min(min(x, ((y*z) + c0)), (z*y)), min(x, (y*z)), (0 <= c0)) ||
 rewrite(min(min(((x*y) + c0), z), (y*x)), min(z, (y*x)), (0 <= c0)))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite(min(min(min(x, y), (z/c0)), ((z + c1)/c0)), min(min(x, y), ((z + c1)/c0)), prove_me(((c1/c0) <= 0))) ||
 rewrite(min(min(min(x, y), ((z + c0)/c1)), (z/c1)), min(min(x, y), ((z + c0)/c1)), prove_me(((c0/c1) <= 0))))))) ||
((a.node_type() == IRNodeType::Max) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Add) && (
 rewrite(min(max(x, y), (max(x, max(y, z)) + c0)), max(x, y), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(x, max(z, y)) + c0)), max(x, y), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(y, max(x, z)) + c0)), max(y, x), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(y, max(z, x)) + c0)), max(y, x), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(max(x, z), y) + c0)), max(y, x), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(max(y, z), x) + c0)), max(x, y), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(max(z, x), y) + c0)), max(y, x), (0 <= c0)) ||
 rewrite(min(max(x, y), (max(max(z, y), x) + c0)), max(x, y), (0 <= c0)) ||
 rewrite(min(max(x, min(y, z)), (max(x, y) + c0)), max(x, min(z, y)), (0 <= c0)) ||
 rewrite(min(max(x, min(y, z)), (max(x, z) + c0)), max(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(max(x, min(y, z)), (max(y, x) + c0)), max(x, min(z, y)), (0 <= c0)) ||
 rewrite(min(max(x, min(y, z)), (max(z, x) + c0)), max(x, min(y, z)), (0 <= c0)) ||
 rewrite(min(max(min(x, y), c0), (max(x, c0) + c1)), max(min(x, y), c0), ((0 <= c1) && (max(c0, 0) <= (max(c0, 1) + c1)))) ||
 rewrite(min(max(min(x, y), c0), (max(y, c0) + c1)), max(min(x, y), c0), ((0 <= c1) && (max(c0, 0) <= (max(c0, 1) + c1)))) ||
 rewrite(min(max(min(x, y), z), (max(x, z) + c0)), max(min(x, y), z), (0 <= c0)) ||
 rewrite(min(max(min(x, y), z), (max(y, z) + c0)), max(min(x, y), z), (0 <= c0)) ||
 rewrite(min(max(min(x, y), z), (max(z, x) + c0)), max(min(x, y), z), (0 <= c0)) ||
 rewrite(min(max(min(x, y), z), (max(z, y) + c0)), max(min(x, y), z), (0 <= c0)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(max(x, y), min(z, max(w, x))), min(max(x, min(w, y)), z)) ||
 rewrite(min(max(x, y), min(z, max(w, y))), min(max(y, min(w, x)), z)) ||
 rewrite(min(max(x, y), min(z, max(x, w))), min(max(x, min(w, y)), z)) ||
 rewrite(min(max(x, y), min(z, max(y, w))), min(max(y, min(w, x)), z)) ||
 rewrite(min(max(x, y), min(max(x, z), w)), min(max(x, min(y, z)), w)) ||
 rewrite(min(max(x, y), min(max(y, z), w)), min(max(y, min(x, z)), w)) ||
 rewrite(min(max(x, y), min(max(z, x), w)), min(max(x, min(z, y)), w)) ||
 rewrite(min(max(x, y), min(max(z, y), w)), min(max(y, min(z, x)), w)))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::Min) && (
 rewrite(min(select(x, y, z), min(w, select(x, u, v))), min(select(x, min(y, u), min(z, v)), w)) ||
 rewrite(min(select(x, y, z), min(select(x, w, u), v)), min(select(x, min(y, w), min(u, z)), v))))))
