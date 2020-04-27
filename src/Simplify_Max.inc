((a.node_type() == IRNodeType::Max) && (
((b.node_type() == IRNodeType::Mul) && (
 rewrite(max(max(x, ((y*z)*c0)), ((w*y)*c0)), max(x, (max((y*z), (w*y))*c0)), ((c0 <= max((c0*4), -7)) && ((c0*-42) <= max((c0*-36), 7)))) ||
 rewrite(max(max(x, ((y*z)*c0)), ((w*z)*c0)), max(x, (max((y*z), (w*z))*c0)), ((c0 <= max((c0*4), -7)) && ((c0*-42) <= max((c0*-36), 7)))) ||
 rewrite(max(max(x, ((y*z)*c0)), ((y*w)*c0)), max(x, (max((y*z), (y*w))*c0)), ((c0 <= max((c0*4), -7)) && ((c0*-42) <= max((c0*-36), 7)))) ||
 rewrite(max(max(x, ((y*z)*c0)), ((z*w)*c0)), max(x, (max((y*z), (z*w))*c0)), ((c0 <= max((c0*4), -7)) && ((c0*-42) <= max((c0*-36), 7)))) ||
 rewrite(max(max((x*min(y, z)), (y*c0)), (z*c0)), max((x*min(y, z)), (max(z, y)*c0)), ((min(c0, -4) <= (min(c0, -1)*4)) && ((max((c0*3), -7) + min(min((c0*-3), c0), 7)) == 0))) ||
 rewrite(max(max((x*min(y, z)), (z*c0)), (y*c0)), max((x*min(y, z)), (max(y, z)*c0)), ((min(c0, -4) <= (min(c0, -1)*4)) && ((max((c0*3), -7) + min(min((c0*-3), c0), 7)) == 0))) ||
 rewrite(max(max(((x*y)*c0), z), ((w*x)*c0)), max(z, (max((x*y), (w*x))*c0)), ((c0 <= max((c0*4), -7)) && ((c0*-42) <= max((c0*-36), 7)))) ||
 rewrite(max(max(((x*y)*c0), z), ((w*y)*c0)), max(z, (max((x*y), (w*y))*c0)), ((c0 <= max((c0*4), -7)) && ((c0*-42) <= max((c0*-36), 7)))) ||
 rewrite(max(max(((x*y)*c0), z), ((x*w)*c0)), max(z, (max((x*y), (x*w))*c0)), ((c0 <= max((c0*4), -7)) && ((c0*-42) <= max((c0*-36), 7)))) ||
 rewrite(max(max(((x*y)*c0), z), ((y*w)*c0)), max(z, (max((x*y), (y*w))*c0)), ((c0 <= max((c0*4), -7)) && ((c0*-42) <= max((c0*-36), 7)))) ||
 rewrite(max(max((min(x, y)*c0), (x*c1)), (y*c1)), max((min(x, y)*c0), (max(y, x)*c1)), ((c1 <= max((c1*5), c0)) && ((max((c0*-1), c1) + min(min((c1*-1), c0), c1)) == 0))) ||
 rewrite(max(max((min(x, y)*c0), (y*c1)), (x*c1)), max((min(x, y)*c0), (max(x, y)*c1)), ((c1 <= max((c1*5), c0)) && ((max((c0*-1), c1) + min(min((c1*-1), c0), c1)) == 0))) ||
 rewrite(max(max((min(x, y)*z), (x*c0)), (y*c0)), max((min(x, y)*z), (max(y, x)*c0)), ((min(c0, -4) <= (min(c0, -1)*4)) && ((max((c0*3), -7) + min(min((c0*-3), c0), 7)) == 0))) ||
 rewrite(max(max((min(x, y)*z), (y*c0)), (x*c0)), max((min(x, y)*z), (max(x, y)*c0)), ((min(c0, -4) <= (min(c0, -1)*4)) && ((max((c0*3), -7) + min(min((c0*-3), c0), 7)) == 0)))))))
