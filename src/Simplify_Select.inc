((b.node_type() == IRNodeType::Add) && (
 rewrite(select(x, (y + c0), c1), (select(x, y, fold((c1 - c0))) + c0)) ||
 rewrite(select(x, (y + (z + w)), (u + (v + w))), (w + select(x, (z + y), (u + v)))) ||
 rewrite(select(x, (y + (z + w)), (u + (v + z))), (z + select(x, (w + y), (u + v)))) ||
 rewrite(select(x, (y + (z + w)), (u + (w + v))), (w + select(x, (z + y), (u + v)))) ||
 rewrite(select(x, (y + (z + w)), (u + (z + v))), (z + select(x, (w + y), (u + v)))) ||
 rewrite(select(x, (y + (z + w)), ((u + w) + v)), (w + select(x, (z + y), (v + u)))) ||
 rewrite(select(x, (y + (z + w)), ((u + z) + v)), (z + select(x, (y + w), (u + v)))) ||
 rewrite(select(x, (y + (z + w)), ((w + u) + v)), (w + select(x, (z + y), (v + u)))) ||
 rewrite(select(x, (y + (z + w)), ((z + u) + v)), (z + select(x, (y + w), (v + u)))) ||
 rewrite(select(x, (y + (z + (w + u))), (u + w)), (w + select(x, (z + (y + u)), u))) ||
 rewrite(select(x, (y + (z + (w + u))), (w + u)), (w + select(x, (z + (y + u)), u))) ||
 rewrite(select(x, (y + ((z + w) + u)), (w + z)), (w + select(x, ((z + y) + u), z))) ||
 rewrite(select(x, (y + ((z + w) + u)), (z + w)), (z + select(x, ((y + w) + u), w))) ||
 rewrite(select(x, ((y + z) + w), (u + (v + y))), (y + select(x, (z + w), (u + v)))) ||
 rewrite(select(x, ((y + z) + w), (u + (v + z))), (z + select(x, (y + w), (u + v)))) ||
 rewrite(select(x, ((y + z) + w), (u + (y + v))), (y + select(x, (z + w), (u + v)))) ||
 rewrite(select(x, ((y + z) + w), (u + (z + v))), (z + select(x, (y + w), (u + v)))) ||
 rewrite(select(x, ((y + z) + w), ((u + y) + v)), (y + select(x, (z + w), (v + u)))) ||
 rewrite(select(x, ((y + z) + w), ((u + z) + v)), (z + select(x, (y + w), (v + u)))) ||
 rewrite(select(x, ((y + z) + w), ((y + u) + v)), (y + select(x, (z + w), (v + u)))) ||
 rewrite(select(x, ((y + z) + w), ((z + u) + v)), (z + select(x, (y + w), (v + u)))) ||
 rewrite(select(x, ((y + (z + w)) + u), (w + z)), (w + select(x, (y + (z + u)), z))) ||
 rewrite(select(x, ((y + (z + w)) + u), (z + w)), (w + select(x, (y + (z + u)), z))) ||
 rewrite(select(x, (((y + z) + w) + u), (y + z)), (y + select(x, ((z + w) + u), z))) ||
 rewrite(select(x, (((y + z) + w) + u), (z + y)), (z + select(x, ((y + w) + u), y))) ||
 rewrite(select(x, (y + (z - w)), (u - w)), (select(x, (z + y), u) - w)) ||
 rewrite(select(x, ((y - z) + w), (u - z)), (select(x, (y + w), u) - z)) ||
 rewrite(select(x, (y + (y*z)), (w*y)), (y*select(x, (z + 1), w))) ||
 rewrite(select(x, (y + (y*z)), (y*w)), (y*select(x, (z + 1), w))) ||
 rewrite(select(x, (y + (z*y)), (w*y)), (y*select(x, (z + 1), w))) ||
 rewrite(select(x, (y + (z*y)), (y*w)), (y*select(x, (z + 1), w))) ||
 rewrite(select(x, ((y*z) + y), (w*y)), (y*select(x, (z + 1), w))) ||
 rewrite(select(x, ((y*z) + y), (y*w)), (y*select(x, (z + 1), w))) ||
 rewrite(select(x, ((y*z) + z), (w*z)), (z*select(x, (y + 1), w))) ||
 rewrite(select(x, ((y*z) + z), (z*w)), (z*select(x, (y + 1), w))))) ||
((a.node_type() == IRNodeType::LT) && EVAL_IN_LAMBDA(
 rewrite(select((x < (y + c0)), c1, z), select((y < x), z, fold(select((0 < c0), c1, 1))), (((c0 <= 1) || (c1 == -1)) && ((((c0 <= 1) || (c1 == 0)) && (0 < c0)) && (select((-1 < c0), c1, 0) == select((0 < c0), c1, 1))))) ||
 rewrite(select((x < (y + c0)), z, c1), select((y < x), fold(select((1 < c0), 0, c1)), z), ((((c1 == 1) && (c1 == 0)) || (0 < c0)) && (c0 <= 1))) ||
 rewrite(select(((x % c0) < select(y, c1, c2)), z, w), select(y, w, z), ((c1 <= 0) && (max(max(max((-1 % c0), (1 % c0)), (3 % c0)), (7721 % c0)) < c2))))) ||
((a.node_type() == IRNodeType::And) && EVAL_IN_LAMBDA(
 rewrite(select((x && !(y || !z)), w, w), select((y || !(x && z)), w, w)) ||
 rewrite(select((x && !(!y || z)), w, w), select((z || !(x && y)), w, w)) ||
 rewrite(select((!(x || !y) && z), w, w), select((x || !(y && z)), w, w)) ||
 rewrite(select((!(!x || y) && z), w, w), select((y || !(x && z)), w, w))))
