#include "Halide.h"
#include "check_call_graphs.h"
#include <gtest/gtest.h>

#include <cstdio>
#include <functional>

namespace {

using std::string;

using namespace Halide;
using namespace Halide::Internal;

struct CountPredicatedStoreLoad final : IRVisitor {
    int store_count = 0;
    int load_count = 0;

protected:
    using IRVisitor::visit;

    void visit(const Load *op) override {
        if (!is_const_one(op->predicate)) {
            load_count++;
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) override {
        if (!is_const_one(op->predicate)) {
            store_count++;
        }
        IRVisitor::visit(op);
    }
};

struct CheckPredicatedStoreLoad final : IRMutator {
    int expected_store_count;
    int expected_load_count;

    CheckPredicatedStoreLoad(int store, int load)
        : expected_store_count(store), expected_load_count(load) {
    }

    using IRMutator::mutate;
    Stmt mutate(const Stmt &s) override {
        CountPredicatedStoreLoad c;
        s.accept(&c);

        EXPECT_EQ(expected_store_count, c.store_count);
        EXPECT_EQ(expected_load_count, c.load_count);
        return s;
    }
};

class PredicatedStoreLoadTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
};

}  // namespace

TEST_F(PredicatedStoreLoadTest, PredicatedTailTest) {
    int size = 73;
    for (auto i : {TailStrategy::Predicate, TailStrategy::PredicateLoads, TailStrategy::PredicateStores}) {
        Var x("x"), y("y");
        Func f("f"), g("g");

        ImageParam p(Int(32), 2);

        f(x, y) = p(x, y);

        // We need a wrapper to avoid getting the bounds inflated by the rounding-up cases by realize.
        g(x, y) = f(x, y);
        f.compute_root();

        const int vector_size = 32;
        f.vectorize(x, vector_size, i);
        if (target.has_feature(Target::HVX)) {
            f.hexagon();
        }
        int predicated_loads = i != TailStrategy::PredicateStores ? 1 : 0;
        int predicated_stores = i != TailStrategy::PredicateLoads ? 1 : 0;
        g.add_custom_lowering_pass(new CheckPredicatedStoreLoad(predicated_stores, predicated_loads));

        int buffer_size = size;
        if (i == TailStrategy::PredicateStores) {
            buffer_size = ((buffer_size + vector_size - 1) / vector_size) * vector_size;
        }

        Buffer<int> input(buffer_size, size);
        input.fill([](int x, int y) { return x; });
        p.set(input);

        Buffer<int> im = g.realize({size, size});
        auto func = [](int x, int y) {
            return x;
        };
        ASSERT_EQ(0, check_image(im, func));
    }
}

TEST_F(PredicatedStoreLoadTest, PredicatedTailWithScalarTest) {
    int size = 73;
    Var x("x"), y("y");
    Func f("f"), g("g");

    g(x) = 10;
    f(x, y) = x + g(0);

    g.compute_at(f, y);
    f.vectorize(x, 32, TailStrategy::Predicate);
    if (target.has_feature(Target::HVX)) {
        f.hexagon();
    }
    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(1, 0));

    Buffer<int> im = f.realize({size, size});
    auto func = [](int x, int y) {
        return x + 10;
    };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, VectorizedPredicatedStoreScalarizedPredicatedLoadTest) {
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 100, 0, 100);
    r.where(r.x + r.y < r.x * r.y);

    ref(x, y) = 10;
    ref(r.x, r.y) += g(2 * r.x, r.y) + g(2 * r.x + 1, r.y);
    Buffer<int> im_ref = ref.realize({170, 170});

    f(x, y) = 10;
    f(r.x, r.y) += g(2 * r.x, r.y) + g(2 * r.x + 1, r.y);

    f.update(0).vectorize(r.x, 32);
    if (target.has_feature(Target::HVX)) {
        f.update(0).hexagon();
    }

    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(2, 6));

    Buffer<int> im = f.realize({170, 170});
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, VectorizedDenseLoadWithStrideMinusOneTest) {
    int size = 73;
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x * y;
    g.compute_root();

    ref(x, y) = select(x < 23, g(size - x, y) * 2 + g(20 - x, y), undef<int>());
    Buffer<int> im_ref = ref.realize({size, size});

    f(x, y) = select(x < 23, g(size - x, y) * 2 + g(20 - x, y), undef<int>());

    f.vectorize(x, 32, TailStrategy::Predicate);
    if (target.has_feature(Target::HVX)) {
        f.hexagon();
    }
    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(3, 6));

    Buffer<int> im = f.realize({size, size});
    auto func = [&im_ref, &im](int x, int y, int z) {
        // For x >= 23, the buffer is undef
        return (x < 23) ? im_ref(x, y, z) : im(x, y, z);
    };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, MultipleVectorizedPredicateTest) {
    int size = 100;
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x * y;
    g.compute_root();

    RDom r(0, size, 0, size);
    r.where(r.x + r.y < 57);
    r.where(r.x * r.y + r.x * r.x < 490);

    ref(x, y) = 10;
    ref(r.x, r.y) = g(size - r.x, r.y) * 2 + g(67 - r.x, r.y);
    Buffer<int> im_ref = ref.realize({size, size});

    f(x, y) = 10;
    f(r.x, r.y) = g(size - r.x, r.y) * 2 + g(67 - r.x, r.y);

    f.update(0).vectorize(r.x, 32);
    if (target.has_feature(Target::HVX)) {
        f.update(0).hexagon();
    }
    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(1, 2));

    Buffer<int> im = f.realize({size, size});
    auto func = [&im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, ScalarLoadTest) {
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 48);

    ref(x, y) = 10;
    ref(r.x, r.y) += 1 + max(g(0, 1), g(2 * r.x + 1, r.y));
    Buffer<int> im_ref = ref.realize({160, 160});

    f(x, y) = 10;
    f(r.x, r.y) += 1 + max(g(0, 1), g(2 * r.x + 1, r.y));

    f.update(0).vectorize(r.x, 32);
    if (target.has_feature(Target::HVX)) {
        f.update(0).hexagon();
    }

    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(1, 2));

    Buffer<int> im = f.realize({160, 160});
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, ScalarStoreTest) {
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 48);

    ref(x, y) = 10;
    ref(13, 13) = max(g(0, 1), g(2 * r.x + 1, r.y));
    Buffer<int> im_ref = ref.realize({160, 160});

    f(x, y) = 10;
    f(13, 13) = max(g(0, 1), g(2 * r.x + 1, r.y));

    f.update(0).allow_race_conditions();

    f.update(0).vectorize(r.x, 32);
    if (target.has_feature(Target::HVX)) {
        f.update(0).hexagon();
    }

    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(1, 1));

    Buffer<int> im = f.realize({160, 160});
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, NotDependentOnVectorizedVarTest) {
    Var x("x"), y("y"), z("z");
    Func f("f"), g("g"), ref("ref");

    g(x, y, z) = x + y + z;
    g.compute_root();

    RDom r(0, 80, 0, 80, 0, 80);
    r.where(r.z * r.z < 47);

    ref(x, y, z) = 10;
    ref(r.x, r.y, 1) = max(g(0, 1, 2), g(r.x + 1, r.y, 2));
    Buffer<int> im_ref = ref.realize({160, 160, 160});

    f(x, y, z) = 10;
    f(r.x, r.y, 1) = max(g(0, 1, 2), g(r.x + 1, r.y, 2));

    f.update(0).allow_race_conditions();

    f.update(0).vectorize(r.z, 32);
    if (target.has_feature(Target::HVX)) {
        f.update(0).hexagon();
    }
    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(0, 0));

    Buffer<int> im = f.realize({160, 160, 160});
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, NoOpStoreTest) {
    Var x("x"), y("y");
    Func f("f"), ref("ref");

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 47);

    ref(x, y) = x + y;
    ref(2 * r.x + 1, r.y) = ref(2 * r.x + 1, r.y);
    ref(2 * r.x, 3 * r.y) = ref(2 * r.x, 3 * r.y);
    Buffer<int> im_ref = ref.realize({240, 240});

    f(x, y) = x + y;
    f(2 * r.x + 1, r.y) = f(2 * r.x + 1, r.y);
    f(2 * r.x, 3 * r.y) = f(2 * r.x, 3 * r.y);

    f.update(0).vectorize(r.x, 32);
    f.update(1).vectorize(r.y, 32);
    if (target.has_feature(Target::HVX)) {
        f.update(0).hexagon();
        f.update(1).hexagon();
    }

    Buffer<int> im = f.realize({240, 240});
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, VectorizedPredicatedPredicateWithPureCallTest) {
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 100, 0, 100);
    r.where(r.x + r.y < r.x * r.y);

    ref(x, y) = 10;
    ref(r.x, r.y) += abs(r.x * r.y) + g(2 * r.x + 1, r.y);
    Buffer<int> im_ref = ref.realize({160, 160});

    f(x, y) = 10;
    f(r.x, r.y) += abs(r.x * r.y) + g(2 * r.x + 1, r.y);

    f.update(0).vectorize(r.x, 32);
    if (target.has_feature(Target::HVX)) {
        f.update(0).hexagon();
    }
    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(2, 4));

    Buffer<int> im = f.realize({160, 160});
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, VectorizedPredicatedLoadConstIndexTest) {
    Buffer<int> in(100, 100);
    for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
            in(x, y) = rand();
        }
    }

    Func f("f"), ref("ref");
    Var x("x"), y("y");
    ImageParam input(Int(32), 2, "input");

    input.set(in);

    RDom r(0, 100);

    ref(x, y) = x + y;
    ref(r.x, y) = clamp(select((r.x % 2) == 0, r.x, y) + input(r.x % 2, y), 0, 10);
    Buffer<int> im_ref = ref.realize({100, 100});

    f(x, y) = x + y;
    f(r.x, y) = clamp(select((r.x % 2) == 0, r.x, y) + input(r.x % 2, y), 0, 10);

    f.update().vectorize(r.x, 32);
    if (target.has_feature(Target::HVX)) {
        f.update().hexagon();
    }
    f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(1, 2));

    Buffer<int> im = f.realize({100, 100});
    auto func = [im_ref](int x, int y) { return im_ref(x, y); };
    ASSERT_EQ(0, check_image(im, func));
}

TEST_F(PredicatedStoreLoadTest, VectorizedPredicatedLoadLutTest) {
    if (target.arch == Target::Hexagon || target.has_feature(Target::HVX)) {
        GTEST_SKIP() << "This test will fail on Hexagon as the LUT is larger than 16 bits.";
    }

    constexpr int vector_size = 4;
    constexpr int lut_height = vector_size + 2;  // Any non-even multiple of vector-size will do.
    constexpr int dst_len = 100;

    Buffer<int32_t> lut(2, lut_height);
    lut.fill(0);

    Var x("x");
    Func dst("dst");

    RDom r(0, lut_height);

    dst(x) = 0.f;
    dst(clamp(lut(0, r), 0, dst_len - 1)) += 1.f;

    dst.output_buffer().dim(0).set_min(0).set_extent(dst_len);

    // Ignore the race condition so we can have predicated vectorized
    // LUT loads on both LHS and RHS of the predicated vectorized store
    dst.update().allow_race_conditions().vectorize(r, vector_size);
    dst.add_custom_lowering_pass(new CheckPredicatedStoreLoad(1, 2));

    dst.realize({dst_len});
}

TEST_F(PredicatedStoreLoadTest, PredicatedAtomicStoreTest) {
    // We don't support atomic predicated stores, so ensure that we don't
    // generate them. See https://github.com/halide/Halide/issues/8280
    ImageParam in(Float(32), 1);
    Func f;
    Var x;
    RDom r(0, 20);

    f(x) = 0.f;
    f(x) += in(r) + x;
    f.update().vectorize(x, 8, TailStrategy::GuardWithIf).atomic().parallel(r);

    // This will cause an internal_error in the LLVM backend if we pass a
    // predicated atomic store down to codegen.
    ASSERT_NO_THROW(f.compile_jit(target));
}
