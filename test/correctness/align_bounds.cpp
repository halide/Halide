#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class CheckForSelects : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Select *op) override {
        result = true;
    }

public:
    bool result = false;
};

int trace_min, trace_extent;
int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (e->event == 2) {
        trace_min = e->coordinates[0];
        trace_extent = e->coordinates[1];
    }
    return 0;
}

int main(int argc, char **argv) {
    // Force the bounds of an intermediate pipeline stage to be even to remove a select
    {
        Func f, g, h;
        Var x;

        f(x) = 3;

        g(x) = select(x % 2 == 0, f(x + 1), f(x - 1) + 8);

        Param<int> p;
        h(x) = g(x - p) + g(x + p);

        f.compute_root();
        g.compute_root().align_bounds(x, 2).unroll(x, 2).trace_realizations();

        // The lowered IR should contain no selects.
        Module m = g.compile_to_module({p});
        CheckForSelects checker;
        m.functions()[0].body.accept(&checker);
        if (checker.result) {
            printf("Lowered code contained a select\n");
            return 1;
        }

        p.set(3);
        h.jit_handlers().custom_trace = my_trace;
        Buffer<int> result = h.realize({10});

        for (int i = 0; i < 10; i++) {
            int correct = (i & 1) == 1 ? 6 : 22;
            if (result(i) != correct) {
                printf("result(%d) = %d instead of %d\n",
                       i, result(i), correct);
                return 1;
            }
        }

        // Bounds of f should be [-p, 10+2*p] rounded outwards
        if (trace_min != -4 || trace_extent != 18) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }

        // Increasing p by one should have no effect
        p.set(4);
        assert(result.data());
        h.realize(result);
        if (trace_min != -4 || trace_extent != 18) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }

        // But increasing it again should cause a jump of two in the bounds computed.
        assert(result.data());
        p.set(5);
        h.realize(result);
        if (trace_min != -6 || trace_extent != 22) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }
    }

    // Now try a case where we misalign with an offset (i.e. force the
    // bounds to be odd). This should also remove the select.
    {
        Func f, g, h;
        Var x;

        f(x) = 3;

        g(x) = select(x % 2 == 0, f(x + 1), f(x - 1) + 8);

        Param<int> p;
        h(x) = g(x - p) + g(x + p);

        f.compute_root();
        g.compute_root().align_bounds(x, 2, 1).unroll(x, 2).trace_realizations();

        // The lowered IR should contain no selects.
        Module m = g.compile_to_module({p});
        CheckForSelects checker;
        m.functions()[0].body.accept(&checker);
        if (checker.result) {
            printf("Lowered code contained a select\n");
            return 1;
        }

        p.set(3);
        h.jit_handlers().custom_trace = my_trace;
        Buffer<int> result = h.realize({10});

        for (int i = 0; i < 10; i++) {
            int correct = (i & 1) == 1 ? 6 : 22;
            if (result(i) != correct) {
                printf("result(%d) = %d instead of %d\n",
                       i, result(i), correct);
                return 1;
            }
        }

        // Now the min/max should stick to odd numbers
        if (trace_min != -3 || trace_extent != 16) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }

        // Increasing p by one should have no effect
        p.set(4);
        h.realize(result);
        if (trace_min != -5 || trace_extent != 20) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }

        // But increasing it again should cause a jump of two in the bounds computed.
        p.set(5);
        h.realize(result);
        if (trace_min != -5 || trace_extent != 20) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }
    }

    // Now try a case where we align the extent but not the min.
    {
        Func f, g, h;
        Var x;

        f(x) = 3;

        g(x) = select(x % 2 == 0, f(x + 1), f(x - 1) + 8);

        Param<int> p;
        h(x) = g(x - p) + g(x + p);

        f.compute_root();
        g.compute_root().align_extent(x, 32).trace_realizations();

        p.set(3);
        h.jit_handlers().custom_trace = my_trace;
        Buffer<int> result = h.realize({10});

        for (int i = 0; i < 10; i++) {
            int correct = (i & 1) == 1 ? 6 : 22;
            if (result(i) != correct) {
                printf("result(%d) = %d instead of %d\n",
                       i, result(i), correct);
                return 1;
            }
        }

        // Now the min/max should stick to odd numbers
        if (trace_min != -3 || trace_extent != 32) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }

        // Increasing p by one should have no effect
        p.set(4);
        h.realize(result);
        if (trace_min != -4 || trace_extent != 32) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }

        // But increasing it again should cause a jump of two in the bounds computed.
        p.set(5);
        h.realize(result);
        if (trace_min != -5 || trace_extent != 32) {
            printf("%d: Wrong bounds: [%d, %d]\n", __LINE__, trace_min, trace_extent);
            return 1;
        }
    }

    // Try a case where aligning a buffer means that strided loads can
    // do dense aligned loads and then shuffle. This used to trigger a
    // bug in codegen.
    {
        Func f, g;
        Var x;

        f(x) = x;

        // Do strided loads of every possible alignment
        Expr e = 0;
        for (int i = -32; i <= 32; i++) {
            e += f(3 * x + i);
        }
        g(x) = e;

        f.compute_root();
        g.bound(x, 0, 1024).vectorize(x, 16, TailStrategy::RoundUp);

        // Just check if it crashes
        g.realize({1024});
    }

    printf("Success!\n");
    return 0;
}
