#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {
// Make sure that "f" is computed at "g"
struct CheckCompute final : IRVisitor {
    std::string producer;
    std::string consumer;
    bool f_computed_at_g = true;

    using IRVisitor::visit;
    void visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            if (op->name == "f") {
                if (producer != "g") {
                    f_computed_at_g = false;
                }
            }
            std::string old_producer = producer;
            producer = op->name;
            IRVisitor::visit(op);
            producer = old_producer;
        } else {
            if (op->name == "f") {
                if (producer != "g") {
                    f_computed_at_g = false;
                }
            }
            std::string old_consumer = consumer;
            consumer = op->name;
            IRVisitor::visit(op);
            consumer = old_consumer;
        }
    }
};

int allocation_bound_test_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    return 0;
}
}  // namespace

TEST(SetCustomTraceTest, SetCustomTrace) {
    // Setting custom trace on "f" should not have nuked its compute_at schedule.

    Var x("x");
    Func f("f"), g("g");

    f(x) = x;
    g(x) += f(x);

    f.compute_at(g, x);
    f.jit_handlers().custom_trace = allocation_bound_test_trace;

    Module m = g.compile_to_module({g.infer_arguments()});
    CheckCompute checker;
    m.functions().front().body.accept(&checker);

    EXPECT_TRUE(checker.f_computed_at_g) << "Produce/consume 'f' should be inside of produce 'g'";
}
