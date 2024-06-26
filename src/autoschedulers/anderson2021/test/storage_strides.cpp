#include "LoopNest.h"
#include "test.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::Autoscheduler;

void test_bounds() {
    Target target("host-cuda");
    Anderson2021Params params;
    bool verbose = false;
    int bytes_per_point = 4;

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
        EXPECT_EQ(thread_bounds_g->region_required(1).extent(), 1);
        EXPECT_EQ(thread_bounds_g->region_required(2).extent(), 1);

        EXPECT_EQ(thread_bounds_f->region_required(0).extent(), 3);
        EXPECT_EQ(thread_bounds_f->region_required(1).extent(), 3);
        EXPECT_EQ(thread_bounds_f->region_required(2).extent(), 3);
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

    // Whole number stride
    {
        Func f("f"), g("g"), out("out");
        f(x) = x;
        out(x) = f(x);

        out.set_estimate(x, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(out.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_out = &dag.nodes[0];
        const FunctionDAG::Node *node_f = &dag.nodes[1];

        EXPECT_EQ(node_out->func.name(), out.name());
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

        const auto &root_bounds_f = root->get_bounds(node_f);

        EXPECT_EQ(root_bounds_f->region_required(0).extent(), 1024);
        EXPECT_EQ(1, (int)node_f->outgoing_edges.size());
        EXPECT_EQ(1, (int)node_f->outgoing_edges.front()->load_jacobians.size());

        ThreadInfo thread_info{0, {32}, node_out->stages[0].loop, {32}};
        const auto &jac = node_f->outgoing_edges.front()->load_jacobians.front();

        const auto &thread = root->children[0]->children[0];
        Strides strides = thread->compute_strides(jac, 0, node_f, root_bounds_f, &thread_info, verbose);

        GlobalAccessAccumulator accumulator{bytes_per_point, 1, strides, verbose};
        thread_info.for_each_thread_id_in_first_warp(accumulator);

        GlobalMemInfo mem_info;
        int num_requests = 1;
        accumulator.add_access_info(
            num_requests,
            mem_info,
            false);

        EXPECT_EQ(4, mem_info.num_transactions());
    }

    // Fractional stride
    {
        Func f("f"), g("g"), out("out");
        f(x) = x;
        out(x) = f(x / 2);

        out.set_estimate(x, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(out.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_out = &dag.nodes[0];
        const FunctionDAG::Node *node_f = &dag.nodes[1];

        EXPECT_EQ(node_out->func.name(), out.name());
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

        const auto &root_bounds_f = root->get_bounds(node_f);

        EXPECT_EQ(root_bounds_f->region_required(0).extent(), 512);
        EXPECT_EQ(1, (int)node_f->outgoing_edges.size());
        EXPECT_EQ(1, (int)node_f->outgoing_edges.front()->load_jacobians.size());

        ThreadInfo thread_info{0, {32}, node_out->stages[0].loop, {32}};
        const auto &jac = node_f->outgoing_edges.front()->load_jacobians.front();

        const auto &thread = root->children[0]->children[0];
        Strides strides = thread->compute_strides(jac, 0, node_f, root_bounds_f, &thread_info, verbose);

        GlobalAccessAccumulator accumulator{bytes_per_point, 1, strides, verbose};
        thread_info.for_each_thread_id_in_first_warp(accumulator);

        GlobalMemInfo mem_info;
        int num_requests = 1;
        accumulator.add_access_info(
            num_requests,
            mem_info,
            false);

        EXPECT_EQ(2, mem_info.num_transactions());
    }

    // Fractional stride with multiple dimensions
    {
        Func f("f"), g("g"), out("out");
        f(x, y) = x + y;
        out(x, y) = f(x, y / 2);

        out.set_estimate(x, 0, 1024);
        out.set_estimate(y, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(out.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_out = &dag.nodes[0];
        const FunctionDAG::Node *node_f = &dag.nodes[1];

        EXPECT_EQ(node_out->func.name(), out.name());
        EXPECT_EQ(node_f->func.name(), f.name());

        std::unique_ptr<LoopNest> root = std::make_unique<LoopNest>();

        // Compute out at root
        root->compute_here(node_out, true, 0, false, params, target);

        // Tile out
        std::vector<int64_t> tiling;
        tiling.push_back(1);
        tiling.push_back(1);
        // Serial loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);
        tiling.clear();
        tiling.push_back(1);
        tiling.push_back(32);
        // Thread loop
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);

        std::unique_ptr<LoopNest> root_copy{new LoopNest};
        root_copy->copy_from(*root);

        const auto &root_bounds_f = root->get_bounds(node_f);

        EXPECT_EQ(root_bounds_f->region_required(0).extent(), 1024);
        EXPECT_EQ(root_bounds_f->region_required(1).extent(), 512);

        EXPECT_EQ(1, (int)node_f->outgoing_edges.size());
        EXPECT_EQ(1, (int)node_f->outgoing_edges.front()->load_jacobians.size());

        ThreadInfo thread_info{1, {1, 32}, node_out->stages[0].loop, {1, 32}};
        const auto &jac = node_f->outgoing_edges.front()->load_jacobians.front();

        const auto &thread = root->children[0]->children[0];
        Strides strides = thread->compute_strides(jac, 0, node_f, root_bounds_f, &thread_info, verbose);
        strides.dump(true);

        GlobalAccessAccumulator accumulator{bytes_per_point, 1, strides, verbose};
        thread_info.for_each_thread_id_in_first_warp(accumulator);

        GlobalMemInfo mem_info;
        int num_requests = 1;
        accumulator.add_access_info(
            num_requests,
            mem_info,
            false);

        EXPECT_EQ(16, mem_info.num_transactions());
    }

    // Fused stage without thread dimension
    {
        Func f("f"), g("g"), out("out");
        g(y) = y;
        f(y) = g(y);
        out(x, y) = f(y);

        out.set_estimate(x, 0, 1024);
        out.set_estimate(y, 0, 1024);

        std::vector<Function> outputs;
        outputs.push_back(out.function());
        FunctionDAG dag(outputs, target);

        const FunctionDAG::Node *node_out = &dag.nodes[0];
        const FunctionDAG::Node *node_f = &dag.nodes[1];
        const FunctionDAG::Node *node_g = &dag.nodes[2];

        EXPECT_EQ(node_out->func.name(), out.name());
        EXPECT_EQ(node_f->func.name(), f.name());
        EXPECT_EQ(node_g->func.name(), g.name());

        std::unique_ptr<LoopNest> root = std::make_unique<LoopNest>();

        // Compute out at root
        root->compute_here(node_out, true, 0, false, params, target);

        // Tile out
        std::vector<int64_t> tiling;
        tiling.push_back(1);
        tiling.push_back(1);
        // Serial loop
        auto thread_loop = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);
        std::unique_ptr<LoopNest> thread_loop_copy{new LoopNest};
        thread_loop_copy->copy_from(*thread_loop);
        thread_loop_copy->compute_here(node_f, true, 0, false, params, target);
        tiling.clear();
        tiling.push_back(32);
        tiling.push_back(1);
        // Thread loop
        root->children[0] = thread_loop_copy.release();
        root->children[0] = root->children[0]->parallelize_in_tiles(tiling, root.get(), params, target, true, false);

        std::unique_ptr<LoopNest> root_copy{new LoopNest};
        root_copy->copy_from(*root);

        const auto &root_bounds_f = root->get_bounds(node_f);

        EXPECT_EQ(root_bounds_f->region_required(0).extent(), 1024);

        EXPECT_EQ(1, (int)node_g->outgoing_edges.size());
        EXPECT_EQ(1, (int)node_g->outgoing_edges.front()->load_jacobians.size());

        ThreadInfo thread_info{1, {32, 1}, node_out->stages[0].loop, {32, 1}};
        const auto &jac = node_g->outgoing_edges.front()->load_jacobians.front();

        const auto &thread = root->children[0]->children[0];
        const auto &thread_bounds_g = thread->get_bounds(node_g);
        Strides strides = thread->compute_strides(jac, 0, node_g, thread_bounds_g, &thread_info, verbose);

        GlobalAccessAccumulator accumulator{bytes_per_point, 1, strides, verbose};
        thread_info.for_each_thread_id_in_first_warp(accumulator);

        GlobalMemInfo mem_info;
        int num_requests = 1;
        accumulator.add_access_info(
            num_requests,
            mem_info,
            false);

        EXPECT_EQ(4, mem_info.num_transactions());
    }

    // Whole number stride with multiple dimensions
    {
        std::vector<int64_t> storage_strides;
        storage_strides.push_back(1);
        storage_strides.push_back(64);

        Strides strides{storage_strides};

        strides.add_valid({1, 0});

        EXPECT_EQ(strides.offset(0, 0), 0);
        EXPECT_EQ(strides.offset(0, 1), 1);
    }

    // Fractional stride with multiple dimensions
    {
        std::vector<int64_t> storage_strides;
        storage_strides.push_back(1);
        storage_strides.push_back(64);

        Strides strides{storage_strides};

        strides.add_valid({0, 0.5});

        EXPECT_EQ(strides.offset(0, 0), 0);
        EXPECT_EQ(strides.offset(0, 1), 0);
        EXPECT_EQ(strides.offset(0, 2), 64);
        EXPECT_EQ(strides.offset(0, 3), 64);
    }

    // More complex fractional stride with multiple dimensions
    {
        std::vector<int64_t> storage_strides;
        storage_strides.push_back(1);
        storage_strides.push_back(321);
        storage_strides.push_back(61953);

        Strides strides{storage_strides};

        strides.add_valid({0, 0.5, 0});
        strides.add_valid({4, 0, 0});
        strides.add_valid({0, 0, 2});

        auto x0 = strides.offset(0, 0);
        auto x1 = strides.offset(0, 1);
        auto x2 = strides.offset(0, 2);
        auto x3 = strides.offset(0, 3);
        EXPECT_EQ(x0, 0);
        EXPECT_EQ(x1, 0);
        EXPECT_EQ(x2, 321);
        EXPECT_EQ(x3, 321);

        auto y0 = strides.offset(1, 0);
        auto y1 = strides.offset(1, 1);
        auto y2 = strides.offset(1, 2);
        auto y3 = strides.offset(1, 3);
        EXPECT_EQ(y0, 0);
        EXPECT_EQ(y1, 4);
        EXPECT_EQ(y2, 8);
        EXPECT_EQ(y3, 12);

        EXPECT_EQ(x0 + y0, 0);
        EXPECT_EQ(x1 + y0, 0);
        EXPECT_EQ(x0 + y1, 4);
        EXPECT_EQ(x1 + y1, 4);
    }
}

int main(int argc, char **argv) {
    test_bounds();
    printf("All tests passed.\n");
    return 0;
}
