#include "Halide.h"
#include <gtest/gtest.h>

// This test demonstrates using tracing to give you something like a
// stack trace in case of a crash (due to a compiler bug, or a bug in
// external code). We use a posix signal handler, which is probably
// os-dependent, so I'm going to enable this test on linux only.

#if defined(__linux__) || defined(__APPLE__) || defined(__unix) || defined(__posix)

#include <signal.h>
#include <stack>
#include <string>

using namespace Halide;

namespace {

using std::stack;
using std::string;

stack<string> stack_trace;

int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    const string event_types[] = {"Load ",
                                  "Store ",
                                  "Begin realization ",
                                  "End realization ",
                                  "Produce ",
                                  "Consume ",
                                  "End consume ",
                                  "Begin pipeline ",
                                  "End pipeline "};

    if (e->event == halide_trace_end_realization ||
        e->event == halide_trace_consume ||
        e->event == halide_trace_end_consume ||
        e->event == halide_trace_end_pipeline) {
        // These events signal the end of some previous event
        stack_trace.pop();
    }
    if (e->event == halide_trace_begin_realization ||
        e->event == halide_trace_produce ||
        e->event == halide_trace_consume ||
        e->event == halide_trace_begin_pipeline) {
        // These events signal the start of some new region
        stack_trace.push(event_types[e->event] + e->func);
    }

    return 0;
}

void signal_handler(int signum) {
    std::cerr << "Correctly triggered a segfault (signal " << signum << ").\n";
    std::cerr << "Stack trace:\n";
    while (!stack_trace.empty()) {
        std::cerr << stack_trace.top() << "\n";
        stack_trace.pop();
    }
    std::cerr << "Success!\n";
    exit(0);
}

}  // namespace

TEST(TracingStackDeathTest, SegfaultWithTrace) {
#ifdef HALIDE_INTERNAL_USING_ASAN
    // ASAN also needs to intercept the SIGSEGV signal handler;
    // we could probably make these work together, but it's
    // also probably not worth the effort.
    GTEST_SKIP() << "tracing_stack does not run under ASAN.";
#endif

    auto test_case = [] {
        signal(SIGSEGV, signal_handler);
        signal(SIGBUS, signal_handler);

        // Loads from this image will barf, because we've messed up the host pointer
        Buffer<int> input(100, 100);
        halide_buffer_t *buf = input.raw_buffer();
        buf->host = (uint8_t *)17;

        Func f("f"), g("g"), h("h");
        Var x("x"), y("y");

        f(x, y) = x + y;
        f.compute_root().trace_realizations();

        g(x, y) = f(x, y) + 37;
        g.compute_root().trace_realizations();

        h(x, y) = g(x, y) + input(x, y);
        h.trace_realizations();

        h.jit_handlers().custom_trace = &my_trace;
        h.realize({100, 100});
    };
    EXPECT_EXIT(test_case(), ::testing::ExitedWithCode(0), "Success!");
}

#else

TEST(TracingStack, SegfaultWithTrace) {
    GTEST_SKIP() << "Test requires UNIX signal handling";
}

#endif
