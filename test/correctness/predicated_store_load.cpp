#include "Halide.h"
#include <assert.h>
#include <functional>
#include <map>
#include <numeric>
#include <stdio.h>

#include "test/common/check_call_graphs.h"

namespace {

using std::map;
using std::string;
using std::vector;

using namespace Halide;
using namespace Halide::Internal;

class CountPredicatedStoreLoad : public IRVisitor {
public:
    int store_count;
    int load_count;

    CountPredicatedStoreLoad()
        : store_count(0), load_count(0) {
    }

protected:
    using IRVisitor::visit;

    void visit(const Load *op) override {
        if (!is_one(op->predicate)) {
            load_count++;
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) override {
        if (!is_one(op->predicate)) {
            store_count++;
        }
        IRVisitor::visit(op);
    }
};

class CheckPredicatedStoreLoad : public IRMutator {
    int expected_store_count;
    int expected_load_count;

public:
    CheckPredicatedStoreLoad(const Target &target, int store, int load)
        : expected_store_count(store), expected_load_count(load) {
        // TODO: disabling for now due to trunk LLVM breakage.
        // See: https://github.com/halide/Halide/issues/3534
        if (target.arch == Target::X86) {
            expected_store_count = 0;
            expected_load_count = 0;
        }
    }
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        CountPredicatedStoreLoad c;
        s.accept(&c);

        if (expected_store_count != c.store_count) {
            printf("There were %d predicated stores; expect %d predicated stores\n",
                   c.store_count, expected_store_count);
            exit(-1);
        }
        if (expected_load_count != c.load_count) {
            printf("There were %d predicated loads; expect %d predicated loads\n",
                   c.load_count, expected_load_count);
            exit(-1);
        }
        return s;
    }
};

int vectorized_predicated_store_scalarized_predicated_load_test(const Target &t) {
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 100, 0, 100);
    r.where(r.x + r.y < r.x * r.y);

    ref(x, y) = 10;
    ref(r.x, r.y) += g(2 * r.x, r.y) + g(2 * r.x + 1, r.y);
    Buffer<int> im_ref = ref.realize(170, 170);

    f(x, y) = 10;
    f(r.x, r.y) += g(2 * r.x, r.y) + g(2 * r.x + 1, r.y);

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (t.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 3, 9));
    }

    Buffer<int> im = f.realize(170, 170);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int vectorized_dense_load_with_stride_minus_one_test(const Target &t) {
    int size = 73;
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x * y;
    g.compute_root();

    ref(x, y) = select(x < 23, g(size - x, y) * 2 + g(20 - x, y), undef<int>());
    Buffer<int> im_ref = ref.realize(size, size);

    f(x, y) = select(x < 23, g(size - x, y) * 2 + g(20 - x, y), undef<int>());

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.hexagon().vectorize(x, 32);
    } else if (t.arch == Target::X86) {
        f.vectorize(x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 2, 4));
    }

    Buffer<int> im = f.realize(size, size);
    auto func = [&im_ref, &im](int x, int y, int z) {
        // For x >= 23, the buffer is undef
        return (x < 23) ? im_ref(x, y, z) : im(x, y, z);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_vectorized_predicate_test(const Target &t) {
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
    Buffer<int> im_ref = ref.realize(size, size);

    f(x, y) = 10;
    f(r.x, r.y) = g(size - r.x, r.y) * 2 + g(67 - r.x, r.y);

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (t.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 3, 6));
    }

    Buffer<int> im = f.realize(size, size);
    auto func = [&im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int scalar_load_test(const Target &t) {
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 48);

    ref(x, y) = 10;
    ref(r.x, r.y) += 1 + max(g(0, 1), g(2 * r.x + 1, r.y));
    Buffer<int> im_ref = ref.realize(160, 160);

    f(x, y) = 10;
    f(r.x, r.y) += 1 + max(g(0, 1), g(2 * r.x + 1, r.y));

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (t.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 1, 2));
    }

    Buffer<int> im = f.realize(160, 160);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int scalar_store_test(const Target &t) {
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 48);

    ref(x, y) = 10;
    ref(13, 13) = max(g(0, 1), g(2 * r.x + 1, r.y));
    Buffer<int> im_ref = ref.realize(160, 160);

    f(x, y) = 10;
    f(13, 13) = max(g(0, 1), g(2 * r.x + 1, r.y));

    f.update(0).allow_race_conditions();

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (t.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 1, 1));
    }

    Buffer<int> im = f.realize(160, 160);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int not_dependent_on_vectorized_var_test(const Target &t) {
    Var x("x"), y("y"), z("z");
    Func f("f"), g("g"), ref("ref");

    g(x, y, z) = x + y + z;
    g.compute_root();

    RDom r(0, 80, 0, 80, 0, 80);
    r.where(r.z * r.z < 47);

    ref(x, y, z) = 10;
    ref(r.x, r.y, 1) = max(g(0, 1, 2), g(r.x + 1, r.y, 2));
    Buffer<int> im_ref = ref.realize(160, 160, 160);

    f(x, y, z) = 10;
    f(r.x, r.y, 1) = max(g(0, 1, 2), g(r.x + 1, r.y, 2));

    f.update(0).allow_race_conditions();

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.z, 32);
    } else if (t.arch == Target::X86) {
        f.update(0).vectorize(r.z, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 0, 0));
    }

    Buffer<int> im = f.realize(160, 160, 160);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int no_op_store_test(const Target &t) {
    Var x("x"), y("y");
    Func f("f"), ref("ref");

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 47);

    ref(x, y) = x + y;
    ref(2 * r.x + 1, r.y) = ref(2 * r.x + 1, r.y);
    ref(2 * r.x, 3 * r.y) = ref(2 * r.x, 3 * r.y);
    Buffer<int> im_ref = ref.realize(240, 240);

    f(x, y) = x + y;
    f(2 * r.x + 1, r.y) = f(2 * r.x + 1, r.y);
    f(2 * r.x, 3 * r.y) = f(2 * r.x, 3 * r.y);

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
        f.update(1).hexagon().vectorize(r.y, 32);
    } else if (t.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.update(1).vectorize(r.y, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 0, 0));
    }

    Buffer<int> im = f.realize(240, 240);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int vectorized_predicated_predicate_with_pure_call_test(const Target &t) {
    Var x("x"), y("y");
    Func f("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 100, 0, 100);
    r.where(r.x + r.y < r.x * r.y);

    ref(x, y) = 10;
    ref(r.x, r.y) += abs(r.x * r.y) + g(2 * r.x + 1, r.y);
    Buffer<int> im_ref = ref.realize(160, 160);

    f(x, y) = 10;
    f(r.x, r.y) += abs(r.x * r.y) + g(2 * r.x + 1, r.y);

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (t.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 3, 6));
    }

    Buffer<int> im = f.realize(160, 160);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int vectorized_predicated_load_const_index_test(const Target &t) {
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
    Buffer<int> im_ref = ref.realize(100, 100);

    f(x, y) = x + y;
    f(r.x, y) = clamp(select((r.x % 2) == 0, r.x, y) + input(r.x % 2, y), 0, 10);

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update().hexagon().vectorize(r.x, 32);
    } else if (t.arch == Target::X86) {
        f.update().vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 1, 2));
    }

    Buffer<int> im = f.realize(100, 100);
    auto func = [im_ref](int x, int y) { return im_ref(x, y); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int vectorized_predicated_load_lut_test(const Target &t) {
    if (t.arch != Target::X86) {
        // This test will fail on Hexagon as the LUT is larger than 16 bits.
        // Since using less than 16-bit LUT will make the predicate on the
        // vector store/load disappear, only run the test for X86.
        return 0;
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
    dst.add_custom_lowering_pass(new CheckPredicatedStoreLoad(t, 1, 3));

    dst.realize(dst_len);

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();

    printf("Running vectorized dense load with stride minus one test\n");
    if (vectorized_dense_load_with_stride_minus_one_test(t) != 0) {
        return -1;
    }

    printf("Running multiple vectorized predicate test\n");
    if (multiple_vectorized_predicate_test(t) != 0) {
        return -1;
    }

    printf("Running vectorized predicated store scalarized predicated load test\n");
    if (vectorized_predicated_store_scalarized_predicated_load_test(t) != 0) {
        return -1;
    }

    printf("Running scalar load test\n");
    if (scalar_load_test(t) != 0) {
        return -1;
    }

    printf("Running scalar store test\n");
    if (scalar_store_test(t) != 0) {
        return -1;
    }

    printf("Running not dependent on vectorized var test\n");
    if (not_dependent_on_vectorized_var_test(t) != 0) {
        return -1;
    }

    printf("Running no-op store test\n");
    if (no_op_store_test(t) != 0) {
        return -1;
    }

    printf("Running vectorized predicated with pure call test\n");
    if (vectorized_predicated_predicate_with_pure_call_test(t) != 0) {
        return -1;
    }

    printf("Running vectorized predicated load with constant index test\n");
    if (vectorized_predicated_load_const_index_test(t) != 0) {
        return -1;
    }

    printf("Running vectorized predicated load lut test\n");
    if (vectorized_predicated_load_lut_test(t) != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
