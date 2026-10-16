#include "Halide.h"
#include <map>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

// Specializations may split and rfactor the same RDom differently, using the
// same user-facing RVar names. Each definition must retain its own bounds.

namespace {

enum class Placement {
    Root,
    Row,
    Reduction,
};

class CheckAllocations : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Allocate *op) override {
        auto it = expected.find(op->name);
        if (it != expected.end()) {
            if (op->constant_allocation_size() != it->second) {
                printf("Expected constant allocation of %d elements for %s, got %d\n",
                       it->second, op->name.c_str(), op->constant_allocation_size());
                failed = true;
            }
            expected.erase(it);
        }
        IRVisitor::visit(op);
    }

public:
    std::map<std::string, int> expected;
    bool failed = false;
};

// dot(x, y) = sum_r a(r, x) * a(r, y), with `factors[i]` the split factor of
// specialization i (on extent(y) >= threshold[i]) and the last factor the
// default definition's.
int check(const std::vector<int> &factors, const std::vector<int> &thresholds, bool nested, Placement placement) {
    const int K = 16, N = 8;
    Buffer<int> a(K, N);
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < K; x++) {
            a(x, y) = x + 3 * y + 1;
        }
    }

    Var x("x"), y("y"), u("u");
    RDom r(0, K, "r");
    Func dot("dot");
    dot(x, y) = 0;
    dot(x, y) += a(r, x) * a(r, y);

    Expr M = dot.output_buffer().dim(1).extent();
    Stage s = dot.update();
    std::vector<Stage> definitions;
    Stage parent = s;
    for (int threshold : thresholds) {
        Stage d = parent.specialize(M >= threshold);
        definitions.push_back(d);
        if (nested) {
            parent = d;
        }
    }
    definitions.push_back(s);

    CheckAllocations checker;
    for (size_t i = 0; i < factors.size(); i++) {
        Stage d = definitions[i];
        RVar ro("ro"), ri("ri");
        d.split(r, ro, ri, factors[i], TailStrategy::GuardWithIf);
        Func intm = d.rfactor(ri, u);
        if (d.get_schedule().rvars().size() != 1 ||
            d.get_schedule().rvars().front().var != ri.name()) {
            printf("Expected rfactor to preserve the RVar name %s\n", ri.name().c_str());
            return 1;
        }
        // Projected RVars remain schedulable with the original handles.
        d.reorder(ri, x, y);
        intm.update().reorder(ro, x, y, u);
        if (placement == Placement::Root) {
            intm.compute_root();
        } else if (placement == Placement::Row) {
            intm.compute_at(dot, y);
            checker.expected[intm.name()] = N * factors[i];
        } else {
            intm.compute_at(dot, ri);
            checker.expected[intm.name()] = 1;
        }
    }
    dot.bound(x, 0, N);

    Module module = dot.compile_to_module({});
    module.functions().front().body.accept(&checker);
    if (checker.failed || !checker.expected.empty()) {
        printf("Expected a constant-sized allocation for every rfactor intermediate\n");
        return 1;
    }

    for (int m : {2, 4, 6, 8}) {
        Buffer<int> out = dot.realize({N, m});
        for (int j = 0; j < m; j++) {
            for (int i = 0; i < N; i++) {
                int correct = 0;
                for (int k = 0; k < K; k++) {
                    correct += a(k, i) * a(k, j);
                }
                if (out(i, j) != correct) {
                    printf("factors %d...: M = %d: out(%d, %d) = %d instead of %d\n",
                           factors[0], m, i, j, out(i, j), correct);
                    return 1;
                }
            }
        }
    }
    return 0;
}

int check_inherited_domain() {
    Buffer<int> input(21);
    for (int i = 0; i < input.width(); i++) {
        input(i) = i * i + 1;
    }

    Param<int> mode;
    Var u("u");
    RDom r(5, 16, "r");
    Func sum("sum");
    sum() = 0;
    sum() += input(r.x);
    Func first = sum.update().rfactor(r.x, u);
    first.compute_root();

    // The specialization inherits the nonzero-min domain. Refactoring the
    // default reuses its name for a domain with different min and extent.
    sum.update().specialize(mode == 1);
    Func scalar = sum.update().specialize(mode == 2).rfactor({});
    scalar.compute_root();
    RVar ro("ro"), ri("ri");
    sum.update().split(r.x, ro, ri, 4).rename(ri, r.x);
    Func second = sum.update().rfactor(r.x, u);
    second.compute_root();

    int expected = 0;
    for (int i = 5; i < 21; i++) {
        expected += input(i);
    }
    for (int value : {0, 1, 2}) {
        mode.set(value);
        Buffer<int> result = sum.realize();
        if (result() != expected) {
            printf("Inherited domain: mode = %d: got %d instead of %d\n",
                   value, result(), expected);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    // A specialization's preserved RVar wider, then narrower, than the
    // default's, and sibling and nested specializations with different bounds.
    for (Placement placement : {Placement::Root, Placement::Row, Placement::Reduction}) {
        if (check({4, 2}, {8}, false, placement) ||
            check({2, 4}, {8}, false, placement) ||
            check({8, 4, 2}, {8, 4}, false, placement) ||
            check({4, 8, 2}, {4, 8}, true, placement)) {
            return 1;
        }
    }
    if (check_inherited_domain()) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
