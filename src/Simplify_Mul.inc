((b.node_type() == IRNodeType::Add) && (
 rewrite((x*(y + ((z*c0) + (w + (u*c1))))), (x*(y + (((u - z)*c1) + w))), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((x*(y + ((z*c0) + ((w*c1) + u)))), (x*(y + (((w - z)*c1) + u))), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((x*(((y*c0) + (z + (w*c1))) + u)), (x*((((w - y)*c1) + z) + u)), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((x*(((y*c0) + ((z*c1) + w)) + u)), (x*((((z - y)*c1) + w) + u)), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((x*select(y, c0, c1)), select(y, x, c1), ((c1 == 0) && (c0 == 1))))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
 rewrite(((x + ((y*c0) + (z + (w*c1))))*u), ((x + (((w - y)*c1) + z))*u), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite(((x + ((y*c0) + ((z*c1) + w)))*u), ((x + (((z - y)*c1) + w))*u), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite(((x + (y*c0))*c1), ((y*fold((c0*c1))) - x), (c1 == -1)) ||
 rewrite(((((x*c0) + (y + (z*c1))) + w)*u), (((((z - x)*c1) + y) + w)*u), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite(((((x*c0) + ((y*c1) + z)) + w)*u), (((((y - x)*c1) + z) + w)*u), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((((x*c0) + y)*c1), ((x*fold((c0*c1))) - y), (c1 == -1)))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite((((x*c0) - y)*c1), ((y + x)*fold((c1*-1))), ((c0 == -1) || (c1 == 0))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, c0, c1)*y), select(x, y, c1), ((c1 == 0) && (c0 == 1)))))
