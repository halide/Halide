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

namespace Halide {
namespace Internal {
namespace Autoscheduler {

#define MAX_THREADS_PER_BLOCK 1024

struct ThreadInfo {
    ThreadInfo(const std::vector<int64_t>& max_thread_counts) {
        init_threads_in_this_block(max_thread_counts);
    }

    ThreadInfo(int vectorized_loop_index, const std::vector<int64_t>& size, const std::vector<int64_t>& max_thread_counts) {
        init_threads_in_this_block(max_thread_counts);

        int num_thread_loops = 0;

        if (vectorized_loop_index != -1 && size[vectorized_loop_index] != 1) {
            threads[num_thread_loops] = size[vectorized_loop_index];
            num_threads *= size[vectorized_loop_index];
            num_thread_loops = 1;
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
        }

        internal_assert(num_threads <= num_threads_in_this_block);
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
    void for_each_active_thread_id(const Fn& fn) const {
        for_each_thread_id([&](int thread_id, bool is_active, bool is_last_thread) {
            if (!is_active) {
                return;
            }

            fn(thread_id, is_last_thread);
        });
    }

    double warp_lane_utilization_at_block_x() const {
        return warp_lane_utilization_at_block(0);
    }

    double warp_lane_utilization_at_block_y() const {
        return warp_lane_utilization_at_block(1);
    }

    double warp_lane_utilization_at_block_z() const {
        return warp_lane_utilization_at_block(2);
    }

    double warp_lane_utilization_at_block(std::size_t i) const {
        return (double)threads[i] / (double)threads_in_this_block[i];
    }

    double total_warp_lane_utilization_at_block() const {
        return (double)num_threads / (double)num_threads_in_this_block;
    }

    double warp_lane_utilization() const {
        return (double)num_threads / (double)(num_warps_per_block * 32);
    }

    double block_occupancy() const {
        return (double)num_threads / MAX_THREADS_PER_BLOCK;
    }

    int num_warps_per_block = 0;
    int num_active_warps_per_block = 0;

    int threads_in_this_block[3] = {1, 1, 1};
    int64_t num_threads_in_this_block = 1;

    int threads[3] = {1, 1, 1};
    int64_t num_threads = 1;

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

        for_each_thread_id([&](int thread_id, bool is_active, bool is_last_thread) {
            current_warp_is_active |= is_active;

            if ((thread_id + 1) % 32 == 0 || is_last_thread) {
                if (current_warp_is_active) {
                    ++num_active_warps_per_block;
                }
                current_warp_is_active = false;
            }
        });
    }
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // THREAD_INFO_H
