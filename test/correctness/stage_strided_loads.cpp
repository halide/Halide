#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class CheckForStridedLoads : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Load *op) override {
        if (const Ramp *r = op->index.as<Ramp>()) {
            if (op->name == buf_name) {
                bool dense = is_const_one(r->stride);
                found |= !dense;
                dense_loads += dense;
            }
        }
        return IRMutator::visit(op);
    }

public:
    bool found = false;
    int dense_loads = 0;
    std::string buf_name;

    void check(Func f, int desired_dense_loads, std::string name = "buf") {
        found = false;
        dense_loads = 0;
        buf_name = name;
        f.add_custom_lowering_pass(this, nullptr);
        f.compile_jit();
        assert(!found);
        assert(dense_loads == desired_dense_loads);
    }

    void check_not(Func f, int desired_dense_loads, std::string name = "buf") {
        found = false;
        dense_loads = 0;
        buf_name = name;
        f.add_custom_lowering_pass(this, nullptr);
        f.compile_jit();
        assert(found);
        assert(dense_loads == desired_dense_loads);
    }
} checker;

int main(int argc, char **argv) {
    ImageParam buf(Float(32), 1, "buf");

    {
        // Clusters of strided loads can share the same underlying dense load
        Func f;
        Var x;
        f(x) = buf(2 * x) + buf(2 * x + 1);
        f.vectorize(x, 8, TailStrategy::RoundUp);

        // We expect no strided loads, and one dense load
        checker.check(f, 1);
    }

    {
        Func f;
        Var x;
        f(x) = buf(2 * x) + buf(2 * x + 1) + buf(2 * x + 2);
        f.vectorize(x, 8, TailStrategy::RoundUp);

        // We expect two dense loads in this case. One for the first two taps,
        // and one for the last tap.
        checker.check(f, 2);
    }

    {
        // Check four taps
        Func f;
        Var x;
        f(x) = (buf(2 * x) + buf(2 * x + 2)) + (buf(2 * x + 1) + buf(2 * x + 3));
        f.vectorize(x, 8, TailStrategy::RoundUp);

        checker.check(f, 2);
    }

    {
        // Check tuples
        Func f;
        Var x;
        f(x) = {0.f, 0.f};
        f(x) += {buf(2 * x), buf(2 * x + 1)};
        f.update().vectorize(x, 8, TailStrategy::RoundUp);

        // In this case, the dense load appears twice across the two store
        // statements for the two tuple components, but it will get deduped by
        // llvm.
        checker.check(f, 2);
    }

    {
        // Far apart constant offsets is still enough evidence that it's safe to
        // do a dense load.
        Func f;
        Var x;
        f(x) = buf(2 * x - 123) + buf(2 * x + 134);
        f.vectorize(x, 8, TailStrategy::RoundUp);

        checker.check(f, 2);
    }

    {
        // Load partners can be split across multiple Funcs in the same block
        Func f, g;
        Var x;
        f(x) = buf(2 * x);
        g(x) = f(x) + buf(2 * x + 1);

        g.vectorize(x, 8, TailStrategy::RoundUp);
        f.compute_at(g, x).vectorize(x);

        checker.check(g, 2);
    }

    {
        // Load partners can be split across update definitions
        Func f, g;
        Var x;
        f(x) = buf(2 * x);
        f(x) += buf(2 * x + 1);
        g(x) = f(x);
        g.vectorize(x, 8, TailStrategy::RoundUp);

        checker.check(g, 2);
    }

    {
        // Load clusters can be split across an unrolled variable
        Func f, g;
        Var x, c;
        f(x, c) = buf(4 * x + c) + 4 * x;
        f.vectorize(x, 8, TailStrategy::RoundUp).bound(c, 0, 4).unroll(c).reorder(c, x);

        checker.check(f, 4);
    }

    {
        // Load clusters can even be split across unrelated inner loop nests
        // (provided they are known to have non-zero extent).

        Func f, g, h;
        Var c, x, y;
        g(x, y) = buf(2 * x) + y;
        h(x, y) = buf(2 * x + 1) + y;
        f(x, y, c) = g(x, y) + h(x, y) + c;

        Var xi, yi;
        f.tile(x, y, xi, yi, 8, 8, TailStrategy::RoundUp).vectorize(xi).reorder(c, x, y);
        g.compute_at(f, x).vectorize(x);
        h.compute_at(f, x).vectorize(x);
        checker.check(f, 2);
    }

    // We can always densify strided loads to internal allocations, because we
    // can just pad the allocation.
    {
        Func f, g;
        Var x;

        f(x) = x;
        g(x) = f(2 * x);
        f.compute_at(g, x).vectorize(x);
        g.vectorize(x, 8, TailStrategy::RoundUp);
        checker.check(g, 1, f.name());
    }

    // Strides up to the the vector size are worth densifying. After that, it's better to just gather.
    {
        Func f;
        Var x;
        f(x) = buf(15 * x) + buf(15 * x + 14);
        f.vectorize(x, 16, TailStrategy::RoundUp);

        checker.check(f, 1);
    }

    {
        Func f;
        Var x;
        f(x) = buf(16 * x) + buf(16 * x + 15);
        f.vectorize(x, 16, TailStrategy::RoundUp);

        checker.check_not(f, 0);
    }

    // Strided loads to external allocations are handled by doing a weird-sized
    // dense load and then shuffling.
    {
        Func f;
        Var x;
        f(x) = buf(3 * x);
        f.vectorize(x, 8, TailStrategy::RoundUp);
        checker.check(f, 1);
    }

    // Ensure memoized allocations have appropriate padding to support overreads too

    // Test out doing a transpose this way

    // Make a pair of unconditionally-executed loads, and check that a
    // conditionally-executed load can share with it.
    {
        Func f;
        Var x;
        f(x) = buf(2 * x) + buf(2 * x + 1);
        RDom r(0, 1);
        Param<bool> p;
        r.where(p);
        f(x) += buf(2 * x + 3) + r;

        Func g;
        g(x) = f(x);
        g.vectorize(x, 8, TailStrategy::RoundUp);
        f.compute_at(g, x).vectorize(x).update().vectorize(x);
        g.compile_to_assembly("/dev/stdout", {buf, p}, Target{"host-no_asserts-no_runtime-no_bounds_query"});
    }

    return 0;
}

/* This branch (321)


 */

/*
main: 523
        add     x12, x8, w11, sxtw #2
        subs    x10, x10, #1
        add     x13, x12, #24
        mov     x14, x12
        add     w11, w11, #8
        ldur    q0, [x12, #8]
        mov     v0.s[1], v0.s[2]
        ld1     { v0.s }[2], [x13]
        ld2     { v1.4s, v2.4s }, [x14], #32
        ld2     { v1.4s, v2.4s }, [x12]
        ld1     { v0.s }[3], [x14]
        fadd    v1.4s, v1.4s, v2.4s
        fadd    v0.4s, v0.4s, v1.4s
        str     q0, [x9], #16

*/

/*
  my change with shuffle hoisting (245):
        ldp     q2, q3, [x10, #-16]
        subs    x9, x9, #1
        ldur    q0, [x10, #-12]
        ldur    q1, [x10, #4]
        add     x10, x10, #32
        fadd    v2.4s, v0.4s, v2.4s
        fadd    v3.4s, v1.4s, v3.4s
        uzp2    v0.4s, v0.4s, v1.4s
        uzp1    v1.4s, v2.4s, v3.4s
        fadd    v0.4s, v0.4s, v1.4s
        str     q0, [x8], #16
*/

/*
my change with shuffle hoisting disabled (320)
        sub     x11, x10, #12
        subs    x9, x9, #1
        ldp     q0, q1, [x10, #-16]
        add     x10, x10, #32
        ld2     { v2.4s, v3.4s }, [x11]
        uzp1    v0.4s, v0.4s, v1.4s
        fadd    v0.4s, v2.4s, v0.4s
        fadd    v0.4s, v3.4s, v0.4s
        str     q0, [x8], #16
*/
