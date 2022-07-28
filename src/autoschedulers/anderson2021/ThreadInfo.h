#ifndef THREAD_INFO_H
#define THREAD_INFO_H

/** \file
 *
 * Data structure containing information about GPU threads for a particular
 * location in the loop nest and its surrounding block. Useful when computing
 * GPU features
 */

#include <vector>

#include "Errors.h"
#include "FunctionDAG.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

#define MAX_THREADS_PER_BLOCK 1024

struct LoopNest;

// Sort / filter thread tile options
struct ThreadTileOption {
    IntrusivePtr<const LoopNest> loop_nest;
    double max_idle_lane_wastage;
    bool operator<(const ThreadTileOption &other) const {
        return max_idle_lane_wastage < other.max_idle_lane_wastage;
    }

    // Ensure we don't accidentally copy this type
    ThreadTileOption() = default;
    ThreadTileOption(ThreadTileOption &&) = default;
    ThreadTileOption &operator=(ThreadTileOption &&) = default;
    ThreadTileOption(const ThreadTileOption &) = delete;
    ThreadTileOption &operator=(const ThreadTileOption &) = delete;
};

struct ThreadInfo {
    ThreadInfo(int vectorized_loop_index, const std::vector<int64_t>& size, const std::vector<FunctionDAG::Node::Loop>& loop, const std::vector<int64_t>& max_thread_counts) {
        init_threads_in_this_block(max_thread_counts);

        std::size_t num_thread_loops = 0;

        if (vectorized_loop_index != -1 && size[vectorized_loop_index] != 1) {
            threads[num_thread_loops] = size[vectorized_loop_index];
            num_threads *= size[vectorized_loop_index];
            num_thread_loops = 1;
            loop_indices.push_back(vectorized_loop_index);
            loop_vars.push_back(loop[vectorized_loop_index].var);
        }

        for (std::size_t i = 0; i < size.size() && num_thread_loops < 3; i++) {
            if (size[i] == 1 || (int)i == vectorized_loop_index) {
                continue;
            }

            if (num_threads * size[i] > MAX_THREADS_PER_BLOCK) {
                break;
            }

            threads[num_thread_loops] = size[i];
            num_threads *= size[i];
            ++num_thread_loops;
            loop_indices.push_back(i);
            loop_vars.push_back(loop[i].var);
        }

        if (loop_indices.size() == 0) {
            internal_assert(size.size() > 0);
            ++num_thread_loops;
            loop_indices.push_back(0);
            loop_vars.push_back(loop[0].var);
        }

        internal_assert(num_threads <= num_threads_in_this_block);
        internal_assert(loop_indices.size() == num_thread_loops);
        internal_assert(loop_vars.size() == num_thread_loops);
        internal_assert(loop_indices.size() > 0 && loop_indices.size() <= 3);
        internal_assert(loop_vars.size() > 0 && loop_vars.size() <= 3);

        count_num_active_warps_per_block();
    }

    template <typename Fn>
    void for_each_thread_id(const Fn& fn) const {
        int thread_id = 0;
        for (int z = 0; z < threads_in_this_block[2]; z++) {
            for (int y = 0; y < threads_in_this_block[1]; y++) {
                for (int x = 0; x < threads_in_this_block[0]; x++) {
                    // Skip any threads in this loop nest with extent less than the
                    // extents of the largest thread loops in this block
                    // for thread.x in [0, 10]:
                    //   ...
                    // for thread.x in [0, 5]:
                    //   ...
                    // For the 2nd loop, skip threads with x id >= 5
                    bool active = x < threads[0]
                        && y < threads[1]
                        && z < threads[2];

                    fn(thread_id, active, thread_id == num_threads_in_this_block - 1);
                    ++thread_id;
                }
            }
        }
    }

    template <typename Fn>
    void for_each_thread_id_in_first_warp(Fn& fn) const {
        int thread_id = 0;
        for (int z = 0; z < threads_in_this_block[2]; z++) {
            for (int y = 0; y < threads_in_this_block[1]; y++) {
                for (int x = 0; x < threads_in_this_block[0]; x++) {
                    // Skip any threads in this loop nest with extent less than the
                    // extents of the largest thread loops in this block
                    // for thread.x in [0, 10]:
                    //   ...
                    // for thread.x in [0, 5]:
                    //   ...
                    // For the 2nd loop, skip threads with x id >= 5
                    bool active = x < threads[0]
                        && y < threads[1]
                        && z < threads[2];

                    bool last_thread = thread_id == 31;
                    fn(thread_id, x, y, z, active, last_thread);
                    ++thread_id;

                    if (last_thread) {
                        return;
                    }
                }
            }
        }
    }

    template <typename Fn>
    void for_each_thread_id_in_tail_warp(Fn& fn) const {
        int thread_id = final_warp_initial_thread_id;
        int last_thread_id = thread_id + num_threads_in_final_warp - 1;

        for (; thread_id <= last_thread_id; ++thread_id) {
            int z = thread_id / (threads_in_this_block[1] * threads_in_this_block[0]);
            int y = (thread_id - z * threads_in_this_block[1] * threads_in_this_block[0]) / threads_in_this_block[0];
            int x = thread_id % threads_in_this_block[0];

            internal_assert(z < threads_in_this_block[2]);
            internal_assert(y < threads_in_this_block[1]);
            internal_assert(x < threads_in_this_block[0]);

            bool active = x < threads[0]
                && y < threads[1]
                && z < threads[2];

            fn(thread_id, x, y, z, active, thread_id == last_thread_id);
        }
    }

    template <typename Fn>
    void for_each_active_thread_id(const Fn& fn) const {
        for_each_thread_id([&](int thread_id, bool is_active, bool is_last_thread) {
            if (!is_active) {
                return;
            }

            fn(thread_id, is_last_thread);
        });
    }

    double warp_lane_utilization() const {
        return (double)num_active_threads / (double)(num_active_warps_per_block * 32);
    }

    double idle_lane_wastage() const {
        return ((double)(num_active_warps_per_block * 32) - (double)num_active_threads) / MAX_THREADS_PER_BLOCK;
    }

    double block_occupancy() const {
        return (double)num_threads / MAX_THREADS_PER_BLOCK;
    }

    int num_warps_per_block = 0;
    int num_active_warps_per_block = 0;
    int num_regular_active_warps_per_block = 0;
    bool has_tail_warp = false;
    int final_warp_initial_thread_id = 0;
    int num_threads_in_final_warp = 0;

    int threads_in_this_block[3] = {1, 1, 1};
    int64_t num_threads_in_this_block = 1;

    int threads[3] = {1, 1, 1};
    int64_t num_threads = 1;
    int64_t num_active_threads = 0;

    std::vector<int> loop_indices;
    std::vector<std::string> loop_vars;

private:
    void init_threads_in_this_block(const std::vector<int64_t>& max_thread_counts) {
        int num_thread_loops = 0;
        for (auto c : max_thread_counts) {
            if (c == 1) {
                continue;
            }

            if (num_thread_loops >= 3 || num_threads_in_this_block * c > MAX_THREADS_PER_BLOCK) {
                break;
            }

            threads_in_this_block[num_thread_loops] = c;
            num_threads_in_this_block *= c;
            ++num_thread_loops;
        }

        num_warps_per_block = num_threads_in_this_block / 32;
        if (num_threads_in_this_block % 32 != 0) {
            num_warps_per_block++;
        }
    }

    void count_num_active_warps_per_block() {
        bool current_warp_is_active = false;
        int num_active_threads_in_cur_warp = 0;
        int num_active_threads_in_first_warp = 0;
        int num_threads_in_cur_warp = 0;
        bool first_warp = true;

        for_each_thread_id([&](int thread_id, bool is_active, bool is_last_thread) {
            current_warp_is_active |= is_active;

            if (is_active) {
                ++num_active_threads_in_cur_warp;
                ++num_active_threads;
            }
            ++num_threads_in_cur_warp;

            if ((thread_id + 1) % 32 == 0 || is_last_thread) {
                if (current_warp_is_active) {
                    ++num_active_warps_per_block;

                    if (first_warp) {
                        first_warp = false;
                        num_active_threads_in_first_warp = num_active_threads_in_cur_warp;
                    }

                    if (is_last_thread) {
                        num_threads_in_final_warp = num_threads_in_cur_warp;
                        has_tail_warp = num_active_threads_in_first_warp != num_active_threads_in_cur_warp;
                        final_warp_initial_thread_id = thread_id - num_threads_in_cur_warp + 1;

                        internal_assert(num_threads_in_final_warp <= 32);
                    }
                }

                current_warp_is_active = false;
                num_threads_in_cur_warp = 0;
                num_active_threads_in_cur_warp = 0;
            }
        });

        num_regular_active_warps_per_block = num_active_warps_per_block;
        if (has_tail_warp) {
            --num_regular_active_warps_per_block;
        }
    }
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // THREAD_INFO_H
