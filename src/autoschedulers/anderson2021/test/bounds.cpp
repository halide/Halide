#include "LoopNest.h"
#include "Tiling.h"
#include "test.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::Autoscheduler;

void test_bounds() {
    Target target("host-cuda");
    Anderson2021Params params;

    Var x("x"), y("y");
    {
        Func f("f"), g("g"), h("h");
        f(x) = (x) * (x);
        g(x) = f(x - 1) + f(x) + f(x + 1);
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

        // Tile h
        std::vector<int64_t> tiling;
        tiling.push_back(1);
        // Serial loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);
        tiling.back() = 32;
        // Thread loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);

        const auto &thread = root->children[0]->children[0];
        const auto &thread_bounds_g = thread->get_bounds(node_g);
        const auto &thread_bounds_f = thread->get_bounds(node_f);

        EXPECT_EQ(thread_bounds_g->region_required(0).extent(), 1);

        EXPECT_EQ(thread_bounds_f->region_required(0).extent(), 3);
    }

    {
        Func f("f2"), g("g2"), h("h2"), out("out");
        g(x) = x;
        f(x) = g(2 * x);
        h(x) = g(x);
        out(x) = h(x) + f(x);

        out.set_estimate(x, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(out.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_out = &dag.nodes[0];
        const FunctionDAG::Node *node_f = &dag.nodes[2];
        const FunctionDAG::Node *node_g = &dag.nodes[3];

        std::unique_ptr<LoopNest> root = std::make_unique<LoopNest>();

        // Compute h at root
        root->compute_here(node_out, true, 0, false, params, target);

        // Tile h
        std::vector<int64_t> tiling;
        tiling.push_back(2);
        // Serial loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);
        tiling.back() = 32;
        // Thread loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);

        const auto &thread = root->children[0]->children[0];
        const auto &thread_bounds_g = thread->get_bounds(node_g);
        const auto &thread_bounds_f = thread->get_bounds(node_f);

        EXPECT_EQ(thread_bounds_g->region_required(0).extent(), 515);

        EXPECT_EQ(thread_bounds_f->region_required(0).extent(), 2);
    }

    // This is a sequence of tests for edge cases of region_required.
    // region_required is defined as the region of a producer required to
    // satisfy ALL of its consumers (not a single consumer). This can lead to
    // surprising results if used unknowingly e.g. to compute the number of
    // bytes required of a producer to satisfy a single consumer.
    {
        Func f("f"), g("g"), h("h"), out("out");
        g(x) = x;
        h(x) = g(x - 1) + g(x) + g(x + 1);
        out(x) = h(x);

        out.set_estimate(x, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(out.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_out = &dag.nodes[0];
        const FunctionDAG::Node *node_h = &dag.nodes[1];
        const FunctionDAG::Node *node_g = &dag.nodes[2];

        EXPECT_EQ(node_out->func.name(), out.name());
        EXPECT_EQ(node_h->func.name(), h.name());
        EXPECT_EQ(node_g->func.name(), g.name());

        std::unique_ptr<LoopNest> root = std::make_unique<LoopNest>();

        // Compute out at root
        root->compute_here(node_out, true, 0, false, params, target);

        // Tile out
        std::vector<int64_t> tiling;
        tiling.push_back(1);
        // Serial loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);
        tiling.back() = 32;
        // Thread loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);

        std::unique_ptr<LoopNest> root_copy{new LoopNest};
        root_copy->copy_from(*root);

        const auto &thread = root->children[0]->children[0];
        const auto &thread_bounds_g = thread->get_bounds(node_g);
        const auto &thread_bounds_h = thread->get_bounds(node_h);

        EXPECT_EQ(thread_bounds_g->region_required(0).extent(), 3);

        EXPECT_EQ(thread_bounds_h->region_required(0).extent(), 1);

        // If 'h' is inlined, the region_required should not change
        root_copy->inline_func(node_h);
        {
            const auto &thread = root_copy->children[0]->children[0];
            const auto &thread_bounds_g = thread->get_bounds(node_g);
            const auto &thread_bounds_h = thread->get_bounds(node_h);

            EXPECT_EQ(thread_bounds_g->region_required(0).extent(), 3);

            EXPECT_EQ(thread_bounds_h->region_required(0).extent(), 1);
        }
    }

    {
        Func f("f"), g("g"), out("out");
        g(x) = x;
        f(x) = g(x - 100) + g(x + 100);  // 201 points of g required for each point of f
        out(x) = f(x) + g(x);            // 1 point of g required for each point of out

        out.set_estimate(x, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(out.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_out = &dag.nodes[0];
        const FunctionDAG::Node *node_f = &dag.nodes[1];
        const FunctionDAG::Node *node_g = &dag.nodes[2];

        EXPECT_EQ(node_out->func.name(), out.name());
        EXPECT_EQ(node_g->func.name(), g.name());
        EXPECT_EQ(node_f->func.name(), f.name());

        std::unique_ptr<LoopNest> root = std::make_unique<LoopNest>();

        // Compute out at root
        root->compute_here(node_out, true, 0, false, params, target);

        // Tile out
        std::vector<int64_t> tiling;
        tiling.push_back(1);
        // Serial loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);
        tiling.back() = 32;
        // Thread loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);

        std::unique_ptr<LoopNest> root_copy{new LoopNest};
        root_copy->copy_from(*root);

        const auto &thread = root->children[0]->children[0];
        const auto &thread_bounds_g = thread->get_bounds(node_g);
        const auto &thread_bounds_f = thread->get_bounds(node_f);
        const auto &thread_bounds_out = thread->get_bounds(node_out);

        EXPECT_EQ(thread_bounds_g->region_required(0).extent(), 201);
        EXPECT_EQ(thread_bounds_g->loops(0, 0).extent(), 201);

        EXPECT_EQ(thread_bounds_out->loops(0, 0).extent(), 1);

        EXPECT_EQ(thread_bounds_f->region_required(0).extent(), 1);

        vector<const FunctionDAG::Edge *> out_g_edge_chain;
        for (const auto *e : node_g->outgoing_edges) {
            if (e->consumer != thread->stage) {
                continue;
            }

            out_g_edge_chain.push_back(e);
        }

        EXPECT_EQ((int)out_g_edge_chain.size(), 1);

        vector<const FunctionDAG::Edge *> out_f_g_edge_chain;
        for (const auto *e : node_f->outgoing_edges) {
            if (e->consumer != thread->stage) {
                continue;
            }

            out_f_g_edge_chain.push_back(e);
        }

        out_f_g_edge_chain.push_back(node_f->stages[0].incoming_edges.front());
        EXPECT_EQ((int)out_f_g_edge_chain.size(), 2);

        const auto &thread_bounds_g_edge = thread->get_bounds_along_edge_chain(node_g, out_g_edge_chain);

        // This should only account for the edge from 'g' -> 'out' (and ignore the
        // edge from 'g' -> 'f')
        EXPECT_EQ(thread_bounds_g_edge->region_required(0).extent(), 1);

        const auto &thread_bounds_f_g_edge = thread->get_bounds_along_edge_chain(node_g, out_f_g_edge_chain);

        EXPECT_EQ(thread_bounds_f_g_edge->region_required(0).extent(), 201);

        // If 'f' is inlined, the region_required should still produce valid results
        root_copy->inline_func(node_f);
        {
            const auto &thread = root_copy->children[0]->children[0];
            const auto &thread_bounds_g = thread->get_bounds(node_g);

            EXPECT_EQ(thread_bounds_g->region_required(0).extent(), 201);

            const auto &thread_bounds_g_edge = thread->get_bounds_along_edge_chain(node_g, out_g_edge_chain);

            EXPECT_EQ(thread_bounds_g_edge->region_required(0).extent(), 1);

            const auto &thread_bounds_f_g_edge = thread->get_bounds_along_edge_chain(node_g, out_f_g_edge_chain);

            EXPECT_EQ(thread_bounds_f_g_edge->region_required(0).extent(), 201);
        }
    }

    {
        Func f("f"), g("g"), out("out");
        g(x) = x;
        f(x) = g(x);           // 1 point of g required for each point of f
        out(x) = f(x) + g(x);  // 1 point of g required for each point of out

        out.set_estimate(x, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(out.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_out = &dag.nodes[0];
        const FunctionDAG::Node *node_f = &dag.nodes[1];
        const FunctionDAG::Node *node_g = &dag.nodes[2];

        EXPECT_EQ(node_out->func.name(), out.name());
        EXPECT_EQ(node_g->func.name(), g.name());
        EXPECT_EQ(node_f->func.name(), f.name());

        std::unique_ptr<LoopNest> root = std::make_unique<LoopNest>();

        // Compute out at root
        root->compute_here(node_out, true, 0, false, params, target);

        // Tile out
        std::vector<int64_t> tiling;
        tiling.push_back(1);
        // Serial loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);
        tiling.back() = 32;
        // Thread loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);

        std::unique_ptr<LoopNest> root_copy{new LoopNest};
        root_copy->copy_from(*root);

        const auto &thread = root->children[0]->children[0];
        const auto &thread_bounds_g = thread->get_bounds(node_g);
        const auto &thread_bounds_f = thread->get_bounds(node_f);

        EXPECT_EQ(thread_bounds_g->region_required(0).extent(), 1);
        EXPECT_EQ(thread_bounds_f->region_required(0).extent(), 1);

        root_copy->inline_func(node_f);
        {
            const auto &thread = root_copy->children[0]->children[0];
            const auto &thread_bounds_g = thread->get_bounds(node_g);

            EXPECT_EQ(thread_bounds_g->region_required(0).extent(), 1);
        }
    }
}

int main(int argc, char **argv) {
    test_bounds();
    printf("All tests passed.\n");
    return 0;
}
