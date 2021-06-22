#ifndef HANNK_MEMORY_PLANNER_H
#define HANNK_MEMORY_PLANNER_H

#include <vector>

namespace hannk {

// AllocationPlanner is used to plan a series of allocations in which we can
// overlap blocks that don't have any lifespan in common. At present,
// the implementation is incredibly simpleminded (it never overlaps anything)
// but will be replaced by something smarter soon (likely a basic greedy allocator).
class AllocationPlanner {
public:
    // All blocks allocated will be aligned to (at least) this amount.
    explicit AllocationPlanner(size_t alignment);

    // Specify a block's size and lifetime. The order this is called implicitly
    // assigns an index to the result, so the block information that's passed into the nth call of
    // this method will be used as the block_index argument to get_block_offset().
    void add_block(size_t size, int first_use, int last_use);

    // How many blocks have been added to the planner.
    size_t block_count() const;

    // Commit all the blocks added and compute a layout. It is an error to
    // call add_block() after this.
    void commit();

    // The largest contiguous block of memory that's needed to hold the layout.
    // It is an error to call this before commit().
    size_t memory_needed() const;

    // Calculated layout offset for the nth block added to the planner.
    // It is an error to call this before commit().
    size_t get_block_offset(size_t block_index) const;

    // Movable but not copyable.
    AllocationPlanner() = delete;
    AllocationPlanner(const AllocationPlanner &) = delete;
    AllocationPlanner &operator=(const AllocationPlanner &) = delete;
    AllocationPlanner(AllocationPlanner &&) = default;
    AllocationPlanner &operator=(AllocationPlanner &&) = default;

private:
    std::vector<size_t> block_offsets_;
    size_t next_free_offset_ = 0;
    size_t alignment_ = 1;
    bool committed_ = false;
};

}  // namespace hannk

#endif  // HANNK_MEMORY_PLANNER_H
