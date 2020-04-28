((b.node_type() == IRNodeType::Add) && (
 rewrite((x < (y + 1)), (x <= y)) ||
 rewrite((x < (y + (x + z))), (0 < (y + z))) ||
 rewrite((x < (y + (z + x))), (0 < (y + z))) ||
 rewrite((x < ((x + y) + z)), (0 < (y + z))) ||
 rewrite((x < ((y + x) + z)), (0 < (y + z))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max((x - y), ((y - x) + c0)) < c1), 0, (((c1*2) + -1) <= c0))))
