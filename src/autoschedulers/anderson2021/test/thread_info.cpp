#include "LoopNest.h"
#include "ThreadInfo.h"
#include "test.h"
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::Autoscheduler;

void test_thread_info() {
    Target target("host-cuda");

    Var x("x"), y("y");
    {
        int vectorized_loop_index = 0;
        std::vector<int64_t> size;
        std::vector<FunctionDAG::Node::Loop> loop;
        std::vector<int64_t> loop_extents;
        std::vector<int64_t> max_thread_counts;

        loop.emplace_back();
        loop.emplace_back();

        // 16x8
        size.push_back(16);
        size.push_back(8);

        // 16x8
        max_thread_counts.push_back(16);
        max_thread_counts.push_back(8);

        {
            ThreadInfo info{vectorized_loop_index, size, loop, max_thread_counts};

            EXPECT_EQ(128, info.num_threads);
            EXPECT_EQ(1.0, info.warp_lane_utilization());
        }

        // Smaller stage: its 'size' is smaller than its loop_extents,
        // indicating that it has been split; it could achieve better
        // utilization if it had not been split
        size.clear();
        size.push_back(8);
        size.push_back(8);

        {
            ThreadInfo info{vectorized_loop_index, size, loop, max_thread_counts};
            EXPECT_EQ(64, info.num_threads);
            EXPECT_EQ(0.5, info.warp_lane_utilization());
        }

        // Smaller stage: its loop is smaller than the max thread loop and
        // cannot possibly achieve better utilization
        {
            ThreadInfo info{vectorized_loop_index, size, loop, max_thread_counts};
            EXPECT_EQ(64, info.num_threads);
            EXPECT_EQ(0.5, info.warp_lane_utilization());
        }

        size.clear();
        size.push_back(11);
        size.push_back(11);
        size.push_back(2);
        max_thread_counts.clear();
        max_thread_counts.push_back(16);
        max_thread_counts.push_back(16);
        max_thread_counts.push_back(2);
        loop.emplace_back();

        {
            ThreadInfo info{vectorized_loop_index, size, loop, max_thread_counts};
            EXPECT_EQ(242, info.num_threads);
            APPROX_EQ(0.630208, info.warp_lane_utilization(), 0.00001);
        }
    }
}

int main(int argc, char **argv) {
    test_thread_info();
    printf("All tests passed.\n");
    return 0;
}
