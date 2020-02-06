#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    for (int dst_lanes : {1, 2, 3, 4, 8}) {
        for (int reduce_factor : {2, 3, 4, 8, 16}) {
            std::vector<Type> types
            {UInt(8), Int(8), UInt(16), Int(16), UInt(32), Int(32),
                    UInt(64), Int(64), Float(16), Float(32), Float(64)};
            const int src_lanes = dst_lanes * reduce_factor;
            for (Type src_type : types) {
                for (int widen_factor : {1, 2, 4}) {
                    Type dst_type = src_type.with_bits(src_type.bits() * widen_factor);
                    if (std::find(types.begin(), types.end(), dst_type) == types.end()) {
                        continue;
                    }

                    Var x, xo, xi;
                    RDom r(0, reduce_factor);
                    RVar rx;
                    {
                        Func in;
                        in(x) = cast(src_type, x);
                        in.compute_root();

                        Expr rhs = cast(dst_type, in(x * reduce_factor + r));

                        Func f, ref;
                        f(x) += rhs;
                        ref(x) += rhs;

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

                        if (e > 1e-10) {
                            std::cerr
                                << "Horizontal reduction produced difference output when vectorized!\n"
                                << "Maximum error = " << e << "\n"
                                << "Reducing from " << src_type.with_lanes(src_lanes)
                                << " to " << dst_type.with_lanes(dst_lanes) << "\n";
                            exit(-1);
                        }
                    }

                }
            }
        }
    }

    return 0;

    // TO test: min/max/add/dot/mul for lots of src/dst type pairs

    // Fused combinations of x and a reduction at various combos

    Func f, g, c;
    Var x, y;

    c(x) = cast<int16_t>(x);
    f(y, x) = cast<int16_t>(x);
    f.compute_root();
    c.compute_root();

    RDom r(0, 16);

    g(x) += cast<int32_t>(f(r, x)) * c(r);

    Var xo, xi;
    RVar rx;
    g.bound(x, 0, 128).update().atomic().split(x, xo, xi, 1).fuse(r, xi, rx).vectorize(rx);

    //g.compile_to_assembly("/dev/stdout", {}, Target("arm-64-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_opt"));
    //g.compile_to_assembly("/dev/stdout", {}, Target("arm-32-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_opt"));
    g.compile_to_assembly("/dev/stdout", {}, Target("host-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_opt"));

    return 0;
}
