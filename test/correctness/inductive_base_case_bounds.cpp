#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Finds the allocation for the inductive Func f and records whether its
// storage extents are constant, and whether any of them depend on a loop
// variable the allocation is nested inside.
class InspectFAllocation : public IRVisitor {
    using IRVisitor::visit;

    std::vector<std::string> enclosing_loops;

    void visit(const For *op) override {
        enclosing_loops.push_back(op->name);
        IRVisitor::visit(op);
        enclosing_loops.pop_back();
    }

    void visit(const Allocate *op) override {
        if (op->name == "f" || starts_with(op->name, "f$") || starts_with(op->name, "f.")) {
            found = true;
            all_extents_const = true;
            depends_on_enclosing_loop = false;
            for (const Expr &e : op->extents) {
                if (!is_const(e)) {
                    all_extents_const = false;
                }
                for (const std::string &v : enclosing_loops) {
                    if (expr_uses_var(e, v)) {
                        depends_on_enclosing_loop = true;
                    }
                }
            }
        }
        IRVisitor::visit(op);
    }

public:
    bool found = false;
    bool all_extents_const = false;
    bool depends_on_enclosing_loop = false;
};

InspectFAllocation inspect(bool compute_root) {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y");
    // Inductive in x; the base case fires at x <= y - 100, so how far the
    // recursion reaches back in x depends on the non-inductive var y.
    f(x, y) = select(x > y - 100, f(x - 1, y) + 1, 0);
    g(x, y) = f(x, y);
    g.bound(x, 0, 200).bound(y, 0, 200);
    if (compute_root) {
        f.compute_root();
    } else {
        f.compute_at(g, y);
    }
    Module m = g.compile_to_module({}, compute_root ? "g_root" : "g_at");
    InspectFAllocation v;
    for (const auto &fn : m.functions()) {
        fn.body.accept(&v);
    }
    return v;
}

}  // namespace

int main(int argc, char **argv) {
    // compute_at(g, y): f is realized inside the y loop, so the amount of f
    // materialized depends on y - the base-case reach x = y - 100 appears in
    // its storage extents.
    InspectFAllocation at = inspect(false);
    if (!at.found) {
        printf("Failed to find an allocation for f in the compute_at schedule\n");
        return 1;
    }
    if (at.all_extents_const || !at.depends_on_enclosing_loop) {
        printf("Expected the amount of f materialized to depend on y under "
               "compute_at(g, y), but its allocation extents were "
               "loop-invariant\n");
        return 1;
    }

    // compute_root: f is realized once over the whole domain, so the base-case
    // reach is bounded over all of y and its storage extents are constant.
    InspectFAllocation root = inspect(true);
    if (!root.found) {
        printf("Failed to find an allocation for f in the compute_root schedule\n");
        return 1;
    }
    if (!root.all_extents_const || root.depends_on_enclosing_loop) {
        printf("Expected the amount of f materialized to be independent of y "
               "under compute_root, but its allocation extents still depended "
               "on a loop variable\n");
        return 1;
    }

    // The result is the same either way: f(x, y) counts up from the base case
    // at x = y - 100, i.e. max(0, x - y + 100).
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y");
    f(x, y) = select(x > y - 100, f(x - 1, y) + 1, 0);
    g(x, y) = f(x, y);
    g.bound(x, 0, 200).bound(y, 0, 200);
    f.compute_at(g, y);
    Buffer<int> out = g.realize({200, 200});
    for (int yy = 0; yy < 200; yy++) {
        for (int xx = 0; xx < 200; xx++) {
            int expected = std::max(0, xx - yy + 100);
            if (out(xx, yy) != expected) {
                printf("out(%d, %d) = %d instead of %d\n", xx, yy, out(xx, yy), expected);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
