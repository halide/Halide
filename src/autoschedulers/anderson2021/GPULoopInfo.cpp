#include "GPULoopInfo.h"
#include "Errors.h"
#include "LoopNest.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

void GPULoopInfo::update(const Target &target, const LoopNest *loop) {
    if (loop->is_gpu_block(target)) {
        current_block_loop = loop;
        num_blocks = loop->get_block_and_serial_extents(loop).first;
        return;
    }

    if (loop->is_gpu_thread(target)) {
        current_thread_loop = loop;
        return;
    }

    if (loop->is_gpu_serial(target) && at_or_inside_block()) {
        int64_t serial_loop_extents = 1;
        for (auto c : loop->size) {
            serial_loop_extents *= c;
        }

        if (at_or_inside_thread()) {
            total_inner_serial_extents *= serial_loop_extents;
            inner_loop_stack.push_back(loop);
        } else {
            total_outer_serial_extents *= serial_loop_extents;
        }
    }
}

int64_t GPULoopInfo::total_serial_extents() const {
    return total_outer_serial_extents * total_inner_serial_extents;
}

bool GPULoopInfo::at_or_inside_block() const {
    return current_block_loop != nullptr;
}

bool GPULoopInfo::at_or_inside_thread() const {
    return current_thread_loop != nullptr;
}

std::vector<int64_t> GPULoopInfo::get_inner_serial_loop_extents(const LoopNest *loop_nest) const {
    internal_assert(at_or_inside_thread());

    std::vector<int64_t> extents;
    std::size_t N = loop_nest->stage->loop.size();
    extents.reserve(N);

    const auto &bounds = current_thread_loop->get_bounds(loop_nest->stage->node);

    for (std::size_t i = 0; i < N; i++) {
        auto extent = bounds->loops(loop_nest->stage->index, i).extent();
        extents.push_back(extent);
    }

    return extents;
}

// If you have a realization inside a serial loop e.g.
// f 80 gpu_block
//  f 32 gpu_thread
//   f 8 gpu_serial
//    realize: g
//    g 1 gpu_serial
//     g 1 gpu_simd
//    f 1 gpu_simd
// This method will give the extents of the loops inside the thread level but
// outside the given loop_nest's realization e.g. 8 for g above.
int64_t GPULoopInfo::get_total_inner_serial_extents_outside_realization(const LoopNest *loop_nest) const {
    int64_t extents = 1;

    for (const auto *loop : inner_loop_stack) {
        if (loop->node == loop_nest->node) {
            break;
        }

        for (auto c : loop->size) {
            extents *= c;
        }
    }

    return extents;
}

const ThreadInfo *GPULoopInfo::create_thread_info() {
    internal_assert(at_or_inside_block());
    internal_assert(at_or_inside_thread());
    internal_assert(thread_info == nullptr) << "create_thread_info() should not be called twice";

    auto max_thread_counts = current_block_loop->get_union_thread_counts(nullptr);
    thread_info = std::make_shared<const ThreadInfo>(
        current_thread_loop->vectorized_loop_index,
        current_thread_loop->size,
        current_thread_loop->stage->loop,
        max_thread_counts);
    return thread_info.get();
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
