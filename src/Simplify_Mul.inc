((a.node_type() == IRNodeType::Add) && (
 rewrite((((x*c0) + ((0 - y) - z))*c2), (((x*c0) - (y + z))*c2)))) ||
((a.node_type() == IRNodeType::Mul) && (
 rewrite(((x*((y*c0) + 0))*c0), (((y*fold((c0*c0))) + 0)*x)))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite((min(min((x*c0), y), (z*c0))*c0), (min((min(x, z)*c0), y)*c0), (1 <= c0)))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max((x*c0), 1)*c0), max((fold(max((c0*c0), (0 - min(c0, -1))))*x), c0), (1 <= c0)))) ||
((a.node_type() == IRNodeType::Select) && (
((b.node_type() == IRNodeType::Select) && (
 rewrite((select(x, y, z)*select(x, w, y)), (select(x, w, z)*y)) ||
 rewrite((select(x, y, z)*select(x, z, w)), (select(x, y, w)*z))))))