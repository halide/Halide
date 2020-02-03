((a.node_type() == IRNodeType::IntImm) && (
((b.node_type() == IRNodeType::Add) && (
 rewrite((0 == ((x - y) + (x - z))), (x == (y - (x - z)))) ||
 rewrite((0 == ((x - y) + (z - y))), (z == (y + (y - x)))) ||
 rewrite((0 == ((x*x) + (y - z))), (((x*x) + y) == z)) ||
 rewrite((0 == (min(y, x) + (x - z))), ((min(y, x) + x) == z)) ||
 rewrite((0 == (min(y, x) + (y - z))), ((min(y, x) + y) == z)) ||
 rewrite((0 == (min(y, x) + (z - x))), (z == max(0, (x - y)))) ||
 rewrite((0 == (min(y, x) + (z - y))), (z == max(0, (y - x)))) ||
 rewrite((0 == (max(y, x) + (x - z))), ((max(y, x) + x) == z)) ||
 rewrite((0 == (max(y, x) + (y - z))), ((max(y, x) + y) == z)) ||
 rewrite((0 == (max(y, x) + (z - x))), (z == min(0, (x - y)))) ||
 rewrite((0 == (max(y, x) + (z - y))), (z == min(0, (y - x)))) ||
 rewrite((0 == (select(x, y, z) + (w - y))), (w == select(x, 0, (y - z)))) ||
 rewrite((0 == (select(x, y, z) + (w - z))), (w == select(x, (z - y), 0))) ||
 rewrite((0 == (select(x, y, z) + (y - w))), ((select(x, y, z) + y) == w)) ||
 rewrite((0 == (select(x, y, z) + (z - w))), ((select(x, y, z) + z) == w)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((0 == select(x, y, (y - z))), (select(x, 0, z) == y)) ||
 rewrite((0 == select(x, y, (z - y))), (y == select(x, 0, z))) ||
 rewrite((0 == select(x, (y - z), y)), (select(x, z, 0) == y)) ||
 rewrite((0 == select(x, (y - z), z)), (select(x, y, 0) == z)) ||
 rewrite((0 == select(x, (y - z), (z - w))), (select(x, y, w) == z)))))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((x == (y + min(x, 0))), (max(x, 0) == y)) ||
 rewrite((x == (y + max(x, 0))), (y == min(x, 0))) ||
 rewrite((x == (z + (x - y))), (z == y)) ||
 rewrite((x == ((x - y) + z)), (z == y)) ||
 rewrite((x == (min(x, 0) + y)), (max(x, 0) == y)) ||
 rewrite((x == (max(x, 0) + y)), (min(x, 0) == y)) ||
 rewrite((y == (x + (x - y))), (y == x)) ||
 rewrite((y == ((x - y) + x)), (x == y)))) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite((x == ((x - y) - z)), ((z + y) == 0)) ||
 rewrite((y == ((x + y) - z)), (x == z)) ||
 rewrite((y == ((y + x) - z)), (x == z)) ||
 rewrite((y == ((x - y) - y)), (x == (y*3))))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite((c0 == (x*x)), false, (c0 <= -1)))) ||
((b.node_type() == IRNodeType::Div) && (
 rewrite((x == (x/c0)), (x == 0), (c0 <= -1)))) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((y == min(z, min(x, y))), (y <= min(z, x))) ||
 rewrite((y == min(z, min(y, x))), (y <= min(z, x))) ||
 rewrite((y == min(min(x, y), z)), (y <= min(x, z))) ||
 rewrite((y == min(min(y, x), z)), (y <= min(x, z))))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((y == max(z, max(x, y))), (max(z, x) <= y)) ||
 rewrite((y == max(z, max(y, x))), (max(z, x) <= y)) ||
 rewrite((y == max(max(x, y), z)), (max(x, z) <= y)) ||
 rewrite((y == max(max(y, x), z)), (max(z, x) <= y)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite((y == select(x, 0, (y - z))), (select(x, y, z) == 0)) ||
 rewrite((y == select(x, (y - z), 0)), (select(x, z, y) == 0)) ||
 rewrite((y == select(x, (y - z), z)), (select(x, 0, y) == z)) ||
 rewrite((y == select(x, (y*c0), z)), (y == select(x, 0, z)), ((2 <= c0) || (c0 <= -1))) ||
 rewrite((z == select(x, y, (y + z))), (select(x, z, 0) == y)) ||
 rewrite((z == select(x, y, (z + y))), (select(x, z, 0) == y)) ||
 rewrite((z == select(x, y, (z - y))), (select(x, z, 0) == y)) ||
 rewrite((z == select(x, y, (z*c0))), (select(x, y, 0) == z), ((2 <= c0) || (c0 <= -1))) ||
 rewrite((z == select(x, (y + z), y)), (select(x, 0, z) == y)) ||
 rewrite((z == select(x, (z + y), y)), (select(x, 0, z) == y)))) ||
((a.node_type() == IRNodeType::Add) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite((((x - y) + (x - z)) == 0), (x == (y - (x - z)))) ||
 rewrite((((x - y) + (z - y)) == 0), (z == (y + (y - x)))) ||
 rewrite((((x*x) + (y - z)) == 0), (((x*x) + y) == z)) ||
 rewrite(((min(x, y) + (x - z)) == 0), ((min(x, y) + x) == z)) ||
 rewrite(((min(x, y) + (y - z)) == 0), ((min(x, y) + y) == z)) ||
 rewrite(((min(x, y) + (z - x)) == 0), (z == max((x - y), 0))) ||
 rewrite(((min(x, y) + (z - y)) == 0), (z == max((y - x), 0))) ||
 rewrite(((max(x, y) + (x - z)) == 0), ((max(x, y) + x) == z)) ||
 rewrite(((max(x, y) + (y - z)) == 0), ((max(x, y) + y) == z)) ||
 rewrite(((max(x, y) + (z - x)) == 0), (z == min((x - y), 0))) ||
 rewrite(((max(x, y) + (z - y)) == 0), (z == min((y - x), 0))) ||
 rewrite(((select(x, y, z) + (w - y)) == 0), (w == select(x, 0, (y - z)))) ||
 rewrite(((select(x, y, z) + (w - z)) == 0), (w == select(x, (z - y), 0))) ||
 rewrite(((select(x, y, z) + (y - w)) == 0), ((select(x, y, z) + y) == w)) ||
 rewrite(((select(x, y, z) + (z - w)) == 0), ((select(x, y, z) + z) == w)))) ||
 rewrite(((y + (y - x)) == x), (x == y)) ||
 rewrite(((y + min(x, 0)) == x), (max(x, 0) == y)) ||
 rewrite(((y + max(x, 0)) == x), (min(x, 0) == y)) ||
 rewrite(((z + (x - y)) == x), (z == y)) ||
 rewrite((((x - y) + z) == x), (z == y)) ||
 rewrite((((y - x) + y) == x), (y == x)) ||
 rewrite(((min(x, 0) + y) == x), (y == max(x, 0))) ||
 rewrite(((max(x, 0) + y) == x), (min(x, 0) == y)) ||
((b.node_type() == IRNodeType::Sub) && (
 rewrite(((y + x) == (y - z)), ((z + x) == 0)) ||
 rewrite(((z + x) == (x - y)), ((z + y) == 0)))) ||
((b.node_type() == IRNodeType::Mul) && (
 rewrite(((x + y) == (y*2)), (y == x)) ||
 rewrite(((y + x) == (y*2)), (y == x)))) ||
((b.node_type() == IRNodeType::Select) && (
 rewrite(((y + z) == select(x, y, z)), (select(x, z, y) == 0)) ||
 rewrite(((z + y) == select(x, y, z)), (select(x, z, y) == 0)))))) ||
((a.node_type() == IRNodeType::Sub) && (
 rewrite((((y + x) - z) == x), (y == z)) ||
 rewrite((((y + x) - z) == y), (x == z)) ||
 rewrite((((x - y) - z) == x), ((y + z) == 0)) ||
 rewrite((((y - x) - x) == x), ((x*3) == y)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((y - z) == (x + y)), ((x + z) == 0)) ||
 rewrite(((y - z) == (y + x)), ((x + z) == 0)))))) ||
((a.node_type() == IRNodeType::Mul) && (
 rewrite(((x*x) == c0), false, (c0 <= -1)) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite(((y*2) == (x + y)), (x == y)) ||
 rewrite(((y*2) == (y + x)), (x == y)))))) ||
((a.node_type() == IRNodeType::Div) && (
 rewrite(((x/c0) == x), (x == 0), (c0 <= -1)))) ||
((a.node_type() == IRNodeType::Min) && (
 rewrite((min(z, min(y, x)) == x), (x <= min(y, z))) ||
 rewrite((min(z, min(y, x)) == y), (y <= min(z, x))) ||
 rewrite((min(min(y, x), z) == x), (x <= min(y, z))) ||
 rewrite((min(min(y, x), z) == y), (y <= min(x, z))) ||
((b.node_type() == IRNodeType::Max) && (
 rewrite((min(y, x) == max(x, y)), (x == y)) ||
 rewrite((min(y, x) == max(y, x)), (y == x)))))) ||
((a.node_type() == IRNodeType::Max) && (
 rewrite((max(z, max(y, x)) == x), (max(z, y) <= x)) ||
 rewrite((max(z, max(y, x)) == y), (max(z, x) <= y)) ||
 rewrite((max(max(y, x), z) == x), (max(y, z) <= x)) ||
 rewrite((max(max(y, x), z) == y), (max(x, z) <= y)) ||
((b.node_type() == IRNodeType::Min) && (
 rewrite((max(y, x) == min(x, y)), (y == x)) ||
 rewrite((max(y, x) == min(y, x)), (y == x)))))) ||
((a.node_type() == IRNodeType::Select) && (
((b.node_type() == IRNodeType::IntImm) && (
 rewrite((select(x, y, (y - z)) == 0), (select(x, 0, z) == y)) ||
 rewrite((select(x, y, (z - y)) == 0), (y == select(x, 0, z))) ||
 rewrite((select(x, (y - z), y) == 0), (y == select(x, z, 0))) ||
 rewrite((select(x, (y - z), z) == 0), (select(x, y, 0) == z)) ||
 rewrite((select(x, (y - z), (w - y)) == 0), (y == select(x, z, w))) ||
 rewrite((select(x, (y - z), (z - w)) == 0), (select(x, y, w) == z)))) ||
 rewrite((select(x, 0, (y - z)) == y), (select(x, y, z) == 0)) ||
 rewrite((select(x, y, (z*c0)) == z), (z == select(x, y, 0)), ((2 <= c0) || (c0 <= -1))) ||
 rewrite((select(x, (y - z), 0) == y), (select(x, z, y) == 0)) ||
 rewrite((select(x, (y*c0), z) == y), (y == select(x, 0, z)), ((2 <= c0) || (c0 <= -1))) ||
 rewrite((select(y, z, (x + z)) == x), (select(y, x, 0) == z)) ||
 rewrite((select(y, z, (z + x)) == x), (z == select(y, x, 0))) ||
 rewrite((select(y, z, (x - z)) == x), (select(y, x, 0) == z)) ||
 rewrite((select(y, (x + z), z) == x), (select(y, 0, x) == z)) ||
 rewrite((select(y, (z + x), z) == x), (select(y, 0, x) == z)) ||
 rewrite((select(y, (x - z), z) == x), (z == select(y, 0, x))) ||
((b.node_type() == IRNodeType::Add) && (
 rewrite((select(z, y, x) == (x + y)), (select(z, x, y) == 0)) ||
 rewrite((select(z, y, x) == (y + x)), (select(z, x, y) == 0))))))