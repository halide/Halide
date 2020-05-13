((b.node_type() == IRNodeType::Add) && (
 rewrite((x*(y + ((z*c0) + (w + (u*c1))))), (x*(y + (((u - z)*c1) + w))), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((x*(y + ((z*c0) + ((w*c1) + u)))), (x*(y + (((w - z)*c1) + u))), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((x*(((y*c0) + (z + (w*c1))) + u)), (x*((((w - y)*c1) + z) + u)), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((x*(((y*c0) + ((z*c1) + w)) + u)), (x*((((z - y)*c1) + w) + u)), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((x*((y*c0) + (z + (w + (u*c1))))), (x*(w + (z + ((u - y)*fold((0 - ((c0*-7) + (c1*-8)))))))), ((((c0*-7) + (c1*-8))*2) == ((c0*7) + (c1*5)))) ||
 rewrite((x*((y*c0) + (z + ((w*c1) + u)))), (x*(u + (z + ((w - y)*fold((0 - ((c0*-7) + (c1*-8)))))))), ((((c0*-7) + (c1*-8))*2) == ((c0*7) + (c1*5)))) ||
 rewrite((x*((y*c0) + ((z + (w*c1)) + u))), (x*(z + (((w - y)*fold((0 - ((c0*-7) + (c1*-8))))) + u))), ((((c0*-7) + (c1*-8))*2) == ((c0*7) + (c1*5)))) ||
 rewrite((x*((y*c0) + (((z*c1) + w) + u))), (x*(w + (((z - y)*fold((0 - ((c0*-7) + (c1*-8))))) + u))), ((((c0*-7) + (c1*-8))*2) == ((c0*7) + (c1*5)))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((x*select(y, 1, 0)), select(y, x, 0)))) ||
((a.node_type() == IRNodeType::Add) && EVAL_IN_LAMBDA(
((b.node_type() == IRNodeType::IntImm) && (
 rewrite(((x + (y*c0))*-1), ((y*fold((c0*-1))) - x)) ||
 rewrite(((x + select(y, c0, c1))*-1), (select(y, fold((c0*-1)), fold((((c1 + 1)*-1) + 1))) - x)) ||
 rewrite((((x*c0) + y)*-1), ((x*fold((c0*-1))) - y)) ||
 rewrite(((select(x, c0, c1) + y)*-1), (select(x, fold((c0*-1)), fold((((c1 + 1)*-1) + 1))) - y)))) ||
 rewrite(((x + ((y*c0) + (z + (w*c1))))*u), ((x + (((w - y)*c1) + z))*u), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite(((x + ((y*c0) + ((z*c1) + w)))*u), ((x + (((z - y)*c1) + w))*u), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite(((((x*c0) + (y + (z*c1))) + w)*u), (((((z - x)*c1) + y) + w)*u), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite(((((x*c0) + ((y*c1) + z)) + w)*u), (((((y - x)*c1) + z) + w)*u), (((c1*4) + ((c0*-3) + (c1*-7))) == 0)) ||
 rewrite((((x*c0) + (y + (z + (w*c1))))*u), ((z + (y + ((w - x)*fold((0 - ((c0*-7) + (c1*-8)))))))*u), ((((c0*-7) + (c1*-8))*2) == ((c0*7) + (c1*5)))) ||
 rewrite((((x*c0) + (y + ((z*c1) + w)))*u), ((w + (y + ((z - x)*fold((0 - ((c0*-7) + (c1*-8)))))))*u), ((((c0*-7) + (c1*-8))*2) == ((c0*7) + (c1*5)))) ||
 rewrite((((x*c0) + ((y + (z*c1)) + w))*u), ((y + (((z - x)*fold((0 - ((c0*-7) + (c1*-8))))) + w))*u), ((((c0*-7) + (c1*-8))*2) == ((c0*7) + (c1*5)))) ||
 rewrite((((x*c0) + (((y*c1) + z) + w))*u), ((z + (((y - x)*fold((0 - ((c0*-7) + (c1*-8))))) + w))*u), ((((c0*-7) + (c1*-8))*2) == ((c0*7) + (c1*5)))))) ||
((a.node_type() == IRNodeType::Sub) && EVAL_IN_LAMBDA(
 rewrite(((0 - x)*c0), (x*fold(((0 + -1)*c0)))) ||
 rewrite((((x*c0) - y)*c1), ((y + x)*fold((c1*-1))), ((c0 == -1) || (c1 == 0))))) ||
((a.node_type() == IRNodeType::Select) && EVAL_IN_LAMBDA(
 rewrite((select(x, 1, 0)*y), select(x, y, 0)) ||
 rewrite((select(x, c0, c1)*c2), select(x, fold((c0*c2)), fold((c1*c2))))))
