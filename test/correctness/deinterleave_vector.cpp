#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {
void check(Expr a, const Expr &even, const Expr &odd) {
    a = simplify(a);
    int lanes = a.type().lanes();
    Expr correct_even = extract_lanes(a, 0, 2, (lanes + 1) / 2);
    Expr correct_odd = extract_lanes(a, 1, 2, lanes / 2);
    if (!equal(correct_even, even)) {
        internal_error << correct_even << " != " << even << "\n";
    }
    if (!equal(correct_odd, odd)) {
        internal_error << correct_odd << " != " << odd << "\n";
    }
}
}  // namespace

int main(int argc, char **argv) {
    std::pair<Expr, Expr> result;
    Expr x = Variable::make(Int(32), "x");
    Expr ramp = Ramp::make(x + 4, 3, 8);
    Expr ramp_a = Ramp::make(x + 4, 6, 4);
    Expr ramp_b = Ramp::make(x + 7, 6, 4);
    Expr broadcast = Broadcast::make(x + 4, 16);
    Expr broadcast_a = Broadcast::make(x + 4, 8);
    const Expr &broadcast_b = broadcast_a;

    check(ramp, ramp_a, ramp_b);
    check(broadcast, broadcast_a, broadcast_b);

    check(Load::make(ramp.type(), "buf", ramp),
          Load::make(ramp_a.type(), "buf", ramp_a),
          Load::make(ramp_b.type(), "buf", ramp_b));

    Expr vec_x = Variable::make(Int(32, 4), "vec_x");
    Expr vec_y = Variable::make(Int(32, 4), "vec_y");
    check(Shuffle::make({vec_x, vec_y}, {0, 4, 2, 6, 4, 2, 3, 7, 1, 2, 3, 4}),
          Shuffle::make({vec_x, vec_y}, {0, 2, 4, 3, 1, 3}),
          Shuffle::make({vec_x, vec_y}, {4, 6, 2, 7, 2, 4}));

    printf("Success!\n");
    return 0;
}
