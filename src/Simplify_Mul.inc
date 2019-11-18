((a.node_type() == IRNodeType::Add) && (
 rewrite((((x*c0) + ((0 - y) - z))*c2), (((x*c0) - (y + z))*c2)))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite((min(min((x*c0), y), (z*c0))*c0), (min((min(x, z)*c0), y)*c0), (1 <= c0))))