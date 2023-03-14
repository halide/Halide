#include "Halide.h"
#include <stdio.h>

namespace {

using namespace Halide;
using namespace Halide::Internal;
using std::string;

// Count the number of stores to a given func, and the number of calls to sin
class Counter : public IRVisitor {
    string func;

    using IRVisitor::visit;

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        if (op->name == func) {
            store_count++;
        }
    }

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->name == "sin_f32") {
            sin_count++;
        }
    }

public:
    int store_count, sin_count;
    Counter(string f)
        : func(f), store_count(0), sin_count(0) {
    }
};

// Check that the number of calls to sin is correct.
class CheckSinCount : public IRMutator {
    int correct;

public:
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        Counter c("");
        s.accept(&c);
        if (c.sin_count != correct) {
            printf("There were %d sin calls instead of %d\n", c.sin_count, correct);
            exit(1);
        }
        return s;
    }

    CheckSinCount(int c)
        : correct(c) {
    }
};

// Check that the number of stores to a given func is correct
class CheckStoreCount : public IRMutator {
    string func;
    int correct;

public:
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        Counter c(func);
        s.accept(&c);
        if (c.store_count != correct) {
            printf("There were %d stores to %s instead of %d\n", c.store_count, func.c_str(), correct);
            exit(1);
        }
        return s;
    }

    CheckStoreCount(string f, int c)
        : func(f), correct(c) {
    }
};

void count_partitions(Func g, int correct) {
    g.add_custom_lowering_pass(new CheckStoreCount(g.name(), correct));
    g.compile_to_module(g.infer_arguments());
}

void count_sin_calls(Func g, int correct) {
    g.add_custom_lowering_pass(new CheckSinCount(correct));
    g.compile_to_module(g.infer_arguments());
}

}  // namespace

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x;
    f.compute_root();

    // Halide will partition a loop into three pieces in a few
    // situations. The pieces are 1) a messy prologue, 2) a clean
    // steady state, and 3) a messy epilogue. One way to trigger this
    // is if you use a boundary condition helper:

    {
        Func g = BoundaryConditions::repeat_edge(f, {{0, 100}});
        count_partitions(g, 3);
    }

    // If you vectorize or otherwise split, then the last vector
    // (which gets shifted leftwards) is its own partition. This
    // removes some clamping logic from the inner loop.

    {
        Func g;
        g(x) = f(x);
        g.vectorize(x, 8);
        count_partitions(g, 2);
    }

    // The slicing applies to every loop level starting from the
    // outermost one, but only recursively simplifies the clean steady
    // state. It either splits things three (start, middle, end). So
    // adding a boundary condition to a 2D computation will produce 5
    // code paths for the top, bottom, left, right, and center of the
    // image.
    {
        Var y;
        Func g;
        g(x, y) = x + y;
        g.compute_root();
        Func h = BoundaryConditions::mirror_image(g, {{0, 10}, {0, 10}});
        count_partitions(h, 5);
    }

    // If you split and also have a boundary condition, or have
    // multiple boundary conditions at play (e.g. because you're
    // blurring an inlined Func that uses a boundary condition), then
    // there are still only three partitions. The steady state is the
    // slice of the loop where *all* of the boundary conditions and
    // splitting logic simplify away.
    {
        Func g = BoundaryConditions::mirror_interior(f, {{0, 10}});
        Func h;
        Param<int> t1, t2;
        h(x) = g(x - 1) + g(x + 1);
        h.vectorize(x, 8);
        count_partitions(h, 3);
    }

    // You can manually control the splitting behavior using the
    // 'likely' intrinsic. When used on one side of a select, min,
    // max, or clamp, it tags the select, min, max, or clamp as likely
    // to simplify to that expression in the steady state case, and
    // tries to solve for loop variable values for which this is true.
    {
        // So this code should produce a prologue that evaluates to sin(x), and
        // a steady state that evaluates to 1:
        Func g;
        g(x) = select(x < 10, sin(x), likely(1.0f));
        // There should be two partitions
        count_partitions(g, 2);
        // But only one should call sin
        count_sin_calls(g, 1);
    }

    {
        // This code should produce a prologue and epilogue that
        // evaluate sin(x), and a steady state that evaluates to 1:
        Func g;
        g(x) = select(x < 10 || x > 100, sin(x), likely(1.0f));
        // There should be three partitions
        count_partitions(g, 3);
        // With calls to sin in the prologue and epilogue.
        count_sin_calls(g, 2);
    }

    // As a specialize case, we treat clamped ramps as likely to
    // simplify to the clamped expression. This handles the many
    // existing cases where people have written their boundary
    // condition manually using clamp.
    {
        Func g;
        g(x) = f(clamp(x, 0, 10));  // treated as clamp(likely(x), 0, 10)
        g.vectorize(x, 8);
        count_partitions(g, 3);
    }

    // Using the likely intrinsic pulls some IR relating to the
    // condition outside of the loop. We'd better check that this
    // respects lets and doesn't do any combinatorial expansion. We'll
    // do this with a nasty comparison:
    {
        Func g;
        Var y;

        // Have an inner reduction loop that the comparisons depend on
        // to make things harder.
        RDom r(0, 5);

        const int N = 25;

        // Make some nasty expressions to compare to.
        Expr e[N];
        e[0] = y;
        for (int i = 1; i < N; i++) {
            e[i] = e[i - 1] * e[i - 1] + y + r;
        }
        // Make a nasty condition that uses all of these.
        Expr nasty = cast<bool>(1);
        for (int i = 0; i < N; i++) {
            nasty = nasty && (x * (i + 1) < e[i]);
        }

        // Have an innermost loop over c to complicate things further.
        Var c;
        g(c, x, y) = sum(select(nasty, likely(10), c + r));

        // Check that it doesn't take the age of the world to compile,
        // and that it produces the right number of partitions.
        count_partitions(g, 3);
    }

    // Make sure partitions that occur outside of the actual bounds
    // don't mess things up.
    {
        Func g;
        Var x;
        Param<int> limit;
        g(x) = select(x > limit, likely(3), 2);

        // If either of these realize calls iterates from 0 to limit,
        // and then from limit to 10, we'll have a nice segfault.
        limit.set(10000000);
        Buffer<int> result = g.realize({10});

        limit.set(-10000000);
        result = g.realize({10});
    }

    // The performance of this behavior is tested in
    // test/performance/boundary_conditions.cpp

    printf("Success!\n");
    return 0;
}
