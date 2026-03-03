#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Test for issue #8709: skip_stages is foiled by CSE
// When a function call appears multiple times in a select expression,
// CSE lifts it outside the select as a let variable. This causes
// skip_stages to incorrectly treat it as unconditionally used.

bool called = false;
extern "C" HALIDE_EXPORT_SYMBOL int track_call(int x) {
    called = true;
    return x;
}
HalideExtern_1(int, track_call, int);

int main(int argc, char **argv) {
    Var x;
    Param<bool> p;
    Param<int> p_int;

    // Test case 1: Single use in select
    {
        Func f, g;
        f(x) = track_call(x);
        g(x) = select(p, f(x), 0);

        f.compute_root();
        g.compile_jit();

        // When p is true, f should be called
        called = false;
        p.set(true);
        g.realize({10});
        if (!called) {
            printf("f should have been called when p = true\n");
            return 1;
        }

        // When p is false, f should NOT be called (skip_stages optimization)
        called = false;
        p.set(false);
        g.realize({10});
        if (called) {
            printf("f should NOT have been called when p = false\n");
            return 1;
        }
    }

    // Test case 2: Multiple uses in select (failed in the past due to CSE)
    {
        Func f, g;
        f(x) = track_call(x);
        // f(x) appears multiple times, so CSE will lift it outside the select
        g(x) = select(p, f(x) * f(x) + f(x), 0);

        f.compute_root();
        g.compile_jit();

        // When p is true, f should be called
        called = false;
        p.set(true);
        g.realize({10});
        if (!called) {
            printf("f should have been called when p = true\n");
            return 1;
        }

        // When p is false, f should NOT be called. This failed in the past
        // (#8709) because CSE lifts f(x) outside the select, and skip_stages
        // incorrectly thought f was unconditionally needed.
        called = false;
        p.set(false);
        g.realize({10});
        if (called) {
            printf("f should NOT have been called when p = false\n");
            return 1;
        }
    }

    // Test case 3: More complex expression with CSE
    {
        printf("Test 3: Complex expression with multiple CSE candidates\n");
        Func f, g;
        f(x) = track_call(x);
        // Multiple occurrences that CSE will optimize, and cse in the condition
        // itself to complicate hoisting it.
        g(x) = select(p_int * p_int + p_int * p_int == 50,
                      (f(x) * f(x) + f(x + 1)) * (f(x) * f(x) + f(x + 1)), 0);

        f.compute_root();
        g.compile_jit();

        called = false;
        p_int.set(5);
        g.realize({10});
        if (!called) {
            printf("f should have been called when p_int = 5\n");
            return 1;
        }

        called = false;
        p_int.set(6);
        g.realize({10});
        if (called) {
            printf("should NOT have been called when p_int = 6\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
