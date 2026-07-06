#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

int single_callee_braceless_test() {
    Var x("x");

    Func shared("single_inline_calls_source");
    shared(x) = x * 4;

    Func consumer("single_inline_calls_consumer");
    consumer(x) = shared(x) + 7;

    consumer.inline_calls(shared);

    Buffer<int> out = consumer.realize({16});
    for (int i = 0; i < 16; i++) {
        const int expected = i * 4 + 7;
        if (out(i) != expected) {
            printf("single_inline_calls_consumer(%d) = %d, expected %d\n",
                   i, out(i), expected);
            return 1;
        }
    }

    return 0;
}

int does_not_schedule_callee_test() {
    Var x("x");

    Func shared_a("shared_inline_calls_source_a");
    shared_a(x) = x * 2;

    Func shared_b("shared_inline_calls_source_b");
    shared_b(x) = x * 3;

    Func inlined_consumer("inlined_consumer");
    inlined_consumer(x) = shared_a(x) + shared_b(x) + 1;

    Func ordinary_consumer("ordinary_consumer");
    ordinary_consumer(x) = shared_a(x) + shared_b(x) + 3;

    inlined_consumer.inline_calls(shared_a, shared_b);

    // inline_calls() is an eager rewrite of inlined_consumer only; it must not
    // mark the callees compute_inline(), lock their schedules early, or
    // otherwise affect ordinary_consumer's remaining calls to them.
    shared_a.compute_root();
    shared_b.compute_root();

    Buffer<int> inlined_out = inlined_consumer.realize({16});
    Buffer<int> ordinary_out = ordinary_consumer.realize({16});
    for (int i = 0; i < 16; i++) {
        const int expected_inlined = i * 5 + 1;
        const int expected_ordinary = i * 5 + 3;
        if (inlined_out(i) != expected_inlined) {
            printf("inlined_consumer(%d) = %d, expected %d\n",
                   i, inlined_out(i), expected_inlined);
            return 1;
        }
        if (ordinary_out(i) != expected_ordinary) {
            printf("ordinary_consumer(%d) = %d, expected %d\n",
                   i, ordinary_out(i), expected_ordinary);
            return 1;
        }
    }

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (single_callee_braceless_test()) {
        return 1;
    }

    if (does_not_schedule_callee_test()) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
