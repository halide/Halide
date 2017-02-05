#include "Halide.h"
#include <assert.h>
#include <stdio.h>
#include <functional>
#include <map>
#include <numeric>

#include "test/common/check_call_graphs.h"

using std::map;
using std::vector;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

class CountPredicatedStoreLoad : public IRVisitor {
public:
    int store_count;
    int load_count;

    CountPredicatedStoreLoad() : store_count(0), load_count(0) {}

protected:
    using IRVisitor::visit;

    void visit(const Load *op) {
        if (!is_one(op->predicate)) {
            load_count++;
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        if (!is_one(op->predicate)) {
            store_count++;
        }
        IRVisitor::visit(op);
    }
};

class CheckPredicatedStoreLoad : public IRMutator {
    int has_store_count;
    int has_load_count;
public:
    CheckPredicatedStoreLoad(bool store, bool load) :
        has_store_count(store), has_load_count(load) {}
    using IRMutator::mutate;

    Stmt mutate(Stmt s) {
        CountPredicatedStoreLoad c;
        s.accept(&c);

        if (has_store_count) {
            if (c.store_count == 0) {
                printf("There should be some predicated stores but didn't find any\n");
                exit(-1);
            }
        } else {
            if (c.store_count > 0) {
                printf("There were %d predicated stores. There weren't supposed to be any stores\n",
                       c.store_count);
                exit(-1);
            }
        }

        if (has_load_count) {
            if (c.load_count == 0) {
                printf("There should be some predicated loads but didn't find any\n");
                exit(-1);
            }
        } else {
            if (c.load_count > 0) {
                printf("There were %d predicated loads. There weren't supposed to be any loads\n",
                       c.load_count);
                exit(-1);
            }
        }
        return s;
    }
};

int vectorized_predicated_store_scalarized_predicated_load_test() {
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 100, 0, 100);
    r.where(r.x + r.y < r.x*r.y);

    ref(x, y) = 10;
    ref(r.x, r.y) += g(2*r.x, r.y) + g(2*r.x + 1, r.y);
    Buffer<int> im_ref = ref.realize(170, 170);

    f(x, y) = 10;
    f(r.x, r.y) += g(2*r.x, r.y) + g(2*r.x + 1, r.y);

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (target.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(true, true));
    }

    Buffer<int> im = f.realize(170, 170);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int vectorized_dense_load_with_stride_minus_one_test() {
    int size = 73;
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x * y;
    g.compute_root();

    ref(x, y) = select(x < 23, g(size-x, y) * 2 + g(20-x, y), undef<int>());
    Buffer<int> im_ref = ref.realize(size, size);

    f(x, y) = select(x < 23, g(size-x, y) * 2 + g(20-x, y), undef<int>());

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.hexagon().vectorize(x, 32);
    } else if (target.arch == Target::X86) {
        f.vectorize(x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(true, true));
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

int multiple_vectorized_predicate_test() {
    int size = 100;
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x * y;
    g.compute_root();

    RDom r(0, size, 0, size);
    r.where(r.x + r.y < 57);
    r.where(r.x*r.y + r.x*r.x < 490);

    ref(x, y) = 10;
    ref(r.x, r.y) = g(size-r.x, r.y) * 2 + g(67-r.x, r.y);
    Buffer<int> im_ref = ref.realize(size, size);

    f(x, y) = 10;
    f(r.x, r.y) = g(size-r.x, r.y) * 2 + g(67-r.x, r.y);

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (target.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(true, true));
    }

    Buffer<int> im = f.realize(size, size);
    auto func = [&im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int scalar_load_test() {
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 48);

    ref(x, y) = 10;
    ref(r.x, r.y) += 1 + max(g(0, 1), g(2*r.x + 1, r.y));
    Buffer<int> im_ref = ref.realize(160, 160);

    f(x, y) = 10;
    f(r.x, r.y) += 1 + max(g(0, 1), g(2*r.x + 1, r.y));

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (target.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(true, true));
    }

    Buffer<int> im = f.realize(160, 160);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int scalar_store_test() {
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 48);

    ref(x, y) = 10;
    ref(13, 13) = max(g(0, 1), g(2*r.x + 1, r.y));
    Buffer<int> im_ref = ref.realize(160, 160);

    f(x, y) = 10;
    f(13, 13) = max(g(0, 1), g(2*r.x + 1, r.y));

    f.update(0).allow_race_conditions();

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (target.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(true, true));
    }

    Buffer<int> im = f.realize(160, 160);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int not_dependent_on_vectorized_var_test() {
    Var x("x"), y("y"), z("z");
    Func f ("f"), g("g"), ref("ref");

    g(x, y, z) = x + y + z;
    g.compute_root();

    RDom r(0, 80, 0, 80, 0, 80);
    r.where(r.z*r.z < 47);

    ref(x, y, z) = 10;
    ref(r.x, r.y, 1) = max(g(0, 1, 2), g(r.x + 1, r.y, 2));
    Buffer<int> im_ref = ref.realize(160, 160, 160);

    f(x, y, z) = 10;
    f(r.x, r.y, 1) = max(g(0, 1, 2), g(r.x + 1, r.y, 2));

    f.update(0).allow_race_conditions();

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.z, 32);
    } else if (target.arch == Target::X86) {
        f.update(0).vectorize(r.z, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(false, false));
    }

    Buffer<int> im = f.realize(160, 160, 160);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int no_op_store_test() {
    Var x("x"), y("y");
    Func f ("f"), ref("ref");

    RDom r(0, 80, 0, 80);
    r.where(r.x + r.y < 47);

    ref(x, y) = x + y;
    ref(2*r.x + 1, r.y) = ref(2*r.x + 1, r.y);
    ref(2*r.x, 3*r.y) = ref(2*r.x, 3*r.y);
    Buffer<int> im_ref = ref.realize(240, 240);

    f(x, y) = x + y;
    f(2*r.x + 1, r.y) = f(2*r.x + 1, r.y);
    f(2*r.x, 3*r.y) = f(2*r.x, 3*r.y);

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
        f.update(1).hexagon().vectorize(r.y, 32);
    } else if (target.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.update(1).vectorize(r.y, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(false, false));
    }

    Buffer<int> im = f.realize(240, 240);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int vectorized_predicated_predicate_with_pure_call_test() {
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 100, 0, 100);
    r.where(r.x + r.y < r.x*r.y);

    ref(x, y) = 10;
    ref(r.x, r.y) += abs(r.x*r.y) + g(2*r.x + 1, r.y);
    Buffer<int> im_ref = ref.realize(160, 160);

    f(x, y) = 10;
    f(r.x, r.y) += abs(r.x*r.y) + g(2*r.x + 1, r.y);

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.update(0).hexagon().vectorize(r.x, 32);
    } else if (target.arch == Target::X86) {
        f.update(0).vectorize(r.x, 32);
        f.add_custom_lowering_pass(new CheckPredicatedStoreLoad(true, true));
    }

    Buffer<int> im = f.realize(160, 160);
    auto func = [im_ref](int x, int y, int z) { return im_ref(x, y, z); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    printf("Running vectorized dense load with stride minus one test\n");
    if (vectorized_dense_load_with_stride_minus_one_test() != 0) {
        return -1;
    }

    printf("Running multiple vectorized predicate test\n");
    if (multiple_vectorized_predicate_test() != 0) {
        return -1;
    }

    printf("Running vectorized predicated store scalarized predicated load test\n");
    if (vectorized_predicated_store_scalarized_predicated_load_test() != 0) {
        return -1;
    }

    printf("Running scalar load test\n");
    if (scalar_load_test() != 0) {
        return -1;
    }

    printf("Running scalar store test\n");
    if (scalar_store_test() != 0) {
        return -1;
    }

    printf("Running not dependent on vectorized var test\n");
    if (not_dependent_on_vectorized_var_test() != 0) {
        return -1;
    }

    printf("Running no-op store test\n");
    if (no_op_store_test() != 0) {
        return -1;
    }

    printf("Running vectorized predicated with pure call test\n");
    if (vectorized_predicated_predicate_with_pure_call_test() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
