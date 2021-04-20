#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    const int dst_lanes = 1;
    const int reduce_factor = 3;
    const int src_lanes = dst_lanes * reduce_factor;
    const Type src_type = UInt(8);
    const int widen_factor = 1;
    const Type dst_type = src_type.with_bits(src_type.bits() * widen_factor);

    Var x, xo, xi;
    RDom r(0, reduce_factor);
    RVar rx;
    Func in;
    in(x) = cast(src_type, random_int());
    in.compute_root();

    Expr rhs = cast(dst_type, in(x * reduce_factor + r));
    Expr rhs2 = cast(dst_type, in(x * reduce_factor + r + 32));

    Func f, ref("ref");
    f(x) *= rhs;
    ref(x) *= rhs;

    f.compute_root()
        .update()
        .split(x, xo, xi, dst_lanes)
        .fuse(r, xi, rx)
        .atomic()
        .vectorize(rx);
    ref.compute_root();

    RDom c(0, 128);
    Expr err = cast<double>(maximum(absd(f(c), ref(c))));

    double e = evaluate<double>(err);

    if (e > 1e-3) {
        std::cerr
            << "Horizontal reduction produced different output when vectorized!\n"
            << "Maximum error = " << e << "\n"
            << "Reducing from " << src_type.with_lanes(src_lanes)
            << " to " << dst_type.with_lanes(dst_lanes) << "\n"
            << "RHS: " << f.update_value() << "\n";
        exit(-1);
    }

    printf("Success!\n");
    return 0;
}
