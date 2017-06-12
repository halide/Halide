#include <stdio.h>
#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// Deliberately odd number
constexpr int kExpectedVectorWidth = 17;

class CheckScheduleParams : public IRVisitor {
private:
    using IRVisitor::visit;

    std::string inside_for_loop;

    void visit(const Ramp *op) {
        IRVisitor::visit(op);
        assert(is_const(op->lanes, kExpectedVectorWidth));
    }

    void visit(const For *op) {
        if (op->name == "f.s0.x.x") {
            assert(op->for_type == ForType::Serial);
            assert(inside_for_loop == "g.s0.y");
        } else if (op->name == "f.s0.y") {
            assert(op->for_type == ForType::Serial);
            assert(inside_for_loop == "g.s0.y");
        } else if (op->name == "g.s0.x") {
            assert(op->for_type == ForType::Serial);
            assert(inside_for_loop == "g.s0.y");
        } else if (op->name == "g.s0.y") {
            assert(op->for_type == ForType::Parallel);
            assert(inside_for_loop == "");
        } else {
            assert(0);
        }

        std::string old_for_loop = inside_for_loop;
        inside_for_loop = op->name;
        IRVisitor::visit(op);
        inside_for_loop = old_for_loop;
    }

    void visit(const Store *op) {
        IRVisitor::visit(op);
        if (op->name == "f") {
            assert(inside_for_loop == "f.s0.x.x");
        } else if (op->name == "g") {
            assert(inside_for_loop == "g.s0.x");
        } else {
            assert(0);
        }
    }
};

int main(int argc, char **argv) {

    ScheduleParam<LoopLevel> compute_at;
    ScheduleParam<int> vector_width;

    compute_at.set(LoopLevel::root());  // this value will not be used
    vector_width.set(kExpectedVectorWidth - 1);  // this value will not be used

    Var x("x"), y("y"), yi("yi");
    Func f("f"), g("g");

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    f.compute_at(compute_at).vectorize(x, vector_width);
    g.parallel(y);

    // We can set the ScheduleParam values any time before lowering.

    // Copied ScheduleParams should still refer to the same underlying reference,
    // so setting the copy should be equivalent to setting the original.
    ScheduleParam<LoopLevel> compute_at_alias(compute_at);  // testing copy ctor
    ScheduleParam<int> vector_width_alias(vector_width);

    compute_at_alias.set(LoopLevel::inlined());  // this value will not be used
    vector_width_alias.set(kExpectedVectorWidth + 1);  // this value will not be used

    ScheduleParam<LoopLevel> compute_at_alias2;
    ScheduleParam<int> vector_width_alias2;
    compute_at_alias2 = compute_at_alias;  // testing operator=
    vector_width_alias2 = vector_width_alias;

    // Should be equivalent to setting the original values
    compute_at_alias2.set(LoopLevel(g, y));
    vector_width_alias2.set(kExpectedVectorWidth);

    // Lower it and inspect the IR to verify that the values we set
    // for vector width and for compute/store_at were used.
    Module m = g.compile_to_module({g.infer_arguments()});
    CheckScheduleParams c;
    m.functions().front().body.accept(&c);

    printf("Success!\n");
    return 0;
}
