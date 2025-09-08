#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(Param, TypedParam) {
    Var x("x");
    Func f("f");

    Param<int> u;
    Param<int> u_name("u_name");

    f(x) = u;

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xo, xi;
        f.gpu_tile(x, xo, xi, 256);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 32);
    }

    u.set(17);
    u.set_estimate(17);
    Buffer<int> out_17 = f.realize({1024}, target);

    // verify the get method.
    ASSERT_EQ(u.get(), 17);

    // Copied Params should still refer to the same underlying Parameter,
    // so setting the copy should be equivalent to setting the original.
    Param<int> u_alias = u;
    u_alias.set(123);
    u_alias.set_estimate(123);
    Buffer<int> out_123 = f.realize({1024}, target);

    // verify the get method, again.
    ASSERT_EQ(u.get(), 123);

    for (int i = 0; i < 1024; i++) {
        ASSERT_EQ(out_17(i), 17) << "i = " << i;
        ASSERT_EQ(out_123(i), 123) << "i = " << i;
    }
}

TEST(Param, RuntimeTypedParam) {
    // Now the same tests, but with Param types specified at runtime
    Var x("x");
    Func f("f");

    Param<> u(Int(32));
    Param<> u_name(Int(32), "u_name");

    f(x) = u;

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xo, xi;
        f.gpu_tile(x, xo, xi, 256);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 32);
    }

    // For Param<void>, you must provide an explicit template argument to set(),
    // and it must match the dynamic type of the Param.
    u.set<int32_t>(17);
    u.set_estimate<int32_t>(17);
    Buffer<int32_t> out_17 = f.realize({1024}, target);

    // For Param<void>, you must provide an explicit template argument to get(),
    // and it must match the dynamic type of the Param.
    ASSERT_EQ(u.get<int32_t>(), 17);

    // This would fail with a user_assert inside the get() method
    // assert(u.get<int16_t>() == 17);

    // Copied Params should still refer to the same underlying Parameter,
    // so setting the copy should be equivalent to setting the original.
    Param<> u_alias = u;
    u_alias.set(123);
    u_alias.set_estimate(123);
    Buffer<int32_t> out_123 = f.realize({1024}, target);

    ASSERT_EQ(u.get<int32_t>(), 123);

    for (int i = 0; i < 1024; i++) {
        ASSERT_EQ(out_17(i), 17) << "i = " << i;
        ASSERT_EQ(out_123(i), 123) << "i = " << i;
    }
}

TEST(Param, CopyConstructorVoidToTyped) {
    // Test copy ctor between void and non-void Params
    Var x("x");
    Param<int32_t> u;

    Func f("f");
    f(x) = u;

    u.set(17);
    u.set_estimate(17);
    Buffer<int32_t> out_17 = f.realize({1});
    ASSERT_EQ(out_17(0), 17);

    // You can always construct a Param<void> from a Param<nonvoid>
    Param<> u_alias = u;
    u_alias.set(123);
    u_alias.set_estimate(123);
    Buffer<int32_t> out_123 = f.realize({1});
    ASSERT_EQ(out_123(0), 123);

    // You can also construct Param<nonvoid> from Param<void>,
    // but only if the runtime type of the RHS matches the static type
    // of the LHS (otherwise, assert-fails)
    Param<int32_t> u_alias2 = u_alias;
    u_alias2.set(124);
    u_alias2.set_estimate(124);
    Buffer<int32_t> out_124 = f.realize({1});
    ASSERT_EQ(out_124(0), 124);
}

TEST(Param, AssignmentOperatorVoidToTyped) {
    // Test operator= between void and non-void Params
    Var x("x");
    Param<int32_t> u;

    Func f("f");
    f(x) = u;

    u.set(17);
    u.set_estimate(17);
    Buffer<int32_t> out_17 = f.realize({1});
    ASSERT_EQ(out_17(0), 17);

    // You can always do Param<void> = Param<nonvoid> (LHS takes on type of RHS)
    Param<> u_alias(Float(64));
    u_alias = u;
    ASSERT_EQ(u_alias.type(), Int(32));
    u_alias.set(123);
    u_alias.set_estimate(123);
    Buffer<int32_t> out_123 = f.realize({1});
    ASSERT_EQ(out_123(0), 123);

    // You can also do Param<nonvoid> = Param<void>,
    // but only if the runtime type of the RHS matches the static type
    // of the LHS (otherwise, assert-fails)
    Param<int32_t> u_alias2;
    u_alias2 = u_alias;
    u_alias2.set(124);
    u_alias2.set_estimate(124);
    Buffer<int32_t> out_124 = f.realize({1});
    ASSERT_EQ(out_124(0), 124);
}
