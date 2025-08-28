#include "Halide.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;
using testing::StartsWith;

namespace {

class CheckLoopLevels final : public IRVisitor {
public:
    CheckLoopLevels(const std::string &inner_loop_level,
                    const std::string &outer_loop_level)
        : inner_loop_level(inner_loop_level), outer_loop_level(outer_loop_level) {
    }

private:
    using IRVisitor::visit;

    const std::string inner_loop_level, outer_loop_level;
    std::string inside_for_loop;

    void visit(const For *op) override {
        std::string old_for_loop = inside_for_loop;
        inside_for_loop = op->name;
        IRVisitor::visit(op);
        inside_for_loop = old_for_loop;
    }

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->name == "sin_f32") {
            ASSERT_THAT(inside_for_loop, StartsWith(inner_loop_level));
        } else if (op->name == "cos_f32") {
            ASSERT_THAT(inside_for_loop, StartsWith(outer_loop_level));
        }
    }

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        if (op->name.substr(0, 5) == "inner") {
            ASSERT_THAT(inside_for_loop, StartsWith(inner_loop_level));
        } else if (op->name.substr(0, 5) == "outer") {
            ASSERT_THAT(inside_for_loop, StartsWith(outer_loop_level));
        } else {
            FAIL() << "Unexpected store name prefix for '" << op->name << "'";
        }
    }
};

Var x("x"), y("y"), c("c");

class DeferredLoopLevelTest : public ::testing::Test {
protected:
    Func inner{"inner"}, outer{"outer"};
    LoopLevel inner_compute_at, inner_store_at;

    std::string inner_s0_x{inner.name() + ".s0.x"};
    std::string outer_s0_x{outer.name() + ".s0.x"};

    void SetUp() override {
        inner(x, y, c) = sin(cast<float>(x + y + c));

        inner.compute_at(inner_compute_at).store_at(inner_store_at);

        outer(x, y, c) = cos(cast<float>(inner(x, y, c)));
    }

    void check(const std::string &inner_loop_level,
               const std::string &outer_loop_level) {
        EXPECT_NO_THROW(outer.realize({1, 1, 1}));

        Module m = outer.compile_to_module({outer.infer_arguments()});
        CheckLoopLevels checker(inner_loop_level, outer_loop_level);
        m.functions().front().body.accept(&checker);
    }
};

}  // namespace

TEST_F(DeferredLoopLevelTest, ComputeAndStoreAtSameOuterX) {
    // Test that LoopLevels set after being specified still take effect.
    inner_compute_at.set(LoopLevel(outer, x));
    inner_store_at.set(LoopLevel(outer, x));
    check(outer_s0_x, outer_s0_x);
}

TEST_F(DeferredLoopLevelTest, InlinedBoth) {
    // Same as before, but using inlined() for both inner LoopLevels.
    inner_compute_at.set(LoopLevel::inlined());
    inner_store_at.set(LoopLevel::inlined());
    check(outer_s0_x, outer_s0_x);
}

TEST_F(DeferredLoopLevelTest, RootBoth) {
    // Same as before, but using root() for both inner LoopLevels.
    inner_compute_at.set(LoopLevel::root());
    inner_store_at.set(LoopLevel::root());
    check(inner_s0_x, outer_s0_x);
}

TEST_F(DeferredLoopLevelTest, StoreAtRootComputeAtOuterY) {
    // Same as before, but using different store_at and compute_at()
    inner_compute_at.set(LoopLevel(outer, y));
    inner_store_at.set(LoopLevel::root());
    check(inner_s0_x, outer_s0_x);
}

TEST_F(DeferredLoopLevelTest, StoreInlinedComputeAtOuterY) {
    // Same as before, but using inlined for store_at() [equivalent to omitting
    // the store_at() call entirely] and non-inlined for compute_at
    inner_compute_at.set(LoopLevel(outer, y));
    inner_store_at.set(LoopLevel::inlined());
    check(inner_s0_x, outer_s0_x);
}
