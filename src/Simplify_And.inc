((a.node_type() == IRNodeType::And) && (
((b.node_type() == IRNodeType::LT) && (
 rewrite(((x && (y < z)) && (y < (w + u))), (x && (y < min(z, (w + u))))) ||
 rewrite(((x && (y < (z + w))) && (y < u)), (x && (y < min((z + w), u)))) ||
 rewrite((((x < y) && z) && (x < (w + u))), (z && (x < min(y, (w + u))))) ||
 rewrite((((x < (y + z)) && w) && (x < u)), ((x < min(u, (y + z))) && w))))))
