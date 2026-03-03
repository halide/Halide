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

    // Test case 1: Single use in select (this should work)
    {
        printf("Test 1: Single use in select\n");
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
            printf("ERROR: f should have been called when p=true\n");
            return 1;
        }

        // When p is false, f should NOT be called (skip_stages optimization)
        called = false;
        p.set(false);
        g.realize({10});
        if (called) {
            printf("ERROR: f should NOT have been called when p=false\n");
            return 1;
        }
        printf("Test 1 passed!\n");
    }

    // Test case 2: Multiple uses in select (this currently fails)
    {
        printf("Test 2: Multiple uses in select (tests CSE interaction)\n");
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
            printf("ERROR: f should have been called when p=true\n");
            return 1;
        }

        // When p is false, f should NOT be called (skip_stages optimization)
        // This is the bug: CSE lifts f(x) outside the select, and skip_stages
        // incorrectly thinks f is unconditionally needed
        called = false;
        p.set(false);
        g.realize({10});
        if (called) {
            printf("ERROR: f should NOT have been called when p=false (CSE bug)\n");
            printf("This is the bug reported in issue #8709\n");
            return 1;
        }
        printf("Test 2 passed!\n");
    }

    // Test case 3: More complex expression with CSE
    {
        printf("Test 3: Complex expression with multiple CSE candidates\n");
        Func f, g;
        f(x) = track_call(x);
        // Multiple occurrences that CSE will optimize
        g(x) = select(p, f(x) + f(x + 1) + f(x) + f(x + 1), 0);

        f.compute_root();
        g.compile_jit();

        called = false;
        p.set(true);
        g.realize({10});
        if (!called) {
            printf("ERROR: f should have been called when p=true\n");
            return 1;
        }

        called = false;
        p.set(false);
        g.realize({10});
        if (called) {
            printf("ERROR: f should NOT have been called when p=false\n");
            return 1;
        }
        printf("Test 3 passed!\n");
    }

    printf("Success!\n");
    return 0;
}
