#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    for (int dst_lanes : {1, 2, 3, 4}) {
        for (int reduce_factor : {2, 3, 4, 8}) {
            std::vector<Type> types =
                {UInt(8), Int(8), UInt(16), Int(16), UInt(32), Int(32),
                 UInt(64), Int(64), Float(16), Float(32), Float(64)};
            const int src_lanes = dst_lanes * reduce_factor;
            for (Type src_type : types) {
                //if (src_type == Float(16)) continue;

                for (int widen_factor : {1, 2, 4}) {
                    Type dst_type = src_type.with_bits(src_type.bits() * widen_factor);
                    if (std::find(types.begin(), types.end(), dst_type) == types.end()) {
                        continue;
                    }

                    for (int op = 0; op < 6; op++) {
                        if (dst_type == Float(16) && reduce_factor > 2) {
                            // Reductions of float16s is really not very associative
                            continue;
                        }

                        Var x, xo, xi;
                        RDom r(0, reduce_factor);
                        RVar rx;
                        Func in;
                        if (src_type.is_float()) {
                            in(x) = cast(src_type, random_float());
                        } else {
                            in(x) = cast(src_type, random_int());
                        }
                        in.compute_root();

                        Expr rhs = cast(dst_type, in(x * reduce_factor + r));

                        if (op == 4 || op == 5) {
                            rhs = rhs > cast(rhs.type(), 5);
                        }

                        Func f, ref("ref");
                        switch (op) {
                        case 0:
                            f(x) += rhs;
                            ref(x) += rhs;
                            break;
                        case 1:
                            f(x) *= rhs;
                            ref(x) *= rhs;
                            break;
                        case 2:
                            f(x) = rhs.type().min();
                            ref(x) = rhs.type().min();
                            f(x) = max(f(x), rhs);
                            ref(x) = max(f(x), rhs);
                            break;
                        case 3:
                            f(x) = rhs.type().max();
                            ref(x) = rhs.type().max();
                            f(x) = min(f(x), rhs);
                            ref(x) = min(f(x), rhs);
                            break;
                        case 4:
                            f(x) = cast<bool>(false);
                            ref(x) = cast<bool>(false);
                            f(x) = f(x) || rhs;
                            ref(x) = f(x) || rhs;
                            break;
                        case 5:
                            f(x) = cast<bool>(true);
                            ref(x) = cast<bool>(true);
                            f(x) = f(x) && rhs;
                            ref(x) = f(x) && rhs;
                            break;
                        }

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
