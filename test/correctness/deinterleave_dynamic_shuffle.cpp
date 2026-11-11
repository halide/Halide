#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    // A dynamic_shuffle's lookup table has its own lane count, unrelated to
    // the result's. Deinterleaving must only touch the indices.
    std::vector<Expr> entries;
    for (int i = 0; i < 16; i++) {
        entries.push_back(cast<int8_t>(i * 7 - 50));
    }
    Expr table = Shuffle::make_concat(entries);
    Expr idx = Variable::make(UInt(8, 32), "idx");
    Expr e = Call::make(Int(8, 32), Call::dynamic_shuffle, {table, idx, 0, 15}, Call::PureIntrinsic);

    for (int odd = 0; odd < 2; odd++) {
        Expr got = odd ? extract_odd_lanes(e) : extract_even_lanes(e);
        Expr idx_part = odd ? extract_odd_lanes(idx) : extract_even_lanes(idx);
        Expr want = Call::make(Int(8, 16), Call::dynamic_shuffle, {table, idx_part, 0, 15}, Call::PureIntrinsic);
        if (!equal(got, want)) {
            internal_error << got << " != " << want << "\n";
        }
    }

    printf("Success!\n");
    return 0;
}
