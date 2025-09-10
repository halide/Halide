#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
struct Counters {
    int bounds_queries;
    int calls;
};

extern "C" HALIDE_EXPORT_SYMBOL int skip_stages_external_call_counter(
    halide_buffer_t *input, int x, Counters *counters, halide_buffer_t *output) {
    if (input->is_bounds_query()) {
        counters->bounds_queries++;
        input->dim[0] = output->dim[0];
        return 0;
    }
    counters->calls++;
    for (int32_t i = 0; i < output->dim[0].extent; i++) {
        output->host[i] = input->host[i] + x;
    }

    return 0;
}

class SkipStagesExternalArrayFunctionsTest : public ::testing::Test {
protected:
    Var x;
    Buffer<uint8_t> out{10};
    std::array<Counters, 3> counters = {};
    Param<Counters *> cc0{"cc0", &counters[0]};
    Param<Counters *> cc1{"cc1", &counters[1]};
    Param<Counters *> cc2{"cc2", &counters[2]};
    void check_counters(const std::initializer_list<Counters> expected) {
        int i = 0;
        for (const auto &[bounds_queries, calls] : expected) {
            EXPECT_EQ(counters[i].bounds_queries, bounds_queries) << "i = " << i;
            EXPECT_EQ(counters[i].calls, calls) << "i = " << i;
            i++;
        }
        counters.fill({});
    }
};
}  // namespace

TEST_F(SkipStagesExternalArrayFunctionsTest, DiamondOneSideActive) {
    Param<bool> toggle;

    // Make a diamond-shaped graph where only one of the two
    // Side-lobes is used.
    Func f1, f2, f3, f4;
    f1(x) = cast<uint8_t>(x);
    f2.define_extern("skip_stages_external_call_counter", {f1, 1, cc0}, UInt(8), 1);
    f3.define_extern("skip_stages_external_call_counter", {f1, 2, cc1}, UInt(8), 1);
    f4(x) = select(toggle, f2(x), f3(x));

    f1.compute_root();
    f2.compute_root();
    f3.compute_root();

    f4.compile_jit();

    toggle.set(true);
    f4.realize(out);
    for (int32_t i = 0; i < 10; i++) {
        EXPECT_EQ(out(i), i + 1);
    }
    check_counters({{2, 1}, {2, 0}});

    toggle.set(false);
    f4.realize(out);
    for (int32_t i = 0; i < 10; i++) {
        EXPECT_EQ(out(i), i + 2);
    }
    check_counters({{2, 0}, {2, 1}});
}

TEST_F(SkipStagesExternalArrayFunctionsTest, DiamondFirstNodeToggleUses) {
    Param<bool> toggle1, toggle2;

    // Make a diamond-shaped graph where the first node can be
    // used in one of two ways.
    Func f1, f2, f3, f4;

    Func identity;
    identity(x) = x;

    f1.define_extern("skip_stages_external_call_counter",
                     {identity, 1, cc0},
                     UInt(8), 1);
    Func f1_plus_one;
    f1_plus_one(x) = f1(x) + 1;

    f2.define_extern("skip_stages_external_call_counter",
                     {f1_plus_one, 1, cc1},
                     UInt(8), 1);
    f3.define_extern("skip_stages_external_call_counter",
                     {f1_plus_one, 1, cc2},
                     UInt(8), 1);
    f4(x) = select(toggle1, f2(x), 0) + select(toggle2, f3(x), 0);

    identity.compute_root();
    f1_plus_one.compute_root();
    f1.compute_root();
    f2.compute_root();
    f3.compute_root();

    f4.compile_jit();

    toggle1.set(true);
    toggle2.set(true);
    f4.realize(out);
    check_counters({{2, 1}, {2, 1}, {2, 1}});

    toggle1.set(false);
    toggle2.set(true);
    f4.realize(out);
    check_counters({{2, 1}, {2, 0}, {2, 1}});

    toggle1.set(true);
    toggle2.set(false);
    f4.realize(out);
    check_counters({{2, 1}, {2, 1}, {2, 0}});

    toggle1.set(false);
    toggle2.set(false);
    f4.realize(out);
    check_counters({{2, 0}, {2, 0}, {2, 0}});
}

TEST_F(SkipStagesExternalArrayFunctionsTest, TupleValueOneSideUsed) {
    Param<bool> toggle;

    // Make a tuple-valued func where one value is used but the
    // other isn't. Currently we need to evaluate both, because we
    // have no way to turn only one of them off, and there might
    // be a recursive dependence of one on the other in an update
    // step.
    Func identity;
    identity(x) = x;

    Func extern1, extern2, f1, f2;
    extern1.define_extern("skip_stages_external_call_counter",
                          {identity, 0, cc0},
                          UInt(8), 1);
    extern2.define_extern("skip_stages_external_call_counter",
                          {identity, 1, cc1},
                          UInt(8), 1);

    f1(x) = Tuple(extern1(x), extern2(x + 1));
    f2(x) = select(toggle, f1(x)[0], 0) + f1(x)[1];

    identity.compute_root();
    extern1.compute_root();
    extern2.compute_root();

    f1.compute_root();

    f2.compile_jit();

    toggle.set(true);
    f2.realize(out);
    check_counters({{2, 1}, {2, 1}});

    toggle.set(false);
    f2.realize(out);
    check_counters({{2, 1}, {2, 1}});
}

TEST_F(SkipStagesExternalArrayFunctionsTest, TupleValueToggleUnused) {
    Param<bool> toggle;

    // Make a tuple-valued func where neither value is used when
    // the toggle is false.
    Func identity;
    identity(x) = x;

    Func extern1, extern2, f1, f2;
    extern1.define_extern("skip_stages_external_call_counter",
                          {identity, 0, cc0},
                          UInt(8), 1);
    extern2.define_extern("skip_stages_external_call_counter",
                          {identity, 1, cc1},
                          UInt(8), 1);

    f1(x) = Tuple(extern1(x), extern2(x + 1));
    f2(x) = select(toggle, f1(x)[0], 0);

    identity.compute_root();
    extern1.compute_root();
    extern2.compute_root();

    f1.compute_root();
    f2.compile_jit();

    toggle.set(true);
    f2.realize(out);
    check_counters({{2, 1}, {2, 1}});

    toggle.set(false);
    f2.realize(out);
    check_counters({{2, 0}, {2, 0}});
}

TEST_F(SkipStagesExternalArrayFunctionsTest, DiamondWithComplexSchedule) {
    Param<bool> toggle1, toggle2;

    // Make our two-toggle diamond-shaped graph again, but use a more complex schedule.
    Func identity;
    identity(x) = x;

    Func extern1, extern2, extern3, f1, f2, f3, f4;
    extern1.define_extern("skip_stages_external_call_counter",
                          {identity, 0, cc0},
                          UInt(8), 1);
    extern2.define_extern("skip_stages_external_call_counter",
                          {identity, 1, cc1},
                          UInt(8), 1);
    extern3.define_extern("skip_stages_external_call_counter",
                          {identity, 1, cc2},
                          UInt(8), 1);

    f1(x) = extern1(x);
    f2(x) = extern2(f1(x) + 1);
    f3(x) = extern3(f1(x) + 1);
    f4(x) = select(toggle1, f2(x), 0) + select(toggle2, f3(x), 0);

    identity.compute_root();
    extern1.compute_root();
    extern2.compute_root();
    extern3.compute_root();

    Var xo, xi;
    f4.split(x, xo, xi, 5);
    f1.compute_at(f4, xo);
    f2.store_root().compute_at(f4, xo);
    f3.store_at(f4, xo).compute_at(f4, xi);

    f4.compile_jit();

    toggle1.set(true);
    toggle2.set(true);
    f4.realize(out);
    check_counters({{2, 1}, {2, 1}, {2, 1}});

    toggle1.set(false);
    toggle2.set(true);
    f4.realize(out);
    check_counters({{2, 1}, {2, 0}, {2, 1}});

    toggle1.set(true);
    toggle2.set(false);
    f4.realize(out);
    check_counters({{2, 1}, {2, 1}, {2, 0}});

    toggle1.set(false);
    toggle2.set(false);
    f4.realize(out);
    check_counters({{2, 0}, {2, 0}, {2, 0}});
}
