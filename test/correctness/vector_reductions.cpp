#include "Halide.h"
#include "test_sharding.h"

using namespace Halide;

namespace {

struct Task {
    Target target;
    std::function<void()> fn;
};

void add_tasks(const Target &target, std::vector<Task> &tasks) {
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
                            // Test cases 4 and 5 in the switch
                            // statement below require a Bool rhs.
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

                        const auto fn = [=]() {
                            // Useful for debugging; leave in (commented out)
                            // std::cout << "Testing: "
                            //           << " target: " << target
                            //           << " dst_lanes: " << dst_lanes
                            //           << " reduce_factor " << reduce_factor
                            //           << " src_type " << src_type
                            //           << " widen_factor " << widen_factor
                            //           << " dst_type " << dst_type
                            //           << " op " << op
                            //           << "\n";

                            RDom c(0, 128);

                            // Func.evaluate() doesn't let you specify a Target (!),
                            // so let's use Func.realize() instead.
                            Func err("err");
                            err() = cast<double>(maximum(absd(f(c), ref(c))));
                            Buffer<double, 0> err_im = err.realize({}, target);
                            double e = err_im();

                            if (e > 1e-3) {
                                std::cerr
                                    << "Horizontal reduction produced different output when vectorized!\n"
                                    << "Maximum error = " << e << "\n"
                                    << "Reducing from " << src_type.with_lanes(src_lanes)
                                    << " to " << dst_type.with_lanes(dst_lanes) << "\n"
                                    << "RHS: " << f.update_value() << "\n";
                                exit(1);
                            }
                        };
                        tasks.push_back({target, fn});
                    }
                }
            }
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    std::vector<Task> tasks;
    add_tasks(target, tasks);

    if (target.arch == Target::X86) {
        // LLVM has had SIMD codegen errors that we missed because we didn't test against
        // multiple SIMD architectures, using just 'host' instead. To remedy this, we'll
        // re-run this multiple times, downgrading the SIMD successively, to ensure we get
        // test coverage. Note that this doesn't attempt to be exhaustive -- there are too
        // many permutations to really test, especially with AVX512 -- but this way we
        // can get at least baseline coverage for the major variants.
        //
        // (Note also that our codegen for x86 implicitly 'fills in' required prerequisites,
        // e.g. if you specify a target with AVX2, the codegen will automatically include
        // AVX and SSE41 as well.)
        if (target.has_feature(Target::AVX512)) {
            Target avx2_target(target.os, target.arch, target.bits, {Target::AVX2});
            add_tasks(avx2_target, tasks);
        }
        if (target.has_feature(Target::AVX2)) {
            Target sse41_target(target.os, target.arch, target.bits, {Target::AVX});
            add_tasks(sse41_target, tasks);
        }
        if (target.has_feature(Target::AVX)) {
            Target sse41_target(target.os, target.arch, target.bits, {Target::SSE41});
            add_tasks(sse41_target, tasks);
        }
        if (target.has_feature(Target::SSE41)) {
            // Halide assumes that all x86 targets have at least sse2
            Target sse2_target(target.os, target.arch, target.bits);
            add_tasks(sse2_target, tasks);
        }
    }

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    Target prev_target;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        if (task.target != prev_target) {
            std::cout << "vector_reductions: Testing with " << task.target << "\n";
            prev_target = task.target;
        }
        task.fn();
    }

    std::cout << "Success!\n";
    return 0;
}
