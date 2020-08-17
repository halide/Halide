#include "test.h"
#include "LoopNest.h"
#include "State.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::Autoscheduler;

void test_state() {
    MachineParams params(80, 16000000, 40);
    Target target("host-cuda");

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
        FunctionDAG dag(outputs, params, target);

        const FunctionDAG::Node* node_h = &dag.nodes[0];
        const FunctionDAG::Node* node_g = &dag.nodes[1];
        const FunctionDAG::Node* node_f = &dag.nodes[2];

        EXPECT_EQ(node_h->func.name(), std::string("h"));
        EXPECT_EQ(node_f->func.name(), std::string("f"));
        EXPECT_EQ(node_g->func.name(), std::string("g"));

        std::unique_ptr<LoopNest> root = make_unique<LoopNest>();

        // Compute h at root
        root->compute_here(node_h, true, 0, false, target);

        std::unique_ptr<State> state = make_unique<State>();
        state->root = root.release();
        state->update_always_consider_inline_options(node_g);
        EXPECT(state->should_always_consider_inline(node_g));
    }

    //{
        //Func f("f"), g("g"), h("h");
        //f(x) = x * x;
        //g(x) = f(x);
        //h(x) = g(x) + f(x);

        //h.set_estimate(x, 0, 1024);

        //std::vector<Function> outputs;
        //outputs.push_back(h.function());
        //FunctionDAG dag(outputs, params, target);

        //const FunctionDAG::Node* node_h = &dag.nodes[0];
        //const FunctionDAG::Node* node_g = &dag.nodes[1];
        //const FunctionDAG::Node* node_f = &dag.nodes[2];

        ////EXPECT_EQ(node_h->func.name(), std::string("h"));
        ////EXPECT_EQ(node_f->func.name(), std::string("f"));
        ////EXPECT_EQ(node_g->func.name(), std::string("g"));

        //std::unique_ptr<LoopNest> root = make_unique<LoopNest>();

        //// Compute h at root
        //root->compute_here(node_h, true, 0, false, target);

        //std::unique_ptr<State> state = make_unique<State>();
        //state->root = root.release();
        //state->update_always_consider_inline_options(node_f);
        //EXPECT(!state->should_always_consider_inline(node_f));

        //state->update_always_consider_inline_options(node_g);
        //EXPECT(state->should_always_consider_inline(node_g));

        //root = make_unique<LoopNest>();
        //root->compute_here(node_h, true, 0, false, target);
        //root->inline_func(node_g);
        //state->root = root.release();
        //state->update_always_consider_inline_options(node_f);
        //EXPECT(state->should_always_consider_inline(node_f));
    //}

    {
        Func f("f"), g("g"), h("h");
        f(x) = x * x;
        g(x) = f(x);
        h(x) = g(x) + f(x);

        h.set_estimate(x, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(h.function());
        FunctionDAG dag(outputs, params, target);

        const FunctionDAG::Node* node_h = &dag.nodes[0];
        const FunctionDAG::Node* node_g = &dag.nodes[1];
        //const FunctionDAG::Node* node_f = &dag.nodes[2];

        //EXPECT_EQ(node_h->func.name(), std::string("h"));
        //EXPECT_EQ(node_f->func.name(), std::string("f"));
        //EXPECT_EQ(node_g->func.name(), std::string("g"));

        std::unique_ptr<LoopNest> root = make_unique<LoopNest>();

        // Compute h at root
        root->compute_here(node_h, true, 0, false, target);

        map<const LoopNest *, pair<const LoopNest *, int>> parent;

        // Tile h
        std::vector<int64_t> tiling;
        tiling.push_back(1);
        // Serial loop
        root->children[0] = root->children[0]->parallelize_in_tiles(params, tiling, root.get(), target, true, false);
        tiling.back() = 32;
        // Thread loop
        root->children[0] = root->children[0]->parallelize_in_tiles(params, tiling, root.get(), target, true, false);

        const LoopNest* root_ptr = root.get();
        const LoopNest* innermost = root->children[0]->children[0]->children[0]->children[0].get();
        const LoopNest* serial = root->children[0]->children[0]->children[0].get();
        const LoopNest* thread = root->children[0]->children[0].get();
        const LoopNest* block = root->children[0].get();

        parent.insert({innermost, {serial, 3}});
        parent.insert({serial, {thread, 2}});
        parent.insert({thread, {block, 2}});
        parent.insert({block, {root_ptr, 1}});

        root->dump();

        std::unique_ptr<State> state = make_unique<State>();

        EXPECT_EQ(thread, state->deepest_valid_compute_location(parent, *node_g, innermost, root_ptr));
        EXPECT_EQ(thread, state->deepest_valid_compute_location(parent, *node_g, serial, root_ptr));
        EXPECT_EQ(thread, state->deepest_valid_compute_location(parent, *node_g, thread, root_ptr));
        EXPECT_EQ(block, state->deepest_valid_compute_location(parent, *node_g, block, root_ptr));
        EXPECT_EQ(root_ptr, state->deepest_valid_compute_location(parent, *node_g, root_ptr, root_ptr));
    }
}

int main(int argc, char **argv) {
    test_state();
    printf("All tests passed.\n");
    return 0;
}
