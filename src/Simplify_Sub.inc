((a.node_type() == IRNodeType::Add) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x + (y*z)) - (w + (u*y))), (x - (w - ((z - u)*y)))) ||
 rewrite(((x + (y*z)) - (w + (u*z))), (x - (w - ((y - u)*z)))) ||
 rewrite(((x + (y*z)) - (w + (y*u))), (x - (w - ((z - u)*y)))) ||
 rewrite(((x + (y*z)) - (w + (z*u))), (((y - u)*z) - (w - x))) ||
 rewrite(((x + (y*z)) - ((w*y) + u)), (x + (((z - w)*y) - u))) ||
 rewrite(((x + (y*z)) - ((w*z) + u)), (x + (((y - w)*z) - u))) ||
 rewrite(((x + (y*z)) - ((y*w) + u)), (x + (((z - w)*y) - u))) ||
 rewrite(((x + (y*z)) - ((z*w) + u)), (x + (((y - w)*z) - u))) ||
 rewrite((((x*y) + z) - (w + (u*x))), ((x*(y - u)) - (w - z))) ||
 rewrite((((x*y) + z) - (w + (u*y))), ((y*(x - u)) - (w - z))) ||
 rewrite((((x*y) + z) - (w + (x*u))), ((x*(y - u)) - (w - z))) ||
 rewrite((((x*y) + z) - (w + (y*u))), ((y*(x - u)) - (w - z))) ||
 rewrite((((x*y) + z) - ((w*x) + u)), (((x*(y - w)) - u) + z)) ||
 rewrite((((x*y) + z) - ((w*y) + u)), (((y*(x - w)) - u) + z)) ||
 rewrite((((x*y) + z) - ((x*w) + u)), (((x*(y - w)) - u) + z)) ||
 rewrite((((x*y) + z) - ((y*w) + u)), (((y*(x - w)) - u) + z))))))
