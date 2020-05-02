((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite((((x*c0) + (y + (z + (x*c1))))*c2), ((y + z)*c2), (((c0 + c1) == 0) || (c2 == 0))) ||
 rewrite((((x*c0) + (y + ((x*c1) + z)))*c2), ((y + z)*c2), (((c0 + c1) == 0) || (c2 == 0))) ||
 rewrite((((x*c0) + (y + (x*c1)))*c2), (y*c2), (((c0 + c1) == 0) || (c2 == 0))) ||
 rewrite((((x*c0) + ((y + (x*c1)) + z))*c2), ((y + z)*c2), (((c0 + c1) == 0) || (c2 == 0))) ||
 rewrite((((x*c0) + (((x*c1) + y) + z))*c2), ((y + z)*c2), (((c0 + c1) == 0) || (c2 == 0))) ||
 rewrite((((x*c0) + ((x*c1) + y))*c2), (y*c2), (((c0 + c1) == 0) || (c2 == 0))))) ||
((a.node_type() == IRNodeType::Min) && EVAL_IN_LAMBDA(
 rewrite((min(x, min(y, min(z, min(w, (z + c0)))))*c1), (min(x, min(y, min(w, z)))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(y, min(z, min((z + c0), w))))*c1), (min(x, min(y, min(z, w)))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(y, min(min(z, (w + c0)), w)))*c1), (min(x, min(y, min(z, w)))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(y, min(min((z + c0), w), z)))*c1), (min(x, min(y, min(w, z)))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(min(y, min(z, (y + c0))), w))*c1), (min(x, min(min(z, y), w))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(min(y, min((y + c0), z)), w))*c1), (min(x, min(min(y, z), w))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(min(min(y, (z + c0)), z), w))*c1), (min(x, min(min(y, z), w))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(x, min(min(min((y + c0), z), y), w))*c1), (min(x, min(min(z, y), w))*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(x, min(y, min(z, (y + c0)))), w)*c1), (min(min(x, min(z, y)), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(x, min(y, min((y + c0), z))), w)*c1), (min(min(x, min(y, z)), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(x, min(min(y, (z + c0)), z)), w)*c1), (min(min(x, min(y, z)), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(x, min(min((y + c0), z), y)), w)*c1), (min(min(x, min(z, y)), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(min(x, min(y, (x + c0))), z), w)*c1), (min(min(min(y, x), z), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(min(x, min((x + c0), y)), z), w)*c1), (min(min(min(x, y), z), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(min(min(x, (y + c0)), y), z), w)*c1), (min(min(min(x, y), z), w)*c1), ((0 <= c0) || (c1 == 0))) ||
 rewrite((min(min(min(min((x + c0), y), x), z), w)*c1), (min(min(min(y, x), z), w)*c1), ((0 <= c0) || (c1 == 0)))))
