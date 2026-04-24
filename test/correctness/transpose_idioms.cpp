#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// This test enumerates all the scheduling idioms in Halide that *should*
// produce good code for a transpose/interleave/deinterleave operation.

class Checker : public IRMutator {

    using IRMutator::visit;

    Expr visit(const Load *op) override {
        if (const Ramp *r = op->index.as<Ramp>();
            r && is_const_one(r->stride)) {
            dense_loads++;
        } else if (op->type.is_vector()) {
            gathers++;
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        if (const Ramp *r = op->index.as<Ramp>();
            r && is_const_one(r->stride)) {
            dense_stores++;
        } else if (op->index.type().is_vector()) {
            scatters++;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Shuffle *op) override {
        transposes += op->is_transpose();
        interleaves += op->is_interleave();
        if (op->is_slice()) {
            if (op->slice_stride() == 1) {
                dense_slices++;
            } else {
                strided_slices++;
            }
        }
        return IRMutator::visit(op);
    }

public:
    int dense_loads = 0;
    int gathers = 0;
    int dense_stores = 0;
    int scatters = 0;
    int dense_slices = 0;
    int strided_slices = 0;
    int interleaves = 0;
    int transposes = 0;

    void check() {
        internal_assert(gathers == 0) << "Vector gathers found";
        internal_assert(scatters == 0) << "Vector scatters found";
        internal_assert(strided_slices == 0) << "strided slices found";
        internal_assert(dense_loads) << "No dense loads found";
        internal_assert(dense_stores) << "No dense stores found";
        internal_assert(interleaves + transposes) << "No interleaves or transposes found";
    }
};

void check(Func g) {
    Checker checker;
    g.add_custom_lowering_pass(&checker, nullptr);

    // Choose a shape with lots of factors so that our RoundUp schedules work
    int n = 16 * 9 * 7;
    Buffer<int> out = g.realize({n, n});
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            int correct = 100 * x + y;
            internal_assert(out(x, y) == correct)
                << "out(" << x << ", " << y << ") = " << out(x, y)
                << " instead of " << correct << "\n";
        }
    }

    checker.check();
}

int main(int argc, char **argv) {
    if (Internal::get_llvm_version() < 220 &&
        get_jit_target_from_environment().has_feature(Target::SVE2)) {
        printf("[SKIP] LLVM %d has known SVE backend bugs for this test.\n",
               Internal::get_llvm_version());
        return 0;
    }

    Var x{"x"}, y{"y"}, xi{"xi"}, yi{"yi"};

    // In each case we'll say g(x, y) = f(y, x) and tile it. We will try power
    // of two sizes, and sizes that are coprime, and sizes that are neither
    // coprime no powers of two. We'll use sizes larger than 4, because some
    // backends like to do different things for small strides.

    for (auto tile : {std::pair{8, 16}, {7, 5}, {6, 9}}) {
        {
            // Idiom 1: Strided stores into a staged transposed copy of the
            // input. The strided stores that get mashed together into one big
            // interleave + store by the pass that interleaves strided
            // stores. This has to be done on a staged copy of the input rather
            // than g so that the strided stores have a constant stride.
            Func f{"f"}, g{"g"};
            f(x, y) = x + 100 * y;
            g(x, y) = f(y, x);
            f.compute_root();

            g.tile(x, y, xi, yi, tile.first, tile.second, TailStrategy::RoundUp)
                .vectorize(xi)
                .unroll(yi);

            f.in().compute_at(g, x).reorder_storage(y, x).vectorize(x).unroll(y);

            check(g);
        }

        {
            // Idiom 2: Vectorize x, unroll y. Stage a copy of the input but
            // don't transpose it. This will create strided loads from the
            // staged input that get hoisted out into one big dense load +
            // transpose by the stage_strided_stores pass. The staging is
            // required so that the strides are constant.
            Func f{"f"}, g{"g"};
            f(x, y) = x + 100 * y;
            g(x, y) = f(y, x);
            f.compute_root();

            g.tile(x, y, xi, yi, tile.first, tile.second, TailStrategy::RoundUp)
                .vectorize(xi)
                .unroll(yi);

            f.in().compute_at(g, x).vectorize(x).unroll(y);

            check(g);
        }

        {
            // Idiom 3: Vectorize both, x innermost. This should be handled by
            // shuffle optimization logic in the simplifier: a store of a concat
            // of ramps turns into a sequence of stores of slices of the RHS,
            // and a load of a ramp of a ramp where the *outer* ramp has stride
            // 1 but the inner doesn't turns into a transpose of a concat of
            // dense loads.
            Func f{"f"}, g{"g"};
            f(x, y) = x + 100 * y;
            g(x, y) = f(y, x);
            f.compute_root();

            g.tile(x, y, xi, yi, tile.first, tile.second, TailStrategy::RoundUp)
                .vectorize(xi)
                .vectorize(yi);

            check(g);
        }

        {
            // Idiom 4: Vectorize both, y innermost. In this case the store of a
            // ramp of a ramp gets rewritten by the simplifier to move the ramp
            // with stride one innermost, transposing the RHS.

            Func f{"f"}, g{"g"};
            f(x, y) = x + 100 * y;
            g(x, y) = f(y, x);
            f.compute_root();

            g.tile(x, y, xi, yi, tile.first, tile.second, TailStrategy::RoundUp)
                .reorder(yi, xi)
                .vectorize(xi)
                .vectorize(yi);

            check(g);
        }
    }

    {
        // Check the double-vectorization approaches also work when there is a
        // vector predicate on one of the two vectors, to be sure the simplifier
        // is transforming the predicate correctly. We can't predicate both,
        // because the vectorizer can't handle it and generates a scalar tail.

        {
            // LLVM 22 has a codegen bug for some x86 versions here, so skip with AVX512
            // See: https://github.com/llvm/llvm-project/issues/191304
            if (Internal::get_llvm_version() >= 220 &&
                Internal::get_llvm_version() < 230 &&
                get_jit_target_from_environment().has_feature(Target::AVX512)) {
                printf("Skipping one subtest for LLVM %d with AVX-512 due to known backend bugs.\n",
                       Internal::get_llvm_version());
            } else {
                Func f{"f"}, g{"g"};
                f(x, y) = x + 100 * y;
                g(x, y) = f(y, x);
                f.compute_root();

                g
                    .never_partition(x, y)
                    .split(x, x, xi, 13, TailStrategy::Predicate)
                    .split(y, y, yi, 11, TailStrategy::ShiftInwards)
                    .reorder(xi, yi, x, y)
                    .vectorize(xi)
                    .vectorize(yi);

                check(g);
            }
        }
        {
            Func f{"f"}, g{"g"};
            f(x, y) = x + 100 * y;
            g(x, y) = f(y, x);
            f.compute_root();

            g
                .never_partition(x, y)
                .split(x, x, xi, 13, TailStrategy::ShiftInwards)
                .split(y, y, yi, 11, TailStrategy::Predicate)
                .reorder(yi, xi, x, y)
                .vectorize(xi)
                .vectorize(yi);

            check(g);
        }
    }

    printf("Success!\n");
}
