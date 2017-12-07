#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class CheckLoopLevels : public IRVisitor {
public:
    CheckLoopLevels(const std::string &inner_loop_level,
                        const std::string &outer_loop_level) :
        inner_loop_level(inner_loop_level), outer_loop_level(outer_loop_level) {}

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
            _halide_user_assert(inside_for_loop == inner_loop_level);
        } else if (op->name == "cos_f32") {
            _halide_user_assert(inside_for_loop == outer_loop_level);
        }
    }

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        if (op->name.substr(0, 5) == "inner") {
            _halide_user_assert(inside_for_loop == inner_loop_level);
        } else if (op->name.substr(0, 5) == "outer") {
            _halide_user_assert(inside_for_loop == outer_loop_level);
        } else {
            _halide_user_assert(0);
        }
    }
};

Var x("x"), y("y"), c("c");

struct Test {
    Func inner, outer;
    LoopLevel inner_compute_at, inner_store_at;

    explicit Test(int i) {
        // We use specific calls as proxies for verifying that compute_at
        // happens where we expect: sin() for the inner function, cos()
        // for the outer one; these are chosen mainly because they won't
        // ever get generated incidentally by the lowering code as part of
        // general code structure.
        inner = Func("inner" + std::to_string(i));
        inner(x, y, c) = sin(cast<float>(x + y + c));

        inner.compute_at(inner_compute_at).store_at(inner_store_at);

        outer = Func("outer" + std::to_string(i));
        outer(x, y, c) = cos(cast<float>(inner(x, y, c)));
    }

    void check(const std::string &inner_loop_level,
               const std::string &outer_loop_level) {
        Buffer<float> result = outer.realize(1, 1, 1);

        Module m = outer.compile_to_module({outer.infer_arguments()});
        CheckLoopLevels c(inner_loop_level, outer_loop_level);
        m.functions().front().body.accept(&c);
    }
};

int main(int argc, char **argv) {

    // Test that LoopLevels set after being specified still take effect.
    {
        Test t(1);

        t.inner_compute_at.set(LoopLevel(t.outer, x));
        t.inner_store_at.set(LoopLevel(t.outer, x));

        t.check("outer1.s0.x", "outer1.s0.x");
    }

    // Same as before, but using inlined() for both inner LoopLevels.
    {
        Test t(2);

        t.inner_compute_at.set(LoopLevel::inlined());
        t.inner_store_at.set(LoopLevel::inlined());

        t.check("outer2.s0.x", "outer2.s0.x");
    }

    // Same as before, but using root() for both inner LoopLevels.
    {
        Test t(3);

        t.inner_compute_at.set(LoopLevel::root());
        t.inner_store_at.set(LoopLevel::root());

        t.check("inner3.s0.x", "outer3.s0.x");
    }

    // Same as before, but using different store_at and compute_at()
    {
        Test t(4);

        t.inner_compute_at.set(LoopLevel(t.outer, y));
        t.inner_store_at.set(LoopLevel::root());

        t.check("inner4.s0.x", "outer4.s0.x");
    }

    // Same as before, but using inlined for store_at() [equivalent to omitting
    // the store_at() call entirely] and non-inlined for compute_at
    {
        Test t(5);

        t.inner_compute_at.set(LoopLevel(t.outer, y));
        t.inner_store_at.set(LoopLevel::inlined());

        t.check("inner5.s0.x", "outer5.s0.x");
    }

    printf("Success!\n");
    return 0;
}
