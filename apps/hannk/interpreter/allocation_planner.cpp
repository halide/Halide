#include "interpreter/allocation_planner.h"

#include <cassert>

namespace hannk {

namespace {
size_t align_up(size_t p, size_t alignment) {
    return (p + alignment - 1) & ~(alignment - 1);
}
}  // namespace

AllocationPlanner::AllocationPlanner(size_t alignment)
    : alignment_(alignment) {
}

int AllocationPlanner::add_block(size_t size, int first_use, int last_use) {
    assert(!committed_);
    assert(next_free_offset_ == align_up(next_free_offset_, alignment_));
    int index = (int)block_offsets_.size();
    block_offsets_.push_back(next_free_offset_);
    next_free_offset_ += align_up(size, alignment_);
    return index;
}

int AllocationPlanner::block_count() const {
    return (int)block_offsets_.size();
}

void AllocationPlanner::commit() {
    assert(!committed_);
    committed_ = true;
}

size_t AllocationPlanner::memory_needed() const {
    assert(committed_);
    return next_free_offset_;
}

size_t AllocationPlanner::get_block_offset(int block_index) const {
    assert(committed_);
    assert(block_index >= 0 && block_index < (int)block_offsets_.size());
    return block_offsets_.at(block_index);
}

}  // namespace hannk
