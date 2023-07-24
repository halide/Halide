#ifndef GPU_LOOP_INFO_H
#define GPU_LOOP_INFO_H

/** \file
 *
 * Data structure containing information about the current GPU loop nest
 * hierarchy of blocks, threads, etc. Useful when computing GPU features
 */

#include <vector>

#include "Halide.h"
#include "ThreadInfo.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct LoopNest;

struct GPULoopInfo {
    explicit GPULoopInfo(const LoopNest *root)
        : root{root} {
    }

    const LoopNest *root = nullptr;
    const LoopNest *current_block_loop = nullptr;
    const LoopNest *current_thread_loop = nullptr;
    std::vector<const LoopNest *> inner_loop_stack;
    int64_t num_blocks = 1;
    int64_t total_outer_serial_extents = 1;
    int64_t total_inner_serial_extents = 1;
    const ThreadInfo *thread_info = nullptr;

    void update(const Target &target, const LoopNest *loop);

    int64_t total_serial_extents() const;

    bool at_or_inside_block() const;

    bool at_or_inside_thread() const;

    std::vector<int64_t> get_inner_serial_loop_extents(const LoopNest *loop_nest) const;

    std::unique_ptr<ThreadInfo> create_thread_info();

    int64_t get_total_inner_serial_extents_outside_realization(const LoopNest *loop_nest) const;
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // GPU_LOOP_INFO_H
