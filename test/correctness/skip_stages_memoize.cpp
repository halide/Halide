#include "Halide.h"
#include <gtest/gtest.h>

#include <utility>

using namespace Halide;

namespace {
struct SingleToggleTrace : JITUserContext {
    Func func;
    Param<bool> toggle{false};
    explicit SingleToggleTrace(Func func)
        : func(std::move(func)) {
        handlers.custom_trace = &custom_trace;
    }
    static int custom_trace(JITUserContext *ctx, const halide_trace_event_t *e) {
        if (const auto *self = static_cast<SingleToggleTrace *>(ctx); !self->toggle.get()) {
            std::string buffer_name = self->func.name();
            if (e->event == halide_trace_store) {
                EXPECT_NE(e->func, buffer_name) << "toggle is false; producer should never have been executed";
            }
        }
        return 0;
    }
};

struct DoubleToggleTrace : JITUserContext {
    Func func1, func2;
    Param<bool> toggle1{false}, toggle2{false};
    explicit DoubleToggleTrace(Func func1, Func func2)
        : func1(std::move(func1)), func2(std::move(func2)) {
        handlers.custom_trace = &custom_trace;
    }
    static int custom_trace(JITUserContext *ctx, const halide_trace_event_t *e) {
        const auto *self = static_cast<DoubleToggleTrace *>(ctx);
        if (!self->toggle1.get()) {
            std::string buffer_name = self->func1.name();
            if (e->event == halide_trace_store) {
                EXPECT_NE(e->func, buffer_name) << "toggle1 is false; producer should never have been executed";
            }
        } else if (!self->toggle2.get()) {
            std::string buffer_name = self->func2.name();
            if (e->event == halide_trace_store) {
                EXPECT_NE(e->func, buffer_name) << "toggle2 is false; producer should never have been executed";
            }
        }
        return 0;
    }
};

class SkipStagesMemoizeTest : public ::testing::Test {
protected:
    void TearDown() override {
        Internal::JITSharedRuntime::release_all();
    }
};

void check_correctness_single(const Buffer<int> &out, bool toggle) {
    for (int x = 0; x < out.width(); ++x) {
        int correct = toggle ? 2 * x : 1;
        EXPECT_EQ(out(x), correct) << "x = " << x;
    }
}

void check_correctness_double(const Buffer<int> &out, bool toggle1, bool toggle2) {
    for (int x = 0; x < out.width(); ++x) {
        int correct = toggle1 && toggle2 ? 2 * x :
                      toggle1            ? x :
                      toggle2            ? x + 1 :
                                           1;
        EXPECT_EQ(out(x), correct) << "x = " << x;
    }
}
}  // namespace

TEST_F(SkipStagesMemoizeTest, Single) {
    Func f1{"f1"}, f2{"f2"};
    Var x;

    SingleToggleTrace trace{f1};

    f1(x) = 2 * x;
    f2(x) = select(trace.toggle, f1(x), 1);

    f1.compute_root().memoize();
    f1.trace_stores();

    for (bool toggle_val : {false, true}) {
        trace.toggle.set(toggle_val);
        Buffer<int> out = f2.realize(&trace, {10});
        check_correctness_single(out, toggle_val);
    }
}

TEST_F(SkipStagesMemoizeTest, Tuple) {
    Func f1{"f1"}, f2{"f2"};
    Var x;

    SingleToggleTrace trace{f1};

    f1(x) = Tuple(2 * x, 2 * x);
    f2(x) = Tuple(select(trace.toggle, f1(x)[0], 1),
                  select(trace.toggle, f1(x)[1], 1));

    f1.compute_root().memoize();
    f1.trace_stores();

    for (bool toggle_val : {false, true}) {
        trace.toggle.set(toggle_val);
        Realization out = f2.realize(&trace, {128});
        check_correctness_single(out[0], toggle_val);
        check_correctness_single(out[1], toggle_val);
    }
}

TEST_F(SkipStagesMemoizeTest, NonTrivialAllocatePredicate) {
    Func f1{"f1"}, f2{"f2"}, f3{"f3"};
    Var x;

    DoubleToggleTrace trace{f1, f2};

    // Generate allocate f1[...] if toggle
    f1(x) = 2 * x;
    f2(x) = select(trace.toggle1, f1(x), 1);
    f3(x) = select(trace.toggle2, f2(x), 1);

    f1.compute_root().memoize();
    f2.compute_root().memoize();

    f1.trace_stores();
    f2.trace_stores();

    for (bool toggle_val : {false, true}) {
        trace.toggle1.set(toggle_val);
        trace.toggle2.set(toggle_val);
        Buffer<int> out = f3.realize(&trace, {10});
        check_correctness_single(out, toggle_val);
    }
}

TEST_F(SkipStagesMemoizeTest, Double) {
    Func f1{"f1"}, f2{"f2"}, f3{"f3"};
    Var x;

    DoubleToggleTrace trace{f1, f2};

    f1(x) = x;
    f2(x) = x;
    f3(x) = select(trace.toggle1, f1(x), 1) +
            select(trace.toggle2, f2(x), 0);

    f1.compute_root().memoize();
    f2.compute_root().memoize();

    f1.trace_stores();
    f2.trace_stores();

    for (bool toggle_val1 : {false, true}) {
        for (bool toggle_val2 : {false, true}) {
            trace.toggle1.set(toggle_val1);
            trace.toggle2.set(toggle_val2);
            Buffer<int> out = f3.realize(&trace, {10});
            check_correctness_double(out, toggle_val1, toggle_val2);
        }
    }
}
