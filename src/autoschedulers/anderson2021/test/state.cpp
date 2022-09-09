#include "State.h"
#include "LoopNest.h"
#include "test.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::Autoscheduler;

void test_state() {
    Target target("host-cuda");
    Anderson2021Params params;

    // Test update_always_consider_inline_options
    Var x("x"), y("y");
    {
        Func f("f"), g("g"), h("h");
        f(x) = x * x;
        g(x) = f(x);
        h(x) = g(x);

        h.set_estimate(x, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(h.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_h = &dag.nodes[0];
        const FunctionDAG::Node *node_g = &dag.nodes[1];
        const FunctionDAG::Node *node_f = &dag.nodes[2];

        EXPECT_EQ(node_h->func.name(), std::string("h"));
        EXPECT_EQ(node_f->func.name(), std::string("f"));
        EXPECT_EQ(node_g->func.name(), std::string("g"));

        std::unique_ptr<LoopNest> root = std::make_unique<LoopNest>();

        // Compute h at root
        root->compute_here(node_h, true, 0, false, params, target);

        std::unique_ptr<State> state = std::make_unique<State>();
        state->root = root.release();
        state->update_always_consider_inline_options(node_g);
        EXPECT(state->should_always_consider_inline(node_g));
    }
}

int main(int argc, char **argv) {
    test_state();
    printf("All tests passed.\n");
    return 0;
}
