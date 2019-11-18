((b.node_type() == IRNodeType::Add) && (
 rewrite((x != (y + min(x, 0))), (max(0, x) != y)) ||
 rewrite((x != (y + max(x, 0))), (min(0, x) != y)) ||
 rewrite((x != (z + (x - y))), (z != y)) ||
 rewrite((x != ((x - y) + z)), (z != y)) ||
 rewrite((x != (min(x, 0) + y)), (max(x, 0) != y)) ||
 rewrite((x != (max(x, 0) + y)), (y != min(x, 0))) ||
 rewrite((y != (x + (x - y))), (y != x)) ||
 rewrite((y != ((x - y) + x)), (x != y)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((x != ((x - y) - z)), ((z + y) != 0)) ||
 rewrite((y != ((x + y) - z)), (x != z)) ||
 rewrite((y != ((y + x) - z)), (x != z)) ||
 rewrite((y != ((x - y) - y)), ((3*y) != x)))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((x != (x/c0)), (x != 0), (c0 <= -1)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((y != min(z, min(x, y))), (min(z, x) < y)) ||
 rewrite((y != min(z, min(y, x))), (min(z, x) < y)) ||
 rewrite((y != min(min(x, y), z)), (min(x, z) < y)) ||
 rewrite((y != min(min(y, x), z)), (min(z, x) < y)))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((y != max(z, max(x, y))), (y < max(z, x))) ||
 rewrite((y != max(z, max(y, x))), (y < max(z, x))) ||
 rewrite((y != max(max(x, y), z)), (y < max(x, z))) ||
 rewrite((y != max(max(y, x), z)), (y < max(x, z))))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((y != select(x, 0, (y - z))), (select(x, y, z) != 0)) ||
 rewrite((y != select(x, (y - z), 0)), (select(x, z, y) != 0)) ||
 rewrite((y != select(x, (y - z), z)), (select(x, 0, y) != z)) ||
 rewrite((y != select(x, (y*c0), z)), (y != select(x, 0, z)), ((2 <= c0) || (c0 <= -1))) ||
 rewrite((z != select(x, y, (y + z))), (select(x, z, 0) != y)) ||
 rewrite((z != select(x, y, (z + y))), (select(x, z, 0) != y)) ||
 rewrite((z != select(x, y, (z - y))), (select(x, z, 0) != y)) ||
 rewrite((z != select(x, y, (z*c0))), (z != select(x, y, 0)), ((2 <= c0) || (c0 <= -1))) ||
 rewrite((z != select(x, (y + z), y)), (select(x, 0, z) != y)) ||
 rewrite((z != select(x, (z + y), y)), (y != select(x, 0, z))))) ||
((a.node_type() == IRNodeType::Add) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite((((x - y) + (x - z)) != 0), (x != (y - (x - z)))) ||
 rewrite((((x - y) + (z - y)) != 0), (z != (y + (y - x)))) ||
 rewrite((((x*x) + (y - z)) != 0), (((x*x) + y) != z)) ||
 rewrite(((min(x, y) + (x - z)) != 0), ((min(x, y) + x) != z)) ||
 rewrite(((min(x, y) + (y - z)) != 0), ((min(x, y) + y) != z)) ||
 rewrite(((min(x, y) + (z - x)) != 0), (z != max(0, (x - y)))) ||
 rewrite(((min(x, y) + (z - y)) != 0), (z != max(0, (y - x)))) ||
 rewrite(((max(x, y) + (x - z)) != 0), ((max(x, y) + x) != z)) ||
 rewrite(((max(x, y) + (y - z)) != 0), ((max(x, y) + y) != z)) ||
 rewrite(((max(x, y) + (z - x)) != 0), (z != min(0, (x - y)))) ||
 rewrite(((max(x, y) + (z - y)) != 0), (z != min(0, (y - x)))) ||
 rewrite(((select(x, y, z) + (w - y)) != 0), (w != select(x, 0, (y - z)))) ||
 rewrite(((select(x, y, z) + (w - z)) != 0), (w != select(x, (z - y), 0))) ||
 rewrite(((select(x, y, z) + (y - w)) != 0), ((select(x, y, z) + y) != w)) ||
 rewrite(((select(x, y, z) + (z - w)) != 0), ((select(x, y, z) + z) != w)))) ||
 rewrite(((y + (y - x)) != x), (y != x)) ||
 rewrite(((y + min(x, 0)) != x), (max(x, 0) != y)) ||
 rewrite(((y + max(x, 0)) != x), (min(x, 0) != y)) ||
 rewrite(((z + (x - y)) != x), (z != y)) ||
 rewrite((((x - y) + z) != x), (z != y)) ||
 rewrite((((y - x) + y) != x), (x != y)) ||
 rewrite(((min(x, 0) + y) != x), (max(x, 0) != y)) ||
 rewrite(((max(x, 0) + y) != x), (min(x, 0) != y)) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((y + x) != (x - z)), ((y + z) != 0)) ||
 rewrite(((y + x) != (y - z)), ((z + x) != 0)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((x + y) != (y*2)), (x != y)) ||
 rewrite(((y + x) != (y*2)), (x != y)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(((y + x) != select(z, x, y)), (select(z, y, x) != 0)) ||
 rewrite(((y + x) != select(z, y, x)), (select(z, x, y) != 0)))))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite((((y + x) - z) != x), (y != z)) ||
 rewrite((((y + x) - z) != y), (x != z)) ||
 rewrite((((x - y) - z) != x), ((y + z) != 0)) ||
 rewrite((((y - x) - x) != x), (y != (3*x))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((x - y) != (x + z)), ((z + y) != 0)) ||
 rewrite(((x - y) != (z + x)), ((z + y) != 0)))))) ||
((a.node_type() == IRNodeType::Mul) && (
 rewrite(((x*x) != c0), true, (c0 <= -1)))) ||
((a.node_type() == IRNodeType::Div) && (
 rewrite(((x/c0) != x), (x != 0), (c0 <= -1)))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite((min(z, min(y, x)) != x), (min(z, y) < x)) ||
 rewrite((min(z, min(y, x)) != y), (min(z, x) < y)) ||
 rewrite((min(min(y, x), z) != x), (min(y, z) < x)) ||
 rewrite((min(min(y, x), z) != y), (min(x, z) < y)) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((min(y, x) != max(x, y)), (x != y)) ||
 rewrite((min(y, x) != max(y, x)), (x != y)))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max(z, max(y, x)) != x), (x < max(y, z))) ||
 rewrite((max(z, max(y, x)) != y), (y < max(z, x))) ||
 rewrite((max(max(y, x), z) != x), (x < max(y, z))) ||
 rewrite((max(max(y, x), z) != y), (y < max(x, z))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((max(y, x) != min(x, y)), (x != y)) ||
 rewrite((max(y, x) != min(y, x)), (y != x)))))) ||
((a.node_type() == IRNodeType::Select) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite((select(x, y, (y - z)) != 0), (select(x, 0, z) != y)) ||
 rewrite((select(x, y, (z - y)) != 0), (y != select(x, 0, z))) ||
 rewrite((select(x, (y - z), y) != 0), (select(x, z, 0) != y)) ||
 rewrite((select(x, (y - z), z) != 0), (select(x, y, 0) != z)) ||
 rewrite((select(x, (y - z), (z - w)) != 0), (select(x, y, w) != z)))) ||
 rewrite((select(x, 0, (y - z)) != y), (select(x, y, z) != 0)) ||
 rewrite((select(x, y, (z*c0)) != z), (z != select(x, y, 0)), ((2 <= c0) || (c0 <= -1))) ||
 rewrite((select(x, (y - z), 0) != y), (select(x, z, y) != 0)) ||
 rewrite((select(x, (y*c0), z) != y), (y != select(x, 0, z)), ((2 <= c0) || (c0 <= -1))) ||
 rewrite((select(y, z, (x + z)) != x), (z != select(y, x, 0))) ||
 rewrite((select(y, z, (z + x)) != x), (select(y, x, 0) != z)) ||
 rewrite((select(y, z, (x - z)) != x), (select(y, x, 0) != z)) ||
 rewrite((select(y, (x + z), z) != x), (select(y, 0, x) != z)) ||
 rewrite((select(y, (z + x), z) != x), (select(y, 0, x) != z)) ||
 rewrite((select(y, (x - z), z) != x), (select(y, 0, x) != z)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((select(x, y, z) != (y + z)), (select(x, z, y) != 0)) ||
 rewrite((select(x, y, z) != (z + y)), (select(x, z, y) != 0))))))