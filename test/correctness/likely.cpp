#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;
using std::string;

// Count the number of stores to a given func, and the number of calls to sin
class Counter : public IRVisitor {
    string func;

    using IRVisitor::visit;

    void visit(const Store *op) {
        IRVisitor::visit(op);
        if (op->name == func) {
            store_count++;
        }
    }

    void visit(const Call *op) {
        IRVisitor::visit(op);
        if (op->name == "sin_f32") {
            sin_count++;
        }
    }

public:
    int store_count, sin_count;
    Counter(string f) : func(f), store_count(0), sin_count(0) {}
};

// Check that the number of calls to sin is correct.
class CheckSinCount : public IRMutator {
    int correct;
public:
    using IRMutator::mutate;

    Stmt mutate(Stmt s) {
        Counter c("");
        s.accept(&c);
        if (c.sin_count != correct) {
            printf("There were %d sin calls instead of %d\n", c.sin_count, correct);
            exit(-1);
        }
        return s;
    }

    CheckSinCount(int c) : correct(c) {}
};

// Check that the number of stores to a given func is correct
class CheckStoreCount : public IRMutator {
    string func;
    int correct;
public:
    using IRMutator::mutate;

    Stmt mutate(Stmt s) {
        Counter c(func);
        s.accept(&c);
        if (c.store_count != correct) {
            printf("There were %d stores to %s calls instead of %d\n", c.store_count, func.c_str(), correct);
            exit(-1);
        }
        return s;
    }

    CheckStoreCount(string f, int c) : func(f), correct(c) {}
};

void count_partitions(Func g, int correct) {
    g.add_custom_lowering_pass(new CheckStoreCount(g.name(), correct));
    g.compile_jit();
}

void count_sin_calls(Func g, int correct) {
    g.add_custom_lowering_pass(new CheckSinCount(correct));
    g.compile_jit();
}

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
        Func g = BoundaryConditions::repeat_edge(f, 0, 100);
        count_partitions(g, 3);
    }

    // If you vectorize or otherwise split, then the last vector
    // (which gets shifted leftwards) is its own partition. This removes
    // some clamping logic from the inner loop.

    {
        Func g;
        g(x) = f(x);
        g.vectorize(x, 8);
        count_partitions(g, 2);
    }

    // The slicing only applies to a single loop level - the
    // innermost one where it can be applied. So adding a boundary
    // condition to a 2D computation will produce 3 code paths, not 9.
    {
        Var y;
        Func g;
        g(x, y) = x + y;
        g.compute_root();
        Func h = BoundaryConditions::mirror_image(g, 0, 10, 0, 10);
        count_partitions(h, 3);
    }

    // If you split and also have a boundary condition, or have
    // multiple boundary conditions at play (e.g. because you're
    // blurring an inlined Func that uses a boundary condition), then
    // there are still only three partitions. The steady state is the
    // partition where *all* of the boundary conditions and splitting
    // logic simplify away.
    {
        Func g = BoundaryConditions::mirror_interior(f, 0, 10);
        Func h;
        Param<int> t1, t2;
        h(x) = g(x-1) + g(x+1);
        h.vectorize(x, 8);
        count_partitions(h, 3);
    }

    // You can manually control the splitting behavior using the
    // 'likely' intrinsic. When used on one side of a select, min, or
    // max, it tags the select, min, or max as likely to simplify to
    // that expression in the steady state case, and tries to solve
    // for loop variable values for which this is true.
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

    {

        // The max here should simplify to 10 in the steady state,
        // which means the select evaluates to 1.0f
        Func g;
        g(x) = select(min(x, likely(10)) > 0, 1.0f, sin(x));
        // There should be two partitions
        count_partitions(g, 2);
        // With a call to sin in the steady-state
        count_sin_calls(g, 1);
    }

    // As a specialize case, we treat clamped ramps as likely to
    // simplify to the clamped expression. This handles the many
    // existing cases where people have written their boundary
    // condition manually using clamp.
    {
        Func g;
        g(x) = f(clamp(x, 0, 10));
        g.vectorize(x, 8);
        count_partitions(g, 3);
    }

    // The performance of this behavior is tested in
    // test/performance/boundary_conditions.cpp

    printf("Success!\n");
    return 0;
}
