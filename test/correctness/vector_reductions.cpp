#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    for (int dst_lanes : {1, 3}) {
        for (int reduce_factor : {2, 3, 4}) {
            std::vector<Type> types =
                {UInt(8), Int(8), UInt(16), Int(16), UInt(32), Int(32),
                 UInt(64), Int(64), Float(16), Float(32), Float(64)};
            const int src_lanes = dst_lanes * reduce_factor;
            for (Type src_type : types) {
                for (int widen_factor : {1, 2, 4}) {
                    Type dst_type = src_type.with_bits(src_type.bits() * widen_factor);
                    if (std::find(types.begin(), types.end(), dst_type) == types.end()) {
                        continue;
                    }

                    for (int op = 0; op < 7; op++) {
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
                        Expr rhs2 = cast(dst_type, in(x * reduce_factor + r + 32));

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
                            // Widening min/max reductions are not interesting
                            if (widen_factor != 1) {
                                continue;
                            }
                            f(x) = rhs.type().min();
                            ref(x) = rhs.type().min();
                            f(x) = max(f(x), rhs);
                            ref(x) = max(f(x), rhs);
                            break;
                        case 3:
                            if (widen_factor != 1) {
                                continue;
                            }
                            f(x) = rhs.type().max();
                            ref(x) = rhs.type().max();
                            f(x) = min(f(x), rhs);
                            ref(x) = min(f(x), rhs);
                            break;
                        case 4:
                            if (widen_factor != 1) {
                                continue;
                            }
                            f(x) = cast<bool>(false);
                            ref(x) = cast<bool>(false);
                            f(x) = f(x) || rhs;
                            ref(x) = f(x) || rhs;
                            break;
                        case 5:
                            if (widen_factor != 1) {
                                continue;
                            }
                            f(x) = cast<bool>(true);
                            ref(x) = cast<bool>(true);
                            f(x) = f(x) && rhs;
                            ref(x) = f(x) && rhs;
                            break;
                        case 6:
                            // Dot product
                            f(x) += rhs * rhs2;
                            ref(x) += rhs * rhs2;
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

    printf("Success!\n");
    return 0;
}
