#ifndef HANNK_MEMORY_PLANNER_H
#define HANNK_MEMORY_PLANNER_H

#include <vector>

namespace hannk {

// AllocationPlanner is used to plan a series of allocations in which we can
// overlap blocks that don't have any lifespan in common.
class AllocationPlanner {
public:
    // All blocks allocated will be aligned to (at least) this amount.
    explicit AllocationPlanner(size_t alignment);

    // Specify a block's size and lifetime. Return an id for the block, which will later
    // be used to retrieve the final layout info via get_block_offset(). Note that -- by design! --
    // the same offset may be returned for multiple blocks.
    int add_block(size_t size, int first_use, int last_use);

    // How many blocks have been added to the planner.
    int block_count() const;

    // Commit all the blocks added and compute a layout. It is an error to
    // call add_block() after this.
    void commit();

    // The largest contiguous block of memory that's needed to hold the layout.
    // It is an error to call this before commit().
    size_t memory_needed() const;

    // Calculated layout offset for the nth block added to the planner.
    // It is an error to call this before commit().
    size_t get_block_offset(int block_id) const;

    // Dump details about the allocation to the given stream, along
    // with an ASCII usage map.
    void dump(std::ostream &o);

    // Movable but not copyable.
    AllocationPlanner() = delete;
    AllocationPlanner(const AllocationPlanner &) = delete;
    AllocationPlanner &operator=(const AllocationPlanner &) = delete;
    AllocationPlanner(AllocationPlanner &&) = default;
    AllocationPlanner &operator=(AllocationPlanner &&) = default;

private:
    size_t alignment_ = 1;

    struct BlockRequirements {
        size_t calculated_offset;
        size_t size_needed;
        int first_use;
        int last_use;
    };
    std::vector<BlockRequirements> block_requirements_;

    bool committed_ = false;

    void check_overlap();
};

}  // namespace hannk

#endif  // HANNK_MEMORY_PLANNER_H
